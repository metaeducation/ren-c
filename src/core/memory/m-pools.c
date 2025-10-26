//
//  file: %m-pools.c
//  summary: "memory allocation pool management"
//  section: memory
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A point of Rebol's design was to remain small and solve its domain without
// relying on a lot of abstraction.  Its memory-management was thus focused on
// staying low-level...and being able to do efficient and lightweight
// allocations of various kinds of Flex.
//
// Unless they've been explicitly marked as fixed-size, Flex have a dynamic
// component.  But they also have a fixed-size component that is allocated
// from a memory pool of other fixed-size things.  This is called the "Stub".
// It is an item whose pointer is valid for the lifetime of the object,
// regardless of resizing.  This is where header information is stored, and
// pointers to these structs may be saved in Cells; such that they are kept
// alive by the garbage collector.
//
// The more complicated thing to do memory pooling of is the variable-sized
// portion of a Flex (currently called the "Flex data")...as Flex sizes
// can vary widely.  But a trick Rebol has is that a Flex might be able to
// take advantage of being given back an allocation larger than requested.
// They can use it as reserved space for growth.
//
// (Typical models for implementation of things like C++'s std::vector do not
// reach below new[] or delete[], which are generally implemented with malloc()
// and free() under the hood.  Their buffered additional capacity is done
// assuming the allocation they get is as big as they asked for...no more and
// no less.)
//
// !!! While the space usage is very optimized in this model, there was no
// consideration for intelligent thread safety for allocations and frees.
// So although code like `tcmalloc` might be slower and have more overhead,
// it does offer that advantage.
//
// R3-Alpha included some code to assist in debugging client code using Flex
// such as by initializing the memory to garbage values.  Given the existence
// of modern tools like Valgrind and Address Sanitizer, Ren-C instead has a
// mode in which pools are not used for data allocations, but going through
// malloc() and free().  You enable this by setting the environment variable
// R3_ALWAYS_MALLOC to 1.
//

#include "sys-core.h"
#include "sys-int-funcs.h"


//
//  Raw_Heap_Alloc: C
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTE: Use Alloc_On_Heap() and Alloc_N_On_Heap() instead of this function,
// as they use the datatype to calculate the size of the alloc/free.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Raw_Heap_Alloc() is low-level, and clients must call Raw_Heap_Free() with
// the size of the memory they're asking to free.  This differs fromm alloc(),
// whose clients do not need to remember the size of the allocation to pass
// into free().
//
// One motivation behind using such an allocator in Rebol is to allow it to
// keep knowledge of how much memory the system is using.  This means it can
// decide when to trigger a garbage collection, or raise an out-of-memory
// error before the operating system would, e.g. via 'ulimit':
//
//     http://stackoverflow.com/questions/1229241/
//
// Finer-grained allocations are done with memory pooling.
//
// 1. malloc() internally remembers the size of the allocation, and is hence
//    "overkill" for this operation.  Yet the current implementations on all
//    C platforms use malloc() and free() anyway.
//
// 2. We cache the size at the head of the allocation in checked builds to
//    make sure the right size is passed in.  This has the side benefit of
//    catching free() use with Alloc_On_Heap() instead of Free_Memory().
//
Result(void*) Raw_Heap_Alloc(Size size)
{
    g_mem.usage += size;

    if (  // check if memory usage limit exceeds *before* the allocation
        g_mem.usage_limit
        and g_mem.usage > unwrap g_mem.usage_limit
    ){
        g_mem.usage -= size;
        return fail (Cell_Error(g_error_no_memory));  // distinguish error?
    }

  #if TRAMPOLINE_COUNTS_TICKS && RUNTIME_CHECKS
    if (g_mem.fuzz_factor != 0) {
        if (SPORADICALLY(g_mem.fuzz_factor))
            return fail ("Artificial allocation failure (fuzz_factor)");
    }
  #endif

  #if NO_RUNTIME_CHECKS
    void *p = malloc(size);  // malloc remembers the size [1]
  #else
    void *p = malloc(size + ALIGN_SIZE);  // cache size in alloc [2]
    if (p) {
        *cast(REBI64*, p) = size;  // ALIGN_SIZE >= 64-bit
        p = cast(char*, p) + ALIGN_SIZE;
    }
  #endif

  #if CHECK_MEMORY_ALIGNMENT
    assert(i_cast(uintptr_t, p) % ALIGN_SIZE == 0);
  #endif

    if (not p) {
        g_mem.usage -= size;
        return fail (Cell_Error(g_error_no_memory));
    }

    return p;
}


//
//  Raw_Heap_Free: C
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTE: Instead of Raw_Heap_Free(), use Free_Memory() and Free_Memory_N()
// wrapper macros to ensure the memory block being freed matches the
// appropriate size and type.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Raw_Heap_Free() is a wrapper over free(), that subtracts from a total count that
// Rebol can see how much memory was released.  This information assists in
// deciding when it is necessary to run a garbage collection, or when to
// impose a quota.
//
void Raw_Heap_Free(void *mem, Size size)
{
  #if NO_RUNTIME_CHECKS
    free(mem);
  #else
    assert(mem);
    char *ptr = cast(char *, mem) - sizeof(REBI64);
    assert(*cast(REBI64*, ptr) == cast(REBI64, size));
    free(ptr);
  #endif

    g_mem.usage -= size;
}


