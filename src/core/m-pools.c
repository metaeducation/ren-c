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

#include "mem-pools.h" // low-level memory pool access
#include "mem-series.h" // low-level series memory access

#include "sys-int-funcs.h"


//
//  Alloc_Mem: C
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTE: Instead of Alloc_Mem, use the ALLOC and ALLOC_N wrapper macros to
// ensure the memory block being freed matches the size for the type.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Alloc_Mem is an interface for a basic memory allocator.  It is coupled with
// a Free_Mem function that clients must call with the correct size of the
// memory block to be freed.  It is thus lower-level than malloc()... whose
// where clients do not need to remember the size of the allocation to pass
// into free().
//
// One motivation behind using such an allocator in Rebol is to allow it to
// keep knowledge of how much memory the system is using.  This means it can
// decide when to trigger a garbage collection, or raise an out-of-memory error
// before the operating system would, e.g. via 'ulimit':
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
    if ((PG_Mem_Limit != 0) && (PG_Mem_Usage > PG_Mem_Limit))
        Check_Security(Canon(SYM_MEMORY), POL_EXEC, 0);

    // While conceptually a simpler interface than malloc(), the
    // current implementations on all C platforms just pass through to
    // malloc and free.

#ifdef NDEBUG
    return malloc(size);
#else
    // In debug builds we cache the size at the head of the allocation
    // so we can check it.  This also allows us to catch cases when
    // free() is paired with Alloc_Mem() instead of using Free_Mem()
    //
    // Note that we use a 64-bit quantity, as we want the allocations
    // to remain suitable in alignment for 64-bit values!

    void *ptr = malloc(size + sizeof(REBI64));
    if (ptr == NULL)
        return NULL;
    *cast(REBI64 *, ptr) = size;
    return cast(char *, ptr) + sizeof(REBI64);
#endif
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
// Release builds have no way to check that the correct size is passed in
// for the allocated unit.  But in debug builds the size is stored with the
// allocation and checked here.  Also, the pointer is skewed such that if
// clients try to use a normal free() and bypass Free_Mem it will trigger
// debug alerts from the C runtime of trying to free a non-head-of-malloc.
//
// We also know the host allocator (OS_Alloc_Mem) uses a similar trick.  But
// since it doesn't require callers to remember the size, it puts a known
// garbage value for this routine to check for--to give a useful message.
//
void Free_Mem(void *mem, size_t size)
{
#ifdef NDEBUG
    free(mem);
#else
    assert(mem != NULL);
    char *ptr = cast(char *, mem) - sizeof(REBI64);
    if (*cast(REBI64 *, ptr) == cast(REBI64, -1020))
        panic ("** FREE() used on OS_Alloc_Mem() memory instead of FREE()");

    assert(*cast(REBI64*, ptr) == cast(REBI64, size));
    free(ptr);
#endif
    PG_Mem_Usage -= size;
}


#define POOL_MAP

#ifdef POOL_MAP
    #ifdef NDEBUG
        #define FIND_POOL(n) \
            ((n <= 4 * MEM_BIG_SIZE) \
                ? cast(REBCNT, PG_Pool_Map[n]) \
                : cast(REBCNT, SYSTEM_POOL))
    #else
        #define FIND_POOL(n) \
            ((!PG_Always_Malloc && (n <= 4 * MEM_BIG_SIZE)) \
                ? cast(REBCNT, PG_Pool_Map[n]) \
                : cast(REBCNT, SYSTEM_POOL))
    #endif
#else
    #ifdef NDEBUG
        #define FIND_POOL(n) Find_Pool(n)
    #else
        #define FIND_POOL(n) (PG_Always_Malloc ? SYSTEM_POOL : Find_Pool(n))
    #endif
#endif

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
    const char *env_always_malloc = NULL;
    env_always_malloc = getenv("R3_ALWAYS_MALLOC");
    if (env_always_malloc != NULL && atoi(env_always_malloc) != 0) {
        Debug_Str(
            "**\n"
            "** R3_ALWAYS_MALLOC is TRUE in environment variable!\n"
            "** Memory allocations aren't pooled, expect slowness...\n"
            "**\n"
        );
        PG_Always_Malloc = TRUE;
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

        // The current invariant is that allocations returned from Make_Node()
        // should always come back as being at a legal 64-bit alignment point.
        // Although it would be possible to round the allocations, turning it
        // into an alert helps make sure available space isn't idly wasted.
        //
        // A panic is used instead of an assert, since the debug sizes and
        // release sizes may be different...and both must be checked.
        //
        if (Mem_Pool_Spec[n].wide % sizeof(REBI64) != 0)
            panic ("memory pool width is not 64-bit aligned");

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
    GC_Manuals = Make_Series_Core(15, sizeof(REBSER *), NODE_FLAG_MANAGED);
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
    // Can't use Free_Series() because GC_Manuals couldn't be put in
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
static void Fill_Pool(REBPOL *pool)
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

    if (pool->first == NULL) {
        assert(pool->last == NULL);
        pool->first = node;
    }
    else {
        assert(pool->last != NULL);
        pool->last->next_if_free = node;
    }

    while (TRUE) {
        //
        // See Init_Endlike_Header() for why we do this
        //
        struct Reb_Header *alias = &node->header;
        alias->bits = FLAGBYTE_FIRST(FREED_SERIES_BYTE);

        if (--units == 0) {
            node->next_if_free = NULL;
            break;
        }

        // Can't use NOD() here because it tests for NODE_FLAG_FREE
        //
        node->next_if_free = cast(REBNOD*, cast(REBYTE*, node) + pool->wide);
        node = node->next_if_free;
    }

    pool->last = node;
}


