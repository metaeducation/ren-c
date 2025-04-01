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
// Shouldn't depend on other include files before it (besides %c-enhanced.h)
//


// Defines `enum Reb_Kind`, which is the enumeration of low-level cell types
// in Rebol (e.g. REB_BLOCK, REB_TEXT, etc.)
//
// The ordering encodes properties of the types for efficiency, so adding or
// removing a type generally means shuffling their values.  They are generated
// from a table and the numbers should not be exported to clients.
//
#include "tmp-kinds.h"


//=//// Byte 8-BIT UNSIGNED /////////////////////////////////////////////=//
//
// Using unsigned characters helps convey information is not limited to
// textual data.  API-wise, ordinary `char`--marked neither signed nor
// unsigned--is used for UTF-8 text.  But internally Byte is used for UTF-8
// when encoding or decoding.
//
// Note: uint8_t may not be equivalent to unsigned char:
// https://stackoverflow.com/a/16138470/211160
//
typedef unsigned char Byte; // don't change to uint8_t, see note


//=//// FLAGS TYPE ////////////////////////////////////////////////////////=//
//
// !!! Originally the Flags type was a `uint_fast32_t`.  However, there were
// several cases of the type being used with macros that only work with
// platform sized ints.  But really, uintptr_t and uint_fast32_t are likely
// the same type on most platforms anyway.
//
typedef uintptr_t Flags;


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
typedef uint_fast32_t REBLEN; // series length, unsigned, at *least* 32 bits
typedef size_t Size; // 32 bit (size in bytes)
typedef int64_t REBI64; // 64 bit integer
typedef uint64_t REBU64; // 64 bit unsigned integer
typedef float REBD32; // 32 bit decimal
typedef double REBDEC; // 64 bit decimal
typedef uintptr_t LineNumber; // type used to store line numbers in Rebol files
typedef uintptr_t Tick; // type the debug build uses for evaluator "ticks"

typedef intptr_t Count;

// !!! Review this choice from R3-Alpha:
//
// https://stackoverflow.com/q/1153548/
//
#define MIN_D64 ((double)-9.2233720368547758e18)
#define MAX_D64 ((double) 9.2233720368547758e18)


//=//// UNICODE CODEPOINT /////////////////////////////////////////////////=//
//
// Ucs2Unit is currently a two-byte representation of a Unicode codepoint.  It
// is not UTF-16...it's simply limited to 16-bit codepoints (UCS-2).  R3-Alpha
// did not have CHAR! values higher than that.
//
// Ren-C is being adapted to where this will become a full 32-bit value.  The
// goal is to retrofit the code to use "UTF-8 Everywhere".  In the meantime,
// Ucs2Unit is used internally to store Rebol ANY-STRING!s.  When all references
// to it have been changed to use the Ucs2(*) interface for safe variable
// sized encoding enumeration, a switch can be flipped and it can be upgraded.
//
typedef REBWCHAR Ucs2Unit;
#define MAX_UNI \
    ((1 << (8 * sizeof(Ucs2Unit))) - 1)

typedef uint32_t Codepoint;


//=//// MEMORY POOLS //////////////////////////////////////////////////////=//
//
typedef struct rebol_mem_pool REBPOL;
typedef struct PoolUnitStruct PoolUnit;


//=//// RELATIVE VALUES ///////////////////////////////////////////////////=//
//
// Note that in the C build, %rebol.h forward-declares `struct Reb_Value` and
// then #defines Value to that.
//
#if NO_CPLUSPLUS_11
    #define Cell RebolValue  // same as Value, no checking in C build
#else
    struct Reb_Relative_Value; // won't implicitly downcast to Value
    #define Cell \
        struct Reb_Relative_Value // *might* be IS_RELATIVE()
#endif


//=//// FLEX SUBCLASSES ///////////////////////////////////////////////////=//
//
// Note that because the StubStruct includes a Cell by value,
// the %sys-rebser.h must be included *after* %sys-rebval.h; however the
// higher level definitions in %sys-series.h are *before* %sys-value.h.
//

struct StubStruct;
typedef struct StubStruct Stub;

typedef Stub Flex;

#if CPLUSPLUS_11
    struct Binary;
    struct Symbol;
    struct String;

    struct Array;

    struct VarList;
    struct Error;

    struct REBACT;
    struct REBMAP;
#else
    typedef Flex Binary;
    typedef Flex Symbol;
    typedef Flex String;

    typedef Flex Array;

    typedef Flex VarList;
    typedef Flex Error;

    typedef Flex REBACT;
    typedef Flex REBMAP;
#endif




//=//// BINDING ///////////////////////////////////////////////////////////=//

typedef void Node;

typedef Stub Specifier;

struct Reb_Binder;
struct Reb_Collector;


//=//// FRAMES ////////////////////////////////////////////////////////////=//
//
// Paths formerly used their own specialized structure to track the path,
// (path-value-state), but now they're just another kind of frame.  It is
// helpful for the moment to give them a different name.
//

struct LevelStruct;

typedef struct LevelStruct Level;
typedef struct LevelStruct REBPVS;

struct Reb_State;

//=//// DATA STACK ////////////////////////////////////////////////////////=//
//
typedef uint_fast32_t StackIndex;  // 0 for empty stack ([0] entry is trash)


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
    cast(Option(SymId), SYM_0_internal)  // 0 cast needed if not -fpermissive

#if DEBUG_CHECK_OPTIONALS
    bool operator==(Option(SymId)& a, Option(SymId)& b) = delete;
    void operator!=(Option(SymId)& a, Option(SymId)& b) = delete;
#endif


// The Bounce type is a Value* but with the idea that it is legal to hold
// types like REB_R_THROWN, etc. which are understood specially by the
// evaluator as return values from natives.
//
// It is called a "Bounce" to match modern Ren-C because these are signals
// understood by the trampoline.  This older codebase is not stackless, but
// having the name of the return values align is good.
//
typedef Value* Bounce;


//=//// DISPATCHERS ///////////////////////////////////////////////////////=//
//
typedef REBINT (*COMPARE_HOOK)(const Cell* a, const Cell* b, REBINT s);
typedef Bounce (*MAKE_HOOK)(Value*, enum Reb_Kind, const Value*);
typedef Bounce (*TO_HOOK)(Value*, enum Reb_Kind, const Value*);


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
struct MolderStruct;
typedef struct MolderStruct Molder;
typedef void (*MOLD_HOOK)(Molder* mo, const Cell* v, bool form);


// These definitions are needed in %sys-rebval.h, and can't be put in
// %sys-rebact.h because that depends on Array, which depends on
// StubStruct, which depends on values... :-/

// C function implementing a native ACTION!
//
typedef Bounce (*REBNAT)(Level* level_);
#define DECLARE_NATIVE(name) \
    Bounce N_##name(Level* level_)

#define NATIVE_CFUNC(name)  N_##name

// Generic hooks: implementing a "verb" ACTION! for a particular
// type (or class of types).
//
typedef Bounce (*GENERIC_HOOK)(Level* level_, Value* verb);
#define REBTYPE(name) \
    Bounce T_##name(Level* level_, Value* verb)

// Port hook: for implementing generic ACTION!s on a PORT! class
//
typedef Bounce (*PORT_HOOK)(Level* level_, Value* port, Value* verb);

// Path evaluator function
//
typedef Bounce (*PATH_HOOK)(
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
    REBLEN bits; // !!! alias used for hashing date, is this standards-legal?
} REBDAT;

typedef struct rebol_time_fields {
    REBLEN h;
    REBLEN m;
    REBLEN s;
    REBLEN n;
} REB_TIMEF;

#include "sys-deci.h"
