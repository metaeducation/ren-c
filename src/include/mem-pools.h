//
//  File: %mem-pools.h
//  Summary: "Memory allocation"
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
// In R3-Alpha, the memory pool details were not exported to most of the
// system.  However, Alloc_Pooled() takes a pool ID, so things that want to
// make nodes need to know about STUB_POOL.  And in order to take advantage of
// inlining, the system has to put a lot of things in header files.  Not
// being able to do so leads to a lot of pushing and popping overhead for
// parameters to commonly called routines (e.g. Alloc_Pooled())
//
// Hence if there are rules on which file is supposed to be calling which,
// those should be implemented in %source-analysis.r.
//


// Linked list of used memory segments
//
typedef struct SegmentStruct {
    struct SegmentStruct* next;
    uintptr_t size;
} Segment;


// Specifies initial pool sizes
//
typedef struct PoolSpecStruct {
    REBLEN wide;  // size of allocation unit
    REBLEN num_units_per_segment;  // units per segment allocation
} PoolSpec;


//=//// MEMORY POOL UNIT //////////////////////////////////////////////////=//
//
// When enumerating over the units in a memory pool, it's important to know
// how that unit was initialized in order to validly read its data.  If the
// unit was initialized through a Series pointer, then you don't want to
// dereference it as if it had been initialized through a Cell.
//
// Similarly, you need to know when you are looking at it through the lens
// of a "freed pool unit" (which then means you can read the data linking it
// to the next free unit).
//
// Using byte-level access on the first byte to detect the initialization
// breaks the Catch-22, since access through `char*` and `unsigned char*` are
// not subject to "strict aliasing" rules.
//

typedef struct PoolUnitStruct {
    //
    // This is not called "header" for a reason: you should *NOT* read the
    // bits of this header-sized slot to try and interpret bits that were
    // assigned through a Series or a Cell.  *You have to read out the
    // bits using the same type that initialized it.*  So only the first
    // byte here should be consulted...accessed through an `unsigned char*`
    // in order to defeat strict aliasing.  See NODE_BYTE()
    //
    // The first byte should *only* be read through a char*!
    //
    union HeaderUnion headspot;  // leftmost byte is FREED_SERIES_BYTE if free

    struct PoolUnitStruct* next_if_free;  // if not free, full item available

    // Size of a node must be a multiple of 64-bits.  This is because there
    // must be a baseline guarantee for node allocations to be able to know
    // where 64-bit alignment boundaries are.
    //
    /* REBI64 payload[N];*/
} PoolUnit;


// Pools manage fixed sized blocks of memory
//
typedef struct PoolStruct {
    Segment* segments;  // first memory segment
    PoolUnit* first;  // first free item in pool
    PoolUnit* last;  // last free item in pool
    Size wide;  // size of allocation unit
    Length num_units_per_segment;  // units per segment allocation
    Count free;  // number of units remaining
    Count has;  // total number of units
} Pool;

#define DEF_POOL(size, count) {size, count}
#define MOD_POOL(size, count) {size * MEM_MIN_SIZE, count}

#define MEM_MIN_SIZE sizeof(CellT)
#define MEM_BIG_SIZE 1024

#define POOLS_BY_SIZE_LEN ((4 * MEM_BIG_SIZE) + 1)

// The ballast is how much memory the garbage collector will allow to be used
// up before it decides to trigger a GC.  This is the default value it is
// primed to, and it keeps track of the remaining amount in `m_gc.depletion`.
//
// !!! Choosing this amount dynamically based on the system is probably
// wiser, but there's a lot of work the naive mark-and-sweep GC needs.
//
#define MEM_BALLAST 3000000

typedef signed int PoolId;  // used with UNLIMITED (-1)

enum PoolSpecEnum {
    MEM_TINY_POOL = 0,
    MEM_SMALL_POOLS = MEM_TINY_POOL + 16,
    MEM_MID_POOLS = MEM_SMALL_POOLS + 4,
    MEM_BIG_POOLS = MEM_MID_POOLS + 4,  // larger pools
    STUB_POOL = MEM_BIG_POOLS,
  #if UNUSUAL_CELL_SIZE
    PAIR_POOL,
  #else
    PAIR_POOL = STUB_POOL,
  #endif
    LEVEL_POOL,
    FEED_POOL,
    SYSTEM_POOL,
    MAX_POOLS
};
