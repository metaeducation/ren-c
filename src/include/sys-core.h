//
//  File: %sys-core.h
//  Summary: "Single Complete Include File for Using the Internal Api"
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
// This is the main include file used in the implementation of the core.
//
// * It defines all the data types and structures used by the auto-generated
//   function prototypes.  This includes the obvious REBINT, REBVAL, REBSER.
//   It also includes any enumerated type parameters to functions which are
//   shared between various C files.
//
// * With those types defined, it includes %tmp-funcs.h - which is basically
//   all the non-inline "internal API" functions.  This list of function
//   prototypes is generated automatically by a Rebol script that scans the
//   %.c files during the build process. 
//
// * Next it starts including various headers in a specific order.  These
//   build on the data definitions and call into the internal API.  Since they
//   are often inline functions and not macros, the complete prototypes and
//   data definitions they use must have already been defined.
//
// %sys-core.h is supposed to be platform-agnostic.  All the code which would
// include something like <windows.h> would be linked in as "host code".  Yet
// if a file wishes to include %sys-core.h and <windows.h>, it should do:
//
//     #define UNICODE // enable unicode OS API in windows.h
//     #include <windows.h>
//
//     /* #include any non-Rebol windows dependencies here */
//
//     #undef IS_ERROR // means something different
//     #undef max // same
//     #undef min // same
//
//     #include "sys-core.h"
//

#include "reb-config.h"

// Internal configuration:
#define REB_DEF                 // kernel definitions and structs
#define STACK_MIN   4000        // data stack increment size
#define STACK_LIMIT 400000      // data stack max (6.4MB)
#define MIN_COMMON 10000        // min size of common buffer
#define MAX_COMMON 100000       // max size of common buffer (shrink trigger)
#define MAX_NUM_LEN 64          // As many numeric digits we will accept on input
#define MAX_EXPAND_LIST 5       // number of series-1 in Prior_Expand list
#define UNICODE_CASES 0x2E00    // size of unicode folding table
#define HAS_SHA1                // allow it
#define HAS_MD5                 // allow it

// External system includes:
#include <stdlib.h>
#include <stdarg.h>     // For var-arg Print functions
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <stddef.h>     // for offsetof()


//
// ASSERTIONS
//
// Assertions are in debug builds only, and use the conventional standard C
// assert macro.  The code inside the assert will be removed if the flag
// NDEBUG is defined to indicate "NoDEBUGging".  While negative logic is
// counter-intuitive (e.g. `#ifndef NDEBUG` vs. `#ifdef DEBUG`) it's the
// standard and is the least of evils:
//
// http://stackoverflow.com/a/17241278/211160
//
// Assertions should mostly be used as a kind of "traffic cone" when working
// on new code (or analyzing a bug you're trying to trigger in development).
// It's preferable to update the design via static typing or otherwise as the
// code hardens.
//
#include <assert.h>
#include "assert-fixes.h"


//
// DISABLE STDIO.H IN RELEASE BUILD
//
// The core build of Rebol published in R3-Alpha sought to not be dependent
// on <stdio.h>.  The intent--ostensibly--was since Rebol had richer tools
// like WORD!s and BLOCK! for dialecting, that including a brittle historic
// string-based C "mini-language" of printf into the executable was a
// wasteful dependency.  Also, many implementations are clunky:
//
// http://blog.hostilefork.com/where-printf-rubber-meets-road/
//
// Hence formatted output was not presumed as a host service, which only
// provided raw character string output.
//
// This "radical decoupling" idea was undermined by including a near-rewrite
// of printf() called Debug_Fmt().  This was a part of release builds, and
// added format specifiers for Rebol values ("%r") or series, as well as
// handling a subset of basic C types.
//
// Ren-C's long-term goal is to not include any string-based dialect for
// formatting output.  Low-level diagnostics in the debug build will rely on
// printf, while all release build formatting will be done through Rebol
// code...where the format specification is done with a Rebol BLOCK! dialect
// that could be used by client code as well.
//
// To formalize this rule, these definitions will help catch uses of <stdio.h>
// in the release build, and give a hopefully informative error.
//
#if defined(NDEBUG) && !defined(REN_C_STDIO_OK)
    #define printf dont_include_stdio_h
    #define fprintf dont_include_stdio_h
    #define putc dont_include_stdio_h
