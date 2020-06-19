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
// from a memory pool of other fixed-size things.  This is called the "Node"
// in both Rebol and Red terminology.  It is an item whose pointer is valid
// for the lifetime of the object, regardless of resizing.  This is where
// header information is stored, and pointers to these objects may be saved
// in REBVAL values; such that they are kept alive by the garbage collector.
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
    if (PG_Mem_Limit != 0 and PG_Mem_Usage > PG_Mem_Limit)
        Check_Security(Canon(SYM_MEMORY), POL_EXEC, 0);

    // malloc() internally remembers the size of the allocation, and is hence
    // "overkill" for this operation.  Yet the current implementations on all
    // C platforms use malloc() and free() anyway.

  #ifdef NDEBUG
    void *p = malloc(size);
  #else
    // Cache size at the head of the allocation in debug builds for checking.
    // Also catches free() use with Alloc_Mem() instead of Free_Mem().
    //
    // Use a 64-bit quantity to preserve DEBUG_MEMORY_ALIGN invariant.

    void *p_extra = malloc(size + sizeof(REBI64));
    if (p_extra == NULL)
        return NULL;
    *cast(REBI64 *, p_extra) = size;
    void *p = cast(char*, p_extra) + sizeof(REBI64);
  #endif

  #ifdef DEBUG_MEMORY_ALIGN
    assert(cast(uintptr_t, p) % sizeof(REBI64) == 0);
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
  #ifdef NDEBUG
    free(mem);
  #else
    assert(mem != NULL);
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
    // Ren-C's pool, because it requires 2*sizeof(void*) for each node at the
    // minimum...because instead of just the freelist pointer, it has a
    // standardized header (0 when free).
    //
    // This is not a problem, since all such small strings would also need
    // REBSERs...and Ren-C has a better answer to embed the payload directly
    // into the REBSER.  This wouldn't apply if you were trying to do very
    // small allocations of strings that did not have associated REBSERs..
    // but those don't exist in the code.

    MOD_POOL( 1, 256),  // 9-16 (when REBVAL is 16)
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

    DEF_POOL(sizeof(REBSER), 4096), // Series headers

  #ifdef UNUSUAL_REBVAL_SIZE // sizeof(REBVAL)*2 not sizeof(REBSER)
    DEF_POOL(sizeof(REBVAL) * 2, 16), // Pairings, PAR_POOL
  #endif

    DEF_POOL(sizeof(REBGOB), 128),  // Gobs
    DEF_POOL(sizeof(REBI64), 1), // Just used for tracking main memory
};


//
//  Startup_Pools: C
//
// Initialize memory pool array.
//
void Startup_Pools(REBINT scale)
{
  #ifndef NDEBUG
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
    REBCNT n;
    for (n = 0; n < MAX_POOLS; n++) {
        Mem_Pools[n].segs = NULL;
        Mem_Pools[n].first = NULL;
        Mem_Pools[n].last = NULL;

        // A panic is used instead of an assert, since the debug sizes and
        // release sizes may be different...and both must be checked.
        //
      #if defined(DEBUG_MEMORY_ALIGN) || 1
        if (Mem_Pool_Spec[n].wide % sizeof(REBI64) != 0)
            panic ("memory pool width is not 64-bit aligned");
      #endif

        Mem_Pools[n].wide = Mem_Pool_Spec[n].wide;

        Mem_Pools[n].units = (Mem_Pool_Spec[n].units * scale) / unscale;
        if (Mem_Pools[n].units < 2) Mem_Pools[n].units = 2;
        Mem_Pools[n].free = 0;
        Mem_Pools[n].has = 0;
    }

    // For pool lookup. Maps size to pool index. (See Find_Pool below)
    PG_Pool_Map = ALLOC_N(REBYTE, (4 * MEM_BIG_SIZE) + 1);

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

  #if !defined(NDEBUG)
    PG_Reb_Stats = ALLOC(REB_STATS);
  #endif

    // Manually allocated series that GC is not responsible for (unless a
    // trap occurs). Holds series pointers.
    //
    // As a trick to keep this series from trying to track itself, say it's
    // managed, then sneak the flag off.
    //
    GC_Manuals = Make_Ser_Core(15, sizeof(REBSER *), NODE_FLAG_MANAGED);
    CLEAR_SER_FLAG(GC_Manuals, NODE_FLAG_MANAGED);

    Prior_Expand = ALLOC_N(REBSER*, MAX_EXPAND_LIST);
    CLEAR(Prior_Expand, sizeof(REBSER*) * MAX_EXPAND_LIST);
    Prior_Expand[0] = (REBSER*)1;
}


