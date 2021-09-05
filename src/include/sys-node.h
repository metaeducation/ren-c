//
//  File: %sys-node.h
//  Summary: {Convenience routines for the Node "superclass" structure}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This provides some convenience routines that require more definitions than
// are available when %sys-rebnod.h is being processed.  (e.g. REBVAL,
// REBSER, REBFRM...)
//
// See %sys-rebnod.h for what a "node" means in this context.
//


#define NODE_BYTE(p) \
    *cast(const REBYTE*, ensure(const REBNOD*, p))

#ifdef NDEBUG
    #define IS_FREE_NODE(p) \
        (did (*cast(const REBYTE*, (p)) & NODE_BYTEMASK_0x40_FREE))
#else
    inline static bool IS_FREE_NODE(const void *p) {
        REBYTE first = *cast(const REBYTE*, p);  // NODE_BYTE asserts on free!

        if (not (first & NODE_BYTEMASK_0x40_FREE))
            return false;  // byte access defeats strict alias

        assert(first == FREED_SERIES_BYTE or first == FREED_CELL_BYTE);
        return true;
    }
#endif


//=//// MEMORY ALLOCATION AND FREEING MACROS //////////////////////////////=//
//
// Rebol's internal memory management is done based on a pooled model, which
// use Try_Alloc_Mem() and Free_Mem() instead of calling malloc directly.
// (Comments on those routines explain why this was done--even in an age of
// modern thread-safe allocators--due to Rebol's ability to exploit extra
// data in its pool block when a series grows.)
//
// Since Free_Mem() requires callers to pass in the size of the memory being
// freed, it can be tricky.  These macros are modeled after C++'s new/delete
// and new[]/delete[], and allocations take either a type or a type and a
// length.  The size calculation is done automatically, and the result is cast
// to the appropriate type.  The deallocations also take a type and do the
// calculations.
//
// In a C++11 build, an extra check is done to ensure the type you pass in a
// FREE or FREE_N lines up with the type of pointer being freed.
//

#define TRY_ALLOC(t) \
    cast(t *, Try_Alloc_Mem(sizeof(t)))

#define TRY_ALLOC_ZEROFILL(t) \
    cast(t *, memset(ALLOC(t), '\0', sizeof(t)))

#define TRY_ALLOC_N(t,n) \
    cast(t *, Try_Alloc_Mem(sizeof(t) * (n)))

#define TRY_ALLOC_N_ZEROFILL(t,n) \
    cast(t *, memset(TRY_ALLOC_N(t, (n)), '\0', sizeof(t) * (n)))

#ifdef CPLUSPLUS_11
    #define FREE(t,p) \
        do { \
            static_assert( \
                std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
                "mismatched FREE type" \
            ); \
            Free_Mem(p, sizeof(t)); \
        } while (0)

    #define FREE_N(t,n,p)   \
        do { \
            static_assert( \
                std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
                "mismatched FREE_N type" \
            ); \
            Free_Mem(p, sizeof(t) * (n)); \
        } while (0)
#else
    #define FREE(t,p) \
        Free_Mem((p), sizeof(t))

    #define FREE_N(t,n,p)   \
        Free_Mem((p), sizeof(t) * (n))
#endif


#define Is_Node_Cell(n) \
    (did (NODE_BYTE(n) & NODE_BYTEMASK_0x01_CELL))


// Allocate a node from a pool.  Returned node will not be zero-filled, but
// the header will have NODE_FLAG_FREE set when it is returned (client is
// responsible for changing that if they plan to enumerate the pool and
// distinguish free nodes from non-free ones.)
//
// All nodes are 64-bit aligned.  This way, data allocated in nodes can be
// structured to know where legal 64-bit alignment points would be.  This
// is required for correct functioning of some types.  (See notes on
// alignment in %sys-rebval.h.)
//
inline static void *Try_Alloc_Node(REBLEN pool_id)
{
    REBPOL *pool = &Mem_Pools[pool_id];
    if (not pool->first) {  // pool has run out of nodes
        if (not Try_Fill_Pool(pool))  // attempt to refill it
            return nullptr;
    }

  #if !defined(NDEBUG)
    if (PG_Fuzz_Factor != 0) {
        if (PG_Fuzz_Factor < 0) {
            ++PG_Fuzz_Factor;
            if (PG_Fuzz_Factor == 0)
                return nullptr;
        }
        else if ((TG_Tick % 10000) <= cast(REBLEN, PG_Fuzz_Factor)) {
            PG_Fuzz_Factor = 0;
            return nullptr;
        }
    }
  #endif

    assert(pool->first);

    REBPLU *unit = pool->first;

    pool->first = unit->next_if_free;
    if (unit == pool->last)
        pool->last = nullptr;

    pool->free--;

  #ifdef DEBUG_MEMORY_ALIGN
    if (cast(uintptr_t, unit) % sizeof(REBI64) != 0) {
        printf(
            "Pool Unit address %p not aligned to %d bytes\n",
            cast(void*, unit),
            cast(int, sizeof(REBI64))
        );
        printf("Pool Unit address is %p and pool-first is %p\n",
            cast(void*, pool),
            cast(void*, pool->first)
        );
        panic (unit);
    }
  #endif

    assert(IS_FREE_NODE(cast(REBNOD*, unit)));  // client must make non-free
    return cast(void*, unit);
}


