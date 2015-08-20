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
**		A point of Rebol's design was to remain small and solve its
**		problems without relying on a lot of abstraction.  Its
**		memory-management was thus focused on staying low-level...and
**		being able to do efficient and lightweight allocations of
**		two major elements: series and graphic objects (GOBs).
**
**		Both series and GOBs have a fixed-size component that can
**		be easily allocated from a memory pool.  This portion is
**		called the "Node" (or NOD) in both Rebol and Red terminology;
**		it is an item whose pointer is valid for the lifetime of
**		the object, regardless of resizing.  This is where header
**		information is stored, and pointers to these objects may
**		be saved in REBVAL values; such that they are kept alive
**		by the garbage collector.
**
**		The more complicated thing to do memory pooling of is the
**		variable-sized portion of a series (currently called the
**		"series data")...as series sizes can vary widely.  But a
**		trick Rebol has is that a series might be able to take
**		advantage of being given back an allocation larger than
**		requested.  They can use it as reserved space for growth.
**
**		(Typical models for implementation of things like C++'s
**		std::vector do not reach below new[] or delete[]...which
**		are generally implemented with malloc and free under
**		the hood.  Their buffered additional capacity is done
**		assuming the allocation they get is as big as they asked
**		for...no more and no less.)
**
**		While Rebol's memory pooling is a likely-useful tool even
**		with modern alternatives, there are also useful tools
**		like Valgrind and Address Sanitizer which can more easily
**		root out bugs if each allocation and free is done
**		separately through malloc and free.  Therefore there is
**		an option for always using malloc, which you can enable
**		by setting the environment variable R3_ALWAYS_MALLOC to 1.
**
***********************************************************************/

//-- Special Debugging Options:
//#define CHAFF					// Fill series data to crash old references
//#define HIT_END				// Panic_DEAD_END if block tail is past block terminator.
//#define WATCH_FREED			// Show # series freed each GC
//#define MEM_STRESS			// Special torture mode enabled
//#define INSPECT_SERIES

#include "sys-core.h"
#include "sys-int-funcs.h"

#ifdef HAVE_ASAN_INTERFACE_H
#include <sanitizer/asan_interface.h>
#else
#define ASAN_POISON_MEMORY_REGION(reg, mem_size)
#define ASAN_UNPOISON_MEMORY_REGION(reg, mem_size)
#endif

/***********************************************************************
**
*/	void *Alloc_Mem(size_t size)
/*
**		NOTE: Instead of Alloc_Mem, use the ALLOC and ALLOC_ARRAY
**		wrapper macros to ensure the memory block being freed matches
**		the appropriate size for the type.
**
*************************************************************************
**
**		Alloc_Mem is an interface for a basic memory allocator.
**		It is coupled with a Free_Mem function that clients must
**		call with the correct size of the memory block to be freed.
**		It is thus lower-level than malloc()... whose memory blocks
**		remember the size of the allocation so you don't need to
**		pass it into free().
**
**		One motivation behind using such an allocator in Rebol
**		is to allow it to keep knowledge of how much memory the
**		system is using.  This means it can decide when to trigger a
**		garbage collection, or raise an out-of-memory error before
**		the operating system would, e.g. via 'ulimit':
**
**			http://stackoverflow.com/questions/1229241/
**
**		Finer-grained allocations are done with memory pooling.  But
**		the blocks of memory used by the pools are still acquired
**		using ALLOC_ARRAY and FREE_ARRAY.
**
***********************************************************************/
{
	// Trap memory usage limit *before* the allocation is performed

	PG_Mem_Usage += size;
	if ((PG_Mem_Limit != 0) && (PG_Mem_Usage > PG_Mem_Limit))
		Check_Security(SYM_MEMORY, POL_EXEC, 0);

	// While conceptually a simpler interface than malloc(), the
	// current implementations on all C platforms just pass through to
	// malloc and free.  NOTE: use of calloc is temporary for the
	// pooling commit, as it covers up bugs.  Those are addressed in
	// a separate patch.

#ifdef NDEBUG
	return calloc(size, 1);
#else
	{
		// In debug builds we cache the size at the head of the
		// allocation so we can check it.

		void *ptr = calloc(size + sizeof(size_t), 1);
		*cast(size_t *, ptr) = size;
		return cast(char *, ptr) + sizeof(size_t);
	}
#endif
}


