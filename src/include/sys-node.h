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


#if !defined(DEBUG_CHECK_CASTS)

    #define NOD(p) \
        m_cast(REBNOD*, (const REBNOD*)(p))  // don't check const in C or C++

#else

    inline static REBNOD *NOD(nullptr_t p) {
        UNUSED(p);
        return nullptr;
    }

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename N = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const REBNOD,  // true branch
            REBNOD  // false branch
        >::type
    >
    inline static N *NOD(T *p) {
        constexpr bool derived =
            std::is_same<T0, REBNOD>::value
            or std::is_same<T0, REBVAL>::value
            or std::is_same<T0, REBSER>::value
            or std::is_same<T0, REBSTR>::value
            or std::is_same<T0, REBARR>::value
            or std::is_same<T0, REBCTX>::value
            or std::is_same<T0, REBACT>::value
            or std::is_same<T0, REBMAP>::value
            or std::is_same<T0, REBFRM>::value;

        constexpr bool base =
            std::is_same<T0, void>::value
            or std::is_same<T0, REBYTE>::value;

        static_assert(
            derived or base,
            "NOD() works on void/REBVAL/REBSER/REBSTR/REBARR/REBCTX/REBACT" \
               "/REBMAP/REBFRM or nullptr"
        );

        bool b = base;  // needed to avoid compiler constexpr warning
        if (b and p and (reinterpret_cast<const REBNOD*>(p)->header.bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE
        )) != (
            NODE_FLAG_NODE
        )){
            panic (p);
        }

        return reinterpret_cast<N*>(p);
    }
#endif


#if defined(NDEBUG)
    #define Is_Node_Cell(n) \
        (did (FIRST_BYTE((n)->header) & NODE_BYTEMASK_0x01_CELL))
#else
    inline static bool Is_Node_Cell(REBNOD *n) {
        REBYTE first = FIRST_BYTE(n->header);
        assert(first & NODE_BYTEMASK_0x80_NODE);
        return did (first & NODE_BYTEMASK_0x01_CELL);
    }
#endif


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
inline static void *Make_Node(REBLEN pool_id)
{
    REBPOL *pool = &Mem_Pools[pool_id];
    if (not pool->first) // pool has run out of nodes
        Fill_Pool(pool); // refill it

    assert(pool->first);

    REBNOD *node = pool->first;

    pool->first = node->next_if_free;
    if (node == pool->last)
        pool->last = nullptr;

    pool->free--;

  #ifdef DEBUG_MEMORY_ALIGN
    if (cast(uintptr_t, node) % sizeof(REBI64) != 0) {
        printf(
            "Node address %p not aligned to %d bytes\n",
            cast(void*, node),
            cast(int, sizeof(REBI64))
        );
        printf("Pool address is %p and pool-first is %p\n",
            cast(void*, pool),
            cast(void*, pool->first)
        );
        panic (node);
    }
  #endif

    assert(IS_FREE_NODE(node)); // client needs to change to non-free
    return cast(void*, node);
}


// Free a node, returning it to its pool.  Once it is freed, its header will
// have NODE_FLAG_FREE...which will identify the node as not in use to anyone
// who enumerates the nodes in the pool (such as the garbage collector).
//
inline static void Free_Node(REBLEN pool_id, REBNOD *node)
{
  #ifdef DEBUG_MONITOR_SERIES
    if (
        pool_id == SER_POOL
        and not (node->header.bits & NODE_FLAG_CELL)
        and GET_SERIES_INFO(SER(node), MONITOR_DEBUG)
    ){
        printf(
            "Freeing series %p on tick #%d\n",
            cast(void*, node),
            cast(int, TG_Tick)
        );
        fflush(stdout);
    }
  #endif

    mutable_FIRST_BYTE(node->header) = FREED_SERIES_BYTE;

    REBPOL *pool = &Mem_Pools[pool_id];

  #ifdef NDEBUG
    node->next_if_free = pool->first;
    pool->first = node;
  #else
    // !!! In R3-Alpha, the most recently freed node would become the first
    // node to hand out.  This is a simple and likely good strategy for
    // cache usage, but makes the "poisoning" nearly useless.
    //
    // This code was added to insert an empty segment, such that this node
    // won't be picked by the next Make_Node.  That enlongates the poisonous
    // time of this area to catch stale pointers.  But doing this in the
    // debug build only creates a source of variant behavior.

    if (not pool->last) // Fill pool if empty
        Fill_Pool(pool);

    assert(pool->last);

    pool->last->next_if_free = node;
    pool->last = node;
    node->next_if_free = nullptr;
  #endif

    pool->free++;
}


//=////////////////////////////////////////////////////////////////////////=//
//
// POINTER DETECTION (UTF-8, SERIES, FREED SERIES, END...)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol's "nodes" all have a platform-pointer-sized header of bits, which
// is constructed using byte-order-sensitive bit flags (see FLAG_LEFT_BIT and
// related definitions).
//
// The values for the bits were chosen carefully, so that the leading byte of
// Rebol structures could be distinguished from the leading byte of a UTF-8
// string.  This is taken advantage of in the API.
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

    DETECTED_AS_END = 5 // may be a cell, or made with Endlike_Header()
};

// !!! Given how often this is called, would a 256 byte table mapping bytes
// to types be worth it?  The function call could be avoided entirely.
// Alternately, it could be folded into the UTF-8 detection so that the
// invalid Rebol-oriented cases gave illegal codepoints...that way, it could
// already be on its first step of a UTF-8 decode otherwise.
//
inline static enum Reb_Pointer_Detect Detect_Rebol_Pointer(const void *p) {
    const REBYTE* bp = cast(const REBYTE*, p);

    switch (bp[0] >> 4) {  // switch on the left 4 bits of the byte
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
        if (bp[1] == REB_0)
            return DETECTED_AS_END;  // may be end cell or "endlike" header
        if (bp[0] & 0x1)
            return DETECTED_AS_CELL;  // unmanaged
        return DETECTED_AS_SERIES;  // unmanaged

      case 9:  // 0xb1001
        if (bp[1] == REB_0)
            return DETECTED_AS_END;  // has to be an "endlike" header
        assert(bp[0] & 0x1);  // marked and unmanaged, must be a cell
        return DETECTED_AS_CELL;

      case 10:  // 0b1010
      case 11:  // 0b1011
        if (bp[1] == REB_0)
            return DETECTED_AS_END;
        if (bp[0] & 0x1)
            return DETECTED_AS_CELL; // managed, marked if `case 11`
        return DETECTED_AS_SERIES; // managed, marked if `case 11`

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


// Unlike with GET_CELL_FLAG() etc, there's not really anything to be checked
// on generic nodes (other than having NODE_FLAG_NODE?)  But these macros
// help make the source a little more readable.

#define SET_NOD_FLAGS(n,f) \
    ((n)->header.bits |= (f))

#define SET_NOD_FLAG(n,f) \
    SET_CELL_FLAGS((n), (f))

#define GET_NOD_FLAG(n, f) \
    (did ((n)->header.bits & (f)))

#define ANY_NOD_FLAGS(n,f) \
    (((n)->header.bits & (f)) != 0)

#define ALL_NOD_FLAGS(n,f) \
    (((n)->header.bits & (f)) == (f))

#define CLEAR_NOD_FLAGS(v,f) \
    ((n)->header.bits &= ~(f))

#define CLEAR_NOD_FLAG(n,f) \
    CLEAR_NOD_FLAGS((n), (f))

#define NOT_NOD_FLAG(n,f) \
    (not GET_NOD_FLAG((n), (f)))