inline static void *Alloc_Node(REBLEN pool_id) {
    void *node = Try_Alloc_Node(pool_id);
    if (node)
        return node;

    REBPOL *pool = &Mem_Pools[pool_id];
    fail (Error_No_Memory(pool->wide * pool->num_units));
}


// Free a node, returning it to its pool.  Once it is freed, its header will
// have NODE_FLAG_FREE...which will identify the node as not in use to anyone
// who enumerates the nodes in the pool (such as the garbage collector).
//
inline static void Free_Node(REBLEN pool_id, REBNOD* node)
{
  #ifdef DEBUG_MONITOR_SERIES
    if (node == PG_Monitor_Node_Debug) {
        printf(
            "Freeing series %p on tick #%d\n",
            cast(void*, node),
            cast(int, TG_Tick)
        );
        fflush(stdout);
    }
  #endif

    REBPLU* unit = cast(REBPLU*, node);

    mutable_FIRST_BYTE(unit->headspot) = FREED_SERIES_BYTE;

    REBPOL *pool = &Mem_Pools[pool_id];

  #ifdef NDEBUG
    unit->next_if_free = pool->first;
    pool->first = unit;
  #else
    // !!! In R3-Alpha, the most recently freed node would become the first
    // node to hand out.  This is a simple and likely good strategy for
    // cache usage, but makes the "poisoning" nearly useless.
    //
    // This code was added to insert an empty segment, such that this node
    // won't be picked by the next Alloc_Node.  That enlongates the poisonous
    // time of this area to catch stale pointers.  But doing this in the
    // debug build only creates a source of variant behavior.

    bool out_of_memory = false;

    if (not pool->last) {  // Fill pool if empty
        if (not Try_Fill_Pool(pool))
            out_of_memory = true;
    }

    if (out_of_memory) {
        //
        // We don't want Free_Node to fail with an "out of memory" error, so
        // just fall back to the release build behavior in this case.
        //
        unit->next_if_free = pool->first;
        pool->first = unit;
    }
    else {
        assert(pool->last);

        pool->last->next_if_free = unit;
        pool->last = unit;
        unit->next_if_free = nullptr;
    }
  #endif

    pool->free++;
}


//=//// POINTER DETECTION (UTF-8, SERIES, FREED SERIES, END) //////////////=//
//
// Ren-C's "nodes" (REBVAL and REBSER derivatives) all have a platform-pointer
// sized header of bits, which is constructed using byte-order-sensitive bit
// flags (see FLAG_LEFT_BIT and related definitions for how those work).
//
// The values for the bits were chosen carefully, so that the leading byte of
// REBVAL and REBSER could be distinguished from the leading byte of a UTF-8
// string, as well as from each other.  This is taken advantage of in the API.
//
// During startup, Assert_Pointer_Detection_Working() checks invariants that
// make this routine able to work.
//

enum Reb_Pointer_Detect {
    DETECTED_AS_UTF8 = 0,

    DETECTED_AS_SERIES = 1,
    DETECTED_AS_FREED_SERIES = 2,

    DETECTED_AS_CELL = 3,
    DETECTED_AS_FREED_CELL = 4,

    DETECTED_AS_END = 5  // may be a cell, or a rebEND signal
};

inline static enum Reb_Pointer_Detect Detect_Rebol_Pointer(const void *p)
{
    const REBYTE* bp = cast(const REBYTE*, p);

    switch (bp[0] >> 4) {  // 4 left bits: 0xbNFGK -> Node Free manaGed marKed
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
        return DETECTED_AS_UTF8;  // ASCII codepoints 0 - 127

    // v-- bit sequences starting with `10` (continuation bytes, so not
    // valid starting points for a UTF-8 string)

      case 8:  // 0xb1000
        if (bp[1] == REB_0) {
            if (bp[0] & NODE_BYTEMASK_0x01_CELL)  // may be series info flags
                return DETECTED_AS_END;
            return DETECTED_AS_SERIES;
        }
        if (bp[0] & NODE_BYTEMASK_0x01_CELL)
            return DETECTED_AS_CELL;  // unmanaged
        return DETECTED_AS_SERIES;  // unmanaged

      case 9:  // 0xb1001 - marked bit set, should not see series in mid-GC
        assert(bp[0] & NODE_BYTEMASK_0x01_CELL);  // must be cell / end header
        if (bp[1] == REB_0)
            return DETECTED_AS_END;
        return DETECTED_AS_CELL;

      case 10:  // 0b1010
      case 11:  // 0b1011
        if (bp[1] == REB_0) {
            if (bp[0] & NODE_BYTEMASK_0x01_CELL)  // may be series info flags
                return DETECTED_AS_END;  // managed, marked if `case 11`
            return DETECTED_AS_SERIES;  // series that just has no [1] flags
        }
        return DETECTED_AS_SERIES;  // managed, marked if `case 11`

    // v-- bit sequences starting with `11` are *usually* legal multi-byte
    // valid starting points for UTF-8, with only the exceptions made for
    // the illegal 192 and 193 bytes which represent freed series and cells.

      case 12:  // 0b1100
        if (bp[0] == FREED_SERIES_BYTE)
            return DETECTED_AS_FREED_SERIES;
        if (bp[0] == FREED_CELL_BYTE)
            return DETECTED_AS_FREED_CELL;
        return DETECTED_AS_UTF8;

      case 13:  // 0b1101
      case 14:  // 0b1110
      case 15:  // 0b1111
        return DETECTED_AS_UTF8;
    }

    DEAD_END;
}
