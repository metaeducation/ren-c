//
//  File: %m-pools.c
//  Summary: "memory allocation pool management"
//  Section: memory
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// allocations of series.
//
// Unless they've been explicitly marked as fixed-size, series have a dynamic
// component.  But they also have a fixed-size component that is allocated
// from a memory pool of other fixed-size things.  This is called the "Node"
// in both Rebol and Red terminology.  It is an item whose pointer is valid
// for the lifetime of the object, regardless of resizing.  This is where
// header information is stored, and pointers to these objects may be saved
// in cells; such that they are kept alive by the garbage collector.
//
// The more complicated thing to do memory pooling of is the variable-sized
// portion of a series (currently called the "series data")...as series sizes
// can vary widely.  But a trick Rebol has is that a series might be able to
// take advantage of being given back an allocation larger than requested.
// They can use it as reserved space for growth.
//
// (Typical models for implementation of things like C++'s std::vector do not
// reach below new[] or delete[]...which are generally implemented with malloc
// and free under the hood.  Their buffered additional capacity is done
// assuming the allocation they get is as big as they asked for...no more and
// no less.)
//
// !!! While the space usage is very optimized in this model, there was no
// consideration for intelligent thread safety for allocations and frees.
// So although code like `tcmalloc` might be slower and have more overhead,
// it does offer that advantage.
//
// R3-Alpha included some code to assist in debugging client code using series
// such as by initializing the memory to garbage values.  Given the existence
// of modern tools like Valgrind and Address Sanitizer, Ren-C instead has a
// mode in which pools are not used for data allocations, but going through
// malloc and free.  You can enable this by setting the environment variable
// R3_ALWAYS_MALLOC to 1.
//

#include "sys-core.h"
#include "sys-int-funcs.h"


