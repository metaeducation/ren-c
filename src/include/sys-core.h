//
//  file: %sys-core.h
//  summary: "Single Complete Include File for Using the Internal Api"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// %sys-core.h is supposed to be platform-agnostic.  Any code that
// includes something like <windows.h> would be linked in as extensions.  Yet
// if a file wishes to include %sys-core.h and <windows.h>, it needs:
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


#if defined(REBOL_CORE_API_INCLUDED)
    #error "sys-core.h included more than once"
    #include <stophere>  // https://stackoverflow.com/a/45661130
#endif

#define REBOL_CORE_API_INCLUDED


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
//    a definition for LIBREBOL_BINDING_NAME, it will assume the user
//    context.  This hasn't really been fleshed out yet.  In any case, we
//    want the core to assume the lib context, in particular because we
//    need to be able to run API functions before the user context has
//    been completely formed.
//

#include <stdlib.h>  // size_t and other types used in rebol.h
#include <stdint.h>
#include <stdbool.h>

#if ALLOW_SPORADICALLY_NON_DETERMINISTIC
  #include <time.h>  // needed for srand()
#endif

#define LIBREBOL_BINDING_NAME()  librebol_binding  // [1]
#include "rebol.h"

typedef RebolValue Value;
typedef RebolHandleCleaner HandleCleaner;


//=//// STANDARD DEPENDENCIES FOR CORE ////////////////////////////////////=//

#if CPLUSPLUS_11 && DEBUG_HAS_PROBE
    //
    // We allow you to do PROBE(some_integer) as well as PROBE(some_rebval)
    // etc. in C++11 - and the stringification comes from the << operator.
    // We must do C++ includes before defining the panic() macro, otherwise
    // the use of panic() methods in C++ won't be able to compile.
    //
    #include <sstream>
#endif

#include <stdarg.h> // va_list, va_arg()...
#include <string.h>
#include <math.h>
#include <stddef.h> // for offsetof()
#include <inttypes.h>  // printf() abstractions e.g. for uint64_t (PRIu64)

#include "bsd-qsort_r.h"  // qsort_r() varies by platform, bundle BSD version


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
#if RUNTIME_CHECKS || DEBUG_PRINTF_PANIC_LOCATIONS || DEBUG_HAS_PROBE
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
    // is only included in checked builds, that loss of precision could wind up
    // being a problem in what's being diagnosed.  :-/  So we use it.
    //
  #if CPLUSPLUS_11
    #include <cinttypes>  // GCC requires to get macros, MSVC doesn't
  #else
    #define __STDC_FORMAT_MACROS
    #include "inttypes.h"
  #endif
#endif


//=//// HELPERS GIVING ENHANCEMENTS TO C IF BUILT AS C++ //////////////////=//
//
// Don't include Needful until *after* standard includes, in case there
// is any contention on naming in the compiler header files (for instance,
// Init() is used in MSVC's xiosbase, and so if you define a macro with
// that name it will break the compilation).  Generally speaking you should
// define your macros after including system headers, not before...
//

#include "needful/needful.h"

#if CPLUSPLUS_11
    using needful::EnableIfSame;

    using needful::PermissiveZeroStruct;
    using needful::ResultWrapper;

  #if NEEDFUL_OPTION_USES_WRAPPER
    using needful::OptionWrapper;
    using needful::UnwrapHelper;
    using needful::MaybeHelper;
  #endif

  #if NEEDFUL_SINK_USES_WRAPPER
    using needful::SinkWrapper;
    using needful::InitWrapper;
    using needful::NeedWrapper;
  #endif
#endif


//=//// SOME EXTRA C THINGS ///////////////////////////////////////////////=//
//
// e.g. EXTERN_C, PP_CONCAT, etc.

#include "c-extras.h"

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

#include "enums/enum-symid.h"  // 16-bit IDs for words (e.g. SYM_THRU, SYM_ON)

#include "reb-defs.h"  // basic typedefs like Byte (wraps symbol IDs as SymId)

#include "sys-flags.h"  // byte-order sensitive macros, used by node
#include "structs/struct-base.h"

#include "mem-pools.h"

#include "structs/struct-cell.h"
#include "enums/enum-types.h"  // defines Heart and Type (safe wrappers if C++)

#include "structs/struct-value.h"

#include "structs/struct-stub.h"  // Stub definition, embeds Cell
#include "structs/struct-flex.h"  // A Flex's identity is its Stub

#include "structs/struct-binary.h"

#include "structs/struct-char.h"  // Utf8(*) is Byte* in validated UTF8
#include "structs/struct-string.h"