//
//  Make_Node: C
//
// Allocate a node from a pool.  If the pool has run out of nodes, it will
// be refilled.
//
// The node will not be zero-filled.  However its header bits will be
// guaranteed to be zero--which is the same as the state of all freed nodes.
// Callers likely want to change this to not be zero, so that zero can be
// used to recognize freed nodes if they enumerate the pool themselves.
//
// All nodes are 64-bit aligned.  This way, data allocated in nodes can be
// structured to know where legal 64-bit alignment points would be.  This
// is required for correct functioning of some types.  (See notes on
// alignment in %sys-rebval.h.)
//
void *Make_Node(REBCNT pool_id)
{
    REBPOL *pool = &Mem_Pools[pool_id];
    if (pool->first == NULL)
        Fill_Pool(pool);

    assert(pool->first != NULL);

    REBNOD *node = pool->first;

    pool->first = node->next_if_free;
    if (node == pool->last)
        pool->last = NULL;

    pool->free--;

    assert(cast(REBUPT, node) % sizeof(REBI64) == 0);
    assert(IS_FREE_NODE(node)); // client needs to change to non-zero

    return cast(void *, node);
}


//
//  Free_Node: C
//
// Free a node, returning it to its pool.  Once it is freed, its header will
// be set to 0.  This will identify the node as not in use to anyone who
// enumerates the nodes in the pool (such as the garbage collector).
//
void Free_Node(REBCNT pool_id, void *p)
{
    REBNOD *node = NOD(p);

    // See Init_Endlike_Header() for why we do this
    //
    struct Reb_Header *alias = &node->header;
    alias->bits = FLAGBYTE_FIRST(FREED_SERIES_BYTE);

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

    if (pool->last == NULL) // Fill pool if empty
        Fill_Pool(pool);

    assert(pool->last != NULL);

    pool->last->next_if_free = node;
    pool->last = node;
    node->next_if_free = NULL;

#endif

    pool->free++;
}


//
//  Series_Data_Alloc: C
//
// Allocates element array for an already allocated REBSER node structure.
// Resets the bias and tail to zero, and sets the new width.  Flags like
// SERIES_FLAG_FIXED_SIZE are left as they were, and other fields in the
// series structure are untouched.
//
// This routine can thus be used for an initial construction or an operation
// like expansion.  Currently not exported from this file.
//
static REBOOL Series_Data_Alloc(REBSER *s, REBCNT length) {
    //
    // Data should have not been allocated yet OR caller has extracted it
    // and nulled it to indicate taking responsibility for freeing it.
    //
    assert(s->content.dynamic.data == NULL);

    REBYTE wide = SER_WIDE(s);
    assert(wide != 0);

    REBCNT size; // size of allocation (possibly bigger than we need)

    REBCNT pool_num = FIND_POOL(length * wide);
    if (pool_num < SYSTEM_POOL) {
        // ...there is a pool designated for allocations of this size range
        s->content.dynamic.data = cast(REBYTE*, Make_Node(pool_num));
        if (s->content.dynamic.data == NULL)
            return FALSE;

        // The pooled allocation might wind up being larger than we asked.
        // Don't waste the space...mark as capacity the series could use.
        size = Mem_Pools[pool_num].wide;
        assert(size >= length * wide);

        // We don't round to power of 2 for allocations in memory pools
        CLEAR_SER_FLAG(s, SERIES_FLAG_POWER_OF_2);
    }
    else {
        // ...the allocation is too big for a pool.  But instead of just
        // doing an unpooled allocation to give you the size you asked
        // for, the system does some second-guessing to align to 2Kb
        // boundaries (or choose a power of 2, if requested).

        size = length * wide;
        if (GET_SER_FLAG(s, SERIES_FLAG_POWER_OF_2)) {
            REBCNT len = 2048;
            while(len < size)
                len *= 2;
            size = len;

            // Clear the power of 2 flag if it isn't necessary, due to even
            // divisibility by the item width.
            //
            if (size % wide == 0)
                CLEAR_SER_FLAG(s, SERIES_FLAG_POWER_OF_2);
        }

        s->content.dynamic.data = ALLOC_N(REBYTE, size);
        if (s->content.dynamic.data == NULL)
            return FALSE;

        Mem_Pools[SYSTEM_POOL].has += size;
        Mem_Pools[SYSTEM_POOL].free++;
    }

    // Note: Bias field may contain other flags at some point.  Because
    // SER_SET_BIAS() uses bit masking on an existing value, we are sure
    // here to clear out the whole value for starters.
    //
    s->content.dynamic.bias = 0;

    // The allocation may have returned more than we requested, so we note
    // that in 'rest' so that the series can expand in and use the space.
    // Note that it wastes remainder if size % wide != 0 :-(
    //
    s->content.dynamic.rest = size / wide;

    // We set the tail of all series to zero initially, but currently do
    // leave series termination to callers.  (This is under review.)
    //
    s->content.dynamic.len = 0;

    // Currently once a series becomes dynamic, it never goes back.  There is
    // no shrinking process that will pare it back to fit completely inside
    // the REBSER node.
    //
    SET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC);

    // See if allocation tripped our need to queue a garbage collection

    if ((GC_Ballast -= size) <= 0)
        SET_SIGNAL(SIG_RECYCLE);

#if !defined(NDEBUG)
    if (pool_num >= SYSTEM_POOL)
        assert(Series_Allocation_Unpooled(s) == size);
