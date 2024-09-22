//
//  File: %rebol-internals.h
//  Summary: "Single Complete Include File for Using the Internal Api"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// This is the main include file used in the implementation of the system.
//
// * It defines all the data types and structures used by the auto-generated
//   function prototypes.  Includes the obvious REBINT, Value*, Flex*.
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
// %rebol-internals.h is supposed to be platform-agnostic.  Any code that
// includes something like <windows.h> would be linked in as extensions.  Yet
// if a file wishes to include %rebol-internals.h and <windows.h>, it needs:
//
//     #define WIN32_LEAN_AND_MEAN  // usually desirable for leaner inclusion
//     #include <windows.h>
//
//     /* #include any non-Rebol windows dependencies here */
//
//     #undef max // same
//     #undef min // same
//     #undef OUT  // %minwindef.h defines this, we have a better use for it
//     #undef VOID  // %winnt.h defines this, we have a better use for it
//     #include "sys-core.h"
//
// !!! Because this header is included by all files in the core, it has been a
// bit of a dumping ground for flags and macros that have no particular home.
// Addressing that is an ongoing process.
//

#include "tmp-version.h"  // historical 5 numbers in a TUPLE! (see %platforms.r)
#include "reb-config.h"


//=//// INCLUDE TWEAKED ASSERT() (IMPROVED FOR DEBUGGING) /////////////////=//
//
// This needs to be done before any assert() macros get expanded, otherwise
// those expansions wouldn't get the tweaked assert.
//
#include "assert-fix.h"


//=//// INCLUDE EXTERNAL API /////////////////////////////////////////////=//
//
// Historically, Rebol source did not include the external library, because it
// was assumed the core would never want to use the less-privileged and higher
// overhead API.  However, libRebol now operates on Value* directly (though
// opaque to clients).  It has many conveniences, and is the preferred way to
// work with isolated values that need indefinite duration.
//
// 1. At present, the default behavior for rebol.h is that if you don't have
//    a definition for LIBREBOL_SPECIFIER, it will assume it's null.  Then,
//    the internals of the code use Get_Context_From_Stack() in the null
//    case for the behavior.
//

#include <stdlib.h>  // size_t and other types used in rebol.h
#include <stdint.h>
#include <stdbool.h>

/*#define LIBREBOL_SPECIFIER Get_Context_From_Stack() */  // not needed [1]
#include "rebol.h"
typedef RebolValue Value;


//=//// STANDARD DEPENDENCIES FOR CORE ////////////////////////////////////=//

#include "c-enhanced.h"

#if CPLUSPLUS_11 && DEBUG_HAS_PROBE
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
#include <math.h>
#include <stddef.h> // for offsetof()


//=//// ALLOW ONLY MINIMAL USE OF STDIO.H IN RELEASE BUILDS ////////////////=//
//
// The core build of Rebol published in R3-Alpha sought to not be dependent
// on <stdio.h>.  Since Rebol has richer tools like WORD!s and BLOCK! for
// dialecting, including a brittle historic string-based C "mini-language" of
// printf into the executable was a wasteful dependency.  Also, many
// implementations are clunky:
//
// http://blog.hostilefork.com/where-printf-rubber-meets-road/
//
// Attempts to use macro trickery to make inclusions of <stdio.h> in release
// build were used for some time.  These tricks began to run afoul of recent
// compilers that are cavalier about making the inclusion of one standard
// header mean you must want them all...so trying to avoid printf() being
// *available* was nigh impossible.
//
// Current focus on avoiding dependencies on printf() are at the object and
// linker level, where in general it's more direct to examine bloat.
//
#if !defined(NDEBUG) || DEBUG_PRINTF_FAIL_LOCATIONS || DEBUG_HAS_PROBE
    //
    // Debug builds may use printf() and such liberally (helps to debug the
    // Ren-C I/O system itself!)
    //
    #include <stdio.h>

    // NOTE: F/PRINTF DOES NOT ALWAYS FFLUSH() BUFFERS AFTER NEWLINES; it is
    // an "implementation defined" behavior, and never applies to redirects:
    //
    // https://stackoverflow.com/a/5229135/211160
    //
    // So when writing information you intend to be flushed before a potential
    // crash, be sure to fflush(), regardless of using `\n` or not.

    // The "right" way in C99 to print out things like uintptr_t is to use
    // weird type specifiers from <inttypes.h>, which looks like:
    //
    //     uintptr_t p = SOME_VALUE;
    //     printf("Here's a pointer for you: %" PRIxPTR "\n", p);
    //
    // So if a uintptr_t is being used to represent an integer, we'd use
    // `PRIuPTR`.  You get compiler warnings otherwise.
    //
    // *or you can just cast it to int and lose precision*.  But since printf()
    // is only included in debug builds, that loss of precision could wind up
    // being a problem in what's being diagnosed.  :-/  So we use it.
    //
  #if CPLUSPLUS_11
    #include <cinttypes>  // GCC requires to get macros, MSVC doesn't
  #else
    #define __STDC_FORMAT_MACROS
    #include "inttypes.h"
  #endif