#include "structs/struct-pairing.h"  // Stub-sized (2 cells), but not a Stub

#include "structs/struct-array.h"  // Flex subclass
#include "structs/struct-source.h"  // array subclass used by BLOCK!, etc.

#include "structs/struct-context.h"  // Context superclass
#include "structs/struct-varlist.h"
#include "structs/struct-sea.h"  // !!! currently a subclass of VarList

#include "structs/struct-feed.h"
#include "structs/struct-state.h"  // state of variables restored on jumps
#include "structs/struct-bounce.h"  // return value from native dispatchers
#include "structs/struct-level.h"  // C struct for running level, uses feed

#include "structs/struct-details.h"  // Array subclass
#include "structs/struct-map.h"  // Array subclass (PairList)

#include "structs/struct-patch.h"

#include "structs/struct-mold.h"

#include "enums/enum-typesets.h"  // built-in order dependent type checks

#include "enums/enum-flavor.h"  // Flex subclass byte (uses sizeof(Cell))

#include "sys-hooks.h"  // function pointer definitions


// There is a significant amount of code that wants to enumerate the parameters
// of functions or keys of a frame.  It's fairly complex logic, because the
// same frame context is viewed different ways depending on what "Lens" is
// encoded in the FRAME! value cell.  Doing it in a callback style creates a
// lot of inconvenience for C code, needing to wrap up state...so this does
// it with an enumeration struct.

enum LensModeEnum {
    LENS_MODE_INPUTS,
    LENS_MODE_PARTIALS,
    LENS_MODE_ALL_UNSEALED
    // Note: viewing ALL including sealed could expose duplicate keys, illegal!
};
typedef enum LensModeEnum LensMode;

struct Reb_Enum_Vars {
    const Key* key;
    const Key* key_tail;
    Param* param;
    LensMode lens_mode;
    Slot* slot;
    REBLEN index;  // important for enumerations that are binding

    // !!! Enumerating key/val pairs in modules in the "sea of words" model is
    // tricky, as what it really is hooks the variables in a linked list off
    // the Symbol Stub Base for the word.  This is accessed via a global
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
    Element* word;
    Element* word_tail;
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

  #if TRAMPOLINE_COUNTS_TICKS  // Used by the FUZZ native to inject alloc failures
    intptr_t fuzz_factor;  // (-) => a countdown, (+) percent of 10000
  #endif

  #if DEBUG_MONITOR_FLEX
    Option(const Base*) monitoring;
  #endif

  #if RUNTIME_CHECKS
    bool watch_expand;
  #endif

  #if RUNTIME_CHECKS
    intptr_t num_black_stubs;
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
    Symbol builtin_canons[MAX_SYM_BUILTIN + 1];

    Flex* by_hash;  // Symbol* pointers indexed by hash
    REBLEN num_slots_in_use;  // Total symbol hash slots (+deleteds)
  #if RUNTIME_CHECKS
    REBLEN num_deleteds;  // Deleted symbol hash slots "in use"
  #endif
    Symbol deleted_symbol;  // pointer used to indicate a deletion
} SymbolState;