#else
    // Desire to not bake in <stdio.h> notwithstanding, in debug builds it
    // can be convenient (or even essential) to have access to stdio.  This
    // is especially true when trying to debug the core I/O routines and
    // unicode/UTF8 conversions that Rebol seeks to replace stdio with.
    //
    // Hence debug builds are allowed to use stdio.h conveniently.  The
    // release build should catch if any of these aren't #if !defined(NDEBUG)
    //
    #include <stdio.h>
#endif


//
// PROGRAMMATIC C BREAKPOINT
//
// This header file brings in the ability to trigger a programmatic breakpoint
// in C code, by calling `debug_break();`  It is not supported by HaikuOS R1,
// so instead kick into an infinite loop which can be broken and stepped out
// of in the debugger.
//
#if !defined(NDEBUG)
    #if defined(TO_HAIKU)
        inline static int debug_break() {
            int x = 0;
            while (1) { ++x; }
            x = 0; // set next statement in debugger to here
        }
    #else
        #include "debugbreak.h"
    #endif
#endif


// The %reb-c.h file includes something like C99's <stdint.h> for setting up
// a basis for concrete data type sizes, which define the Rebol basic types
// (such as REBOOL, REBYTE, REBU64, etc.)  It also contains some other helpful
// macros and tools for C programming.
//
#include "reb-c.h"
#include "reb-defs.h"

#include "reb-device.h"
#include "reb-types.h"
#include "reb-event.h"

#include "reb-file.h"

#include "sys-rebnod.h"

#include "sys-deci.h"

#include "tmp-bootdefs.h"

#include "sys-rebval.h" // REBVAL structure definition
#include "sys-action.h"

typedef void (*CLEANUP_FUNC)(const REBVAL*); // for some HANDLE!s GC callback

#include "sys-rebser.h" // REBSER series definition (embeds REBVAL definition)

typedef void (*MAKE_FUNC)(REBVAL*, enum Reb_Kind, const REBVAL*);
typedef void (*TO_FUNC)(REBVAL*, enum Reb_Kind, const REBVAL*);

#include "sys-state.h"
#include "sys-rebfrm.h" // `REBFRM` definition (also used by value)
#include "sys-indexor.h" // REBIXO definition

//-- Port actions (for native port schemes):

typedef struct rebol_port_action_map {
    REBSYM action;
    REBPAF func;
} PORT_ACTION;

typedef struct rebol_mold {
    REBSER *series;     // destination series (uni)
    REBCNT start;       // index where this mold starts within series
    REBFLGS opts;        // special option flags
    REBCNT limit;       // how many characters before cutting off with "..."
    REBCNT reserve;     // how much capacity to reserve at the outset
    REBINT indent;      // indentation amount
    REBYTE period;      // for decimal point
    REBYTE dash;        // for date fields
    REBYTE digits;      // decimal digits
} REB_MOLD;

#define Drop_Mold_If_Pushed(mo) \
    Drop_Mold_Core((mo), TRUE)

#define Drop_Mold(mo) \
    Drop_Mold_Core((mo), FALSE)

#define Pop_Molded_String(mo) \
    Pop_Molded_String_Core((mo), UNKNOWN)

#define Pop_Molded_String_Len(mo,len) \
    Pop_Molded_String_Core((mo), (len))

#define Mold_Value(mo,v) \
    Mold_Or_Form_Value((mo), (v), FALSE)

#define Form_Value(mo,v) \
    Mold_Or_Form_Value((mo), (v), TRUE)

#define Copy_Mold_Value(v,opts) \
    Copy_Mold_Or_Form_Value((v), (opts), FALSE)

#define Copy_Form_Value(v,opts) \
    Copy_Mold_Or_Form_Value((v), (opts), TRUE)


/***********************************************************************
**
**  Structures
**
***********************************************************************/

