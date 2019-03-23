//
//  File: %reb-defs.h
//  Summary: "Miscellaneous structures and definitions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// These are the forward declarations of datatypes used by %tmp-internals.h
// (the internal Rebol API).  They must be at least mentioned before that file
// will be able to compile, after which the structures are defined in order.
//
// This shouldn't depend on other include files before it (besides %reb-c.h)
//


//=//// REBYTE 8-BIT UNSIGNED /////////////////////////////////////////////=//
//
// Using unsigned characters helps convey information is not limited to
// textual data.  API-wise, ordinary `char`--marked neither signed nor
// unsigned--is used for UTF-8 text.  But internally REBYTE is used for UTF-8
// when encoding or decoding.
//
// Note: uint8_t may not be equivalent to unsigned char:
// https://stackoverflow.com/a/16138470/211160
//
typedef unsigned char REBYTE; // don't change to uint8_t, see note


// Defines `enum Reb_Kind`, which is the enumeration of low-level cell types
// in Rebol (e.g. REB_BLOCK, REB_TEXT, etc.)
//
// The ordering encodes properties of the types for efficiency, so adding or
// removing a type generally means shuffling their values.  They are generated
// from a table and the numbers should not be exported to clients.
//
#include "tmp-kinds.h"
#include "sys-ordered.h"  // shuffling types *must* consider these macros!


//=//// REBOL NUMERIC TYPES ("REBXXX") ////////////////////////////////////=//
//
// The 64-bit build modifications to R3-Alpha after its open sourcing changed
// *pointers* internal to data structures to be 64-bit.  But indexes did not
// get changed to 64-bit: REBINT and REBCNT remained 32-bit.
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
typedef uint_fast32_t REBCNT; // series length, unsigned, at *least* 32 bits
typedef size_t REBSIZ; // 32 bit (size in bytes)
typedef int64_t REBI64; // 64 bit integer
typedef uint64_t REBU64; // 64 bit unsigned integer
typedef float REBD32; // 32 bit decimal
typedef double REBDEC; // 64 bit decimal
typedef uint_fast32_t REBFLGS; // unsigned used for working with bit flags
typedef uintptr_t REBLIN; // type used to store line numbers in Rebol files
typedef uintptr_t REBTCK; // type the debug build uses for evaluator "ticks"


// These were used in R3-Alpha.  Could use some better typing in C++ to avoid
// mistaking untested errors for ordinary integers.
//
#define NOT_FOUND ((REBCNT)-1)
#define UNKNOWN ((REBCNT)-1)


// !!! Review this choice from R3-Alpha:
//
// https://stackoverflow.com/q/1153548/
//
#define MIN_D64 ((double)-9.2233720368547758e18)
#define MAX_D64 ((double) 9.2233720368547758e18)


//=//// UNICODE CODEPOINT /////////////////////////////////////////////////=//
//
// We use the <stdint.h> fast 32 bit unsigned for REBUNI, as it doesn't need
// to be a standardized size (not persisted in files, etc.)

typedef uint_fast32_t REBUNI;


//=//// MEMORY POOLS //////////////////////////////////////////////////////=//
//
typedef struct rebol_mem_pool REBPOL;
typedef struct Reb_Node REBNOD;


//=//// RELATIVE VALUES ///////////////////////////////////////////////////=//
//
// Note that in the C build, %rebol.h forward-declares `struct Reb_Value` and
// then #defines REBVAL to that.
//
#if !defined(CPLUSPLUS_11)
    #define RELVAL \
        struct Reb_Value // same as REBVAL, no checking in C build
#else
    struct Reb_Relative_Value; // won't implicitly downcast to REBVAL
    #define RELVAL \
        struct Reb_Relative_Value // *might* be IS_RELATIVE()
#endif


