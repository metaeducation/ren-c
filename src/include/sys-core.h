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
// * With those types defined, it includes %tmp-internals.h - which is all
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
// !!! Because this header is included by all files in the core, it has been a
// bit of a dumping ground for flags and macros that have no particular home.
// Addressing that is an ongoing process.
//

#define REB_DEF // kernel definitions and structs
#include "reb-config.h"


//=//// INCLUDE EXTERNAL API /////////////////////////////////////////////=//
//
// Historically, Rebol source did not include the external library, because it
// was assumed the core would never want to use the less-privileged and higher
// overhead API.  However, libRebol now operates on REBVAL* directly (though
// opaque to clients).  It has many conveniences, and is the preferred way to
// work with isolated values that need indefinite duration.
//
#include <stdlib.h> // size_t and other types used in rebol.h
#include "pstdint.h" // polyfill <stdint.h> for pre-C99/C++11 compilers
#include "pstdbool.h" // polyfill <stdbool.h> for pre-C99/C++11 compilers
#if !defined(REBOL_IMPLICIT_END)
    #define REBOL_EXPLICIT_END // ensure core compiles with pre-C99/C++11
#endif
#include "rebol.h"


//=//// STANDARD DEPENDENCIES FOR CORE ////////////////////////////////////=//

#include "reb-c.h"

#include <stdarg.h> // va_list, va_arg()...
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <stddef.h> // for offsetof()

// assert() is enabled by default; disable with `#define NDEBUG`
// http://stackoverflow.com/a/17241278
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
#if defined(NDEBUG) && !defined(DEBUG_STDIO_OK)
    #define printf dont_include_stdio_h
    #define fprintf dont_include_stdio_h
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

    // NOTE: F/PRINTF DOES NOT ALWAYS FFLUSH() BUFFERS AFTER NEWLINES; it is
    // an "implementation defined" behavior, and never applies to redirects:
    //
    // https://stackoverflow.com/a/5229135/211160
    //
    // So when writing information you intend to be flushed before a potential
    // crash, be sure to fflush(), regardless of using `\n` or not.
#endif


// Internal configuration:
#define STACK_MIN   4000        // data stack increment size
#define STACK_LIMIT 400000      // data stack max (6.4MB)
#define MIN_COMMON 10000        // min size of common buffer
#define MAX_COMMON 100000       // max size of common buffer (shrink trigger)
#define MAX_NUM_LEN 64          // As many numeric digits we will accept on input
#define MAX_EXPAND_LIST 5       // number of series-1 in Prior_Expand list
#define UNICODE_CASES 0x2E00    // size of unicode folding table
#define HAS_SHA1                // allow it
#define HAS_MD5                 // allow it


// This does all the forward definitions that are necessary for the compiler
// to be willing to build %tmp-internals.h.  Some structures are fully defined
// and some are only forward declared.
//
#include "reb-defs.h"


// Small integer symbol IDs, e.g. SYM_THRU or SYM_ON, for built-in words so
// that they can be used in C switch() statements.
//
#include "tmp-symbols.h"


//=////////////////////////////////////////////////////////////////////////=//
//
// #INCLUDE THE AUTO-GENERATED FUNCTION PROTOTYPES FOR THE INTERNAL API
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The somewhat-awkward requirement to have all the definitions up-front for
// all the prototypes, instead of defining them in a hierarchy, comes from
// the automated method of prototype generation.  If they were defined more
// naturally in individual includes, it could be cleaner...at the cost of
// needing to update prototypes separately from the definitions.
//
// See %make/make-headers.r for the generation of this list.
//
#include "tmp-internals.h"


// Rebol versioning information, basically 5 numbers for a tuple.
//
#include "tmp-version.h"