//-- Measurement Variables:
typedef struct rebol_stats {
    REBI64  Series_Memory;
    REBCNT  Series_Made;
    REBCNT  Series_Freed;
    REBCNT  Series_Expanded;
    REBCNT  Recycle_Counter;
    REBCNT  Recycle_Series_Total;
    REBCNT  Recycle_Series;
    REBI64  Recycle_Prior_Eval;
    REBCNT  Mark_Count;
    REBCNT  Blocks;
    REBCNT  Objects;
} REB_STATS;

//-- Options of various kinds:
typedef struct rebol_opts {
    REBOOL  watch_recycle;
    REBOOL  watch_series;
    REBOOL  watch_expand;
    REBOOL  crash_dump;
} REB_OPTS;

typedef struct rebol_time_fields {
    REBCNT h;
    REBCNT m;
    REBCNT s;
    REBCNT n;
} REB_TIMEF;


/***********************************************************************
**
**  Constants
**
***********************************************************************/

enum Boot_Phases {
    BOOT_START = 0,
    BOOT_LOADED,
    BOOT_ERRORS,
    BOOT_MEZZ,
    BOOT_DONE
};

enum Boot_Levels {
    BOOT_LEVEL_BASE,
    BOOT_LEVEL_SYS,
    BOOT_LEVEL_MODS,
    BOOT_LEVEL_FULL
};

// Modes allowed by Make_Function:
enum {
    MKF_NONE        = 0,        // no special handling (e.g. MAKE FUNCTION!)
    MKF_RETURN      = 1 << 0,   // has definitional RETURN
    MKF_LEAVE       = 1 << 1,   // has definitional LEAVE
    MKF_KEYWORDS    = 1 << 2,   // respond to tags like <opt>, <with>, <local>
    MKF_ANY_VALUE   = 1 << 3,   // args and return are [<opt> any-value!]
    MKF_FAKE_RETURN = 1 << 4    // has RETURN but not actually in frame
};

// Modes allowed by FORM
enum {
    FORM_FLAG_ONLY = 0,
    FORM_FLAG_REDUCE = 1 << 0,
    FORM_FLAG_NEWLINE_SEQUENTIAL_STRINGS = 1 << 1,
    FORM_FLAG_NEWLINE_UNLESS_EMPTY = 1 << 2,
    FORM_FLAG_MOLD = 1 << 3
};

// Modes allowed by Copy_Block function:
enum {
    COPY_SHALLOW = 0,
    COPY_DEEP,          // recurse into blocks
    COPY_STRINGS,       // copy strings in blocks
    COPY_ALL,           // both deep, strings (3)
//  COPY_IGNORE = 4,    // ignore tail position (used for stack args)
    COPY_OBJECT = 8,    // copy an object
    COPY_SAME = 16
};


// Breakpoint hook callback, signature:
//
//     REBOOL Do_Breakpoint_Throws(
//         REBVAL *out,
//         REBOOL interrupted, // Ctrl-C (as opposed to a BREAKPOINT)
//         const REBVAL *default_value,
//         REBOOL do_default
//      );
//
// Typically, the handler will be set up to dispatch back into the REPL.
//
typedef REBOOL (*REBBRK)(REBVAL *, REBOOL, const REBVAL*, REBOOL);


// Flags used for Protect functions
//
enum {
    PROT_SET = 1 << 0,
    PROT_DEEP = 1 << 1,
    PROT_HIDE = 1 << 2,
    PROT_WORD = 1 << 3,
    PROT_FREEZE = 1 << 4
};

// Mold and form options:
enum REB_Mold_Opts {
    MOLD_FLAG_0 = 0,
    MOLD_FLAG_ALL = 1 << 0, // Output lexical types in #[type...] format
    MOLD_FLAG_COMMA_PT = 1 << 1, // Decimal point is a comma.
    MOLD_FLAG_SLASH_DATE = 1 << 2, // Date as 1/1/2000
    MOLD_FLAG_INDENT = 1 << 3, // Indentation
    MOLD_FLAG_TIGHT = 1 << 4, // No space between block values
    MOLD_FLAG_ONLY = 1 << 5, // Mold/only - no outer block []
    MOLD_FLAG_LINES  = 1 << 6, // add a linefeed between each value
    MOLD_FLAG_LIMIT = 1 << 7, // Limit length to mold->limit, then "..."
    MOLD_FLAG_RESERVE = 1 << 8  // At outset, reserve capacity for buffer
};

