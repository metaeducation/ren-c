//
//  File: %sys-core.h
//  Summary: "Single Complete Include File for Using the Internal Api"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// include something like <windows.h> would be linked in as extensions.  Yet
// if a file wishes to include %sys-core.h and <windows.h>, it should do:
//
//     #define WIN32_LEAN_AND_MEAN  // usually desirable for leaner inclusion
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

#include "reb-config.h"


//=//// INCLUDE EXTERNAL API /////////////////////////////////////////////=//
//
// Historically, Rebol source did not include the external library, because it
// was assumed the core would never want to use the less-privileged and higher
// overhead API.  However, libRebol now operates on REBVAL* directly (though
// opaque to clients).  It has many conveniences, and is the preferred way to
// work with isolated values that need indefinite duration.
//
#include <stdlib.h>  // size_t and other types used in rebol.h
#include "pstdint.h"  // polyfill <stdint.h> for pre-C99/C++11 compilers
#include "pstdbool.h"  // polyfill <stdbool.h> for pre-C99/C++11 compilers
#if !defined(REBOL_IMPLICIT_END)
    #define REBOL_EXPLICIT_END  // ensure core compiles with pre-C99/C++11
#endif
#include "rebol.h"

// assert() is enabled by default; disable with `#define NDEBUG`
// http://stackoverflow.com/a/17241278
//
#include <assert.h>

//=//// STANDARD DEPENDENCIES FOR CORE ////////////////////////////////////=//

#include "reb-c.h"

#if defined(CPLUSPLUS_11) && defined(DEBUG_HAS_PROBE)
    //
    // We allow you to do PROBE(some_integer) as well as PROBE(some_rebval)
    // etc. in C++11 - and the stringification comes from the << operator.
    // We must do C++ includes before defining the fail() macro, otherwise
    // the use of fail() methods in C++ won't be able to compile.
    //
    #include <sstream>
#endif

#include <stdarg.h> // va_list, va_arg()...
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <stddef.h> // for offsetof()


//
// DISABLE STDIO.H IN RELEASE BUILD
//
// The core build of Rebol published in R3-Alpha sought to not be dependent
// on <stdio.h>.  Since Rebol has richer tools like WORD!s and BLOCK! for
// dialecting, including a brittle historic string-based C "mini-language" of
// printf into the executable was a wasteful dependency.  Also, many
// implementations are clunky:
//
// http://blog.hostilefork.com/where-printf-rubber-meets-road/
//
// To formalize this rule, these definitions will help catch uses of <stdio.h>
// in the release build, and give a hopefully informative error.
//
#if defined(NDEBUG) && !defined(DEBUG_STDIO_OK)
    //
    // `stdin` is required to be macro https://en.cppreference.com/w/c/io
    //
    #if defined(__clang__)
        //
        // !!! At least as of XCode 12.0 and Clang 9.0.1, including basic
        // system headers will force the inclusion of <stdio.h>.  If someone
        // wants to dig into why that is, they may...but tolerate it for now.
        // Checking if `printf` and such makes it into the link would require
        // dumping the library symbols, in general anyway...
        //
    #elif defined(stdin) and !defined(REBOL_ALLOW_STDIO_IN_RELEASE_BUILD)
        #error "<stdio.h> included prior to %sys-core.h in release build"
    #endif

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


#include "tmp-version.h"  // historical 5 numbers in a TUPLE! (see %systems.r)

#include "sys-panic.h"  // "blue screen of death"-style termination

#include "mem-pools.h"

#include "sys-rebnod.h"

#include "sys-rebval.h"  // low level Rebol cell structure definition
#include "sys-rebser.h"  // series structure definition (embeds REBVAL)
#include "sys-rebarr.h"  // array structure definition (subclass of REBSER)
#include "sys-rebact.h"  // action structure definition (subclass of REBSER)
#include "sys-rebctx.h"  // context structure definition (subclass of REBSER)

#include "sys-state.h"
#include "sys-rebfrm.h"  // REBFRM definition (also used by value)

#include "sys-mold.h"

/***********************************************************************
**
**  Structures
**
***********************************************************************/