/***********************************************************************
**
*/	void Free_Mem(void *mem, size_t size)
/*
**		NOTE: Instead of Free_Mem, use the FREE and FREE_ARRAY
**		wrapper macros to ensure the memory block being freed matches
**		the appropriate size for the type.
**
***********************************************************************/
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

#define	BAD_MEM_PTR ((REBYTE *)0xBAD1BAD1)

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
**	MEMORY POOLS
**
**		Memory management operates off an array of pools, the first
**		group of which are fixed size (so require no compaction).
**
***********************************************************************/
const REBPOOLSPEC Mem_Pool_Spec[MAX_POOLS] =
{
	{8, 256},			// 0-8 Small string pool

	MOD_POOL( 1, 256),	// 9-16 (when REBVAL is 16)
	MOD_POOL( 2, 512),	// 17-32 - Small series (x 16)
	MOD_POOL( 3, 1024),	// 33-64
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
	MOD_POOL(16,  64),	// 257
	MOD_POOL(20,  32),	// 321 - Mid-size series (x 64)
	MOD_POOL(24,  16),	// 385
	MOD_POOL(28,  16),	// 449
	MOD_POOL(32,   8),	// 513

	DEF_POOL(MEM_BIG_SIZE,  16),	// 1K - Large series (x 1024)
	DEF_POOL(MEM_BIG_SIZE*2, 8),	// 2K
	DEF_POOL(MEM_BIG_SIZE*3, 4),	// 3K
	DEF_POOL(MEM_BIG_SIZE*4, 4),	// 4K

	DEF_POOL(sizeof(REBSER), 4096),	// Series headers
	DEF_POOL(sizeof(REBGOB), 128),	// Gobs
	DEF_POOL(sizeof(REBLHL), 32), // external libraries
	DEF_POOL(sizeof(REBRIN), 128), // external routines
	DEF_POOL(1, 1),	// Just used for tracking main memory
};


/***********************************************************************
**
*/	void Init_Pools(REBINT scale)
/*
**		Initialize memory pool array.
**
***********************************************************************/
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
	Mem_Pools = ALLOC_ARRAY(REBPOL, MAX_POOLS);
	for (n = 0; n < MAX_POOLS; n++) {
		Mem_Pools[n].wide = Mem_Pool_Spec[n].wide;
		Mem_Pools[n].units = (Mem_Pool_Spec[n].units * scale) / unscale;
		if (Mem_Pools[n].units < 2) Mem_Pools[n].units = 2;
	}

	// For pool lookup. Maps size to pool index. (See Find_Pool below)
	PG_Pool_Map = ALLOC_ARRAY(REBYTE, (4 * MEM_BIG_SIZE) + 4); // extra
	n = 9;  // sizes 0 - 8 are pool 0
	for (; n <= 16 * MEM_MIN_SIZE; n++) PG_Pool_Map[n] = MEM_TINY_POOL     + ((n-1) / MEM_MIN_SIZE);
	for (; n <= 32 * MEM_MIN_SIZE; n++) PG_Pool_Map[n] = MEM_SMALL_POOLS-4 + ((n-1) / (MEM_MIN_SIZE * 4));
	for (; n <=  4 * MEM_BIG_SIZE; n++) PG_Pool_Map[n] = MEM_MID_POOLS     + ((n-1) / MEM_BIG_SIZE);
}


#ifndef POOL_MAP
/***********************************************************************
**
*/	static REBCNT Find_Pool(REBCNT size)
/*
**		Given a size, tell us what pool it belongs to.
**
***********************************************************************/
{
	if (size <= 8) return 0;  // Note: 0 - 8 (and size change for proper modulus)
	size--;
	if (size < 16 * MEM_MIN_SIZE) return MEM_TINY_POOL   + (size / MEM_MIN_SIZE);
	if (size < 32 * MEM_MIN_SIZE) return MEM_SMALL_POOLS-4 + (size / (MEM_MIN_SIZE * 4));
	if (size <  4 * MEM_BIG_SIZE) return MEM_MID_POOLS   + (size / MEM_BIG_SIZE);
	return SYSTEM_POOL;
}