// Temporary:
#define MOLD_FLAG_NON_ANSI_PARENED \
    MOLD_FLAG_ALL // Non ANSI chars are ^() escaped

#define DECLARE_MOLD(name) \
    REB_MOLD mold_struct; \
    CLEARS(&mold_struct); \
    REB_MOLD *name = &mold_struct; \

#define SET_MOLD_FLAG(mo,f) \
    ((mo)->opts |= (f))

#define GET_MOLD_FLAG(mo,f) \
    LOGICAL((mo)->opts & (f))

#define NOT_MOLD_FLAG(mo,f) \
    NOT((mo)->opts & (f))

#define CLEAR_MOLD_FLAG(mo,f) \
    ((mo)->opts &= ~(f))

typedef void (*MOLD_FUNC)(REB_MOLD *mo, const RELVAL *v, REBOOL form);

// Special flags for decimal formatting:
enum {
    DEC_MOLD_PERCENT = 1 << 0,      // follow num with %
    DEC_MOLD_MINIMAL = 1 << 1       // allow decimal to be integer
};

// Options for To_REBOL_Path
enum {
    PATH_OPT_SRC_IS_DIR = 1 << 0
};


#define TAB_SIZE 4

// Move these things:
enum act_modify_mask {
    AM_BINARY_SERIES = 1 << 0,
    AM_PART = 1 << 1,
    AM_ONLY = 1 << 2
};
enum act_find_mask {
    AM_FIND_ONLY = 1 << 0,
    AM_FIND_CASE = 1 << 1,
    AM_FIND_LAST = 1 << 2,
    AM_FIND_REVERSE = 1 << 3,
    AM_FIND_TAIL = 1 << 4,
    AM_FIND_MATCH = 1 << 5
};
enum act_open_mask {
    AM_OPEN_NEW = 1 << 0,
    AM_OPEN_READ = 1 << 1,
    AM_OPEN_WRITE = 1 << 2,
    AM_OPEN_SEEK = 1 << 3,
    AM_OPEN_ALLOW = 1 << 4
};
// Rounding flags (passed as refinements to ROUND function):
enum {
    RF_TO = 1 << 0,
    RF_EVEN = 1 << 1,
    RF_DOWN = 1 << 2,
    RF_HALF_DOWN = 1 << 3,
    RF_FLOOR = 1 << 4,
    RF_CEILING = 1 << 5,
    RF_HALF_CEILING = 1 << 6
};

enum rebol_signals {
    //
    // SIG_RECYCLE indicates a need to run the garbage collector, when
    // running it synchronously could be dangerous.  This is important in
    // particular during memory allocation, which can detect crossing a
    // memory usage boundary that suggests GC'ing would be good...but might
    // be in the middle of code that is halfway through manipulating a
    // managed series.
    //
    SIG_RECYCLE = 1 << 0,

    // SIG_HALT means return to the topmost level of the evaluator, regardless
    // of how deep a debug stack might be.  It is the only instruction besides
    // QUIT and RESUME that can currently get past a breakpoint sandbox.
    //
    SIG_HALT = 1 << 1,

    // SIG_INTERRUPT indicates a desire to enter an interactive debugging
    // state.  Because the ability to manage such a state may not be
    // registered by the host, this could generate an error.
    //
    SIG_INTERRUPT = 1 << 2,

    // SIG_EVENT_PORT is to-be-documented
    //
    SIG_EVENT_PORT = 1 << 3
};

// Security flags:
enum {
    SEC_ALLOW,
    SEC_ASK,
    SEC_THROW,
    SEC_QUIT,
    SEC_MAX
};

// Security policy byte offsets:
enum {
    POL_READ,
    POL_WRITE,
    POL_EXEC,
    POL_MAX
};

