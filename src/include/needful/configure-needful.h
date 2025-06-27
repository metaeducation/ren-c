//
//  file: %configure-needful.h
//  summary: "Configuration flags for the needful library"
//  homepage: <needful homepage TBD>
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// It's best to use 0 and 1 to define things, to avoid typo problems when you
// just use `#ifdef SOME_MISPELT_FLAG` vs. `#if SOME_MISPELT_FLAG`.
//


//=//// STDINT.H AND STDBOOL.H ////////////////////////////////////////////=//
//
// Ren-C assumes the availability of <stdint.h> and <stdbool.h>
//
// If for some reason the only barrier to compiling the codebase is lack of
// these definitions, there are shims on the Internet that implement them
// (look for pstdint.h and pstdbool.h)  But lack of variadic macros is likely
// to be a bigger showstopper on older platforms.
//
// * One aspect of pstdint.h is that it considers 64-bit "optional".
//   Some esoteric platforms may have a more hidden form of 64-bit support,
//   e.g. this case from R3-Alpha for "Windows VC6 nonstandard typing":
//
//     #if (defined(_MSC_VER) && (_MSC_VER <= 1200))
//         typedef _int64 i64;
//         typedef unsigned _int64 u64;
//         #define I64_C(c) c ## I64
//         #define U64_C(c) c ## U64
//     #endif
//
//   If %pstdint.h isn't trying hard enough for an unsupported platform of
//   interest to get 64-bit integers, then patches should be made there.
//
// * INT32_MAX and INT32_C can be missing in C++ builds on some older
//   compilers without __STDC_LIMIT_MACROS and __STDC_CONSTANT_MACROS:
//
// https://sourceware.org/bugzilla/show_bug.cgi?id=15366
//
#include <stdint.h>
#if !defined(__cplusplus)
    #include <stdbool.h>
#endif


//=//// NEEDFUL EXPECTS assert() TO BE DEFINED ALREADY ////////////////////=//
//
// This file uses assert(), but doesn't #include <assert.h> on its own, since
// there may be overriding assertion definitions desired.  (Some assert
// implementations provided by the system are antagontic to debuggers and
// need replacement.)
//
#if !defined(assert)
    #error "Include <assert.h> or assert-fix.h before including needful.h"
    #include <stophere>  // https://stackoverflow.com/a/45661130
#endif


//=//// CPLUSPLUS_11 PREPROCESSOR DEFINE //////////////////////////////////=//
//
// Because the goal of Ren-C is generally to be buildable as C, the C++ build
// is mostly for static analysis and debug checks.  (But also exceptions with
// try/catch, on platforms where setjmp()/longjmp() are not viable.)
//
// There's not much value in trying to tailor reduced versions of the checks
// to ANSI C++98 compilers, so the "C++ build" is an "at least C++11 build".
//
// Besides being a little less verbose to use, testing via a #define allows
// override when using with Microsoft Visual Studio via a command line
// definition.  For some reason they didn't bump the version number from 1997
// (even by MSVC 2017!!!)
//
#if !defined(CPLUSPLUS_11)
  #if defined(__cplusplus) && __cplusplus >= 201103L
    #define CPLUSPLUS_11  1
  #else
    #define CPLUSPLUS_11  0
  #endif
#endif
#if !defined(NO_CPLUSPLUS_11)
    #define NO_CPLUSPLUS_11 (! CPLUSPLUS_11)
#endif

#if !defined(C_11)
  #if defined(STDC_VERSION) && STDC_VERSION >= 201112L
    #define C_11 1
  #else
    #define C_11 0
  #endif
#endif


//=//// FEATURE TESTING AND ATTRIBUTE MACROS //////////////////////////////=//
//
// Feature testing macros __has_builtin() and __has_feature() were originally
// a Clang extension, but GCC added support for them.  If compiler doesn't
// have them, default all features unavailable.
//
// http://clang.llvm.org/docs/LanguageExtensions.html#feature-checking-macros
//
// Similarly, the __attribute__ feature is not in the C++ standard and only
// available in some compilers.  Even compilers that have __attribute__ may
// have different individual attributes available on a case-by-case basis.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Note: Placing the attribute after the prototype seems to lead to
// complaints, and technically there is a suggestion you may only define
// attributes on prototypes--not definitions:
//
// http://stackoverflow.com/q/23917031/211160
//
// Putting the attribute *before* the prototype seems to allow it on both the
// prototype and definition in gcc, however.
//

#ifndef __has_builtin
    #define __has_builtin(x) 0