//
//  Shutdown_Pools: C
//
// Release all segments in all pools, and the pools themselves.
//
void Shutdown_Pools(void)
{
    // Can't use Free_Unmanaged_Series() because GC_Manuals couldn't be put in
    // the manuals list...
    //
    GC_Kill_Series(GC_Manuals);

  #if !defined(NDEBUG)
    REBSEG *debug_seg = Mem_Pools[SER_POOL].segs;
    for(; debug_seg != NULL; debug_seg = debug_seg->next) {
        REBSER *series = cast(REBSER*, debug_seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; n--, series++) {
            if (IS_FREE_NODE(series))
                continue;

            assert(NOT_SER_FLAG(series, NODE_FLAG_MANAGED));
            printf("At least one leaked series at shutdown...\n");
            panic (series);
        }
    }
  #endif

    REBCNT pool_num;
    for (pool_num = 0; pool_num < MAX_POOLS; pool_num++) {
        REBPOL *pool = &Mem_Pools[pool_num];
        REBCNT mem_size = pool->wide * pool->units + sizeof(REBSEG);

        REBSEG *seg = pool->segs;
        while (seg) {
            REBSEG *next;
            next = seg->next;
            FREE_N(char, mem_size, cast(char*, seg));
            seg = next;
        }
    }

    FREE_N(REBPOL, MAX_POOLS, Mem_Pools);

    FREE_N(REBYTE, (4 * MEM_BIG_SIZE) + 1, PG_Pool_Map);

    // !!! Revisit location (just has to be after all series are freed)
    FREE_N(REBSER*, MAX_EXPAND_LIST, Prior_Expand);

  #if !defined(NDEBUG)
    FREE(REB_STATS, PG_Reb_Stats);
  #endif

  #if !defined(NDEBUG)
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
// the size and units specified when the pool header was created.  The nodes
// of the pool are linked to the free list.
//
void Fill_Pool(REBPOL *pool)
{
    REBCNT units = pool->units;
    REBCNT mem_size = pool->wide * units + sizeof(REBSEG);

    REBSEG *seg = cast(REBSEG *, ALLOC_N(char, mem_size));
    if (seg == NULL) {
        panic ("Out of memory error during Fill_Pool()");

        // Rebol's safe handling of running out of memory was never really
        // articulated.  Yet it should be possible to run a fail()...at least
        // of a certain type...without allocating more memory.  (This probably
        // suggests a need for pre-creation of the out of memory objects,
        // as is done with the stack overflow error)
        //
        // fail (Error_No_Memory(mem_size));
    }

    seg->size = mem_size;
    seg->next = pool->segs;
    pool->segs = seg;
    pool->has += units;
    pool->free += units;

    // Add new nodes to the end of free list:

    // Can't use NOD() here because it tests for NOT(NODE_FLAG_FREE)
    //
    REBNOD *node = cast(REBNOD*, seg + 1);

    if (not pool->first) {
        assert(not pool->last);
        pool->first = node;
    }
    else {
        assert(pool->last);
        pool->last->next_if_free = node;
    }

    while (true) {
        FIRST_BYTE(node->header) = FREED_SERIES_BYTE;

        if (--units == 0) {
            node->next_if_free = nullptr;
            break;
        }

        // Can't use NOD() here because it tests for NODE_FLAG_FREE
        //
        node->next_if_free = cast(REBNOD*, cast(REBYTE*, node) + pool->wide);
        node = node->next_if_free;
    }

    pool->last = node;
}


#if !defined(NDEBUG)

//
//  Try_Find_Containing_Node_Debug: C
//
// This debug-build-only routine will look to see if it can find what series
// a data pointer lives in.  It returns NULL if it can't find one.  It's very
// slow, because it has to look at all the series.  Use sparingly!
//
REBNOD *Try_Find_Containing_Node_Debug(const void *p)
{
    REBSEG *seg;

    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        REBSER *s = cast(REBSER*, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; --n, ++s) {
            if (IS_FREE_NODE(s))
                continue;

            if (s->header.bits & NODE_FLAG_CELL) { // a "pairing"
                if (p >= cast(void*, s) and p < cast(void*, s + 1))
                    return NOD(s); // REBSER is REBVAL[2]
                continue;
            }

            if (not IS_SER_DYNAMIC(s)) {
                if (
                    p >= cast(void*, &s->content)
                    && p < cast(void*, &s->content + 1)
                ){
                    return NOD(s);
                }
                continue;
            }

            if (p < cast(void*,
                s->content.dynamic.data - (SER_WIDE(s) * SER_BIAS(s))
            )) {
                // The memory lies before the series data allocation.
                //
                continue;
            }

            if (p >= cast(void*, s->content.dynamic.data
                + (SER_WIDE(s) * SER_REST(s))
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
                return NOD(s);
            }

            if (p >= cast(void*,
                s->content.dynamic.data
                + (SER_WIDE(s) * SER_LEN(s))
            )) {
                printf("Pointer found in freed tail capacity of series\n");
                fflush(stdout);
                return NOD(s);
            }

            return NOD(s);
        }
    }

    return NULL; // not found
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
// is a fail(), they will leak--unless whichever API client that is using
// them ensures they are cleaned up.  So in C++, this is done with exception
// handling.
//
// However, untracked/unmanaged pairings have a special ability.  It's
// possible for them to be "owned" by a FRAME!, which sits in the first cell.
// This provides an alternate mechanism for plain C code to do cleanup besides
// handlers based on PUSH_TRAP().
//
REBVAL *Alloc_Pairing(void) {
    REBVAL *paired = cast(REBVAL*, Make_Node(PAR_POOL)); // 2x REBVAL size
    REBVAL *key = PAIRING_KEY(paired);

    Prep_Non_Stack_Cell(paired);
    TRASH_CELL_IF_DEBUG(paired);

    // Client will need to put *something* in the key slot (accessed with
    // PAIRING_KEY).  Whatever they end up writing should be acceptable
    // to avoid a GC, since the header is not purely 0...and it works out
    // that all "ordinary" values will just act as unmanaged metadata.
    //
    // Init_Pairing_Key_Owner is one option.
    //
    Prep_Non_Stack_Cell(key);
    TRASH_CELL_IF_DEBUG(key);

    return paired;
}


//
//  Manage_Pairing: C
//
// The paired management status is handled by bits directly in the first (the
// paired value) REBVAL header.  API handle REBVALs are all managed.
//
void Manage_Pairing(REBVAL *paired) {
    SET_VAL_FLAG(paired, NODE_FLAG_MANAGED);
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
void Unmanage_Pairing(REBVAL *paired) {
    assert(GET_VAL_FLAG(paired, NODE_FLAG_MANAGED));
    CLEAR_VAL_FLAG(paired, NODE_FLAG_MANAGED);
}


//
//  Free_Pairing: C
//
void Free_Pairing(REBVAL *paired) {
    assert(NOT_VAL_FLAG(paired, NODE_FLAG_MANAGED));
    REBSER *s = cast(REBSER*, paired);
    Free_Node(SER_POOL, s);

  #if !defined(NDEBUG)
    #if defined(DEBUG_COUNT_TICKS)
        s->tick = TG_Tick; // update to be tick on which node was freed
    #endif
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
void Free_Unbiased_Series_Data(char *unbiased, REBCNT total)
{
    REBCNT pool_num = FIND_POOL(total);
    REBPOL *pool;

    if (pool_num < SYSTEM_POOL) {
        //
        // The series data does not honor "node protocol" when it is in use
        // The pools are not swept the way the REBSER pool is, so only the
        // free nodes have significance to their headers.  Use a cast and not
        // NOD() because that assumes not (NODE_FLAG_FREE)
        //
        REBNOD *node = cast(REBNOD*, unbiased);

        assert(Mem_Pools[pool_num].wide >= total);

        pool = &Mem_Pools[pool_num];
        node->next_if_free = pool->first;
        pool->first = node;
        pool->free++;

        FIRST_BYTE(node->header) = FREED_SERIES_BYTE;
    }
    else {
        FREE_N(char, total, unbiased);
        Mem_Pools[SYSTEM_POOL].has -= total;
        Mem_Pools[SYSTEM_POOL].free++;
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
// write just an END marker into the terminal REBVAL vs. copying
// the entire value cell.  (Of course, with a good memcpy it
// might be an irrelevant difference.)  For the moment we reverse
// the burden by enforcing the assumption that the incoming series
// was already terminated.  That way our "slide" of the data via
// memcpy will keep it terminated.
//
// WARNING: never use direct pointers into the series data, as the
// series data can be relocated in memory.
//
void Expand_Series(REBSER *s, REBCNT index, REBCNT delta)
{
    assert(index <= SER_LEN(s));
    if (delta & 0x80000000) fail (Error_Past_End_Raw()); // 2GB max

    if (delta == 0) return;

    REBCNT len_old = SER_LEN(s);

    REBYTE wide = SER_WIDE(s);

    const bool was_dynamic = IS_SER_DYNAMIC(s);

    if (was_dynamic and index == 0 and SER_BIAS(s) >= delta) {

    //=//// HEAD INSERTION OPTIMIZATION ///////////////////////////////////=//

        s->content.dynamic.data -= wide * delta;
        s->content.dynamic.len += delta;
        s->content.dynamic.rest += delta;
        SER_SUB_BIAS(s, delta);

      #if !defined(NDEBUG)
        if (IS_SER_ARRAY(s)) {
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
                Prep_Non_Stack_Cell(ARR_AT(ARR(s), index));
        }
      #endif
        return;
    }

    // Width adjusted variables:

    REBCNT start = index * wide;
    REBCNT extra = delta * wide;
    REBCNT size = SER_LEN(s) * wide;

    // + wide for terminator
    if ((size + extra + wide) <= SER_REST(s) * SER_WIDE(s)) {
        //
        // No expansion was needed.  Slide data down if necessary.  Note that
        // the tail is not moved and instead the termination is done
        // separately with TERM_SERIES (in case it reaches an implicit
        // termination that is not a full-sized cell).

        memmove(
            SER_DATA_RAW(s) + start + extra,
            SER_DATA_RAW(s) + start,
            size - start
        );

        SET_SERIES_LEN(s, len_old + delta);
        assert(
            not was_dynamic or (
                SER_TOTAL(s) > ((SER_LEN(s) + SER_BIAS(s)) * wide)
            )
        );

        TERM_SERIES(s);

      #if !defined(NDEBUG)
        if (IS_SER_ARRAY(s)) {
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
                Prep_Non_Stack_Cell(ARR_AT(ARR(s), index + delta));
            }
        }
      #endif

        return;
    }

//=//// INSUFFICIENT CAPACITY, NEW ALLOCATION REQUIRED ////////////////////=//

    if (GET_SER_FLAG(s, SERIES_FLAG_FIXED_SIZE))
        fail (Error_Locked_Series_Raw());

  #ifndef NDEBUG
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

    REBCNT x = 1;
    REBCNT n_available = 0;
    REBCNT n_found;
    for (n_found = 0; n_found < MAX_EXPAND_LIST; n_found++) {
        if (Prior_Expand[n_found] == s) {
            x = SER_LEN(s) + delta + 1; // Double the size
            break;
        }
        if (!Prior_Expand[n_found])
            n_available = n_found;
    }

  #ifndef NDEBUG
    if (Reb_Opts->watch_expand) {
        // Print_Num("Expand:", series->tail + delta + 1);
    }
  #endif

    // !!! The protocol for doing new allocations currently mandates that the
    // dynamic content area be cleared out.  But the data lives in the content
    // area if there's no dynamic portion.  The in-REBSER content has to be
    // copied to preserve the data.  This could be generalized so that the
    // routines that do calculations operate on the content as a whole, not
    // the REBSER node, so the content is extracted either way.
    //
    union Reb_Series_Content content_old;
    REBINT bias_old;
    REBCNT size_old;
    char *data_old;
    if (was_dynamic) {
        data_old = s->content.dynamic.data;
        bias_old = SER_BIAS(s);
        size_old = SER_TOTAL(s);
    }
    else {
        // `char*` casts needed: https://stackoverflow.com/q/57721104
        memcpy(
            cast(char*, &content_old),
            cast(char*, &s->content),
            sizeof(union Reb_Series_Content)
        );
        data_old = cast(char*, &content_old);
    }

    // The new series will *always* be dynamic, because it would not be
    // expanding if a fixed size allocation was sufficient.

    LEN_BYTE_OR_255(s) = 255; // series alloc caller sets
    SET_SER_FLAG(s, SERIES_FLAG_POWER_OF_2);
    if (not Did_Series_Data_Alloc(s, len_old + delta + x))
        fail (Error_No_Memory((len_old + delta + x) * wide));

    assert(IS_SER_DYNAMIC(s));
    if (IS_SER_ARRAY(s))
        Prep_Array(ARR(s), 0); // capacity doesn't matter it will prep

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

    TERM_SERIES(s);

    if (was_dynamic) {
        //
        // We have to de-bias the data pointer before we can free it.
        //
        assert(SER_BIAS(s) == 0); // should be reset
        Free_Unbiased_Series_Data(data_old - (wide * bias_old), size_old);
    }

  #if !defined(NDEBUG)
    PG_Reb_Stats->Series_Expanded++;
  #endif

    assert(NOT_SER_FLAG(s, NODE_FLAG_MARKED));
}


//
//  Swap_Series_Content: C
//
// Retain the identity of the two series but do a low-level swap of their
// content with each other.
//
void Swap_Series_Content(REBSER* a, REBSER* b)
{
    // While the data series underlying a string may change widths over the
    // lifetime of that string node, there's not really any reasonable case
    // for mutating an array node into a non-array or vice versa.
    //
    assert(IS_SER_ARRAY(a) == IS_SER_ARRAY(b));

    // There are bits in the ->info and ->header which pertain to the content,
    // which includes whether the series is dynamic or if the data lives in
    // the node itself, the width (right 8 bits), etc.  Note that the length
    // of non-dynamic series lives in the info.

    REBYTE a_wide = WIDE_BYTE_OR_0(a); // indicates array if 0
    WIDE_BYTE_OR_0(a) = WIDE_BYTE_OR_0(b);
    WIDE_BYTE_OR_0(b) = a_wide;

    REBYTE a_len = LEN_BYTE_OR_255(a); // indicates dynamic if 255
    LEN_BYTE_OR_255(a) = LEN_BYTE_OR_255(b);
    LEN_BYTE_OR_255(b) = a_len;

    union Reb_Series_Content a_content;

    // `char*` casts needed: https://stackoverflow.com/q/57721104
    memcpy(
        cast(char*, &a_content),
        cast(char*, &a->content),
        sizeof(union Reb_Series_Content)
    );
    memcpy(
        cast(char*, &a->content),
        cast(char*, &b->content),
        sizeof(union Reb_Series_Content)
    );
    memcpy(
        cast(char*, &b->content),
        cast(char*, &a_content),
        sizeof(union Reb_Series_Content)
    );
}


//
//  Remake_Series: C
//
// Reallocate a series as a given maximum size.  Content in the retained
// portion of the length will be preserved if NODE_FLAG_NODE is passed in.
//
void Remake_Series(REBSER *s, REBCNT units, REBYTE wide, REBFLGS flags)
{
    // !!! This routine is being scaled back in terms of what it's allowed to
    // do for the moment; so the method of passing in flags is a bit strange.
    //
    assert((flags & ~(NODE_FLAG_NODE | SERIES_FLAG_POWER_OF_2)) == 0);

    bool preserve = did (flags & NODE_FLAG_NODE);

    REBCNT len_old = SER_LEN(s);
    REBYTE wide_old = SER_WIDE(s);

  #if !defined(NDEBUG)
    if (preserve)
        assert(wide == wide_old); // can't change width if preserving
  #endif

    assert(NOT_SER_FLAG(s, SERIES_FLAG_FIXED_SIZE));

    bool was_dynamic = IS_SER_DYNAMIC(s);

    REBINT bias_old;
    REBINT size_old;

    // Extract the data pointer to take responsibility for it.  (The pointer
    // may have already been extracted if the caller is doing their own
    // updating preservation.)

    char *data_old;
    union Reb_Series_Content content_old;
    if (was_dynamic) {
        assert(s->content.dynamic.data != NULL);
        data_old = s->content.dynamic.data;
        bias_old = SER_BIAS(s);
        size_old = SER_TOTAL(s);
    }
    else {
        // `char*` casts needed: https://stackoverflow.com/q/57721104
        memcpy(
            cast(char*, &content_old),
            cast(char*, &s->content),
            sizeof(union Reb_Series_Content)
        );
        data_old = cast(char*, &content_old);
    }

    WIDE_BYTE_OR_0(s) = wide;
    s->header.bits |= flags;

    // !!! Currently the remake won't make a series that fits in the size of
    // a REBSER.  All series code needs a general audit, so that should be one
    // of the things considered.

    LEN_BYTE_OR_255(s) = 255; // series alloc caller sets
    if (not Did_Series_Data_Alloc(s, units + 1)) {
        // Put series back how it was (there may be extant references)
        s->content.dynamic.data = cast(char*, data_old);
        fail (Error_No_Memory((units + 1) * wide));
    }
    assert(IS_SER_DYNAMIC(s));
    if (IS_SER_ARRAY(s))
        Prep_Array(ARR(s), 0); // capacity doesn't matter, it will prep

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

    if (IS_SER_ARRAY(s))
        TERM_ARRAY_LEN(ARR(s), SER_LEN(s));
    else
        TERM_SEQUENCE(s);

    if (was_dynamic)
        Free_Unbiased_Series_Data(data_old - (wide_old * bias_old), size_old);
}


//
//  Decay_Series: C
//
void Decay_Series(REBSER *s)
{
    assert(NOT_SER_INFO(s, SERIES_INFO_INACCESSIBLE));

    if (GET_SER_FLAG(s, SERIES_FLAG_UTF8_STRING))
        GC_Kill_Interning(s); // needs special handling to adjust canons

    // Remove series from expansion list, if found:
    REBCNT n;
    for (n = 1; n < MAX_EXPAND_LIST; n++) {
        if (Prior_Expand[n] == s) Prior_Expand[n] = 0;
    }

    if (IS_SER_DYNAMIC(s)) {
        REBYTE wide = SER_WIDE(s);
        REBCNT bias = SER_BIAS(s);
        REBCNT total = (bias + SER_REST(s)) * wide;
        char *unbiased = s->content.dynamic.data - (wide * bias);

        // !!! Contexts and actions keep their archetypes, for now, in the
        // now collapsed node.  For FRAME! this means holding onto the binding
        // which winds up being used in Derelativize().  See SPC_BINDING.
        // Preserving ACTION!'s archetype is speculative--to point out the
        // possibility exists for the other array with a "canon" [0]
        //
        if (ANY_SER_FLAGS(s, ARRAY_FLAG_VARLIST | ARRAY_FLAG_PARAMLIST)) {
            // `char*` casts needed: https://stackoverflow.com/q/57721104
            memcpy(
                cast(char*, &s->content.fixed),
                cast(char*, ARR_HEAD(ARR(s))),
                sizeof(REBVAL)
            );
        }

        Free_Unbiased_Series_Data(unbiased, total);

        // !!! This indicates reclaiming of the space, not for the series
        // nodes themselves...have they never been accounted for, e.g. in
        // R3-Alpha?  If not, they should be...additional sizeof(REBSER),
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
        // handle matches the REBARR being freed.  (It may have been just a
        // singular array that happened to contain a handle, otherwise, as
        // opposed to the specific singular made for the handle's GC awareness)

        if (IS_SER_ARRAY(s)) {
            RELVAL *v = ARR_HEAD(ARR(s));
            if (NOT_END(v) and VAL_TYPE_RAW(v) == REB_HANDLE) {
                if (v->extra.singular == ARR(s)) {
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

    SET_SER_INFO(s, SERIES_INFO_INACCESSIBLE);
}


//
//  GC_Kill_Series: C
//
// Only the garbage collector should be calling this routine.
// It frees a series even though it is under GC management,
// because the GC has figured out no references exist.
//
void GC_Kill_Series(REBSER *s)
{
  #if !defined(NDEBUG)
    if (IS_FREE_NODE(s)) {
        printf("Freeing already freed node.\n");
        panic (s);
    }
  #endif

    if (NOT_SER_INFO(s, SERIES_INFO_INACCESSIBLE))
        Decay_Series(s);

  #if !defined(NDEBUG)
    s->info.bits = FLAG_WIDE_BYTE_OR_0(77); // corrupt SER_WIDE()
  #endif

    TRASH_POINTER_IF_DEBUG(MISC(s).trash);
    TRASH_POINTER_IF_DEBUG(LINK(s).trash);

    Free_Node(SER_POOL, s);

    // GC may no longer be necessary:
    if (GC_Ballast > 0) CLR_SIGNAL(SIG_RECYCLE);

  #if !defined(NDEBUG)
    PG_Reb_Stats->Series_Freed++;

    #if defined(DEBUG_COUNT_TICKS)
        s->tick = TG_Tick; // update to be tick on which series was freed
    #endif
  #endif
}


inline static void Untrack_Manual_Series(REBSER *s)
{
    REBSER ** const last_ptr
        = &cast(REBSER**, GC_Manuals->content.dynamic.data)[
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
        REBSER **current_ptr = last_ptr - 1;
        while (*current_ptr != s) {
          #if !defined(NDEBUG)
            if (
                current_ptr
                <= cast(REBSER**, GC_Manuals->content.dynamic.data)
            ){
                printf("Series not in list of last manually added series\n");
                panic(s);
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
//  Free_Unmanaged_Series: C
//
// Returns series node and data to memory pools for reuse.
//
void Free_Unmanaged_Series(REBSER *s)
{
  #if !defined(NDEBUG)
    if (IS_FREE_NODE(s)) {
        printf("Trying to Free_Umanaged_Series() on already freed series\n");
        panic (s); // erroring here helps not conflate with tracking problems
    }

    if (IS_SERIES_MANAGED(s)) {
        printf("Trying to Free_Unmanaged_Series() on a GC-managed series\n");
        panic (s);
    }
  #endif

    Untrack_Manual_Series(s);
    GC_Kill_Series(s); // with bookkeeping done, use same routine as GC
}


//
//  Manage_Series: C
//
// If NODE_FLAG_MANAGED is not explicitly passed to Make_Ser_Core, a
// series will be manually memory-managed by default.  Thus, you don't need
// to worry about the series being freed out from under you while building it,
// and can call Free_Unmanaged_Series() on it if you are done with it.
//
// Rather than free a series, this function can be used--which will transition
// a manually managed series to be one managed by the GC.  There is no way to
// transition back--once a series has become managed, only the GC can free it.
//
// Putting series into a value cell (by using Init_String(), etc.) will
// implicitly ensure it is managed, as it is generally the case that all
// series in user-visible cells should be managed.  Doing otherwise requires
// careful hooks into Move_Value() and Derelativize().
//
void Manage_Series(REBSER *s)
{
  #if !defined(NDEBUG)
    if (IS_SERIES_MANAGED(s)) {
        printf("Attempt to manage already managed series\n");
        panic (s);
    }
  #endif

    s->header.bits |= NODE_FLAG_MANAGED;

    Untrack_Manual_Series(s);
}


#if !defined(NDEBUG)

//
//  Assert_Pointer_Detection_Working: C
//
// Check the conditions that are required for Detect_Rebol_Pointer() and
// Endlike_Header() to work, and throw some sample cases at it to make sure
// they give the right answer.
//
void Assert_Pointer_Detection_Working(void)
{
    uintptr_t cell_flag = NODE_FLAG_CELL;
    assert(FIRST_BYTE(cell_flag) == 0x1);
    uintptr_t protected_flag = CELL_FLAG_PROTECTED;
    assert(THIRD_BYTE(protected_flag) == 0x80);

    assert(Detect_Rebol_Pointer("") == DETECTED_AS_UTF8);
    assert(Detect_Rebol_Pointer("asdf") == DETECTED_AS_UTF8);

    assert(Detect_Rebol_Pointer(EMPTY_ARRAY) == DETECTED_AS_SERIES);
    assert(Detect_Rebol_Pointer(BLANK_VALUE) == DETECTED_AS_CELL);

    // The system does not really intentionally "free" any cells, but they
    // can happen in bad memory locations.  Along with CELL_FLAG_PROTECED and
    // the potential absence of NODE_FLAG_CELL or NODE_FLAG_NODE, they make
    // four good ways that a random Move_Value() might fail in the debug
    // build.  It could also become useful if one wanted a more "serious"
    // form of trashing than TRASH_CELL_IF_DEBUG().
    //
  #ifdef DEBUG_TRASH_MEMORY
    DECLARE_LOCAL (freed_cell);
    freed_cell->header.bits =
        NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
        | FLAG_KIND_BYTE(REB_T_TRASH)
        | VALUE_FLAG_FALSEY; // speeds up VAL_TYPE_Debug() check
    assert(Detect_Rebol_Pointer(freed_cell) == DETECTED_AS_FREED_CELL);
  #endif

    DECLARE_LOCAL (end_cell);
    SET_END(end_cell);
    assert(Detect_Rebol_Pointer(end_cell) == DETECTED_AS_END);
    assert(Detect_Rebol_Pointer(END_NODE) == DETECTED_AS_END);
    assert(Detect_Rebol_Pointer(rebEND) == DETECTED_AS_END);

    // An Endlike_Header() can use the NODE_FLAG_MANAGED bit however it wants.
    // But the canon END_NODE is not managed, which was once used for a trick
    // of using it vs. nullptr...but that trick isn't being used right now.
    //
    assert(not (END_NODE->header.bits & NODE_FLAG_MANAGED));

    REBSER *ser = Make_Ser(1, sizeof(char));
    assert(Detect_Rebol_Pointer(ser) == DETECTED_AS_SERIES);
    Free_Unmanaged_Series(ser);
    assert(Detect_Rebol_Pointer(ser) == DETECTED_AS_FREED_SERIES);
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
REBCNT Check_Memory_Debug(void)
{
    REBSEG *seg;
    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        REBSER *s = cast(REBSER*, seg + 1);

        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; --n, ++s) {
            if (IS_FREE_NODE(s))
                continue;

            if (GET_SER_FLAG(s, NODE_FLAG_CELL))
                continue; // a pairing

            if (not IS_SER_DYNAMIC(s))
                continue; // data lives in the series node itself

            if (SER_REST(s) == 0)
                panic (s); // zero size allocations not legal

            REBCNT pool_num = FIND_POOL(SER_TOTAL(s));
            if (pool_num >= SER_POOL)
                continue; // size doesn't match a known pool

            if (Mem_Pools[pool_num].wide != SER_TOTAL(s))
                panic (s);
        }
    }

    REBCNT total_free_nodes = 0;

    REBCNT pool_num;
    for (pool_num = 0; pool_num != SYSTEM_POOL; pool_num++) {
        REBCNT pool_free_nodes = 0;

        REBNOD *node = Mem_Pools[pool_num].first;
        for (; node != NULL; node = node->next_if_free) {
            assert(IS_FREE_NODE(node));

            ++pool_free_nodes;

            bool found = false;
            seg = Mem_Pools[pool_num].segs;
            for (; seg != NULL; seg = seg->next) {
                if (
                    cast(uintptr_t, node) > cast(uintptr_t, seg)
                    and (
                        cast(uintptr_t, node)
                        < cast(uintptr_t, seg) + cast(uintptr_t, seg->size)
                    )
                ){
                    if (found) {
                        printf("node belongs to more than one segment\n");
                        panic (node);
                    }

                    found = true;
                }
            }

            if (not found) {
                printf("node does not belong to one of the pool's segments\n");
                panic (node);
            }
        }

        if (Mem_Pools[pool_num].free != pool_free_nodes)
            panic ("actual free node count does not agree with pool header");

        total_free_nodes += pool_free_nodes;
    }

    return total_free_nodes;
}


//
//  Dump_All_Series_Of_Size: C
//
void Dump_All_Series_Of_Size(REBCNT size)
{
    REBCNT count = 0;

    REBSEG *seg;
    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        REBSER *s = cast(REBSER*, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; --n, ++s) {
            if (IS_FREE_NODE(s))
                continue;

            if (SER_WIDE(s) == size) {
                ++count;
                printf(
                    "%3d %4d %4d\n",
                    cast(int, count),
                    cast(int, SER_LEN(s)),
                    cast(int, SER_REST(s))
                );
            }
            fflush(stdout);
        }
    }
}


//
//  Dump_Series_In_Pool: C
//
// Dump all series in pool @pool_id, UNKNOWN (-1) for all pools
//
void Dump_Series_In_Pool(REBCNT pool_id)
{
    REBSEG *seg;
    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        REBSER *s = cast(REBSER*, seg + 1);
        REBCNT n = 0;
        for (n = Mem_Pools[SER_POOL].units; n > 0; --n, ++s) {
            if (IS_FREE_NODE(s))
                continue;

            if (GET_SER_FLAG(s, NODE_FLAG_CELL))
                continue; // pairing

            if (
                pool_id == UNKNOWN
                or (
                    IS_SER_DYNAMIC(s)
                    and pool_id == FIND_POOL(SER_TOTAL(s))
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
    REBCNT total = 0;
    REBCNT tused = 0;

    REBCNT n;
    for (n = 0; n != SYSTEM_POOL; n++) {
        REBCNT segs = 0;
        REBCNT size = 0;

        size = segs = 0;

        REBSEG *seg;
        for (seg = Mem_Pools[n].segs; seg; seg = seg->next, segs++)
            size += seg->size;

        REBCNT used = Mem_Pools[n].has - Mem_Pools[n].free;
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
//  Inspect_Series: C
//
// !!! This is an old routine which was exposed through STATS to "expert
// users".  Its purpose is to calculate the total amount of memory currently
// in use by series, but it could also print out a breakdown of categories.
//
REBU64 Inspect_Series(bool show)
{
    REBCNT segs = 0;
    REBCNT tot = 0;
    REBCNT blks = 0;
    REBCNT strs = 0;
    REBCNT unis = 0;
    REBCNT odds = 0;
    REBCNT fre = 0;

    REBCNT seg_size = 0;
    REBCNT str_size = 0;
    REBCNT uni_size = 0;
    REBCNT blk_size = 0;
    REBCNT odd_size = 0;

    REBU64 tot_size = 0;

    REBSEG *seg;
    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {

        seg_size += seg->size;
        segs++;

        REBSER *s = cast(REBSER*, seg + 1);

        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; n--) {
            if (IS_FREE_NODE(s)) {
                ++fre;
                continue;
            }

            ++tot;

            if (GET_SER_FLAG(s, NODE_FLAG_CELL))
                continue;

            tot_size += SER_TOTAL_IF_DYNAMIC(s); // else 0

            if (IS_SER_ARRAY(s)) {
                blks++;
                blk_size += SER_TOTAL_IF_DYNAMIC(s);
            }
            else if (SER_WIDE(s) == 1) {
                strs++;
                str_size += SER_TOTAL_IF_DYNAMIC(s);
            }
            else if (SER_WIDE(s) == sizeof(REBUNI)) {
                unis++;
                uni_size += SER_TOTAL_IF_DYNAMIC(s);
            }
            else if (SER_WIDE(s)) {
                odds++;
                odd_size += SER_TOTAL_IF_DYNAMIC(s);
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
        printf("Series Memory Info:\n");
        printf("  REBVAL size = %lu\n", cast(unsigned long, sizeof(REBVAL)));
        printf("  REBSER size = %lu\n", cast(unsigned long, sizeof(REBSER)));
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
        printf("  %lu bytes node-space\n", cast(unsigned long, fre_size));
        printf("\n");
    }

    fflush(stdout);

    return tot_size;
}

#endif
