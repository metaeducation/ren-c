//
//  File: %sys-node.h
//  Summary: {Convenience routines for the Node "superclass" structure}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// are available when %sys-rebnod.h is being processed.  (e.g. Value(*),
// Series(*), Level(*)...)
//
// See %sys-rebnod.h for what a "Node" means in this context.
//


#ifdef HEAVY_NODE_BYTE_CHECK
    //
    // Just checking that the NODE_FLAG_NODE bit is set is quite costly to
    // be doing on *every* NODE_BYTE() operation.  But, sometimes it comes in
    // handy when tracing down misunderstandings.  A build that enables this
    // should be run every so often.

    inline static Byte& NODE_BYTE(Node* node) {
        assert(cast(Byte*, node)[0] & NODE_BYTEMASK_0x80_NODE);
        return cast(Byte*, node)[0];
    }

    inline static const Byte& NODE_BYTE(const Node* node) {
        assert(cast(const Byte*, node)[0] & NODE_BYTEMASK_0x80_NODE);
        return cast(const Byte*, node)[0];
    }
#else
    // Fast version.  Losing const information for fetching NODE_BYTE() isn't
    // much of an issue.  GC can fiddle with the marked flag even on const
    // series, for example.  If you're changing something from a Cell to a
    // Stub--or otherwise--you have much bigger concerns regarding safety and
    // unsafety than const.  (HEAVY_NODE_BYTE_CHECK checks const also)

    #define NODE_BYTE(p) \
        *x_cast(Byte*, ensure(const Node*, (p)))
#endif

#define FLAG_NODE_BYTE(byte)    FLAG_FIRST_BYTE(byte)

#define Is_Node(p) \
    (cast(const Byte*, (p))[0] & NODE_BYTEMASK_0x80_NODE)

#define Is_Node_A_Cell(n)   (did (NODE_BYTE(n) & NODE_BYTEMASK_0x01_CELL))
#define Is_Node_A_Stub(n)   (not Is_Node_A_Cell(n))

#define Is_Node_Marked(n)   (did (NODE_BYTE(n) & NODE_BYTEMASK_0x10_MARKED))
#define Not_Node_Marked(n)  (not Is_Node_Marked(n))

#define Is_Node_Managed(n)  (did (NODE_BYTE(n) & NODE_BYTEMASK_0x20_MANAGED))
#define Not_Node_Managed(n) (not Is_Node_Managed(n))

#define Is_Node_Free(n)     (did (NODE_BYTE(n) & NODE_BYTEMASK_0x40_FREE))
#define Not_Node_Free(n)    (not Is_Node_Free(n))

// Is_Node_Root() sounds like it might be the only node.
// Is_Node_A_Root() sounds like a third category vs Is_Node_A_Cell()/Stub()
//
#define Is_Node_Root_Bit_Set(n) \
    (did (NODE_BYTE(n) & NODE_BYTEMASK_0x02_ROOT))

#define Not_Node_Root_Bit_Set(n) \
    (not (NODE_BYTE(n) & NODE_BYTEMASK_0x02_ROOT))

// Add "_Bit" suffix to reinforce lack of higher level function.  (A macro
// with the name Set_Node_Managed() might sound like it does more, like
// removing from the manuals list the way Managed_Series() etc. do)

#define Set_Node_Root_Bit(n) \
    NODE_BYTE(n) |= NODE_BYTEMASK_0x02_ROOT

#define Clear_Node_Root_Bit(n) \
    NODE_BYTE(n) &= (~ NODE_BYTEMASK_0x02_ROOT)

#define Set_Node_Marked_Bit(n) \
    NODE_BYTE(n) |= NODE_BYTEMASK_0x10_MARKED

#define Clear_Node_Marked_Bit(n) \
    NODE_BYTE(n) &= (~ NODE_BYTEMASK_0x10_MARKED)

#define Set_Node_Managed_Bit(n) \
    NODE_BYTE(n) |= NODE_BYTEMASK_0x20_MANAGED

#define Clear_Node_Managed_Bit(n) \
    NODE_BYTE(n) &= (~ NODE_BYTEMASK_0x20_MANAGED)

#define Set_Node_Free_Bit(n) \
    NODE_BYTE(n) |= NODE_BYTEMASK_0x40_FREE

#define Clear_Node_Free_Bit(n) \
    NODE_BYTE(n) &= (~ NODE_BYTEMASK_0x40_FREE)


//=//// POINTER DETECTION (UTF-8, SERIES, FREED SERIES, END) //////////////=//
//
// Ren-C's "nodes" (Cell and Stub derivatives) all have a platform-pointer
// sized header of bits, which is constructed using byte-order-sensitive bit
// flags (see FLAG_LEFT_BIT and related definitions for how those work).
//
// The values for the bits were chosen carefully, so that the leading byte of
// Cell and Stub could be distinguished from the leading byte of a UTF-8
// string, as well as from each other.  This is taken advantage of in the API.
//
// During startup, Assert_Pointer_Detection_Working() checks invariants that
// make this routine able to work.
//

