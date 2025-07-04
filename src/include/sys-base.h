//
//  file: %sys-base.h
//  summary: -[Convenience routines for the Base "superclass" structure]-
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// are available when %struct-base.h is being processed.  (e.g. Value*,
// Flex*, Level*...)
//
// See %struct-base.h for what a "Base" means in this context.
//


// 1. Just checking that the BASE_FLAG_BASE bit is set is quite costly to
//    be doing on *every* BASE_BYTE() operation.  But, sometimes it comes in
//    handy when tracing down misunderstandings.  A build that enables this
//    should be run every so often.
//
// 2. Losing const information for fetching BASE_BYTE() is intentional.  GC
//    needs to fiddle with the marked flag bit even on Flex that are
//    conceptually immutable, and the managed bit needs to be set on bindings
//    where the reference is const.  m_cast() still enforces that the type
//    isn't something random (so Base*-compatible)

typedef Byte BaseByte;

#if !defined(HEAVY_BASE_BYTE_CHECK)  // [1]
    #define BASE_BYTE(p) \
        FIRST_BYTE(m_cast(Base*, (p)))  // m_cast [2]

#else
    INLINE Byte& BASE_BYTE(const Base* base) {
        assert(u_cast(Byte*, base)[0] & BASE_BYTEMASK_0x80_NODE);
        return m_cast(Byte*, base)[0];   // cast away constness [2]
    }
#endif

#define FLAG_BASE_BYTE(byte)    FLAG_FIRST_BYTE(byte)

#define Is_Base(p) \
    (c_cast(Byte*, (p))[0] & BASE_BYTEMASK_0x80_NODE)

#define Is_Base_A_Cell(n)   (did (BASE_BYTE(n) & BASE_BYTEMASK_0x08_CELL))
#define Is_Base_A_Stub(n)   (not Is_Base_A_Cell(n))

#define Is_Base_Marked(n)   (did (BASE_BYTE(n) & BASE_BYTEMASK_0x01_MARKED))
#define Not_Base_Marked(n)  (not Is_Base_Marked(n))

#define Is_Base_Managed(n)  (did (BASE_BYTE(n) & BASE_BYTEMASK_0x04_MANAGED))
#define Not_Base_Managed(n) (not Is_Base_Managed(n))

#define Is_Base_Readable(n) \
    (not (BASE_BYTE(n) & BASE_BYTEMASK_0x40_UNREADABLE))

#define Not_Base_Readable(n) (not Is_Base_Readable(n))

// Is_Base_Root() sounds like it might be the only node.
// Is_Base_A_Root() sounds like a third category vs Is_Base_A_Cell()/Stub()
//
#define Is_Base_Root_Bit_Set(n) \
    (did (BASE_BYTE(n) & BASE_BYTEMASK_0x02_ROOT))

#define Not_Base_Root_Bit_Set(n) \
    (not (BASE_BYTE(n) & BASE_BYTEMASK_0x02_ROOT))

// Add "_Bit" suffix to reinforce lack of higher level function.  (A macro
// with the name Set_Base_Managed() might sound like it does more, like
// removing from the manuals list the way Manage_Flex() etc. do)

#define Set_Base_Root_Bit(n) \
    BASE_BYTE(n) |= BASE_BYTEMASK_0x02_ROOT

#define Clear_Base_Root_Bit(n) \
    BASE_BYTE(n) &= (~ BASE_BYTEMASK_0x02_ROOT)

#define Set_Base_Marked_Bit(n) \
    BASE_BYTE(n) |= BASE_BYTEMASK_0x01_MARKED

#define Clear_Base_Marked_Bit(n) \
    BASE_BYTE(n) &= (~ BASE_BYTEMASK_0x01_MARKED)

#define Set_Base_Managed_Bit(n) \
    BASE_BYTE(n) |= BASE_BYTEMASK_0x04_MANAGED

#define Clear_Base_Managed_Bit(n) \
    BASE_BYTE(n) &= (~ BASE_BYTEMASK_0x04_MANAGED)

#define Set_Base_Unreadable_Bit(n) \
    BASE_BYTE(n) |= BASE_BYTEMASK_0x40_UNREADABLE

#define Clear_Base_Unreadable_Bit(n) \
    BASE_BYTE(n) &= (~ BASE_BYTEMASK_0x40_UNREADABLE)


//=//// POINTER DETECTION (UTF-8, STUB, CELL, END) ////////////////////////=//
//
// Ren-C's Cell and Stub derivatives all have a platform-pointer-sized header
// of bits, which is constructed using byte-order-sensitive bit flags (see
// FLAG_LEFT_BIT and related definitions for how those work).
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
    DETECTED_AS_FREE,
    DETECTED_AS_WILD  // arbitrary out-of-band purposes
};

typedef enum PointerDetectEnum PointerDetect;

