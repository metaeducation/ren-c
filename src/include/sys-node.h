//
//  File: %sys-node.h
//  Summary: -{Convenience routines for the Node "superclass" structure}-
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// are available when %struct-node.h is being processed.  (e.g. Value*,
// Flex*, Level*...)
//
// See %struct-node.h for what a "Node" means in this context.
//


// 1. Just checking that the NODE_FLAG_NODE bit is set is quite costly to
//    be doing on *every* NODE_BYTE() operation.  But, sometimes it comes in
//    handy when tracing down misunderstandings.  A build that enables this
//    should be run every so often.
//
// 2. Losing const information for fetching NODE_BYTE() is intentional.  GC
//    needs to fiddle with the marked flag bit even on Flex that are
//    conceptually immutable, and the managed bit needs to be set on bindings
//    where the reference is const.  If you're changing something from a Cell
//    to a Stub--or otherwise--you have much bigger concerns regarding safety
//    and unsafety than C-level constness!
//
#if !defined(HEAVY_NODE_BYTE_CHECK)  // [1]
    #define NODE_BYTE(p) \
        FIRST_BYTE(x_cast(Node*, ensure(const Node*, (p))))  // x_cast [2]

#else
    INLINE Byte& NODE_BYTE(const Node* node) {
        assert(cast(const Byte*, node)[0] & NODE_BYTEMASK_0x80_NODE);
        return x_cast(Byte*, node)[0];   // cast away constness [2]
    }
#endif

#define FLAG_NODE_BYTE(byte)    FLAG_FIRST_BYTE(byte)

#define Is_Node(p) \
    (c_cast(Byte*, (p))[0] & NODE_BYTEMASK_0x80_NODE)

#define Is_Node_A_Cell(n)   (did (NODE_BYTE(n) & NODE_BYTEMASK_0x08_CELL))
#define Is_Node_A_Stub(n)   (not Is_Node_A_Cell(n))

// !!! There's currently no generic way to tell if a node is a Level.  It has
// the cell flag set in its header, and uses all the other flags.  It's a lie
// to say it's a stub or a cell in any case--even if the layout were changed
// so the leading area was an actual stub or a cell with a special flavor or
// heart byte.  It hasn't been a problem because places Level can be seen
// can't generally hold cells, so the single flag is enough.  Calling out
// this test helps find places that rely on that behavior.
//
#define Is_Non_Cell_Node_A_Level Is_Node_A_Cell

#define Is_Node_Marked(n)   (did (NODE_BYTE(n) & NODE_BYTEMASK_0x01_MARKED))
#define Not_Node_Marked(n)  (not Is_Node_Marked(n))

#define Is_Node_Managed(n)  (did (NODE_BYTE(n) & NODE_BYTEMASK_0x04_MANAGED))
#define Not_Node_Managed(n) (not Is_Node_Managed(n))

#define Is_Node_Readable(n) \
    (not (NODE_BYTE(n) & NODE_BYTEMASK_0x40_UNREADABLE))

#define Not_Node_Readable(n) (not Is_Node_Readable(n))

// Is_Node_Root() sounds like it might be the only node.
// Is_Node_A_Root() sounds like a third category vs Is_Node_A_Cell()/Stub()
//
#define Is_Node_Root_Bit_Set(n) \
    (did (NODE_BYTE(n) & NODE_BYTEMASK_0x02_ROOT))

#define Not_Node_Root_Bit_Set(n) \
    (not (NODE_BYTE(n) & NODE_BYTEMASK_0x02_ROOT))

// Add "_Bit" suffix to reinforce lack of higher level function.  (A macro
// with the name Set_Node_Managed() might sound like it does more, like
// removing from the manuals list the way Manage_Flex() etc. do)

#define Set_Node_Root_Bit(n) \
    NODE_BYTE(n) |= NODE_BYTEMASK_0x02_ROOT

#define Clear_Node_Root_Bit(n) \
    NODE_BYTE(n) &= (~ NODE_BYTEMASK_0x02_ROOT)

#define Set_Node_Marked_Bit(n) \
    NODE_BYTE(n) |= NODE_BYTEMASK_0x01_MARKED

#define Clear_Node_Marked_Bit(n) \
    NODE_BYTE(n) &= (~ NODE_BYTEMASK_0x01_MARKED)

#define Set_Node_Managed_Bit(n) \
    NODE_BYTE(n) |= NODE_BYTEMASK_0x04_MANAGED

#define Clear_Node_Managed_Bit(n) \
    NODE_BYTE(n) &= (~ NODE_BYTEMASK_0x04_MANAGED)

#define Set_Node_Unreadable_Bit(n) \
    NODE_BYTE(n) |= NODE_BYTEMASK_0x40_UNREADABLE

#define Clear_Node_Unreadable_Bit(n) \
    NODE_BYTE(n) &= (~ NODE_BYTEMASK_0x40_UNREADABLE)


