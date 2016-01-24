/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  m-pools.c
**  Summary: memory allocation pool management
**  Section: memory
**  Author:  Carl Sassenrath
**  Notes:
**      A point of Rebol's design was to remain small and solve its
**      problems without relying on a lot of abstraction.  Its
**      memory-management was thus focused on staying low-level...and
**      being able to do efficient and lightweight allocations of
**      two major elements: series and graphic objects (GOBs).
**
**      Both series and GOBs have a fixed-size component that can
**      be easily allocated from a memory pool.  This portion is
**      called the "Node" (or NOD) in both Rebol and Red terminology;
**      it is an item whose pointer is valid for the lifetime of
**      the object, regardless of resizing.  This is where header
**      information is stored, and pointers to these objects may
**      be saved in REBVAL values; such that they are kept alive
**      by the garbage collector.
**
**      The more complicated thing to do memory pooling of is the
**      variable-sized portion of a series (currently called the
**      "series data")...as series sizes can vary widely.  But a
**      trick Rebol has is that a series might be able to take
**      advantage of being given back an allocation larger than
**      requested.  They can use it as reserved space for growth.
**
**      (Typical models for implementation of things like C++'s
**      std::vector do not reach below new[] or delete[]...which
**      are generally implemented with malloc and free under
**      the hood.  Their buffered additional capacity is done
**      assuming the allocation they get is as big as they asked
**      for...no more and no less.)
**
**      While Rebol's memory pooling is a likely-useful tool even
**      with modern alternatives, there are also useful tools
**      like Valgrind and Address Sanitizer which can more easily
**      root out bugs if each allocation and free is done
**      separately through malloc and free.  Therefore there is
**      an option for always using malloc, which you can enable
**      by setting the environment variable R3_ALWAYS_MALLOC to 1.
**
***********************************************************************/

//-- Special Debugging Options:
//#define CHAFF                 // Fill series data to crash old references
//#define HIT_END               // Panic if block tail is past block terminator.
//#define WATCH_FREED           // Show # series freed each GC
//#define MEM_STRESS            // Special torture mode enabled
//#define INSPECT_SERIES

#include "sys-core.h"

#include "mem-pools.h" // low-level memory pool access
#include "mem-series.h" // low-level series memory access

#include "sys-int-funcs.h"


//
//  Alloc_Mem: C
// 
// NOTE: Instead of Alloc_Mem, use the ALLOC and ALLOC_N
// wrapper macros to ensure the memory block being freed matches
// the appropriate size for the type.
// 
// ***********************************************************************
// 
// Alloc_Mem is an interface for a basic memory allocator.
// It is coupled with a Free_Mem function that clients must
// call with the correct size of the memory block to be freed.
// It is thus lower-level than malloc()... whose memory blocks
// remember the size of the allocation so you don't need to
// pass it into free().
// 
// One motivation behind using such an allocator in Rebol
// is to allow it to keep knowledge of how much memory the
// system is using.  This means it can decide when to trigger a
// garbage collection, or raise an out-of-memory error before
// the operating system would, e.g. via 'ulimit':
// 
//     http://stackoverflow.com/questions/1229241/
// 
// Finer-grained allocations are done with memory pooling.  But
// the blocks of memory used by the pools are still acquired
// using ALLOC_N and FREE_N.
//
void *Alloc_Mem(size_t size)
{
    // Trap memory usage limit *before* the allocation is performed

    PG_Mem_Usage += size;
    if ((PG_Mem_Limit != 0) && (PG_Mem_Usage > PG_Mem_Limit))
        Check_Security(SYM_MEMORY, POL_EXEC, 0);

    // While conceptually a simpler interface than malloc(), the
    // current implementations on all C platforms just pass through to
    // malloc and free.

#ifdef NDEBUG
    return calloc(size, 1);
#else
    {
        // In debug builds we cache the size at the head of the allocation
        // so we can check it.  This also allows us to catch cases when
        // free() is paired with Alloc_Mem() instead of using Free_Mem()

        void *ptr = malloc(size + sizeof(size_t));
        *cast(size_t *, ptr) = size;
        return cast(char *, ptr) + sizeof(size_t);
    }
#endif
}


//
//  Free_Mem: C
// 
// NOTE: Instead of Free_Mem, use the FREE and FREE_N
// wrapper macros to ensure the memory block being freed matches
// the appropriate size for the type.
//
void Free_Mem(void *mem, size_t size)
{
#ifdef NDEBUG
    free(mem);
#else
    {
        // In debug builds we will not only be able to assert the
        // correct size...but if someone tries to use a normal free()
        // and bypass Free_Mem it will trigger debug alerts from the
        // C runtime of trying to free a non-head-of-malloc.  This
        // helps in ensuring we get a balanced PG_Mem_Usage of 0 at the
        // end of the program.  We also know the host allocator uses
        // a similar trick, but since it doesn't need to remember the
        // size it puts a known garbage value for us to check for.

        char *ptr = cast(char *, mem) - sizeof(size_t);
        if (*cast(size_t *, ptr) == cast(size_t, -1020)) {
            Debug_Fmt("** Free_Mem() likely used on OS_Alloc_Mem() memory!");
            Debug_Fmt("** You should use OS_FREE() instead of FREE().");
            assert(FALSE);
        }
        assert(*cast(size_t *, ptr) == size);
        free(ptr);
    }
#endif
    PG_Mem_Usage -= size;
}