//
// PROGRAMMATIC C BREAKPOINT
//
// This header file brings in the ability to trigger a programmatic breakpoint
// in C code, by calling `debug_break();`  It is not supported by HaikuOS R1,
// so instead kick into an infinite loop which can be broken and stepped out
// of in the debugger.
//
#if !defined(NDEBUG) || defined(DEBUG_COUNT_TICKS)
    #if defined(TO_HAIKU) || defined(TO_EMSCRIPTEN)
        inline static int debug_break() {
            int x = 0;
            while (1) { ++x; }
            x = 0; // set next statement in debugger to here
        }
    #else
        #include "debugbreak.h"
    #endif
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  TICK-RELATED FUNCTIONS <== **THESE ARE VERY USEFUL**
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Each iteration of DO bumps a global count, that in deterministic repro
// cases can be very helpful in identifying the "tick" where certain problems
// are occurring.  The debug build pokes this ticks lots of places--into
// value cells when they are formatted, into series when they are allocated
// or freed, or into stack frames each time they perform a new operation.
//
// BREAK_NOW() will show the stack status at the right moment.  If you have a
// reproducible tick count, then BREAK_ON_TICK() is useful.  See also
// TICK_BREAKPOINT in %c-eval.c for a description of all the places the debug
// build hides tick counts which may be useful for sleuthing bug origins.
//
// The SPORADICALLY() macro uses the count to allow flipping between different
// behaviors in debug builds--usually to run the release behavior some of the
// time, and the debug behavior some of the time.  This exercises the release
// code path even when doing a debug build.
//

#define BREAK_NOW() /* macro means no stack frame, breaks at callsite */ \
    do { \
        printf("BREAK_ON_TICK() @ tick %ld\n", cast(long int, TG_Tick)); \
        fflush(stdout); \
        Dump_Frame_Location(nullptr, FS_TOP); \
        debug_break(); /* see %debug_break.h */ \
    } while (false)

#define BREAK_ON_TICK(tick) \
    if (tick == TG_Tick) BREAK_NOW()

#if defined(NDEBUG) || !defined(DEBUG_COUNT_TICKS)
    #define SPORADICALLY(modulus) \
        false
#else
    #define SPORADICALLY(modulus) \
        (TG_Tick % modulus == 0)
#endif



#include "reb-device.h"
#include "reb-event.h"

// !!! Definitions for the memory allocator generally don't need to be
// included by all clients, though currently it is necessary to indicate
// whether a "node" is to be allocated from the REBSER pool or the REBGOB
// pool.  Hence, the REBPOL has to be exposed to be included in the
// function prototypes.  Review this necessity when REBGOB is changed.
//
#include "mem-pools.h"

#include "sys-rebnod.h"

#include "sys-rebval.h" // REBVAL structure definition
#include "sys-rebser.h" // REBSER series definition (embeds REBVAL definition)
#include "sys-rebact.h" // REBACT and ACT()
#include "sys-rebctx.h" // REBCTX and CTX()

#include "sys-state.h"
#include "sys-rebfrm.h" // `REBFRM` definition (also used by value)
#include "sys-indexor.h" // REBIXO definition

#include "sys-mold.h"

//-- Port actions (for native port schemes):


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
    bool  watch_recycle;
    bool  watch_series;
    bool  watch_expand;
    bool  crash_dump;
} REB_OPTS;


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
    MKF_RETURN      = 1 << 0,   // give a RETURN (but local RETURN: overrides)
    MKF_KEYWORDS    = 1 << 1,   // respond to tags like <opt>, <with>, <local>
    MKF_ANY_VALUE   = 1 << 2,   // args and return are [<opt> any-value!]
    MKF_FAKE_RETURN = 1 << 3    // has RETURN label for HELP, but not in frame
};

#define MKF_MASK_NONE 0 // no special handling (e.g. MAKE ACTION!)

// Mathematical set operations for UNION, INTERSECT, DIFFERENCE
enum {
    SOP_NONE = 0, // used by UNIQUE (other flags do not apply)
    SOP_FLAG_BOTH = 1 << 0, // combine and interate over both series
    SOP_FLAG_CHECK = 1 << 1, // check other series for value existence
    SOP_FLAG_INVERT = 1 << 2 // invert the result of the search
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


// Options for To_REBOL_Path
enum {
    PATH_OPT_SRC_IS_DIR = 1 << 0
};


#define TAB_SIZE 4

// Move these things:
enum act_modify_mask {
    AM_PART = 1 << 0,
    AM_SPLICE = 1 << 1,
    AM_LINE = 1 << 2
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

// Encoding options (reduced down to just being used by WRITE-STDOUT)
//
enum encoding_opts {
    OPT_ENC_0 = 0,
    OPT_ENC_RAW = 1 << 0
};


enum {
    REB_FILETOLOCAL_0 = 0, // make it clearer when using no options
    REB_FILETOLOCAL_FULL = 1 << 0, // expand path relative to current dir
    REB_FILETOLOCAL_WILD = 1 << 1, // add on a `*` for wildcard listing