#endif


// Internal configuration:
#define STACK_MIN   4000        // data stack increment size
#define STACK_LIMIT 400000      // data stack max (6.4MB)
#define MIN_COMMON 10000        // min size of common buffer
#define MAX_COMMON 100000       // max size of common buffer (shrink trigger)
#define MAX_NUM_LEN 64          // As many numeric digits we will accept on input
#define MAX_EXPAND_LIST 5       // num flexes - 1 in g_mem.prior_expand list


//=//// FORWARD-DECLARE TYPES USED IN %tmp-internals.h ////////////////////=//
//
// This does all the forward definitions that are necessary for the compiler
// to be willing to build %tmp-internals.h.  Some structures are fully defined
// and some are only forward declared.  See notes in %structs/README.md
//

#include "tmp-symid.h"  // small integer IDs for words (e.g. SYM_THRU, SYM_ON)

#include "reb-defs.h"  // basic typedefs like Byte (wraps symbol IDs as SymId)

#include "structs/struct-node.h"
#include "mem-pools.h"

#include "structs/struct-cell.h"
#include "structs/struct-value.h"

#include "structs/struct-stub.h"  // Stub definition (Flex), embeds Cell

#include "structs/struct-array.h"  // Flex subclass
#include "structs/struct-action.h"  // Array subclass
#include "structs/struct-context.h"  // Array subclass

#include "structs/struct-patch.h"

#include "structs/struct-char.h"  // Utf8(*) is Byte* in validated UTF8

#include "structs/struct-feed.h"
#include "structs/struct-state.h"  // state of variables restored on jumps
#include "structs/struct-level.h"  // C struct for running level, uses feed


#include "tmp-kinds.h"  // HeartEnum and KindEnum (REB_BLOCK, REB_TEXT, etc.)
#include "sys-kinds.h"  // defines Heart and Kind as safe wrappers if C++

#include "sys-ordered.h"  // changing the type enum *must* update these macros
#include "sys-flavor.h"  // Flex subclass byte (uses sizeof(Cell))


#include "sys-hooks.h"  // function pointer definitions


// There is a significant amount of code that wants to enumerate the parameters
// of functions or keys of a frame.  It's fairly complex logic, because the
// same frame context is viewed different ways depending on what phase is
// encoded in the FRAME! value cell.  Doing it in a callback style creates a
// lot of inconvenience for C code, needing to wrap up state...so this does
// it with an enumeration struct.

enum Reb_Var_Visibility {
    VAR_VISIBILITY_ALL,
    VAR_VISIBILITY_INPUTS,
    VAR_VISIBILITY_NONE
};

struct Reb_Enum_Vars {
    const Key* key;
    const Key* key_tail;
    Param* param;
    enum Reb_Var_Visibility visibility;
    Value* var;
    REBLEN index;  // important for enumerations that are binding

