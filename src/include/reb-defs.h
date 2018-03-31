//
//  File: %reb-defs.h
//  Summary: "Miscellaneous structures and definitions"
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
// This file is used by internal and external C code.  It should not depend
// on any other include files before it.
//
// If REB_DEF is defined, it expects full definitions of the structures behind
// REBVAL and REBSER.  If not, then it treats them opaquely.  The reason this
// is done in a single file with an #ifdef as opposed to just doing the
// opaque definitions in %reb-ext.h (and not including %reb-defs.h there) is
// because of %a-lib.c - which wants to use the non-opaque definitions to
// implement the API while still having the various enums in %reb-ext.h
// available to the compiler.
//

#ifndef REB_DEFS_H  // due to sequences within the lib build itself
#define REB_DEFS_H


//=////////////////////////////////////////////////////////////////////////=//
//
// REBOL NUMERIC TYPES ("REBXXX")
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The 64-bit build modifications to R3-Alpha after its open sourcing changed
// *pointers* internal to data structures to be 64-bit.  But indexes did not
// get changed to 64-bit: REBINT and REBCNT remained 32-bit.
//
// This meant there was often extra space in the structures used on 64-bit
// machines, and a possible loss of performance for forcing a platform to use
// a specific size int (instead of deferring to C's generic `int`).
//

typedef int32_t REBINT; // 32 bit signed integer
typedef uint32_t REBCNT; // 32 bit (counting number, length in "units")
typedef uint32_t REBSIZ; // 32 bit (size in bytes)
typedef int64_t REBI64; // 64 bit integer
typedef uint64_t REBU64; // 64 bit unsigned integer
typedef float REBD32; // 32 bit decimal
typedef double REBDEC; // 64 bit decimal

typedef intptr_t REBIPT; // integral counterpart of void*
typedef uintptr_t REBUPT; // unsigned counterpart of void*

typedef uintptr_t REBFLGS; // platform-pointer-size unsigned for bit flags

// Using unsigned characters is good for conveying information is not limited
// to textual data.  It provides type-checking that helps discern between
// single-codepoint null terminated data (on which you might legitimately
// use `strlen()`, for instance) and something like UTF-8 data.
//
typedef uint8_t REBYTE; // unsigned byte data

// !!! Review this choice from R3-Alpha:
//
// https://stackoverflow.com/q/1153548/
//
#define MIN_D64 ((double)-9.2233720368547758e18)
#define MAX_D64 ((double) 9.2233720368547758e18)


//=////////////////////////////////////////////////////////////////////////=//
//
// UNICODE CHARACTER TYPE
//
//=////////////////////////////////////////////////////////////////////////=//
//
// REBUNI is a two-byte UCS-2 representation of a Unicode codepoint.  Some
// routines once errantly conflated wchar_t with REBUNI, but a wchar_t is not
// 2 bytes on all platforms (it's 4 on GCC in 64-bit Linux, for instance).
// Routines for handling UCS-2 must be custom-coded or come from a library.
// (For example: you can't use wcslen() so Strlen_Uni() is implemented inside
// of Rebol.)
//
// Rebol is able to have its strings start out as UCS-1, with a single byte
// per character.  For that it uses REBYTEs.  But when you insert something
// requiring a higher codepoint, it goes to UCS-2 with REBUNI and will not go
// back (at time of writing).
//
// !!! BEWARE that several lower level routines don't do this widening, so be
// sure that you check which are which.
//
// Longer term, the growth of emoji usage in Internet communication has led
// to supporting higher "astral" codepoints as being a priority.  This means
// either being able to "double-widen" to UCS-4, as is Red's strategy:
//
// http://www.red-lang.org/2012/09/plan-for-unicode-support.html
//
// Or it could also mean shifting to "UTF-8 everywhere":
//
// http://utf8everywhere.org
//

typedef uint16_t REBUNI;

#define MAX_UNI \
    ((1 << (8 * sizeof(REBUNI))) - 1)