//
//  Try_Alloc_Mem: C
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTE: Use Try_Alloc and Try_Alloc_N instead of Try_Alloc_Mem to ensure the
// memory matches the size for the type, and that the code builds as C++.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Try_Alloc_Core() is a basic memory allocator, which clients must call with
// the correct size of memory block to be freed.  This differs from malloc(),
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
// Finer-grained allocations are done with memory pooling.  But the blocks of
// memory used by the pools are still acquired using Try_Alloc_N and FREE_N,
// which are interfaces to the Try_Alloc_Core() and Free_Core() routines.
//
void *Try_Alloc_Core(size_t size)
{
    // notice memory usage limit exceeded *before* the allocation is performed

    g_mem.usage += size;
    if (g_mem.usage_limit and g_mem.usage > unwrap(g_mem.usage_limit)) {
        g_mem.usage -= size;
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

    // malloc() internally remembers the size of the allocation, and is hence
    // "overkill" for this operation.  Yet the current implementations on all
    // C platforms use malloc() and Free() anyway.

  #ifdef NDEBUG
    void *p = malloc(size);
  #else
    // Cache size at the head of the allocation in debug builds for checking.
    // Also catches Free() use with Try_Alloc_Core() instead of Free_Core().
    //
    // Use a 64-bit quantity to preserve DEBUG_MEMORY_ALIGN invariant.

    void *p_extra = malloc(size + ALIGN_SIZE);
    if (not p_extra) {
        g_mem.usage -= size;
        return nullptr;
    }
    *cast(REBI64*, p_extra) = size;
    void *p = cast(char*, p_extra) + ALIGN_SIZE;
  #endif

  #if DEBUG_MEMORY_ALIGN
    assert(i_cast(uintptr_t, p) % ALIGN_SIZE == 0);
  #endif

    return p;
}


//
//  Free_Mem: C
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTE: Instead of Free_Mem, use the FREE and FREE_N wrapper macros to ensure
// the memory block being freed matches the appropriate size for the type.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Free_Mem is a wrapper over Free(), that subtracts from a total count that
// Rebol can see how much memory was released.  This information assists in
// deciding when it is necessary to run a garbage collection, or when to
// impose a quota.
//
void Free_Core(void *mem, size_t size)
{
  #ifdef NDEBUG
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
    // Ren-C's pool, because it requires 2*sizeof(void*) for each node at the
    // minimum...because instead of just the freelist pointer, it has a
    // standardized header (0 when free).
    //
    // This is not a problem, since all such small strings would also need
    // Stubs...and Ren-C has a better answer to embed the payload directly
    // into the Stub when SERIES_FLAG_DYNAMIC is not set.  This wouldn't apply
    // if you were trying to do very small allocations of strings that did not
    // have associated Stubs...but those don't exist in the code.

    MOD_POOL( 1, 256),  // 9-16 (when sizeof(Cell) is 16)
    MOD_POOL( 2, 512),  // 17-32 - Small series (x 16)
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
    MOD_POOL(20,  32),  // 321 - Mid-size series (x 64)
    MOD_POOL(24,  16),  // 385
    MOD_POOL(28,  16),  // 449
    MOD_POOL(32,   8),  // 513

    DEF_POOL(MEM_BIG_SIZE,  16),    // 1K - Large series (x 1024)
    DEF_POOL(MEM_BIG_SIZE*2, 8),    // 2K
    DEF_POOL(MEM_BIG_SIZE*3, 4),    // 3K
    DEF_POOL(MEM_BIG_SIZE*4, 4),    // 4K

    DEF_POOL(sizeof(Series), 4096), // Series headers

  #if UNUSUAL_CELL_SIZE  // sizeof(Cell)*2 != sizeof(Stub)
    DEF_POOL(sizeof(Cell) * 2, 16),  // Pairings, PAIR_POOL
  #endif

    DEF_POOL(ALIGN(sizeof(Level), sizeof(REBI64)), 128),  // Levels
    DEF_POOL(ALIGN(sizeof(Feed), sizeof(REBI64)), 128),  // Feeds

    DEF_POOL(sizeof(REBI64), 1), // Just used for tracking main memory
};


//
//  Startup_Pools: C
//
// Initialize memory pool array.
//
void Startup_Pools(REBINT scale)
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

    g_mem.pools = Try_Alloc_N(Pool, MAX_POOLS);

    // Copy pool sizes to new pool structure:
    //
    REBLEN n;
    for (n = 0; n < MAX_POOLS; n++) {
        g_mem.pools[n].segments = nullptr;
        g_mem.pools[n].first = nullptr;
        g_mem.pools[n].last = nullptr;

        // A panic is used instead of an assert, since the debug sizes and
        // release sizes may be different...and both must be checked.
        //
      #if DEBUG_MEMORY_ALIGN
        if (Mem_Pool_Spec[n].wide % sizeof(REBI64) != 0)
            panic ("memory pool width is not 64-bit aligned");
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

    g_mem.pools_by_size = Try_Alloc_N(Byte, POOLS_BY_SIZE_LEN);

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
    assert(g_mem.series_memory == 0);
    assert(g_mem.series_made == 0);
    assert(g_mem.series_freed == 0);
    assert(g_mem.series_expanded == 0);
    assert(g_mem.blocks_made == 0);
    assert(g_mem.objects_made == 0);
  #endif

    g_mem.prior_expand = Try_Alloc_N(Series*, MAX_EXPAND_LIST);
    memset(g_mem.prior_expand, 0, sizeof(Series*) * MAX_EXPAND_LIST);
    g_mem.prior_expand[0] = (Series*)1;
}


//
//  Shutdown_Pools: C
//
// Release all segments in all pools, and the pools themselves.
//
void Shutdown_Pools(void)
{
  #if !defined(NDEBUG)
  blockscope {
    Count num_leaks = 0;
    Series* leaked = nullptr;
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for(; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            ++num_leaks;

            Series* s = x_cast(Series*, unit);
            if (Is_Node_Managed(s)) {
                printf("MANAGED series leak, this REALLY shouldn't happen\n");
                leaked = s;  // report a managed one if found
            }
            else if (not leaked) {
                leaked = s;  // first one found
            }
            else if (
                Not_Node_Managed(leaked)
                and leaked->tick < s->tick
            ){
                leaked = s;  // update if earlier tick reference
            }
        }
    }
    if (leaked) {
        printf("%d leaked series...panic()ing one\n", cast(int, num_leaks));
        panic (leaked);
    }
  }
  #endif

    PoolId pool_id;
    for (pool_id = 0; pool_id < MAX_POOLS; ++pool_id) {
        Pool *pool = &g_mem.pools[pool_id];
        Size mem_size = (
            pool->wide * pool->num_units_per_segment + sizeof(Segment)
        );

        Segment* seg = pool->segments;
        while (seg) {
            Segment* next = seg->next;
            Free_N(char, mem_size, cast(char*, seg));
            seg = next;
        }
    }

    Free_N(Pool, MAX_POOLS, g_mem.pools);

    Free_N(Byte, POOLS_BY_SIZE_LEN, g_mem.pools_by_size);

    // !!! Revisit location (just has to be after all series are freed)
    Free_N(Series*, MAX_EXPAND_LIST, g_mem.prior_expand);

  #if DEBUG_COLLECT_STATS
    g_mem.series_memory = 0;
    g_mem.series_made = 0;
    g_mem.series_freed = 0;
    g_mem.series_expanded = 0;
    g_mem.blocks_made = 0;
    g_mem.objects_made = 0;
  #endif

  #if !defined(NDEBUG)
    if (g_mem.usage != 0) {
        //
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
    }
  #endif
}


//
//  Fill_Pool: C
//
// Allocate memory for a pool.  The amount allocated will be determined from
// the size and units specified when the pool header was created.  The nodes
// of the pool are linked to the free list.
//
bool Try_Fill_Pool(Pool* pool)
{
    REBLEN num_units = pool->num_units_per_segment;
    REBLEN mem_size = pool->wide * num_units + sizeof(Segment);

    Segment* seg = cast(Segment*, Try_Alloc_N(char, mem_size));
    if (seg == nullptr)
        return false;

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
    return true;
}


#if DEBUG_FANCY_PANIC