/***********************************************************************
**
**  MEMORY POOLS
**
**      Memory management operates off an array of pools, the first
**      group of which are fixed size (so require no compaction).
**
***********************************************************************/
const PoolSpec Mem_Pool_Spec[MAX_POOLS] =
{
    // R3-Alpha had a "0-8 small string pool".  e.g. a pool of allocations for
    // payloads 0 to 8 bytes in length.  These are not technically possible in
    // Ren-C's pool, because it requires 2*sizeof(void*) for each base at the
    // minimum...because instead of just the freelist pointer, it has a
    // standardized header (0 when free).
    //
    // This is not a problem, since all such small strings would also need
    // Stubs...and Ren-C has a better answer to embed the payload directly
    // into the Stub when STUB_FLAG_DYNAMIC is not set.  This wouldn't apply
    // if you were trying to do very small allocations of strings that did not
    // have associated Stubs...but those don't exist in the code.

    MOD_POOL( 1, 256),  // 9-16 (when sizeof(Cell) is 16)
    MOD_POOL( 2, 512),  // 17-32 - Small Flexes (x 16)
    MOD_POOL( 3, 1024), // 33-64
    MOD_POOL( 4, 512),
    MOD_POOL( 5, 256),
    MOD_POOL( 6, 128),
    MOD_POOL( 7, 128),
    MOD_POOL( 8,  64),
    MOD_POOL( 9,  64),
    MOD_POOL(10,  64),
    MOD_POOL(11,  32),
    MOD_POOL(12,  32),
    MOD_POOL(13,  32),
    MOD_POOL(14,  32),
    MOD_POOL(15,  32),
    MOD_POOL(16,  64),  // 257
    MOD_POOL(20,  32),  // 321 - Mid-size Flexes (x 64)
    MOD_POOL(24,  16),  // 385
    MOD_POOL(28,  16),  // 449
    MOD_POOL(32,   8),  // 513

    DEF_POOL(MEM_BIG_SIZE,  16),    // 1K - Large Flexes (x 1024)
    DEF_POOL(MEM_BIG_SIZE*2, 8),    // 2K
    DEF_POOL(MEM_BIG_SIZE*3, 4),    // 3K
    DEF_POOL(MEM_BIG_SIZE*4, 4),    // 4K

    DEF_POOL(sizeof(Stub), 4096),  // Stubs

  #if UNUSUAL_CELL_SIZE  // sizeof(Cell)*2 != sizeof(Stub)
    DEF_POOL(sizeof(Cell) * 2, 16),  // Pairings, PAIR_POOL
  #endif

    DEF_POOL(
        Adjust_Size_For_Align_Evil_Macro(sizeof(Level), sizeof(REBI64)),
        128
    ),
    DEF_POOL(
        Adjust_Size_For_Align_Evil_Macro(sizeof(Feed), sizeof(REBI64)),
        128
    ),

    DEF_POOL(sizeof(REBI64), 1), // Just used for tracking main memory
};


//
//  Startup_Pools: C
//
// Initialize memory pool array.
//
void  Startup_Pools(REBINT scale)
{
    g_mem.usage = 0;
    g_mem.usage_limit = 0;  // unlimited

  #if DEBUG_ENABLE_ALWAYS_MALLOC
    g_mem.always_malloc = false;

    const char *env_always_malloc = getenv("R3_ALWAYS_MALLOC");
    if (env_always_malloc and atoi(env_always_malloc) != 0)
        g_mem.always_malloc = true;
    if (g_mem.always_malloc) {
        printf(
            "**\n" \
            "** R3_ALWAYS_MALLOC is nonzero in environment variable!\n" \
            "** (Or hardcoded g_mem.always_malloc = true in initialization)\n" \
            "** Memory allocations aren't pooled, expect slowness...\n" \
            "**\n"
        );
        fflush(stdout);
    }
  #endif

    REBINT unscale = 1;
    if (scale == 0)
        scale = 1;
    else if (scale < 0) {
        unscale = -scale;
        scale = 1;
    }

    require (
      g_mem.pools = Alloc_N_On_Heap(Pool, MAX_POOLS)
    );

    REBLEN n;
    for (n = 0; n < MAX_POOLS; n++) {  // copy pool sizes to new pool structure
        g_mem.pools[n].segments = nullptr;
        g_mem.pools[n].first = nullptr;
        g_mem.pools[n].last = nullptr;

      #if CHECK_MEMORY_ALIGNMENT
        if (Mem_Pool_Spec[n].wide % sizeof(REBI64) != 0)
            crash ("memory pool width is not 64-bit aligned");
      #endif

        g_mem.pools[n].wide = Mem_Pool_Spec[n].wide;

        g_mem.pools[n].num_units_per_segment = (
            (Mem_Pool_Spec[n].num_units_per_segment * scale) / unscale
        );

        if (g_mem.pools[n].num_units_per_segment < 2)
            g_mem.pools[n].num_units_per_segment = 2;
        g_mem.pools[n].free = 0;
        g_mem.pools[n].has = 0;
    }

    require (
      g_mem.pools_by_size = Alloc_N_On_Heap(Byte, POOLS_BY_SIZE_LEN)
    );

    // sizes 0 - 8 are pool 0
    for (n = 0; n <= 8; n++) g_mem.pools_by_size[n] = 0;
    for (; n <= 16 * MEM_MIN_SIZE; n++)
        g_mem.pools_by_size[n] = MEM_TINY_POOL + ((n-1) / MEM_MIN_SIZE);
    for (; n <= 32 * MEM_MIN_SIZE; n++)
        g_mem.pools_by_size[n] = MEM_SMALL_POOLS-4 + ((n-1) / (MEM_MIN_SIZE * 4));
    for (; n <=  4 * MEM_BIG_SIZE; n++)
        g_mem.pools_by_size[n] = MEM_MID_POOLS + ((n-1) / MEM_BIG_SIZE);

    assert(n == POOLS_BY_SIZE_LEN);

  #if DEBUG_COLLECT_STATS
    assert(g_mem.flex_memory == 0);
    assert(g_mem.num_flex_made == 0);
    assert(g_mem.num_flex_freed == 0);
    assert(g_mem.num_flex_expanded == 0);
    assert(g_mem.blocks_made == 0);
    assert(g_mem.objects_made == 0);
  #endif

    require (
      g_mem.prior_expand = Alloc_N_On_Heap(Flex*, MAX_EXPAND_LIST)
    );
    memset(g_mem.prior_expand, 0, sizeof(Flex*) * MAX_EXPAND_LIST);
    g_mem.prior_expand[0] = (Flex*)1;
}