//=//// ESCAPE-ALIASABLE CELLS ////////////////////////////////////////////=//
//
// The system uses a trick in which the type byte is bumped by multiples of
// 64 to indicate up to 3 levels of escaping.  VAL_TYPE() will report these
// as being REB_QUOTED, but the entire payload for them is in the cell.
//
// Most of the time, routines want to see these as being QUOTED!.  But some
// lower-level routines (like molding or comparison) want to be able to act
// on them in-place witout making a copy.  To ensure they see the value for
// the "type that it is" and use CELL_KIND() and not VAL_TYPE(), this alias
// for RELVAL prevents VAL_TYPE() operations.
//
#if !defined(CPLUSPLUS_11)
    #define REBCEL \
        struct Reb_Value // same as RELVAL, no checking in C build
#else
    struct Reb_Cell; // won't implicitly downcast to RELVAL
    #define REBCEL \
        struct Reb_Cell // *might* have KIND_BYTE() > REB_64
#endif



//=//// SERIES SUBCLASSES /////////////////////////////////////////////////=//
//
// Note that because the Reb_Series structure includes a Reb_Value by value,
// the %sys-rebser.h must be included *after* %sys-rebval.h; however the
// higher level definitions in %sys-series.h are *before* %sys-value.h.
//

struct Reb_Series;
typedef struct Reb_Series REBSER;

typedef REBSER REBBIN;  // generic binary series, e.g. for BINARY! (byte-size)

struct Reb_String;
typedef struct Reb_String REBSTR;  // see %sys-string.h

struct Reb_Array;
typedef struct Reb_Array REBARR;  // array of value cells

struct Reb_Context;
typedef struct Reb_Context REBCTX;

struct Reb_Action;
typedef struct Reb_Action REBACT;

struct Reb_Map;
typedef struct Reb_Map REBMAP;

typedef REBARR REBBMK;  // "bookmark" (list of UTF-8 index=>offset singulars)


//=//// BINDING ///////////////////////////////////////////////////////////=//

struct Reb_Node;
typedef struct Reb_Node REBSPC;

struct Reb_Binder;
struct Reb_Collector;


//=//// FRAMES ////////////////////////////////////////////////////////////=//
//
// Paths formerly used their own specialized structure to track the path,
// (path-value-state), but now they're just another kind of frame.  It is
// helpful for the moment to give them a different name.
//

struct Reb_Frame;

typedef struct Reb_Frame REBFRM;
typedef struct Reb_Frame REBPVS;

struct Reb_State;

//=//// DATA STACK ////////////////////////////////////////////////////////=//
//
typedef uint_fast32_t REBDSP; // Note: 0 for empty stack ([0] entry is trash)


// The REB_R type is a REBVAL* but with the idea that it is legal to hold
// types like REB_R_THROWN, etc.  This helps document interface contract.
//
typedef REBVAL *REB_R;


//=//// TYPE HOOKS ///////////////////////////////////////////////////////=//



// PER-TYPE COMPARE HOOKS, to support GREATER?, EQUAL?, LESSER?...
//
// Every datatype should have a comparison function, because otherwise a
// block containing an instance of that type cannot SORT.  Like the
// generic dispatchers, compare hooks are done on a per-class basis, with
// no overrides for individual types (only if they are the only type in
// their class).
//
typedef REBINT (COMPARE_HOOK)(const REBCEL *a, const REBCEL *b, REBINT s);


// PER-TYPE MAKE HOOKS: for `make datatype def`
//
// These functions must return a REBVAL* to the type they are making
// (either in the output cell given or an API cell)...or they can return
// R_THROWN if they throw.  (e.g. `make object! [return]` can throw)
//
typedef REB_R (MAKE_HOOK)(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *def
);