//-- Measurement Variables:
typedef struct rebol_stats {
    REBI64  Series_Memory;
    REBLEN  Series_Made;
    REBLEN  Series_Freed;
    REBLEN  Series_Expanded;
    REBLEN  Recycle_Counter;
    REBLEN  Recycle_Series_Total;
    REBLEN  Recycle_Series;
    REBI64  Recycle_Prior_Eval;
    REBLEN  Mark_Count;
    REBLEN  Blocks;
    REBLEN  Objects;
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

    // These flags are set during the process of spec analysis.  It helps
    // avoid the inefficiency of creating documentation frames on functions
    // that don't have any.
    //
    MKF_HAS_DESCRIPTION = 1 << 3,
    MKF_HAS_TYPES = 1 << 4,
    MKF_HAS_NOTES = 1 << 5,

    // These flags are also set during the spec analysis process.
    //
    MKF_IS_VOIDER = 1 << 6,
    MKF_HAS_RETURN = 1 << 7,

    // Gather "LET" statements, done during body relativization
    //
    MKF_GATHER_LETS = 1 << 8
};

#define MKF_MASK_NONE 0 // no special handling (e.g. MAKE ACTION!)



#define TAB_SIZE 4



#define ALL_BITS \
    ((REBLEN)(-1))


typedef int cmp_t(void *, const void *, const void *);
extern void reb_qsort_r(void *a, size_t n, size_t es, void *thunk, cmp_t *cmp);



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


#include "sys-trap.h" // includes PUSH_TRAP, fail()

#include "sys-node.h"


// Lives in %sys-bind.h, but needed for Move_Value() and Derelativize()
//
inline static void INIT_BINDING_MAY_MANAGE(RELVAL *out, REBNOD* binding);

#include "datatypes/sys-track.h"
#include "datatypes/sys-value.h"  // these defines don't need series accessors

#include "datatypes/sys-nulled.h"
#include "datatypes/sys-void.h"
#include "datatypes/sys-blank.h"
#include "datatypes/sys-logic.h"
#include "datatypes/sys-integer.h"
#include "datatypes/sys-decimal.h"

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

inline static void SET_SIGNAL(REBFLGS f) { // used in %sys-series.h
    Eval_Signals |= f;
    Eval_Count = 1;
}

#define GET_SIGNAL(f) \
    (did (Eval_Signals & (f)))

#define CLR_SIGNAL(f) \
    cast(void, Eval_Signals &= ~(f))

#include "datatypes/sys-series.h"
#include "datatypes/sys-array.h"  // REBARR used by UTF-8 string bookmarks

#include "sys-protect.h"

#include "datatypes/sys-datatype.h"

#include "datatypes/sys-binary.h"  // BIN_XXX(), etc. used by strings
#include "datatypes/sys-char.h"  // use Init_Integer() for bad codepoint error
#include "datatypes/sys-string.h"  // REBSYM needed for typesets
#include "datatypes/sys-word.h"
#include "datatypes/sys-token.h"

#include "datatypes/sys-pair.h"
#include "datatypes/sys-quoted.h"  // requires pairings for cell storage

#include "datatypes/sys-action.h"
#include "datatypes/sys-typeset.h"  // needed for keys in contexts
#include "datatypes/sys-context.h"  // needs actions for FRAME! contexts

#include "datatypes/sys-bitset.h"

#include "sys-stack.h"

#include "sys-bind.h" // needs DS_PUSH() and DS_TOP from %sys-stack.h
#include "datatypes/sys-sequence.h"  // also needs DS_PUSH()

#include "sys-roots.h"

#include "sys-throw.h"
#include "sys-feed.h"
#include "datatypes/sys-frame.h"  // needs words for frame-label helpers

#include "datatypes/sys-time.h"
#include "datatypes/sys-handle.h"
#include "datatypes/sys-map.h"
#include "datatypes/sys-varargs.h"

#include "reb-device.h"


#include "sys-eval.h"  // low-level single-step evaluation API
#include "sys-do.h"  // higher-level evaluate-until-end API

#include "sys-pick.h"


// !!! The SECURE dialect and subsystem is now in an extension.  But
// because SECURE was never fully implemented, it is really just a stub
// implementation.  The `_Placeholder` name helps keep every callsite
// from having to be documented that it's effectively a no-op.
//
inline static void Check_Security_Placeholder(
    const REBSTR *subsystem,
    enum Reb_Symbol policy,
    const REBVAL *value
){
    /* see Check_Security() in the SECURE Extension */

    UNUSED(subsystem);
    UNUSED(policy);
    UNUSED(value);
}