#endif

    if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY)) {
        assert(wide == sizeof(REBVAL));

        REBCNT n;

    #if !defined(NDEBUG)
        PG_Reb_Stats->Blocks++;
    #endif

        // For REBVAL-valued-arrays, we mark as trash to mark the "settable"
        // bit, heeded by both SET_END() and RESET_HEADER().  See remarks on
        // VALUE_FLAG_CELL for why this is done.
        //
        // Note that the "len" field of the series (its number of valid
        // elements as maintained by the client) will be 0.  As far as this
        // layer is concerned, we've given back `length` entries for the
        // caller to manage...they do not know about the ->rest
        //
        for (n = 0; n < length; n++)
            Prep_Non_Stack_Cell(ARR_AT(ARR(s), n));

        // !!! We should intentionally mark the overage range as not having
        // NODE_FLAG_CELL in the debug build.  Then have the series go through
        // an expansion to overrule it.
        //
        // That's complicated logic that is likely best done in the context of
        // a simplifying review of the series mechanics themselves.  So
        // for now we just use ordinary trash...which means we don't get
        // as much potential debug warning as we might when writing into
        // bias or tail capacity.
        //
        // !!! Also, should the release build do the NODE_FLAG_CELL setting
        // up front, or only on expansions?
        //
        for(; n < s->content.dynamic.rest - 1; n++) {
            Prep_Non_Stack_Cell(ARR_AT(ARR(s), n));
        }

        // The convention is that the *last* cell in the allocated capacity
        // is an unwritable end.  This may be located arbitrarily beyond the
        // capacity the user requested, if a pool unit was used that was
        // bigger than they asked for...but this will be used in expansion.
        //
        // Having an unwritable END in that spot paves the way for more forms
        // of implicit termination.  In theory one should not need 5 cells
        // to hold an array of length 4...the 5th header position can merely
        // mark termination with the low bit clear.
        //
        // Currently only singular arrays exploit this, but since they exist
        // they must be accounted for.  Because callers cannot write past the
        // capacity they requested, they must use TERM_ARRAY_LEN(), which
        // avoids writing the unwritable locations by checking for END first.
        //
        RELVAL *ultimate = ARR_AT(ARR(s), s->content.dynamic.rest - 1);
        Init_Endlike_Header(&ultimate->header, 0);
    #if !defined(NDEBUG)
        Set_Track_Payload_Debug(ultimate, __FILE__, __LINE__);
    #endif
    }

    return TRUE;
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
                if ((p >= cast(void*, s)) && (p < cast(void*, s + 1)))
                    return NOD(s); // REBSER is REBVAL[2]
                continue;
            }

            if (NOT_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC)) {
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
//  Series_Allocation_Unpooled: C
//
// When we want the actual memory accounting for a series, the whole story may
// not be told by the element size multiplied by the capacity.  The series may
// have been allocated from a pool where it was rounded up to the pool size,
// and elements may not fit evenly in that space.  Or it may be allocated from
// the "system pool" via Alloc_Mem, but rounded up to a power of 2.
//
// (Note: It's necessary to know the size because Free_Mem requires it, as
// Rebol's allocator doesn't remember the size of system pool allocations for
// you.  It also needs it in order to keep track of GC boundaries and memory
// use quotas.)
//
// Rather than pay for the cost on every series of an "actual allocation size",
// the optimization choice is to only pay for a "rounded up to power of 2" bit.
//
REBCNT Series_Allocation_Unpooled(REBSER *series)
{
    REBCNT total = SER_TOTAL(series);

    if (GET_SER_FLAG(series, SERIES_FLAG_POWER_OF_2)) {
        REBCNT len = 2048;
        while(len < total)
            len *= 2;
        return len;
    }

    return total;
}


//
//  Make_Series_Core: C
//
// Make a series of a given capacity and width (unit size).
// If the data is tiny enough, it will be fit into the series node itself.
// Small series will be allocated from a memory pool.
// Large series will be allocated from system memory.
// The series will be zero length to start with.
//
REBSER *Make_Series_Core(REBCNT capacity, REBYTE wide, REBUPT flags)
{
    assert(wide != 0 && capacity != 0); // not allowed

    if (cast(REBU64, capacity) * wide > MAX_I32)
        fail (Error_No_Memory(cast(REBU64, capacity) * wide));

#if !defined(NDEBUG)
    PG_Reb_Stats->Series_Made++;
    PG_Reb_Stats->Series_Memory += capacity * wide;
#endif

    REBSER *s = cast(REBSER*, Make_Node(SER_POOL));

    // Header bits can't be zero.  NODE_FLAG_NODE is sufficient to identify
    // this as a REBSER node that is not GC managed.
    //
    s->header.bits = NODE_FLAG_NODE | flags;

    if ((GC_Ballast -= sizeof(REBSER)) <= 0)
        SET_SIGNAL(SIG_RECYCLE);

#if !defined(NDEBUG)
    //
    // For debugging purposes, it's nice to be able to crash on some
    // kind of guard for tracking the call stack at the point of allocation
    // if we find some undesirable condition that we want a trace from
    //
    s->guard = cast(int*, malloc(sizeof(*s->guard)));
    free(s->guard);

    TRASH_POINTER_IF_DEBUG(LINK(s).trash);
    TRASH_POINTER_IF_DEBUG(MISC(s).trash);

    // It's necessary to have another value in order to round out the size of
    // the pool node so pointer-aligned entries are given out, so might as well
    // make that hold a useful value--the tick count when the series was made
    //
    s->tick = TG_Tick;
#endif

    // The info bits must be able to implicitly terminate the `content`,
    // so that if a REBVAL is in slot [0] then it would appear terminated
    // if the [1] slot was read.
    //
    Init_Endlike_Header(&s->info, 0); // acts as unwritable END marker
    assert(IS_END(cast(RELVAL*, &s->content.values[1]))); // ^-- test that

    s->content.dynamic.data = NULL;

    assert(wide != 0);
    SER_SET_WIDE(s, wide);

    if ((flags & SERIES_FLAG_ARRAY) && capacity <= 2) {
        //
        // An array requested of capacity 2 actually means one cell of data
        // and one cell that can serve as an END marker.  The invariant that
        // is guaranteed is that the final slot will already be written as
        // an END, and that the caller must never write it...hence it can
        // be less than a full cell's size.
        //
        assert(NOT_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC));
        Prep_Non_Stack_Cell(&s->content.values[0]);
    }
    else if (capacity * wide <= sizeof(s->content)) {
        assert(NOT_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC));
    }
    else {
        // Allocate the actual data blob that holds the series elements

        if (!Series_Data_Alloc(s, capacity)) {
            Free_Node(SER_POOL, s);
            fail (Error_No_Memory(capacity * wide));
        }

        // <<IMPORTANT>> - The capacity that will be given back as the ->rest
        // field may be larger than the requested size.  The memory pool API
        // is able to give back the size of the actual allocated block--which
        // includes any overage.  So to keep that from going to waste it is
        // recorded as the block's capacity, in case it ever needs to grow
        // it might be able to save on a reallocation.
    }

    // It is possible for a series to start out unmanaged and then be
    // transitioned to managed, or it may start off in a managed state.  It
    // is more efficient if you know a series is going to be managed to
    // create it in the managed state (it doesn't have to be added and
    // removed from a manuals list).  But be sure no evaluations are called
    // in that case before the places that will hold it live are set up.
    //
    // Note: The call to create GC_Manuals itself lies and says it is managed,
    // just for the moment of set up, so it doesn't try to add itself to the
    // manuals list!  It removes the managed flag after the create.
    //
    if (NOT(flags & NODE_FLAG_MANAGED)) {
        assert(GET_SER_INFO(GC_Manuals, SERIES_INFO_HAS_DYNAMIC));

        if (SER_FULL(GC_Manuals))
            Extend_Series(GC_Manuals, 8);

        cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.len++
        ] = s;
    }

    // Since we're not the scanner, the only way we can attribute a file and
    // a line number to a series created at runtime is to examine the frame
    // stack and propagate whatever file and line number information it might
    // know about from the source it's running onto this series.
    //
    if (flags & SERIES_FLAG_FILE_LINE) {
        if (FS_TOP != NULL) {
            LINK(s).file = FRM_FILE(FS_TOP);
            MISC(s).line = FRM_LINE(FS_TOP);
        }
        else
            CLEAR_SER_FLAG(s, SERIES_FLAG_FILE_LINE);
    }

    assert(s->info.bits & NODE_FLAG_END);
    assert(NOT(s->info.bits & NODE_FLAG_CELL));
    assert(SER_LEN(s) == 0);
    return s;
}


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
REBVAL *Alloc_Pairing(REBFRM *opt_owning_frame) {
    REBSER *s = cast(REBSER*, Make_Node(SER_POOL)); // 2x REBVAL size

    REBVAL *key = cast(REBVAL*, s);
    REBVAL *paired = key + 1;

    Prep_Non_Stack_Cell(key);
    if (opt_owning_frame != NULL) {
        //
        // !!! Currently this reifies the frame... but it would not have to do
        // so if FRAME! could hold a non-reified REBFRM* and if Move_Value()
        // and Derelativize() were sensitive to reifying those frames on
        // demand.  This general concept could be used for transient contexts
        // as well--consider it
        //
        Init_Any_Context(
            key,
            REB_FRAME,
            Context_For_Frame_May_Reify_Managed(opt_owning_frame)
        );
        SET_VAL_FLAGS(
            key, ANY_CONTEXT_FLAG_OWNS_PAIRED | NODE_FLAG_ROOT
        );
    }
    else {
        // Client will need to put *something* in the key slot (accessed with
        // PAIRING_KEY).  Whatever they end up writing should be acceptable
        // to avoid a GC, since the header is not purely 0...and it works out
        // that all "ordinary" values will just act as unmanaged metadata.
        //
        TRASH_CELL_IF_DEBUG(key);
    }

    Prep_Non_Stack_Cell(paired);
    TRASH_CELL_IF_DEBUG(paired);

#if !defined(NDEBUG)
    s->guard = cast(int*, malloc(sizeof(*s->guard)));
    free(s->guard);

    s->tick = TG_Tick;
#endif

    return paired;
}


