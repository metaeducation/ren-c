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
// REBYTE unsigned 8-bit value
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Using unsigned characters is good for conveying information is not limited
// to textual data.  It provides type-checking that helps discern between
// single-codepoint null terminated data (on which you might legitimately
// use `strlen()`, for instance) and something like UTF-8 data.
//
// Note: uint8_t might seem like an equally appropriate choice for REBYTE, if
// not better.  But in 99% of cases they will be the same.  In the 1% where
// they might be different, the entire difference would likely be that
// uint8_t would not offer the strict aliasing exemption of char types:
//
// https://stackoverflow.com/a/16138470/211160
//
// Hence it's better to use unsigned char, so the compiler doesn't assume that
// a REBYTE isn't pointing to the same address as another type, leading to
// cache problems (e.g. if you try to read the first byte of an integer)
//
typedef unsigned char REBYTE;


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

typedef uintptr_t REBFLGS; // platform-pointer-size unsigned for bit flags
typedef uintptr_t REBLIN; // type used to store line numbers in Rebol files
typedef uintptr_t REBTCK; // type the debug build uses for evaluator "ticks"

// !!! Review this choice from R3-Alpha:
//
// https://stackoverflow.com/q/1153548/
//
#define MIN_D64 ((double)-9.2233720368547758e18)
#define MAX_D64 ((double) 9.2233720368547758e18)


//=////////////////////////////////////////////////////////////////////////=//
//
// "WIDE" CHARACTER DEFINITION (UCS2)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Consensus about the wchar_t datatype is generally that it's a pre-Unicode
// abstraction that should be avoided unless you absolutely need it.  It
// varies in size by platform...though it is standardized to 2 bytes in size
// on Windows, where there is `#define WCHAR wchar_t`
//
// Some APIs (such as unixodbc) use UCS2 for wide character handling in order
// to be compatible with Windows, vs. using the native wchar_t type.  It thus
// defines SQLWCHAR as an unsigned short integer (itself not *guaranteed* to
// be 16-bits in size).  However, such a definition cannot be used if
// compiling as C++ and be compatible with Windows's #define:
//
// https://stackoverflow.com/q/1238609
//
// The primary focus of Ren-C is on UTF-8, but it does grudgingly provide
// some UCS2 APIs.  To avoid duplicating a u16-based "UCS2" API and a wchar_t
// API, the API is exposed as being REBWCHAR based, which does a #define
// based on the platform.
//
// *** However, don't use REBWCHAR in client code.  If the client code is
// on Windows, use WCHAR.  If it's in a unixodbc client use SQLWCHAR.  In
// general, try and use UTF8 if you possibly can. ***
//

#ifdef TO_WINDOWS
    #define REBWCHAR wchar_t
#else
    #define REBWCHAR uint16_t
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// UNICODE CODEPOINT
//
//=////////////////////////////////////////////////////////////////////////=//
//
// REBUNI is currently a two-byte representation of a Unicode codepoint.  It
// is not UCS2...it's simply limited to 16-bit codepoints.  R3-Alpha did not
// have CHAR! values higher than that.
//
// Ren-C is being adapted to where this will become a full 32-bit value.  The
// goal is to retrofit the code to use "UTF-8 Everywhere".  In the meantime,
// REBUNI is used internally to store Rebol ANY-STRING!s.  When all references
// to it have been changed to use the REBCHR(*) interface for safe variable
// sized encoding enumeration, a switch can be flipped and it can be upgraded.
//

typedef REBWCHAR REBUNI;

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
#if !defined(REB_DEF)
    //
    // The %reb-xxx.h files define structures visible to host code (client)
    // which don't also require pulling in all of the %sys-xxx.h files and
    // dependencies.  Some of these definitions are shared with the core,
    // and mention things like REBVAL.  When building as core that's fine,
    // but when building as host this will be undefined unless something
    // is there.  Define as a void so that it can point at it, but not know
    // anything else about it (including size).
    //
    typedef void REBVAL;
#else
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

    struct Reb_Action;  // function parameters + ACTION! value
    typedef struct Reb_Action REBACT;

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
#endif


// "Dangerous Function" which is called by rebRescue().  Argument can be a
// REBVAL* but does not have to be.  Result must be a REBVAL* or NULL.
//
// !!! If the dangerous function returns an ERROR!, it will currently be
// converted to void, in a behavior which parallels TRAP without a handler.
// voids will also be converted to BLANK!s.
//
typedef REBVAL* (REBDNG)(void *opaque);

// "Rescue Function" which is called as the handler in rebRescueWith().  It
// receives the REBVAL* of the error that occurred, and the opaque pointer.
//
// !!! If either the dangerous function or the rescuing function return an
// ERROR! value, that is not interfered with the way rebRescue() does.
//
typedef REBVAL* (REBRSC)(REBVAL *error, void *opaque);


//
// These flags are used by file to local conversion, and that is exposed from
// the core in %a-lib.c
//
enum {
    REB_FILETOLOCAL_0 = 0, // make it clearer when using no options
    REB_FILETOLOCAL_FULL = 1 << 0, // expand path relative to current dir
    REB_FILETOLOCAL_WILD = 1 << 1, // add on a `*` for wildcard listing

    // !!! A comment in the R3-Alpha %p-dir.c said "Special policy: Win32 does
    // not want tail slash for dir info".
    //
    REB_FILETOLOCAL_NO_TAIL_SLASH = 1 << 2 // don't include the terminal slash
};


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
