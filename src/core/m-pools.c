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
// Copyright 2012-2016 Rebol Open Source Contributors
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
    char *ptr = cast(char *, mem) - sizeof(REBI64);
    if (*cast(REBI64 *, ptr) == cast(REBI64, -1020)) {
        Debug_Fmt("** Free_Mem() likely used on OS_Alloc_Mem() memory!");
        Debug_Fmt("** You should use OS_FREE() instead of FREE().");
        assert(FALSE);
    }
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
//  Init_Pools: C
//
// Initialize memory pool array.
//
void Init_Pools(REBINT scale)
{
    REBCNT n;
    REBINT unscale = 1;

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

    if (scale == 0) scale = 1;
    else if (scale < 0) unscale = -scale, scale = 1;

    // Copy pool sizes to new pool structure:
    Mem_Pools = ALLOC_N(REBPOL, MAX_POOLS);
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
            panic (Error(RE_POOL_ALIGNMENT));
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
    GC_Manuals = Make_Series(15, sizeof(REBSER *), MKS_NONE | MKS_GC_MANUALS);

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
    REBSEG *seg = Mem_Pools[SER_POOL].segs;
    for(; seg != NULL; seg = seg->next) {
        REBSER *series = cast(REBSER*, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; n--, series++) {
            UNPOISON_MEMORY(series, sizeof(REBSER));
            if (IS_FREE_NODE(series))
                continue;

            printf("Leaked series at shutdown");
            Panic_Series(series);
        }
        UNPOISON_MEMORY(seg, sizeof(REBSEG)); // for access to seg->next
    }
#endif

    REBCNT pool_num;
    for (pool_num = 0; pool_num < MAX_POOLS; pool_num++) {
        REBPOL *pool = &Mem_Pools[pool_num];
        REBCNT mem_size = pool->wide * pool->units + sizeof(REBSEG);

        REBSEG *seg = pool->segs;
        while (seg) {
            REBSEG *next;
            UNPOISON_MEMORY(seg, sizeof(REBSEG)); // for access to seg->next
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
        // The release build of the core doesn't want to link in printf.
        // It's used here because all the alloc-dependent outputting code
        // will not work at this point.  Exit normally instead of asserting
        // to make it easier for those tools.
        //
        if (PG_Mem_Usage <= MAX_U32)
            printf("*** PG_Mem_Usage = %u ***\n", cast(REBCNT, PG_Mem_Usage));
        else
            printf("*** PG_Mem_Usage > MAX_U32 ***\n");

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
    if (!seg) panic (Error_No_Memory(mem_size));

    seg->size = mem_size;
    seg->next = pool->segs;
    pool->segs = seg;
    pool->free += units;
    pool->has += units;

    // Add new nodes to the end of free list:

    REBNOD *node = cast(REBNOD*, seg + 1);

    if (pool->first == NULL) {
        assert(pool->last == NULL);
        pool->first = node;
    }
    else {
        assert(pool->last != NULL);
        UNPOISON_MEMORY(pool->last, pool->wide);
        pool->last->next_if_free = node;
        POISON_MEMORY(pool->last, pool->wide);
    }

    while (TRUE) {
        struct Reb_Header *alias = &node->header; // pointer alias
        alias->bits = 0; // alias ensures compiler invalidates ALL Reb_Headers

        if (--units == 0) {
            node->next_if_free = NULL;
            break;
        }

        node->next_if_free = cast(REBNOD*, cast(REBYTE*, node) + pool->wide);
        node = node->next_if_free;
    }

    pool->last = node;

    POISON_MEMORY(seg, mem_size);
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
    if (!pool->first) Fill_Pool(pool);

    REBNOD *node = pool->first;

    UNPOISON_MEMORY(node, pool->wide);

    pool->first = node->next_if_free;
    if (node == pool->last)
        pool->last = NULL;

    pool->free--;

    assert(cast(REBUPT, node) % sizeof(REBI64) == 0);
    assert(node->header.bits == 0); // client needs to change to non-zero

    return cast(void *, node);
}


//
//  Free_Node: C
//
// Free a node, returning it to its pool.  Once it is freed, its header will
// be set to 0.  This will identify the node as not in use to anyone who
// enumerates the nodes in the pool (such as the garbage collector).
//
void Free_Node(REBCNT pool_id, void *pv)
{
    REBNOD *node = cast(REBNOD*, pv);
    assert(node->header.bits != 0); // 0 would indicate already free
    node->header.bits = 0;

    REBPOL *pool = &Mem_Pools[pool_id];

    if (pool->last == NULL) // Fill pool if empty
        Fill_Pool(pool);

    // insert an empty segment, such that this node won't be picked by
    // next Make_Node to enlongate the poisonous time of this area to
    // catch stale pointers

    UNPOISON_MEMORY(pool->last, pool->wide);
    pool->last->next_if_free = node;
    POISON_MEMORY(pool->last, pool->wide);
    pool->last = node;
    node->next_if_free = NULL;

    POISON_MEMORY(node, pool->wide);

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
static REBOOL Series_Data_Alloc(
    REBSER *s,
    REBCNT length,
    REBYTE wide,
    REBCNT flags
) {
    REBCNT size; // size of allocation (possibly bigger than we need)

    REBCNT pool_num = FIND_POOL(length * wide);

    // Data should have not been allocated yet OR caller has extracted it
    // and nulled it to indicate taking responsibility for freeing it.
    assert(s->content.dynamic.data == NULL);

    // !!! See BYTE_SIZE() for the rationale, and consider if this is a
    // good tradeoff to be making.
    //
    assert(wide == 1 || (wide & 1) != 1);

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
        if (flags & MKS_POWER_OF_2) {
            REBCNT len = 2048;
            while(len < size)
                len *= 2;
            size = len;

            // Only set the power of 2 flag if it adds information, e.g. if
            // the size doesn't divide evenly by the item width
            //
            if (size % wide != 0)
                SET_SER_FLAG(s, SERIES_FLAG_POWER_OF_2);
            else
                CLEAR_SER_FLAG(s, SERIES_FLAG_POWER_OF_2);
        }
        else
            CLEAR_SER_FLAG(s, SERIES_FLAG_POWER_OF_2);

        s->content.dynamic.data = ALLOC_N(REBYTE, size);
        if (s->content.dynamic.data == NULL)
            return FALSE;

        Mem_Pools[SYSTEM_POOL].has += size;
        Mem_Pools[SYSTEM_POOL].free++;
    }

    // Keep flags like SERIES_FLAG_FIXED_SIZE, but use new width and bias to 0
    //
    SER_SET_WIDE(s, wide);

    // Note: Bias field may contain other flags at some point.  Because
    // SER_SET_BIAS() uses bit masking on an existing value, we are sure
    // here to clear out the whole value for starters.
    //
    s->content.dynamic.bias = 0;

    if (flags & MKS_ARRAY) {
        assert(wide == sizeof(REBVAL));
        SET_SER_FLAG(s, SERIES_FLAG_ARRAY);
        assert(Is_Array_Series(s));
    }
    else {
        CLEAR_SER_FLAG(s, SERIES_FLAG_ARRAY);
        assert(!Is_Array_Series(s));
    }

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
    SET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC);

    // See if allocation tripped our need to queue a garbage collection

    if ((GC_Ballast -= size) <= 0) SET_SIGNAL(SIG_RECYCLE);

#if !defined(NDEBUG)
    if (pool_num >= SYSTEM_POOL)
        assert(Series_Allocation_Unpooled(s) == size);
#endif

    if (flags & MKS_ARRAY) {
#if !defined(NDEBUG)
        REBCNT n;

        PG_Reb_Stats->Blocks++;

        // For REBVAL-valued-arrays, we mark as trash to mark the "settable"
        // bit, heeded by both SET_END() and RESET_HEADER().  See remarks on
        // WRITABLE_MASK_DEBUG for why this is done.
        //
        // Note that the "len" field of the series (its number of valid
        // elements as maintained by the client) will be 0.  As far as this
        // layer is concerned, we've given back `length` entries for the
        // caller to manage...they do not know about the ->rest
        //
        for (n = 0; n < length; n++)
            INIT_CELL_IF_DEBUG(ARR_AT(AS_ARRAY(s), n));

        // !!! We should intentionally mark the overage range as being a
        // kind of trash that is both not an end *and* not possible to set.
        // (The series must go through an expansion to overrule this.)  That
        // is complicated logic that is likely best done in the context of
        // a simplifying review of the series mechanics themselves, so
        // for now we just use ordinary trash...which means we don't get
        // as much potential debug warning as we might when writing into
        // bias or tail capacity.
        //
        for(; n < s->content.dynamic.rest - 1; n++) {
            INIT_CELL_IF_DEBUG(ARR_AT(AS_ARRAY(s), n));
          /*MARK_CELL_UNWRITABLE_IF_CPP_DEBUG(ARR_AT(AS_ARRAY(s), n);*/
        }
    #endif

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
        RELVAL *ultimate = ARR_AT(AS_ARRAY(s), s->content.dynamic.rest - 1);
        ultimate->header.bits = 0;
    #if !defined(NDEBUG)
        Set_Track_Payload_Debug(ultimate, __FILE__, __LINE__);
    #endif
    }

    return TRUE;
}


#if !defined(NDEBUG)

//
//  Try_Find_Containing_Series_Debug: C
//
// This debug-build-only routine will look to see if it can find what series
// a data pointer lives in.  It returns NULL if it can't find one.  It's very
// slow, because it has to look at all the series.  Use sparingly!
//
REBSER *Try_Find_Containing_Series_Debug(const void *p)
{
    REBSEG *seg;

    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        REBSER *s = cast(REBSER*, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; ++n, ++s) {
            if (IS_FREE_NODE(s))
                continue;

            if (s->header.bits & CELL_MASK) { // a pairing, REBSER is REBVAL[2]
                if ((p >= cast(void*, s)) && (p < cast(void*, s + 1))) {
                    printf("pointer found in 'pairing' series");
                    printf("not a real REBSER, no information available");
                    assert(FALSE);
                }
                continue;
            }

            if (NOT(GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC))) {
                if (
                    p >= cast(void*, &s->content)
                    && p < cast(void*, &s->content + 1)
                ){
                    return s;
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

            if (p > cast(void*, s->content.dynamic.data
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
                return s;
            }

            if (p > cast(void*,
                s->content.dynamic.data
                + (SER_WIDE(s) * SER_LEN(s))
            )) {
                printf("Pointer found in freed tail capacity of series\n");
                fflush(stdout);
                return s;
            }

            return s;
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
//  Make_Series: C
//
// Make a series of a given capacity and width (unit size).
// If the data is tiny enough, it will be fit into the series node itself.
// Small series will be allocated from a memory pool.
// Large series will be allocated from system memory.
// The series will be zero length to start with.
//
REBSER *Make_Series(REBCNT capacity, REBYTE wide, REBCNT flags)
{
    // PRESERVE flag only makes sense for Remake_Series, where there is
    // previous data to be kept.
    assert(!(flags & MKS_PRESERVE));
    assert(wide != 0 && capacity != 0); // not allowed

    if (cast(REBU64, capacity) * wide > MAX_I32)
        fail (Error_No_Memory(cast(REBU64, capacity) * wide));

#if !defined(NDEBUG)
    PG_Reb_Stats->Series_Made++;
    PG_Reb_Stats->Series_Memory += capacity * wide;
#endif

    REBSER *s = cast(REBSER*, Make_Node(SER_POOL));

    // Header bits can't be zero.  For now, set the NOT_END_MASK always (the
    // CELL_MASK is used by "Paireds").
    //
    s->header.bits = NOT_END_MASK;

    if ((GC_Ballast -= sizeof(REBSER)) <= 0) SET_SIGNAL(SIG_RECYCLE);

#if !defined(NDEBUG)
    //
    // For debugging purposes, it's nice to be able to crash on some
    // kind of guard for tracking the call stack at the point of allocation
    // if we find some undesirable condition that we want a trace from
    //
    s->guard = cast(int*, malloc(sizeof(*s->guard)));
    free(s->guard);

    TRASH_POINTER_IF_DEBUG(s->link.keylist);
    TRASH_POINTER_IF_DEBUG(s->misc.canon);

    // It's necessary to have another value in order to round out the size of
    // the pool node so pointer-aligned entries are given out, so might as well
    // make that hold a useful value--the tick count when the series was made
    //
    s->do_count = TG_Do_Count;
#endif

    // The info bits must be able to implicitly terminate the `content`,
    // so that if a REBVAL is in slot [0] then it would appear terminated
    // if the [1] slot was read.
    //
    Init_Header_Aliased(&s->info, 0); // will act as unwritable END marker
    assert(IS_END(&s->content.values[1])); // test by using Reb_Value pointer

    s->content.dynamic.data = NULL;

    if (flags & MKS_EXTERNAL) {
        //
        // External series will poke in their own data pointer after the
        // REBSER header allocation is done.  Note that despite using a
        // data pointer, it is still considered a dynamic series...as it
        // uses fields in `content.dynamic` (for length and data)
        //
        SER_SET_WIDE(s, wide);
        SET_SER_FLAGS(s, SERIES_FLAG_EXTERNAL | SERIES_FLAG_HAS_DYNAMIC);
        s->content.dynamic.rest = capacity;
        s->content.dynamic.len = 0;
    }
    else if ((flags & MKS_ARRAY) && capacity <= 2) {
        //
        // An array requested of capacity 2 actually means one cell of data
        // and one cell that can serve as an END marker.  The invariant that
        // is guaranteed is that the final slot will already be written as
        // an END, and that the caller must never write it...hence it can
        // be less than a full cell's size.
        //
        SER_SET_WIDE(s, wide);
        assert(!GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC));
        SET_SER_FLAG(s, SERIES_FLAG_ARRAY);
        INIT_CELL_IF_DEBUG(&s->content.values[0]);
    }
    else if (capacity * wide <= sizeof(s->content)) {
        SER_SET_WIDE(s, wide);
        assert(!GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC));
    }
    else {
        // Allocate the actual data blob that holds the series elements

        if (!Series_Data_Alloc(s, capacity, wide, flags)) {
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

    // All series (besides the series that is the list of manual series
    // itself) start out in the list of manual series.  The only way
    // the series will be cleaned up automatically is if a trap happens,
    // or if it winds up handed to the GC to manage with MANAGE_SERIES().
    //
    // !!! Should there be a MKS_MANAGED to start a series out in the
    // managed state, for efficiency?
    //
    if (NOT(flags & MKS_GC_MANUALS)) {
        //
        // We can only add to the GC_Manuals series if the series itself
        // is not GC_Manuals...
        //
        assert(GET_SER_FLAG(GC_Manuals, SERIES_FLAG_HAS_DYNAMIC));

        if (SER_FULL(GC_Manuals)) Extend_Series(GC_Manuals, 8);

        cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.len++
        ] = s;
    }

    CHECK_MEMORY(2);

    assert(NOT(s->info.bits & NOT_END_MASK));
    assert(NOT(s->info.bits & CELL_MASK));
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
REBVAL *Alloc_Pairing(REBCTX *opt_owning_frame) {
    REBSER *s = cast(REBSER*, Make_Node(SER_POOL)); // 2x REBVAL size

    REBVAL *key = cast(REBVAL*, s);
    REBVAL *paired = key + 1;

    INIT_CELL_IF_DEBUG(key);
    if (opt_owning_frame) {
        Val_Init_Context(key, REB_FRAME, opt_owning_frame);
        SET_VAL_FLAGS(
            key, ANY_CONTEXT_FLAG_OWNS_PAIRED | REBSER_REBVAL_FLAG_ROOT
        );
    }
    else {
        // Client will need to put *something* in the key slot (accessed with
        // PAIRING_KEY).  Whatever they end up writing should be acceptable
        // to avoid a GC, since the header is not purely 0...and it works out
        // that all "ordinary" values will just act as unmanaged metadata.
        //
        SET_TRASH_IF_DEBUG(key);
    }

    INIT_CELL_IF_DEBUG(paired);
    SET_TRASH_IF_DEBUG(paired);

#if !defined(NDEBUG)
    s->guard = cast(int*, malloc(sizeof(*s->guard)));
    free(s->guard);

    s->do_count = TG_Do_Count;
#endif

    return paired;
}


//
//  Manage_Pairing: C
//
// GC management is a one-way street in Ren-C, and the paired management
// status is handled by bits directly in the first (or key's) REBVAL header.
// Switching to managed mode means the key can no longer be changed--only
// the value.
//
void Manage_Pairing(REBVAL *paired) {
    REBVAL *key = PAIRING_KEY(paired);
    SET_VAL_FLAG(key, REBSER_REBVAL_FLAG_MANAGED);
    MARK_CELL_UNWRITABLE_IF_CPP_DEBUG(key);
}


//
//  Free_Pairing: C
//
void Free_Pairing(REBVAL *paired) {
    REBVAL *key = PAIRING_KEY(paired);
    assert(!GET_VAL_FLAG(key, REBSER_REBVAL_FLAG_MANAGED));
    REBSER *series = cast(REBSER*, key);
    SET_TRASH_IF_DEBUG(paired);
    Free_Node(SER_POOL, series);

#if !defined(NDEBUG)
    series->do_count = TG_Do_Count;
#endif
}


//
//  Swap_Underlying_Series_Data: C
//
void Swap_Underlying_Series_Data(REBSER *s1, REBSER *s2)
{
    assert(SER_WIDE(s1) == SER_WIDE(s2));
    assert(Is_Array_Series(s1) == Is_Array_Series(s2));

    REBSER temp = *s1;
    *s1 = *s2;
    *s2 = temp;
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
        REBNOD *node = cast(REBNOD*, unbiased);

        assert(Mem_Pools[pool_num].wide >= size_unpooled);

        pool = &Mem_Pools[pool_num];
        node->next_if_free = pool->first;
        pool->first = node;
        pool->free++;

        struct Reb_Header *alias = &node->header;
        node->header.bits = 0;
    }
    else {
        FREE_N(REBYTE, size_unpooled, unbiased);
        Mem_Pools[SYSTEM_POOL].has -= size_unpooled;
        Mem_Pools[SYSTEM_POOL].free--;
    }

    CHECK_MEMORY(2);
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
    if (delta & 0x80000000) fail (Error(RE_PAST_END)); // 2GB max

    if (delta == 0) return;

    REBCNT len_old = SER_LEN(s);

    REBYTE wide = SER_WIDE(s);
    const REBOOL is_array = Is_Array_Series(s);

    const REBOOL was_dynamic = GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC);

    if (was_dynamic && index == 0 && SER_BIAS(s) >= delta) {

    //=//// HEAD INSERTION OPTIMIZATION ///////////////////////////////////=//

        s->content.dynamic.data -= wide * delta;
        s->content.dynamic.len += delta;
        s->content.dynamic.rest += delta;
        SER_SUB_BIAS(s, delta);

    #if !defined(NDEBUG)
        if (is_array) {
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
                INIT_CELL_IF_DEBUG(ARR_AT(AS_ARRAY(s), index));
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
        if (is_array) {
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
                INIT_CELL_IF_DEBUG(
                    ARR_AT(AS_ARRAY(s), index + delta)
                );
            }
        }
    #endif

        return;
    }

//=//// INSUFFICIENT CAPACITY, NEW ALLOCATION REQUIRED ////////////////////=//

    if (GET_SER_FLAG(s, SERIES_FLAG_FIXED_SIZE))
        panic (Error(RE_LOCKED_SERIES));

#ifndef NDEBUG
    if (Reb_Opts->watch_expand) {
        Debug_Fmt(
            "Expand %x wide: %d tail: %d delta: %d",
            s, wide, len_old, delta
        );
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
    if (!Series_Data_Alloc(
        s,
        len_old + delta + x,
        wide,
        is_array ? (MKS_ARRAY | MKS_POWER_OF_2) : MKS_POWER_OF_2
    )) {
        fail (Error_No_Memory((len_old + delta + x) * wide));
    }

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
}


//
//  Remake_Series: C
//
// Reallocate a series as a given maximum size. Content in the
// retained portion of the length may be kept as-is if the
// MKS_PRESERVE is passed in the flags.  The other flags are
// handled the same as when passed to Make_Series.
//
void Remake_Series(REBSER *s, REBCNT units, REBYTE wide, REBCNT flags)
{
    REBOOL is_array = Is_Array_Series(s);
    REBCNT len_old = SER_LEN(s);
    REBYTE wide_old = SER_WIDE(s);

#if !defined(NDEBUG)
    assert(!(flags & MKS_EXTERNAL)); // manages own memory
    assert(!GET_SER_FLAG(s, SERIES_FLAG_EXTERNAL));

    assert(is_array == LOGICAL(flags & MKS_ARRAY)); // can't switch arrayness

    if (flags & MKS_PRESERVE)
        assert(wide == wide_old); // can't change width if preserving
#endif

    assert(!GET_SER_FLAG(s, SERIES_FLAG_FIXED_SIZE));

    REBOOL was_dynamic = GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC);

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

    // !!! Currently the remake won't make a series that fits in the size of
    // a REBSER.  All series code needs a general audit, so that should be one
    // of the things considered.

    s->content.dynamic.data = NULL;

    if (!Series_Data_Alloc(
        s, units + 1, wide, is_array ? MKS_ARRAY | flags : flags
    )) {
        // Put series back how it was (there may be extant references)
        s->content.dynamic.data = data_old;
        fail (Error_No_Memory((units + 1) * wide));
    }

    if (flags & MKS_PRESERVE) {
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

    if (flags & MKS_ARRAY)
        TERM_ARRAY_LEN(AS_ARRAY(s), SER_LEN(s));
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
    assert(NOT(s->header.bits & CELL_MASK)); // use Free_Paired

    // Special handling for adjusting canons.  (REVIEW: do this by keeping the
    // symbol REBSERs in their own pools, and letting that pool's sweeper
    // do it instead of checking all series for it)
    //
    if (GET_SER_FLAG(s, SERIES_FLAG_STRING))
        GC_Kill_Interning(s);

    // Remove series from expansion list, if found:
    REBCNT n;
    for (n = 1; n < MAX_EXPAND_LIST; n++) {
        if (Prior_Expand[n] == s) Prior_Expand[n] = 0;
    }

    if (GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC)) {
        if (GET_SER_FLAG(s, SERIES_FLAG_EXTERNAL)) {
            //
            // External series have their REBSER GC'd when Rebol doesn't need it,
            // but the data pointer itself is not one that Rebol allocated
            // !!! Should the external owner be told about the GC/free event?
        }
        else {
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

            if (REB_I32_ADD_OF(GC_Ballast, size, &GC_Ballast))
                GC_Ballast = MAX_I32;
        }
    }
    else {
        assert(!GET_SER_FLAG(s, SERIES_FLAG_EXTERNAL));

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
            RELVAL *v = ARR_HEAD(AS_ARRAY(s));
            if (NOT_END(v) && IS_HANDLE(v)) {
                if (v->extra.singular == AS_ARRAY(s)) {
                    (s->misc.cleaner)(KNOWN(v));
                }
            }
        }
    }

    s->info.bits = 0; // includes width

    TRASH_POINTER_IF_DEBUG(s->link.keylist);

    Free_Node(SER_POOL, s);

    // GC may no longer be necessary:
    if (GC_Ballast > 0) CLR_SIGNAL(SIG_RECYCLE);

#if !defined(NDEBUG)
    PG_Reb_Stats->Series_Freed++;

    // Update the do count to be the count on which the series was freed
    //
    s->do_count = TG_Do_Count;
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
                printf("Series not in list of last manually added series");
                fflush(stdout);
                Panic_Series(s);
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
        Debug_Fmt("Trying to Free_Series() on an already freed series");
        Panic_Series(s);
    }

    // We can only free a series that is not under management by the
    // garbage collector
    //
    if (IS_SERIES_MANAGED(s)) {
        Debug_Fmt("Trying to Free_Series() on a series managed by GC.");
        Panic_Series(s);
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

    REBOOL was_dynamic = GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC);

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

    if (!Series_Data_Alloc(
        s, len_old + 1, cast(REBYTE, sizeof(REBUNI)), MKS_NONE
    )) {
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
        Debug_Fmt("Attempt to manage already managed series");
        Panic_Series(s);
    }
#endif

    s->header.bits |= REBSER_REBVAL_FLAG_MANAGED;

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
REBOOL Is_Value_Managed(const RELVAL *value)
{
    assert(!THROWN(value));

    if (ANY_CONTEXT(value)) {
        REBCTX *context = VAL_CONTEXT(value);
        if (IS_ARRAY_MANAGED(CTX_VARLIST(context))) {
            ASSERT_ARRAY_MANAGED(CTX_KEYLIST(context));
            return TRUE;
        }
        assert(NOT(IS_ARRAY_MANAGED(CTX_KEYLIST(context)))); // !!! untrue?
        return FALSE;
    }

    if (ANY_SERIES(value))
        return IS_SERIES_MANAGED(VAL_SERIES(value));

    return TRUE;
}


//
//  Series_In_Pool: C
//
// Confirm that the series value is in the series pool.
//
REBOOL Series_In_Pool(REBSER *series)
{
    REBSEG  *seg;
    REBSER *start;

    // Scan all series headers to check that series->size is correct:
    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        start = (REBSER *) (seg + 1);
        if (series >= start && series <= (REBSER*)((REBYTE*)start + seg->size - sizeof(REBSER)))
            return TRUE;
    }

    return FALSE;
}


#if !defined(NDEBUG)

//
//  Check_Memory: C
//
// FOR DEBUGGING ONLY:
// Traverse the free lists of all pools -- just to prove we can.
// This is useful for finding corruption from bad memory writes,
// because a write past the end of a node will destory the pointer
// for the next free area.
//
REBCNT Check_Memory(void)
{
#if !defined(NDEBUG)
    //Debug_Str("<ChkMem>");
    PG_Reb_Stats->Free_List_Checked++;
#endif

    REBSEG *seg;
    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        REBSER *s = cast(REBSER*, seg + 1);

        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; --n, ++s) {
            if (IS_FREE_NODE(s))
                continue;

            if (NOT(GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC)))
                continue;

            if (!SER_REST(s) || s->content.dynamic.data == NULL)
                panic (Error(RE_CORRUPT_MEMORY));

            // If the size matches a known pool, be sure it's a match

            REBCNT pool_num = FIND_POOL(SER_TOTAL(s));
            if (pool_num < SER_POOL && Mem_Pools[pool_num].wide != SER_TOTAL(s))
                panic (Error(RE_CORRUPT_MEMORY));
        }
    }

    REBCNT count = 0;

    REBCNT pool_num;
    for (pool_num = 0; pool_num < SYSTEM_POOL; pool_num++) {
        // Check each free node in the memory pool:

        REBNOD *node = Mem_Pools[pool_num].first;
        for (; node != NULL; node = node->next_if_free) {
            count++;
            // The node better belong to one of the pool's segments:
            for (seg = Mem_Pools[pool_num].segs; seg; seg = seg->next) {
                if ((REBUPT)node > (REBUPT)seg && (REBUPT)node < (REBUPT)seg + (REBUPT)seg->size) break;
            }
            if (!seg) panic (Error(RE_CORRUPT_MEMORY));
        }

        // The number of free nodes must agree with header:
        if (
            (Mem_Pools[pool_num].free != count) ||
            (Mem_Pools[pool_num].free == 0 && Mem_Pools[pool_num].first != 0)
        )
            panic (Error(RE_CORRUPT_MEMORY));
    }

    return count;
}