//
//  Manage_Pairing: C
//
// The paired management status is handled by bits directly in the first (or
// key's) REBVAL header.
//
void Manage_Pairing(REBVAL *paired) {
    REBVAL *key = PAIRING_KEY(paired);
    SET_VAL_FLAG(key, NODE_FLAG_MANAGED);
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
    REBVAL *key = PAIRING_KEY(paired);
    assert(GET_VAL_FLAG(key, NODE_FLAG_MANAGED));
    CLEAR_VAL_FLAG(key, NODE_FLAG_MANAGED);
}


//
//  Free_Pairing: C
//
void Free_Pairing(REBVAL *paired) {
    REBVAL *key = PAIRING_KEY(paired);
    assert(NOT_VAL_FLAG(key, NODE_FLAG_MANAGED));
    REBSER *series = cast(REBSER*, key);
    TRASH_CELL_IF_DEBUG(paired);
    Free_Node(SER_POOL, series);

#if !defined(NDEBUG)
    series->tick = TG_Tick; // update to be tick on which node was freed
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
static void Free_Unbiased_Series_Data(REBYTE *unbiased, REBCNT size_unpooled)
{
    REBCNT pool_num = FIND_POOL(size_unpooled);
    REBPOL *pool;

    if (pool_num < SYSTEM_POOL) {
        //
        // The series data does not honor "node protocol" when it is in use
        // The pools are not swept the way the REBSER pool is, so only the
        // free nodes have significance to their headers.  Use a cast and not
        // NOD() because that assumes NOT(NODE_FLAG_FREE)
        //
        REBNOD *node = cast(REBNOD*, unbiased);

        assert(Mem_Pools[pool_num].wide >= size_unpooled);

        pool = &Mem_Pools[pool_num];
        node->next_if_free = pool->first;
        pool->first = node;
        pool->free++;

        // See Init_Endlike_Header() for why we do this
        //
        struct Reb_Header *alias = &node->header;
        alias->bits = FLAGBYTE_FIRST(FREED_SERIES_BYTE);
    }
    else {
        FREE_N(REBYTE, size_unpooled, unbiased);
        Mem_Pools[SYSTEM_POOL].has -= size_unpooled;
        Mem_Pools[SYSTEM_POOL].free++;
    }
}


//
//  Expand_Series: C
//
// Expand a series at a particular index point by the number
// number of units specified by delta.
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

    const REBOOL was_dynamic = GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC);

    if (was_dynamic && index == 0 && SER_BIAS(s) >= delta) {

    //=//// HEAD INSERTION OPTIMIZATION ///////////////////////////////////=//

        s->content.dynamic.data -= wide * delta;
        s->content.dynamic.len += delta;
        s->content.dynamic.rest += delta;
        SER_SUB_BIAS(s, delta);

    #if !defined(NDEBUG)
        if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY)) {
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
            !was_dynamic ||
            (
                (SER_LEN(s) + SER_BIAS(s)) * wide
                < SER_TOTAL(s)
            )
        );

        TERM_SERIES(s);

    #if !defined(NDEBUG)
        if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY)) {
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
    REBUPT n_available = 0;
    REBUPT n_found;
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
    REBYTE *data_old;
    if (was_dynamic) {
        data_old = s->content.dynamic.data;
        bias_old = SER_BIAS(s);
        size_old = Series_Allocation_Unpooled(s);
    }
    else {
        content_old = s->content; // may be raw bits
        data_old = cast(REBYTE*, &content_old);
    }

    // The new series will *always* be dynamic, because it would not be
    // expanding if a fixed size allocation was sufficient.

    s->content.dynamic.data = NULL;
    SET_SER_FLAG(s, SERIES_FLAG_POWER_OF_2);
    if (!Series_Data_Alloc(s, len_old + delta + x))
        fail (Error_No_Memory((len_old + delta + x) * wide));

    assert(s->content.dynamic.data != NULL);

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
    assert(
        GET_SER_FLAG(a, SERIES_FLAG_ARRAY)
        == GET_SER_FLAG(b, SERIES_FLAG_ARRAY)
    );

    // There are bits in the ->info and ->header which pertain to the content,
    // which includes whether the series is dynamic or if the data lives in
    // the node itself, the width (right 8 bits), etc.  Note that the length
    // of non-dynamic series lives in the header.

    REBYTE a_wide = SER_WIDE(a);
    SER_SET_WIDE(a, SER_WIDE(b));
    SER_SET_WIDE(b, a_wide);

    REBOOL a_has_dynamic = GET_SER_INFO(a, SERIES_INFO_HAS_DYNAMIC);
    if (GET_SER_INFO(b, SERIES_INFO_HAS_DYNAMIC))
        SET_SER_INFO(a, SERIES_INFO_HAS_DYNAMIC);
    else
        CLEAR_SER_INFO(a, SERIES_INFO_HAS_DYNAMIC);
    if (a_has_dynamic)
        SET_SER_INFO(b, SERIES_INFO_HAS_DYNAMIC);
    else
        CLEAR_SER_INFO(b, SERIES_INFO_HAS_DYNAMIC);

    REBCNT a_len = SER_LEN(a);
    REBCNT b_len = SER_LEN(b);

    union Reb_Series_Content a_content = a->content;
    a->content = b->content;
    b->content = a_content;

    SET_SERIES_LEN(a, b_len);
    SET_SERIES_LEN(b, a_len);
}