/***********************************************************************
**
*/	void Check_Pool_Map(void)
**
************************************************************************
{
	int n;

	for (n = 0; n <= 4 * MEM_BIG_SIZE + 1; n++)
		if (FIND_POOL(n) != Find_Pool(n))
			Debug_Fmt("%d: %d %d", n, FIND_POOL(n), Find_Pool(n));
}
*/
#endif


/***********************************************************************
**
*/	static void Fill_Pool(REBPOL *pool)
/*
**		Allocate memory for a pool.  The amount allocated will be
**		determined from the size and units specified when the
**		pool header was created.  The nodes of the pool are linked
**		to the free list.
**
***********************************************************************/
{
	REBSEG	*seg;
	REBNOD	*node;
	REBYTE	*next;
	REBCNT	units = pool->units;
	REBCNT	mem_size = pool->wide * units + sizeof(REBSEG);

	seg = cast(REBSEG *, ALLOC_ARRAY(char, mem_size));
	if (!seg) {
		assert(FALSE);
		Panic_Core(RP_NO_MEMORY, mem_size);
	}

	CLEAR(seg, mem_size);  // needed to clear series nodes
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
		ASAN_UNPOISON_MEMORY_REGION(node, pool->wide);
	}

	for (next = (REBYTE *)(seg + 1); units > 0; units--, next += pool->wide) {
		*node = (REBNOD) next;
		node  = cast(void**, *node);
	}

	*node = 0;
	if (pool->last != NULL) {
		ASAN_POISON_MEMORY_REGION(pool->last, pool->wide);
	}
	pool->last = node;
	ASAN_POISON_MEMORY_REGION(seg, mem_size);
}


/***********************************************************************
**
*/	void *Make_Node(REBCNT pool_id)
/*
**		Allocate a node from a pool.  The node will NOT be cleared.
**		If the pool has run out of nodes, it will be refilled.
**
***********************************************************************/
{
	REBNOD *node;
	REBPOL *pool;

	pool = &Mem_Pools[pool_id];
	if (!pool->first) Fill_Pool(pool);
	node = pool->first;

	ASAN_UNPOISON_MEMORY_REGION(node, pool->wide);

	pool->first = cast(void**, *node);
	if (node == pool->last) {
		pool->last = NULL;
	}
	pool->free--;
	return (void *)node;
}


/***********************************************************************
**
*/	void Free_Node(REBCNT pool_id, REBNOD *node)
/*
**		Free a node, returning it to its pool.
**
***********************************************************************/
{
	REBPOL *pool = &Mem_Pools[pool_id];

	if (pool->last == NULL) { //pool is empty
		Fill_Pool(pool); //insert an empty segment, such that this node won't be picked by next Make_Node to enlongate the poisonous time of this area to catch stale pointers
	}
	ASAN_UNPOISON_MEMORY_REGION(pool->last, pool->wide);
	*(pool->last) = node;
	ASAN_POISON_MEMORY_REGION(pool->last, pool->wide);
	pool->last = node;
	*node = NULL;

	ASAN_POISON_MEMORY_REGION(node, pool->wide);

	pool->free++;
}


/***********************************************************************
**
*/	static REBOOL Series_Data_Alloc(REBSER *series, REBYTE wide, REBCNT length, REBCNT flags)
/*
**		Allocates element array for an already allocated REBSER header
**		structure.  Resets the bias and tail to zero, and sets the new
**		width.  Flags like SER_PROTECT or SER_KEEP are left as they
**		were, and other fields in the series structure are untouched.
**
**		This routine can thus be used for an initial construction
**		or an operation like expansion.  Currently not exported
**		from this file.
**
***********************************************************************/
{
	REBCNT size; // size of allocation (possibly bigger than we need)

	REBCNT pool_num = FIND_POOL(length * wide);

	// Data should have not been allocated yet OR caller has extracted it
	// and nulled it to indicate taking responsibility for freeing it.
	assert(!series->data);

	if (pool_num < SYSTEM_POOL) {
		// ...there is a pool designated for allocations of this size range
		series->data = cast(REBYTE*, Make_Node(pool_num));

		// The pooled allocation might wind up being larger than we asked.
		// Don't waste the space...mark as capacity the series could use.
		size = Mem_Pools[pool_num].wide;
		assert(size >= length * wide);
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
		}
		else
			size = ALIGN(size, 2048);

		series->data = ALLOC_ARRAY(REBYTE, size);
		if (!series->data)
			return FALSE;

		Mem_Pools[SYSTEM_POOL].has += size;
		Mem_Pools[SYSTEM_POOL].free++;
	}