//
//  Dump_All: C
//
// Dump all series of a given size.
//
void Dump_All(REBCNT size)
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
                //Debug_Fmt("%3d %4d %4d = \"%s\"", n++, series->tail, SER_TOTAL(series), series->data);
                Debug_Fmt(
                    "%3d %4d %4d = \"%s\"",
                    ++count,
                    SER_LEN(s),
                    SER_REST(s),
                    "-" // !label
                );
            }

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

            REBOOL is_dynamic = GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC);

            if (
                pool_id == UNKNOWN
                || FIND_POOL(SER_TOTAL(s)) == pool_id
            ) {
                Debug_Fmt(
                    "%s Series %x \"%s\":"
                        " wide: %2d"
                        " size: %6d"
                        " bias: %d"
                        " tail: %d"
                        " rest: %d"
                        " flags: %x",
                    "Dump",
                    s,
                    "-", // !label
                    SER_WIDE(s),
                    SER_TOTAL(s),
                    is_dynamic ? SER_BIAS(s) : 0,
                    SER_LEN(s),
                    SER_REST(s),
                    s->info.bits // flags + width
                );

                if (Is_Array_Series(s)) {
                    Debug_Values(
                        ARR_HEAD(AS_ARRAY(s)),
                        SER_LEN(s),
                        1024 // !!! "FIXME limit
                    );
                }
                else {
                    Dump_Bytes(
                        SER_DATA_RAW(s),
                        (SER_LEN(s) + 1) * SER_WIDE(s)
                    );
                }
            }

        }
    }
}