//
//  Remake_Series: C
//
// Reallocate a series as a given maximum size.  Content in the retained
// portion of the length will be preserved if NODE_FLAG_NODE is passed in.
//
void Remake_Series(REBSER *s, REBCNT units, REBYTE wide, REBUPT flags)
{
    // !!! This routine is being scaled back in terms of what it's allowed to
    // do for the moment; so the method of passing in flags is a bit strange.
    //
    assert((flags & ~(NODE_FLAG_NODE | SERIES_FLAG_POWER_OF_2)) == 0);

    REBOOL preserve = LOGICAL(flags & NODE_FLAG_NODE);

    REBCNT len_old = SER_LEN(s);
    REBYTE wide_old = SER_WIDE(s);

#if !defined(NDEBUG)
    if (preserve)
        assert(wide == wide_old); // can't change width if preserving
#endif

    assert(NOT_SER_FLAG(s, SERIES_FLAG_FIXED_SIZE));

    REBOOL was_dynamic = GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC);

    REBINT bias_old;
    REBINT size_old;

    // Extract the data pointer to take responsibility for it.  (The pointer
    // may have already been extracted if the caller is doing their own
    // updating preservation.)

    REBYTE *data_old;
    union Reb_Series_Content content_old;
    if (was_dynamic) {
        assert(s->content.dynamic.data != NULL);
        data_old = s->content.dynamic.data;
        bias_old = SER_BIAS(s);
        size_old = Series_Allocation_Unpooled(s);
    }
    else {
        content_old = s->content;
        data_old = cast(REBYTE*, &content_old);
    }

    // We don't want to update the header bits to reflect a new state of the
    // SERIES_FLAG_POWER_OF_2 until *after* Series_Allocation_Unpooled
    // was able to take the old state into account.
    //
    SER_SET_WIDE(s, wide);
    s->header.bits |= flags;

    // !!! Currently the remake won't make a series that fits in the size of
    // a REBSER.  All series code needs a general audit, so that should be one
    // of the things considered.

    s->content.dynamic.data = NULL;

    if (!Series_Data_Alloc(s, units + 1)) {
        // Put series back how it was (there may be extant references)
        s->content.dynamic.data = data_old;
        fail (Error_No_Memory((units + 1) * wide));
    }
    assert(s->content.dynamic.data != NULL);

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

    if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY))
        TERM_ARRAY_LEN(ARR(s), SER_LEN(s));
    else
        TERM_SEQUENCE(s);

    if (was_dynamic)
        Free_Unbiased_Series_Data(data_old - (wide_old * bias_old), size_old);
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
    assert(!IS_FREE_NODE(s));
    assert(NOT(s->header.bits & NODE_FLAG_CELL)); // use Free_Paired

    if (GET_SER_FLAG(s, SERIES_FLAG_UTF8_STRING))
        GC_Kill_Interning(s); // needs special handling to adjust canons

    // Remove series from expansion list, if found:
    REBCNT n;
    for (n = 1; n < MAX_EXPAND_LIST; n++) {
        if (Prior_Expand[n] == s) Prior_Expand[n] = 0;
    }

    if (GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC)) {
        REBCNT size = SER_TOTAL(s);

        REBYTE wide = SER_WIDE(s);
        REBCNT bias = SER_BIAS(s);
        s->content.dynamic.data -= wide * bias;
        Free_Unbiased_Series_Data(
            s->content.dynamic.data,
            Series_Allocation_Unpooled(s)
        );

        // !!! This indicates reclaiming of the space, not for the series
        // nodes themselves...have they never been accounted for, e.g. in
        // R3-Alpha?  If not, they should be...additional sizeof(REBSER),
        // also tracking overhead for that.  Review the question of how
        // the GC watermarks interact with Alloc_Mem and the "higher
        // level" allocations.

        i32 tmp;
        GC_Ballast = REB_I32_ADD_OF(GC_Ballast, size, &tmp) ? MAX_I32 : tmp;
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

        if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY)) {
            RELVAL *v = ARR_HEAD(ARR(s));
            if (NOT_END(v) && IS_HANDLE(v)) {
                if (v->extra.singular == ARR(s)) {
                    (MISC(s).cleaner)(KNOWN(v));
                }
            }
        }
    }