//
//  Try_Find_Containing_Node_Debug: C
//
// This debug-build-only routine will look to see if it can find what series
// a data pointer lives in.  It returns NULL if it can't find one.  It's very
// slow, because it has to look at all the series.  Use sparingly!
//
Node* Try_Find_Containing_Node_Debug(const void *p)
{
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            if (unit[0] & NODE_BYTEMASK_0x01_CELL) {  // a "pairing"
                Value* pairing = x_cast(Value*, unit);
                if (p >= cast(void*, pairing) and p < cast(void*, pairing + 1))
                    return pairing;  // this Stub is actually Cell[2]
                continue;
            }

            Series* s = x_cast(Series*, unit);
            if (Not_Series_Flag(s, DYNAMIC)) {
                if (
                    p >= cast(void*, &s->content)
                    && p < cast(void*, &s->content + 1)
                ){
                    return s;
                }
                continue;
            }

            if (p < cast(void*,
                s->content.dynamic.data - (Series_Wide(s) * Series_Bias(s))
            )) {
                // The memory lies before the series data allocation.
                //
                continue;
            }

            if (p >= cast(void*, s->content.dynamic.data
                + (Series_Wide(s) * Series_Rest(s))
            )) {
                // The memory lies after the series capacity.
                //
                continue;
            }

            // We now have a bad condition, in that the pointer is known to
            // be resident in a series data allocation.  But it could be doubly
            // bad if the pointer is in the extra head or tail capacity,
            // because that's effectively free data.  Since we're already
            // going to be asserting if we get here, go ahead and pay to
            // check if either of those is the case.

            if (p < cast(void*, s->content.dynamic.data)) {
                printf("Pointer found in freed head capacity of series\n");
                fflush(stdout);
                return s;
            }

            if (p >= cast(void*,
                s->content.dynamic.data
                + (Series_Wide(s) * Series_Used(s))
            )) {
                printf("Pointer found in freed tail capacity of series\n");
                fflush(stdout);
                return s;
            }

            return s;
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
// means the GC manuals list needs to be Node* and not just Series*.
//
Value* Alloc_Pairing(Flags flags) {
    assert(flags == 0 or flags == NODE_FLAG_MANAGED);
    Value* paired = cast(Value*, Alloc_Pooled(PAIR_POOL));  // 2x cell size

    Erase_Cell(paired);
    Erase_Cell(Pairing_Second(paired));

    if (flags)
        Manage_Pairing(paired);
    return paired;
}


//
//  Copy_Pairing: C
//
Value* Copy_Pairing(const Value* paired, Flags flags) {
    Value* copy = Alloc_Pairing(flags);

    Copy_Cell(copy, paired);
    Copy_Cell(Pairing_Second(copy), Pairing_Second(paired));

    return copy;
}


//
//  Manage_Pairing: C
//
// The paired management status is handled by bits directly in the first (the
// paired value) header.
//
void Manage_Pairing(Cell* paired) {
    assert(Not_Node_Managed(paired));
    Set_Node_Managed_Bit(paired);
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
void Unmanage_Pairing(Value* paired) {
    assert(Is_Node_Managed(paired));
    Clear_Node_Managed_Bit(paired);
}


//
//  Free_Pairing: C
//
void Free_Pairing(Cell* paired) {
    assert(Not_Node_Managed(paired));
    Free_Pooled(STUB_POOL, paired);

  #if DEBUG_COUNT_TICKS
    //
    // This wasn't actually a Series, but poke the tick where the node was
    // freed into the memory spot so panic finds it.
    //
    x_cast(Stub*, paired)->tick = TG_tick;
  #endif
}


//
//  Free_Unbiased_Series_Data: C
//
// Routines that are part of the core series implementation
// call this, including Expand_Series.  It requires a low-level
// awareness that the series data pointer cannot be freed
// without subtracting out the "biasing" which skips the pointer
// ahead to account for unused capacity at the head of the
// allocation.  They also must know the total allocation size.
//
// !!! Ideally this wouldn't be exported, but series data is now used to hold
// function arguments.
//
void Free_Unbiased_Series_Data(char *unbiased, Size total)
{
    PoolId pool_id = Pool_Id_For_Size(total);
    Pool* pool;

    if (pool_id < SYSTEM_POOL) {
        //
        // The series data does not honor "node protocol" when it is in use
        // The pools are not swept the way the Stub pool is, so only the
        // free nodes have significance to their headers.
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
        Free_N(char, total, unbiased);
        g_mem.pools[SYSTEM_POOL].has -= total;
        g_mem.pools[SYSTEM_POOL].free++;
    }
}


//
//  Expand_Series: C
//
// Expand a series at a particular index point by `delta` units.
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
// If the series has enough space within it, then it will be used,
// otherwise the series data will be reallocated.
//
// When expanded at the head, if bias space is available, it will
// be used (if it provides enough space).
//
// !!! It seems the original intent of this routine was
// to be used with a group of other routines that were "Noterm"
// and do not terminate.  However, Expand_Series assumed that
// the capacity of the original series was at least (tail + 1)
// elements, and would include the terminator when "sliding"
// the data in the update.  This makes the other Noterm routines
// seem a bit high cost for their benefit.  If this were to be
// changed to Expand_Series_Noterm it would put more burden
// on the clients...for a *potential* benefit in being able to
// write just an END marker into the terminal cell vs. copying
// the entire value cell.  (Of course, with a good memcpy it
// might be an irrelevant difference.)  For the moment we reverse
// the burden by enforcing the assumption that the incoming series
// was already terminated.  That way our "slide" of the data via
// memcpy will keep it terminated.
//
// WARNING: never use direct pointers into the series data, as the
// series data can be relocated in memory.
//
void Expand_Series(Series* s, REBLEN index, REBLEN delta)
{
    Assert_Series_Term_If_Needed(s);

    assert(index <= Series_Used(s));
    if (delta & 0x80000000)
        fail (Error_Index_Out_Of_Range_Raw()); // 2GB max

    if (delta == 0)
        return;

    REBLEN used_old = Series_Used(s);

    Byte wide = Series_Wide(s);

    const bool was_dynamic = Get_Series_Flag(s, DYNAMIC);

    if (was_dynamic and index == 0 and Series_Bias(s) >= delta) {

    //=//// HEAD INSERTION OPTIMIZATION ///////////////////////////////////=//

        s->content.dynamic.data -= wide * delta;
        s->content.dynamic.used += delta;
        s->content.dynamic.rest += delta;
        SER_SUB_BIAS(s, delta);

      #if !defined(NDEBUG)
        if (Is_Series_Array(s)) {
            //
            // When the bias region was marked, it was made "unsettable" if
            // this was a debug build.  Now that the memory is included in
            // the array again, we want it to be "settable".
            //
            // !!! The unsettable feature is currently not implemented,
            // but when it is this will be useful.
            //
            for (index = 0; index < delta; index++)
                Erase_Cell(Array_At(x_cast(Array*, s), index));
        }
      #endif
        Assert_Series_Term_If_Needed(s);
        return;
    }

    // Width adjusted variables:

    REBLEN start = index * wide;
    REBLEN extra = delta * wide;
    REBLEN size = Series_Used(s) * wide;

    // + wide for terminator
    if ((size + extra + wide) <= Series_Rest(s) * Series_Wide(s)) {
        //
        // No expansion was needed.  Slide data down if necessary.  Note that
        // the tail is not moved and instead the termination is done
        // separately with TERM_SERIES (in case it reaches an implicit
        // termination that is not a full-sized cell).

        UNPOISON_SERIES_TAIL(s);
        memmove(
            Series_Data(s) + start + extra,
            Series_Data(s) + start,
            size - start
        );
        Set_Series_Used_Internal(s, used_old + delta);
        POISON_SERIES_TAIL(s);

        assert(
            not was_dynamic or (
                Series_Total(s) > ((Series_Used(s) + Series_Bias(s)) * wide)
            )
        );

      #if !defined(NDEBUG)
        if (Is_Series_Array(s)) {
            //
            // The opened up area needs to be set to "settable" in the
            // debug build.  This takes care of making "unsettable" values
            // settable (if part of the expansion is in what was formerly the
            // ->rest), as well as just making sure old data which was in
            // the expanded region doesn't get left over on accident.
            //
            while (delta != 0) {
                --delta;
                Erase_Cell(Array_At(x_cast(Array*, s), index + delta));
            }
        }
      #endif
        return;
    }

//=//// INSUFFICIENT CAPACITY, NEW ALLOCATION REQUIRED ////////////////////=//

    if (Get_Series_Flag(s, FIXED_SIZE))
        fail (Error_Locked_Series_Raw());

  #if DEBUG
    if (g_mem.watch_expand) {
        printf(
            "Expand %p wide: %d tail: %d delta: %d\n",
            cast(void*, s),
            cast(int, wide),
            cast(int, used_old),
            cast(int, delta)
        );
        fflush(stdout);
    }
  #endif

    // Have we recently expanded the same series?

    REBLEN x = 1;
    REBLEN n_available = 0;
    REBLEN n_found;
    for (n_found = 0; n_found < MAX_EXPAND_LIST; n_found++) {
        if (g_mem.prior_expand[n_found] == s) {
            x = Series_Used(s) + delta + 1; // Double the size
            break;
        }
        if (!g_mem.prior_expand[n_found])
            n_available = n_found;
    }

  #if DEBUG
    if (g_mem.watch_expand)
        printf("Expand: %d\n", cast(int, Series_Used(s) + delta + 1));
  #endif

    // !!! The protocol for doing new allocations currently mandates that the
    // dynamic content area be cleared out.  But the data lives in the content
    // area if there's no dynamic portion.  The in-Stub content has to be
    // copied to preserve the data.  This could be generalized so that the
    // routines that do calculations operate on the content as a whole, not
    // the Stub, so the content is extracted either way.
    //
    union StubContentUnion content_old;
    REBINT bias_old;
    REBLEN size_old;
    char *data_old;
    if (was_dynamic) {
        data_old = s->content.dynamic.data;
        bias_old = Series_Bias(s);
        size_old = Series_Total(s);
    }
    else {
        Mem_Copy(&content_old, &s->content, sizeof(union StubContentUnion));
        data_old = cast(char*, &content_old);
    }

    // The new series will *always* be dynamic, because it would not be
    // expanding if a fixed size allocation was sufficient.

    Set_Series_Flag(s, DYNAMIC);
    Set_Series_Flag(s, POWER_OF_2);
    if (not Did_Series_Data_Alloc(s, used_old + delta + x))
        fail (Error_No_Memory((used_old + delta + x) * wide));

    assert(Get_Series_Flag(s, DYNAMIC));
    if (Is_Series_Array(s))
        Prep_Array(x_cast(Array*, s), 0); // capacity doesn't matter to prep

    // If necessary, add series to the recently expanded list
    //
    if (n_found >= MAX_EXPAND_LIST)
        g_mem.prior_expand[n_available] = s;

    // Copy the series up to the expansion point
    //
    memcpy(s->content.dynamic.data, data_old, start);

    // Copy the series after the expansion point.
    //
    memcpy(
        s->content.dynamic.data + start + extra,
        data_old + start,
        size - start
    );
    s->content.dynamic.used = used_old + delta;

    if (was_dynamic) {
        //
        // We have to de-bias the data pointer before we can free it.
        //
        assert(Series_Bias(s) == 0); // should be reset
        Free_Unbiased_Series_Data(data_old - (wide * bias_old), size_old);
    }

  #if DEBUG_COLLECT_STATS
    g_mem.series_expanded += 1;
  #endif

    assert(Not_Node_Marked(s));
    Term_Series_If_Necessary(s);  // code will not copy terminator over
}


//
//  Swap_Series_Content: C
//
// Retain the identity of the two series but do a low-level swap of their
// content with each other.
//
// It does not swap flags, e.g. whether something is managed or a paramlist
// or anything of that nature.  Those are properties that cannot change, due
// to the expectations of things that link to the series.  Hence this is
// a risky operation that should only be called when the client is sure it
// is safe to do so (more asserts would probably help).
//
void Swap_Series_Content(Series* a, Series* b)
{
    // Can't think of any reasonable case for mutating an array node into a
    // non-array or vice versa.  Cases haven't come up for swapping series
    // of varying width, either.
    //
    assert(Is_Series_Array(a) == Is_Series_Array(b));
    assert(Series_Wide(a) == Series_Wide(b));

    bool a_dynamic = Get_Series_Flag(a, DYNAMIC);
    if (Get_Series_Flag(b, DYNAMIC))
        Set_Series_Flag(a, DYNAMIC);
    else
        Clear_Series_Flag(a, DYNAMIC);
    if (a_dynamic)
        Set_Series_Flag(b, DYNAMIC);
    else
        Clear_Series_Flag(b, DYNAMIC);

    Byte a_len = USED_BYTE(a);  // unused (for now) when SERIES_FLAG_DYNAMIC()
    USED_BYTE(a) = USED_BYTE(b);
    USED_BYTE(b) = a_len;

    union StubContentUnion a_content;
    Mem_Copy(&a_content, &a->content, sizeof(union StubContentUnion));
    Mem_Copy(&a->content, &b->content, sizeof(union StubContentUnion));
    Mem_Copy(&b->content, &a_content, sizeof(union StubContentUnion));

    union StubMiscUnion a_misc = a->misc;
    a->misc = b->misc;
    b->misc = a_misc;

    union StubLinkUnion a_link = a->link;
    a->link = b->link;
    b->link = a_link;
}


//
//  swap-contents: native [
//
//  "Low-level operation for swapping the underlying data for two series"
//
//      return: [~]
//      series1 [any-series?]
//      series2 [any-series?]
//  ]
//
DECLARE_NATIVE(swap_contents)
{
    INCLUDE_PARAMS_OF_SWAP_CONTENTS;

    if (Any_Array(ARG(series1)) != Any_Array(ARG(series2)))
        fail ("Can only SWAP-CONTENTS of arrays with other arrays");

    // !!! This is a conservative check, as some binaries could be swapped
    // with ANY-STRING?.  However, that would require checking that the
    // binary is valid UTF-8...mechanics that are available in AS TEXT! etc.
    // Let the user do their own aliasing for now, since the checks are
    // annoying to write.
    //
    if (Is_Binary(ARG(series1)) != Is_Binary(ARG(series2)))
        fail ("Can only SWAP-CONTENTS of binaries with other binaries");

    Series* s1 = Cell_Series_Ensure_Mutable(ARG(series1));
    Series* s2 = Cell_Series_Ensure_Mutable(ARG(series2));
    Swap_Series_Content(s1, s2);

    return TRASH;
}


//
//  Remake_Series: C
//
// Reallocate a series as a given maximum size.  Content in the retained
// portion of the length will be preserved if NODE_FLAG_NODE is passed in.
//
void Remake_Series(Series* s, REBLEN units, Flags flags)
{
    // !!! This routine is being scaled back in terms of what it's allowed to
    // do for the moment; so the method of passing in flags is a bit strange.
    //
    assert((flags & ~(NODE_FLAG_NODE | SERIES_FLAG_POWER_OF_2)) == 0);

    bool preserve = did (flags & NODE_FLAG_NODE);

    REBLEN used_old = Series_Used(s);
    Byte wide = Series_Wide(s);

    assert(Not_Series_Flag(s, FIXED_SIZE));

    bool was_dynamic = Get_Series_Flag(s, DYNAMIC);

    REBINT bias_old;
    REBINT size_old;

    // Extract the data pointer to take responsibility for it.  (The pointer
    // may have already been extracted if the caller is doing their own
    // updating preservation.)

    char *data_old;
    union StubContentUnion content_old;
    if (was_dynamic) {
        assert(s->content.dynamic.data != NULL);
        data_old = s->content.dynamic.data;
        bias_old = Series_Bias(s);
        size_old = Series_Total(s);
    }
    else {
        Mem_Copy(&content_old, &s->content, sizeof(union StubContentUnion));
        data_old = cast(char*, &content_old);
    }

    s->leader.bits |= flags;

    // !!! Currently the remake won't make a series that fits entirely in
    // a Stub (so always SERIES_FLAG_DYNAMIC).  All series code needs a general
    // audit, so that should be one of the things considered.

    Set_Series_Flag(s, DYNAMIC);
    if (not Did_Series_Data_Alloc(s, units + 1)) {
        // Put series back how it was (there may be extant references)
        s->content.dynamic.data = cast(char*, data_old);

        fail (Error_No_Memory((units + 1) * wide));
    }
    assert(Get_Series_Flag(s, DYNAMIC));
    if (Is_Series_Array(s))
        Prep_Array(x_cast(Array*, s), 0); // capacity doesn't matter to prep

    if (preserve) {
        // Preserve as much data as possible (if it was requested, some
        // operations may extract the data pointer ahead of time and do this
        // more selectively)

        s->content.dynamic.used = MIN(used_old, units);
        memcpy(
            s->content.dynamic.data,
            data_old,
            s->content.dynamic.used * wide
        );
    } else
        s->content.dynamic.used = 0;

  #if DEBUG_UTF8_EVERYWHERE
    if (Is_String_NonSymbol(s)) {
        s->misc.length = 0xDECAFBAD;
        Touch_Stub_If_Debug(s);
    }
  #endif

    if (was_dynamic)
        Free_Unbiased_Series_Data(data_old - (wide * bias_old), size_old);
}


//
//  Decay_Series: C
//
Stub *Decay_Series(Series* s)
{
    Assert_Node_Accessible(s);

    switch (Series_Flavor(s)) {
      case FLAVOR_STRING:
        Free_Bookmarks_Maybe_Null(cast(String*, s));
        break;

      case FLAVOR_SYMBOL:
        GC_Kill_Interning(cast(Symbol*, s));  // special handling adjust canons
        break;

      case FLAVOR_PATCH: {
        //
        // This is a variable definition for a module.  It is a member of a
        // circularly-linked list that goes through the other variables of the
        // same name in other modules...with the name itself as a symbol
        // being in that circular list.  Remove this patch from that list.
        //
        Stub* temp = MISC(PatchHitch, s);
        while (node_MISC(Hitch, temp) != s) {
            temp = cast(Stub*, node_MISC(Hitch, temp));
            assert(IS_PATCH(temp) or Is_String_Symbol(temp));
        }
        node_MISC(Hitch, temp) = node_MISC(Hitch, s);
        break; }

      case FLAVOR_LET:
        break;

      case FLAVOR_USE:
        //
        // At one point, this would remove the USE from a linked list of
        // "variants" which were other examples of the USE.  That feature was
        // removed for the moment.
        //
        break;

      case FLAVOR_HANDLE: {
        Value* v = Stub_Cell(s);
        assert(Cell_Heart_Unchecked(v) == REB_HANDLE);

        // Some handles use the managed form just because they want changes to
        // the pointer in one instance to be seen by other instances...there
        // may be no cleaner function.
        //
        if (s->misc.cleaner)
            (s->misc.cleaner)(v);
        break; }

      default:
        break;
    }

    // Remove series from expansion list, if found:
    REBLEN n;
    for (n = 1; n < MAX_EXPAND_LIST; n++) {
        if (g_mem.prior_expand[n] == s) g_mem.prior_expand[n] = 0;
    }

    if (Get_Series_Flag(s, DYNAMIC)) {
        Byte wide = Series_Wide(s);
        REBLEN bias = Series_Bias(s);
        REBLEN total = (bias + Series_Rest(s)) * wide;
        char *unbiased = s->content.dynamic.data - (wide * bias);

        // !!! Contexts and actions keep their archetypes, for now, in the
        // now collapsed node.  For FRAME! this means holding onto the binding
        // which winds up being used in Derelativize().  See SPC_BINDING.
        // Preserving ACTION!'s archetype is speculative--to point out the
        // possibility exists for the other array with a "canon" [0]
        //
        if (IS_VARLIST(s) or IS_DETAILS(s))
            Mem_Copy(
                &s->content.fixed.cell,
                Array_Head(c_cast(Array*, s)),
                sizeof(Cell)
            );

        Free_Unbiased_Series_Data(unbiased, total);

        // !!! This indicates reclaiming of the space, not for the series
        // nodes themselves...have they never been accounted for, e.g. in
        // R3-Alpha?  If not, they should be...additional sizeof(Stub),
        // also tracking overhead for that.  Review the question of how
        // the GC watermarks interact with Try_Alloc_Core() and the "higher
        // level" allocations.

        int tmp;
        g_gc.depletion = REB_I32_ADD_OF(g_gc.depletion, total, &tmp)
            ? INT32_MAX
            : tmp;
    }

    Set_Series_Inaccessible(s);
    return s;
}


//
//  GC_Kill_Stub: C
//
// Only the garbage collector should be calling this routine.
// It frees a series even though it is under GC management,
// because the GC has figured out no references exist.
//
void GC_Kill_Stub(Stub* s)
{
  #if !defined(NDEBUG)
    if (NODE_BYTE(s) == FREE_POOLUNIT_BYTE) {
        printf("Killing already deallocated stub.\n");
        panic (s);
    }
  #endif

    assert(Not_Node_Accessible(s));  // must Decay_Series() first

    // By default the series is touched so its tick reflects the tick that
    // freed it.  If you need to know the tick where it was allocated, then
    // comment this out so it remains that way.
    //
    Touch_Stub_If_Debug(s);

  #if !defined(NDEBUG)
    FreeCorrupt_Pointer_Debug(s->info.node);
    // The spot LINK occupies will be used by Free_Pooled() to link the freelist
    FreeCorrupt_Pointer_Debug(s->misc.corrupt);
  #endif

    Free_Pooled(STUB_POOL, s);

    if (g_gc.depletion > 0)
        CLR_SIGNAL(SIG_RECYCLE);  // Enough space that requested GC can cancel

  #if DEBUG_COLLECT_STATS
    g_mem.series_freed += 1;
  #endif
}


//
//  GC_Kill_Series: C
//
// Only the garbage collector should be calling this routine.
// It frees a series even though it is under GC management,
// because the GC has figured out no references exist.
//
void GC_Kill_Series(Series* s) {
    Assert_Node_Accessible(s);
    GC_Kill_Stub(Decay_Series(s));
}


//
//  Free_Unmanaged_Series: C
//
// Returns series node and data to memory pools for reuse.
//
void Free_Unmanaged_Series(Series* s)
{
  #if !defined(NDEBUG)
    if (Is_Node_Free(s)) {
        printf("Trying to Free_Umanaged_Series() on already freed series\n");
        panic (s); // erroring here helps not conflate with tracking problems
    }

    if (Is_Node_Managed(s)) {
        printf("Trying to Free_Unmanaged_Series() on a GC-managed series\n");
        panic (s);
    }
  #endif

    Untrack_Manual_Series(s);
    GC_Kill_Series(s); // with bookkeeping done, use same routine as GC
}


#if !defined(NDEBUG)

//
//  Assert_Pointer_Detection_Working: C
//
// Check the conditions that are required for Detect_Rebol_Pointer() and
// to work, and throw some sample cases at it to make sure they detect right.
//
void Assert_Pointer_Detection_Working(void)
{
    uintptr_t cell_flag = NODE_FLAG_CELL;
    assert(FIRST_BYTE(&cell_flag) == 0x01);
    uintptr_t protected_flag = CELL_FLAG_PROTECTED;
    assert(FOURTH_BYTE(&protected_flag) == 0x80);

    assert(Detect_Rebol_Pointer("") == DETECTED_AS_UTF8);
    assert(Detect_Rebol_Pointer("asdf") == DETECTED_AS_UTF8);

    assert(Detect_Rebol_Pointer(EMPTY_ARRAY) == DETECTED_AS_SERIES);
    assert(Detect_Rebol_Pointer(Lib(BLANK)) == DETECTED_AS_CELL);

    // A cell with NODE_FLAG_FREE will appear to be UTF-8.  Be sure not to
    // pass such cells to the API, as Detect_Rebol_Pointer() will be wrong!
    //
    DECLARE_ATOM (stale_cell);
    stale_cell->header.bits =
        NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
        | FLAG_HEART_BYTE(REB_BLANK);
    assert(Detect_Rebol_Pointer(WRITABLE(stale_cell)) == DETECTED_AS_UTF8);

    assert(Detect_Rebol_Pointer(rebEND) == DETECTED_AS_END);

    Binary* bin = Make_Series(Binary, 1, FLAG_FLAVOR(BINARY));
    assert(Detect_Rebol_Pointer(bin) == DETECTED_AS_SERIES);
    Free_Unmanaged_Series(bin);
}


//
//  Check_Memory_Debug: C
//
// Traverse the free lists of all pools -- just to prove we can.
//
// Note: This was useful in R3-Alpha for finding corruption from bad memory
// writes, because a write past the end of a node destroys the pointer for the
// next free area.  The Always_Malloc option for Ren-C leverages the faster
// checking built into Valgrind or Address Sanitizer for the same problem.
// However, a call to this is kept in the debug build on init and shutdown
// just to keep it working as a sanity check.
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

            if (unit[0] & NODE_BYTEMASK_0x01_CELL)
                continue; // a pairing

            Series* s = x_cast(Series*, unit);
            if (Not_Series_Flag(s, DYNAMIC))
                continue; // data lives in the series node itself

            if (Series_Rest(s) == 0)
                panic (s); // zero size allocations not legal

            PoolId pool_id = Pool_Id_For_Size(Series_Total(s));
            if (pool_id >= STUB_POOL)
                continue; // size doesn't match a known pool

            if (g_mem.pools[pool_id].wide < Series_Total(s))
                panic (s);
        }
    }

    Count total_free_nodes = 0;

    PoolId pool_id;
    for (pool_id = 0; pool_id != SYSTEM_POOL; pool_id++) {
        Count pool_free_nodes = 0;

        PoolUnit* unit = g_mem.pools[pool_id].first;
        for (; unit != nullptr; unit = unit->next_if_free) {
            assert(FIRST_BYTE(unit) == FREE_POOLUNIT_BYTE);

            ++pool_free_nodes;

            bool found = false;
            seg = g_mem.pools[pool_id].segments;
            for (; seg != nullptr; seg = seg->next) {
                if (
                    cast(Byte*, unit) > cast(Byte*, seg)
                    and (cast(Byte*, unit) < cast(Byte*, seg) + seg->size)
                ){
                    if (found) {
                        printf("unit belongs to more than one segment\n");
                        panic (unit);
                    }

                    found = true;
                }
            }

            if (not found) {
                printf("unit does not belong to one of the pool's segments\n");
                panic (unit);
            }
        }

        if (g_mem.pools[pool_id].free != pool_free_nodes)
            panic ("actual free unit count does not agree with pool header");

        total_free_nodes += pool_free_nodes;
    }

    return total_free_nodes;
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

            if (unit[0] & NODE_BYTEMASK_0x01_CELL)  // a pairing
                continue;

            Series* s = x_cast(Series*, unit);
            if (Series_Wide(s) == wide) {
                ++count;
                printf(
                    "%3d %4d %4d\n",
                    cast(int, count),
                    cast(int, Series_Used(s)),
                    cast(int, Series_Rest(s))
                );
            }
            fflush(stdout);
        }
    }
}