//
//  Dump_Pools: C
//
// Print statistics about all memory pools.
//
static void Dump_Pools(void)
{
    REBSEG  *seg;
    REBCNT  segs;
    REBCNT  size;
    REBCNT  used;
    REBCNT  total = 0;
    REBCNT  tused = 0;
    REBCNT  n;

    for (n = 0; n < SYSTEM_POOL; n++) {
        size = segs = 0;

        for (seg = Mem_Pools[n].segs; seg; seg = seg->next, segs++)
            size += seg->size;

        used = Mem_Pools[n].has - Mem_Pools[n].free;
        Debug_Fmt("Pool[%-2d] %-4dB %-5d/%-5d:%-4d (%-2d%%) %-2d segs, %-07d total",
            n,
            Mem_Pools[n].wide,
            used,
            Mem_Pools[n].has,
            Mem_Pools[n].units,
            Mem_Pools[n].has ? ((used * 100) / Mem_Pools[n].has) : 0,
            segs,
            size
        );

        tused += used * Mem_Pools[n].wide;
        total += size;
    }
    Debug_Fmt("Pools used %d of %d (%2d%%)", tused, total, (tused*100) / total);
    Debug_Fmt("System pool used %d", Mem_Pools[SYSTEM_POOL].has);
    //Debug_Fmt("Raw allocator reports %d", PG_Mem_Usage);
}