// Encoding options:
enum encoding_opts {
    OPT_ENC_0 = 0,
    OPT_ENC_CRLF = 1 << 1, // CR line termination, see OPT_ENC_CRLF_MAYBE
    OPT_ENC_RAW = 1 << 2 // raw binary, no encoding
};

#if OS_CRLF
    #define OPT_ENC_CRLF_MAYBE OPT_ENC_CRLF
#else
    #define OPT_ENC_CRLF_MAYBE 0
#endif


// These 3 operations are the current legal set of what can be done with a
// VARARG!.  They integrate with Do_Core()'s limitations in the prefetch
// evaluator--such as to having one unit of lookahead.
//
// While it might seem natural for this to live in %sys-varargs.h, the enum
// type is used by a function prototype in %tmp-funcs.h...hence it must be
// defined before that is included.
//
enum Reb_Vararg_Op {
    VARARG_OP_TAIL_Q, // tail?
    VARARG_OP_FIRST, // "lookahead"
    VARARG_OP_TAKE // doesn't modify underlying data stream--advances index
};

// %sys-do.h needs to call into the scanner if Fetch_Next_In_Frame() is to
// be inlined at all (at its many time-critical callsites), so the scanner
// API has to be exposed with SCAN_STATE before %tmp-funcs.h
//
#include "sys-scan.h"

// Historically, Rebol source did not include %reb-ext.h...because it was
// assumed the core would never want to use the less-privileged and higher
// overhead API.  Hence data types like REBRXT were not included in the core,
// except in the one file that was implementing the "user library".
//
// However, there are cases where code is being migrated to use the internal
// API over to using the external one, so it's helpful to allow calling both.
// We therefore include %reb-ext so it defines RX* for all of the core, and
// these types must be available to process %tmp-funcs.h since the RL_API
// functions appear there too.
//
#include "reb-ext.h"
#include "reb-lib.h"


//=////////////////////////////////////////////////////////////////////////=//
//
// #INCLUDE THE AUTO-GENERATED FUNCTION PROTOTYPES FOR THE INTERNAL API
//
//=////////////////////////////////////////////////////////////////////////=//
//
// All the prior definitions and includes built up to this.  That's to have
// enough of the structs, enumerated types, and typedefs set up to define
// the function prototypes for all the functions shared between the %.c files.
//
// The somewhat-awkward requirement to have all the definitions up-front for
// all the prototypes, instead of defining them in a hierarchy, comes from
// the automated method of prototype generation.  If they were defined more
// naturally in individual includes, it could be cleaner...at the cost of
// needing to update prototypes separately from the definitions.
//
// See %tools/make-headers.r for the generation of this list.
//
#include "tmp-funcs.h"

#include "tmp-strings.h"

// %tmp-paramlists.h is the file that contains macros for natives and actions
// that map their argument names to indices in the frame.  This defines the
// macros like INCLUDE_ARGS_FOR_INSERT which then allow you to naturally
// write things like REF(part) and ARG(limit), instead of the brittle integer
// based system used in R3-Alpha such as D_REF(7) and D_ARG(3).
//
#include "tmp-paramlists.h"

#include "tmp-boot.h"
#include "tmp-errnums.h"
#include "tmp-sysobj.h"
#include "tmp-sysctx.h"


/***********************************************************************
**
**  Threaded Global Variables
**
***********************************************************************/

// !!! In the R3-Alpha open source release, there had apparently been a switch
// from the use of global variables to the classification of all globals as
// being either per-thread (TVAR) or for the whole program (PVAR).  This
// was apparently intended to use the "thread-local-variable" feature of the
// compiler.  It used the non-standard `__declspec(thread)`, which as of C11
// and C++11 is standardized as `thread_local`.
//
// Despite this basic work for threading, greater issues were not hammered
// out.  And so this separation really just caused problems when two different
// threads wanted to work with the same data (at different times).  Such a
// feature is better implemented as in the V8 JavaScript engine as "isolates"  

#ifdef __cplusplus
    #define PVAR extern "C" RL_API
    #define TVAR extern "C" RL_API