//
//  Dump_Series_In_Pool: C
//
// Dump all series in pool @pool_id, UNLIMITED (-1) for all pools
//
void Dump_Series_In_Pool(PoolId pool_id)
{
    Segment* seg = g_mem.pools[STUB_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[STUB_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += sizeof(Stub)) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            if (unit[0] & NODE_BYTEMASK_0x01_CELL)
                continue;  // pairing

            Series* s = x_cast(Series*, unit);
            if (
                pool_id == UNLIMITED
                or (
                    Get_Series_Flag(s, DYNAMIC)
                    and pool_id == Pool_Id_For_Size(Series_Total(s))
                )
            ){
                Dump_Series(s, "Dump_Series_In_Pool");
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
//  Inspect_Series: C
//
// !!! This is an old routine which was exposed through STATS to "expert
// users".  Its purpose is to calculate the total amount of memory currently
// in use by series, but it could also print out a breakdown of categories.
//
REBU64 Inspect_Series(bool show)
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

            if (unit[0] & NODE_BYTEMASK_0x01_CELL)
                continue;

            Series* s = x_cast(Series*, unit);

            if (Get_Series_Flag(s, DYNAMIC))
                tot_size += Series_Total(s);

            if (Is_Series_Array(s)) {
                blks++;
                if (Get_Series_Flag(s, DYNAMIC))
                    blk_size += Series_Total(s);
            }
            else if (Series_Wide(s) == 1) {
                strs++;
                if (Get_Series_Flag(s, DYNAMIC))
                    str_size += Series_Total(s);
            }
            else if (Series_Wide(s) != 0) {
                odds++;
                if (Get_Series_Flag(s, DYNAMIC))
                    odd_size += Series_Total(s);
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
        printf("Series Memory Info:\n");
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
            "  %-6d odds = %-7d bytes - odd series\n",
            cast(int, odds),
            cast(int, odd_size)
        );
        printf(
            "  %-6d used = %lu bytes - total used\n",
            cast(int, tot),
            cast(unsigned long, tot_size)
        );
        printf("  %lu free headers\n", cast(unsigned long, fre));
        printf("  %lu bytes node-space\n", cast(unsigned long, fre_size));
        printf("\n");
    }

    fflush(stdout);

    return tot_size;
}

#endif