//
//  Shutdown_Pools: C
//
// Release all segments in all pools, and the pools themselves.
//
void Shutdown_Pools(void)
{
  check_for_leaks: {

  #if RUNTIME_CHECKS
    Count num_leaks = 0;
    Stub* leaked = nullptr;
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for(; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            ++num_leaks;

            Stub* stub = u_cast(Stub*, unit);
            if (Is_Base_Managed(stub)) {
                printf("MANAGED Stub leak, this REALLY shouldn't happen\n");
                leaked = stub;  // report a managed one if found
            }
            else if (not leaked) {
                leaked = stub;  // first one found
            }
            else if (Not_Base_Managed(leaked)) {
              #if TRAMPOLINE_COUNTS_TICKS && DEBUG_STUB_ORIGINS
                if (leaked->tick < stub->tick)
                    leaked = stub;  // update if earlier tick reference
              #endif
            }
        }
    }
    if (leaked) {
        printf("%d leaked Flexes...crash()ing one\n", cast(int, num_leaks));
        crash (leaked);
    }
  #endif

} shutdown_pools: {

    PoolId pool_id;
    for (pool_id = 0; pool_id < MAX_POOLS; ++pool_id) {
        Pool *pool = &g_mem.pools[pool_id];
        Size mem_size = (
            pool->wide * pool->num_units_per_segment + sizeof(Segment)
        );

        Segment* seg = pool->segments;
        while (seg) {
            Segment* next = seg->next;
            Free_Memory_N(char, mem_size, cast(char*, seg));
            seg = next;
        }
    }

    Free_Memory_N(Pool, MAX_POOLS, g_mem.pools);

    Free_Memory_N(Byte, POOLS_BY_SIZE_LEN, g_mem.pools_by_size);

    // !!! Revisit location (just has to be after all Flex are freed)
    Free_Memory_N(Flex*, MAX_EXPAND_LIST, g_mem.prior_expand);

  #if DEBUG_COLLECT_STATS
    g_mem.flex_memory = 0;
    g_mem.num_flex_made = 0;
    g_mem.num_flex_freed = 0;
    g_mem.num_flex_expanded = 0;
    g_mem.blocks_made = 0;
    g_mem.objects_made = 0;
  #endif

    if (g_mem.usage != 0) {
      #if NO_RUNTIME_CHECKS
        crash ("g_mem.usage != 0 on exit, please report this memory leak");
      #else
        // If using valgrind or address sanitizer, they can present more
        // information about leaks than just how much was leaked.  So don't
        // assert...exit normally so they go through their process of
        // presenting the leaks at program termination.
        //
        printf(
            "*** g_mem.usage = %lu ***\n",
            cast(unsigned long, g_mem.usage)
        );

        printf(
            "Memory accounting imbalance: Rebol internally tracks how much\n"
            "memory it uses to know when to garbage collect, etc.  For\n"
            "some reason this accounting did not balance to zero on exit.\n"
            "Run under Valgrind with --leak-check=full --track-origins=yes\n"
            "to find out why this is happening.\n"
        );
      #endif
    }
}}


//
//  Fill_Pool: C
//
// Allocate memory for a pool.  The amount allocated will be determined from
// the size and units specified when the pool header was created.  The nodes
// of the pool are linked to the free list.
//
Result(Zero) Fill_Pool(Pool* pool)
{
    REBLEN num_units = pool->num_units_per_segment;
    REBLEN mem_size = pool->wide * num_units + sizeof(Segment);

    trap (
      Segment* seg = u_downcast Raw_Heap_Alloc(mem_size)
    );
    seg->size = mem_size;
    seg->next = pool->segments;
    pool->segments = seg;
    pool->has += num_units;
    pool->free += num_units;

    // Add new nodes to the end of free list:

    PoolUnit* unit = cast(PoolUnit*, seg + 1);

    if (not pool->first) {
        assert(not pool->last);
        pool->first = unit;
    }
    else {
        assert(pool->last);
        pool->last->next_if_free = unit;
    }

    while (true) {
        FIRST_BYTE(unit) = FREE_POOLUNIT_BYTE;

        if (--num_units == 0) {
            unit->next_if_free = nullptr;
            break;
        }

        unit->next_if_free = cast(PoolUnit*, cast(Byte*, unit) + pool->wide);
        unit = unit->next_if_free;
    }

    pool->last = unit;
    return zero;
}


#if DEBUG_FANCY_CRASH

//
//  Try_Find_Containing_Base_Debug: C
//
// This debug-build-only routine will look to see if it can find what Flex
// a data pointer lives in.  It returns nullptr if it can't find one.  It's
// very slow, because it has to look at all the Stubs.  Use sparingly!
//
Base* Try_Find_Containing_Base_Debug(const void *p)
{
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            if (unit[0] & BASE_BYTEMASK_0x08_CELL) {  // a "pairing"
                Pairing* pairing = u_cast(Pairing*, unit);
                if (
                    p >= cast(void*, Pairing_Head(pairing))
                    and p < cast(void*, Pairing_Tail(pairing))
                ){
                    return pairing;  // in stub pool, but actually Cell[2]
                }
                continue;
            }

            Flex* f = u_cast(Flex*, unit);
            if (Not_Stub_Flag(f, DYNAMIC)) {
                if (
                    p >= cast(void*, &f->content)
                    && p < cast(void*, &f->content + 1)
                ){
                    return f;
                }
                continue;
            }

            if (p < cast(void*,
                f->content.dynamic.data - (Flex_Wide(f) * Flex_Bias(f))
            )) {
                // The memory lies before the Flex data allocation.
                //
                continue;
            }

            if (p >= cast(void*, f->content.dynamic.data
                + (Flex_Wide(f) * Flex_Rest(f))
            )) {
                // The memory lies after the Flex capacity.
                //
                continue;
            }

            // We now have a bad condition, in that the pointer is known to
            // be resident in a Flex data allocation.  But it could be doubly
            // bad if the pointer is in the extra head or tail capacity,
            // because that's effectively free data.  Since we're already
            // going to be asserting if we get here, go ahead and pay to
            // check if either of those is the case.

            if (p < cast(void*, f->content.dynamic.data)) {
                printf("Pointer found in freed head capacity of Flex\n");
                fflush(stdout);
                return f;
            }

            if (p >= cast(void*,
                f->content.dynamic.data
                + (Flex_Wide(f) * Flex_Used(f))
            )) {
                printf("Pointer found in freed tail capacity of Flex\n");
                fflush(stdout);
                return f;
            }

            return f;
        }
    }

    return nullptr;  // not found
}

#endif


//
//  Alloc_Pairing: C
//
// !!! Pairings are not currently put into any tracking lists, so they'll
// leak if not freed or managed.  This shouldn't be hard to fix--it just
// means the GC manuals list needs to be Base* and not just Flex*.
//
Pairing* Alloc_Pairing(Flags flags) {
    assert(flags == 0 or flags == BASE_FLAG_MANAGED);
    require (
      Pairing* p = u_downcast Raw_Pooled_Alloc(PAIR_POOL)  // 2x cell size
    );
    Pairing_First(p)->header.bits = CELL_MASK_UNREADABLE | flags;
    Pairing_Second(p)->header.bits = CELL_MASK_UNREADABLE;

    return p;
}


//
//  Copy_Pairing: C
//
Pairing* Copy_Pairing(const Pairing* p, Flags flags) {
    assert(flags == 0 or flags == BASE_FLAG_MANAGED);

    Pairing* copy = Alloc_Pairing(flags);
    Copy_Cell(Pairing_First(copy), Pairing_First(p));
    Copy_Cell(Pairing_Second(copy), Pairing_Second(p));

    return copy;
}


//
//  Manage_Pairing: C
//
// The paired management status is handled by bits directly in the first (the
// paired value) header.
//
void Manage_Pairing(Pairing* p) {
    assert(Not_Base_Managed(p));
    Set_Base_Managed_Bit(p);
}


