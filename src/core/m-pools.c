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
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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
// A point of Rebol's design was to remain small and solve its domain without
// relying on a lot of abstraction.  Its memory-management was thus focused on
// staying low-level...and being able to do efficient and lightweight
// allocations of series.
//
// Unless they've been explicitly marked as fixed-size, series have a dynamic
// component.  But they also have a fixed-size component that is allocated
// from a memory pool of other fixed-size things.  This is called the "Base"
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
//  Alloc_Mem: C
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTE: Use the ALLOC and ALLOC_N macros instead of Alloc_Mem to ensure the
// memory matches the size for the type, and that the code builds as C++.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Alloc_Mem is a basic memory allocator, which clients must call with the
// correct size of memory block to be freed.  This differs from malloc(),
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
// memory used by the pools are still acquired using ALLOC_N and FREE_N, which
// are interfaces to this routine.
//
void *Alloc_Mem(size_t size)
{
    // Trap memory usage limit *before* the allocation is performed

    PG_Mem_Usage += size;

    // malloc() internally remembers the size of the allocation, and is hence
    // "overkill" for this operation.  Yet the current implementations on all
    // C platforms use malloc() and free() anyway.

  #if NO_RUNTIME_CHECKS
    void *p = malloc(size);
  #else
    // Cache size at the head of the allocation in debug builds for checking.
    // Also catches free() use with Alloc_Mem() instead of Free_Mem().
    //
    // Use a 64-bit quantity to preserve DEBUG_MEMORY_ALIGNMENT invariant.

    void *p_extra = malloc(size + sizeof(REBI64));
    if (p_extra == nullptr)
        return nullptr;
    *cast(REBI64 *, p_extra) = size;
    void *p = cast(char*, p_extra) + sizeof(REBI64);
  #endif

  #if DEBUG_MEMORY_ALIGNMENT
    assert(i_cast(uintptr_t, p) % sizeof(REBI64) == 0);
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
// Free_Mem is a wrapper over free(), that subtracts from a total count that
// Rebol can see how much memory was released.  This information assists in
// deciding when it is necessary to run a garbage collection, or when to
// impose a quota.
//
void Free_Mem(void *mem, size_t size)
{
  #if NO_RUNTIME_CHECKS
    free(mem);
  #else
    assert(mem != nullptr);
    char *ptr = cast(char *, mem) - sizeof(REBI64);
    assert(*cast(REBI64*, ptr) == cast(REBI64, size));
    free(ptr);
  #endif

    PG_Mem_Usage -= size;
}


/***********************************************************************
**
**  MEMORY POOLS
**
**      Memory management operates off an array of pools, the first
**      group of which are fixed size (so require no compaction).
**
***********************************************************************/
const REBPOOLSPEC Mem_Pool_Spec[MAX_POOLS] =
{
    // R3-Alpha had a "0-8 small string pool".  e.g. a pool of allocations for
    // payloads 0 to 8 bytes in length.  These are not technically possible in
    // Ren-C's pool, because it requires 2*sizeof(void*) for each Unit at the
    // minimum...because instead of just the freelist pointer, it has a
    // standardized header (0 when free).
    //
    // This is not a problem, since all such small strings would also need
    // Stubs...and Ren-C has a better answer to embed the payload directly
    // into the Stub.  This wouldn't apply if you were trying to do very
    // small allocations of strings that did not have associated Stubs..
    // but those don't exist in the code.

    MOD_POOL( 1, 256),  // 9-16 (when Cell is 16)
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

    DEF_POOL(sizeof(Stub), 4096),  // Series stubs

  #if UNUSUAL_CELL_SIZE  // sizeof(Cell)*2 not sizeof(Stub)
    DEF_POOL(sizeof(Cell) * 2, 16), // Pairings, PAR_POOL
  #endif

    DEF_POOL(sizeof(REBI64), 1), // Just used for tracking main memory
};


//
//  Startup_Pools: C
//
// Initialize memory pool array.
//
void Startup_Pools(REBINT scale)
{
  #if DEBUG_HAS_ALWAYS_MALLOC
    const char *env_always_malloc = getenv("R3_ALWAYS_MALLOC");
    if (env_always_malloc and atoi(env_always_malloc) != 0)
        PG_Always_Malloc = true;
    if (PG_Always_Malloc) {
        printf(
            "**\n" \
            "** R3_ALWAYS_MALLOC is nonzero in environment variable!\n" \
            "** (Or hardcoded PG_Always_Malloc = true in initialization)\n" \
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

    Mem_Pools = ALLOC_N(REBPOL, MAX_POOLS);

    // Copy pool sizes to new pool structure:
    //
    REBLEN n;
    for (n = 0; n < MAX_POOLS; n++) {
        Mem_Pools[n].segs = nullptr;
        Mem_Pools[n].first = nullptr;
        Mem_Pools[n].last = nullptr;

        // A crash is used instead of an assert, since the debug sizes and
        // release sizes may be different...and both must be checked.
        //
      #if DEBUG_MEMORY_ALIGNMENT || 1
        if (Mem_Pool_Spec[n].wide % sizeof(REBI64) != 0)
            crash ("memory pool width is not 64-bit aligned");
      #endif

        Mem_Pools[n].wide = Mem_Pool_Spec[n].wide;

        Mem_Pools[n].units = (Mem_Pool_Spec[n].units * scale) / unscale;
        if (Mem_Pools[n].units < 2) Mem_Pools[n].units = 2;
        Mem_Pools[n].free = 0;
        Mem_Pools[n].has = 0;
    }

    // For pool lookup. Maps size to pool index. (See Find_Pool below)
    PG_Pool_Map = ALLOC_N(Byte, (4 * MEM_BIG_SIZE) + 1);

    // sizes 0 - 8 are pool 0
    for (n = 0; n <= 8; n++) PG_Pool_Map[n] = 0;
    for (; n <= 16 * MEM_MIN_SIZE; n++)
        PG_Pool_Map[n] = MEM_TINY_POOL + ((n-1) / MEM_MIN_SIZE);
    for (; n <= 32 * MEM_MIN_SIZE; n++)
        PG_Pool_Map[n] = MEM_SMALL_POOLS-4 + ((n-1) / (MEM_MIN_SIZE * 4));
    for (; n <=  4 * MEM_BIG_SIZE; n++)
        PG_Pool_Map[n] = MEM_MID_POOLS + ((n-1) / MEM_BIG_SIZE);

    // !!! Revisit where series init/shutdown goes when the code is more
    // organized to have some of the logic not in the pools file

  #if RUNTIME_CHECKS
    PG_Reb_Stats = ALLOC(REB_STATS);
  #endif

    // Manually allocated Flex that GC is not responsible for (unless a
    // trap occurs). Holds Flex pointers.
    //
    // As a trick to keep this Flex from trying to track itself, say it's
    // managed, then sneak the flag off.
    //
    GC_Manuals = Make_Flex_Core(15, sizeof(Flex* ), BASE_FLAG_MANAGED);
    Clear_Base_Managed_Bit(GC_Manuals);

    Prior_Expand = ALLOC_N(Flex*, MAX_EXPAND_LIST);
    CLEAR(Prior_Expand, sizeof(Flex*) * MAX_EXPAND_LIST);
    Prior_Expand[0] = (Flex*)1;
}


//
//  Shutdown_Pools: C
//
// Release all segments in all pools, and the pools themselves.
//
void Shutdown_Pools(void)
{
    // Can't use Free_Unmanaged_Flex() because GC_Manuals couldn't be put in
    // the manuals list...
    //
    GC_Kill_Flex(GC_Manuals);

  #if RUNTIME_CHECKS
    REBSEG *debug_seg = Mem_Pools[STUB_POOL].segs;
    for(; debug_seg != nullptr; debug_seg = debug_seg->next) {
        Flex* series = cast(Flex*, debug_seg + 1);
        REBLEN n;
        for (n = Mem_Pools[STUB_POOL].units; n > 0; n--, series++) {
            if (Not_Base_Readable(series))
                continue;

            assert(Not_Base_Managed(series));
            printf("At least one leaked series at shutdown...\n");
            crash (series);
        }
    }
  #endif

    REBLEN pool_num;
    for (pool_num = 0; pool_num < MAX_POOLS; pool_num++) {
        REBPOL *pool = &Mem_Pools[pool_num];
        REBLEN mem_size = pool->wide * pool->units + sizeof(REBSEG);

        REBSEG *seg = pool->segs;
        while (seg) {
            REBSEG *next;
            next = seg->next;
            FREE_N(char, mem_size, cast(char*, seg));
            seg = next;
        }
    }

    FREE_N(REBPOL, MAX_POOLS, Mem_Pools);

    FREE_N(Byte, (4 * MEM_BIG_SIZE) + 1, PG_Pool_Map);

    // !!! Revisit location (just has to be after all series are freed)
    FREE_N(Flex*, MAX_EXPAND_LIST, Prior_Expand);

  #if RUNTIME_CHECKS
    FREE(REB_STATS, PG_Reb_Stats);
  #endif

  #if RUNTIME_CHECKS
    if (PG_Mem_Usage != 0) {
        //
        // If using valgrind or address sanitizer, they can present more
        // information about leaks than just how much was leaked.  So don't
        // assert...exit normally so they go through their process of
        // presenting the leaks at program termination.
        //
        printf(
            "*** PG_Mem_Usage = %lu ***\n",
            cast(unsigned long, PG_Mem_Usage)
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
// the size and units specified when the pool header was created.  The units
// of the pool are linked to the free list.
//
void Fill_Pool(REBPOL *pool)
{
    REBLEN units = pool->units;
    REBLEN mem_size = pool->wide * units + sizeof(REBSEG);

    REBSEG *seg = cast(REBSEG *, ALLOC_N(char, mem_size));
    if (seg == nullptr) {
        crash ("Out of memory error during Fill_Pool()");

        // Rebol's safe handling of running out of memory was never really
        // articulated.  Yet it should be possible to run a panic()...at least
        // of a certain type...without allocating more memory.  (This probably
        // suggests a need for pre-creation of the out of memory objects,
        // as is done with the stack overflow error)
        //
        // panic (Error_No_Memory(mem_size));
    }

    seg->size = mem_size;
    seg->next = pool->segs;
    pool->segs = seg;
    pool->has += units;
    pool->free += units;

    // Add new units to the end of free list:

    // Can't use BAS() here because it tests for NOT(BASE_FLAG_UNREADABLE)
    //
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

        if (--units == 0) {
            unit->next_if_free = nullptr;
            break;
        }

        // Can't use BAS() here because it tests for BASE_FLAG_UNREADABLE
        //
        unit->next_if_free = cast(PoolUnit*, cast(Byte*, unit) + pool->wide);
        unit = unit->next_if_free;
    }

    pool->last = unit;
}


#if RUNTIME_CHECKS

//
//  Try_Find_Containing_Base_Debug: C
//
// This debug-build-only routine will look to see if it can find what series
// a data pointer lives in.  It returns nullptr if it can't find one.  It's very
// slow, because it has to look at all the series.  Use sparingly!
//
Base* Try_Find_Containing_Base_Debug(const void *p)
{
    REBSEG *seg;

    for (seg = Mem_Pools[STUB_POOL].segs; seg; seg = seg->next) {
        Flex* s = cast(Flex*, seg + 1);
        REBLEN n;
        for (n = Mem_Pools[STUB_POOL].units; n > 0; --n, ++s) {
            if (Not_Base_Readable(s))
                continue;

            if (s->header.bits & BASE_FLAG_CELL) {  // a "pairing"
                if (p >= cast(void*, s) and p < cast(void*, s + 1))
                    return s;  // Stub slots are (sizeof(Cell) * 2)
                continue;
            }

            if (not Is_Flex_Dynamic(s)) {
                if (
                    p >= cast(void*, &s->content)
                    && p < cast(void*, &s->content + 1)
                ){
                    return s;
                }
                continue;
            }

            if (p < cast(void*,
                s->content.dynamic.data - (Flex_Wide(s) * Flex_Bias(s))
            )) {
                // The memory lies before the series data allocation.
                //
                continue;
            }

            if (p >= cast(void*, s->content.dynamic.data
                + (Flex_Wide(s) * Flex_Rest(s))
            )) {
                // The memory lies after the series capacity.
                //
                continue;
            }

            // We now have a bad condition, in that the pointer is known to
            // be inside a series data allocation.  But it could be doubly
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
                + (Flex_Wide(s) * Flex_Len(s))
            )) {
                printf("Pointer found in freed tail capacity of series\n");
                fflush(stdout);
                return s;
            }

            return s;
        }
    }

    return nullptr; // not found
}

#endif


//
//  Alloc_Pairing: C
//
// Allocate a paired set of values.  The "key" is in the cell *before* the
// returned pointer.
//
// Because pairings are created in large numbers and left outstanding, they
// are not put into any tracking lists by default.  This means that if there
// is a panic(), they will leak--unless whichever API client that is using
// them ensures they are cleaned up.  So in C++, this is done with exception
// handling.
//
// However, untracked/unmanaged pairings have a special ability.  It's
// possible for them to be "owned" by a FRAME!, which sits in the first cell.
// This provides an alternate mechanism for plain C code to do cleanup besides
// handlers based on PUSH_TRAP().
//
Value* Alloc_Pairing(void) {
    Value* paired = cast(Value*, Alloc_Pooled(PAR_POOL)); // 2x Cell size
    Value* key = PAIRING_KEY(paired);

    Erase_Cell(paired);

    // Client will need to put *something* in the key slot (accessed with
    // PAIRING_KEY).  Whatever they end up writing should be acceptable
    // to avoid a GC, since the header is not purely 0...and it works out
    // that all "ordinary" values will just act as unmanaged metadata.
    //
    // Init_Pairing_Key_Owner is one option.
    //
    Erase_Cell(key);

    return paired;
}


//
//  Manage_Pairing: C
//
// The paired management status is handled by bits directly in the first (the
// paired value) Cell header.  API handle Cells are all managed.
//
void Manage_Pairing(Value* paired) {
    Set_Base_Managed_Bit(paired);
}


//
//  Unmanage_Pairing: C
//
// A pairing may become unmanaged.  This is not a good idea for things like
// the pairing used by a PAIR! value.  But pairings are used for API handles
// which default to tying their lifetime to the currently executing frame.
// It may be desirable to extend, shorten, or otherwise explicitly control
// their lifetime.
//
void Unmanage_Pairing(Value* paired) {
    assert(Is_Base_Managed(paired));
    Clear_Base_Managed_Bit(paired);
}


//
//  Free_Pairing: C
//
void Free_Pairing(Value* paired) {
    assert(Not_Base_Managed(paired));
    Flex* s = cast(Flex*, paired);
    Free_Pooled(STUB_POOL, s);

  #if DEBUG_STUB_ORIGINS
    s->tick = TICK;  // update to be tick on which pairing was freed
  #endif
}


//
//  Free_Unbiased_Flex_Data: C
//
// Routines that are part of the core series implementation
// call this, including Expand_Flex.  It requires a low-level
// awareness that the series data pointer cannot be freed
// without subtracting out the "biasing" which skips the pointer
// ahead to account for unused capacity at the head of the
// allocation.  They also must know the total allocation size.
//
// !!! Ideally this wouldn't be exported, but series data is now used to hold
// function arguments.
//
void Free_Unbiased_Flex_Data(char *unbiased, REBLEN total)
{
    REBLEN pool_num = FIND_POOL(total);
    REBPOL *pool;

    if (pool_num < SYSTEM_POOL) {
        //
        // The series data does not honor "BASE_BYTE()" when it is in use
        // The pools are not swept the way the Stub pool is, so only the
        // free units have significance to their headers.  Use a cast and not
        // BAS() because that assumes not (BASE_FLAG_UNREADABLE)
        //
        PoolUnit* unit = cast(PoolUnit*, unbiased);

        assert(Mem_Pools[pool_num].wide >= total);

        pool = &Mem_Pools[pool_num];
        unit->next_if_free = pool->first;
        pool->first = unit;
        pool->free++;

        FIRST_BYTE(unit) = FREE_POOLUNIT_BYTE;
    }
    else {
        FREE_N(char, total, unbiased);
        Mem_Pools[SYSTEM_POOL].has -= total;
        Mem_Pools[SYSTEM_POOL].free++;
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
// If the Flex has enough space within it, then it will be used,
// otherwise the Flex data will be reallocated.
//
// When expanded at the head, if bias space is available, it will
// be used (if it provides enough space).
//
// !!! It seems the original intent of this routine was
// to be used with a group of other routines that were "Noterm"
// and do not terminate.  However, Expand_Flex assumed that
// the capacity of the original series was at least (tail + 1)
// elements, and would include the terminator when "sliding"
// the data in the update.  This makes the other Noterm routines
// seem a bit high cost for their benefit.  If this were to be
// changed to Expand_Flex_Noterm it would put more burden
// on the clients...for a *potential* benefit in being able to
// write just an END marker into the terminal Cell vs. copying
// the entire value cell.  (Of course, with a good memcpy it
// might be an irrelevant difference.)  For the moment we reverse
// the burden by enforcing the assumption that the incoming series
// was already terminated.  That way our "slide" of the data via
// memcpy will keep it terminated.
//
// WARNING: never use direct pointers into the Flex data, as the
// Flex data can be relocated in memory.
//
void Expand_Flex(Flex* s, REBLEN index, REBLEN delta)
{
    assert(index <= Flex_Len(s));
    if (delta & 0x80000000)
        panic (Error_Past_End_Raw()); // 2GB max

    if (delta == 0)
        return;

    REBLEN len_old = Flex_Len(s);

    Byte wide = Flex_Wide(s);

    const bool was_dynamic = Is_Flex_Dynamic(s);

    if (was_dynamic and index == 0 and Flex_Bias(s) >= delta) {

    //=//// HEAD INSERTION OPTIMIZATION ///////////////////////////////////=//

        s->content.dynamic.data -= wide * delta;
        s->content.dynamic.len += delta;
        s->content.dynamic.rest += delta;
        Subtract_Flex_Bias(s, delta);

      #if RUNTIME_CHECKS
        if (Is_Flex_Array(s)) {
            //
            // When the bias region was marked, it was made "unsettable" if
            // this was a debug build.  Now that the memory is included in
            // the array again, we want it to be "settable", but still trash
            // until the caller puts something there.
            //
            // !!! The unsettable feature is currently not implemented,
            // but when it is this will be useful.
            //
            for (index = 0; index < delta; index++)
                Erase_Cell(Array_At(cast_Array(s), index));
        }
      #endif
        return;
    }

    // Width adjusted variables:

    REBLEN start = index * wide;
    REBLEN extra = delta * wide;
    REBLEN size = Flex_Len(s) * wide;

    // + wide for terminator
    if ((size + extra + wide) <= Flex_Rest(s) * Flex_Wide(s)) {
        //
        // No expansion was needed.  Slide data down if necessary.  Note that
        // the tail is not moved and instead the termination is done
        // separately with Term_Flex (in case it reaches an implicit
        // termination that is not a full-sized cell).

        memmove(
            Flex_Data(s) + start + extra,
            Flex_Data(s) + start,
            size - start
        );

        Set_Flex_Len(s, len_old + delta);
        assert(
            not was_dynamic or (
                Flex_Total(s) > ((Flex_Len(s) + Flex_Bias(s)) * wide)
            )
        );

        Term_Flex(s);

      #if RUNTIME_CHECKS
        if (Is_Flex_Array(s)) {
            //
            // The opened up area needs to be set to "settable" trash in the
            // debug build.  This takes care of making "unsettable" values
            // settable (if part of the expansion is in what was formerly the
            // ->rest), as well as just making sure old data which was in
            // the expanded region doesn't get left over on accident.
            //
            // !!! The unsettable feature is not currently implemented, but
            // when it is this will be useful.
            //
            while (delta != 0) {
                --delta;
                Erase_Cell(Array_At(cast_Array(s), index + delta));
            }
        }
      #endif

        return;
    }

//=//// INSUFFICIENT CAPACITY, NEW ALLOCATION REQUIRED ////////////////////=//

    if (Get_Flex_Flag(s, FIXED_SIZE))
        panic (Error_Locked_Series_Raw());

  #if RUNTIME_CHECKS
    if (Reb_Opts->watch_expand) {
        printf(
            "Expand %p wide: %d tail: %d delta: %d\n",
            cast(void*, s),
            cast(int, wide),
            cast(int, len_old),
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
        if (Prior_Expand[n_found] == s) {
            x = Flex_Len(s) + delta + 1; // Double the size
            break;
        }
        if (!Prior_Expand[n_found])
            n_available = n_found;
    }

  #if RUNTIME_CHECKS
    if (Reb_Opts->watch_expand) {
        // Print_Num("Expand:", series->tail + delta + 1);
    }
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
        bias_old = Flex_Bias(s);
        size_old = Flex_Total(s);
    }
    else {
        // `char*` casts needed: https://stackoverflow.com/q/57721104
        memcpy(
            cast(char*, &content_old),
            cast(char*, &s->content),
            sizeof(union StubContentUnion)
        );
        data_old = cast(char*, &content_old);
    }

    // The new series will *always* be dynamic, because it would not be
    // expanding if a fixed size allocation was sufficient.

    LEN_BYTE_OR_255(s) = 255; // series alloc caller sets
    Set_Flex_Flag(s, POWER_OF_2);
    if (not Did_Flex_Data_Alloc(s, len_old + delta + x))
        panic (Error_No_Memory((len_old + delta + x) * wide));

    assert(Is_Flex_Dynamic(s));
    if (Is_Flex_Array(s))
        Prep_Array(cast_Array(s), 0); // capacity doesn't matter it will prep

    // If necessary, add series to the recently expanded list
    //
    if (n_found >= MAX_EXPAND_LIST)
        Prior_Expand[n_available] = s;

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
    s->content.dynamic.len = len_old + delta;

    Term_Flex(s);

    if (was_dynamic) {
        //
        // We have to de-bias the data pointer before we can free it.
        //
        assert(Flex_Bias(s) == 0); // should be reset
        Free_Unbiased_Flex_Data(data_old - (wide * bias_old), size_old);
    }

  #if RUNTIME_CHECKS
    PG_Reb_Stats->Series_Expanded++;
  #endif

    assert(Not_Base_Marked(s));
}


//
//  Swap_Flex_Content: C
//
// Retain the identity of the two series but do a low-level swap of their
// content with each other.
//
void Swap_Flex_Content(Flex* a, Flex* b)
{
    // While the data series underlying a string may change widths over the
    // lifetime of that string Stub, there's not really any reasonable case
    // for mutating an array Stub into a non-array or vice versa.
    //
    assert(Is_Flex_Array(a) == Is_Flex_Array(b));

    // There are bits in the ->info and ->header which pertain to the content,
    // which includes whether the series is dynamic or if the data lives in
    // the Stub itself, the width (right 8 bits), etc.  Note that the length
    // of non-dynamic series lives in the info.

    Byte a_wide = WIDE_BYTE_OR_0(a); // indicates array if 0
    WIDE_BYTE_OR_0(a) = WIDE_BYTE_OR_0(b);
    WIDE_BYTE_OR_0(b) = a_wide;

    Byte a_len = LEN_BYTE_OR_255(a); // indicates dynamic if 255
    LEN_BYTE_OR_255(a) = LEN_BYTE_OR_255(b);
    LEN_BYTE_OR_255(b) = a_len;

    union StubContentUnion a_content;

    // `char*` casts needed: https://stackoverflow.com/q/57721104
    memcpy(
        cast(char*, &a_content),
        cast(char*, &a->content),
        sizeof(union StubContentUnion)
    );
    memcpy(
        cast(char*, &a->content),
        cast(char*, &b->content),
        sizeof(union StubContentUnion)
    );
    memcpy(
        cast(char*, &b->content),
        cast(char*, &a_content),
        sizeof(union StubContentUnion)
    );
}


//
//  Remake_Flex: C
//
// Reallocate a Flex as a given maximum size.  Content in the retained
// portion of the length will be preserved if BASE_FLAG_BASE is passed in.
//
void Remake_Flex(Flex* s, REBLEN units, Byte wide, Flags flags)
{
    // !!! This routine is being scaled back in terms of what it's allowed to
    // do for the moment; so the method of passing in flags is a bit strange.
    //
    assert((flags & ~(BASE_FLAG_BASE | FLEX_FLAG_POWER_OF_2)) == 0);

    bool preserve = did (flags & BASE_FLAG_BASE);

    REBLEN len_old = Flex_Len(s);
    Byte wide_old = Flex_Wide(s);

  #if RUNTIME_CHECKS
    if (preserve)
        assert(wide == wide_old); // can't change width if preserving
  #endif

    assert(Not_Flex_Flag(s, FIXED_SIZE));

    bool was_dynamic = Is_Flex_Dynamic(s);

    REBINT bias_old;
    REBINT size_old;

    // Extract the data pointer to take responsibility for it.  (The pointer
    // may have already been extracted if the caller is doing their own
    // updating preservation.)

    char *data_old;
    union StubContentUnion content_old;
    if (was_dynamic) {
        assert(s->content.dynamic.data != nullptr);
        data_old = s->content.dynamic.data;
        bias_old = Flex_Bias(s);
        size_old = Flex_Total(s);
    }
    else {
        // `char*` casts needed: https://stackoverflow.com/q/57721104
        memcpy(
            cast(char*, &content_old),
            cast(char*, &s->content),
            sizeof(union StubContentUnion)
        );
        data_old = cast(char*, &content_old);
    }

    WIDE_BYTE_OR_0(s) = wide;
    s->header.bits |= flags;

    // !!! Currently the remake won't make a series that fits in the size of
    // a Stub.  All series code needs a general audit, so that should be one
    // of the things considered.

    LEN_BYTE_OR_255(s) = 255; // series alloc caller sets
    if (not Did_Flex_Data_Alloc(s, units + 1)) {
        // Put series back how it was (there may be extant references)
        s->content.dynamic.data = cast(char*, data_old);
        panic (Error_No_Memory((units + 1) * wide));
    }
    assert(Is_Flex_Dynamic(s));
    if (Is_Flex_Array(s))
        Prep_Array(cast_Array(s), 0); // capacity doesn't matter, it will prep

    if (preserve) {
        // Preserve as much data as possible (if it was requested, some
        // operations may extract the data pointer ahead of time and do this
        // more selectively)

        s->content.dynamic.len = MIN(len_old, units);
        memcpy(
            s->content.dynamic.data,
            data_old,
            s->content.dynamic.len * wide
        );
    } else
        s->content.dynamic.len = 0;

    if (Is_Flex_Array(s))
        Term_Array_Len(cast_Array(s), Flex_Len(s));
    else
        Term_Non_Array_Flex(s);

    if (was_dynamic)
        Free_Unbiased_Flex_Data(data_old - (wide_old * bias_old), size_old);
}


//
//  Decay_Flex: C
//
void Decay_Flex(Flex* s)
{
    assert(Not_Flex_Info(s, INACCESSIBLE));

    if (Get_Flex_Flag(s, UTF8_SYMBOL))
        GC_Kill_Interning(cast(Symbol*, s));  // needs to adjust canons

    // Remove series from expansion list, if found:
    REBLEN n;
    for (n = 1; n < MAX_EXPAND_LIST; n++) {
        if (Prior_Expand[n] == s) Prior_Expand[n] = 0;
    }

    if (Is_Flex_Dynamic(s)) {
        Byte wide = Flex_Wide(s);
        REBLEN bias = Flex_Bias(s);
        REBLEN total = (bias + Flex_Rest(s)) * wide;
        char *unbiased = s->content.dynamic.data - (wide * bias);

        // !!! Contexts and actions keep their archetypes, for now, in the
        // now collapsed Stub.  For FRAME! this means holding onto the binding
        // which winds up being used in Derelativize().  See SPC_BINDING.
        // Preserving ACTION!'s archetype is speculative--to point out the
        // possibility exists for the other array with a "canon" [0]
        //
        if (
            Get_Array_Flag(s, IS_VARLIST)
            or Get_Array_Flag(s, IS_PARAMLIST)
        ){
            // `char*` casts needed: https://stackoverflow.com/q/57721104
            memcpy(
                cast(char*, &s->content.fixed),
                cast(char*, Array_Head(cast_Array(s))),
                sizeof(Cell)
            );
        }

        Free_Unbiased_Flex_Data(unbiased, total);

        // !!! This indicates reclaiming of the space, not for the series
        // Stubs themselves...have they never been accounted for, e.g. in
        // R3-Alpha?  If not, they should be...additional sizeof(Stub),
        // also tracking overhead for that.  Review the question of how
        // the GC watermarks interact with Alloc_Mem and the "higher
        // level" allocations.

        int tmp;
        GC_Ballast = REB_I32_ADD_OF(GC_Ballast, total, &tmp)
            ? INT32_MAX
            : tmp;

        LEN_BYTE_OR_255(s) = 1; // !!! is this right?
    }
    else {
        // Special GC processing for HANDLE! when the handle is implemented as
        // a singular array, so that if the handle represents a resource, it
        // may be freed.
        //
        // Note that not all singular arrays containing a HANDLE! should be
        // interpreted that when the array is freed the handle is freed (!)
        // Only when the handle array pointer in the freed singular
        // handle matches the Array being freed.  (It may have been just a
        // singular array that happened to contain a handle, otherwise, as
        // opposed to the specific singular made for the handle's GC awareness)

        if (Is_Flex_Array(s)) {
            Cell* v = Array_Head(cast_Array(s));
            if (NOT_END(v) and Unchecked_Type_Of(v) == TYPE_HANDLE) {
                if (v->extra.singular == cast_Array(s)) {
                    //
                    // Some handles use the managed form just because they
                    // want changes to the pointer in one instance to be seen
                    // by other instances...there may be no cleaner function.
                    //
                    // !!! Would a no-op cleaner be more efficient for those?
                    //
                    if (MISC(s).cleaner)
                        (MISC(s).cleaner)(KNOWN(v));
                }
            }
        }
    }

    Set_Flex_Info(s, INACCESSIBLE);
}


//
//  GC_Kill_Flex: C
//
// Only the garbage collector should be calling this routine.
// It frees a series even though it is under GC management,
// because the GC has figured out no references exist.
//
void GC_Kill_Flex(Flex* s)
{
  #if RUNTIME_CHECKS
    if (Not_Base_Readable(s)) {
        printf("Freeing already freed flex.\n");
        crash (s);
    }
  #endif

    if (Not_Flex_Info(s, INACCESSIBLE))
        Decay_Flex(s);

  #if RUNTIME_CHECKS
    s->info.bits = FLAG_WIDE_BYTE_OR_0(77); // corrupt Flex_Wide()
  #endif

    Corrupt_If_Needful(MISC(s).corrupt);
    Corrupt_If_Needful(LINK(s).corrupt);

    Free_Pooled(STUB_POOL, s);

    // GC may no longer be necessary:
    if (GC_Ballast > 0) CLR_SIGNAL(SIG_RECYCLE);

  #if RUNTIME_CHECKS
    PG_Reb_Stats->Series_Freed++;

    #if DEBUG_STUB_ORIGINS
        s->tick = TICK;  // update to be tick on which series was freed
    #endif
  #endif
}


INLINE void Untrack_Manual_Flex(Flex* s)
{
    Flex* * const last_ptr
        = &cast(Flex**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.len - 1
        ];

    assert(GC_Manuals->content.dynamic.len >= 1);
    if (*last_ptr != s) {
        //
        // If the series is not the last manually added series, then
        // find where it is, then move the last manually added series
        // to that position to preserve it when we chop off the tail
        // (instead of keeping the series we want to free).
        //
        Flex* *current_ptr = last_ptr - 1;
        while (*current_ptr != s) {
          #if RUNTIME_CHECKS
            if (
                current_ptr
                <= cast(Flex**, GC_Manuals->content.dynamic.data)
            ){
                printf("Series not in list of last manually added series\n");
                crash (s);
            }
          #endif
            --current_ptr;
        }
        *current_ptr = *last_ptr;
    }

    // !!! Should GC_Manuals ever shrink or save memory?
    //
    GC_Manuals->content.dynamic.len--;
}


//
//  Free_Unmanaged_Flex: C
//
// Releases Flex Stub and data to memory pools for reuse.
//
void Free_Unmanaged_Flex(Flex* s)
{
  #if RUNTIME_CHECKS
    if (Not_Base_Readable(s)) {
        printf("Trying to Free_Unmanaged_Flex() on already freed Flex\n");
        crash (s); // erroring here helps not conflate with tracking problems
    }

    if (Is_Flex_Managed(s)) {
        printf("Trying to Free_Unmanaged_Flex() on a GC-managed Flex\n");
        crash (s);
    }
  #endif

    Untrack_Manual_Flex(s);
    GC_Kill_Flex(s); // with bookkeeping done, use same routine as GC
}


//
//  Manage_Flex: C
//
// If BASE_FLAG_MANAGED is not explicitly passed to Make_Flex_Core(), a
// Flex will be manually memory-managed by default.  Thus, you don't need
// to worry about the Flex being freed out from under you while building it,
// and can call Free_Unmanaged_Flex() on it if you are done with it.
//
// Rather than free a Flex, this function can be used--which will transition
// a manually managed Flex to be one managed by the GC.  There is no way to
// transition back--once a Flex has become managed, only the GC can free it.
//
// Putting Flex into a Cell Payload (by using Init_String(), etc.) will
// implicitly ensure it is managed, as it is generally the case that all
// series in user-visible cells should be managed.  Doing otherwise requires
// careful hooks into Copy_Cell() and Derelativize().
//
void Manage_Flex(Flex* s)
{
  #if RUNTIME_CHECKS
    if (Is_Flex_Managed(s)) {
        printf("Attempt to manage already managed series\n");
        crash (s);
    }
  #endif

    s->header.bits |= BASE_FLAG_MANAGED;

    Untrack_Manual_Flex(s);
}


#if RUNTIME_CHECKS

//
//  Assert_Pointer_Detection_Working: C
//
// Check the conditions that are required for Detect_Rebol_Pointer() and
// Endlike_Header() to work, and throw some sample cases at it to make sure
// they give the right answer.
//
void Assert_Pointer_Detection_Working(void)
{
    uintptr_t cell_flag = BASE_FLAG_CELL;
    assert(FIRST_BYTE(&cell_flag) == BASE_BYTEMASK_0x08_CELL);
    uintptr_t flag_left_bit_31 = FLAG_LEFT_BIT(31);
    assert(FOURTH_BYTE(&flag_left_bit_31) == 0x01);

    assert(Detect_Rebol_Pointer("") == DETECTED_AS_UTF8);
    assert(Detect_Rebol_Pointer("asdf") == DETECTED_AS_UTF8);

    assert(Detect_Rebol_Pointer(EMPTY_ARRAY) == DETECTED_AS_STUB);
    assert(Detect_Rebol_Pointer(BLANK_VALUE) == DETECTED_AS_CELL);

    DECLARE_VALUE (end_cell);
    SET_END(end_cell);
    assert(Detect_Rebol_Pointer(end_cell) == DETECTED_AS_END);
    assert(Detect_Rebol_Pointer(END_BASE) == DETECTED_AS_END);
    assert(Detect_Rebol_Pointer(rebEND) == DETECTED_AS_END);

    // An Endlike_Header() can use the BASE_FLAG_MANAGED bit however it wants.
    // But the canon END_BASE is not managed, which was once used for a trick
    // of using it vs. nullptr...but that trick isn't being used right now.
    //
    assert(not (END_BASE->header.bits & BASE_FLAG_MANAGED));

    Flex* flex = Make_Flex(1, sizeof(char));
    assert(Detect_Rebol_Pointer(flex) == DETECTED_AS_STUB);
    Free_Unmanaged_Flex(flex);
    assert(Detect_Rebol_Pointer(flex) == DETECTED_AS_FREE);
}


//
//  Check_Memory_Debug: C
//
// Traverse the free lists of all pools -- just to prove we can.
//
// Note: This was useful in R3-Alpha for finding corruption from bad memory
// writes, because a write past the end of a Unit destroys the pointer for the
// next free area.  The Always_Malloc option for Ren-C leverages the faster
// checking built into Valgrind or Address Sanitizer for the same problem.
// However, a call to this is kept in the debug build on init and shutdown
// just to keep it working as a sanity check.
//
REBLEN Check_Memory_Debug(void)
{
    REBSEG *seg;
    for (seg = Mem_Pools[STUB_POOL].segs; seg; seg = seg->next) {
        Flex* s = cast(Flex*, seg + 1);

        REBLEN n;
        for (n = Mem_Pools[STUB_POOL].units; n > 0; --n, ++s) {
            if (Not_Base_Readable(s))
                continue;

            if (Is_Base_A_Cell(s))
                continue; // a pairing

            if (not Is_Flex_Dynamic(s))
                continue; // data lives in the series stub itself

            if (Flex_Rest(s) == 0)
                crash (s); // zero size allocations not legal

            REBLEN pool_num = FIND_POOL(Flex_Total(s));
            if (pool_num >= STUB_POOL)
                continue; // size doesn't match a known pool

            if (Mem_Pools[pool_num].wide != Flex_Total(s))
                crash (s);
        }
    }

    REBLEN total_free_units = 0;

    REBLEN pool_num;
    for (pool_num = 0; pool_num != SYSTEM_POOL; pool_num++) {
        REBLEN pool_free_units = 0;

        PoolUnit* unit = Mem_Pools[pool_num].first;
        for (; unit != nullptr; unit = unit->next_if_free) {
            assert(Not_Base_Readable(unit));

            ++pool_free_units;

            bool found = false;
            seg = Mem_Pools[pool_num].segs;
            for (; seg != nullptr; seg = seg->next) {
                if (
                    i_cast(uintptr_t, unit) > i_cast(uintptr_t, seg)
                    and (
                        i_cast(uintptr_t, unit)
                        < i_cast(uintptr_t, seg) + cast(uintptr_t, seg->size)
                    )
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

        if (Mem_Pools[pool_num].free != pool_free_units)
            crash ("actual free unit count does not agree with pool header");

        total_free_units += pool_free_units;
    }

    return total_free_units;
}


//
//  Dump_All_Flex_Of_Size: C
//
void Dump_All_Flex_Of_Size(REBLEN size)
{
    REBLEN count = 0;

    REBSEG *seg;
    for (seg = Mem_Pools[STUB_POOL].segs; seg; seg = seg->next) {
        Flex* s = cast(Flex*, seg + 1);
        REBLEN n;
        for (n = Mem_Pools[STUB_POOL].units; n > 0; --n, ++s) {
            if (Not_Base_Readable(s))
                continue;

            if (Flex_Wide(s) == size) {
                ++count;
                printf(
                    "%3d %4d %4d\n",
                    cast(int, count),
                    cast(int, Flex_Len(s)),
                    cast(int, Flex_Rest(s))
                );
            }
            fflush(stdout);
        }
    }
}


//
//  Dump_Flex_In_Pool: C
//
// Dump all Flex in pool @pool_id, UNKNOWN (-1) for all pools
//
void Dump_Flex_In_Pool(REBLEN pool_id)
{
    REBSEG *seg;
    for (seg = Mem_Pools[STUB_POOL].segs; seg; seg = seg->next) {
        Flex* s = cast(Flex*, seg + 1);
        REBLEN n = 0;
        for (n = Mem_Pools[STUB_POOL].units; n > 0; --n, ++s) {
            if (Not_Base_Readable(s))
                continue;

            if (Is_Base_A_Cell(s))
                continue;  // pairing

            if (
                pool_id == UNKNOWN
                or (
                    Is_Flex_Dynamic(s)
                    and pool_id == FIND_POOL(Flex_Total(s))
                )
            ){
                Dump_Flex(s, "Dump_Flex_In_Pool");
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

    REBLEN n;
    for (n = 0; n != SYSTEM_POOL; n++) {
        REBLEN segs = 0;
        REBLEN size = 0;

        size = segs = 0;

        REBSEG *seg;
        for (seg = Mem_Pools[n].segs; seg; seg = seg->next, segs++)
            size += seg->size;

        REBLEN used = Mem_Pools[n].has - Mem_Pools[n].free;
        printf(
            "Pool[%-2d] %5dB %-5d/%-5d:%-4d (%3d%%) ",
            cast(int, n),
            cast(int, Mem_Pools[n].wide),
            cast(int, used),
            cast(int, Mem_Pools[n].has),
            cast(int, Mem_Pools[n].units),
            cast(int,
                Mem_Pools[n].has != 0 ? ((used * 100) / Mem_Pools[n].has) : 0
            )
        );
        printf("%-2d segs, %-7d total\n", cast(int, segs), cast(int, size));

        tused += used * Mem_Pools[n].wide;
        total += size;
    }

    printf(
        "Pools used %d of %d (%2d%%)\n",
        cast(int, tused),
        cast(int, total),
        cast(int, (tused * 100) / total)
    );
    printf("System pool used %d\n", cast(int, Mem_Pools[SYSTEM_POOL].has));
    printf("Raw allocator reports %lu\n", cast(unsigned long, PG_Mem_Usage));

    fflush(stdout);
}


//
//  Inspect_Flex: C
//
// !!! This is an old routine which was exposed through STATS to "expert
// users".  Its purpose is to calculate the total amount of memory currently
// in use by series, but it could also print out a breakdown of categories.
//
REBU64 Inspect_Flex(bool show)
{
    REBLEN segs = 0;
    REBLEN tot = 0;
    REBLEN blks = 0;
    REBLEN strs = 0;
    REBLEN unis = 0;
    REBLEN odds = 0;
    REBLEN fre = 0;

    REBLEN seg_size = 0;
    REBLEN str_size = 0;
    REBLEN uni_size = 0;
    REBLEN blk_size = 0;
    REBLEN odd_size = 0;

    REBU64 tot_size = 0;

    REBSEG *seg;
    for (seg = Mem_Pools[STUB_POOL].segs; seg; seg = seg->next) {

        seg_size += seg->size;
        segs++;

        Flex* s = cast(Flex*, seg + 1);

        REBLEN n;
        for (n = Mem_Pools[STUB_POOL].units; n > 0; n--) {
            if (Not_Base_Readable(s)) {
                ++fre;
                continue;
            }

            ++tot;

            if (Is_Base_A_Cell(s))
                continue;

            tot_size += Flex_Total_If_Dynamic(s); // else 0

            if (Is_Flex_Array(s)) {
                blks++;
                blk_size += Flex_Total_If_Dynamic(s);
            }
            else if (Flex_Wide(s) == 1) {
                strs++;
                str_size += Flex_Total_If_Dynamic(s);
            }
            else if (Flex_Wide(s) == sizeof(Ucs2Unit)) {
                unis++;
                uni_size += Flex_Total_If_Dynamic(s);
            }
            else if (Flex_Wide(s)) {
                odds++;
                odd_size += Flex_Total_If_Dynamic(s);
            }

            ++s;
        }
    }

    // Size up unused memory:
    //
    REBU64 fre_size = 0;
    REBINT pool_num;
    for (pool_num = 0; pool_num != SYSTEM_POOL; pool_num++) {
        fre_size += Mem_Pools[pool_num].free * Mem_Pools[pool_num].wide;
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
            "  %-6d unis = %-7d bytes - uni strings\n",
            cast(int, unis),
            cast(int, uni_size)
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
        printf("  %lu bytes stub-space\n", cast(unsigned long, fre_size));
        printf("\n");
    }

    fflush(stdout);

    return tot_size;
}

#endif