#if !defined(NDEBUG)
    s->info.bits = 0; // makes it look like width is 0
#endif

    TRASH_POINTER_IF_DEBUG(MISC(s).trash);
    TRASH_POINTER_IF_DEBUG(LINK(s).trash);

    Free_Node(SER_POOL, s);

    // GC may no longer be necessary:
    if (GC_Ballast > 0) CLR_SIGNAL(SIG_RECYCLE);

#if !defined(NDEBUG)
    PG_Reb_Stats->Series_Freed++;
    s->tick = TG_Tick; // update to be tick on which series was freed
#endif
}


inline static void Drop_Manual_Series(REBSER *s)
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
//  Free_Series: C
//
// Free a series, returning its memory for reuse.  You can only
// call this on series that are not managed by the GC.
//
void Free_Series(REBSER *s)
{
#if !defined(NDEBUG)
    //
    // If a series has already been freed, we'll find out about that
    // below indirectly, so better in the debug build to get a clearer
    // error that won't be conflated with a possible tracking problem
    //
    if (IS_FREE_NODE(s)) {
        printf("Trying to Free_Series() on an already freed series\n");
        panic (s);
    }

    // We can only free a series that is not under management by the
    // garbage collector
    //
    if (IS_SERIES_MANAGED(s)) {
        printf("Trying to Free_Series() on a series managed by GC.\n");
        panic (s);
    }
#endif

    Drop_Manual_Series(s);

    // With bookkeeping done, use the same routine the GC uses to free
    //
    GC_Kill_Series(s);
}


//
//  Widen_String: C
//
// Widen string from 1 byte to 2 bytes.
//
// NOTE: allocates new memory. Cached pointers are invalid.
//
void Widen_String(REBSER *s, REBOOL preserve)
{
    REBCNT len_old = SER_LEN(s);

    REBYTE wide_old = SER_WIDE(s);
    assert(wide_old == 1);

    REBOOL was_dynamic = GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC);

    REBCNT bias_old;
    REBCNT size_old;
    REBYTE *data_old;
    union Reb_Series_Content content_old;
    if (was_dynamic) {
        data_old = s->content.dynamic.data;
        bias_old = SER_BIAS(s);
        size_old = Series_Allocation_Unpooled(s);
    }
    else {
        content_old = s->content;
        data_old = cast(REBYTE*, &content_old);
    }

#if !defined(NDEBUG)
    // We may be resizing a partially constructed series, or otherwise
    // not want to preserve the previous contents
    if (preserve)
        ASSERT_SERIES(s);
#endif

    s->content.dynamic.data = NULL;

    SER_SET_WIDE(s, cast(REBYTE, sizeof(REBUNI)));
    if (!Series_Data_Alloc(s, len_old + 1)) {
        // Put series back how it was (there may be extant references)
        s->content.dynamic.data = data_old;
        fail (Error_No_Memory((len_old + 1) * sizeof(REBUNI)));
    }

    if (preserve) {
        REBYTE *bp = data_old;
        REBUNI *up = UNI_HEAD(s);

        REBCNT n;
        for (n = 0; n <= len_old; n++) up[n] = bp[n]; // includes terminator
        s->content.dynamic.len = len_old;
    }
    else {
        s->content.dynamic.len = 0;
        TERM_SEQUENCE(s);
    }

    if (was_dynamic)
        Free_Unbiased_Series_Data(data_old - (wide_old * bias_old), size_old);

    ASSERT_SERIES(s);
}


//
//  Manage_Series: C
//
// When a series is first created, it is in a state of being
// manually memory managed.  Thus, you can call Free_Series on
// it if you are sure you do not need it.  This will transition
// a manually managed series to be one managed by the GC.  There
// is no way to transition it back--once a series has become
// managed, only the GC can free it.
//
// All series that wind up in user-visible values *must* be
// managed, because the user can make copies of values
// containing that series.  When these copies are made, it's
// no longer safe to assume it's okay to free the original.
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

    Drop_Manual_Series(s);
}


//
//  Is_Value_Managed: C
//
// Determines if a value would be visible to the garbage collector or not.
// Defaults to the answer of TRUE if the value has nothing the GC cares if
// it sees or not.
//
// Note: Avoid causing conditional behavior on this casually.  It's really
// for GC internal use and ASSERT_VALUE_MANAGED.  Most code should work
// with either managed or unmanaged value states for variables w/o needing
// this test to know which it has.)
//
REBOOL Is_Value_Managed(const RELVAL *v)
{
    assert(!THROWN(v));

    // Generally this is called by GC code, and that code is supposed to be
    // tolerant of unreadable blanks in the debug build.  If a non-GC client
    // happens to not catch the alarm in this routine, they'll catch it as
    // soon as they try to do pretty much anything else with the value.
    //
    if (IS_UNREADABLE_IF_DEBUG(v))
        return TRUE;

    if (ANY_CONTEXT(v)) {
        REBCTX *c = VAL_CONTEXT(v);
        if (IS_ARRAY_MANAGED(CTX_VARLIST(c))) {
            ASSERT_ARRAY_MANAGED(CTX_KEYLIST(c));
            return TRUE;
        }
        assert(NOT(IS_ARRAY_MANAGED(CTX_KEYLIST(c)))); // !!! untrue?
        return FALSE;
    }

    if (ANY_SERIES(v))
        return IS_SERIES_MANAGED(VAL_SERIES(v));

    return TRUE;
}


//
// Detect_Rebol_Pointer: C
//
// See the elaborate explanation in %m-gc.c for how this works!  It is a
// trustworthy method for "sniffing" pointers and discerning whether it is a
// REBSER*, a REBVAL*, or a UTF-8 character string.
//
// During startup, Assert_Pointer_Detection_Working() checks that:
//
//     LEFT_8_BITS(NODE_FLAG_CELL) == 0x1
//     LEFT_8_BITS(NODE_FLAG_END) == 0x8
//
enum Reb_Pointer_Detect Detect_Rebol_Pointer(const void *p) {
    REBYTE bp = *cast(const REBYTE*, p);

