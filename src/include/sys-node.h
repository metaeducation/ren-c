//
//  File: %sys-node.h
//  Summary: {Convenience routines for the Node "superclass" structure}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This provides some convenience routines that require more definitions than
// are available when %sys-rebnod.h is being processed.  (e.g. REBVAL, 
// REBSER, REBFRM...)
//
// See %sys-rebnod.h for what a "node" means in this context.
//


#if !defined(DEBUG_CHECK_CASTS) || !defined(CPLUSPLUS_11)

    #define NOD(p) \
        cast(REBNOD*, (p)) // NOD() just does a cast (maybe with added checks)

#else

    template <typename T>
    inline static REBNOD *NOD(T *p) {
        constexpr bool derived =
            std::is_same<T, REBVAL>::value
            or std::is_same<T, REBSER>::value
            or std::is_same<T, REBSTR>::value
            or std::is_same<T, REBARR>::value
            or std::is_same<T, REBCTX>::value
            or std::is_same<T, REBACT>::value
            or std::is_same<T, REBMAP>::value
            or std::is_same<T, REBFRM>::value;

        constexpr bool base = std::is_same<T, void>::value;

        static_assert(
            derived or base,
            "NOD() works on void/REBVAL/REBSER/REBSTR/REBARR/REBCTX/REBACT" \
               "/REBMAP/REBFRM"
        );

        if (not p) { // !!! include a static check for nullptr/0?
            assert(!"Use cast(REBNOD*, x) and not NOD(x) on null pointers");
        }
        else if (base)
            assert(
                (reinterpret_cast<REBNOD*>(p)->header.bits & (
                    NODE_FLAG_NODE | NODE_FLAG_FREE
                )) == (
                    NODE_FLAG_NODE
                )
            );

        return reinterpret_cast<REBNOD*>(p);
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
inline static void *Make_Node(REBCNT pool_id)
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
inline static void Free_Node(REBCNT pool_id, void *p)
{
  #ifdef DEBUG_MONITOR_SERIES
    if (
        pool_id == SER_POOL
        and not (cast(union Reb_Header*, p)->bits & NODE_FLAG_CELL)
        and GET_SER_INFO(cast(REBSER*, p), SERIES_INFO_MONITOR_DEBUG)
    ){
        printf("Freeing series %p on tick #%d\n", p, cast(int, TG_Tick));
        fflush(stdout);
    }
  #endif

    REBNOD *node = NOD(p);

    FIRST_BYTE(node->header) = FREED_SERIES_BYTE;

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

inline static enum Reb_Pointer_Detect Detect_Rebol_Pointer(const void *p) {
    const REBYTE* bp = cast(const REBYTE*, p);

    switch (bp[0] >> 4) { // switch on the left 4 bits of the byte
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
        return DETECTED_AS_UTF8; // ASCII codepoints 0 - 127

    // v-- bit sequences starting with `10` (continuation bytes, so not
    // valid starting points for a UTF-8 string)

    case 8: // 0xb1000
        if (bp[1] == REB_0)
            return DETECTED_AS_END; // may be end cell or "endlike" header
        if (bp[0] & 0x1)
            return DETECTED_AS_CELL; // unmanaged
        return DETECTED_AS_SERIES; // unmanaged

    case 9: // 0xb1001
        if (bp[1] == REB_0)
            return DETECTED_AS_END; // has to be an "endlike" header
        assert(bp[0] & 0x1); // marked and unmanaged, must be a cell
        return DETECTED_AS_CELL;

    case 10: // 0b1010
    case 11: // 0b1011
        if (bp[1] == REB_0)
            return DETECTED_AS_END;
        if (bp[0] & 0x1)
            return DETECTED_AS_CELL; // managed, marked if `case 11`
        return DETECTED_AS_SERIES; // managed, marked if `case 11`

    // v-- bit sequences starting with `11` are *usually* legal multi-byte
    // valid starting points for UTF-8, with only the exceptions made for
    // the illegal 192 and 193 bytes which represent freed series and cells.

    case 12: // 0b1100
        if (bp[0] == FREED_SERIES_BYTE)
            return DETECTED_AS_FREED_SERIES;

        if (bp[0] == FREED_CELL_BYTE)
            return DETECTED_AS_FREED_CELL;

        return DETECTED_AS_UTF8;

    case 13: // 0b1101
    case 14: // 0b1110
    case 15: // 0b1111
        return DETECTED_AS_UTF8;
    }

    DEAD_END;
}


// Unlike with GET_VAL_FLAG() etc, there's not really anything to be checked
// on generic nodes (other than having NODE_FLAG_NODE?)  But these macros
// help make the source a little more readable.

#define SET_NOD_FLAGS(n,f) \
    ((n)->header.bits |= (f))

#define SET_NOD_FLAG(n,f) \
    SET_VAL_FLAGS((n), (f))

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