enum PointerDetectEnum {
    DETECTED_AS_UTF8 = 0,

    DETECTED_AS_END = 1,  // may be in a cell, or a rebEND signal (char* align)
    DETECTED_AS_SERIES = 2,
    DETECTED_AS_CELL = 3
};

typedef enum PointerDetectEnum PointerDetect;

inline static PointerDetect Detect_Rebol_Pointer(const void *p)
{
    const Byte* bp = cast(const Byte*, p);

    if (*bp == END_SIGNAL_BYTE) {  // reserved illegal UTF-8 byte 192
        assert(bp[1] == '\0');  // rebEND C string "\xC0", terminates with '\0'
        return DETECTED_AS_END;
    }

    if (
        (*bp & (NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x40_FREE))
        == NODE_BYTEMASK_0x80_NODE  // e.g. leading bit pattern is 10xxxxxx
    ){
        // In UTF-8 these are all continuation bytes, so not a legal way to
        // start a string.  We leverage that to distinguish cells and series.
        //
        if (*bp & NODE_BYTEMASK_0x01_CELL)
            return DETECTED_AS_CELL;

        // Clients of this function should not be passing in series in mid-GC.
        // (PROBE uses it, so that throws a wrench into this check.  Review.)
        //
        /*assert(not (*bp & NODE_BYTEMASK_0x10_MARKED));*/

        return DETECTED_AS_SERIES;
    }

    // Note: technically there are some internal states that overlap with UTF-8
    // range, e.g. when a cell is marked "stale" in the output location of
    // a level.  Such states are not supposed to be leaked to where clients of
    // this routine would be concerned about them.
    //
    return DETECTED_AS_UTF8;
}


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
inline static void *Try_Alloc_Pooled(PoolId pool_id)
{
    Pool* pool = &g_mem.pools[pool_id];
    if (not pool->first) {  // pool has run out of nodes
        if (not Try_Fill_Pool(pool))  // attempt to refill it
            return nullptr;
    }

  #if !defined(NDEBUG)
    if (g_mem.fuzz_factor != 0) {
        if (g_mem.fuzz_factor < 0) {
            ++g_mem.fuzz_factor;
            if (g_mem.fuzz_factor == 0)
                return nullptr;
        }
        else if ((TG_tick % 10000) <= cast(REBLEN, g_mem.fuzz_factor)) {
            g_mem.fuzz_factor = 0;
            return nullptr;
        }
    }
  #endif

    assert(pool->first);

    PoolUnit* unit = pool->first;

    pool->first = unit->next_if_free;
    if (unit == pool->last)
        pool->last = nullptr;

    pool->free--;

  #if DEBUG_MEMORY_ALIGN
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

    // It's up to the client to update the bytes of the returned unit so that
    // it doesn't appear free (which it may not care about, if it's storing
    // arbitrary bytes...but if storing `Node`s then they should initialize
    // to not have NODE_FLAG_FREE set.)
    //
    assert(cast(Byte*, unit)[0] == FREE_POOLUNIT_BYTE);
    return cast(void*, unit);
}


inline static void *Alloc_Pooled(PoolId pool_id) {
    void *node = Try_Alloc_Pooled(pool_id);
    if (node)
        return node;

    Pool* pool = &g_mem.pools[pool_id];
    fail (Error_No_Memory(pool->wide * pool->num_units_per_segment));
}

#define Alloc_Stub() ( \
    (g_gc.depletion -= sizeof(Stub)) <= 0 ? SET_SIGNAL(SIG_RECYCLE) : NOOP, \
    Alloc_Pooled(STUB_POOL))  // won't pass SER() yet, don't cast it


// Free a node, returning it to its pool.  Once it is freed, its header will
// have NODE_FLAG_FREE...which will identify the node as not in use to anyone
// who enumerates the nodes in the pool (such as the garbage collector).
//
inline static void Free_Pooled(PoolId pool_id, void* p)
{
  #if DEBUG_MONITOR_SERIES
    if (p == g_mem.monitor_node) {
        printf(
            "Freeing series %p on tick #%d\n", p,
            cast(int, TG_tick)
        );
        fflush(stdout);
    }
  #endif

    PoolUnit* unit = cast(PoolUnit*, p);

    FIRST_BYTE(unit->headspot.bits) = FREE_POOLUNIT_BYTE;

    Pool* pool = &g_mem.pools[pool_id];

  #ifdef NDEBUG
    unit->next_if_free = pool->first;
    pool->first = unit;
  #else
    // !!! In R3-Alpha, the most recently freed node would become the first
    // node to hand out.  This is a simple and likely good strategy for
    // cache usage, but makes the "poisoning" nearly useless.
    //
    // This code was added to insert an empty segment, such that this node
    // won't be picked by the next Alloc_Pooled.  That enlongates the poisonous
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

#if CPLUSPLUS_11
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
