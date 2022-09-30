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
// This shouldn't depend on other include files before it (besides %reb-c.h)
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
// Hence Ren-C switches to using indexes that are provided by <stdint.h> (or
// the stub "pstdint.h") that are deemed by the compiler to be the fastest
// representation for 32-bit integers...even if that might be larger.
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
typedef uintptr_t Tick; // type the debug build uses for evaluator "ticks"


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


//=//// MEMORY POOLS //////////////////////////////////////////////////////=//
//
typedef struct rebol_mem_pool Pool;

struct Reb_Pool_Unit;
typedef struct Reb_Pool_Unit PoolUnit;

typedef signed int PoolID;  // used with UNLIMITED (-1)


//=//// SERIES AND NON-INHERITED SUBCLASS DEFINITIONS /////////////////////=//
//
// The C++ build defines Raw_Array, Raw_Binary, and Raw_String as being
// derived from Raw_Series.  This affords convenience by having it possible
// to pass the derived class to something taking a base class, but not vice
// versa.  However, you cannot forward-declare inheritance:
//
// https://stackoverflow.com/q/2159390/
//
// Hence, those derived definitions have to be in %sys-rebser.h.
//
// Aggregate types that are logically collections of multiple series do not
// inherit.  You have to specify which series you want to extract, e.g.
// GET_SERIES_FLAG(CTX_VARLIST(context)), not just GET_SERIES_FLAG(context).
//
// Note that because the Raw_Series structure includes a Reb_Value by value,
// the %sys-rebser.h must be included *after* %sys-rebval.h; however the
// higher level definitions in %sys-series.h are *before* %sys-value.h.
//

struct Reb_Stub;

typedef struct Reb_Stub Stub;

typedef Stub Raw_Series;

typedef Raw_Series REBSER;


struct Reb_Bookmark {
    REBLEN index;
    Size offset;
};

//=//// BINDING ///////////////////////////////////////////////////////////=//

struct Reb_Binder;
struct Reb_Collector;


//=//// FRAMES ////////////////////////////////////////////////////////////=//
//
// Paths formerly used their own specialized structure to track the path,
// (path-value-state), but now they're just another kind of frame.  It is
// helpful for the moment to give them a different name.
//

struct Reb_Frame;

#define Frame(star) \
    struct Reb_Frame star

struct Reb_Feed_Struct;
typedef struct Reb_Feed_Struct Reb_Feed;

struct Reb_State;
struct Reb_Jump;


//=//// DATA STACK ////////////////////////////////////////////////////////=//
//
typedef uint_fast32_t StackIndex;  // 0 for empty stack ([0] entry is trash)


//=//// PARAMETER CLASSES ////////////////////////////////////////////////=//

enum Reb_Param_Class {
    PARAM_CLASS_0,  // used to indicate an "unset" state

    // `PARAM_CLASS_NORMAL` is cued by an ordinary WORD! in the function spec
    // to indicate that you would like that argument to be evaluated normally.
    //
    //     >> foo: function [a] [print [{a is} a]]
    //
    //     >> foo 1 + 2
    //     a is 3
    //
    PARAM_CLASS_NORMAL,

    PARAM_CLASS_RETURN,

    PARAM_CLASS_OUTPUT,

    // `PARAM_CLASS_HARD` is cued by a quoted WORD! in the function spec
    // dialect.  It indicates that a single value of content at the callsite
    // should be passed through *literally*, without any evaluation:
    //
    //     >> foo: function ['a] [print [{a is} a]]
    //
    //     >> foo (1 + 2)
    //     a is (1 + 2)
    //
    //     >> foo :(1 + 2)
    //     a is :(1 + 2)
    //
    //
    PARAM_CLASS_HARD,

    // `PARAM_CLASS_MEDIUM` is cued by a QUOTED GET-WORD! in the function spec
    // dialect.  It quotes with the exception of GET-GROUP!, GET-WORD!, and
    // GET-PATH!...which will be evaluated:
    //
    //     >> foo: function [':a] [print [{a is} a]
    //
    //     >> foo (1 + 2)
    //     a is (1 + 2)
    //
    //     >> foo :(1 + 2)
    //     a is 3
    //
    // Although possible to implement medium quoting with hard quoting, it is
    // a convenient way to allow callers to "escape" a quoted context when
    // they need to.
    //
    PARAM_CLASS_MEDIUM,