    // !!! A comment in the R3-Alpha %p-dir.c said "Special policy: Win32 does
    // not want tail slash for dir info".
    //
    REB_FILETOLOCAL_NO_TAIL_SLASH = 1 << 2 // don't include the terminal slash
};


#define ALL_BITS \
    ((REBCNT)(-1))

enum {
    BEL =   7,
    BS  =   8,
    LF  =  10,
    CR  =  13,
    ESC =  27,
    DEL = 127
};

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
    cast(int32_t, floor((MAX(cast(REBD32, INT32_MIN), MIN(cast(REBD32, INT32_MAX), d))) + 0.5))



#include "tmp-constants.h"

// %tmp-paramlists.h is the file that contains macros for natives and actions
// that map their argument names to indices in the frame.  This defines the
// macros like INCLUDE_ARGS_FOR_INSERT which then allow you to naturally
// write things like REF(part) and ARG(limit), instead of the brittle integer
// based system used in R3-Alpha such as D_REF(7) and D_ARG(3).
//
#include "tmp-paramlists.h"

#include "tmp-boot.h"
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


#include "tmp-error-funcs.h" // functions below are called


#include "sys-trap.h" // includes PUSH_TRAP, fail(), and panic() macros

#include "sys-node.h"

#include "sys-value.h" // basic definitions that don't need series accessrors
#include "sys-time.h"

inline static void SET_SIGNAL(REBFLGS f) { // used in %sys-series.h
    Eval_Signals |= f;
    Eval_Count = 1;
}

#include "sys-series.h"
#include "sys-binary.h"
#include "sys-string.h"
#include "sys-typeset.h"

#include "sys-array.h"

#include "sys-handle.h"

#include "sys-action.h"
#include "sys-context.h"
#include "sys-word.h"

#include "sys-pair.h"
#include "sys-map.h"

#include "sys-varargs.h"

#include "sys-stack.h"

#include "sys-frame.h"
#include "sys-bind.h"

#include "sys-library.h"

#include "sys-image.h" // should be defined in an extension

#include "host-lib.h"

/***********************************************************************
**
**  Macros
**
***********************************************************************/

#define UP_CASE(c) Upper_Cases[c]
#define LO_CASE(c) Lower_Cases[c]
#define IS_WHITE(c) ((c) <= 32 and (White_Chars[c] & 1) != 0)
#define IS_SPACE(c) ((c) <= 32 and (White_Chars[c] & 2) != 0)

#define GET_SIGNAL(f) \
    (did (Eval_Signals & (f)))

#define CLR_SIGNAL(f) \
    cast(void, Eval_Signals &= ~(f))


//-- Temporary Buffers
//   These are reused for cases for appending, when length cannot be known.

#define BUF_COLLECT \
    TG_Buf_Collect

#define BYTE_BUF \
    TG_Byte_Buf

#define MOLD_BUF \
    TG_Mold_Buf

#define BUF_UTF8 \
    TG_Buf_Utf8

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
        and IS_TRUTHY(Get_System(SYS_OPTIONS, (option))) \
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

extern GENERIC_HOOK Generic_Hooks[REB_MAX];
extern PATH_HOOK Path_Hooks[REB_MAX];
extern COMPARE_HOOK Compare_Hooks[REB_MAX];
extern MAKE_HOOK Make_Hooks[REB_MAX];
extern TO_HOOK To_Hooks[REB_MAX];
extern MOLD_HOOK Mold_Or_Form_Hooks[REB_MAX];

#include "sys-roots.h"

#include "sys-eval.h" // low-level single-step evaluation API
#include "sys-do.h" // higher-level evaluate-until-end API

#include "sys-path.h"
