// %sys-gc.h


//=//// FLEX MANAGED MEMORY ///////////////////////////////////////////////=//
//
// If BASE_FLAG_MANAGED is not explicitly passed to Make_Flex(), a
// Flex will be manually memory-managed by default.  Hence you don't need
// to worry about the Flex being freed out from under you while building it.
// Manual Flexes are tracked, and automatically freed in the case of a panic().
//
// All manual Flexes *must* either be freed with Free_Unmanaged_Flex() or
// delegated to the GC with Manage_Stub() before the Level ends.  Once a
// Flex is managed, only the GC is allowed to free it.
//
// Manage_Stub() is shallow--it only sets a bit on that *one* Flex, not
// any Flexes referenced by values resident in it.  Hence many routines that
// build hierarchical structures (like the scanner) only return managed
// results, since they can manage it as they build them.

INLINE void Untrack_Manual_Stub(const Stub* s)
{
    Stub* * const last_ptr
        = &cast(Stub**, g_gc.manuals->content.dynamic.data)[
            g_gc.manuals->content.dynamic.used - 1
        ];

    assert(g_gc.manuals->content.dynamic.used >= 1);
    if (*last_ptr != s) {
        //
        // If the Stub is not the last manually added Stub, then find where it
        // is, then move the last manually added Stub to that position to
        // preserve it when we chop off the tail (instead of keeping the Stub
        // we want to free).
        //
        Stub* *current_ptr = last_ptr - 1;
        for (; *current_ptr != s; --current_ptr) {
          #if RUNTIME_CHECKS
            if (
                current_ptr
                <= cast(Stub**, g_gc.manuals->content.dynamic.data)
            ){
                printf("Stub not in list of last manually added Stubs\n");
                crash (s);
            }
          #endif
        }
        *current_ptr = *last_ptr;
    }

    // !!! Should g_gc.manuals ever shrink or save memory?
    //
    --g_gc.manuals->content.dynamic.used;
}

INLINE void Manage_Stub(const Stub* s)  // give manual Stub to GC
{
    assert(not Is_Base_Managed(s));
    Untrack_Manual_Stub(s);
    Set_Base_Managed_Bit(s);
}

INLINE void Force_Stub_Managed(const Stub* s) {
    if (Not_Base_Managed(s)) {
        Untrack_Manual_Stub(s);
        Set_Base_Managed_Bit(s);
    }
}

#if NO_RUNTIME_CHECKS
    #define Assert_Stub_Managed(f) NOOP
#else
    INLINE void Assert_Stub_Managed(const Stub* f) {
        if (Not_Base_Managed(f))
            crash (f);
    }
#endif




//=////////////////////////////////////////////////////////////////////////=//
//
//  GUARDING FLEXES FROM GARBAGE COLLECTION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The garbage collector can run anytime the trampoline runs (and possibly
// at certain other times).  So if a Flex has BASE_FLAG_MANAGED on it, the
// potential exists that any C pointers that are outstanding may "go bad"
// if the Flex wasn't reachable from the root set.  This is important to
// remember any time a pointer is held across a call that runs arbitrary
// user code.
//
// This simple stack approach allows pushing protection for a Flex, and
// then can release protection only for the last Flex pushed.  A parallel
// pair of macros exists for pushing and popping of guard status for Cells,
// to protect any Flexes referred to by the Cells's contents.  (Note: This can
// only be used on values that aren't resident in Arrays, because there is
// no way to guarantee a Cell in an array will keep its address besides
// guarding the array AND locking it from resizing.)
//
// The guard stack is not meant to accumulate, and must be cleared out
// before a level returns to the trampoline.
//

// Push_Lifeguard() is not INLINE

INLINE void Drop_Lifeguard(const void* p) {  // p may be erased cell (not Base)
  #if NO_RUNTIME_CHECKS
    UNUSED(p);
  #else
    if (p != *Flex_Last(const void*, g_gc.guarded)) {
        printf("Drop_Lifeguard() pointer that wasn't last Push_Lifeguard()\n");
        crash (p);
    }
  #endif

    g_gc.guarded->content.dynamic.used -= 1;
}


//=//// NOTE WHEN CELL KEEPS A GC LIVE REFERENCE //////////////////////////=//
//
// If a cell is under the natural control of the GC (e.g. a Level's OUT or
// SPARE, or a frame variable) then that cell can often give GC protection
// for "free", instead of using Push_Lifeguard() to keep something alive.
//
// It's helpful for the checked build to give some enforcement that you don't
// accidentally overwrite these lifetime-holding references, so the PROTECT
// bit can come in handy (if you're using a DEBUG_CELL_READ_WRITE build,
// because it checks for all writes to cells carrying the bit.)
//
// 1. You don't always have to call Forget_Cell_Was_Lifeguard(), e.g. if
//    it's a frame cell for a native then there's no harm to letting the
//    cell stay protected as long as the frame is alive.  Anywhere that you
//    can't leave a protection bit on--such as a frame's OUT cell--will need
//    to have the protection removed.
//
#if DEBUG_CELL_READ_WRITE
    INLINE void Remember_Cell_Is_Lifeguard(Cell* c) {
        assert(Not_Cell_Flag((c), PROTECTED));
        Set_Cell_Flag((c), PROTECTED);
    }

    INLINE void Forget_Cell_Was_Lifeguard(Cell* c) {  // unpaired calls ok [1]
        assert(Get_Cell_Flag((c), PROTECTED));
        Clear_Cell_Flag((c), PROTECTED);
    }
#else
    #define Remember_Cell_Is_Lifeguard(c)  USED(c)
    #define Forget_Cell_Was_Lifeguard(c)   USED(c)
#endif


//=//// FLEX DECAY ////////////////////////////////////////////////////////=//

#define GC_Kill_Flex(f) \
    GC_Kill_Stub(Diminish_Stub(f))