    // !!! Enumerating key/val pairs in modules in the "sea of words" model is
    // tricky, as what it really is hooks the variables in a linked list off
    // the Symbol Stub Node for the word.  This is accessed via a global
    // hash table that can expand and rearrange freely...it's not possible
    // to lock the table during enumeration.  Locking the module itself may
    // be possible, but the iteration order could get messed up by a hash
    // table resize.  There are technical ways to attack such problems that
    // are within the realm of possibility, but building an array and then
    // enumerating the array is the easiest near-term option.  This is a list
    // of the bound words.
    //
    Context* ctx;
    Array* wordlist;
    Value* word;
    Value* word_tail;
    const Symbol* keybuf;  // backing store for key
};

typedef struct Reb_Enum_Vars EVARS;


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


/***********************************************************************
**
**  Structures
**
***********************************************************************/

typedef struct {
    Pool* pools;  // memory pool array
    Byte* pools_by_size;  // map for speedup during allocation (made on boot)

  #if DEBUG_ENABLE_ALWAYS_MALLOC
    bool always_malloc;   // For memory-related troubleshooting
  #endif

    Flex** prior_expand;  // Track prior Flex expansions (acceleration)

    uintptr_t usage;  // Overall memory used
    Option(uintptr_t) usage_limit;  // Memory limit set by SECURE

  #if !defined(NDEBUG)  // Used by the FUZZ native to inject alloc failures
    intptr_t fuzz_factor;  // (-) => a countdown, (+) percent of 10000
  #endif

  #if DEBUG_MONITOR_FLEX
    const Node* monitor_node;
  #endif

  #if DEBUG
    bool watch_expand;
  #endif

  #if DEBUG
    intptr_t num_black_flex;
  #endif

  #if DEBUG_COLLECT_STATS
    Size flex_memory;
    Count num_flex_made;
    Count num_flex_freed;
    Count num_flex_expanded;
    Count blocks_made;
    Count objects_made;
  #endif
} MemoryState;

typedef struct {
    Symbol builtin_canons[ALL_SYMS_MAX + 1];

    Flex* by_hash;  // Symbol* pointers indexed by hash
    REBLEN num_slots_in_use;  // Total symbol hash slots (+deleteds)
  #if !defined(NDEBUG)
    REBLEN num_deleteds;  // Deleted symbol hash slots "in use"
  #endif
    Symbol deleted_symbol;  // pointer used to indicate a deletion
} SymbolState;

typedef struct {
    bool recycling;  // True when the GC is in a recycle
    intptr_t depletion;  // bytes left to allocate until automatic GC is forced
    intptr_t ballast;  // what depletion is reset to after a GC
    bool disabled;  // true when RECYCLE/OFF is run
    Flex* guarded;  // stack of GC protected Flexes and (maybe erased) Cells
    Flex* mark_stack;  // Flexes pending to mark their reachables as live
    Flex* manuals;  // Manually memory managed (not by GC)

  #if DEBUG
    intptr_t mark_count;  // Count of stubs with NODE_FLAG_MARKED, must balance
  #endif

  #if DEBUG
    bool watch_recycle;
  #endif

  #if DEBUG_COLLECT_STATS
    REBLEN recycle_counter;
    REBLEN recycled_stubs_total;
    REBLEN recycled_stubs;
  #endif
} GarbageCollectorState;


typedef struct {
    Array* array;
    StackIndex index;
    Cell* movable_top;
    const Cell* movable_tail;

  #if DEBUG_EXTANT_STACK_POINTERS
    Count num_refs_extant;  // # of Data_Stack_At()/TOP refs extant
  #endif
} DataStackState;

typedef struct {
    Level* top_level;
    Level* bottom_level;

    Jump* jump_list;  // Saved state for RESCUE_SCOPE

    Atom thrown_arg;
    Value thrown_label;
    Level* unwind_level;

    Flags signal_flags;  // signal flags (Rebol signals, not unix ones!)
    Flags signal_mask;  // masking out signal flags
    int_fast32_t eval_countdown;  // evaluation counter until Do_Signals()
    int_fast32_t eval_dose;  // evaluation counter reset value
    Tick total_eval_cycles;  // total evals, unsigned overflow well defined
    Option(Tick) eval_cycles_limit;  // evaluation limit (set by secure)
} TrampolineState;

