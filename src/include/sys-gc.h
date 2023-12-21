// %sys-gc.h


//=//// SERIES MANAGED MEMORY /////////////////////////////////////////////=//
//
// If NODE_FLAG_MANAGED is not explicitly passed to Make_Series(), a
// series will be manually memory-managed by default.  Hence you don't need
// to worry about the series being freed out from under you while building it.
// Manual series are tracked, and automatically freed in the case of a fail().
//
// All manual series *must* either be freed with Free_Unmanaged_Series() or
// delegated to the GC with Manage_Series() before the level ends.  Once a
// series is managed, only the GC is allowed to free it.
//
// Manage_Series() is shallow--it only sets a bit on that *one* series, not
// any series referenced by values inside of it.  Hence many routines that
// build hierarchical structures (like the scanner) only return managed
// results, since they can manage it as they build them.

INLINE void Untrack_Manual_Series(Series* s)
{
    Series* * const last_ptr
        = &cast(Series**, g_gc.manuals->content.dynamic.data)[
            g_gc.manuals->content.dynamic.used - 1
        ];

    assert(g_gc.manuals->content.dynamic.used >= 1);
    if (*last_ptr != s) {
        //
        // If the series is not the last manually added series, then
        // find where it is, then move the last manually added series
        // to that position to preserve it when we chop off the tail
        // (instead of keeping the series we want to free).
        //
        Series* *current_ptr = last_ptr - 1;
        for (; *current_ptr != s; --current_ptr) {
          #if !defined(NDEBUG)
            if (
                current_ptr
                <= cast(Series**, g_gc.manuals->content.dynamic.data)
            ){
                printf("Series not in list of last manually added series\n");
                panic(s);
            }
          #endif
        }
        *current_ptr = *last_ptr;
    }

    // !!! Should g_gc.manuals ever shrink or save memory?
    //
    --g_gc.manuals->content.dynamic.used;
}

INLINE Series* Manage_Series(Series* s)  // give manual series to GC
{
  #if !defined(NDEBUG)
    if (Is_Node_Managed(s))
        panic (s);  // shouldn't manage an already managed series
  #endif

    Untrack_Manual_Series(s);
    Set_Node_Managed_Bit(s);
    return s;
}

#ifdef NDEBUG
    #define Assert_Series_Managed(s) NOOP
#else
    INLINE void Assert_Series_Managed(const Series* s) {
        if (Not_Node_Managed(s))
            panic (s);
    }
#endif

INLINE Series* Force_Series_Managed(const_if_c Series* s) {
    if (Not_Node_Managed(s))
        Manage_Series(m_cast(Series*, s));
    return m_cast(Series*, s);
}

#if (! CPLUSPLUS_11)
    #define Force_Series_Managed_Core Force_Series_Managed
#else
    INLINE Series* Force_Series_Managed_Core(Series* s)
      { return Force_Series_Managed(s); }  // mutable series may be unmanaged

    INLINE Series* Force_Series_Managed_Core(const Series* s) {
        Assert_Series_Managed(s);  // const series should already be managed
        return m_cast(Series*, s);
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  GUARDING SERIES FROM GARBAGE COLLECTION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The garbage collector can run anytime the trampoline runs (and possibly
// at certain other times).  So if a series has NODE_FLAG_MANAGED on it, the
// potential exists that any C pointers that are outstanding may "go bad"
// if the series wasn't reachable from the root set.  This is important to
// remember any time a pointer is held across a call that runs arbitrary
// user code.
//
// This simple stack approach allows pushing protection for a series, and
// then can release protection only for the last series pushed.  A parallel
// pair of macros exists for pushing and popping of guard status for values,
// to protect any series referred to by the value's contents.  (Note: This can
// only be used on values that do not live inside of series, because there is
// no way to guarantee a value in a series will keep its address besides
// guarding the series AND locking it from resizing.)
//
// The guard stack is not meant to accumulate, and must be cleared out
// before a level returns to the trampoline.
//

#if DEBUG
    #define Push_GC_Guard(node) \
        Push_Guard_Node({(node), __FILE__, __LINE__})
#else
    #define Push_GC_Guard(node) \
        Push_Guard_Node({node})
#endif

INLINE void Drop_GC_Guard(const Node* node) {
  #if defined(NDEBUG)
    UNUSED(node);
  #else
    if (node != Series_Last(NodeGuardInfo, g_gc.guarded)->node) {
        printf("Drop_GC_Guard() pointer that wasn't last Push_GC_Guard()\n");
        panic (node);
    }
  #endif

    g_gc.guarded->content.dynamic.used -= 1;
}

// Cells memset 0 for speed.  But Push_Guard_Node() expects a Node, where
// the NODE_BYTE() has NODE_FLAG_NODE set.  Use this with DECLARE_LOCAL().
//
INLINE void Push_GC_Guard_Erased_Cell(Cell* cell) {
    assert(FIRST_BYTE(cell) == 0);
    FIRST_BYTE(cell) = NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x01_CELL;

    Push_GC_Guard(cell);
}