//
//  Unmanage_Pairing: C
//
// A pairing may become unmanaged.  This is not a good idea for things like
// the pairing used by a PAIR! value.  But pairings are used for API handles
// which default to tying their lifetime to the currently executing level.
// It may be desirable to extend, shorten, or otherwise explicitly control
// their lifetime.
//
void Unmanage_Pairing(Pairing* p) {
    assert(Is_Base_Managed(p));
    Clear_Base_Managed_Bit(p);
}


//
//  Free_Pairing: C
//
void Free_Pairing(Cell* paired) {
    assert(Not_Base_Managed(paired));
    Raw_Pooled_Free(STUB_POOL, paired);

  #if DEBUG_STUB_ORIGINS && TRAMPOLINE_COUNTS_TICKS
    //
    // This wasn't actually a Series, but poke the tick where the base was
    // freed into the memory spot so crash() finds it.
    //
    u_cast(Stub*, paired)->tick = g_tick;
  #endif
}


//
//  Free_Unbiased_Flex_Data: C
//
// Routines that are part of the core Flex implementation call this, including
// Expand_Flex_At_Index_And_Update_Used().  It requires a low-level awareness
// that the Flex data pointer cannot be freed without subtracting out the
// "biasing" which skips the pointer ahead to account for unused capacity at
// the head of the allocation.  They also must know the total allocation size.
//
static void Free_Unbiased_Flex_Data(char *unbiased, Size total)
{
    PoolId pool_id = Pool_Id_For_Size(total);
    Pool* pool;

    if (pool_id < SYSTEM_POOL) {
        //
        // The Flex data does not honor "Base protocol" when it is in use
        // The pools are not swept the way the Stub pool is, so only the
        // free nodes have significance to their PoolUnit headers.
        //
        PoolUnit* unit = cast(PoolUnit*, unbiased);

        assert(g_mem.pools[pool_id].wide >= total);

        pool = &g_mem.pools[pool_id];
        unit->next_if_free = pool->first;
        pool->first = unit;
        pool->free++;

        cast(Byte*, unit)[0] = FREE_POOLUNIT_BYTE;
    }
    else {
        Free_Memory_N(char, total, unbiased);
        g_mem.pools[SYSTEM_POOL].has -= total;
        g_mem.pools[SYSTEM_POOL].free++;
    }
}


//
//  Expand_Flex: C
//
// Expand a Flex at a particular index point by `delta` units.
//
//     index - where space is expanded (but not cleared)
//     delta - number of UNITS to expand (keeping terminator)
//     tail  - will be updated
//
//             |<---rest--->|
//     <-bias->|<-tail->|   |
//     +--------------------+
//     |       abcdefghi    |
//     +--------------------+
//             |    |
//             data index
//
// If the Flex has enough space within it, then it will be used, otherwise the
// Flex data will be reallocated.
//
// When expanded at the head, if bias space is available, it will
// be used (if it provides enough space).
//
// WARNING: Avoid storing direct pointers into the Flex data, as the Flex data
// can be relocated in memory.  Only store if you are certain the Flex is
// set to not be resizable and you control the lifetime of the Flex.
//
Result(Zero) Expand_Flex_At_Index_And_Update_Used(
    Flex* f,
    REBLEN index,
    REBLEN delta
){
    Assert_Flex_Term_If_Needed(f);

    assert(index <= Flex_Used(f));
    if (delta & 0x80000000)
        return fail (Error_Index_Out_Of_Range_Raw());  // 2GB max

    if (delta == 0)
        return zero;

    REBLEN used_old = Flex_Used(f);

    Byte wide = Flex_Wide(f);

    const bool was_dynamic = Get_Stub_Flag(f, DYNAMIC);

    if (was_dynamic and index == 0 and Flex_Bias(f) >= delta) {

    //=//// HEAD INSERTION OPTIMIZATION ///////////////////////////////////=//

        f->content.dynamic.data -= wide * delta;
        f->content.dynamic.used += delta;
        f->content.dynamic.rest += delta;
        Subtract_Flex_Bias(f, delta);

      #if RUNTIME_CHECKS
        if (Stub_Holds_Cells(f)) {
            //
            // When the bias region was marked, it was made "unsettable" if
            // this was a checked build.  Now that the memory is included in
            // the array again, we want it to be "settable".
            //
            // !!! The unsettable feature is currently not implemented,
            // but when it is this will be useful.
            //
            for (index = 0; index < delta; index++)
                Erase_Cell(Array_At(u_cast(Array*, f), index));
        }
      #endif
        Assert_Flex_Term_If_Needed(f);
        return zero;
    }

    // Width adjusted variables:

    Size start = index * wide;
    Size extra = delta * wide;
    Size size = Flex_Used(f) * wide;

    // + wide for terminator
    if ((size + extra + wide) <= Flex_Rest(f) * Flex_Wide(f)) {
        //
        // No expansion was needed.  Slide data down if necessary.

        Unpoison_Flex_Tail_If_Debug(f);
        memmove(
            Flex_Data(f) + start + extra,
            Flex_Data(f) + start,
            size - start
        );
        Set_Flex_Used_Internal(f, used_old + delta);
        Poison_Flex_Tail_If_Debug(f);

        assert(
            not was_dynamic or (
                Flex_Total(f) > ((Flex_Used(f) + Flex_Bias(f)) * wide)
            )
        );

      #if RUNTIME_CHECKS
        if (Stub_Holds_Cells(f)) {
            //
            // The opened up area needs to be set to "settable" in the
            // checked build.  This takes care of making "unsettable" values
            // settable (if part of the expansion is in what was formerly the
            // ->rest), as well as just making sure old data which was in
            // the expanded region doesn't get left over on accident.
            //
            while (delta != 0) {
                --delta;
                Erase_Cell(Array_At(u_cast(Array*, f), index + delta));
            }
        }
      #endif
        return zero;
    }

//=//// INSUFFICIENT CAPACITY, NEW ALLOCATION REQUIRED ////////////////////=//

    if (Get_Flex_Flag(f, FIXED_SIZE))
        return fail (Error_Locked_Series_Raw());

  #if RUNTIME_CHECKS
    if (g_mem.watch_expand) {
        printf(
            "Expand %p wide: %d tail: %d delta: %d\n",
            cast(void*, f),
            cast(int, wide),
            cast(int, used_old),
            cast(int, delta)
        );
        fflush(stdout);
    }
  #endif

    // Have we recently expanded the same Flex?

    REBLEN x = 1;
    REBLEN n_available = 0;
    REBLEN n_found;
    for (n_found = 0; n_found < MAX_EXPAND_LIST; n_found++) {
        if (g_mem.prior_expand[n_found] == f) {
            x = Flex_Used(f) + delta + 1; // Double the size
            break;
        }
        if (!g_mem.prior_expand[n_found])
            n_available = n_found;
    }

  #if RUNTIME_CHECKS
    if (g_mem.watch_expand)
        printf("Expand: %d\n", cast(int, Flex_Used(f) + delta + 1));
  #endif

    // !!! The protocol for doing new allocations currently mandates that the
    // dynamic content area be cleared out.  But the data lives in the content
    // area if there's no dynamic portion.  The in-Stub content has to be
    // copied to preserve the data.  This could be generalized so that the
    // routines that do calculations operate on the content as a whole, not
    // the Stub, so the content is extracted either way.
    //
    StubContentUnion content_old;
    REBINT bias_old;
    REBLEN size_old;
    char *data_old;
    if (was_dynamic) {
        data_old = f->content.dynamic.data;
        bias_old = Flex_Bias(f);
        size_old = Flex_Total(f);
    }
    else {
        Mem_Copy(&content_old, &f->content, sizeof(StubContentUnion));
        data_old = cast(char*, &content_old);
    }

    // The new Flex will *always* be dynamic, because it would not be expanding
    // if a fixed size allocation was sufficient.

    Set_Stub_Flag(f, DYNAMIC);
    Set_Flex_Flag(f, POWER_OF_2);
    trap (
      Flex_Data_Alloc(f, used_old + delta + x)
    );
    assert(Get_Stub_Flag(f, DYNAMIC));
    if (Stub_Holds_Cells(f))
        Prep_Array(u_cast(Array*, f), 0);  // capacity doesn't matter to prep

    // If necessary, add Flex to the recently expanded list
    //
    if (n_found >= MAX_EXPAND_LIST)
        g_mem.prior_expand[n_available] = f;

    // Copy the Flex data up to the expansion point
    //
    memcpy(f->content.dynamic.data, data_old, start);

    // Copy the Flex data after the expansion point.
    //
    memcpy(
        f->content.dynamic.data + start + extra,
        data_old + start,
        size - start
    );
    f->content.dynamic.used = used_old + delta;

    if (was_dynamic) {
        //
        // We have to de-bias the data pointer before we can free it.
        //
        assert(Flex_Bias(f) == 0);  // should be reset
        Free_Unbiased_Flex_Data(data_old - (wide * bias_old), size_old);
    }

  #if DEBUG_COLLECT_STATS
    g_mem.num_flex_expanded += 1;
  #endif

    assert(Not_Base_Marked(f));
    Term_Flex_If_Necessary(f);  // code will not copy terminator over

    return zero;
}