#ifdef CHAFF
	// REVIEW: Rely completely on address sanitizer "poisoning" instead?
	memset(series->data, 0xff, size);
#endif

	// Keep the series flags like SER_KEEP, but use new width and set bias 0.

	series->info = ((series->info >> 8) << 8) | wide;
	SERIES_SET_BIAS(series, 0);

	if (flags & MKS_BLOCK) {
		assert(wide == sizeof(REBVAL));
		SERIES_SET_FLAG(series, SER_BLOCK);
		assert(IS_BLOCK_SERIES(series));
	}
	else {
		SERIES_CLR_FLAG(series, SER_BLOCK);
		assert(!IS_BLOCK_SERIES(series));
	}

	// The allocation may have returned more than we requested, so we note
	// that in 'rest' so that the series can expand in and use the space.

	series->rest = size / wide; // wastes remainder if size % wide != 0 :-(

	// We set the tail of all series to zero initially, but currently do
	// leave series termination to callers.  (This is under review.)

	series->tail = 0;

	// See if allocation tripped our need to queue a garbage collection

	if ((GC_Ballast -= size) <= 0) SET_SIGNAL(SIG_RECYCLE);

	return TRUE;
}


/***********************************************************************
**
*/	REBSER *Make_Series(REBCNT length, REBCNT wide, REBCNT flags)
/*
**		Make a series of a given length and width (unit size).
**		Small series will be allocated from a REBOL pool.
**		Large series will be allocated from system memory.
**		A width of zero is not allowed.
**
***********************************************************************/
{
	REBSER *series;

	CHECK_C_STACK_OVERFLOW(&series);

	if ((cast(REBU64, length) * wide) > MAX_I32) Trap(RE_NO_MEMORY);

	PG_Reb_Stats->Series_Made++;
	PG_Reb_Stats->Series_Memory += length * wide;

//	if (GC_TRIGGER) Recycle();

	series = cast(REBSER*, Make_Node(SERIES_POOL));


	if ((GC_Ballast -= sizeof(REBSER)) <= 0) SET_SIGNAL(SIG_RECYCLE);

#ifndef NDEBUG
	// For debugging purposes, it's nice to be able to crash on some
	// kind of guard for tracking the call stack at the point of allocation
	// if we find some undesirable condition that we want a trace from
	series->guard = cast(REBINT*, malloc(sizeof(*series->guard)));
	free(series->guard);
#endif

	series->info = 0; // start with all flags clear...
	series->data = NULL;

	if (flags & MKS_EXTERNAL) {
		// External series will poke in their own data pointer after the
		// REBSER header allocation is done

		SERIES_SET_FLAG(series, SER_EXTERNAL);
	}
	else {
		// Allocate the actual data blob that holds the series elements

		if (!Series_Data_Alloc(series, wide, length, flags)) {
			Free_Node(SERIES_POOL, cast(REBNOD*, series));
			Trap(RE_NO_MEMORY);
		}
	}

	series->extra.size = 0;

	// Keep the last few series in the nursery, safe from GC:
	if (GC_Last_Infant >= MAX_SAFE_SERIES) GC_Last_Infant = 0;
	GC_Infants[GC_Last_Infant++] = series;

	CHECK_MEMORY(2);

	return series;
}


/***********************************************************************
**
*/	static void Free_Unbiased_Series_Data(REBYTE *unbiased, REBCNT size)
/*
**		Routines that are part of the core series implementation
**		call this, including Expand_Series.  It requires a low-level
**		awareness that the series data pointer cannot be freed
**		without subtracting out the "biasing" which skips the pointer
**		ahead to account for unused capacity at the head of the
**		allocation.  They also must know the total allocation size.
**
***********************************************************************/
{
	REBCNT pool_num = FIND_POOL(size);
	REBPOL *pool;

	if (GC_Stay_Dirty) {
		memset(unbiased, 0xbb, size);
		return;
	}

	// Verify that size matches pool size:
	if (pool_num < SERIES_POOL) {
		/* size < wide when "wide" is not a multiple of element size */
		assert(Mem_Pools[pool_num].wide >= size);
	}

	if (pool_num < SYSTEM_POOL) {
		REBNOD *node = cast(REBNOD*, unbiased);
		pool = &Mem_Pools[pool_num];
		*node = pool->first;
		pool->first = node;
		pool->free++;
	}
	else {
		FREE_ARRAY(REBYTE, size, unbiased);
		Mem_Pools[SYSTEM_POOL].has -= size;
		Mem_Pools[SYSTEM_POOL].free--;
	}

	CHECK_MEMORY(2);
}


