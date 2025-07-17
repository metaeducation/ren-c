//
//  file: %mem-pools.h
//  summary: "Memory allocation"
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
// In R3-Alpha, the memory pool details were not exported to most of the
// system.  However, Alloc_Pooled() takes a pool ID, so things that make
// Stubs need to know about STUB_POOL.  And in order to take advantage of
// inlining, the system has to put a lot of things in header files.  Not
// being able to do so leads to a lot of pushing and popping overhead for
// parameters to commonly called routines (e.g. Alloc_Pooled())
//
// Hence if there are rules on which file is supposed to be calling which,
// those should be implemented in %source-analysis.r.
//


//=//// ALIGNMENT SIZE ////////////////////////////////////////////////////=//
//
// Data alignment is a complex topic, which has to do with the fact that the
// following kind of assignment can be slowed down or fail entirely on
// many platforms:
//
//    char *cp = (char*)malloc(sizeof(double) + 1);
//    double *dp = (double*)(cp + 1);
//    *dp = 6.28318530718
//
// malloc() guarantees that the pointer it returns is aligned to store any
// fundamental type safely.  But skewing that pointer to not be aligned in
// a way for that type (e.g. by a byte above) means assignments and reads of
// types with more demanding alignment will fail.  e.g. a double often needs
// to read or write at pointers where `((uintptr_t)ptr % sizeof(double)) == 0`
//
// (Note: Often, not always.  For instance, Linux systems with System V ABI
// for i386 are permitted to use 4 byte boundaries instead of 8 byte for
// doubles unless you use `-malign-double`.  See page 28 of the spec:
//
// http://www.uclibc.org/docs/psABI-i386.pdf
//
// Windows 32-bit compilers seem to also permit 4 bytes.  WebAssembly does
// not seem to work when doubles are on 4 byte boundaries, however.)
//
// 1. The C standard has no way to know what the largest fundamental type is,
//    even though malloc() must be compatible with it.  :-(  So if one is
//    writing one's own allocator to give back memory blocks, it's necessary
//    to guess.  Guess the larger of sizeof(double) and sizeof(void*), though
//    this may not be enough for absolutely any type in the compiler:
//
//      "In Visual C++, the fundamental alignment is the alignment that's
//       required for a double, or 8 bytes. In code that targets 64-bit
//       platforms, it's 16 bytes.)
//
// 2. This macro is often called ALIGN() in various codebases, but if we
//    define it under that name we risk colliding with definitions on the
//    platform that may or may not be what we want.
//
// 3. Since it does compile-time calculations, aligning has to be a macro
//    in C.  Unlike some cases where "evil" parameter-repeating macros can
//    catch bad uses in C++ builds (see STATIC_ASSERT_LVALUE()), there's not
//    really anything that can be done with this: it has to work with compile
//    time constants, so there's no way to stop a function with side effects
//    working, if it produces an integer.  Naming just suggests: be careful.
//

#define ALIGN_SIZE /* not in the C or C++ standard [1] */ \
    (sizeof(double) > sizeof(void*) ? sizeof(double) : sizeof(void*))

#define Adjust_Size_For_Align_Evil_Macro(size,align)  /* aka ALIGN() [2] */ \
    (((size) + (align) - 1) & ~((align) - 1))  // repeats align, use care [3]


// Linked list of used memory segments
//
typedef struct rebol_mem_segment {
    struct rebol_mem_segment *next;
    uintptr_t size;
} REBSEG;


// Specifies initial pool sizes
//
typedef struct rebol_mem_spec {
    REBLEN wide; // size of allocation unit
    REBLEN units; // units per segment allocation
} REBPOOLSPEC;


// Pools manage fixed sized blocks of memory
//
struct rebol_mem_pool {
    REBSEG *segs; // first memory segment
    PoolUnit* first; // first free unit in pool
    PoolUnit* last; // last free unit in pool
    REBLEN wide; // size of allocation unit
    REBLEN units; // units per segment allocation
    REBLEN free; // number of units remaining
    REBLEN  has; // total number of units
};

#define DEF_POOL(size, count) {size, count}
#define MOD_POOL(size, count) {size * MEM_MIN_SIZE, count}

#define MEM_MIN_SIZE sizeof(Cell)
#define MEM_BIG_SIZE 1024

#define MEM_BALLAST 3000000

enum Mem_Pool_Specs {
    MEM_TINY_POOL = 0,
    MEM_SMALL_POOLS = MEM_TINY_POOL + 16,
    MEM_MID_POOLS = MEM_SMALL_POOLS + 4,
    MEM_BIG_POOLS = MEM_MID_POOLS + 4, // larger pools
    STUB_POOL = MEM_BIG_POOLS,
  #if UNUSUAL_CELL_SIZE
    PAR_POOL,
  #else
    PAR_POOL = STUB_POOL,
  #endif
    GOB_POOL,
    SYSTEM_POOL,
    MAX_POOLS
};
