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
// !!! Is it better to use the _MAX definitions than this?
//
// https://github.com/LambdaSchool/CS-Wiki/wiki/Casting-Signed-to-Unsigned-in-C
//
#define NOT_FOUND ((REBLEN)-1)
#define UNLIMITED ((REBLEN)-1)


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
// Because a REBCEL can be linked to by a QUOTED!, it's important not to
// modify the potentially-shared escaped data.  So all REBCEL* should be
// const.  That's enforced in the C++ debug build, and a wrapping class is
// used for the pointer to make sure one doesn't assume it lives in an array
// and try to do pointer math on it...since it may be a singular allocation.
//
#if !defined(CPLUSPLUS_11)

    #define REBCEL(const_star) \
        const struct Reb_Value *  // same as RELVAL, no checking in C build

#elif defined(NDEBUG)
    //
    // The %sys-internals.h API is used by core extensions, and we may want
    // to build the executable with C++ but an extension with C.  If there
    // are "trick" pointer types that are classes with methods passed in
    // the API, that would inhibit such an implementation.
    //
    // Making it easy to configure such a mixture isn't a priority at this
    // time.  But just make sure some C++ builds are possible without
    // using the active pointer class.  Choose debug builds for now.
    //
    struct Reb_Cell;  // won't implicitly downcast to RELVAL
    #define REBCEL(const_star) \
        const struct Reb_Cell *  // not a class instance in %sys-internals.h
#else
    struct Reb_Cell;  // won't implicitly downcast to RELVAL
    template<typename T>
    struct RebcellPtr {
        T p;
        static_assert(
            std::is_same<const Reb_Cell*, T>::value,
            "Instantiations of REBCEL only work as REBCEL(const*)"
        );

        RebcellPtr () { }
        RebcellPtr (const Reb_Cell *p) : p (p) {}

        const Reb_Cell **operator&() { return &p; }
        const Reb_Cell *operator->() { return p; }
        const Reb_Cell &operator*() { return *p; }

        operator const Reb_Cell* () { return p; }

        explicit operator const Reb_Value* ()
          { return reinterpret_cast<const Reb_Value*>(p); }

        explicit operator const Reb_Relative_Value* ()
          { return reinterpret_cast<const Reb_Relative_Value*>(p); }
    };
    #define REBCEL(const_star) \
        struct RebcellPtr<Reb_Cell const_star>
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

typedef REBSER REBBMK;  // "bookmark" (list of UTF-8 index=>offsets)

typedef REBSER REBTYP;  // Rebol Type (list of hook function pointers)


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

typedef struct Reb_Feed REBFED;

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
typedef REBINT (COMPARE_HOOK)(REBCEL(const*) a, REBCEL(const*) b, bool strict);


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


// PER-TYPE MOLD HOOKS: for `mold value` and `form value`
//
// Note: ERROR! may be a context, but it has its own special FORM-ing
// beyond the class (falls through to ANY-CONTEXT! for mold), and BINARY!
// has a different handler than strings.  So not all molds are driven by
// their class entirely.
//
typedef void (MOLD_HOOK)(REB_MOLD *mo, REBCEL(const*) v, bool form);


//=//// PARAMETER ENUMERATION /////////////////////////////////////////////=//
//
// Parameter lists of composed/derived functions still must have compatible
// frames with their underlying C code.  This makes parameter enumeration of
// a derived function a 2-pass process that is a bit tricky.
//
// !!! Due to a current limitation of the prototype scanner, a function type
// can't be used directly in a function definition and have it be picked up
// for %tmp-internals.h, it has to be a typedef.
//
typedef enum {
    PHF_SORTED_PASS = 1 << 0,
    PHF_UNREFINED = 1 << 1,  // a /refinement that takes an arg, made "normal"
    PHF_DEMODALIZED = 1 << 2  // an @param with its refinement specialized out
} Reb_Param_Hook_Flags;
#define PHF_MASK_NONE 0
typedef bool (PARAM_HOOK)(REBVAL *v, REBFLGS flags, void *opaque);


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
    REBPVS *pvs, const RELVAL *picker, const REBVAL *opt_setval
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
    REBLEN h;
    REBLEN m;
    REBLEN s;
    REBLEN n;
} REB_TIMEF;

#include "sys-deci.h"



// To help document places in the core that are complicit in the "extension
// hack", alias arrays being used for the FFI and GOB to another name.
//
typedef REBARR REBGOB;

typedef REBARR REBSTU;
typedef REBARR REBFLD;


//=//// R3-ALPHA DEVICE / DEVICE REQUEST //////////////////////////////////=//
//
// In order to decouple the interpreter from R3-Alpha's device model (and
// still keep that code as optional in the build for those who need it),
// REBREQ has become a series instead of a raw C struct.  That gives it the
// necessary features to be GC marked--either by holding cells in it as an
// array, or using LINK()/MISC() with SERIES_INFO_XXX_IS_CUSTOM_NODE.
//
struct Reb_Request;
#define REBREQ struct Reb_Series
struct rebol_device;
#define REBDEV struct rebol_device
