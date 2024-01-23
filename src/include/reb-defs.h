//
//  File: %reb-defs.h
//  Summary: "Miscellaneous structures and definitions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Ren-C Open Source Contributors
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
// These are the forward declarations of datatypes used by %tmp-internals.h
// (the internal Rebol API).  They must be at least mentioned before that file
// will be able to compile, after which the structures are defined in order.
//
// Shouldn't depend on other include files before it (besides %c-enhanced.h)
//


//=//// 8-BIT UNSIGNED BYTE ///////////////////////////////////////////////=//
//
// Using unsigned characters helps convey information is not limited to
// textual data.  API-wise, ordinary `char`--marked neither signed nor
// unsigned--is used for UTF-8 text.  But internally Byte is used for UTF-8
// when encoding or decoding.
//
// Note: uint8_t may not be equivalent to unsigned char:
// https://stackoverflow.com/a/16138470/211160
//
// This is a macro, to make it possible to undefine out of the way for things
// that define it (most likely to a synonym, as zlib.h does)
//
#define Byte unsigned char  // don't change to uint8_t, see note


//=//// REBOL NUMERIC TYPES ("REBXXX") ////////////////////////////////////=//
//
// The 64-bit build modifications to R3-Alpha after its open sourcing changed
// *pointers* internal to data structures to be 64-bit.  But indexes did not
// get changed to 64-bit: REBINT and REBLEN remained 32-bit.
//
// This meant there was often extra space in the structures used on 64-bit
// machines, and a possible loss of performance for forcing a platform to use
// a specific size int (instead of deferring to C's generic `int`).
//
// Hence Ren-C switches to using indexes that are provided by <stdint.h>
// that are deemed by the compiler to be the fastest representation for 32-bit
// integers...even if that might be larger.
//
typedef int_fast32_t REBINT; // series index, signed, at *least* 32 bits
typedef intptr_t REBIDX; // series index, signed, at *least* 32 bits
typedef uint_fast32_t REBLEN; // series length, unsigned, at *least* 32 bits

// !!! These values are an attempt to differentiate 0-based indexing from
// 1-based indexing, and try to be type-incompatible.
//
typedef int_fast32_t Index;
typedef uint_fast32_t Offset;
typedef uint_fast32_t Length;
typedef uint_fast32_t Count;

typedef size_t Size;  // Size (in bytes)

typedef int64_t REBI64; // 64 bit integer
typedef uint64_t REBU64; // 64 bit unsigned integer
typedef float REBD32; // 32 bit decimal
typedef double REBDEC; // 64 bit decimal
typedef uintptr_t LineNumber; // type used to store line numbers in Rebol files

typedef uint64_t Tick;  // evaluator cycles; unsigned overflow is well defined


// These were used in R3-Alpha, with the benefit that UNLIMITED will cast to
// a very large unsigned integer.  With sign checks turned up, it's possible to
// catch cases that are looking for unsigned integers but don't test for
// the -1 condition.
//
// https://github.com/LambdaSchool/CS-Wiki/wiki/Casting-Signed-to-Unsigned-in-C
//
#define NOT_FOUND (-1)
#define UNLIMITED (-1)


// !!! Review this choice from R3-Alpha:
//
// https://stackoverflow.com/q/1153548/
//
#define MIN_D64 ((double)-9.2233720368547758e18)
#define MAX_D64 ((double) 9.2233720368547758e18)


//=//// UNICODE CODEPOINT /////////////////////////////////////////////////=//
//
// We use the <stdint.h> fast 32 bit unsigned for Codepoint, as it doesn't need
// to be a standardized size (not persisted in files, etc.)

typedef uint_fast32_t Codepoint;


//=//// SERIES AND NON-INHERITED SUBCLASS DEFINITIONS /////////////////////=//
//
// The C++ build defines Array, Binary, and String as being
// derived from Series.  This affords convenience by having it possible
// to pass the derived class to something taking a base class, but not vice
// versa.  However, you cannot forward-declare inheritance:
//
// https://stackoverflow.com/q/2159390/
//
// Hence, those derived definitions have to be in %sys-rebser.h.
//
// Aggregate types that are logically collections of multiple series do not
// inherit.  You have to specify which series you want to extract, e.g.
// Get_Series_Flag(CTX_VARLIST(context)), not just Get_Series_Flag(context).
//
// Note that because the Series structure includes a Cell by value,
// the %sys-rebser.h must be included *after* %sys-rebval.h; however the
// higher level definitions in %sys-series.h are *before* %sys-value.h.
//

typedef struct BookmarkStruct {
    REBLEN index;
    Size offset;
} BookmarkT;

//=//// BINDING ///////////////////////////////////////////////////////////=//

struct Reb_Binder;
struct Reb_Collector;

typedef struct JumpStruct Jump;



//=//// DATA STACK ////////////////////////////////////////////////////////=//
//
typedef uint_fast32_t StackIndex;  // 0 for empty stack ([0] entry poison)