    switch (bp >> 4) { // switch on the left 4 bits of the byte
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
        if (bp & 0x8)
            return DETECTED_AS_END; // may be end cell or "endlike" header
        if (bp & 0x1)
            return DETECTED_AS_VALUE; // unmanaged
        return DETECTED_AS_SERIES; // unmanaged

    case 9: // 0xb1001
        if (bp & 0x8)
            return DETECTED_AS_END; // has to be an "endlike" header
        panic (p); // would be "marked and unmanaged", not legal

    case 10: // 0b1010
    case 11: // 0b1011
        if (bp & 0x8)
            return DETECTED_AS_END;
        if (bp & 0x1)
            return DETECTED_AS_VALUE; // managed, marked if `case 11`
        return DETECTED_AS_SERIES; // managed, marked if `case 11`

    // v-- bit sequences starting with `11` are *usually* legal multi-byte
    // valid starting points for UTF-8, with only the exceptions made for
    // the illegal 192 and 193 bytes which represent freed series and trash.

    case 12: // 0b1100
        if (bp == FREED_SERIES_BYTE)
            return DETECTED_AS_FREED_SERIES;

        if (bp == TRASH_CELL_BYTE)
            return DETECTED_AS_TRASH_CELL;

        return DETECTED_AS_UTF8;

    case 13: // 0b1101
    case 14: // 0b1110
    case 15: // 0b1111
        return DETECTED_AS_UTF8;
    }

    DEAD_END;
}


//
//  Rebol_Malloc: C
//
// This routine is a malloc replacement which uses REBSER data as a backing
// store.  It returns a void* but can find a pointer to the REBSER itself,
// and can be "Rebserized" and converted into an ordinary REBSER.
//
// Currently all Rebserize()'d series are byte-sized.  It would be technically
// involved to allow other element widths, because the length/rest/bias must
// be processed in terms of the element widths, and the underlying memory
// must be allocated at an even multiple of that width.  Hence, Rebol_Malloc()
// would have to be parameterized with the width and keep it in sync, -or-
// the Rebserize() would need to undergo a reallocation and copy to a new
// buffer to round it.  The former is undesirable and complex--with a lot
// of continuous rounding and management of large series terminators.  The
// latter undermines the point of Rebserize() in the first place.
//
// Since Rebol images currently use element widths of a 32-bit unsigned int,
// this makes it not useful for that.  It is probably better to switch
// images to use BINARY! series than to complicate these routines, but that
// can be the call of whoever thinks PNG decoding is too inefficient.
//
void *Rebol_Malloc(size_t size)
{
    // The +1 is for series termination, which all series have, including
    // binary series have (even though it might be superfluous).
    //
    REBSER *s = Make_Series(sizeof(REBSER*) + size + 1, sizeof(REBYTE));
    *cast(REBSER**, BIN_HEAD(s)) = s;
    TERM_BIN_LEN(s, sizeof(REBSER*) + size); // uninitialized data

    POISON_MEMORY(BIN_HEAD(s), sizeof(REBSER*));

    return BIN_HEAD(s) + sizeof(REBSER*);
}


//
//  Rebol_Realloc: C
//
// Realloc implementation to accompany Rebol_Malloc() and Rebol_Free().
//
void *Rebol_Realloc(void *ptr, size_t new_size)
{
    if (ptr == NULL) // NULL is legal, LodePNG uses this
        return Rebol_Malloc(new_size);

    UNPOISON_MEMORY(cast(REBSER**, ptr) - 1, sizeof(REBSER*));

    REBSER *s = SER(cast(void*, *(cast(REBSER**, ptr) - 1)));
    assert(BYTE_SIZE(s));
    assert(NOT(IS_SERIES_MANAGED(s))); // holds REBSER pointer

    // Binary expansion must still account for terminator (hence the 1)
    //
    if (new_size > SER_REST(s) - sizeof(REBSER*) - 1)
        Expand_Series(
            s,
            SER_LEN(s),
            new_size - (SER_LEN(s) - sizeof(REBSER*) - 1)
        );
    TERM_BIN_LEN(s, sizeof(REBSER*) + new_size);

    POISON_MEMORY(BIN_HEAD(s), sizeof(REBSER*));

    return BIN_HEAD(s) + sizeof(REBSER*);
}


//
//  Rebol_Free: C
//
// Free implementation to accompany Rebol_Malloc() and Rebol_Realloc().  An
// alternative operation is to turn the buffer into a REBSER with Rebserize.
//
void Rebol_Free(void *ptr)
{
    if (ptr == NULL) // NULL is legal, LodePNG uses this
        return;

    UNPOISON_MEMORY(cast(REBSER**, ptr) - 1, sizeof(REBSER*));

    REBSER *s = SER(cast(void*, *(cast(REBSER**, ptr) - 1)));
    assert(BYTE_SIZE(s));
    assert(NOT(IS_SERIES_MANAGED(s))); // holds REBSER pointer
    Free_Series(s);
}