//=//// POINTER DETECTION (UTF-8, STUB, CELL, END) ////////////////////////=//
//
// Ren-C's "Nodes" (Cell and Stub derivatives) all have a platform-pointer
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
    DETECTED_AS_UTF8 = 1,
    DETECTED_AS_CELL,
    DETECTED_AS_STUB,
    DETECTED_AS_END,  // a rebEND signal (Note: has char* alignment!)
    DETECTED_AS_FREE
};

typedef enum PointerDetectEnum PointerDetect;

INLINE PointerDetect Detect_Rebol_Pointer(const void *p)
{
    Byte b = FIRST_BYTE(p);

    if (not (b & NODE_BYTEMASK_0x80_NODE))  // test for 1xxxxxxx
        return DETECTED_AS_UTF8;  // < 0x80 is string w/1st char in ASCII range

    if (not (b & NODE_BYTEMASK_0x40_UNREADABLE)) {  // test for 10xxxxxx
        if (b & NODE_BYTEMASK_0x08_CELL)  // 10xxxxxx never starts UTF-8
            return DETECTED_AS_CELL;
        return DETECTED_AS_STUB;
    }

    if (  // we know it's 11xxxxxx... now test for 1111xxxx
        (b & (NODE_BYTEMASK_0x20_GC_ONE | NODE_BYTEMASK_0x10_GC_TWO))
            == (NODE_BYTEMASK_0x20_GC_ONE | NODE_BYTEMASK_0x10_GC_TWO)
    ){
        if (b & NODE_BYTEMASK_0x08_CELL)  // ...now test for 11111xxx
            return DETECTED_AS_CELL;  // 11111xxx never starts UTF-8!

        // There are 3 patterns of 0b11110xxx that are illegal in UTF-8:
        //
        //     0xF5 (11110101), 0xF6 (11110110), 0xF7 (11110111)
        //
        // Hence if the sixth bit is clear (0b111100xx) detect it as UTF-8.
        //
        if (not (b & NODE_BYTEMASK_0x04_MANAGED))
            return DETECTED_AS_UTF8;

        if (b == END_SIGNAL_BYTE) {  // 0xF7
            assert(SECOND_BYTE(p) == '\0');
            return DETECTED_AS_END;
        }

        if (b == FREE_POOLUNIT_BYTE)  // 0xF6
            return DETECTED_AS_FREE;

        if (b == NODE_BYTE_RESERVED)  // 0xF5
            fail ("NODE_BYTE_RESERVED Encountered in Detect_Rebol_Pointer()");

        return DETECTED_AS_STUB;
    }

    if (b == DECAYED_CANON_BYTE or b == DECAYED_NON_CANON_BYTE)
        return DETECTED_AS_FREE;  // 11000000 and 11000001 illegal UTF-8

    return DETECTED_AS_UTF8;
}