//
//  Inspect_Series: C
//
REBU64 Inspect_Series(REBCNT flags)
{
    REBSEG  *seg;
    REBSER  *series;
    REBCNT  segs, n, tot, blks, strs, unis, nons, odds, fre;
    REBCNT  str_size, uni_size, blk_size, odd_size, seg_size, fre_size;
    REBOOL  f = FALSE;
    REBINT  pool_num;
#ifdef SER_LABELS
    REBYTE  *kind;
#endif
    REBU64  tot_size;

    segs = tot = blks = strs = unis = nons = odds = fre = 0;
    seg_size = str_size = uni_size = blk_size = odd_size = fre_size = 0;
    tot_size = 0;

    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {

        seg_size += seg->size;
        segs++;

        series = (REBSER *) (seg + 1);

        for (n = Mem_Pools[SER_POOL].units; n > 0; n--) {

            if (SER_WIDE(series)) {
                tot++;
                tot_size += SER_TOTAL(series);
                f = FALSE;
            } else {
                fre++;
            }

            if (Is_Array_Series(series)) {
                blks++;
                blk_size += SER_TOTAL(series);
                if (f) Debug_Fmt_("BLOCK ");
            }
            else if (SER_WIDE(series) == 1) {
                strs++;
                str_size += SER_TOTAL(series);
                if (f) Debug_Fmt_("STRING");
            }
            else if (SER_WIDE(series) == sizeof(REBUNI)) {
                unis++;
                uni_size += SER_TOTAL(series);
                if (f) Debug_Fmt_("UNICOD");
            }
            else if (SER_WIDE(series)) {
                odds++;
                odd_size += SER_TOTAL(series);
                if (f) Debug_Fmt_("ODD[%d]", SER_WIDE(series));
            }
            if (f && SER_WIDE(series)) {
                Debug_Fmt(" units: %-5d tail: %-5d bytes: %-7d", SER_REST(series), SER_LEN(series), SER_TOTAL(series));
            }

            series++;
        }
    }

    // Size up unused memory:
    for (pool_num = 0; pool_num < SYSTEM_POOL; pool_num++) {
        fre_size += Mem_Pools[pool_num].free * Mem_Pools[pool_num].wide;
    }

    if (flags & 1) {
        Debug_Fmt(
              "Series Memory Info:\n"
              "  node   size = %d\n"
              "  series size = %d\n"
              "  %-6d segs = %-7d bytes - headers\n"
              "  %-6d blks = %-7d bytes - blocks\n"
              "  %-6d strs = %-7d bytes - byte strings\n"
              "  %-6d unis = %-7d bytes - unicode strings\n"
              "  %-6d odds = %-7d bytes - odd series\n"
              "  %-6d used = %-7d bytes - total used\n"
              "  %-6d free / %-7d bytes - free headers / node-space\n"
              ,
              sizeof(REBVAL),
              sizeof(REBSER),
              segs, seg_size,
              blks, blk_size,
              strs, str_size,
              unis, uni_size,
              odds, odd_size,
              tot,  tot_size,
              fre,  fre_size   // the 2 are not related
        );
    }

    if (flags & 2) Dump_Pools();

    return tot_size;
}

#endif