//
//  Rebserize: C
//
// If a void* was allocated via Rebol_Malloc or Rebol_Realloc, then it can
// be cheaply transitioned to being a series.  This is done by bumping the
// "bias" to treat the embedded REBSER* at the head of the series data as
// if it were unused capacity at the beginning of the series.
//
// See notes in Rebol_Malloc about why the series is byte-sized, and that
// it would be difficult or undermine efficiencty to do otherwise.
//
REBSER *Rebserize(void *ptr)
{
    UNPOISON_MEMORY(cast(REBSER**, ptr) - 1, sizeof(REBSER*));

    REBSER *s = SER(cast(void*, *(cast(REBSER**, ptr) - 1)));
    assert(BYTE_SIZE(s));
    assert(NOT(IS_SERIES_MANAGED(s))); // holds REBSER pointer

    // Don't allow Rebol_Realloc() or Rebol_Free() of ptr after Rebserize has
    // run; overwrite the self-referential handle that permits it.
    //
    TRASH_POINTER_IF_DEBUG(*cast(REBSER**, BIN_HEAD(s)));

    if (GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC)) {
        SER_SET_BIAS(s, sizeof(REBSER*));
        s->content.dynamic.data += sizeof(REBSER*);
        s->content.dynamic.rest -= sizeof(REBSER*);
    }
    else {
        // Not a big series, it fits in the REBSER node itself, so just slide
        // the data down.
        //
        memmove( // overlaps, can't use memcpy()
            BIN_HEAD(s),
            BIN_HEAD(s) + sizeof(REBSER*),
            SER_LEN(s) - sizeof(REBSER*)
        );
    }

    TERM_BIN_LEN(s, SER_LEN(s) - sizeof(REBSER*));
    return s;
}


#if !defined(NDEBUG)

//
//  Assert_Pointer_Detection_Working: C
//
// Check the conditions that are required for Detect_Rebol_Pointer() and
// Init_Endlike_Header() to work, and throw some sample cases at it to make
// sure they give the right answer.
//
void Assert_Pointer_Detection_Working(void)
{
    REBUPT cell_flag = NODE_FLAG_CELL;
    assert(LEFT_8_BITS(cell_flag) == 0x1);
    REBUPT end_flag = NODE_FLAG_END;
    assert(LEFT_8_BITS(end_flag) == 0x8);

    assert(
        SERIES_INFO_0_IS_TRUE == NODE_FLAG_NODE
        && SERIES_INFO_1_IS_FALSE == NODE_FLAG_FREE
        && SERIES_INFO_4_IS_TRUE == NODE_FLAG_END
        && SERIES_INFO_7_IS_FALSE == NODE_FLAG_CELL
    );
    assert(
        DO_FLAG_0_IS_TRUE == NODE_FLAG_NODE
        && DO_FLAG_1_IS_FALSE == NODE_FLAG_FREE
        && DO_FLAG_4_IS_TRUE == NODE_FLAG_END
        && DO_FLAG_7_IS_FALSE == NODE_FLAG_CELL
    );

    assert(Detect_Rebol_Pointer("") == DETECTED_AS_UTF8);
    assert(Detect_Rebol_Pointer("asdf") == DETECTED_AS_UTF8);

    assert(Detect_Rebol_Pointer(EMPTY_ARRAY) == DETECTED_AS_SERIES);
    assert(Detect_Rebol_Pointer(BLANK_VALUE) == DETECTED_AS_VALUE);

    DECLARE_LOCAL (trash_cell);
    assert(IS_TRASH_DEBUG(trash_cell));
    assert(Detect_Rebol_Pointer(trash_cell) == DETECTED_AS_TRASH_CELL);

    DECLARE_LOCAL (end_cell);
    SET_END(end_cell);
    assert(Detect_Rebol_Pointer(end_cell) == DETECTED_AS_END);
    assert(Detect_Rebol_Pointer(END) == DETECTED_AS_END);

    // It's not generally known that an Init_Endlike_Header() header will
    // not be managed.  But the canon END is not managed, and end cells can
    // be either managed or unmanaged...but by default, not.
    //
    assert(NOT(end_cell->header.bits & NODE_FLAG_MANAGED));
    assert(NOT(END->header.bits & NODE_FLAG_MANAGED));

    REBSER *series = Make_Series(1, sizeof(char));
    assert(Detect_Rebol_Pointer(series) == DETECTED_AS_SERIES);
    Free_Series(series);
    assert(Detect_Rebol_Pointer(series) == DETECTED_AS_FREED_SERIES);
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
    REBOOL expansion_null_found = FALSE;

    REBSEG *seg;
    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        REBSER *s = cast(REBSER*, seg + 1);

        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; --n, ++s) {
            if (IS_FREE_NODE(s))
                continue;

            if (GET_SER_FLAG(s, NODE_FLAG_CELL))
                continue; // a pairing

            if (NOT_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC))
                continue; // data lives in the series node itself

            if (SER_REST(s) == 0)
                panic (s); // zero size allocations not legal

            if (s->content.dynamic.data == NULL) {
                //
                // !!! legal during the moment of series expansion only; e.g.
                // can only be true for one series at a time (current invariant
                // which was needed as a patch so Check_Memory could be called
                // during Make_Node()...hacky, should be rethought)
                //
                if (expansion_null_found)
                    panic (s);

                expansion_null_found = TRUE;
            }

            REBCNT pool_num = FIND_POOL(SER_TOTAL(s));
            if (pool_num >= SER_POOL)
                continue; // size doesn't match a known pool

            if (Mem_Pools[pool_num].wide != SER_TOTAL(s))
                panic (s);
        }
    }

    REBCNT total_free_nodes = 0;

    REBCNT pool_num;
    for (pool_num = 0; pool_num < SYSTEM_POOL; pool_num++) {
        REBCNT pool_free_nodes = 0;

        REBNOD *node = Mem_Pools[pool_num].first;
        for (; node != NULL; node = node->next_if_free) {
            ++pool_free_nodes;

            REBOOL found = FALSE;
            seg = Mem_Pools[pool_num].segs;
            for (; seg != NULL; seg = seg->next) {
                if (
                    cast(REBUPT, node) > cast(REBUPT, seg)
                    && (
                        cast(REBUPT, node)
                        < cast(REBUPT, seg) + cast(REBUPT, seg->size)
                    )
                ){
                    if (found)
                        panic ("node belongs to more than one segment");

                    found = TRUE;
                }
            }

            if (NOT(found))
                panic ("node does not belong to one of the pool's segments");
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
                || (
                    GET_SER_INFO(s, SERIES_INFO_HAS_DYNAMIC)
                    && FIND_POOL(SER_TOTAL(s)) == pool_id
                )
            ) {
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
    for (n = 0; n < SYSTEM_POOL; n++) {
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
REBU64 Inspect_Series(REBOOL show)
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

            if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY)) {
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
    for (pool_num = 0; pool_num < SYSTEM_POOL; pool_num++) {
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