//=//// SYMBOL IDs ////////////////////////////////////////////////////////=//
//
// Built-in symbols get a hardcoded integer number that can be used in the
// C code--for instance in switch() statements.  However, any symbols which
// are not in the hardcoded table have a symbol ID of 0.
//
// We want to avoid bugs that can happen when you say things like:
//
//     if (Cell_Word_Id(a) == Cell_Word_Id(b)) { ... }
//
// If you were allowed to do that, then all non-built-ins would give back
// SYM_) and appear to be equal.  It's a tricky enough bug to catch to warrant
// an extra check in C++ that disallows comparing SYMIDs with ==
//
// So we wrap the enum into an Option(), which the C++ build is able to do
// added type checking on.  It also prohibits comparisons unless you unwrap
// the values, which in debug builds has a runtime check of non-zeroness.
//

typedef enum SymIdEnum SymId;

#define SYM_0 \
    cast(Option(SymId),  cast(SymId, 0))  // 0 cast needed if not -fpermissive

#if DEBUG_CHECK_OPTIONALS && CPLUSPLUS_11
    bool operator==(Option(SymId)& a, Option(SymId)& b) = delete;
    void operator!=(Option(SymId)& a, Option(SymId)& b) = delete;
#endif


//=//// STRING MODES //////////////////////////////////////////////////////=//
//
// Ren-C is prescriptive about disallowing 0 bytes in strings to more safely
// use the rebSpell() API, which only returns a pointer and must interoperate
// with C.  It enforces the use of BINARY! if you want to embed 0 bytes (and
// using the rebBytes() API, which always returns a size.)
//
// Additionally, it tries to build on Rebol's historical concept of unifying
// strings within the system to use LF-only.  But rather than try "magic" to
// filter out CR LF sequences (and "magically" put them back later), it adds
// in speedbumps to try and stop CR from casually getting into strings.  Then
// it encourages active involvement at the source level with functions like
// ENLINE and DELINE when a circumstance can't be solved by standardizing the
// data sources themselves:
//
// https://forum.rebol.info/t/1264
//
// Note: These policies may over time extend to adding more speedbumps for
// other invisibles, e.g. choosing prescriptivisim about tab vs. space also.
//

enum Reb_Strmode {
    STRMODE_ALL_CODEPOINTS,  // all codepoints allowed but 0
    STRMODE_NO_CR,  // carriage returns not legal
    STRMODE_CRLF_TO_LF,  // convert CR LF to LF (error on isolated CR or LF)
    STRMODE_LF_TO_CRLF  // convert plain LF to CR LF (error on stray CR)
};


//=//// MOLDING ///////////////////////////////////////////////////////////=//
//
struct rebol_mold;
typedef struct rebol_mold REB_MOLD;


//=//// VARIADIC OPERATIONS ///////////////////////////////////////////////=//
//
// These 3 operations are the current legal set of what can be done with a
// VARARG!.  They integrate with Eval_Core()'s limitations in the prefetch
// evaluator--such as to having one unit of lookahead.
//
// While it might seem natural for this to live in %sys-varargs.h, the enum
// type is used by a function prototype in %tmp-internals.h...hence it must be
// defined before that is included.
//
enum Reb_Vararg_Op {
    VARARG_OP_TAIL_Q, // tail?
    VARARG_OP_FIRST, // "lookahead"
    VARARG_OP_TAKE // doesn't modify underlying data stream--advances index
};


//=//// TYPE HOOK ACCESS //////////////////////////////////////////////////=//
//
// Built-in types identify themselves as one of ~64 fundamental "kinds".  This
// occupies a byte in the header (64 is chosen as a limit currently in order
// to be used with 64-bit typesets, but this is due for change).
//
// For efficiency, what's put in the extra is what would be like that type's
// row in the `Builtin_Type_Hooks` if it had been built-in.  These table
// rows are speculatively implemented as an untyped array of CFunction* which is
// null terminated (vs. a struct with typed fields) so that the protocol can
// be expanded without breaking strict aliasing.
//

enum Reb_Type_Hook_Index {
    IDX_GENERIC_HOOK,
    IDX_COMPARE_HOOK,
    IDX_MAKE_HOOK,
    IDX_TO_HOOK,
    IDX_MOLD_HOOK,
    IDX_HOOK_NULLPTR,  // see notes on why null termination convention
    IDX_HOOKS_MAX
};


typedef struct rebol_time_fields {
    REBLEN h;
    REBLEN m;
    REBLEN s;
    REBLEN n;
} REB_TIMEF;

#include "sys-deci.h"


enum Reb_Attach_Mode {
    ATTACH_READ,
    ATTACH_WRITE
};

enum act_modify_mask {
    AM_PART = 1 << 0,
    AM_SPLICE = 1 << 1,
    AM_LINE = 1 << 2
};

enum act_find_mask {
    AM_FIND_CASE = 1 << 1,
    AM_FIND_MATCH = 1 << 2
};

// Flags used for Protect functions
//
enum {
    PROT_SET = 1 << 0,
    PROT_DEEP = 1 << 1,
    PROT_HIDE = 1 << 2,
    PROT_WORD = 1 << 3,
    PROT_FREEZE = 1 << 4
};