    // `PARAM_CLASS_SOFT` is cued by a PLAIN GET-WORD!.  It's a more nuanced
    // version of PARAM_CLASS_MEDIUM which is escapable but will defer to enfix.
    // This covers cases like:
    //
    //     if true [...] then :(func [...] [...])  ; want escapability
    //     if true [...] then x -> [...]  ; but want enfix -> lookback to win
    //
    // Hence it is the main mode of quoting for branches.  It would be
    // unsuitable for cases like OF, however, due to this problem:
    //
    //     integer! = type of 1  ; want left quoting semantics on `type` WORD!
    //     integer! = :(first [type length]) of 1  ; want escapability
    //
    // OF wants its left hand side to be escapable, however it wants the
    // quoting behavior to out-prioritize the completion of enfix on the
    // left.  Contrast this with how THEN wants the enfix on the right to
    // win out ahead of its quoting.
    //
    // This is a subtlety that most functions don't have to worry about, so
    // using soft quoting is favored to medium quoting for being one less
    // character to type.
    //
    PARAM_CLASS_SOFT,

    // `PARAM_CLASS_META` is the only parameter type that can accept isotope
    // forms of BAD-WORD!.  They become plain forms of BAD-WORD! when they
    // are an argument, and all other types receive one added quote level
    // (except for pure NULL, which is left as NULL).
    //
    //     >> foo: function [^a] [print [{a is} a]
    //
    //     >> foo 1 + 2
    //     a is '3
    //
    //     >> foo get/any 'asdfasfasdf
    //     a is ~
    //
    PARAM_CLASS_META
};


//=//// SYMBOL IDs ////////////////////////////////////////////////////////=//
//
// Built-in symbols get a hardcoded integer number that can be used in the
// C code--for instance in switch() statements.  However, any symbols which
// are not in the hardcoded table have a symbol ID of 0.
//
// We want to avoid bugs that can happen when you say things like:
//
//     if (VAL_WORD_ID(a) == VAL_WORD_ID(b)) { ... }
//
// So we wrap the enum into an option(), which the C++ build is able to do
// added type checking on.  It also prohibits comparisons unless you unwrap
// the values, which in debug builds has a runtime check of non-zeroness.
//

typedef enum Reb_Symbol_Id SymId;

#define SYM_0 \
    cast(option(SymId), cast(enum Reb_Symbol_Id, 0))

#if DEBUG_CHECK_OPTIONALS && CPLUSPLUS_11
    bool operator==(option(SymId)& a, option(SymId)& b) = delete;
    void operator!=(option(SymId)& a, option(SymId)& b) = delete;
#endif

typedef struct {
    option(SymId) sym;
    REBU64 bits;
} SymToBits;


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


//=//// REBVAL PAYLOAD CONTENTS ///////////////////////////////////////////=//
//
// Some internal APIs pass around the extraction of value payloads, like take
// a REBYMD* or REBGOB*, when they could probably just as well pass around a
// REBVAL*.  The usages are few and far enough between.  But for the moment
// just define things here.
//

typedef struct reb_ymdz {
    unsigned year:16;
    unsigned month:4;
    unsigned day:5;
    int zone:7; // +/-15:00 res: 0:15
} REBYMD;

typedef struct rebol_time_fields {
    REBLEN h;
    REBLEN m;
    REBLEN s;
    REBLEN n;
} REB_TIMEF;

#include "sys-deci.h"



//=//// R3-ALPHA DEVICE / DEVICE REQUEST //////////////////////////////////=//
//
// This may become part of the rebXXX API, if someone wants to just register
// something that wants an opportunity to get polled (?)
//
typedef bool (DEVICE_POLL_CFUNC)(void);

struct Reb_Device;
struct Reb_Device {
    const char *name;
    DEVICE_POLL_CFUNC *poll;

    struct Reb_Device *next;  // next in linked list of registered devices
};
#define REBDEV struct Reb_Device