//=////////////////////////////////////////////////////////////////////////=//
//
// REBOL SERIES TYPES
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Forward declarations of the series subclasses defined in %sys-series.h
// Because the Reb_Series structure includes a Reb_Value by value, it
// must be included *after* %sys-value.h
//
#ifdef REB_DEF
    struct Reb_Cell;

    #if !defined(CPLUSPLUS_11)
        #define RELVAL struct Reb_Cell
        #define REBVAL struct Reb_Cell
        #define const_RELVAL_NO_END_PTR const struct Reb_Cell *
    #else
        struct Reb_Relative_Value;
        #define RELVAL struct Reb_Relative_Value // *maybe* IS_RELATIVE()

        struct Reb_Specific_Value;
        #define REBVAL struct Reb_Specific_Value // guaranteed IS_SPECIFIC()

        struct const_Reb_Relative_Value_No_End_Ptr;
        #define const_RELVAL_NO_END_PTR \
            struct const_Reb_Relative_Value_No_End_Ptr
    #endif

    struct Reb_Series; // Rebol series node
    typedef struct Reb_Series REBSER;

    // UTF-8 Everywhere series (used for WORD!s only ATM)
    typedef REBSER REBSTR;

    struct Reb_Array; // REBSER containing REBVALs ("Rebol Array")
    typedef struct Reb_Array REBARR;

    struct Reb_Context; // parallel REBARR key/var arrays + ANY-CONTEXT! value
    typedef struct Reb_Context REBCTX;

    struct Reb_Func;  // function parameters + FUNCTION! value
    typedef struct Reb_Func REBFUN;

    struct Reb_Map; // REBARR listing key/value pairs with hash
    typedef struct Reb_Map REBMAP;

    struct Reb_Frame; // Non-GC'd raw call frame, see %sys-frame.h
    typedef struct Reb_Frame REBFRM;

    struct Reb_Binder; // used as argument in %tmp-funcs.h, needs forward decl
    struct Reb_Collector; // same

    // Paths formerly used their own specialized structure to track the path,
    // (path-value-state), but now they're just another kind of frame.  It is
    // helpful for the moment to give them a different name.
    //
    typedef struct Reb_Frame REBPVS;

    // Compare Types Function
    //
    typedef REBINT (*REBCTF)(const RELVAL *a, const RELVAL *b, REBINT s);

    // A standard integer is currently used to represent the data stack
    // pointer.  `unsigned int` instead of a `REBCNT` in order to leverage the
    // native performance of the integer type unconstrained by bit size, as
    // data stack pointers are not stored in REBVALs or similar, and
    // performance in comparing and manipulation is more important than size.
    //
    // Note that a value of 0 indicates an empty stack; the [0] entry is made
    // to be alerting trash to trap invalid reads or writes of empty stacks.
    //
    typedef unsigned int REBDSP;
    struct Reb_Chunk;
    struct Reb_Chunker;

    struct Reb_Node;
    typedef struct Reb_Node REBNOD;

    typedef struct Reb_Node REBSPC;

    // This defines END as the address of a global node.  It's important to
    // point out that several definitions you might think would work for END
    // will not.  For example, this string literal seems to have the right
    // bits in the leading byte (NODE_FLAG_NODE and NODE_FLAG_END):
    //
    //     #define END ((const REBVAL*)"\x88")
    //
    // (Note: it's actually two bytes, C adds a terminator \x00)
    //
    // But the special "endlike" value of "the" END global node is set up to
    // assuming further that it has 0 in its rightmost bits, where the type is
    // stored.  Why would this be true when you cannot run a VAL_TYPE() on an
    // arbitrary end marker?
    //
    // (Note: the reason you can't run VAL_TYPE() on arbitrary cells that
    // return true to IS_END() is because some--like the above--only set
    // enough bits to say that they're ends and not cells, so they can use
    // subsequent bits for other purposes.  See Init_Endlike_Header())
    //
    // The reason there's a special loophole for this END is to help avoid
    // extra testing for NULL.  So in various internal code where NULL might
    // be used, this END is...which permits the operation VAL_TYPE_OR_0.
    //
    // So you might think that more zero bytes would help.  If you're on a
    // 64-bit platform, that means you'd need at least 7 bytes plus null
    // terminator:
    //
    //     #define END ((const REBVAL*)"\x88\x00\x00\x00\x00\x00\x00")
    //
    // ...but even that doesn't work for the core, since END is expected to
    // have a single memory address across translation units.  This means if
    // one C file assigns a variable to END, another C file can turn around
    // and test `value == END` instead of with `IS_END(value)` (though it's
    // not clear whether that actually benefits performance much or not.)
    //
    #define END \
        ((const REBVAL*)&PG_End_Node) // sizeof(REBVAL) but not NODE_FLAG_CELL
#else
    // The %reb-xxx.h files define structures visible to host code (client)
    // which don't also require pulling in all of the %sys-xxx.h files and
    // dependencies.  Some of these definitions are shared with the core,
    // and mention things like REBVAL.  When building as core that's fine,
    // but when building as host this will be undefined unless something
    // is there.  Define as a void so that it can point at it, but not know
    // anything else about it (including size).
    //
    // Note: R3-Alpha allowed stack instantiation of values as "RXIARG".  But
    // the Ren-C API version of REBVAL* needs to know where all the values are
    // so they can be GC managed, since external clients are not expected to
    // know how to do that.  Hence the values are opaque.
    //
    typedef void REBVAL;
    typedef void REBFRM;
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// MISCELLANY
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! This is stuff that needs a better home.

// Useful char constants:
enum {
    BEL =   7,
    BS  =   8,
    LF  =  10,
    CR  =  13,
    ESC =  27,
    DEL = 127
};

// Used for MOLDing:
#define MAX_DIGITS 17   // number of digits
#define MAX_NUMCHR 32   // space for digits and -.e+000%

#define MAX_INT_LEN     21
#define MAX_HEX_LEN     16

#ifdef ITOA64           // Integer to ascii conversion
    #define INT_TO_STR(n,s) _i64toa(n, s_cast(s), 10)
#else
    #define INT_TO_STR(n,s) Form_Int_Len(s, n, MAX_INT_LEN)
#endif

#ifdef ATOI64           // Ascii to integer conversion
#define CHR_TO_INT(s)   _atoi64(cs_cast(s))
#else
#define CHR_TO_INT(s)   strtoll(cs_cast(s), 0, 10)
#endif

#define LDIV            lldiv
#define LDIV_T          lldiv_t

// Skip to the specified byte but not past the provided end
// pointer of the byte string.  Return NULL if byte is not found.
//
inline static const REBYTE *Skip_To_Byte(
    const REBYTE *cp,
    const REBYTE *ep,
    REBYTE b
) {
    while (cp != ep && *cp != b) cp++;
    if (*cp == b) return cp;
    return 0;
}

typedef int cmp_t(void *, const void *, const void *);
extern void reb_qsort_r(void *a, size_t n, size_t es, void *thunk, cmp_t *cmp);

#define ROUND_TO_INT(d) \
    cast(int32_t, floor((MAX(INT32_MIN, MIN(INT32_MAX, d))) + 0.5))

#endif