//
//  Swap_Stub_Content: C
//
// Retain the identity of the two Stubs but do a low-level swap of their
// content with each other.
//
// This is a low-level operation that should only be called when the client is
// sure it is safe to do so (more asserts would probably help).
//
// 1. Sequences that have put mirror bytes into arrays intend that to encode a
//    list type, and the sequence needs that to persist.  Review what to do if
//    such arrays ever are seen to be used with this routine.
//
// 2. Swapping managed stubs with unmanaged ones does come up, and when it
//    does the flags have to be correct for their original identity.
//
void Swap_Stub_Content(Stub* a, Stub* b)
{
    assert(Flex_Wide(a) == Flex_Wide(b));  // seemingly should always be true
    assert(Stub_Holds_Cells(a) == Stub_Holds_Cells(b));  // also seems sane

    if (Stub_Flavor(a) == FLAVOR_SOURCE)  // mirror bytes complicate things [1]
        assert(not Mirror_Of(cast(Source*, a)));
    if (Stub_Flavor(b) == FLAVOR_SOURCE)
        assert(not Mirror_Of(cast(Source*, b)));

    bool a_managed = Is_Base_Managed(a);
    bool b_managed = Is_Base_Managed(b);

    Stub temp;
    Mem_Copy(&temp, a, sizeof(Stub));
    Mem_Copy(a, b, sizeof(Stub));
    Mem_Copy(b, &temp, sizeof(Stub));

    if (a_managed != b_managed) {  // managedness mismatches do come up [2]
        if (a_managed)
            Set_Base_Managed_Bit(a);
        else
            Clear_Base_Managed_Bit(a);

        if (b_managed)
            Set_Base_Managed_Bit(b);
        else
            Clear_Base_Managed_Bit(b);
    }
}


//
//  swap-contents: native [
//
//  "Low-level operation for swapping the underlying data for two series"
//
//      return: []
//      series1 [any-series?]
//      series2 [any-series?]
//  ]
//
DECLARE_NATIVE(SWAP_CONTENTS)
{
    INCLUDE_PARAMS_OF_SWAP_CONTENTS;

    if (Any_List(ARG(SERIES1)) != Any_List(ARG(SERIES2)))
        panic ("Can only SWAP-CONTENTS of arrays with other arrays");

    // !!! This is a conservative check, as some binaries could be swapped
    // with ANY-STRING?.  However, that would require checking that the
    // binary is valid UTF-8...mechanics that are available in AS TEXT! etc.
    // Let the user do their own aliasing for now, since the checks are
    // annoying to write.
    //
    if (Is_Blob(ARG(SERIES1)) != Is_Blob(ARG(SERIES2)))
        panic ("Can only SWAP-CONTENTS of binaries with other binaries");

    Flex* f1 = Cell_Flex_Ensure_Mutable(ARG(SERIES1));
    Flex* f2 = Cell_Flex_Ensure_Mutable(ARG(SERIES2));
    Swap_Stub_Content(f1, f2);

    return TRIPWIRE;
}


