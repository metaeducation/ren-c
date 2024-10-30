// %sys-gc.h


//=//// FLEX MANAGED MEMORY ///////////////////////////////////////////////=//
//
// If NODE_FLAG_MANAGED is not explicitly passed to Make_Flex(), a
// Flex will be manually memory-managed by default.  Hence you don't need
// to worry about the Flex being freed out from under you while building it.
// Manual Flexes are tracked, and automatically freed in the case of a fail().
//
// All manual Flexes *must* either be freed with Free_Unmanaged_Flex() or
// delegated to the GC with Manage_Flex() before the Level ends.  Once a
// Flex is managed, only the GC is allowed to free it.
//
// Manage_Flex() is shallow--it only sets a bit on that *one* Flex, not
// any Flexes referenced by values resident in it.  Hence many routines that
// build hierarchical structures (like the scanner) only return managed
// results, since they can manage it as they build them.

INLINE void Untrack_Manual_Flex(Flex* f)
{
    Flex* * const last_ptr
        = &cast(Flex**, g_gc.manuals->content.dynamic.data)[
            g_gc.manuals->content.dynamic.used - 1
        ];

    assert(g_gc.manuals->content.dynamic.used >= 1);
    if (*last_ptr != f) {
        //
        // If the Flex is not the last manually added Flex, then find where it
        // is, then move the last manually added Flex to that position to
        // preserve it when we chop off the tail (instead of keeping the Flex
        // we want to free).
        //
        Flex* *current_ptr = last_ptr - 1;
        for (; *current_ptr != f; --current_ptr) {
          #if !defined(NDEBUG)
            if (
                current_ptr
                <= cast(Flex**, g_gc.manuals->content.dynamic.data)
            ){
                printf("Flex not in list of last manually added Flexes\n");
                panic (f);
            }
          #endif
        }
        *current_ptr = *last_ptr;
    }

    // !!! Should g_gc.manuals ever shrink or save memory?
    //
    --g_gc.manuals->content.dynamic.used;
}

INLINE Flex* Manage_Flex(Flex* f)  // give manual Flex to GC
{
  #if !defined(NDEBUG)
    if (Is_Node_Managed(f))
        panic (f);  // shouldn't manage an already managed Flex
  #endif

    Untrack_Manual_Flex(f);
    Set_Node_Managed_Bit(f);
    return f;
}

#ifdef NDEBUG
    #define Assert_Flex_Managed(f) NOOP
#else
    INLINE void Assert_Flex_Managed(const Flex* f) {
        if (Not_Node_Managed(f))
            panic (f);
    }
#endif

INLINE Flex* Force_Flex_Managed(const_if_c Flex* f) {
    if (Not_Node_Managed(f))
        Manage_Flex(m_cast(Flex*, f));
    return m_cast(Flex*, f);
}

#if (! CPLUSPLUS_11)
    #define Force_Flex_Managed_Core Force_Flex_Managed
#else
    INLINE Flex* Force_Flex_Managed_Core(Flex* f)
      { return Force_Flex_Managed(f); }  // mutable Flex may be unmanaged

    INLINE Flex* Force_Flex_Managed_Core(const Flex* f) {
        Assert_Flex_Managed(f);  // const Flex should already be managed
        return m_cast(Flex*, f);
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  GUARDING FLEXES FROM GARBAGE COLLECTION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The garbage collector can run anytime the trampoline runs (and possibly
// at certain other times).  So if a Flex has NODE_FLAG_MANAGED on it, the
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

#define Push_GC_Guard(node) \
    Push_Guard_Node(node)

INLINE void Drop_GC_Guard(const void* p) {  // p may be erased cell (not Node)
  #if defined(NDEBUG)
    UNUSED(p);
  #else
    if (p != *Flex_Last(const void*, g_gc.guarded)) {
        printf("Drop_GC_Guard() pointer that wasn't last Push_GC_Guard()\n");
        panic (p);
    }
  #endif

    g_gc.guarded->content.dynamic.used -= 1;
}


//=//// NOTE WHEN CELL KEEPS A GC LIVE REFERENCE //////////////////////////=//
//
// If a cell is under the natural control of the GC (e.g. a Level's OUT or
// SPARE, or a frame variable) then that cell can often be used instead of
// Push_GC_Guard() to keep something alive.  It's helpful to have some
// enforcement that you don't accidentally overwrite these lifetime-holding
// references, so the PROTECT bit can come in handy...if you're using a
// build that enforces it on arbitrary mutable cells.
//

#if DEBUG_CELL_READ_WRITE
    #define Remember_Cell_Is_Lifetime_Guard(c) do { \
        STATIC_ASSERT_LVALUE(c); \
        assert(Not_Cell_Flag((c), PROTECTED)); \
        Set_Cell_Flag((c), PROTECTED); \
    } while (0)

    #define Forget_Cell_Is_Lifetime_Guard(c) do { \
        STATIC_ASSERT_LVALUE(c); \
        assert(Get_Cell_Flag((c), PROTECTED)); \
        Clear_Cell_Flag((c), PROTECTED); \
    } while (0)
#else
    #define Remember_Cell_Is_Lifetime_Guard(c)  NOOP
    #define Forget_Cell_Is_Lifetime_Guard(c)  NOOP
#endif