#else
    // When being preprocessed by TCC and combined with the user - native
    // code, all global variables need to be declared
    // `extern __attribute__((dllimport))` on Windows, or incorrect code
    // will be generated for dereferences.  Hence these definitions for
    // PVAR and TVAR allow for overriding at the compiler command line.
    //
    #if !defined(PVAR)
        #define PVAR extern RL_API
    #endif
    #if !defined(TVAR)
        #define TVAR extern RL_API
    #endif
#endif

#include "sys-globals.h"

#include "tmp-error-funcs.h"

#include "sys-trap.h" // includes PUSH_TRAP, fail(), and panic() macros

#include "sys-node.h"

#include "sys-value.h" // basic definitions that don't need series accessrors
#include "sys-time.h"

#include "sys-series.h"
#include "sys-binary.h"
#include "sys-string.h"
#include "sys-typeset.h"

#include "sys-array.h"

#include "sys-handle.h"

#include "sys-context.h"
#include "sys-function.h"
#include "sys-word.h"

#include "sys-pair.h"
#include "sys-map.h"

#include "sys-varargs.h"

#include "sys-stack.h"

#include "sys-frame.h"
#include "sys-bind.h"

#include "sys-library.h"

#include "host-lib.h"

/***********************************************************************
**
**  Macros
**
***********************************************************************/

// Generic defines:
#define ALIGN(s, a) (((s) + (a)-1) & ~((a)-1))

#define UP_CASE(c) Upper_Cases[c]
#define LO_CASE(c) Lower_Cases[c]
#define IS_WHITE(c) ((c) <= 32 && (White_Chars[c]&1) != 0)
#define IS_SPACE(c) ((c) <= 32 && (White_Chars[c]&2) != 0)

inline static void SET_SIGNAL(REBFLGS f) {
    Eval_Signals |= f;
    Eval_Count = 1;
}

#define GET_SIGNAL(f) \
    LOGICAL(Eval_Signals & (f))

#define CLR_SIGNAL(f) \
    cast(void, Eval_Signals &= ~(f))


//-- Temporary Buffers
//   These are reused for cases for appending, when length cannot be known.

#define BUF_COLLECT     VAL_ARRAY(TASK_BUF_COLLECT)

#define BYTE_BUF        VAL_SERIES(TASK_BYTE_BUF)
#define MOLD_BUF        VAL_SERIES(TASK_MOLD_BUF)
#define BUF_UTF8        VAL_SERIES(TASK_BUF_UTF8)

enum {
    TRACE_FLAG_FUNCTION = 1 << 0
};

// Most of Ren-C's backwards compatibility with R3-Alpha is attempted through
// usermode "shim" functions.  But some things affect fundamental mechanics
// and can't be done that way.  So in the debug build, system/options
// contains some flags that enable the old behavior to be turned on.
//
// !!! These are not meant to be kept around long term.
//
#if !defined(NDEBUG)
    #define LEGACY(option) ( \
        (PG_Boot_Phase >= BOOT_ERRORS) \
        && IS_TRUTHY(Get_System(SYS_OPTIONS, (option))) \
    )
#endif


//
// Dispatch Table Prototypes
//
// These dispatch tables are generated and have data declarations in the
// %tmp-dispatch.c file.  Those data declarations can only be included once,
// yet the tables may be used in multiple modules.
//
// The tables never contain NULL values.  Instead there is a dispatcher in
// the slot which will fail if it is ever called.
//
// !!! These used to be const, but the desire to move REB_STRUCT and REB_GOB
// into extensions required the tables to be dynamically modified.  This
// should likely be changed back in the future in case it helps performance,
// as these will be "user defined types" that are more like a context than
// a built-in "kind".

extern REBACT Value_Dispatch[REB_MAX];
extern REBPEF Path_Dispatch[REB_MAX];
extern REBCTF Compare_Types[REB_MAX];
extern MAKE_FUNC Make_Dispatch[REB_MAX];
extern TO_FUNC To_Dispatch[REB_MAX];
extern MOLD_FUNC Mold_Or_Form_Dispatch[REB_MAX];


#include "sys-do.h"
#include "sys-path.h"