//
//  Remake_Flex: C
//
// Reallocate a Flex as a given maximum size.  Content in the retained
// portion of the length will be preserved if BASE_FLAG_BASE is passed in.
//
Result(Zero) Remake_Flex(Flex* f, REBLEN units, Flags flags)
{
    // !!! This routine is being scaled back in terms of what it's allowed to
    // do for the moment; so the method of passing in flags is a bit strange.
    //
    assert((flags & ~(BASE_FLAG_BASE | FLEX_FLAG_POWER_OF_2)) == 0);

    bool preserve = did (flags & BASE_FLAG_BASE);

    REBLEN used_old = Flex_Used(f);
    Byte wide = Flex_Wide(f);

    assert(Not_Flex_Flag(f, FIXED_SIZE));

    bool was_dynamic = Get_Stub_Flag(f, DYNAMIC);

    REBINT bias_old;
    REBINT size_old;

    // Extract the data pointer to take responsibility for it.  (The pointer
    // may have already been extracted if the caller is doing their own
    // updating preservation.)

    char *data_old;
    StubContentUnion content_old;
    if (was_dynamic) {
        assert(f->content.dynamic.data != nullptr);
        data_old = f->content.dynamic.data;
        bias_old = Flex_Bias(f);
        size_old = Flex_Total(f);
    }
    else {
        Mem_Copy(&content_old, &f->content, sizeof(StubContentUnion));
        data_old = cast(char*, &content_old);
    }

    f->header.bits |= flags;

    // !!! Currently the remake won't make a Flex that fits entirely in
    // a Stub (so always STUB_FLAG_DYNAMIC).  All Flex code needs a general
    // audit, so that should be one of the things considered.

    Set_Stub_Flag(f, DYNAMIC);
    Flex_Data_Alloc(f, units + 1) except (Error* e) {
        // Put Flex back how it was (there may be extant references)
        f->content.dynamic.data = cast(char*, data_old);

        Size requested_size = (units + 1) * wide;
        UNUSED(requested_size);  // can't alloc memory, how to report?
        return fail (e);
    }
    assert(Get_Stub_Flag(f, DYNAMIC));
    if (Stub_Holds_Cells(f))
        Prep_Array(u_cast(Array*, f), 0);  // capacity doesn't matter to prep

    if (preserve) {
        // Preserve as much data as possible (if it was requested, some
        // operations may extract the data pointer ahead of time and do this
        // more selectively)

        f->content.dynamic.used = MIN(used_old, units);
        memcpy(
            f->content.dynamic.data,
            data_old,
            f->content.dynamic.used * wide
        );
    } else
        f->content.dynamic.used = 0;

  #if DEBUG_UTF8_EVERYWHERE
    if (Is_Stub_Non_Symbol(f))
        Corrupt_If_Needful(MISC_STRAND_NUM_CODEPOINTS(f));
  #endif

    if (was_dynamic)
        Free_Unbiased_Flex_Data(data_old - (wide * bias_old), size_old);

    return zero;
}


//
//  Diminish_Stub: C
//
// 1. There's a generic feature for stubs of storing an arbitrary function in
//    the Stub.misc.cleaner field to run when the stub is GC'd.  But some
//    built-in stub flavors would rather pay for the switch() here than give
//    up the misc slot for that purpose.  (Extension types can't add new
//    FLAVOR_XXX constants, so they *have* to give up the misc slot if they
//    want their Stubs to have GC cleanup behavior.)
//
// 2. We don't want to give a RebolValue* here to the handle cleaner, because
//    then the only way API clients could clean up would be to use API calls
//    to extract the data pointer and length.  Yet this code is running
//    during the garbage collector, and arbitrary API calls are not allowed.
//    So pass the extracted properties instead.
//
// 3. Unlike how CELL_FLAG_DONT_MARK_PAYLOAD_1-style flags work, the cleaner
//    can't be nullptr if MISC_IS_GC_CLEANER is set.  Generally speaking
//    there isn't as much dynamism of tweaking cleaners on and off--once
//    a stub is created and gets a cleaner, it tends to keep it for its
//    entire lifetime.  It may be that Stub flags like LINK_NEEDS_MARK
//    should also not tolerate nullptr, for efficiency, as Stub fields tend
//    not to be fiddled to null and back as often as Cell flags.
//
Stub* Diminish_Stub(Stub* s)
{
    assert(Is_Base_Readable(s));

    if (Not_Stub_Flag(s, CLEANS_UP_BEFORE_GC_DECAY))
        goto do_decay;

    switch (Stub_Flavor(s)) {  // flavors that clean, but can't spare misc [1]
      case FLAVOR_NONSYMBOL:
        Free_Bookmarks_Maybe_Null(cast(Strand*, s));
        break;

      case FLAVOR_SYMBOL:
        GC_Kill_Interning(cast(Symbol*, s));  // special handling adjust canons
        break;

      case FLAVOR_PATCH: {  // remove from Hitch list (see STUB_MASK_PATCH)
        Stub* temp = Misc_Hitch(s);
        while (Misc_Hitch(temp) != s) {
            temp = Misc_Hitch(temp);
        }
        Tweak_Misc_Hitch(temp, Misc_Hitch(s));
        break; }

      case FLAVOR_HANDLE: {  // managed HANDLE! has FLAVOR_HANDLE Stub
        RebolValue* v = cast(RebolValue*, Stub_Cell(s));
        assert(Type_Of(v) == TYPE_HANDLE);
        Option(HandleCleaner*) cleaner = Handle_Cleaner(s);
        if (cleaner)  // can't call librebol API during GC [2]
            Apply_Cfunc(
                unwrap cleaner,
                Cell_Handle_Void_Pointer(v),
                Cell_Handle_Len(v)
            );
        break; }

      default:  // flavors that clean, but CAN spare misc [1]
        Apply_Cfunc(*Stub_Cleaner(s), s);
        break;
    }

  do_decay: { /////////////////////////////////////////////////////////////=//

    // 1. !!! Contexts and actions keep their archetypes, for now, in the
    //    now collapsed base.  For FRAME! this means holding onto the binding
    //    which winds up being used in Derelativize().  See SPC_BINDING.
    //    Preserving ACTION!'s archetype is speculative--to point out the
    //    possibility exists for the other array with a "canon" [0]
    //
    // 2. !!! This indicates reclaiming the data pointer, not the Flex Stubs
    //    themselves...have they never been accounted for, e.g. in R3-Alpha?
    //    If not, they should be...additional sizeof(Stub), also tracking
    //    overhead for that.  Review the question of how the GC watermarks
    //    interact with Try_Alloc_On_Heap() and "higher level" allocations.

    for (REBLEN n = 1; n < MAX_EXPAND_LIST; n++) {  // might be in expand list
        if (g_mem.prior_expand[n] == s)
            g_mem.prior_expand[n] = nullptr;  // remove it if found
    }

    if (Get_Stub_Flag(s, DYNAMIC)) {
        Flex* f = cast(Flex*, s);
        Byte wide = Flex_Wide(f);
        REBLEN bias = Flex_Bias(f);
        REBLEN total = (bias + Flex_Rest(f)) * wide;
        char *unbiased = f->content.dynamic.data - (wide * bias);

        if (Is_Stub_Varlist(f) or Is_Stub_Details(f))  // save archetype [1]
            Mem_Copy(
                &f->content.fixed.cell,
                Array_Head(cast(Array*, f)),
                sizeof(Cell)
            );

        Free_Unbiased_Flex_Data(unbiased, total);

        int tmp;
        g_gc.depletion = Add_I32_Overflows(&tmp, g_gc.depletion, total)
            ? INT32_MAX
            : tmp;  // what about the space for the Stub itself? [2]
    }

    Set_Stub_Unreadable(s);
    return s;
}}