// Allocate a node from a pool.  Returned node will not be zero-filled, but
// the header will have NODE_FLAG_UNREADABLE set when it is returned (client is
// responsible for changing that if they plan to enumerate the pool and
// distinguish free nodes from non-free ones.)
//
// All nodes are 64-bit aligned.  This way, data allocated in nodes can be
// structured to know where legal 64-bit alignment points would be.  This
// is required for correct functioning of some types.  (See notes on
// alignment in %struct-cell.h.)
//
INLINE void *Try_Alloc_Pooled(PoolId pool_id)
{
    Pool* pool = &g_mem.pools[pool_id];
    if (not pool->first) {  // pool has run out of nodes
        if (not Try_Fill_Pool(pool))  // attempt to refill it
            return nullptr;
    }

  #if TRAMPOLINE_COUNTS_TICKS
    if (g_mem.fuzz_factor != 0) {
        if (g_mem.fuzz_factor < 0) {
            ++g_mem.fuzz_factor;
            if (g_mem.fuzz_factor == 0)
                return nullptr;
        }
        else if ((g_ts.tick % 10000) <= cast(Tick, g_mem.fuzz_factor)) {
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

  #if DEBUG_MEMORY_ALIGNMENT
    if (i_cast(uintptr_t, unit) % sizeof(REBI64) != 0) {
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
    // to not have NODE_FLAG_UNREADABLE set.)
    //
    assert(FIRST_BYTE(unit) == FREE_POOLUNIT_BYTE);
    return cast(void*, unit);
}


INLINE void *Alloc_Pooled(PoolId pool_id) {
    void *node = Try_Alloc_Pooled(pool_id);
    if (node)
        return node;

    Pool* pool = &g_mem.pools[pool_id];
    fail (Error_No_Memory(pool->wide * pool->num_units_per_segment));
}

#define Alloc_Stub() ( \
    (g_gc.depletion -= sizeof(Stub)) <= 0 \
        ? Set_Trampoline_Flag(RECYCLE) \
        : NOOP, \
    Alloc_Pooled(STUB_POOL))  // not a formed stub yet, don't cast it


// Free a node, returning it to its pool.  Once it is freed, its header will
// have NODE_FLAG_UNREADABLE...which will identify the node as not in use to anyone
// who enumerates the nodes in the pool (such as the garbage collector).
//
INLINE void Free_Pooled(PoolId pool_id, void* p)
{
  #if DEBUG_MONITOR_FLEX
    if (p == g_mem.monitor_node) {
        printf("Freeing Flex %p on TICK %" PRIu64 "\n", p, TICK);
        fflush(stdout);
    }
  #endif

    PoolUnit* unit = cast(PoolUnit*, p);

    FIRST_BYTE(unit) = FREE_POOLUNIT_BYTE;

    Pool* pool = &g_mem.pools[pool_id];

  #if RUNTIME_CHECKS
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
    // checked build only creates a source of variant behavior.

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

INLINE PoolId Pool_Id_For_Size(Size size) {
  #if DEBUG_ENABLE_ALWAYS_MALLOC
    if (g_mem.always_malloc)
        return SYSTEM_POOL;
  #endif

    if (size < POOLS_BY_SIZE_LEN)
        return g_mem.pools_by_size[size];

    return SYSTEM_POOL;
}


//=//// MEMORY ALLOCATION AND FREEING MACROS //////////////////////////////=//
//
// Rebol's internal memory management is done based on a pooled model, which
// use Try_Alloc_Memory_Core() and Free_Memory_Core() instead of malloc/free.
// (Comments on those routines explain why this was done--even in an age of
// modern thread-safe allocators--due to Rebol's ability to exploit extra
// data in its pool unit when a Flex grows.)
//
// Free_MemorY_Core() requires callers to pass in the size of the memory being
// freed, and can be tricky.  These macros are modeled after C++'s new/delete
// and new[]/delete[], and allocations take either a type or a type and a
// length.  The size calculation is done automatically, and the result is cast
// to the appropriate type.  The deallocations also take a type and do the
// calculations.
//
// In a C++11 build, an extra check is done to ensure the type you pass in a
// Free_Memory() call lines up with the type of pointer being freed.
//

#define Try_Alloc_Memory(T) \
    cast(T*, Try_Alloc_Memory_Core(sizeof(T)))

#define Try_Alloc_Memory_Zerofill(t) \
    cast(T*, memset(Try_Alloc_Memory_Core(T), '\0', sizeof(T)))

#define Try_Alloc_Memory_N(T,n) \
    cast(T*, Try_Alloc_Memory_Core(sizeof(T) * (n)))

#define Try_Alloc_Memory_N_Zerofill(T,n) \
    cast(T*, memset(Try_Alloc_Memory_N(T, (n)), '\0', sizeof(T) * (n)))

#if CPLUSPLUS_11
    #define Free_Memory(T,p) \
        do { \
            static_assert( \
                std::is_same<decltype(p), std::add_pointer<T>::type>::value, \
                "mismatched Free_Memory() type" \
            ); \
            Free_Memory_Core(p, sizeof(T)); \
        } while (0)

    #define Free_Memory_N(T,n,p) \
        do { \
            static_assert( \
                std::is_same<decltype(p), std::add_pointer<T>::type>::value, \
                "mismatched Free_Memory_N() type" \
            ); \
            Free_Memory_Core(p, sizeof(T) * (n)); \
        } while (0)
#else
    #define Free_Memory(T,p) \
        Free_Memory_Core((p), sizeof(T))

    #define Free_Memory_N(T,n,p) \
        Free_Memory_Core((p), sizeof(T) * (n))
#endif


//=//// NODE HOLDER C++ TEMPLATE //////////////////////////////////////////=//
//
// The NodeHolder is a neat trick which is used by accessors like LINK() and
// MISC() to be able to put type checking onto the extraction of a node
// subclass, while not causing errors if used as the left-hand side of an
// assignment (on a possibly uninitialized piece of data).  This means you
// don't need to have separate macros:
//
//    LINK(Property, s) = foo;
//    bar = LINK(Property, s);
//
// It simply puts the reference in a state of suspended animation until it
// knows if it's going to be used on the left hand side of an assignment or
// not.  If it's on the left, it accepts the assignment--type checked to the
// template parameter.  If it's on the right, it runs a validating cast of
// the template parameter type.

#if CPLUSPLUS_11
    template<typename T>
    struct NodeHolder {
        const Node* & ref;

        NodeHolder(const Node* const& ref)
            : ref (const_cast<const Node* &>(ref))
          {}

        void operator=(T &right)
          { ref = right; }
        void operator=(NodeHolder<T> const& right)
          { ref = right.ref; }
        void operator=(nullptr_t)
          { ref = nullptr; }

        T operator-> () const
          { return cast(T, m_cast(Node*, ref)); }

        operator T () const
          { return cast(T, m_cast(Node*, ref)); }
    };

  #if RUNTIME_CHECKS
    template<class T>
    INLINE void Corrupt_Pointer_If_Debug(NodeHolder<T> const& nh)
      { nh.ref = p_cast(Node*, cast(uintptr_t, 0xDECAFBAD)); }
  #endif
#endif