typedef struct {
    Flex* stack;  // tracked to prevent infinite loop in cyclical molds

    String* buffer;  // temporary UTF8 buffer

  #if DEBUG
    bool currently_pushing;  // Push_Mold() should not directly recurse
  #endif
} MoldState;



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

#include "sys-globals.h"  // includes things like TG_tick, used by panic()


#include "sys-panic.h"  // "blue screen of death"-style termination

#include "sys-mold.h"



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
    MKF_RETURN      = 1 << 0,   // built-in FUNC-style RETURN (vs LAMBDA)
    MKF_1           = 1 << 1,
    MKF_PARAMETER_SEEN = 1 << 2,  // text will be description until this

    // These flags are set during the process of spec analysis.  It helps
    // avoid the inefficiency of creating documentation frames on functions
    // that don't have any.
    //
    MKF_HAS_DESCRIPTION = 1 << 3,

    // These flags are also set during the spec analysis process.
    //
    MKF_HAS_RETURN = 1 << 6
};

#define MKF_MASK_NONE 0 // no special handling



#define TAB_SIZE 4



#define ALL_BITS \
    ((REBLEN)(-1))


typedef int cmp_t(void *, const void *, const void *);
extern void reb_qsort_r(void *a, size_t n, size_t es, void *thunk, cmp_t *cmp);



#include "tmp-constants.h"

#include "tmp-boot.h"
#include "tmp-sysobj.h"


#include "tmp-error-funcs.h" // functions below are called


#include "sys-trap.h" // includes RESCUE_SCOPE, fail()

#include "sys-node.h"



//=//// TRAMPOLINE_FLAG_RECYCLE ///////////////////////////////////////////=//
//
// The recycle flag indicates a need to run the garbage collector, when
// running it synchronously could be dangerous.  This is important e.g. during
// memory allocation, which can detect crossing a memory usage boundary that
// suggests GC'ing would be good...but might be in the middle of code that is
// halfway through manipulating a managed Flex.  Recycling does not happen
// until the trampoline regains control.
//
#define TRAMPOLINE_FLAG_RECYCLE \
    FLAG_LEFT_BIT(0)


//=//// TRAMPOLINE_FLAG_HALT //////////////////////////////////////////////=//
//
// The halt flag requests returning to the topmost level of the evaluator,
// regardless of how deep a debug stack might be.  The trampoline will only
// react to it when the top level doesn't have LEVEL_FLAG_UNINTERRUPTIBLE set.
// Clients of the API can choose to react to interruption outside of the
// evaluator by asking for rebWasHaltRequested().
//
#define TRAMPOLINE_FLAG_HALT \
    FLAG_LEFT_BIT(1)


//=//// TRAMPOLINE_FLAG_DEBUG_BREAK ///////////////////////////////////////=//
//
// The Debug Break request indicates a desire to enter an interactive
// debugging state.  Because the ability to manage such a state may not be
// registered by the host, this could generate an error.
//
#define TRAMPOLINE_FLAG_DEBUG_BREAK \
    FLAG_LEFT_BIT(2)


INLINE void Set_Trampoline_Flag_Core(Flags f) { // used in %sys-series.h
    g_ts.signal_flags |= f;

    if (g_ts.eval_countdown == -1)  // already set to trigger on next tick...
        return;  // ...we already reconciled the dose

    assert(g_ts.eval_countdown > 0);  // transition to 0 triggers signals

    // This forces the next step in the evaluator to count down to 0 and
    // trigger an interrupt.  But we have to reconcile the count first.
    //
    uintptr_t delta = g_ts.eval_dose - g_ts.eval_countdown;
    if (UINTPTR_MAX - g_ts.total_eval_cycles > delta)
        g_ts.total_eval_cycles += delta;
    else
        g_ts.total_eval_cycles = UINTPTR_MAX;

  #if DEBUG_COUNT_TICKS
    assert(g_ts.total_eval_cycles == TG_tick);
  #endif

    g_ts.eval_countdown = -1;
}

