//
//  file: %reb-config.h
//  summary: "General build configuration"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// This is the first file included by %sys-core.h.
//
// Many of the flags controlling the build (such as
// the TO_<target> definitions) come from -DTO_<target> in the
// compiler command-line.  These command lines are generally
// produced automatically, based on the build that is picked
// from %platforms.r.
//
// However, some flags require the preprocessor's help to
// decide if they are relevant, for instance if they involve
// detecting features of the compiler while it's running.
// Or they may adjust a feature so narrowly that putting it
// into the system configuration would seem unnecessary.
//
// Over time, this file should be balanced and adjusted with
// %platforms.r in order to make the most convenient and clear
// build process.  If there is difficulty in making a build
// work on a system, use that as an opportunity to reflect
// how to make this better.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * This favors #if with defined values of 0 and 1, instead of #ifdef, because
//   over the long run it makes typos easier to spot.  It also offers more
//   control over defaults.  For good arguments supporting this choice, see:
//
//     https://www.iar.com/knowledge/learn/programming/advanced-preprocessor-tips-and-tricks/
//


#ifndef REB_CONFIG_H_1020_0304  // "include guard" allows multiple #includes
#define REB_CONFIG_H_1020_0304  // #s in case REB_CONFIG_H defined elsewhere

//=//// [REDUNDANT] CPLUSPLUS_11 PREPROCESSOR DEFINE ///////////////////////=//
//
// There is a Catch-22 in defining CPLUSPLUS_11 from %needful.h, because it
// currently depends on TO_WINDOWS and %reb-config.h depends on CPLUSPLUS_11.
// For the moment, sort that out by defining the macro here...but we might
// just say it's the responsibility of whoever's doing the compiling to supply
// it on the command line, as you have to for MSVC anyway.
//
// (See notes on definition in %needful/needful.h)

#if !defined(CPLUSPLUS_11)
  #if defined(__cplusplus) && __cplusplus >= 201103L
    #define CPLUSPLUS_11  1
  #else
    #define CPLUSPLUS_11  0
  #endif
#endif
#if !defined(NO_CPLUSPLUS_11)
    #define NO_CPLUSPLUS_11  (! CPLUSPLUS_11)
#endif


//=//// [REDUNDANT] STATIC_ASSERT PREPROCESSOR DEFINE /////////////////////=//
//
// It's inconvenient to not have STATIC_ASSERT in this file, but we want it
// in %needful-asserts.h as well.  Allow the redundant definition (Needful
// checks to see if it's already defined).
//
// Note: STATIC_ASSERT((std::is_same<T, U>::value)) is a common pattern,
// and needs double parentheses for the < and > to work with the macro.