// PER-TYPE TO HOOKS: for `to datatype value`
//
// These functions must return a REBVAL* to the type they are making
// (either in the output cell or an API cell).  They are NOT allowed to
// throw, and are not supposed to make use of any binding information in
// blocks they are passed...so no evaluations should be performed.
//
// !!! Note: It is believed in the future that MAKE would be constructor
// like and decided by the destination type, while TO would be "cast"-like
// and decided by the source type.  For now, the destination decides both,
// which means TO-ness and MAKE-ness are a bit too similar.
//
typedef REB_R (TO_HOOK)(REBVAL*, enum Reb_Kind, const REBVAL*);


//=//// MOLDING ///////////////////////////////////////////////////////////=//
//
struct rebol_mold;
typedef struct rebol_mold REB_MOLD;


// PER-TYPE MOLD HOOKS: for `mold value` and `form value`
//
// Note: ERROR! may be a context, but it has its own special FORM-ing
// beyond the class (falls through to ANY-CONTEXT! for mold), and BINARY!
// has a different handler than strings.  So not all molds are driven by
// their class entirely.
//
typedef void (MOLD_HOOK)(REB_MOLD *mo, const REBCEL *v, bool form);


//=//// PARAMETER ENUMERATION /////////////////////////////////////////////=//
//
// !!! Due to a current limitation of the prototype scanner, a function type
// can't be used directly in a function definition and have it be picked up
// for %tmp-internals.h, it has to be a typedef.
//
typedef bool (PARAM_HOOK)(REBVAL *v, bool sorted_pass, void *opaque);


// These definitions are needed in %sys-rebval.h, and can't be put in
// %sys-rebact.h because that depends on Reb_Array, which depends on
// Reb_Series, which depends on values... :-/

// C function implementing a native ACTION!
//
typedef REB_R (*REBNAT)(REBFRM *frame_);
#define REBNATIVE(n) \
    REB_R N_##n(REBFRM *frame_)

//
// PER-TYPE GENERIC HOOKS: e.g. for `append value x` or `select value y`
//
// This is using the term in the sense of "generic functions":
// https://en.wikipedia.org/wiki/Generic_function
//
// The current assumption (rightly or wrongly) is that the handler for
// a generic action (e.g. APPEND) doesn't need a special hook for a
// specific datatype, but that the class has a common function.  But note
// any behavior for a specific type can still be accomplished by testing
// the type passed into that common hook!
//
typedef REB_R (GENERIC_HOOK)(REBFRM *frame_, const REBVAL *verb);
#define REBTYPE(n) \
    REB_R T_##n(REBFRM *frame_, const REBVAL *verb)


// PER-TYPE PATH HOOKS: for `a/b`, `:a/b`, `a/b:`, `pick a b`, `poke a b`
//
typedef REB_R (PATH_HOOK)(
    REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval
);


// Port hook: for implementing generic ACTION!s on a PORT! class
//
typedef REB_R (PORT_HOOK)(REBFRM *frame_, REBVAL *port, const REBVAL *verb);


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


// REBCHR(*) is defined in %sys-scan.h, along with SCAN_STATE, and both are
// referenced by internal API functions.
//
// (Note: %sys-do.h needs to call into the scanner if Fetch_Next_In_Frame() is
// to be inlined at all--at its many time-critical callsites--so the scanner
// has to be in the internal API)
//
#include "sys-scan.h"


//=//// API OPCODES ///////////////////////////////////////////////////////=//
//
// The libRebol API can take REBVAL*, or UTF-8 strings of raw textual material
// to scan and bind, or it can take a REBARR* of an "API instruction".
//
// These opcodes must be visible to the REBSER definition, as they live in
// the `MISC()` section.
//

enum Reb_Api_Opcode {
    API_OPCODE_UNUSED  // !!! Not currently used, review
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
    REBCNT h;
    REBCNT m;
    REBCNT s;
    REBCNT n;
} REB_TIMEF;

#include "sys-deci.h"



// To help document places in the core that are complicit in the "extension
// hack", alias arrays being used for the FFI and GOB to another name.
//
typedef REBARR REBGOB;

typedef REBARR REBSTU;
typedef REBARR REBFLD;