INLINE PointerDetect Detect_Rebol_Pointer(const void *p)
{
    Byte b = FIRST_BYTE(p);

    if (not (b & BASE_BYTEMASK_0x80_NODE))  // test for 1xxxxxxx
        return DETECTED_AS_UTF8;  // < 0x80 is string w/1st char in ASCII range

    if (not (b & BASE_BYTEMASK_0x40_UNREADABLE)) {  // test for 10xxxxxx
        if (b & BASE_BYTEMASK_0x08_CELL)  // 10xxxxxx never starts UTF-8
            return DETECTED_AS_CELL;
        return DETECTED_AS_STUB;
    }

    if (  // we know it's 11xxxxxx... now test for 1111xxxx
        (b & (BASE_BYTEMASK_0x20_GC_ONE | BASE_BYTEMASK_0x10_GC_TWO))
            == (BASE_BYTEMASK_0x20_GC_ONE | BASE_BYTEMASK_0x10_GC_TWO)
    ){
        if (b & BASE_BYTEMASK_0x08_CELL)  // ...now test for 11111xxx
            return DETECTED_AS_CELL;  // 11111xxx never starts UTF-8!

        // There are 3 patterns of 0b11110xxx that are illegal in UTF-8:
        //
        //     0xF5 (11110101), 0xF6 (11110110), 0xF7 (11110111)
        //
        // Hence if the sixth bit is clear (0b111100xx) detect it as UTF-8.
        //
        if (not (b & BASE_BYTEMASK_0x04_MANAGED))
            return DETECTED_AS_UTF8;

        if (b == END_SIGNAL_BYTE) {  // 0xF7
            assert(SECOND_BYTE(p) == '\0');
            return DETECTED_AS_END;
        }

        if (b == FREE_POOLUNIT_BYTE)  // 0xF6
            return DETECTED_AS_FREE;

        if (b == BASE_BYTE_WILD)  // 0xF5
            return DETECTED_AS_WILD;

        return DETECTED_AS_STUB;
    }

    if (b == DIMINISHED_CANON_BYTE or b == DIMINISHED_NON_CANON_BYTE)
        return DETECTED_AS_FREE;  // 11000000 and 11000001 illegal UTF-8

    return DETECTED_AS_UTF8;
}


// Allocate a Unit from a pool.
//
// 1. The first byte of the returned allocation will be FREE_POOLUNIT_BYTE
//    in release builds.  It's up to the client to update the bytes of the
//    returned unit.
//
// 2. Checked builds scramble the first byte occasionally, just to keep code
//    from depending on the allocation returning FREE_POOLUNIT_BYTE.  There's
//    not a good reason to depend on it at this time, and it may be desirable
//    to change the implementation so guaranteeing it is avoided.
//
// 3. All nodes are 64-bit aligned.  This way, data allocated in nodes can be
//    structured to know where legal 64-bit alignment points would be.  This
//    is required for correct functioning of some types.  (See notes on
//    alignment in %struct-cell.h.)
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
        else if ((g_tick % 10000) <= cast(Tick, g_mem.fuzz_factor)) {
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

  #if CHECK_MEMORY_ALIGNMENT  // always 64-bit aligned returns [3]
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
        crash (unit);
    }
  #endif

    assert(FIRST_BYTE(unit) == FREE_POOLUNIT_BYTE);  // client must adjust [1]

  #if RUNTIME_CHECKS && TRAMPOLINE_COUNTS_TICKS  // scramble occasionally [2]
    if (SPORADICALLY(8))
        FIRST_BYTE(unit) = u_cast(Byte, g_tick % 256);
  #endif

    return cast(void*, unit);
}


INLINE void *Alloc_Pooled(PoolId pool_id) {
    void *node = Try_Alloc_Pooled(pool_id);
    if (node)
        return node;

    Pool* pool = &g_mem.pools[pool_id];
    abrupt_panic (Error_No_Memory(pool->wide * pool->num_units_per_segment));
}

#define Alloc_Stub() ( \
    (g_gc.depletion -= sizeof(Stub)) <= 0 \
        ? Set_Trampoline_Flag(RECYCLE) \
        : NOOP, \
    Alloc_Pooled(STUB_POOL))  // not a formed stub yet, don't cast it


// Free a Unit, returning it to its pool.  Once it is freed, its header will
// have BASE_FLAG_UNREADABLE...which will identify the Unit as not in use to
// anyone enumerating Units in the pool (such as the garbage collector).
//
INLINE void Free_Pooled(PoolId pool_id, void* p)
{
  #if DEBUG_MONITOR_FLEX
    if (p == maybe g_mem.monitoring) {
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
    // !!! In R3-Alpha, the most recently freed Unit would become the first
    // Unit to hand out.  This is a simple and likely good strategy for
    // cache usage, but makes the "poisoning" nearly useless.
    //
    // This code was added to insert an empty segment, such that this Unit
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
        // We don't want Free_Base to panic with an "out of memory" error, so
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