//
//  GC_Kill_Stub: C
//
// Usually, only the garbage collector should be calling this routine.
//
// It frees a Stub even though it is under GC management, because the GC has
// figured out no references exist.
//
void GC_Kill_Stub(Stub* s)
{
  #if RUNTIME_CHECKS
    if (BASE_BYTE(s) == FREE_POOLUNIT_BYTE) {
        printf("Killing already deallocated stub.\n");
        crash (s);
    }
  #endif

    assert(Is_Stub_Diminished(s));  // must Diminish_Stub() first

    // By default the Stub is touched so its tick reflects the tick that
    // freed it.  If you need to know the tick where it was allocated, then
    // comment this out so it remains that way.
    //
    Touch_Stub_If_Debug(s);

    Corrupt_If_Needful(s->info.corrupt);
    // The spot LINK occupies will be used by Raw_Pooled_Free() to link the freelist
    Corrupt_If_Needful(s->misc.corrupt);

    Raw_Pooled_Free(STUB_POOL, s);

    if (g_gc.depletion > 0)
        Clear_Trampoline_Flag(RECYCLE);  // Enough space GC request can cancel

  #if DEBUG_COLLECT_STATS
    g_mem.num_flex_freed += 1;
  #endif
}


//
//  Free_Unmanaged_Flex: C
//
// Release a Flex's Stub and data allocation to memory pools for reuse.
//
void Free_Unmanaged_Flex(Flex* f)
{
  #if RUNTIME_CHECKS
    if (BASE_BYTE(f) == FREE_POOLUNIT_BYTE or Not_Base_Readable(f)) {
        printf("Called Free_Unmanaged_Flex() on decayed or freed Flex\n");
        crash (f);  // erroring here helps not conflate with tracking problems
    }

    if (Is_Base_Managed(f)) {
        printf("Trying to Free_Unmanaged_Flex() on a GC-managed Flex\n");
        crash (f);
    }
  #endif

    Untrack_Manual_Stub(f);
    GC_Kill_Flex(f);  // with bookkeeping done, use same routine as GC
}


#if RUNTIME_CHECKS

//
//  Assert_Pointer_Detection_Working: C
//
// Check the conditions that are required for Detect_Rebol_Pointer() and
// to work, and throw some sample cases at it to make sure they detect right.
//
void Assert_Pointer_Detection_Working(void)
{
    uintptr_t cell_flag = BASE_FLAG_CELL;
    assert(FIRST_BYTE(&cell_flag) == BASE_BYTEMASK_0x08_CELL);
    uintptr_t type_specific_b = CELL_FLAG_TYPE_SPECIFIC_B;
    assert(FOURTH_BYTE(&type_specific_b) == 0x01);

    assert(Detect_Rebol_Pointer("") == DETECTED_AS_UTF8);
    assert(Detect_Rebol_Pointer("asdf") == DETECTED_AS_UTF8);

    assert(Detect_Rebol_Pointer(g_empty_array) == DETECTED_AS_STUB);
    assert(Detect_Rebol_Pointer(g_quasi_null) == DETECTED_AS_CELL);

    DECLARE_ELEMENT (unreadable);
    Init_Unreadable(unreadable);
    Assert_Cell_Writable(unreadable);
    assert(Detect_Rebol_Pointer(unreadable) == DETECTED_AS_CELL);

    assert(Detect_Rebol_Pointer(rebEND) == DETECTED_AS_END);

    const Binary* b = Cell_Binary(g_empty_blob);
    assert(Detect_Rebol_Pointer(b) == DETECTED_AS_STUB);
}


//
//  Check_Memory_Debug: C
//
// Traverse the free lists of all pools -- just to prove we can.
//
// Note: This was useful in R3-Alpha for finding corruption from bad memory
// writes, because a write past the end of a base destroys the pointer for the
// next free area.  The Always_Malloc option for Ren-C leverages the faster
// checking built into Valgrind or Address Sanitizer for the same problem.
// However, a call to this is kept in the checked build on init and shutdown
// just to keep it working.
//
REBLEN Check_Memory_Debug(void)
{
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            if (unit[0] & BASE_BYTEMASK_0x08_CELL)
                continue; // a pairing

            Flex* f = u_cast(Flex*, unit);
            if (Not_Stub_Flag(f, DYNAMIC))
                continue;  // data lives in the Flex Stub itself

            if (Flex_Rest(f) == 0)
                crash (f);  // zero size allocations not legal

            PoolId pool_id = Pool_Id_For_Size(Flex_Total(f));
            if (pool_id >= STUB_POOL)
                continue;  // size doesn't match a known pool

            if (g_mem.pools[pool_id].wide < Flex_Total(f))
                crash (f);
        }
    }

    Count total_free_units = 0;

    PoolId pool_id;
    for (pool_id = 0; pool_id != SYSTEM_POOL; pool_id++) {
        Count pool_free_units = 0;

        PoolUnit* unit = g_mem.pools[pool_id].first;
        for (; unit != nullptr; unit = unit->next_if_free) {
            assert(FIRST_BYTE(unit) == FREE_POOLUNIT_BYTE);

            ++pool_free_units;

            bool found = false;
            seg = g_mem.pools[pool_id].segments;
            for (; seg != nullptr; seg = seg->next) {
                if (
                    cast(Byte*, unit) > cast(Byte*, seg)
                    and (cast(Byte*, unit) < cast(Byte*, seg) + seg->size)
                ){
                    if (found) {
                        printf("unit belongs to more than one segment\n");
                        crash (unit);
                    }

                    found = true;
                }
            }

            if (not found) {
                printf("unit does not belong to one of the pool's segments\n");
                crash (unit);
            }
        }

        if (g_mem.pools[pool_id].free != pool_free_units)
            crash ("actual free unit count does not agree with pool header");

        total_free_units += pool_free_units;
    }

    return total_free_units;
}


//
//  Check_Level_Pool_For_Leaks: C
//
void Check_Level_Pool_For_Leaks(void)
{
    Segment* seg = g_mem.pools[LEVEL_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[LEVEL_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += g_mem.pools[LEVEL_POOL].wide) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            Level* L = cast(Level*, unit);  // ^-- pool size may round up
          #if TRAMPOLINE_COUNTS_TICKS
            printf(
                "** LEVEL LEAKED at tick %lu\n",
                cast(unsigned long, L->tick)
            );
          #else
            assert(!"** LEVEL LEAKED but TRAMPOLINE_COUNTS_TICKS not enabled");
          #endif
        }
    }
}