#define Set_Trampoline_Flag(name) \
    Set_Trampoline_Flag_Core(TRAMPOLINE_FLAG_##name)

#define Get_Trampoline_Flag(name) \
    (did (g_ts.signal_flags & TRAMPOLINE_FLAG_##name))

#define Clear_Trampoline_Flag(name) \
    g_ts.signal_flags &= (~ TRAMPOLINE_FLAG_##name)


//=//// DEBUG HOOKS INTO THE CAST() OPERATOR //////////////////////////////=//
//
// In the C++ build, there is the opportunity to hook any cast() operation
// with code that can do checking or validation.  See comments in file.
//
#if DEBUG_CHECK_CASTS
    #include "sys-debug-casts.hpp"
#endif


//=//// STUB-DERIVED STRUCTURE ACCESSORS //////////////////////////////////=//

#include "sys-track.h"

#if DEBUG_HAS_PROBE
    #include "sys-probe.h"  // VERY USEFUL!  See file for details.
#endif

#include "sys-cell.h"

#include "stubs/stub-series.h"  // needs Is_Cell_Poisoned(), Erase_Cell()

#include "sys-gc.h"

#include "stubs/stub-array.h"  // Array* used by UTF-8 string bookmarks
#include "stubs/stub-symbol.h"
#include "stubs/stub-binary.h"  // Binary_At(), etc. used by strings
#include "sys-utf8.h"
#include "stubs/stub-string.h"  // SymId needed for typesets
#include "stubs/stub-action.h"
#include "stubs/stub-context.h"  // needs actions for FRAME! contexts


//=//// GENERAL CELL SERVICES THAT NEED SERIES DEFINED ////////////////////=//

#include "sys-stack.h"
#include "sys-protect.h"


//=//// API HANDLES ///////////////////////////////////////////////////////=//

#include "sys-roots.h"


//=//// CELL ACCESSOR FUNCTIONS ///////////////////////////////////////////=//

#include "cells/cell-quoted.h"  // has special handling for voids/nones

#include "cells/cell-blank.h"
#include "cells/cell-integer.h"
#include "cells/cell-decimal.h"
#include "cells/cell-time.h"
#include "cells/cell-bitset.h"
#include "cells/cell-handle.h"

#include "cells/cell-pair.h"

#include "cells/cell-series.h"
#include "cells/cell-array.h"

#include "cells/cell-comma.h"  // Is_Elision() references nihil block antiform


#include "cells/cell-word.h"  // needs to know about QUOTED! for binding
#include "cells/cell-unreadable.h"  // requires WORD! for `unreadable`
#include "cells/cell-void.h"  // uses pack array for Init_Heavy_Void()
#include "cells/cell-nulled.h"  // ~null~ is an antiform word
#include "cells/cell-logic.h"  // TRUE, FALSE, YES, NO, ON, OFF are words

#include "cells/cell-string.h"
#include "cells/cell-binary.h"
#include "cells/cell-token.h"

#include "cells/cell-sigil.h"  // requires quoted/quasi and char

#include "cells/cell-context.h"
#include "cells/cell-frame.h"
#include "cells/cell-error.h"

#include "cells/cell-map.h"
#include "cells/cell-varargs.h"

#include "cells/cell-parameter.h"

#include "sys-patch.h"
#include "sys-bind.h"

#include "cells/cell-datatype.h"  // needs Derelativize()

#include "cells/cell-sequence.h"  // needs Derelativize()

#include "sys-isotope.h"  // needs cell definitions


//=//// EVALUATOR SERVICES ////////////////////////////////////////////////=//

#include "sys-throw.h"
#include "sys-feed.h"
#include "sys-level.h"  // needs words for frame-label helpers

#include "sys-eval.h"  // low-level single-step evaluation API
#include "sys-bounce.h"

#include "sys-pick.h"