#define POOL_MAP

#define BAD_MEM_PTR ((REBYTE *)0xBAD1BAD1)

//#define GC_TRIGGER (GC_Active && (GC_Ballast <= 0 || (GC_Pending && !GC_Disabled)))

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
    {8, 256},           // 0-8 Small string pool

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
    DEF_POOL(sizeof(REBLHL), 32), // external libraries
    DEF_POOL(sizeof(REBRIN), 128), // external routines
    DEF_POOL(1, 1), // Just used for tracking main memory
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
    REBCNT n;

    // !!! Ideally we would free all the manual series by calling them out by
    // name and not "cheat" here, to be sure everything is under control.
    // But for the moment we use the same sweep as the garbage collector,
    // except sweeping the series it *wasn't* responsible for freeing.
    {
        REBSEG *seg = Mem_Pools[SER_POOL].segs;
        REBCNT n;

        for (; seg != NULL; seg = seg->next) {
            REBSER *series = cast(REBSER*, seg + 1);
            for (n = Mem_Pools[SER_POOL].units; n > 0; n--, series++) {
                if (SER_FREED(series))
                    continue;

                // Free_Series asserts that a manual series is freed from
                // the manuals list.  But the GC_Manuals series was never
                // added to itself (it couldn't be!)
                if (series != GC_Manuals)
                    Free_Series(series);
            }
        }
    }

    // Can't use Free_Series() because GC_Manuals couldn't be put in
    // the manuals list...
    GC_Kill_Series(GC_Manuals);

    for (n = 0; n < MAX_POOLS; n++) {
        REBPOL *pool = &Mem_Pools[n];
        REBSEG *seg = pool->segs;
        REBCNT units = pool->units;
        REBCNT mem_size = pool->wide * units + sizeof(REBSEG);

        while (seg) {
            REBSEG *next = seg->next;
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

    // Rebol's Alloc_Mem() does not save the size of an allocation, so
    // callers of the Alloc_Free() routine must say how big the memory block
    // they are freeing is.  This information is used to decide when to GC,
    // as well as to be able to set boundaries on mem usage without "ulimit".
    // The tracked number of total memory used should balance to 0 here.
    //
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


#ifndef POOL_MAP
//
//  Find_Pool: C
// 
// Given a size, tell us what pool it belongs to.
//
static REBCNT Find_Pool(REBCNT size)
{
    if (size <= 8) return 0;  // Note: 0 - 8 (and size change for proper modulus)
    size--;
    if (size < 16 * MEM_MIN_SIZE) return MEM_TINY_POOL   + (size / MEM_MIN_SIZE);
    if (size < 32 * MEM_MIN_SIZE) return MEM_SMALL_POOLS-4 + (size / (MEM_MIN_SIZE * 4));
    if (size <  4 * MEM_BIG_SIZE) return MEM_MID_POOLS   + (size / MEM_BIG_SIZE);
    return SYSTEM_POOL;
}


//
//  Check_Pool_Map: C
// 
void Check_Pool_Map(void)
{
    int n;

    for (n = 0; n <= 4 * MEM_BIG_SIZE + 1; n++)
        if (FIND_POOL(n) != Find_Pool(n))
            Debug_Fmt("%d: %d %d", n, FIND_POOL(n), Find_Pool(n));
}
*/
#endif


//
//  Fill_Pool: C
// 
// Allocate memory for a pool.  The amount allocated will be
// determined from the size and units specified when the
// pool header was created.  The nodes of the pool are linked
// to the free list.
//
static void Fill_Pool(REBPOL *pool)
{
    REBSEG  *seg;
    REBNOD  *node;
    REBYTE  *next;
    REBCNT  units = pool->units;
    REBCNT  mem_size = pool->wide * units + sizeof(REBSEG);

    seg = cast(REBSEG *, ALLOC_N(char, mem_size));

    if (!seg) panic (Error_No_Memory(mem_size));

    // !!! See notes above whether a more limited contract between the node
    // types and the pools could prevent needing to zero all the units.
    // Also note that (for instance) there is no guarantee that memsetting
    // a pointer variable to zero will make that into a NULL pointer.
    //
    CLEAR(seg, mem_size);

    seg->size = mem_size;
    seg->next = pool->segs;
    pool->segs = seg;
    pool->free += units;
    pool->has += units;

    // Add new nodes to the end of free list:
    if (pool->last == NULL) {
        node = (REBNOD*)&pool->first;
    } else {
        node = pool->last;
        UNPOISON_MEMORY(node, pool->wide);
    }

    for (next = (REBYTE *)(seg + 1); units > 0; units--, next += pool->wide) {
        *node = (REBNOD) next;

        // !!! Were a more limited contract established between the node
        // type and the pools, this is where it would write the signal
        // into the unit that it is in a free state.  As it stands, we
        // do not know what bit the type will use...just that it uses
        // zero (of something that isn't the first pointer sized thing,
        // that we just assigned).  If it were looking for zero in
        // the second pointer sized thing, we might put this line here:
        //
        //     *(node + 1) = cast(REBNOD, 0);
        //
        // For now we just clear the remaining bits...but we do it all
        // in one call with the CLEAR() above vs. repeated calls on
        // each individual unit.  Note each unit only receives a zero
        // filling once in its lifetime; if it is freed and then reused
        // it will not be zero filled again (depending on the client to
        // have done whatever zeroing they needed to indicate the free
        // state prior to free).

        node  = cast(void**, *node);
    }

    *node = 0;
    if (pool->last != NULL) {
        POISON_MEMORY(pool->last, pool->wide);
    }
    pool->last = node;
    POISON_MEMORY(seg, mem_size);
}


//
//  Make_Node: C
// 
// Allocate a node from a pool.  If the pool has run out of
// nodes, it will be refilled.
// 
// Note that the node you get back will not be zero-filled
// in the general case.  BUT *at least one bit of the node
// will be zero*, and that one bit will *not be in the first
// pointer-sized object of your node*.  This results from the
// way that the pools and the node types must cooperate in
// order to indicate that a node is in a free state when all
// the nodes of a certain type--freed or not--are being
// enumerated (e.g. by the garbage collector).
// 
// Here's how:
// 
// When a pool segment is allocated, it will initialize all
// the units (which will become REBSERs, REBGOBs, etc.) to
// zero bytes, *except* for the first pointer-sized thing in
// each unit.  That is used whenever a unit is in the freed
// state to indicate the next free unit.  Because the unit
// has the rest of the bits zero, it can pick the zeroness
// any one of those bits to signify a free state.  However,
// when it frees the node then it must set the bit it chose
// back to zero before freeing.  Except for changes to the
// first pointer-size slot, a reused unit being handed out
// via Make_Node will have all the same bits it had when it
// was freed.
// 
// !!! Should a stricter contract be established between the
// pool and the node type about what location will be used
// to indicate the free state?  For instance, there's already
// a prescriptiveness that the first pointer-sized thing can't
// be used to indicate anything in the free state...why not
// push that to two and say that freed things always have the
// second pointer-sized thing be 0?  That would prevent the
// need for a full zero-fill, at the cost of dictating the
// layout of the node type's struct a little more.
//
void *Make_Node(REBCNT pool_id)
{
    REBNOD *node;
    REBPOL *pool;

    pool = &Mem_Pools[pool_id];
    if (!pool->first) Fill_Pool(pool);
    node = pool->first;

    UNPOISON_MEMORY(node, pool->wide);

    pool->first = cast(void**, *node);
    if (node == pool->last) {
        pool->last = NULL;
    }
    pool->free--;
    return (void *)node;
}


//
//  Free_Node: C
// 
// Free a node, returning it to its pool.  If the nodelist for
// this pool_id is going to be enumerated, then some bit of
// the data must be set to 0 prior to freeing in order to
// distinguish the allocated from free state.  (See notes on
// Make_Node.)
//
void Free_Node(REBCNT pool_id, REBNOD *node)
{
    REBPOL *pool = &Mem_Pools[pool_id];

    if (pool->last == NULL) {
        //
        // Pool is empty, so fill it.
        //
        Fill_Pool(pool);
    }

    // insert an empty segment, such that this node won't be picked by
    // next Make_Node to enlongate the poisonous time of this area to
    // catch stale pointers

    UNPOISON_MEMORY(pool->last, pool->wide);
    *(pool->last) = node;
    POISON_MEMORY(pool->last, pool->wide);
    pool->last = node;
    *node = NULL;

    POISON_MEMORY(node, pool->wide);

    pool->free++;
}


//
//  Series_Data_Alloc: C
// 
// Allocates element array for an already allocated REBSER header
// structure.  Resets the bias and tail to zero, and sets the new
// width.  Flags like SERIES_FLAG_LOCKED are left as they were, and other
// fields in the series structure are untouched.
// 
// This routine can thus be used for an initial construction
// or an operation like expansion.  Currently not exported
// from this file.
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
    assert(!s->content.dynamic.data);

    // !!! See BYTE_SIZE() for the rationale, and consider if this is a
    // good tradeoff to be making.
    //
    assert(wide == 1 || (wide & 1) != 1);

    if (pool_num < SYSTEM_POOL) {
        // ...there is a pool designated for allocations of this size range
        s->content.dynamic.data = cast(REBYTE*, Make_Node(pool_num));
        if (!s->content.dynamic.data)
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
        if (!s->content.dynamic.data)
            return FALSE;

        Mem_Pools[SYSTEM_POOL].has += size;
        Mem_Pools[SYSTEM_POOL].free++;
    }

#ifdef CHAFF
    // REVIEW: Rely completely on address sanitizer "poisoning" instead?
    memset(s->data, 0xff, size);
#endif

    // Keep tflags like SERIES_FLAG_LOCKED, but use new width and bias to 0
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

#if !defined(NDEBUG)
    if (flags & MKS_ARRAY) {
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
            VAL_INIT_WRITABLE_DEBUG(ARR_AT(AS_ARRAY(s), n));

        // !!! We should intentionally mark the overage range as being a
        // kind of trash that is both not an end *and* not possible to set.
        // (The series must go through an expansion to overrule this.)  That
        // is complicated logic that is likely best done in the context of
        // a simplifying review of the series mechanics themselves, so
        // for now we just use ordinary trash...which means we don't get
        // as much potential debug warning as we might when writing into
        // bias or tail capacity.
        //
        for(; n < s->content.dynamic.rest; n++) {
            VAL_INIT_WRITABLE_DEBUG(ARR_AT(AS_ARRAY(s), n));
          /*SET_VAL_FLAG(ARR_AT(AS_ARRAY(series), n), VALUE_FLAG_READ_ONLY);*/
        }
    }
#endif

    return TRUE;
}


#if !defined(NDEBUG)

//
//  Assert_Not_In_Series_Data_Debug: C
//
void Assert_Not_In_Series_Data_Debug(const void *pointer, REBOOL locked_ok)
{
    REBSEG *seg;

    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        REBSER *series = cast(REBSER*, seg + 1);
        REBCNT n;
        for (n = Mem_Pools[SER_POOL].units; n > 0; n--, series++) {
            if (SER_FREED(series))
                continue;

            // A locked series can be in some cases considered "safe" for the
            // purposes that this routine is checking for.  Closures use
            // series to gather their arguments, for instance.
            //
            if (locked_ok && GET_SER_FLAG(series, SERIES_FLAG_FIXED_SIZE))
                continue;

            if (pointer < cast(void*,
                series->content.dynamic.data
                - (SER_WIDE(series) * SER_BIAS(series))
            )) {
                // The memory lies before the series data allocation.
                //
                continue;
            }

            if (pointer > cast(void*, series->content.dynamic.data
                + (SER_WIDE(series) * SER_REST(series))
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

            if (pointer < cast(void*, series->content.dynamic.data)) {
                Debug_Fmt("Pointer found in freed head capacity of series");
                assert(FALSE);
            }

            if (pointer > cast(void*,
                series->content.dynamic.data
                + (SER_WIDE(series) * SER_LEN(series))
            )) {
                Debug_Fmt("Pointer found in freed tail capacity of series");
                assert(FALSE);
            }

            Debug_Fmt("Pointer not supposed to be in series data, but is.");
            assert(FALSE);
        }
    }
}

#endif


//
//  Series_Allocation_Unpooled: C
// 
// When we want the actual memory accounting for a series, the
// whole story may not be told by the element size multiplied
// by the capacity.  The series may have been allocated from
// a pool where it was rounded up to the pool size, and the
// elements may not fit evenly in that space.  Or it may have
// been allocated from the "system pool" via Alloc_Mem, but
// rounded up to a power of 2.
// 
// (Note: It's necessary to know the size because Free_Mem
// requires it, as Rebol's allocator doesn't remember the size
// of system pool allocations for you.  It also needs it in
// order to keep track of GC boundaries and memory use quotas.)
// 
// Rather than pay for the cost on every series of an "actual
// allocation size", the optimization choice is to only pay
// for a "rounded up to power of 2" bit.  (Since there are a
// LOT of series created in Rebol, each byte is scrutinized.)
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
// Make a series of a given length and width (unit size).
// Small series will be allocated from a REBOL pool.
// Large series will be allocated from system memory.
// A width of zero is not allowed.
//
REBSER *Make_Series(REBCNT length, REBYTE wide, REBCNT flags)
{
    REBSER *s;

    // PRESERVE flag only makes sense for Remake_Series, where there is
    // previous data to be kept.
    assert(!(flags & MKS_PRESERVE));
    assert(wide != 0 && length != 0);

    if (cast(REBU64, length) * wide > MAX_I32)
        fail (Error_No_Memory(cast(REBU64, length) * wide));

#if !defined(NDEBUG)
    PG_Reb_Stats->Series_Made++;
    PG_Reb_Stats->Series_Memory += length * wide;
#endif

//  if (GC_TRIGGER) Recycle();

    s = cast(REBSER*, Make_Node(SER_POOL));

    if ((GC_Ballast -= sizeof(REBSER)) <= 0) SET_SIGNAL(SIG_RECYCLE);

#if !defined(NDEBUG)
    //
    // For debugging purposes, it's nice to be able to crash on some
    // kind of guard for tracking the call stack at the point of allocation
    // if we find some undesirable condition that we want a trace from
    //
    s->guard = cast(REBINT*, malloc(sizeof(*s->guard)));
    free(s->guard);
#endif

    // The trick which is used to allow s->info to pose as an IS_END() marker
    // for data traversals inside the series node as REBVAL* has to write to
    // the info through an aliased pointer to stay on the right side of the
    // compiler.  Because writing to a `Reb_Series.info` does not naturally
    // signal memory coherence with accesses through a `Reb_Value.header`
    // as those are members of different structs.  Creating a generic alias
    // pointer tells the optimizer all bets are off and any write to the alias
    // could invalidate any Reb_Value_Header-typed field anywhere...
    {
        struct Reb_Value_Header *alias = &s->info;
        alias->bits = 0; // no NOT_END_MASK, no WRITABLE_MASK_DEBUG set...

        // Make sure it worked (so that if we interpreted the REBSER content
        // as a REBVAL it would appear terminated if the [1] slot was read.)
        //
        assert(IS_END(&s->content.values[1]));
    }

    s->content.dynamic.data = NULL;

    if (flags & MKS_EXTERNAL) {
        //
        // External series will poke in their own data pointer after the
        // REBSER header allocation is done
        //
        // !!! For the moment, external series are conflated with the frame
        // series that have only stack data and no dynamic data.  Hence we
        // initialize the REBVAL as writable here, but also set the length
        // and rest fields.  How exactly are external series used, and how
        // much of a problem is it to share the flag?  Could they set their
        // own length, rest, wide, height vs. doing it here, where those
        // fields could conceivably be just turned around and overwritten by
        // the use of the slot as a REBVAL?
        //
        VAL_INIT_WRITABLE_DEBUG(&s->content.values[0]);

        SET_SER_FLAG(s, SERIES_FLAG_EXTERNAL);
        SER_SET_WIDE(s, wide);
        s->content.dynamic.rest = length;
    }
    else {
        // Allocate the actual data blob that holds the series elements

        if (!Series_Data_Alloc(s, length, wide, flags)) {
            Free_Node(SER_POOL, cast(REBNOD*, s));
            fail (Error_No_Memory(length * wide));
        }

        // <<IMPORTANT>> - The capacity that will be given back as the ->rest
        // field may be larger than the requested size.  The memory pool API
        // is able to give back the size of the actual allocated block--which
        // includes any overage.  So to keep that from going to waste it is
        // recorded as the block's capacity, in case it ever needs to grow
        // it might be able to save on a reallocation.
    }

    // Note: This used to initialize the "extra" portion of the REBSER to 0.
    // Such initialization is a bad idea because extra is a union, and it's
    // undefined behavior to read from it if you don't know which field
    // was last assigned.

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

        if (SER_FULL(GC_Manuals)) Extend_Series(GC_Manuals, 8);

        cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.len++
        ] = s;
    }

    CHECK_MEMORY(2);

    assert(
        NOT(s->info.bits & NOT_END_MASK)
        && NOT(s->info.bits & WRITABLE_MASK_DEBUG)
    );

    return s;
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
        *node = pool->first;
        pool->first = node;
        pool->free++;
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
void Expand_Series(REBSER *series, REBCNT index, REBCNT delta)
{
    REBYTE wide = SER_WIDE(series);
    const REBOOL any_array = Is_Array_Series(series);

    REBCNT start;
    REBCNT size;
    REBCNT extra;
    REBUPT n_found;
    REBUPT n_available;
    REBCNT x;
    REBYTE *data_old;
    REBCNT size_old;
    REBINT bias_old;
    REBINT len_old;

    // ASSERT_SERIES_TERM(series);

    if (delta == 0) return;

    // Optimized case of head insertion:
    if (index == 0 && SER_BIAS(series) >= delta) {
        series->content.dynamic.data -= wide * delta;
        series->content.dynamic.len += delta;
        SER_REST(series) += delta;
        SER_SUB_BIAS(series, delta);

    #if !defined(NDEBUG)
        if (any_array) {
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
                VAL_INIT_WRITABLE_DEBUG(ARR_AT(AS_ARRAY(series), index));
        }
    #endif
        return;
    }

    // Range checks
    //
    assert(index <= series->content.dynamic.len);
    if (delta & 0x80000000) fail (Error(RE_PAST_END)); // 2GB max

    // Width adjusted variables:
    start = index * wide;
    extra = delta * wide;
    size  = (series->content.dynamic.len + 1) * wide;

    if ((size + extra) <= SER_SPACE(series)) {
        // No expansion was needed. Slide data down if necessary.
        // Note that the tail is always moved here. This is probably faster
        // than doing the computation to determine if it needs to be done.

        memmove(
            series->content.dynamic.data + start + extra,
            series->content.dynamic.data + start,
            size - start
        );

        series->content.dynamic.len += delta;

        if (
            (series->content.dynamic.len + SER_BIAS(series)) * wide
            >= SER_TOTAL(series)
        ) {
            // This shouldn't be possible, but R3-Alpha had code checking for
            // it that panicked.  Should it be made into an assert?
            //
        #if !defined(NDEBUG)
            Panic_Series(series);
        #endif
            panic (Error(RE_MISC));
        }

    #if !defined(NDEBUG)
        if (any_array) {
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
                VAL_INIT_WRITABLE_DEBUG(
                    ARR_AT(AS_ARRAY(series), index + delta)
                );
            }
        }
    #endif

        return;
    }

    // We need to expand the current series allocation.

    if (GET_SER_FLAG(series, SERIES_FLAG_FIXED_SIZE))
        panic (Error(RE_LOCKED_SERIES));

#ifndef NDEBUG
    if (Reb_Opts->watch_expand) {
        Debug_Fmt(
            "Expand %x wide: %d tail: %d delta: %d",
            series, wide, series->content.dynamic.len, delta
        );
    }
#endif

    // Create a new series that is bigger.
    // Have we recently expanded the same series?
    x = 1;
    n_available = 0;
    for (n_found = 0; n_found < MAX_EXPAND_LIST; n_found++) {
        if (Prior_Expand[n_found] == series) {
            x = series->content.dynamic.len + delta + 1; // Double the size
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

    data_old = series->content.dynamic.data;
    bias_old = SER_BIAS(series);
    size_old = Series_Allocation_Unpooled(series);
    len_old = series->content.dynamic.len;

    series->content.dynamic.data = NULL;
    if (!Series_Data_Alloc(
        series,
        series->content.dynamic.len + delta + x,
        wide,
        any_array ? (MKS_ARRAY | MKS_POWER_OF_2) : MKS_POWER_OF_2
    )) {
        fail (Error_No_Memory(
            (series->content.dynamic.len + delta + x) * wide)
        );
    }

    assert(SER_BIAS(series) == 0); // should be reset

    // If necessary, add series to the recently expanded list
    //
    if (n_found >= MAX_EXPAND_LIST)
        Prior_Expand[n_available] = series;

    // Copy the series up to the expansion point
    //
    memcpy(series->content.dynamic.data, data_old, start);

    // Copy the series after the expansion point.  If at tail, this
    // just moves the terminator to the new tail.
    //
    memcpy(
        series->content.dynamic.data + start + extra,
        data_old + start,
        size - start
    );
    series->content.dynamic.len = len_old + delta;

    // We have to de-bias the data pointer before we can free it.
    //
    Free_Unbiased_Series_Data(data_old - (wide * bias_old), size_old);

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
void Remake_Series(REBSER *series, REBCNT units, REBYTE wide, REBCNT flags)
{
    REBINT bias_old = SER_BIAS(series);
    REBINT size_old = Series_Allocation_Unpooled(series);
    REBCNT len_old = series->content.dynamic.len;
    REBYTE wide_old = SER_WIDE(series);
    REBOOL any_array = Is_Array_Series(series);

    // Extract the data pointer to take responsibility for it.  (The pointer
    // may have already been extracted if the caller is doing their own
    // updating preservation.)
    REBYTE *data_old = series->content.dynamic.data;

    assert(series->content.dynamic.data);
    series->content.dynamic.data = NULL;

    // SERIES_FLAG_EXTERNAL manages its own memory and shouldn't call Remake
    assert(!(flags & MKS_EXTERNAL));
    assert(!GET_SER_FLAG(series, SERIES_FLAG_EXTERNAL));

    // SERIES_FLAG_FIXED_SIZE has unexpandable data and shouldn't call Remake
    assert(!GET_SER_FLAG(series, SERIES_FLAG_FIXED_SIZE));

    // We only let you preserve if the data is the same width as original
#if !defined(NDEBUG)
    if (flags & MKS_PRESERVE) {
        assert(wide == wide_old);
        if (flags & MKS_ARRAY) assert(GET_SER_FLAG(series, SERIES_FLAG_ARRAY));
    }
#endif

    if (!Series_Data_Alloc(
        series, units + 1, wide, any_array ? MKS_ARRAY | flags : flags
    )) {
        // Put series back how it was (there may be extant references)
        series->content.dynamic.data = data_old;
        fail (Error_No_Memory((units + 1) * wide));
    }

    if (flags & MKS_PRESERVE) {
        // Preserve as much data as possible (if it was requested, some
        // operations may extract the data pointer ahead of time and do this
        // more selectively)

        series->content.dynamic.len = MIN(len_old, units);
        memcpy(
            series->content.dynamic.data,
            data_old,
            series->content.dynamic.len * wide
        );
    } else
        series->content.dynamic.len = 0;

    if (flags & MKS_ARRAY)
        TERM_ARRAY(AS_ARRAY(series));
    else
        TERM_SEQUENCE(series);

    Free_Unbiased_Series_Data(data_old - (wide_old * bias_old), size_old);
}


//
//  GC_Kill_Series: C
// 
// Only the garbage collector should be calling this routine.
// It frees a series even though it is under GC management,
// because the GC has figured out no references exist.
//
void GC_Kill_Series(REBSER *series)
{
    REBCNT n;
    REBCNT size = SER_TOTAL(series);

    // !!! Original comment on freeing series data said: "Protect flag can
    // be used to prevent GC away from the data field".  ???
    REBOOL protect = TRUE;

    assert(!SER_FREED(series));

#if !defined(NDEBUG)
    PG_Reb_Stats->Series_Freed++;
#endif

    // Remove series from expansion list, if found:
    for (n = 1; n < MAX_EXPAND_LIST; n++) {
        if (Prior_Expand[n] == series) Prior_Expand[n] = 0;
    }

    if (GET_SER_FLAG(series, SERIES_FLAG_EXTERNAL)) {
        // External series have their REBSER GC'd when Rebol doesn't need it,
        // but the data pointer itself is not one that Rebol allocated
        // !!! Should the external owner be told about the GC/free event?
    }
    else {
        REBYTE wide = SER_WIDE(series);
        REBCNT bias = SER_BIAS(series);
        series->content.dynamic.data -= wide * bias;
        Free_Unbiased_Series_Data(
            series->content.dynamic.data,
            Series_Allocation_Unpooled(series)
        );
    }

    series->info.bits = 0; // includes width
    //series->data = BAD_MEM_PTR;
    //series->tail = 0xBAD2BAD2;
    //series->misc.size = 0xBAD3BAD3;

    Free_Node(SER_POOL, cast(REBNOD*, series));

    if (REB_I32_ADD_OF(GC_Ballast, size, &GC_Ballast)) {
        GC_Ballast = MAX_I32;
    }

    // GC may no longer be necessary:
    if (GC_Ballast > 0) CLR_SIGNAL(SIG_RECYCLE);
}


//
//  Free_Series: C
// 
// Free a series, returning its memory for reuse.  You can only
// call this on series that are not managed by the GC.
//
void Free_Series(REBSER *series)
{
    REBSER ** const last_ptr
        = &cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.len - 1
        ];

#if !defined(NDEBUG)
    //
    // If a series has already been freed, we'll find out about that
    // below indirectly, so better in the debug build to get a clearer
    // error that won't be conflated with a possible tracking problem
    //
    if (SER_FREED(series)) {
        Debug_Fmt("Trying to Free_Series() on an already freed series");
        Panic_Series(series);
    }

    // We can only free a series that is not under management by the
    // garbage collector
    //
    if (GET_SER_FLAG(series, SERIES_FLAG_MANAGED)) {
        Debug_Fmt("Trying to Free_Series() on a series managed by GC.");
        Panic_Series(series);
    }
#endif

    // Note: Code repeated in Manage_Series()
    //
    assert(GC_Manuals->content.dynamic.len >= 1);
    if (*last_ptr != series) {
        //
        // If the series is not the last manually added series, then
        // find where it is, then move the last manually added series
        // to that position to preserve it when we chop off the tail
        // (instead of keeping the series we want to free).
        //
        REBSER **current_ptr = last_ptr - 1;
        while (*current_ptr != series) {
            assert(
                current_ptr > cast(REBSER**, GC_Manuals->content.dynamic.data
            ));
            --current_ptr;
        }
        *current_ptr = *last_ptr;
    }

    // !!! Should GC_Manuals ever shrink or save memory?
    //
    GC_Manuals->content.dynamic.len--;

    // With bookkeeping done, use the same routine the GC uses to free
    //
    GC_Kill_Series(series);
}


//
//  Widen_String: C
// 
// Widen string from 1 byte to 2 bytes.
// 
// NOTE: allocates new memory. Cached pointers are invalid.
//
void Widen_String(REBSER *series, REBOOL preserve)
{
    REBINT bias_old = SER_BIAS(series);
    REBINT size_old = Series_Allocation_Unpooled(series);
    REBCNT len_old = series->content.dynamic.len;
    REBYTE wide_old = SER_WIDE(series);

    REBYTE *data_old = series->content.dynamic.data;

    REBYTE *bp;
    REBUNI *up;
    REBCNT n;

#if !defined(NDEBUG)
    // We may be resizing a partially constructed series, or otherwise
    // not want to preserve the previous contents
    if (preserve)
        ASSERT_SERIES(series);
#endif

    assert(SER_WIDE(series) == 1);

    series->content.dynamic.data = NULL;

    if (!Series_Data_Alloc(
        series, len_old + 1, cast(REBYTE, sizeof(REBUNI)), MKS_NONE
    )) {
        // Put series back how it was (there may be extant references)
        series->content.dynamic.data = data_old;
        fail (Error_No_Memory((len_old + 1) * sizeof(REBUNI)));
    }

    bp = data_old;
    up = UNI_HEAD(series);

    if (preserve) {
        for (n = 0; n <= len_old; n++) up[n] = bp[n]; // includes terminator
        series->content.dynamic.len = len_old;
    }
    else {
        series->content.dynamic.len = 0;
        TERM_SEQUENCE(series);
    }

    Free_Unbiased_Series_Data(data_old - (wide_old * bias_old), size_old);

    ASSERT_SERIES(series);
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
void Manage_Series(REBSER *series)
{
    REBSER ** const last_ptr
        = &cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.len - 1
        ];

#if !defined(NDEBUG)
    if (GET_SER_FLAG(series, SERIES_FLAG_MANAGED)) {
        Debug_Fmt("Attempt to manage already managed series");
        Panic_Series(series);
    }
#endif

    SET_SER_FLAG(series, SERIES_FLAG_MANAGED);

    // Note: Code repeated in Free_Series()
    //
    assert(GC_Manuals->content.dynamic.len >= 1);
    if (*last_ptr != series) {
        //
        // If the series is not the last manually added series, then
        // find where it is, then move the last manually added series
        // to that position to preserve it when we chop off the tail
        // (instead of keeping the series we want to free).
        //
        REBSER **current_ptr = last_ptr - 1;
        while (*current_ptr != series) {
            assert(
                current_ptr > cast(REBSER**, GC_Manuals->content.dynamic.data)
            );
            --current_ptr;
        }
        *current_ptr = *last_ptr;
    }

    // !!! Should GC_Manuals ever shrink or save memory?
    //
    GC_Manuals->content.dynamic.len--;
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
REBOOL Is_Value_Managed(const REBVAL *value, REBOOL thrown_or_end_ok)
{
#if !defined(NDEBUG)
    //
    // !thrown_or_end_ok might as well be the "called from GC setting", and it
    // might need to be reframed that way.  Because the GC is willing to
    // consider safe trash to be managed but can't tolerate unsafe trash.
    //
    if (!thrown_or_end_ok) {
        if (IS_TRASH_DEBUG(value)) {
            assert(GET_VAL_FLAG(value, TRASH_FLAG_SAFE));
            return TRUE;
        }
    }
#endif

    if (IS_END(value))
        return thrown_or_end_ok;

    if (THROWN(value))
        return thrown_or_end_ok;

    if (ANY_CONTEXT(value)) {
        REBCTX *context = VAL_CONTEXT(value);
        if (GET_ARR_FLAG(CTX_VARLIST(context), SERIES_FLAG_MANAGED)) {
            ASSERT_ARRAY_MANAGED(CTX_KEYLIST(context));
            return TRUE;
        }
        assert(!GET_ARR_FLAG(CTX_KEYLIST(context), SERIES_FLAG_MANAGED));
        return FALSE;
    }

    if (ANY_SERIES(value))
        return GET_SER_FLAG(VAL_SERIES(value), SERIES_FLAG_MANAGED);

    return TRUE;
}


//
//  Free_Gob: C
// 
// Free a gob, returning its memory for reuse.
//
void Free_Gob(REBGOB *gob)
{
    FREE_GOB(gob);

    Free_Node(GOB_POOL, (REBNOD *)gob);

    if (REB_I32_ADD_OF(GC_Ballast, Mem_Pools[GOB_POOL].wide, &GC_Ballast)) {
        GC_Ballast = MAX_I32;
    }

    if (GC_Ballast > 0) CLR_SIGNAL(SIG_RECYCLE);
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
    REBCNT pool_num;
    REBNOD *node;
    REBCNT count = 0;
    REBSEG *seg;
    REBSER *series;

#if !defined(NDEBUG)
    //Debug_Str("<ChkMem>");
    PG_Reb_Stats->Free_List_Checked++;
#endif

    // Scan all series headers to check that series->size is correct:
    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        series = (REBSER *) (seg + 1);
        for (count = Mem_Pools[SER_POOL].units; count > 0; count--) {
            if (!SER_FREED(series)) {
                if (!SER_REST(series) || !series->content.dynamic.data)
                    goto crash;
                // Does the size match a known pool?
                pool_num = FIND_POOL(SER_TOTAL(series));
                // Just to be sure the pool matches the allocation:
                if (pool_num < SER_POOL && Mem_Pools[pool_num].wide != SER_TOTAL(series))
                    goto crash;
            }
            series++;
        }
    }

    // Scan each memory pool:
    for (pool_num = 0; pool_num < SYSTEM_POOL; pool_num++) {
        count = 0;
        // Check each free node in the memory pool:
        for (node = cast(void **, Mem_Pools[pool_num].first); node; node = cast(void**, *node)) {
            count++;
            // The node better belong to one of the pool's segments:
            for (seg = Mem_Pools[pool_num].segs; seg; seg = seg->next) {
                if ((REBUPT)node > (REBUPT)seg && (REBUPT)node < (REBUPT)seg + (REBUPT)seg->size) break;
            }
            if (!seg) goto crash;
        }
        // The number of free nodes must agree with header:
        if (
            (Mem_Pools[pool_num].free != count) ||
            (Mem_Pools[pool_num].free == 0 && Mem_Pools[pool_num].first != 0)
        )
            goto crash;
    }

    return count;
crash:
    panic (Error(RE_CORRUPT_MEMORY));
}


//
//  Dump_All: C
// 
// Dump all series of a given size.
//
void Dump_All(REBCNT size)
{
    REBSEG  *seg;
    REBSER *series;
    REBCNT count;
    REBCNT n = 0;

    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        series = (REBSER *) (seg + 1);
        for (count = Mem_Pools[SER_POOL].units; count > 0; count--) {
            if (!SER_FREED(series)) {
                if (SER_WIDE(series) == size) {
                    //Debug_Fmt("%3d %4d %4d = \"%s\"", n++, series->tail, SER_TOTAL(series), series->data);
                    Debug_Fmt(
                        "%3d %4d %4d = \"%s\"",
                        n++,
                        series->content.dynamic.len,
                        SER_REST(series),
                        "-" // !label
                    );
                }
            }
            series++;
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
    REBSEG  *seg;
    REBSER *series;
    REBCNT count;
    REBCNT n = 0;

    for (seg = Mem_Pools[SER_POOL].segs; seg; seg = seg->next) {
        series = (REBSER *) (seg + 1);
        for (count = Mem_Pools[SER_POOL].units; count > 0; count--) {
            if (!SER_FREED(series)) {
                if (
                    pool_id == UNKNOWN
                    || FIND_POOL(SER_TOTAL(series)) == pool_id
                ) {
                    Debug_Fmt(
                              Str_Dump, //"%s Series %x %s: Wide: %2d Size: %6d - Bias: %d Tail: %d Rest: %d Flags: %x"
                              "Dump",
                              series,
                              "-", // !label
                              SER_WIDE(series),
                              SER_TOTAL(series),
                              SER_BIAS(series),
                              SER_LEN(series),
                              SER_REST(series),
                              series->info.bits // flags + width
                             );
                    //Dump_Series(series, "Dump");
                    if (Is_Array_Series(series)) {
                        Debug_Values(
                            ARR_HEAD(AS_ARRAY(series)),
                            SER_LEN(series),
                            1024 // !!! "FIXME limit
                        );
                    }
                    else {
                        Dump_Bytes(
                            series->content.dynamic.data,
                            (SER_LEN(series) + 1) * SER_WIDE(series)
                        );
                    }
                }
            }
            series++;
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