//
//  Check_Feed_Pool_For_Leaks: C
//
void Check_Feed_Pool_For_Leaks(void)
{
    Segment* seg = g_mem.pools[FEED_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        REBLEN n = g_mem.pools[FEED_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += g_mem.pools[FEED_POOL].wide) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            Feed* feed = cast(Feed*, unit);
          #if TRAMPOLINE_COUNTS_TICKS
            printf(
                "** FEED LEAKED at tick %lu\n",
                cast(unsigned long, feed->tick)
            );
          #else
            assert(!"** FEED LEAKED but no TRAMPOLINE_COUNTS_TICKS enabled\n");
          #endif
        }
    }
}


//
//  Dump_All_Series_Of_Width: C
//
void Dump_All_Series_Of_Width(Size wide)
{
    Count count = 0;
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            if (unit[0] & BASE_BYTEMASK_0x08_CELL)  // a pairing
                continue;

            Flex* f = u_cast(Flex*, unit);
            if (Flex_Wide(f) == wide) {
                ++count;
                printf(
                    "%3d %4d %4d\n",
                    cast(int, count),
                    cast(int, Flex_Used(f)),
                    cast(int, Flex_Rest(f))
                );
            }
            fflush(stdout);
        }
    }
}


//
//  Dump_All_Flex_In_Pool: C
//
// Dump all the Flex in pool.
//
void Dump_All_Flex_In_Pool(PoolId pool_id)
{
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            if (unit[0] & BASE_BYTEMASK_0x08_CELL)
                continue;  // pairing

            Flex* f = u_cast(Flex*, unit);
            if (
                Get_Stub_Flag(f, DYNAMIC)
                and pool_id == Pool_Id_For_Size(Flex_Total(f))
            ){
                Dump_Flex(f, "Dump_All_Flex_In_Pool");
            }

        }
    }
}


//
//  Dump_Pools: C
//
// Print statistics about all memory pools.
//
void Dump_Pools(void)
{
    REBLEN total = 0;
    REBLEN tused = 0;

    PoolId id;
    for (id = 0; id != SYSTEM_POOL; ++id) {
        Count num_segs = 0;
        Size size = 0;

        Segment* seg = g_mem.pools[id].segments;

        for (; seg != nullptr; seg = seg->next, ++num_segs)
            size += seg->size;

        REBLEN used = g_mem.pools[id].has - g_mem.pools[id].free;
        printf(
            "Pool[%-2d] %5dB %-5d/%-5d:%-4d (%3d%%) ",
            cast(int, id),
            cast(int, g_mem.pools[id].wide),
            cast(int, used),
            cast(int, g_mem.pools[id].has),
            cast(int, g_mem.pools[id].num_units_per_segment),
            cast(int,
                g_mem.pools[id].has != 0 ? ((used * 100) / g_mem.pools[id].has) : 0
            )
        );
        printf("%-2d segs, %-7d total\n", cast(int, num_segs), cast(int, size));

        tused += used * g_mem.pools[id].wide;
        total += size;
    }

    printf(
        "Pools used %d of %d (%2d%%)\n",
        cast(int, tused),
        cast(int, total),
        cast(int, (tused * 100) / total)
    );
    printf("System pool used %d\n", cast(int, g_mem.pools[SYSTEM_POOL].has));
    printf("Raw allocator reports %lu\n", cast(unsigned long, g_mem.usage));

    fflush(stdout);
}


//
//  Inspect_Flex: C
//
// !!! This is an old routine which was exposed through STATS to "expert
// users".  Its purpose is to calculate the total amount of memory currently
// in use by Flexes, but it could also print out a breakdown of categories.
//
REBU64 Inspect_Flex(bool show)
{
    Count segs = 0;
    Count tot = 0;
    Count blks = 0;
    Count strs = 0;
    Count odds = 0;
    Count fre = 0;

    Size seg_size = 0;
    Size str_size = 0;
    Size blk_size = 0;
    Size odd_size = 0;

    Size tot_size = 0;

    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next, seg_size += seg->size, ++segs) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE) {
                ++fre;
                continue;
            }

            ++tot;

            if (unit[0] & BASE_BYTEMASK_0x08_CELL)
                continue;

            Flex* f = u_cast(Flex*, unit);

            if (Get_Stub_Flag(f, DYNAMIC))
                tot_size += Flex_Total(f);

            if (Stub_Holds_Cells(f)) {
                blks++;
                if (Get_Stub_Flag(f, DYNAMIC))
                    blk_size += Flex_Total(f);
            }
            else if (Stub_Holds_Bytes(f)) {
                strs++;
                if (Get_Stub_Flag(f, DYNAMIC))
                    str_size += Flex_Total(f);
            }
            else {
                odds++;
                if (Get_Stub_Flag(f, DYNAMIC))
                    odd_size += Flex_Total(f);
            }
        }
    }

    // Size up unused memory:
    //
    REBU64 fre_size = 0;
    PoolId pool_id;
    for (pool_id = 0; pool_id != SYSTEM_POOL; pool_id++) {
        fre_size += g_mem.pools[pool_id].free * g_mem.pools[pool_id].wide;
    }

    if (show) {
        printf("Flex Memory Info:\n");
        printf("  Cell size = %lu\n", cast(unsigned long, sizeof(Cell)));
        printf("  Stub size = %lu\n", cast(unsigned long, sizeof(Stub)));
        printf(
            "  %-6d segs = %-7d bytes - headers\n",
            cast(int, segs),
            cast(int, seg_size)
        );
        printf(
            "  %-6d blks = %-7d bytes - blocks\n",
            cast(int, blks),
            cast(int, blk_size)
        );
        printf(
            "  %-6d strs = %-7d bytes - byte strings\n",
            cast(int, strs),
            cast(int, str_size)
        );
        printf(
            "  %-6d odds = %-7d bytes - odd Flexes\n",
            cast(int, odds),
            cast(int, odd_size)
        );
        printf(
            "  %-6d used = %lu bytes - total used\n",
            cast(int, tot),
            cast(unsigned long, tot_size)
        );
        printf("  %lu free headers\n", cast(unsigned long, fre));
        printf("  %lu bytes base-space\n", cast(unsigned long, fre_size));
        printf("\n");
    }

    fflush(stdout);

    return tot_size;
}

#endif