#endif

#ifndef __has_feature
    #define __has_feature(x) 0
#endif

#ifdef __GNUC__
    #define GCC_VERSION_AT_LEAST(m, n) \
        (__GNUC__ > (m) || (__GNUC__ == (m) && __GNUC_MINOR__ >= (n)))
#else
    #define GCC_VERSION_AT_LEAST(m, n) 0
#endif


//=//// RUNTIME_CHECKS ////////////////////////////////////////////////////=//

#if !defined(RUNTIME_CHECKS)  // prefer "RUNTIME_CHECKS" as integer #define
    #if defined(NDEBUG)
       #define RUNTIME_CHECKS  0
    #else
       #define RUNTIME_CHECKS  1
    #endif
#endif
#if !defined(NO_RUNTIME_CHECKS)
    #define NO_RUNTIME_CHECKS (! RUNTIME_CHECKS)
#endif


//=//// DEBUG_STATIC_ANALYZING ////////////////////////////////////////////=//

#if !defined(DEBUG_STATIC_ANALYZING)
    #define DEBUG_STATIC_ANALYZING  0
#endif


//=//// NEEDFUL_OPTION_USES_WRAPPER //////////////////////////////////////////=//

#if !defined(NEEDFUL_OPTION_USES_WRAPPER)
    #define NEEDFUL_OPTION_USES_WRAPPER  0
#endif


//=//// CHECK_NEVERNULL_TYPEMACRO /////////////////////////////////////////=//

#if !defined(CHECK_NEVERNULL_TYPEMACRO)
    #define CHECK_NEVERNULL_TYPEMACRO  0
#endif


//=//// DEBUG_USE_SINKS + DEBUG_CHECK_INIT_SINKS /////////////////////////=//

#if !defined(DEBUG_USE_SINKS)
    #define DEBUG_USE_SINKS  0
#endif

#if !defined(DEBUG_CHECK_INIT_SINKS)
    #define DEBUG_CHECK_INIT_SINKS  0
#endif


//=//// ASSIGN_UNUSED_FIELDS //////////////////////////////////////////////=//
//
// See Corrupt_Unused_Field()
//
#if !defined(ASSIGN_UNUSED_FIELDS)
    #define ASSIGN_UNUSED_FIELDS 1
#endif


//=//// PERFORM_CORRUPTIONS + CORRUPTION SEED/DOSE ////////////////////////=//

#if !defined(PERFORM_CORRUPTIONS)  // 1. See Corrupt_If_Debug()
    #define PERFORM_CORRUPTIONS \
        (RUNTIME_CHECKS && (! DEBUG_STATIC_ANALYZING))  // [1]
#endif

#if PERFORM_CORRUPTIONS  // generate some variability, but still deterministic
  #if defined(__clang__)
    #define CORRUPT_IF_DEBUG_SEED 5  // e.g. fifth corrupt pointer is zero
    #define CORRUPT_IF_DEBUG_DOSE 11
  #else
    #define CORRUPT_IF_DEBUG_SEED 0  // e.g. first corrupt pointer is zero
    #define CORRUPT_IF_DEBUG_DOSE 7
  #endif
#endif


//=//// NEEDFUL_DONT_INCLUDE_STDARG_H /////////////////////////////////////=//

// Not all clients necessarily want to #include <stdarg.h> ... it may not be
// available on the platform or could cause problems if included in some
// codebases.  Default to including it since it offers protections people
// may not be aware are necessary, but allow it to be turned off.
//
#if !defined(NEEDFUL_DONT_INCLUDE_STDARG_H)
    #define NEEDFUL_DONT_INCLUDE_STDARG_H  0
#endif

#if (! NEEDFUL_DONT_INCLUDE_STDARG_H)
    #include <stdarg.h>  // va_list disallowed in cast() and used in h_cast()
#endif


//=//// ASSERT_IMPOSSIBLE_THINGS //////////////////////////////////////////=//

#if !defined(ASSERT_IMPOSSIBLE_THINGS)
    #define ASSERT_IMPOSSIBLE_THINGS  0  // don't bother with impossible()
#endif


//=//// USE DEFAULT SHORTHANDS ////////////////////////////////////////////=//
//
// By default, we define things like `Option()` and `maybe` and `unwrap`.
// But these may be defined by clients, so allow disablement of these short
// forms, so that they can define them via other names.
//

#if !defined(NEEDFUL_USE_DEFAULT_SHORTHANDS)
    #define NEEDFUL_USE_DEFAULT_SHORTHANDS  1
#endif
