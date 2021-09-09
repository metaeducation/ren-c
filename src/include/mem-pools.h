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
// system.  However, Alloc_Node() takes a pool ID, so things that want to make
// nodes need to know about SER_POOL.  And in order to take advantage of
// inlining, the system has to put a lot of things in header files.  Not
// being able to do so leads to a lot of pushing and popping overhead for
// parameters to commonly called routines (e.g. Alloc_Node())
//
// Hence if there are rules on which file is supposed to be calling which,
// those should be implemented in %source-analysis.r.
//


// Linked list of used memory segments
//
typedef struct rebol_mem_segment {
    struct rebol_mem_segment *next;
    uintptr_t size;
} REBSEG;


// Specifies initial pool sizes
//
typedef struct rebol_mem_spec {
    REBLEN wide;  // size of allocation unit
    REBLEN num_units;  // units per segment allocation
} REBPOOLSPEC;


// Pools manage fixed sized blocks of memory
//
struct rebol_mem_pool {
    REBSEG *segs;  // first memory segment
    REBPLU *first;  // first free item in pool
    REBPLU *last;  // last free item in pool
    REBLEN wide;  // size of allocation unit
    REBLEN num_units;  // units per segment allocation
    REBLEN free;  // number of units remaining
    REBLEN has;  // total number of units
};

#define DEF_POOL(size, count) {size, count}
#define MOD_POOL(size, count) {size * MEM_MIN_SIZE, count}

#define MEM_MIN_SIZE sizeof(REBVAL)
#define MEM_BIG_SIZE 1024

#define MEM_BALLAST 3000000

enum Mem_Pool_Specs {
    MEM_TINY_POOL = 0,
    MEM_SMALL_POOLS = MEM_TINY_POOL + 16,
    MEM_MID_POOLS = MEM_SMALL_POOLS + 4,
    MEM_BIG_POOLS = MEM_MID_POOLS + 4, // larger pools
    SER_POOL = MEM_BIG_POOLS,
  #if UNUSUAL_REBVAL_SIZE
    PAR_POOL,
  #else
    PAR_POOL = SER_POOL,
  #endif
    FRM_POOL,
    FED_POOL,
    SYSTEM_POOL,
    MAX_POOLS
};


//=//// MEMORY POOL UNIT //////////////////////////////////////////////////=//
//
// When enumerating over the units in a memory pool, it's important to know
// how that unit was initialized in order to validly read its data.  If the
// unit was initialized through a REBSER pointer, then you don't want to
// dereference it as if it had been initialized through a REBVAL.
//
// Similarly, you need to know when you are looking at it through the lens
// of a "freed pool unit" (which then means you can read the data linking it
// to the next free unit).
//
// Using byte-level access on the first byte to detect the initialization
// breaks the Catch-22, since access through `char*` and `unsigned char*` are
// not subject to "strict aliasing" rules.
//

struct Reb_Pool_Unit {
    //
    // This is not called "header" for a reason: you should *NOT* read the
    // bits of this header-sized slot to try and interpret bits that were
    // assigned through a REBSER or a REBVAL.  *You have to read out the
    // bits using the same type that initialized it.*  So only the first
    // byte here should be consulted...accessed through an `unsigned char*`
    // in order to defeat strict aliasing.  See NODE_BYTE()
    //
    // The first byte should *only* be read through a char*!
    //
    union Reb_Header headspot;  // leftmost byte is FREED_SERIES_BYTE if free

    struct Reb_Pool_Unit *next_if_free;  // if not free, full item available

    // Size of a node must be a multiple of 64-bits.  This is because there
    // must be a baseline guarantee for node allocations to be able to know
    // where 64-bit alignment boundaries are.
    //
    /* REBI64 payload[N];*/
};