/***********************************************************************
**
*/	void Expand_Series(REBSER *series, REBCNT index, REBCNT delta)
/*
**		Expand a series at a particular index point by the number
**		number of units specified by delta.
**
**			index - where space is expanded (but not cleared)
**			delta - number of UNITS to expand (keeping terminator)
**			tail  - will be updated
**
**			        |<---rest--->|
**			<-bias->|<-tail->|   |
**			+--------------------+
**			|       abcdefghi    |
**			+--------------------+
**			        |    |
**			        data index
**
**		If the series has enough space within it, then it will be used,
**		otherwise the series data will be reallocated.
**
**		When expanded at the head, if bias space is available, it will
**		be used (if it provides enough space).
**
**		!!! It seems the original intent of this routine was
**		to be used with a group of other routines that were "Noterm"
**		and do not terminate.  However, Expand_Series assumed that
**		the capacity of the original series was at least (tail + 1)
**		elements, and would include the terminator when "sliding"
**		the data in the update.  This makes the other Noterm routines
**		seem a bit high cost for their benefit.  If this were to be
**		changed to Expand_Series_Noterm it would put more burden
**		on the clients...for a *potential* benefit in being able to
**		write just a REB_END byte into the terminal REBVAL vs. copying
**		the entire value cell.  (Of course, with a good memcpy it
**		might be an irrelevant difference.)  For the moment we reverse
**		the burden by enforcing the assumption that the incoming series
**		was already terminated.  That way our "slide" of the data via
**		memcpy will keep it terminated.
**
**		WARNING: never use direct pointers into the series data, as the
**		series data can be relocated in memory.
**
***********************************************************************/
{
	REBYTE wide = SERIES_WIDE(series);
	REBOOL any_block = IS_BLOCK_SERIES(series);

	REBCNT start;
	REBCNT size;
	REBCNT extra;
	REBUPT n_found;
	REBUPT n_available;
	REBCNT x;
	REBYTE *data_old;
	REBCNT size_old;
	REBINT bias_old;
	REBINT tail_old;

	// ASSERT_SERIES_TERM(series);

	if (delta == 0) return;

	any_block = IS_BLOCK_SERIES(series);

	// Optimized case of head insertion:
	if (index == 0 && SERIES_BIAS(series) >= delta) {
		series->data -= wide * delta;
		SERIES_TAIL(series) += delta;
		SERIES_REST(series) += delta;
		SERIES_SUB_BIAS(series, delta);
		return;
	}

	// Range checks:
	if (delta & 0x80000000) Trap(RE_PAST_END); // 2GB max
	if (index > series->tail) index = series->tail; // clip

	// Width adjusted variables:
	start = index * wide;
	extra = delta * wide;
	size  = (series->tail + 1) * wide;

	if ((size + extra) <= SERIES_SPACE(series)) {
		// No expansion was needed. Slide data down if necessary.
		// Note that the tail is always moved here. This is probably faster
		// than doing the computation to determine if it needs to be done.

		memmove(series->data + start + extra, series->data + start, size - start);
		series->tail += delta;

		if ((SERIES_TAIL(series) + SERIES_BIAS(series)) * wide >= SERIES_TOTAL(series)) {
			Dump_Series(series, "Overflow");
			assert(FALSE);
			Panic(RP_MISC); // shouldn't be possible, but code here panic'd
		}

		return;
	}

	// We need to expand the current series allocation.

	if (SERIES_GET_FLAG(series, SER_LOCK)) Panic(RP_LOCKED_SERIES);

#ifndef NDEBUG
	if (Reb_Opts->watch_expand) {
		Debug_Fmt(
			"Expand %x wide: %d tail: %d delta: %d",
			series, wide, series->tail, delta
		);
	}
#endif

	// Create a new series that is bigger.
	// Have we recently expanded the same series?
	x = 1;
	n_available = 0;
	for (n_found = 0; n_found < MAX_EXPAND_LIST; n_found++) {
		if (Prior_Expand[n_found] == series) {
			x = series->tail + delta + 1; // Double the size
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

	data_old = series->data;
	bias_old = SERIES_BIAS(series);
	size_old = SERIES_REST(series) * wide;
	tail_old = SERIES_TAIL(series);

	series->data = NULL;
	if (!Series_Data_Alloc(
		series,
		wide,
		series->tail + delta + x,
		any_block ? (MKS_BLOCK | MKS_POWER_OF_2) : MKS_POWER_OF_2
	)) {
		Trap(RE_NO_MEMORY);
		DEAD_END_VOID;
	}

	assert(SERIES_BIAS(series) == 0); // should be reset

	// If necessary, add series to the recently expanded list:
	if (n_found >= MAX_EXPAND_LIST)
		Prior_Expand[n_available] = series;

	// Copy the series up to the expansion point:
	memcpy(series->data, data_old, start);

	// Copy the series after the expansion point:
	// In AT_TAIL cases, this just moves the terminator to the new tail.
	memcpy(series->data + start + extra, data_old + start, size - start);
	series->tail = tail_old + delta;

	// We have to de-bias the data pointer before we can free it.
	Free_Unbiased_Series_Data(data_old - (wide * bias_old), size_old);

	PG_Reb_Stats->Series_Expanded++;
}


/***********************************************************************
**
*/	void Shrink_Series(REBSER *series, REBCNT units, REBOOL keep)
/*
**		Shrink a series back to a given maximum size. All
**		content is deleted and tail is reset.
**
**      A comment here used to say:
**
**		"WARNING: This should only be used for strings or other
**		series that cannot contain internally referenced values."
**
**      What does that mean?  What series "contains internally
**      referenced values?"  It's as if it's suggesting you
**      can't take a value out of a block when you obviously can.
**
***********************************************************************/
{
	REBYTE *data_old;
	REBINT bias_old;
	REBINT size_old;
	REBCNT tail_old;
	REBYTE wide;
	REBOOL any_block = IS_BLOCK_SERIES(series);

	if (SERIES_REST(series) <= units) return;

	bias_old = SERIES_BIAS(series);
	size_old = SERIES_REST(series) * SERIES_WIDE(series);
	data_old = series->data;
	tail_old = series->tail;
	wide = SERIES_WIDE(series);

	series->data = NULL;
	if (!Series_Data_Alloc(
		series, wide, units + 1, any_block ? MKS_BLOCK : MKS_NONE
	)) {
		Trap(RE_NO_MEMORY);
		DEAD_END_VOID;
	}

	if (keep) {
		assert(tail_old <= units);
		memcpy(series->data, data_old, wide * tail_old);
		series->tail = tail_old;
	} else
		series->tail = 0;

	TERM_SERIES(series);

	Free_Unbiased_Series_Data(data_old - (wide * bias_old), size_old);
}


/***********************************************************************
**
*/	void Free_Series(REBSER *series)
/*
**		Free a series, returning its memory for reuse.
**
**		!!! As it's obviously not easy to realize when it is safe
**		to call Free_Series, especially if you're not the garbage
**		collector, see:
**
**			https://github.com/metaeducation/ren-c/issues/2
**
***********************************************************************/
{
	REBCNT n;
	REBCNT size = SERIES_TOTAL(series);

	// !!! Original comment on freeing series data said: "Protect flag can
	// be used to prevent GC away from the data field".  ???
	REBOOL protect = TRUE;

	assert(!SERIES_FREED(series));

	PG_Reb_Stats->Series_Freed++;

	// Remove series from expansion list, if found:
	for (n = 1; n < MAX_EXPAND_LIST; n++) {
		if (Prior_Expand[n] == series) Prior_Expand[n] = 0;
	}

	if (SERIES_GET_FLAG(series, SER_EXTERNAL)) {
		// External series have their REBSER GC'd when Rebol doesn't need it,
		// but the data pointer itself is not one that Rebol allocated
		// !!! Should the external owner be told about the GC/free event?
	}
	else {
		series->data -= SERIES_WIDE(series) * SERIES_BIAS(series);
		Free_Unbiased_Series_Data(series->data, size);
	}

	series->info = 0; // includes width
	//series->data = BAD_MEM_PTR;
	//series->tail = 0xBAD2BAD2;
	//series->extra.size = 0xBAD3BAD3;

	/* remove from GC_Infants */
	for (n = 0; n < MAX_SAFE_SERIES; n++) {
		if (GC_Infants[n] == series)
			GC_Infants[n] = NULL;
	}

	Free_Node(SERIES_POOL, cast(REBNOD*, series));

	if (REB_I32_ADD_OF(GC_Ballast, size, &GC_Ballast)) {
		GC_Ballast = MAX_I32;
	}

	// GC may no longer be necessary:
	if (GC_Ballast > 0) CLR_SIGNAL(SIG_RECYCLE);
}


/***********************************************************************
**
*/	void Free_Gob(REBGOB *gob)
/*
**		Free a gob, returning its memory for reuse.
**
***********************************************************************/
{
	FREE_GOB(gob);

	Free_Node(GOB_POOL, (REBNOD *)gob);

	if (REB_I32_ADD_OF(GC_Ballast, Mem_Pools[GOB_POOL].wide, &GC_Ballast)) {
		GC_Ballast = MAX_I32;
	}

	if (GC_Ballast > 0) CLR_SIGNAL(SIG_RECYCLE);
}


/***********************************************************************
**
*/	REBFLG Series_In_Pool(REBSER *series)
/*
**		Confirm that the series value is in the series pool.
**
***********************************************************************/
{
	REBSEG	*seg;
	REBSER *start;

	// Scan all series headers to check that series->size is correct:
	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {
		start = (REBSER *) (seg + 1);
		if (series >= start && series <= (REBSER*)((REBYTE*)start + seg->size - sizeof(REBSER)))
			return TRUE;
	}

	return FALSE;
}


/***********************************************************************
**
*/	REBCNT Check_Memory(void)
/*
**		FOR DEBUGGING ONLY:
**		Traverse the free lists of all pools -- just to prove we can.
**		This is useful for finding corruption from bad memory writes,
**		because a write past the end of a node will destory the pointer
**		for the next free area.
**
***********************************************************************/
{
	REBCNT pool_num;
	REBNOD *node;
	REBCNT count = 0;
	REBSEG *seg;
	REBSER *series;

	//Debug_Str("<ChkMem>");
	PG_Reb_Stats->Free_List_Checked++;

	// Scan all series headers to check that series->size is correct:
	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {
		series = (REBSER *) (seg + 1);
		for (count = Mem_Pools[SERIES_POOL].units; count > 0; count--) {
			if (!SERIES_FREED(series)) {
				if (!SERIES_REST(series) || !series->data)
					goto crash;
				// Does the size match a known pool?
				pool_num = FIND_POOL(SERIES_TOTAL(series));
				// Just to be sure the pool matches the allocation:
				if (pool_num < SERIES_POOL && Mem_Pools[pool_num].wide != SERIES_TOTAL(series))
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
	Panic_DEAD_END(RP_CORRUPT_MEMORY);
	return 0; // for compiler only
}


/***********************************************************************
**
*/	void Dump_All(REBINT size)
/*
**		Dump all series of a given size.
**
***********************************************************************/
{
	REBSEG	*seg;
	REBSER *series;
	REBCNT count;
	REBCNT n = 0;

	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {
		series = (REBSER *) (seg + 1);
		for (count = Mem_Pools[SERIES_POOL].units; count > 0; count--) {
			if (!SERIES_FREED(series)) {
				if (SERIES_WIDE(series) == size && SERIES_GET_FLAG(series, SER_MON)) {
					//Debug_Fmt("%3d %4d %4d = \"%s\"", n++, series->tail, SERIES_TOTAL(series), series->data);
					Debug_Fmt("%3d %4d %4d = \"%s\"", n++, series->tail, SERIES_REST(series), (SERIES_LABEL(series) ? SERIES_LABEL(series) : "-"));
				}
			}
			series++;
		}
	}
}

/***********************************************************************
**
*/	void Dump_Series_In_Pool(REBCNT pool_id)
/*
**		Dump all series in pool @pool_id, UNKNOWN (-1) for all pools
**
***********************************************************************/
{
	REBSEG	*seg;
	REBSER *series;
	REBCNT count;
	REBCNT n = 0;

	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {
		series = (REBSER *) (seg + 1);
		for (count = Mem_Pools[SERIES_POOL].units; count > 0; count--) {
			if (!SERIES_FREED(series)) {
				if (
					pool_id == UNKNOWN
					|| FIND_POOL(SERIES_TOTAL(series)) == pool_id
				) {
					Debug_Fmt(
							  Str_Dump, //"%s Series %x %s: Wide: %2d Size: %6d - Bias: %d Tail: %d Rest: %d Flags: %x"
							  "Dump",
							  series,
							  (SERIES_LABEL(series) ? SERIES_LABEL(series) : "-"),
							  SERIES_WIDE(series),
							  SERIES_TOTAL(series),
							  SERIES_BIAS(series),
							  SERIES_TAIL(series),
							  SERIES_REST(series),
							  SERIES_FLAGS(series)
							 );
					//Dump_Series(series, "Dump");
					if (IS_BLOCK_SERIES(series)) {
						Debug_Values(BLK_HEAD(series), SERIES_TAIL(series), 1024); /* FIXME limit */
					} else{
						Dump_Bytes(series->data, (SERIES_TAIL(series)+1) * SERIES_WIDE(series));
					}
				}
			}
			series++;
		}
	}
}


/***********************************************************************
**
*/	static void Dump_Pools(void)
/*
**		Print statistics about all memory pools.
**
***********************************************************************/
{
	REBSEG	*seg;
	REBCNT	segs;
	REBCNT	size;
	REBCNT  used;
	REBCNT	total = 0;
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


/***********************************************************************
**
*/	REBU64 Inspect_Series(REBCNT flags)
/*
***********************************************************************/
{
	REBSEG	*seg;
	REBSER	*series;
	REBCNT  segs, n, tot, blks, strs, unis, nons, odds, fre;
	REBCNT  str_size, uni_size, blk_size, odd_size, seg_size, fre_size;
	REBFLG  f = 0;
	REBINT  pool_num;
#ifdef SERIES_LABELS
	REBYTE  *kind;
#endif
	REBU64  tot_size;

	segs = tot = blks = strs = unis = nons = odds = fre = 0;
	seg_size = str_size = uni_size = blk_size = odd_size = fre_size = 0;
	tot_size = 0;

	for (seg = Mem_Pools[SERIES_POOL].segs; seg; seg = seg->next) {

		seg_size += seg->size;
		segs++;

		series = (REBSER *) (seg + 1);

		for (n = Mem_Pools[SERIES_POOL].units; n > 0; n--) {

			if (SERIES_WIDE(series)) {
				tot++;
				tot_size += SERIES_TOTAL(series);
				f = 0;
			} else {
				fre++;
			}

#ifdef SERIES_LABELS
			kind = "----";
			if (SERIES_GET_FLAG(series, SER_KEEP)) kind = "KEEP";
			//if (Find_Root(series)) kind = "ROOT";
			if (!SERIES_FREED(series) && series->label) {
				Debug_Fmt_("%08x: %16s %s ", series, series->label, kind);
				f = 1;
			} else if (!SERIES_FREED(series) && (flags & 0x100)) {
				Debug_Fmt_("%08x: %s ", series, kind);
				f = 1;
			}
#endif
			if (IS_BLOCK_SERIES(series)) {
				blks++;
				blk_size += SERIES_TOTAL(series);
				if (f) Debug_Fmt_("BLOCK ");
			}
			else if (SERIES_WIDE(series) == 1) {
				strs++;
				str_size += SERIES_TOTAL(series);
				if (f) Debug_Fmt_("STRING");
			}
			else if (SERIES_WIDE(series) == sizeof(REBUNI)) {
				unis++;
				uni_size += SERIES_TOTAL(series);
				if (f) Debug_Fmt_("UNICOD");
			}
			else if (SERIES_WIDE(series)) {
				odds++;
				odd_size += SERIES_TOTAL(series);
				if (f) Debug_Fmt_("ODD[%d]", SERIES_WIDE(series));
			}
			if (f && SERIES_WIDE(series)) {
				Debug_Fmt(" units: %-5d tail: %-5d bytes: %-7d", SERIES_REST(series), SERIES_TAIL(series), SERIES_TOTAL(series));
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

