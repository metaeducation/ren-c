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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These are the forward declarations of datatypes used by %tmp-internals.h
// (the internal Rebol API).  They must be at least mentioned before that file
// will be able to compile, after which the structures are defined in order.
//
// This shouldn't depend on other include files before it (besides %reb-c.h)
//


// Defines `enum Reb_Kind`, which is the enumeration of low-level cell types
// in Rebol (e.g. REB_BLOCK, REB_TEXT, etc.)
//
// The ordering encodes properties of the types for efficiency, so adding or
// removing a type generally means shuffling their values.  They are generated
// from a table and the numbers should not be exported to clients.
//
#include "tmp-kinds.h"


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
typedef uintptr_t REBFLGS; // platform-pointer-size unsigned for bit flags
typedef uintptr_t REBLIN; // type used to store line numbers in Rebol files
typedef uintptr_t REBTCK; // type the debug build uses for evaluator "ticks"

// !!! Review this choice from R3-Alpha:
//
// https://stackoverflow.com/q/1153548/
//
#define MIN_D64 ((double)-9.2233720368547758e18)
#define MAX_D64 ((double) 9.2233720368547758e18)


//=//// UNICODE CODEPOINT /////////////////////////////////////////////////=//
//
// REBUNI is currently a two-byte representation of a Unicode codepoint.  It
// is not UTF-16...it's simply limited to 16-bit codepoints (UCS-2).  R3-Alpha
// did not have CHAR! values higher than that.
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


//=//// MEMORY POOLS //////////////////////////////////////////////////////=//
//
typedef struct rebol_mem_pool REBPOL;
typedef struct Reb_Node REBNOD;


//=//// RELATIVE VALUES ///////////////////////////////////////////////////=//
//
// Note that in the C build, %rebol.h forward-declares `struct Reb_Value` and
// then #defines Value to that.
//
#if !defined(CPLUSPLUS_11)
    #define Cell RebolValue  // same as Value, no checking in C build
#else
    struct Reb_Relative_Value; // won't implicitly downcast to Value
    #define Cell \
        struct Reb_Relative_Value // *might* be IS_RELATIVE()
#endif


//=//// SERIES SUBCLASSES /////////////////////////////////////////////////=//
//
// Note that because the Reb_Series structure includes a Reb_Value by value,
// the %sys-rebser.h must be included *after* %sys-rebval.h; however the
// higher level definitions in %sys-series.h are *before* %sys-value.h.
//

struct Reb_Series;
typedef struct Reb_Series REBSER;

typedef REBSER REBSTR;

struct Reb_Array;
typedef struct Reb_Array REBARR;

struct Reb_Context;
typedef struct Reb_Context REBCTX;

struct Reb_Action;
typedef struct Reb_Action REBACT;

struct Reb_Map;
typedef struct Reb_Map REBMAP;


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


// The REB_R type is a Value* but with the idea that it is legal to hold
// types like REB_R_THROWN, etc.  This helps document interface contract.
//
typedef Value* REB_R;


//=//// DISPATCHERS ///////////////////////////////////////////////////////=//
//
typedef REBINT (*COMPARE_HOOK)(const Cell* a, const Cell* b, REBINT s);
typedef REB_R (*MAKE_HOOK)(Value*, enum Reb_Kind, const Value*);
typedef REB_R (*TO_HOOK)(Value*, enum Reb_Kind, const Value*);


//=//// MOLDING ///////////////////////////////////////////////////////////=//
//
struct rebol_mold;
typedef struct rebol_mold REB_MOLD;
typedef void (*MOLD_HOOK)(REB_MOLD *mo, const Cell* v, bool form);


// These definitions are needed in %sys-rebval.h, and can't be put in
// %sys-rebact.h because that depends on Reb_Array, which depends on
// Reb_Series, which depends on values... :-/

// C function implementing a native ACTION!
//
typedef REB_R (*REBNAT)(REBFRM *frame_);
#define REBNATIVE(n) \
    REB_R N_##n(REBFRM *frame_)

// Generic hooks: implementing a "verb" ACTION! for a particular
// type (or class of types).
//
typedef REB_R (*GENERIC_HOOK)(REBFRM *frame_, Value* verb);
#define REBTYPE(n) \
    REB_R T_##n(REBFRM *frame_, Value* verb)

// Port hook: for implementing generic ACTION!s on a PORT! class
//
typedef REB_R (*PORT_HOOK)(REBFRM *frame_, Value* port, Value* verb);

// Path evaluator function
//
typedef REB_R (*PATH_HOOK)(
    REBPVS *pvs, const Value* picker, const Value* opt_setval
);


//=//// VARIADIC OPERATIONS ///////////////////////////////////////////////=//
//
// These 3 operations are the current legal set of what can be done with a
// VARARG!.  They integrate with Eval_Core_Throws()'s limitations in the
// prefetch evaluator--such as to having one unit of lookahead.
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


//=//// DEVICE REQUEST ////////////////////////////////////////////////////=//
//
// !!! The device and port model is being reviewed/excised.  However, for the
// moment REBREQ appears in the prototypes of functions in the internal API.
//
struct rebol_devreq;
typedef struct rebol_devreq REBREQ;
struct devreq_file;

//=//// CELL PAYLOAD CONTENTS /////////////////////////////////////////////=//
//
// Some internal APIs pass around the extraction of value payloads, like take
// a REBDAT*, when they could probably just as well pass around a Cell*.
// The usages are few and far enough between.  But for the moment just define
// things here.
//

// !!! This structure varies the layout based on endianness, so that when it
// is seen throuh the .bits field of the REBDAT union, a later date will
// have a value that will be greater (>) than an earlier date.  This should
// be reviewed for standards compliance; masking and shifting is generally
// safer than bit field union tricks.
//
typedef struct reb_ymdz {
#ifdef ENDIAN_LITTLE
    int zone:7; // +/-15:00 res: 0:15
    unsigned day:5;
    unsigned month:4;
    unsigned year:16;
#else
    unsigned year:16;
    unsigned month:4;
    unsigned day:5;
    int zone:7; // +/-15:00 res: 0:15
#endif
} REBYMD;

typedef union reb_date {
    REBYMD date;
    REBCNT bits; // !!! alias used for hashing date, is this standards-legal?
} REBDAT;

typedef struct rebol_time_fields {
    REBCNT h;
    REBCNT m;
    REBCNT s;
    REBCNT n;
} REB_TIMEF;

#include "sys-deci.h"