#if CPLUSPLUS_11
    #define STATIC_ASSERT(cond) \
        static_assert((cond), #cond) // callsite has semicolon, see C trick
#else
    #define STATIC_ASSERT(cond) \
        struct GlobalScopeNoopTrick // https://stackoverflow.com/q/53923706
#endif


/** Primary Configuration **********************************************

The primary target system is defined by:

    TO_(os-base)    - for example TO_WINDOWS or TO_LINUX
    TO_(os-name)    - for example TO_WINDOWS_X86 or TO_LINUX_X64

The default config builds an R3 HOST executable program.

To change the config, host-kit developers can define:

    REB_EXT         - build an extension module
                      * create a DLL, not a host executable
                      * do not export a host lib (OS_ lib)
                      * call r3lib via struct and macros

    REB_CORE        - build /core only, no graphics, windows, etc.

Special internal defines used by RT, not Host-Kit developers:

    REB_API         - build r3lib as API
                      * export r3lib functions
                      * build r3lib dispatch table
                      * call host lib (OS_) via struct and macros
*/

//* Common *************************************************************

#if !defined(LIBREBOL_USES_API_TABLE)
    #define LIBREBOL_USES_API_TABLE  0  // !!! librebol has this same define
#endif

#if LIBREBOL_USES_API_TABLE
    // Building extensions as external libraries (.dll, .so etc.)
    // or r3 host against r3lib dll
    #define RL_API API_IMPORT
#else
    // Extensions are builtin
    #define RL_API
#endif


//=//// WINDOWS ////////////////////////////////////////////////////////////=//

#if !defined(TO_WINDOWS_X86)
    #define TO_WINDOWS_X86 0
#endif

#if !defined(TO_WINDOWS_X64)
    #define TO_WINDOWS_X64 0
#endif

#if !defined(TO_WINDOWS)
    #define TO_WINDOWS 0
#endif

#if TO_WINDOWS
    // ASCII strings to Integer
    #define ATOI                    // supports it
    #define ATOI64                  // supports it
    #define ITOA64                  // supports it

    // Used when we build REBOL as a DLL:
    #define API_EXPORT __declspec(dllexport)
    #define API_IMPORT __declspec(dllimport)

#else
    #define API_IMPORT
    // Note: Unsupported by gcc 2.95.3-haiku-121101
    // (We #undef it in the Haiku section)
    #define API_EXPORT __attribute__((visibility("default")))
#endif


// Windows headers define the macros IN and OUT as part of an interface
// definition language.  Ren-C core uses the OUT macro is used as a shorthand
// for accessing `level_->out` in a native.  You can #undef the Windows
// macros after you #include <windows.h>, but having the definitions under
// a switch gives more flexibility to define your own macros and leave the
// Windows ones alone.
//
#if !defined(REBOL_LEVEL_SHORTHAND_MACROS)
    #define REBOL_LEVEL_SHORTHAND_MACROS 1
#endif


//=//// LINUX //////////////////////////////////////////////////////////////=//

#if !defined(TO_LINUX_X86)
    #define TO_LINUX_X86 0
#endif

#if !defined(TO_LINUX_X64)
    #define TO_LINUX_X64 0
#endif

#if !defined(TO_LINUX_PPC)
    #define TO_LINUX_PPC 0
#endif

#if !defined(TO_LINUX_ARM)
    #define TO_LINUX_ARM 0
#endif

#if !defined(TO_LINUX_AARCH64)
    #define TO_LINUX_AARCH64 0
#endif

#if !defined(TO_LINUX_MIPS)
    #define TO_LINUX_MIPS 0
#endif

#if !defined(TO_LINUX)
    #define TO_LINUX 0
#else
    #define PROC_EXEC_PATH "/proc/self/exe"
#endif

// !!! The Atronix build introduced a differentiation between a Linux build
// and a POSIX build, and one difference is the usage of some signal functions
// that are not available if you compile with a strict --std=c99 switch:
//
//      http://stackoverflow.com/a/22913324/211160
//
// Yet it appears that defining _POSIX_C_SOURCE is good enough to get it
// working in --std=gnu99.  Because there are some other barriers to pure C99
// for the moment in the additions from Saphirion (such as the use of
// alloca()), backing off the pure C99 and doing it this way for now.
//
// These files may not include reb-config.h as the first include, so be sure
// to say:
//
//     #define _POSIX_C_SOURCE 199309L
//
// ...at the top of the file.
//
#if !defined(HAS_POSIX_SIGNAL)
    #define HAS_POSIX_SIGNAL  TO_LINUX
#endif


//=//// APPLE //////////////////////////////////////////////////////////////=//

#if !defined(TO_OSX_PPC)
    #define TO_OSX_PPC 0
#endif

#if !defined(TO_OSX_X86)
    #define TO_OSX_X86 0
#endif

#if !defined(TO_OSX_X64)
    #define TO_OSX_X64 0
#endif

#if !defined(TO_OSX)
    #define TO_OSX 0
#endif


//=//// ANDROID ////////////////////////////////////////////////////////////=//

#if !defined(TO_ANDROID_ARM)
    #define TO_ANDROID_ARM 0
#endif

#if !defined(TO_ANDROID)
    #define TO_ANDROID 0
#elif TO_ANDROID
    #define PROC_EXEC_PATH "/proc/self/exe"
#endif


//=//// BSD ////////////////////////////////////////////////////////////////=//

#if !defined(TO_FREEBSD_X86)
    #define TO_FREEBSD_X86 0
#endif

#if !defined(TO_FREEBSD_X64)
    #define TO_FREEBSD_X64 0
#endif

#if !defined(TO_FREEBSD)
    #define TO_FREEBSD 0
#elif TO_FREEBSD
    #define HAVE_PROC_PATHNAME
#endif

#if !defined(TO_NETBSD)
    #define TO_NETBSD 0
#elif TO_NETBSD
    #define PROC_EXEC_PATH "/proc/curproc/exe"
#endif

#if !defined(TO_OPENBSD)
    #define TO_OPENBSD 0
#endif

#if !defined(TO_OPENBSD_X64)
    #define TO_OPENBSD_X64 0
#endif

#if !defined(TO_POSIX)
    #define TO_POSIX 0
#endif


//=//// HAIKU OS ///////////////////////////////////////////////////////////=//

#if !defined(TO_HAIKU)
    #define TO_HAIKU 0
#elif TO_HAIKU
    #undef API_EXPORT
    #define API_EXPORT

    #define DEF_UINT
#endif

#if !defined(TO_HAIKU_X64)
    #define TO_HAIKU_X64 0
#endif


//=//// EMSCRIPTEN and WASI ////////////////////////////////////////////////=//

#if !defined(TO_EMSCRIPTEN)
    #define TO_EMSCRIPTEN 0
#endif

#if !defined(TO_WASI)
    #define TO_WASI 0
#endif


//=//// AMIGA //////////////////////////////////////////////////////////////=//

// Note: The Amiga target is kept for its historical significance.
// Rebol required Amiga OS4 to be able to run, and the only
// machines that could run it had third-party add-on boards with
// PowerPC processors.  Hence stock machines like the Amiga4000
// which had a Motorola 68040 cannot built Rebol.
//
// To date, there has been no success reported in building Rebol
// for an Amiga emulator.  The last known successful build on
// Amiga hardware is dated 5-Mar-2011

#if !defined(TO_AMIGA)
    #define TO_AMIGA 0
#endif


//=//// (#if RUNTIME_CHECKS) BETTER THAN (#ifndef NDEBUG) /////////////////=//
//
// NDEBUG is the variable that is either #defined or not by the C assert.h
// convention.  The reason NDEBUG was used was because it was a weird name and
// unlikely to compete with codebases that had their own DEBUG definition.
//
// In order to help with not confusing terminology with things related to
// breakpoints of otherwise in the interpreter (debugger features), we call
// it a "checked build" and not a "debug build".  The flags are CHECK_XXX
// and not DEBUG_XXX for this reason, and RUNTIME_CHECKS is the general
// flag to test with #if.
//
// NO_RUNTIME_CHECKS is defined as well, because:
//
//     #if !RUNTIME_CHECKS          // easy to miss the !
//     #if (! RUNTIME_CHECKS)       // easier to see, but still obtuse
//     #if not RUNTIME_CHECKS       // MSVC preprocessor doesn't allow
//
#if !defined(RUNTIME_CHECKS)
    #if defined(NDEBUG)
        #define RUNTIME_CHECKS 0
    #else
        #define RUNTIME_CHECKS 1
    #endif
#endif
#if !defined(NO_RUNTIME_CHECKS)
    #define NO_RUNTIME_CHECKS (! RUNTIME_CHECKS)
#endif


//=//// DEBUG_STATIC_ANALYZING (BUILDING BLOCKS FOR MORE CHECKS) //////////=//
//
// Static analysis via tools such as Clang Static Analyzer aren't just useful
// for the obvious reasons, but also because you can use their checks as a
// box of parts for making custom checks that piggy-back on their powers.
//
// For instance: The ability to detect when you assign a variable the result
// of a malloc() but do not pass it anywhere or free() it will be checked on
// *all code paths* that return from a function.  This means that in a static
// analysis build a construction primitive can be expressed as a macro that
// spits out a dummy local variable assigned with a malloc, and a destruction
// primitive can spit out a free() instruction for that dummy local.  Then
// the static analysis can guarantee you run the destruction on all code
// paths before they return!
//
// Note also the potentially useful attribute: `ownership_returns(malloc, N)`
//
//   https://stackoverflow.com/a/71249340
//   https://github.com/llvm-mirror/clang/blob/master/lib/StaticAnalyzer/Checkers/MallocChecker.cpp
//
// 1. The implementation of Rebol uses idiomatic tricks that are like in
//    Rebol itself, such as assignments in expressions:
//
//        while (
//            (*temp == '<' and (seen_angles = true))
//            or (*temp == '>' and (seen_angles = true))
//            or *temp == '+' or ...
//        ){...}
//
//    MSVC's /analyze switch doesn't like it, so use a #pragma to allow it.
//    (Analyzer errors can't be disabled on the command line e.g. /Wd6282,
//    so we have to use a #pragma for it.)
//
// 2. Ren-C uses variable shadowing, and it's integral to the API:
//
//      https://forum.rebol.info/t/2224
//
//    So we have to disable warnings pertaining to that.
//
// 3. Static analysis can trigger false positives--e.g. noticing you're not
//    assigning an output parameter in an inline function, but then not
//    noticing that you only use it on control paths where you knew it
//    returned a value.  The unfortunate thing about bad static analysis is
//    that if you then write meaningless data into variables, you trip up an
//    actually *good* static analyzer that won't catch cases where you did
//    make a mistake...because the code path looks to return a valid value.
//
//    If you're stuck with having to do an assignment, do it under a flag
//    saying you're only doing it to appease the static analyzer.  Note that
//    use of Sink() in the debug build will actually randomize the data so
//    if you don't assign it you get a better fuzz test of that bad usage,
//    so there's no need to do the appeasing when you're using sinks (as it
//    will always look to be assigned).  But for this reason sinks should
//    not be used with static analysis!

#if !defined(DEBUG_STATIC_ANALYZING)
    #if defined(__clang_analyzer__)
        #define DEBUG_STATIC_ANALYZING 1
    #else
        #define DEBUG_STATIC_ANALYZING 0
    #endif
#endif

#if defined(_MSC_VER) && defined(_PREFAST_)  // _PREFAST_ if MSVC /analyze
    #pragma warning(disable : 6282)  // suppress "incorrect operator" [1]
    #pragma warning(disable : 6244)  // suppress hiding previous decl [2]
#endif

#if !defined(APPEASE_WEAK_STATIC_ANALYSIS)  // static analysis can be bad [3]
    #define APPEASE_WEAK_STATIC_ANALYSIS \
        ((! DEBUG_STATIC_ANALYZING) && (! DEBUG_USE_SINKS))
#else
    STATIC_ASSERT(DEBUG_STATIC_ANALYZING == 0)
#endif



//=//// CONTROL TICK COUNTING IN THE TRAMPOLINE ///////////////////////////=//
//
// Originally, the counting of evaluator "ticks" was a debug-only feature,
// as it exposed something kind of arbitrary about the internals...seemingly
// only useful when debugging the interpreter itself.  However, it came to
// be so useful in reproducible diagnostics that it's included even in
// otherwise optimized builds.
//
// It does mean you have to increment an additional integer every evaluator
// step, so it's not free.  But still rather low cost.  However, until it is
// deemed a core feature, thre's still the possibility to build without it
// (though many instrumentation scenarios require it).
//
#if !defined(TRAMPOLINE_COUNTS_TICKS)
    #define TRAMPOLINE_COUNTS_TICKS  RUNTIME_CHECKS
#endif


// Initially checked build switches were all (default) or nothing (-DNDEBUG)
// but needed to be broken down into a finer-grained list.  This way, more
// constrained systems (like emscripten) can build in just the features it
// needs for a specific debug scenario.
//
// !!! Revisit a more organized way to inventory these settings and turn them
// on and off as time permits.
//
#if !defined(DEBUG_HAS_PROBE)
    #define DEBUG_HAS_PROBE  RUNTIME_CHECKS
#endif

#if !defined(DEBUG_FANCY_CRASH)
    #define DEBUG_FANCY_CRASH  RUNTIME_CHECKS
#endif

#if !defined(DEBUG_MONITOR_FLEX)
    #define DEBUG_MONITOR_FLEX  RUNTIME_CHECKS
#endif

// Extra field added to Level for the UTF-8 string of a currently dispatched
// function.  Helpful for viewing in C watchlists...
//
#if !defined(DEBUG_LEVEL_LABELS)
    #define DEBUG_LEVEL_LABELS  RUNTIME_CHECKS
#endif

#if !defined(DEBUG_POISON_EXCESS_CAPACITY)
    #define DEBUG_POISON_EXCESS_CAPACITY  RUNTIME_CHECKS
#endif

#if !defined(DEBUG_BALANCE_STATE)
    #define DEBUG_BALANCE_STATE  RUNTIME_CHECKS
#endif

#if !defined(ALLOW_SPORADICALLY_NON_DETERMINISTIC)
    #define ALLOW_SPORADICALLY_NON_DETERMINISTIC RUNTIME_CHECKS
#endif

// See debugbreak.h and DECLARE_NATIVE(C_DEBUG_BREAK)...useful!
//
#if !defined(INCLUDE_C_DEBUG_BREAK_NATIVE)
    #define INCLUDE_C_DEBUG_BREAK_NATIVE  RUNTIME_CHECKS
#endif

// See DECLARE_NATIVE(TEST_LIBREBOL)
//
#if !defined(INCLUDE_TEST_LIBREBOL_NATIVE)
    #define INCLUDE_TEST_LIBREBOL_NATIVE  RUNTIME_CHECKS
#endif

#if !defined(DEBUG_CELL_READ_WRITE)
    #define DEBUG_CELL_READ_WRITE  RUNTIME_CHECKS
#endif

// Usually corrupting corrupts the headers only.
//
#if !defined(CORRUPT_CELL_HEADERS_ONLY)
    #define CORRUPT_CELL_HEADERS_ONLY  1
#endif

#if !defined(DEBUG_CHECK_BINDING)
    #define DEBUG_CHECK_BINDING  (RUNTIME_CHECKS && CPLUSPLUS_11)
#endif
#if DEBUG_CHECK_BINDING
    STATIC_ASSERT(CPLUSPLUS_11);
#endif


// See notes on ALIGN_SIZE regarding why we check this, and when it does and
// does not apply (some platforms need this invariant for `double` to work).
//
// !!! People using MLton to compile found that GCC 4.4.3 does not always
// align doubles to 64-bit boundaries on Windows, even when -malign-double
// is used.  It's a very old compiler, and may be a bug.  Disable align
// checking for GCC 4 on Windows, hope it just means slower loads/stores.
//
//   https://stackoverflow.com/a/11110283/211160
//
// "System V ABI for X86 says alignment can be 4 bytes for double.  But you
//  can change this in the compiler settings.  We should either sync with that
//  setting or just skip it, and assume that we do enough checking on 64-bit".
//
//   https://stackoverflow.com/q/14893802/
//
#if !defined(CHECK_MEMORY_ALIGNMENT)
  #ifdef __GNUC__
    #if !defined(TO_WINDOWS) || (__GNUC__ >= 5)  // only if at least version 5
        #define CHECK_MEMORY_ALIGNMENT  RUNTIME_CHECKS
    #else
        #define CHECK_MEMORY_ALIGNMENT  0
    #endif
  #else
    #define CHECK_MEMORY_ALIGNMENT  RUNTIME_CHECKS
  #endif
#endif


// When it comes to exception-handling mechanisms, we have 3 choices:
//
//    * PANIC_USES_LONGJMP to use C's setjmp()/longjmp()
//    * PANIC_USES_TRY_CATCH to use C++'s try {...} catch {...}
//    * PANIC_JUST_ABORTS will crash() and terminate the program
//
// It's considered desirable to support both a C and C++ approach.  Plain C
// compilation (e.g. with TCC) runs on many legacy/embedded platforms.  But
// structured exception handling has support on other systems like WasmEdge
// that cannot handle setjmp()/longjmp().
//
// To abstract this, Ren-C uses a keyword-like-macro called `panic()` that
// hides the differences.  See RESCUE_SCOPE_IN_CASE_OF_ABRUPT_PANIC for a
// breakdown of how this is pulled off.
//
// 1. setjmp()/longjmp() are essentially "goto on steroids", and on a
//    traditional platform they introduce the least baggage in terms of the
//    runtime needed to support them.  But while they are simple for many
//    traditional platforms, a runtime that enforces a de-facto structured
//    model may find it difficult-if-not-impossible to emulate them.
//
#if !defined(PANIC_USES_LONGJMP) \
        && !defined(PANIC_USES_TRY_CATCH) \
        && !defined(PANIC_JUST_ABORTS)

    #define PANIC_USES_LONGJMP 1  // often simplest, not always: [1]
    #define PANIC_USES_TRY_CATCH 0
    #define PANIC_JUST_ABORTS 0

#elif defined(PANIC_USES_LONGJMP)

    STATIC_ASSERT(PANIC_USES_LONGJMP == 1);
    #if defined(PANIC_USES_TRY_CATCH)
        STATIC_ASSERT(PANIC_USES_TRY_CATCH == 0);
    #else
        #define PANIC_USES_TRY_CATCH 0
    #endif
    #if defined(PANIC_JUST_ABORTS)
        STATIC_ASSERT(PANIC_JUST_ABORTS == 0);
    #else
        #define PANIC_JUST_ABORTS 0
    #endif

#elif defined(PANIC_USES_TRY_CATCH)

  #if !defined(__cplusplus)
    #error "PANIC_USES_TRY_CATCH requires compiling Ren-C with C++"
    #include <stophere>  // https://stackoverflow.com/a/45661130
  #endif

    STATIC_ASSERT(PANIC_USES_TRY_CATCH == 1);
    #if defined(PANIC_USES_LONGJMP)
        STATIC_ASSERT(PANIC_USES_LONGJMP == 0)
    #else
        #define PANIC_USES_LONGJMP 0
    #endif
    #if defined(PANIC_JUST_ABORTS)
        STATIC_ASSERT(PANIC_JUST_ABORTS == 0);
    #else
        #define PANIC_JUST_ABORTS 0
    #endif

#else

    STATIC_ASSERT(PANIC_JUST_ABORTS == 1);
    #if defined(PANIC_USES_LONGJMP)
        STATIC_ASSERT(PANIC_USES_LONGJMP == 0)
    #else
        #define PANIC_USES_LONGJMP 0
    #endif
    #if defined(PANIC_USES_TRY_CATCH)
        STATIC_ASSERT(PANIC_USES_TRY_CATCH == 0);
    #else
        #define PANIC_USES_TRY_CATCH 0
    #endif

#endif


// The cell subclasses [Element Value Atom] help to quarantine antiforms and
// unstable antiforms into slots that should have them.  I couldn't figure
// out a clean way to get the compile-time errors I wanted without adding
// runtime cost via wrapper classes...so they are only used in checked builds
// (and won't work in the C build at all).
//
#if !defined(CHECK_CELL_SUBCLASSES)
   #if RUNTIME_CHECKS && CPLUSPLUS_11
    #define CHECK_CELL_SUBCLASSES 1
  #else
    #define CHECK_CELL_SUBCLASSES 0
  #endif
    #define DONT_CHECK_CELL_SUBCLASSES (! CHECK_CELL_SUBCLASSES)
#endif


// Sinks are a feature which lets you mark a parameter as being output on an
// interface--which is good to know.  But the Sink()/Need() functions are
// actually crucial to CHECK_CELL_SUBCLASSES working.
//
#if !defined(DEBUG_USE_SINKS)
    #define DEBUG_USE_SINKS CHECK_CELL_SUBCLASSES
#else
    #if (! DEBUG_USE_SINKS) && CHECK_CELL_SUBCLASSES
        #error "DEBUG_USE_SINKS muts be enabled for CHECK_CELL_SUBCLASSES"
    #endif
#endif


// Natives can be decorated with a RETURN: annotation, but this is not
// checked in the release build.  It's assumed they will only return the
// correct types.  This switch is used to crash() if they're wrong.
//
#if !defined(CHECK_RAW_NATIVE_RETURNS)
    #define CHECK_RAW_NATIVE_RETURNS  RUNTIME_CHECKS
#endif

// It can be nice to see aliases of platform pointers as if they were
// individual bytes, through union "puns".  Though this behavior is not
// well defined, it can be useful a lot of the time.
//
// https://en.wikipedia.org/wiki/Type_punning
//
#if !defined(DEBUG_USE_UNION_PUNS)
    #define DEBUG_USE_UNION_PUNS  RUNTIME_CHECKS
#endif

// Bitfields are poorly specified, and so even if it looks like your bits
// should pack into a struct exactly, they might not.  Only try this on
// Linux, where it has seemed to work out (MinGW64 build on Cygwin made
// invalid cell sizes with this on)
//
#if defined(ENDIAN_LITTLE) && TO_LINUX_X64
    #define DEBUG_USE_BITFIELD_HEADER_PUNS 1
#else
    #define DEBUG_USE_BITFIELD_HEADER_PUNS 0
#endif

#if !defined(DEBUG_ENABLE_ALWAYS_MALLOC)
    //
    // We may want to test the valgrind build even if it's release so that
    // it checks the R3_ALWAYS_MALLOC environment variable
    //
    #if defined(INCLUDE_CALLGRIND_NATIVE)
        #define DEBUG_ENABLE_ALWAYS_MALLOC  1
    #else
        #define DEBUG_ENABLE_ALWAYS_MALLOC  RUNTIME_CHECKS
    #endif
#endif

// Each PUSH() on the data stack can potentially move all the pointers on the
// stack.  Hence there is a debug setting for managing these pointers in a
// special C++ container called OnStack(Value*).  This counts to see how many
// stack pointers the user has in local variables, and if that number is not
// zero then it asserts when a push or pop is requested, or upon evaluation.
//
#if !defined(DEBUG_EXTANT_STACK_POINTERS)
  #if CPLUSPLUS_11 && RUNTIME_CHECKS
    #define DEBUG_EXTANT_STACK_POINTERS 1
  #else
    #define DEBUG_EXTANT_STACK_POINTERS 0
  #endif
#endif

// The PG_Reb_Stats structure is only tracked in checked builds, as this
// data gathering is a sort of constant "tax" on the system.  While it
// might arguably be interesting to release build users who are trying
// to optimize their code, the compromise of having to maintain the
// numbers suggests those users should be empowered with a checked build if
// they are doing such work (they should probably have one for other
// reasons; note this has been true of things like Windows NT where there
// were indeed "checked" builds given to those who had such interest.)
//
#if !defined(DEBUG_COLLECT_STATS)
    #define DEBUG_COLLECT_STATS  RUNTIME_CHECKS
#endif

// See notes on ensure_executor() for why executor files define their own
// set of macros for use within their files.
//
#if !defined(DEBUG_ENSURE_EXECUTOR_FLAGS)
    #define DEBUG_ENSURE_EXECUTOR_FLAGS  RUNTIME_CHECKS
#endif

// UTF-8 Everywhere is a particularly large system change, which requires
// careful bookkeeping to allow the caching of positions to work.  These
// checks are too slow to run on most builds, but should be turned on if
// any problems are seen.
//
#if !defined(DEBUG_UTF8_EVERYWHERE)
    #define DEBUG_UTF8_EVERYWHERE  0
#endif
#if DEBUG_UTF8_EVERYWHERE
  #if NO_CPLUSPLUS_11
    #error "DEBUG_UTF8_EVERYWHERE requires C++11 or higher"
    #include <stophere>  // https://stackoverflow.com/a/45661130
  #endif
#endif


#if !defined(DEBUG_VERIFY_STR_AT)  // check cache correctness on every STR_AT
    #define DEBUG_VERIFY_STR_AT DEBUG_UTF8_EVERYWHERE
#endif

#if !defined(DEBUG_SPORADICALLY_DROP_BOOKMARKS)
    #define DEBUG_SPORADICALLY_DROP_BOOKMARKS DEBUG_UTF8_EVERYWHERE
#endif

#if !defined(DEBUG_BOOKMARKS_ON_MODIFY)  // test bookmark absence
    #define DEBUG_BOOKMARKS_ON_MODIFY DEBUG_UTF8_EVERYWHERE
#endif

#if !defined(DEBUG_TRACE_BOOKMARKS)
    #define DEBUG_TRACE_BOOKMARKS 0
#endif


// Due to using the `cast(...)` operator instead of a plain cast, the fact
// that it goes through a helper template means that it can be hooked with
// code in diagnostic builds.  This is taken advantage of by the build
// setting DEBUG_CHECK_CASTS.
//
// Currently disable this by default unless you are using address sanitizer,
// which is the build you'd be using if there were unexpected problems (and
// you'd expect things to be slow anyway.)
//
#if !defined(DEBUG_CHECK_CASTS)
  #if defined(__SANITIZE_ADDRESS__) && CPLUSPLUS_11
    #define DEBUG_CHECK_CASTS  RUNTIME_CHECKS
  #else
    #define DEBUG_CHECK_CASTS  0  // requires C++
  #endif
#endif
#if DEBUG_CHECK_CASTS
  #if NO_CPLUSPLUS_11
    #error "DEBUG_CHECK_CASTS requires C++11 (or later)"
    #include <stophere>  // https://stackoverflow.com/a/45661130
  #endif
#endif


// Both Valgrind and Address Sanitizer can provide the call stack at the moment
// of allocation when a freed pointer is used.  Touch_Stub() uses a bogus
// allocation to help mark Stub origins that can later be used by `crash()`.
// But the feature is a waste if you're not using such tools.
//
// If you plan to use Valgrind with this, you'll have to set it explicitly...
// only Address Sanitizer can be detected here.
//
#if !defined(DEBUG_STUB_ORIGINS)
  #if defined(__SANITIZE_ADDRESS__)
    #define DEBUG_STUB_ORIGINS  RUNTIME_CHECKS
  #else
    #define DEBUG_STUB_ORIGINS  0
  #endif
#endif


// Option(TYPE*) is a poor-man's implementation of optionals that lets you
// mark when a pointer is supposed to be passable as a nullptr.  It has some
// runtime costs because it will assert if you `unwrap` the pointer and it is
// null when it shouldn't be.  Add it to the sanitized build.
//
#if !defined(CHECK_OPTIONAL_TYPEMACRO)
  #if defined(__SANITIZE_ADDRESS__)
    #define CHECK_OPTIONAL_TYPEMACRO  (RUNTIME_CHECKS && CPLUSPLUS_11)
  #else
    #define CHECK_OPTIONAL_TYPEMACRO  0
  #endif
#endif

#if !defined(CHECK_NEVERNULL_TYPEMACRO)
  #if defined(__SANITIZE_ADDRESS__)
    #define CHECK_NEVERNULL_TYPEMACRO  (RUNTIME_CHECKS && CPLUSPLUS_11)
  #else
    #define CHECK_NEVERNULL_TYPEMACRO 0
  #endif
#endif

#if !defined(DEBUG_PROTECT_FEED_CELLS)
    #define DEBUG_PROTECT_FEED_CELLS  RUNTIME_CHECKS
#endif

#if !defined(DEBUG_PROTECT_PARAM_CELLS)
    #define DEBUG_PROTECT_PARAM_CELLS  RUNTIME_CHECKS
#endif


// Uninitialized memory has no predictable pattern.  We could pay to memset()
// all uninitialized cells to zero to erase them, but that has a cost you
// don't want to pay if you're just going to overwrite it.  Poisoning the
// uninitialized cells in the checked build has the advantage of letting
// "fast" operations that ovewrite all a cell's bit without masking know that
// you're not overwriting something important.
//
#if !defined(DEBUG_POISON_UNINITIALIZED_CELLS)
    #define DEBUG_POISON_UNINITIALIZED_CELLS  RUNTIME_CHECKS
#endif


// In order to make sure that a good mix of debug settings get tested, this
// does array termination checks on non-sanitizer checked builds.  Arrays are
// not usually marked at their tails (unlike R3-Alpha which used END! cells to
// terminate)...but the residual functionality helps catch overruns.
//
#if !defined(DEBUG_POISON_FLEX_TAILS)
  #if defined(__SANITIZE_ADDRESS__)
    #define DEBUG_POISON_FLEX_TAILS  0  // *not* when sanitized
  #else
    #define DEBUG_POISON_FLEX_TAILS  RUNTIME_CHECKS
  #endif
#endif

#if !defined(DEBUG_CHECK_ENDS)
  #if defined(__SANITIZE_ADDRESS__)
    #define DEBUG_CHECK_ENDS 0  // *not* when sanitized
  #else
    #define DEBUG_CHECK_ENDS (CPLUSPLUS_11 && RUNTIME_CHECKS) ? 1 : 0
  #endif
#endif

#if !defined(DEBUG_TRACK_EXTEND_CELLS)
    #define DEBUG_TRACK_EXTEND_CELLS 0
#endif

#if !defined(DEBUG_TRACK_COPY_PRESERVES)
    #define DEBUG_TRACK_COPY_PRESERVES 0
#endif

#if DEBUG_TRACK_COPY_PRESERVES
    #if (! DEBUG_TRACK_EXTEND_CELLS)
        #error "DEBUG_TRACK_COPY_PRESERVES requires DEBUG_TRACK_EXTEND_CELLS"
    #endif
#endif


#if !defined(UNUSUAL_CELL_SIZE)  // sizeof(Cell)*2 may be > sizeof(Stub)
    #define UNUSUAL_CELL_SIZE DEBUG_TRACK_EXTEND_CELLS
#endif

#if !defined(DEBUG_POISON_DROPPED_STACK_CELLS)
    #define DEBUG_POISON_DROPPED_STACK_CELLS  RUNTIME_CHECKS
#endif

#if !defined(DEBUG_EXTRA_HEART_CHECKS)
  #if !defined(__clang__)  // Note: MSVC has more lax enum conversion than gcc
    #define DEBUG_EXTRA_HEART_CHECKS  (RUNTIME_CHECKS && CPLUSPLUS_11)
  #else
    #define DEBUG_EXTRA_HEART_CHECKS  0  // clang won't cross switch() enums
  #endif
#endif

#if DEBUG_EXTRA_HEART_CHECKS
    //
    // Note: There's actually a benefit to clang not tolerating this, as
    // it means that enum comparisons that aren't intentional from distinct
    // enums will be caught in clang builds.
    //
  #if defined(__clang__)
    #error "DEBUG_EXTRA_HEART_CHECKS won't work in clang (partial gcc support)"
    #include <stophere>  // https://stackoverflow.com/a/45661130
  #endif
#endif

#if !defined(DEBUG_HOOK_HEART_BYTE)
  #if defined(_MSC_VER)  // can use enum class for TYPE_XXX, so needs hook
    #define DEBUG_HOOK_HEART_BYTE  DEBUG_EXTRA_HEART_CHECKS
  #else
    #define DEBUG_HOOK_HEART_BYTE  0
  #endif
#endif

#if !defined(DEBUG_HOOK_MIRROR_BYTE)
  #if defined(_MSC_VER)  // can use enum class for TYPE_XXX, so needs hook
    #define DEBUG_HOOK_MIRROR_BYTE  DEBUG_EXTRA_HEART_CHECKS
  #else
    #define DEBUG_HOOK_MIRROR_BYTE  0  // without enum class, it's useless
  #endif
#endif

#if !defined(DEBUG_HOOK_LIFT_BYTE)
    #define DEBUG_HOOK_LIFT_BYTE  DEBUG_EXTRA_HEART_CHECKS
#endif


// This checks to make sure that when you are assigning or fetching something
// like Stub.misc.node, then the flag like STUB_FLAG_MISC_NODE_NEEDS_MARK
// is also set.  It's good for helping track down GC bugs, but just slows
// things down most of the time...so default it to being off.
//
#if !defined(DEBUG_CHECK_GC_HEADER_FLAGS)
    #define DEBUG_CHECK_GC_HEADER_FLAGS  0
#endif


// This is a *painfully* slow debug switch, which allows you to say that the
// intrinsic functions never run intrinsically, but are called normally with
// their own Level.  That means type checking is very slow, because things
// like ANY-VALUE? become first-class function calls in all cases.
//
// If you don't use this switch, the debug build SPORADICALLY() throws in a
// normal evaluation for intrinsics just to test the code path.  But if you
// want to really torture it, set this to 1.
//
#if !defined(DEBUG_DISABLE_INTRINSICS)
    #define DEBUG_DISABLE_INTRINSICS  0
#endif


// It can be very difficult in release builds to know where a panic came
// from.  This arises in pathological cases where an error only occurs in
// release builds, or if making a full checked build bloats the code too much.
// (e.g. the JavaScript asyncify version).  A small but helpful debug
// switch does a printf of the __FILE__ and __LINE__ of panic() callsites.
//
#if !defined(DEBUG_PRINTF_PANIC_LOCATIONS)
    #define DEBUG_PRINTF_PANIC_LOCATIONS 0
#endif

#if !defined(DEBUG_VIRTUAL_BINDING)
    #define DEBUG_VIRTUAL_BINDING 0
#endif

// The third-party "dtoa.c" file was sensitive to whether DEBUG was #ifdef'd.
// In the world where #if is used instead of #ifdef, that means it includes the
// debug code whether DEBUG is 1 or 0.  The file was tweaked to include a more
// specific flag for debugging dtoa.c, which we will hopefully never need.
//
// !!! Now that DEBUG has been changed to RUNTIME_CHECKS, the file could be
// reverted, if that were important.
//
#if !defined(DEBUG_DTOA)
    #define DEBUG_DTOA 0
#endif

#endif