typedef struct {
    bool recycling;  // True when the GC is in a recycle
    intptr_t depletion;  // bytes left to allocate until automatic GC is forced
    intptr_t ballast;  // what depletion is reset to after a GC
    bool disabled;  // true when RECYCLE:OFF is run
    Flex* guarded;  // stack of GC protected Flexes and (maybe erased) Cells
    Flex* mark_stack;  // Flexes pending to mark their reachables as live
    Flex* manuals;  // Manually memory managed (not by GC)

  #if RUNTIME_CHECKS
    intptr_t mark_count;  // Count of stubs with BASE_FLAG_MARKED, must balance
  #endif

  #if RUNTIME_CHECKS
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
    Value* movable_top;
    const Value* movable_tail;

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

    Strand* buffer;  // temporary UTF8 buffer

  #if RUNTIME_CHECKS
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

#include "sys-globals.h"  // includes things like g_tick, used by crash()

#include "sys-crash.h"  // "blue screen of death"-style termination


/***********************************************************************
**
**  Constants
**
***********************************************************************/

enum Boot_Phases {
    BOOT_START_0 = 0,
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
    MKF_DONT_POP_RETURN = 1 << 1,  // leave RETURN parameter on stack (natives)

    // These flags are set during the process of spec analysis.  It helps
    // avoid the inefficiency of creating documentation frames on functions
    // that don't have any.
    //
    MKF_PARAMETER_SEEN = 1 << 2  // text will be description until this
};

#define MKF_MASK_NONE 0 // no special handling



#define TAB_SIZE 4



#define ALL_BITS \
    ((REBLEN)(-1))


#include "tmp-constants.h"

#include "tmp-boot.h"
#include "tmp-sysobj.h"

#define Make_Error_Managed(cat_id, id, ...) \
    Make_Error_Managed_Raw( \
        u_cast(int, ensure(Option(SymId), cat_id)), \
        u_cast(int, ensure(Option(SymId), id)), \
        __VA_ARGS__ \
    )

#include "tmp-error-funcs.h" // functions below are called

#include "sys-tick.h"

#include "sys-rescue.h" // includes RESCUE_SCOPE, panic_abruptly()

#include "sys-base.h"

#include "sys-trampoline.h"



//=//// STUB-DERIVED STRUCTURE ACCESSORS //////////////////////////////////=//

#include "sys-track.h"

#if DEBUG_HAS_PROBE
    #include "sys-probe.h"  // VERY USEFUL!  See file for details.
#endif

#include "sys-cell.h"
#include "cells/cell-quoted.h"  // defines Is_Cell_Stable(), used by API stubs

#include "sys-stub.h"


//=//// INSTRUMENTATION HOOKS INTO THE CAST() OPERATOR ////////////////////=//
//
// In the C++ build, there is the opportunity to hook any cast() operation
// with code that can do checking or validation.  See comments in file.
//
// We do this after the %sys-cell.h and %sys-stub.h files, because they have
// fundamental definitions that are important for the casts.
//
#if DEBUG_CHECK_CASTS
  namespace needful {  // so things like CastHook<> specialization easier
    #include "casts/cast-base.hpp"
    #include "casts/cast-stubs.hpp"
    #include "casts/cast-cells.hpp"
    #include "casts/cast-misc.hpp"
  }
#endif

#include "sys-mold.h"

#include "stubs/stub-flex.h"  // needs Is_Cell_Poisoned(), Erase_Cell()

#include "sys-gc.h"

#include "stubs/stub-array.h"  // Array* used by UTF-8 string bookmarks
#include "stubs/stub-source.h"  // subclass of Array*
#include "stubs/stub-symbol.h"
#include "stubs/stub-binary.h"  // Binary_At(), etc. used by strings
#include "sys-utf8.h"

#include "stubs/stub-strand.h"  // SymId needed for typesets

#include "stubs/stub-context.h"  // needs actions for FRAME! contexts
#include "stubs/stub-sea.h"
#include "stubs/stub-action.h"  // needed by stub-varlist ATM
#include "stubs/stub-varlist.h"

#include "stubs/stub-map.h"



//=//// GENERAL CELL SERVICES THAT NEED SERIES DEFINED ////////////////////=//

#include "sys-protect.h"


//=//// API HANDLES ///////////////////////////////////////////////////////=//

#include "stubs/stub-api.h"  // requires Is_Cell_Stable() to be defined


//=//// CELL ACCESSOR FUNCTIONS ///////////////////////////////////////////=//

#include "sys-datastack.h"

#include "cells/cell-integer.h"
#include "cells/cell-decimal.h"
#include "cells/cell-time.h"
#include "cells/cell-bitset.h"
#include "cells/cell-handle.h"

#include "cells/cell-pair.h"

#include "cells/cell-series.h"
#include "cells/cell-array.h"

#include "cells/cell-comma.h"  // Is_Ghost_Or_Void() references nihil block antiform


#include "cells/cell-word.h"  // needs to know about QUOTED! for binding
#include "cells/cell-void.h"
#include "cells/cell-nulled.h"  // ~null~ is an antiform word
#include "cells/cell-logic.h"  // TRUE, FALSE, YES, NO, ON, OFF are words

#include "cells/cell-string.h"
#include "cells/cell-binary.h"

#include "cells/cell-context.h"
#include "cells/cell-frame.h"
#include "cells/cell-error.h"

#include "cells/cell-sigil.h"
#include "cells/cell-rune.h"

#include "cells/cell-map.h"
#include "cells/cell-varargs.h"

#include "cells/cell-parameter.h"

#include "stubs/stub-use.h"
#include "sys-bind.h"

#include "cells/cell-datatype.h"  // needs Derelativize()

#include "cells/cell-sequence.h"  // needs Derelativize()

//=//// EVALUATOR SERVICES ////////////////////////////////////////////////=//

#include "sys-lib.h"

#include "sys-bounce.h"
#include "sys-throw.h"
#include "sys-feed.h"
#include "sys-level.h"  // needs words for frame-label helpers
#include "sys-intrinsic.h"  // helpers because intrinsics don't process args

#include "sys-eval.h"  // low-level single-step evaluation API
#include "sys-continuation.h"


//=//// ISOTOPE COERCION AND DECAY ////////////////////////////////////////=//

#include "sys-isotope.h"  // needs cell definitions, Drop_Level()


//=//// NATIVES ////////////////////////////////////////////////////////////=//
//
// The core has a different definition of DECLARE_NATIVE() than extensions.
// Extensions have to include the module name in the function name, in case
// they are linked directly into the executable--so their linknames aren't
// ambiguous with core natives (or other extension natives) of the same name.
//
// 1. The `name` argument is taken as uppercase.  This helps use token pasting
//    to get the functions SYM_XXX name via SYM_##name.
//
// 2. Because there are macros for things like `maybe`, trying to reuse the
//    NATIVE_CFUNC() macro inside DECLARE_NATIVE() would expand maybe before
//    passing it to the token paste.  It's easiest just to repeat `N_##name`
//
// 3. Forward definitions of DECLARE_NATIVE() for all the core natives.  This
//    means functions are available via NATIVE_CFUNC() throughout the core code
//    if it wants to explicitly reference a native's dispatcher function.
//
// 4. %tmp-paramlists.h is the file containing macros for natives and actions
//    that map their argument names to indices in the frame.  This defines the
//    macros like INCLUDE_ARGS_OF_INSERT which then allow you to naturally
//    write things like Bool_ARG(PART) and ARG(LIMIT), instead of the brittle
//    integer-based system used in R3-Alpha such as D_REF(7) and ARG_N(3).

#define NATIVE_CFUNC(name)  N_##name  // e.g. NATIVE_CFUNC(FOO) => N_FOO [1]

#define DECLARE_NATIVE(name) \
    Bounce N_##name(Level* level_)  // NATIVE_CFUNC(macro) would expand [2]

#include "tmp-native-fwd-decls.h"  // forward declarations of natives [3]

#include "tmp-paramlists.h"  // INCLUDE_ARGS_OF_XXX macro definitions [4]


//=//// GENERICS ///////////////////////////////////////////////////////////=//
//
// Historical Rebol mapped each datatype to a function which had a switch()
// statement with cases representing every generic function that type could
// handle.  It was possible to write code that was shared among all the
// generics at the top before the switch() or at the bottom after it, and goto
// could be used to jump between the handlers.
//
// Ren-C uses a more granular approach, where each generic's entry point is
// very much like a native.  This makes it possible to write common code that
// runs before or after the moment of dispatch, implementing invariants that
// are specific to each generic.  Then implementations are more granular,
// associating an implementation with a TypesetByte in tables that are
// assembled during the build preparation.
//
// 1. At the moment, extensions are not allowed to define generics.  That
//    would complicate the table generation, but such complications would
//    be necessary if user types were going to handle the generic.
//
// 2. See DECLARE_NATIVE() notes for why G_##name##_##type is repeated here.
//
// 3. Forward definitions of IMPLEMENT_GENERIC() for all the generics.
//
//    The name should be all caps, and the type propercase, e.g.:
//
//        IMPLEMENT_GENERIC(MULTIPLY, Is_Integer)
//        IMPLEMENT_GENERIC(APPEND, Any_List)
//
//    It's done this way to easily generate SYM_APPEND through token pasting,
//    and the type is propercase so it looks like a function Any_List()
//    instead of a variable (any_list).

#define GENERIC_CFUNC(name,type)  G_##name##_##type  // no extension form [1]

#define GENERIC_TABLE(name)  g_generic_##name  // name is all caps

#define IMPLEMENT_GENERIC(name,type) \
    Bounce G_##name##_##type(Level* level_)  // doesn't use GENERIC_CFUNC() [2]

#include "tmp-generic-fwd-decls.h"  // forward generic handler definitions [3]

#include "sys-pick.h"


//=//// UNDEFINE MACROS IF USER DIDN'T WANT THEM //////////////////////////=//
//
// It would be too burdensome to not be able to write inline code using the
// macros, but some %sys-core.h clients may not want these defined.

#define REBOL_EXCEPTION_SHORTHAND_MACROS  1  // WIP: investigate problem

#if (! REBOL_EXCEPTION_SHORTHAND_MACROS)
    #undef fail
    #undef panic

    #undef trap
    #undef require
    #undef guarantee
    #undef except

    #undef trapped
    #undef required
    #undef guaranteed
    #undef excepted
#endif
