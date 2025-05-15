//
//  file: %c-enhanced.h
//  summary: "Tools for C with enhanced features if built as C++11 or higher"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// Ren-C is designed to be able to build as C99 (or higher).
//
// BUT if the system is built as C++11 (or higher), there are extended runtime
// and compile-time checks available.
//
// This file contains various definitions for constructs that will behave
// in more "interesting" ways when built as C++.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Not all that many C99 features are required:
//
//      * __VA_ARGS__ variadic macros,
//      * double-slash comments
//      * declaring variables in the middle of functions
//
//   Many C89-era compilers could do these things before they were standards.
//   So there's a possibility Ren-C will compile on pre-C99 systems.
//
// * C++98 support was included for a while, but it lacks <type_traits> and
//   other features which are required to make the C++ build of any real use
//   beyond what C provides.  So support for C++98 was ultimately dropped.
//

#ifndef C_ENHANCED_H  // "include guard" allows multiple #includes
#define C_ENHANCED_H


//=//// CONFIGURATION /////////////////////////////////////////////////////=//

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

#if !defined(DEBUG_STATIC_ANALYZING)
    #define DEBUG_STATIC_ANALYZING  0
#endif

#if !defined(CHECK_OPTIONAL_TYPEMACRO)
    #define CHECK_OPTIONAL_TYPEMACRO  0
#endif

#if !defined(CHECK_NEVERNULL_TYPEMACRO)
    #define CHECK_NEVERNULL_TYPEMACRO  0
#endif

#if !defined(DEBUG_USE_SINKS)
    #define DEBUG_USE_SINKS  0
#endif

#if !defined(DEBUG_CHECK_INIT_SINKS)
    #define DEBUG_CHECK_INIT_SINKS  0
#endif

#if !defined(ASSIGN_UNUSED_FIELDS)
    #define ASSIGN_UNUSED_FIELDS 1
#endif


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


//=//// EXPECTS assert() TO BE DEFINED ALREADY ////////////////////////////=//
//
// This file uses assert(), but doesn't #include <assert.h> on its own, since
// there may be overriding assertion definitions desired.  (Some assert
// implementations provided by the system are antagontic to debuggers and
// need replacement.)
//
#if !defined(assert)
    #error "Include <assert.h> or assert-fix.h before including c-enhanced.h"
    #include <stophere>  // https://stackoverflow.com/a/45661130
#endif


//=//// ISO646 ALTERNATE TOKENS FOR BOOLEAN OPERATIONS ////////////////////=//
//
// It is much more readable to see `and` and `or` instead of `&&` and `||`
// when reading expressions.  Ren-C embraces the ISO646 standard:
//
// https://en.wikipedia.org/wiki/C_alternative_tokens
//
// It also adds one more to the list: `did` for converting "truthy" values to
// boolean.  This is clearer than `not not` or `!!`:
//
// http://blog.hostilefork.com/did-programming-opposite-of-not/
//

#define did !!  // Not in iso646.h

#if defined(__cplusplus)
  #if defined(_MSC_VER)
    #include <iso646.h>  // MSVC doesn't have `and`, `not`, etc. w/o this
  #else
    // legitimate compilers define them, they're even in the C++98 standard!
  #endif
#else
    // is646.h has been defined since the C90 standard of C.  But TCC doesn't
    // ship with it, and maybe other C's don't either.  The issue isn't so
    // much the file, as it is agreeing on the 11 macros, just define them.
    //
    #define and &&
    #define and_eq &=
    #define bitand &
    #define bitor |
    #define compl ~
    #define not !
    #define not_eq !=
    #define or ||
    #define or_eq |=
    #define xor ^
    #define xor_eq ^=
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


//=//// INCLUDE "CASTS FOR THE MASSES" ////////////////////////////////////=//
//
// This defines macros of the style `cast(T, value)` as a replacement for the
// hard-to-discern `(T)value` classic C cast.  The rationale is laid out in
// this article:
//
//   http://blog.hostilefork.com/c-casts-for-the-masses/
//
// ...but the implementation has evolved significantly since 2015.  Until the
// article is updated to reflect the code, see the comments for details.
//
// The file dependens on the definitions of CPLUSPLUS_11 and NO_CPLUSPLUS_11,
// so inclusion can't be earlier than this point.
//
// (c-casts.h includes <type_traits>)
//
#include "c-casts.h"


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


//=//// UNREACHABLE CODE ANNOTATIONS //////////////////////////////////////=//
//
// Because Rebol uses `longjmp` and `exit` there are cases where a function
// might look like not all paths return a value, when those paths actually
// aren't supposed to return at all.  For instance:
//
//     int foo(int x) {
//         if (x < 1020)
//             return x + 304;
//         panic ("x is too big"); // compiler may warn about no return value
//     }
//
// One way of annotating to say this is okay is on the caller, with DEAD_END:
//
//     int foo(int x) {
//         if (x < 1020)
//             return x + 304;
//         panic ("x is too big");
//         DEAD_END; // our warning-suppression macro for applicable compilers
//     }
//
// DEAD_END is just a no-op in compilers that don't have the feature of
// suppressing the warning--which can often mean they don't have the warning
// in the first place.
//
// Another macro we define is ATTRIBUTE_NO_RETURN.  This can be put on the
// declaration site of a function like `panic()` itself, so the callsites don't
// need to be changed.  As with DEAD_END it degrades into a no-op in compilers
// that don't support it.
//

/* THESE HAVE BEEN RELOCATED TO %rebol.h, SEE DEFINITIONS THERE */


//=//// STATIC ASSERT /////////////////////////////////////////////////////=//
//
// Some conditions can be checked at compile-time, instead of deferred to a
// runtime assert.  This macro triggers an error message at compile time.
// `static_assert` is an arity-2 keyword in C++11 and can act as arity-1 in
// C++17, for expedience we mock up an arity-1 form.
//
// It's possible to hack up a static assert in C:
//
// http://stackoverflow.com/questions/3385515/static-assert-in-c
//
// But it's too limited.  Since the code can (and should) be built as C++11
// to test anyway, just make it a no-op in the C build.
//
#if !defined(STATIC_ASSERT)  // used in %reb-config.h so also defined there
    #if CPLUSPLUS_11
        #define STATIC_ASSERT(cond) \
            static_assert((cond), #cond) // callsite has semicolon, see C trick
    #else
        #define STATIC_ASSERT(cond) \
            struct GlobalScopeNoopTrick // https://stackoverflow.com/q/53923706
    #endif
#endif


//=//// CONDITIONAL C++ NAME MANGLING MACROS //////////////////////////////=//
//
// When linking C++ code, different functions with the same name need to be
// discerned by the types of their parameters.  This means their name is
// "decorated" (or "mangled") from the fairly simple and flat convention of
// a C function.
//
// https://en.wikipedia.org/wiki/Name_mangling
// http://en.cppreference.com/w/cpp/language/language_linkage
//
// This also applies to global variables in some compilers (e.g. MSVC), and
// must be taken into account:
//
// https://stackoverflow.com/a/27939238/211160
//
// When built as C++, Ren-C must tell the compiler that functions/variables
// it exports to the outside world should *not* use C++ name mangling, so that
// they can be used sensibly from C.  But the instructions to tell it that
// are not legal in C.  This conditional macro avoids needing to put #ifdefs
// around those prototypes.
//
#if defined(__cplusplus)
    #define EXTERN_C extern "C"
#else
    // !!! There is some controversy on whether EXTERN_C should be a no-op in
    // a C build, or decay to the meaning of C's `extern`.  Notably, WinSock
    // headers from Microsoft use this "decays to extern" form:
    //
    // https://stackoverflow.com/q/47027062/
    //
    // Review if this should be changed to use an EXTERN_C_BEGIN and an
    // EXTERN_C_END style macro--which would be a no-op in the C build and
    // require manual labeling of `extern` on any exported variables.
    //
    #define EXTERN_C extern
#endif


//=//// C-ONLY CONSTNESS //////////////////////////////////////////////////=//
//
// C lacks overloading, which means that having one version of code for const
// input and another for non-const input requires two entirely different names
// for the function variations.  That can wind up seeming noisier than is
// worth it for a compile-time check.  This makes it easier to declare the C++
// variation for the const case:
//
//    // C build mutable result even if const input (mutable case in C++)
//    Member* Get_Member(const_if_c Object* ptr) { ... }
//
//    #if CPLUSPLUS_11
//        // C++ build adds protection to the const input case
//        const Member* Get_Member(const Object *ptr) { ... }
//    #endif
//
// Note: If writing a simple wrapper whose only purpose is to pipe the
// const-correct output result from the input's constness, another trick is to
// use `c_cast()` which is a "const-preserving cast".
//
//    #define Get_Member_As_Foo(ptr)  c_cast(Foo*, Get_Member(ptr))
//
#if CPLUSPLUS_11
    #define const_if_c
#else
    #define const_if_c const
#endif


//=//// INLINE MACRO FOR LEVERAGING C++ OPTIMIZATIONS /////////////////////=//
//
// "inline" has a long history in C/C++ of being different on different
// compilers, and took a long time to get into the standard.  Once it was in
// the standard it essentially didn't mean anything in particular about
// inlining--just "this function is legal to appear in a header file and be
// included in multiple source files without generating conflicts."  The
// compiler makes no particular promises about actually inlining the code.
//
// R3-Alpha had few inline functions, but mostly used macros--in unsafe ways
// (repeating arguments, risking double evaluations, lacking typechecking.)
// Ren-C reworked the code to use inline functions fairly liberally, even
// putting fairly large functions in header files to give the compiler the
// opportunity to not need to push or pop registers to make a call.
//
// However, GCC in C99 mode requires you to say `static inline` or else you'll
// get errors at link time.  This means that every translation unit has its
// own copy of the code.  A study of the pathology of putting larger functions
// in headers as inline with `static inline` on them found that about five
// functions were getting inlined often enough to add 400K to the executable.
// Moving them out of .h files and into .c files dropped that size, and was
// only about *0.4%* slower (!) making it an obvious win to un-inline them.
//
// This led to experimentation with C++ builds just using `inline`, which
// saved a not-insignificant 8% of space in an -O2 build, as well as being ever
// so slightly faster.  Even if link-time-optimization was used, it still
// saved 3% on space.
//
// The long story short here is that plain `inline` is better if you can use
// it, but you can't use it in gcc in C99 mode (and probably not other places
// like TinyC compiler or variants).  So this clunky INLINE macro actually
// isn't some pre-standards anachronism...it has concrete benefits.
//
#if CPLUSPLUS_11
    #define INLINE inline
#else
    #define INLINE static inline
#endif


//=//// nullptr SHIM FOR C ////////////////////////////////////////////////=//
//
// The C language definition allows compilers to simply define NULL as 0.
// This creates ambiguity in C++ when one overloading of a function takes an
// integer, and another a pointer...since 0 can be coerced to either.  So
// a specific `nullptr` was defined to work around this.
//
// But the problem isn't just with C++.  There is a common issue in variadics
// where NULL is used to terminate a sequence of values that are interpreted
// as pointers:
//
//     variadic_print("print me", "and me", "stop @ NULL", NULL);
//
// Yet there is no way to do this in standards-compliant C.  On a platform
// where integers and pointers aren't compatible sizes or bit patterns, then
// the `0` which NULL evaluates to in that last slot can't be interpreted
// as a null pointer.  You'd have to write:
//
//     variadic_print("print me", "and me", "stop @ NULL", (char*)NULL);
//
// Since libRebol hinges on a premise of making the internal ~null~ signifier
// interface as a C NULL pointer, and hinges on variadics, this is a problem.
// Rather than introduce a "new" abstraction or macro, this adds a simple
// macro to C.
//
// This also means that NULL can be used in comments for the Rebol concept,
// as opposed to the C idea (though NULLED may be clearer, depending on
// context).  Either way, when discussing C's "0 pointer", say `nullptr`.
//

#if NO_CPLUSPLUS_11
  #if !defined(nullptr)
    #define nullptr  u_cast(void*, 0)
  #endif
#else
    // http://en.cppreference.com/w/cpp/language/nullptr
    // is defined as `using nullptr_t = decltype(nullptr);` in <cstddef>
    //
    #include <cstddef>
    using std::nullptr_t;
#endif


//=//// CONST COPYING TYPE TRAIT //////////////////////////////////////////=//
//
// This is a simple trait which adds const to the first type if the second
// type is const.
//
#if CPLUSPLUS_11
    template<typename U,typename T>
    struct copy_const {
        using type = typename std::conditional<
            std::is_const<T>::value,
            typename std::add_const<U>::type,
            U
        >::type;
    };

    template<typename U,typename T>
    using copy_const_t = typename copy_const<U,T>::type;
#endif


//=//// ENABLE IF FOR SAME TYPE ///////////////////////////////////////////=//
//
// This is useful for SFINAE (Substitution Failure Is Not An Error), as a
// very common pattern.  It's variadic, so you can use it like:
//
//   template <typename T, EnableIfSame<T, TypeOne, TypeTwo> = nullptr>
//   INLINE bool operator==(const TypeThree& three, T&& t) = delete;
//
// Written out long form that would look like:
//
//    template <
//        typename T,
//        typename std::enable_if<
//            std::is_same<T, TypeOne>::value
//            or std::is_same<T, TypeTwo>::value
//        >::type* = nullptr
//     >
//     INLINE bool operator==(const TypeThree& three, T&& t) = delete;
//
#if CPLUSPLUS_11
    template <typename T, typename... Allowed>
    struct IsSameAny;

    template <typename T, typename First, typename... Rest>
    struct IsSameAny<T, First, Rest...> {
        static constexpr bool value =
            std::is_same<T, First>::value or IsSameAny<T, Rest...>::value;
    };

    template <typename T>
    struct IsSameAny<T> {
        static constexpr bool value = false;
    };

    template <typename T, typename... Allowed>
    using EnableIfSame =
        typename std::enable_if<IsSameAny<T, Allowed...>::value>::type*;
#endif


//=//// NOOP a.k.a. VOID GENERATOR ////////////////////////////////////////=//
//
// VOID would be a more purposeful name, but Windows headers define that
// for the type (as used in types like LPVOID)
//
// As an added application, it is used to annotate a block that you don't
// want picked up as an argument to an `if` or anything.  It makes it more
// obvious why the block is there, and looks like a language feature:
//
//     blockscope {  // better looking, explains more than `{` by itself
//         int x = 3;
//         y = y + x;
//     }
//
#ifndef NOOP
    #define NOOP ((void)0)
#endif
#define blockscope NOOP;


//=//// "ATTEMPT" AND "UNTIL" CONSTRUCTS //////////////////////////////////=//
//
// This is a fun trick that brings a little bit of the ATTEMPT and UNTIL loop
// functionality from Ren-C into C.
//
// The `attempt` macro is a loop that runs its body just once, and then
// evaluates the `then` or `else` clause (if present):
//
//     attempt {
//         ... some code ...
//         if (condition) { break; }  /* exit attempt, run "else" clause */
//         if (condition) { continue; }  /* exit attempt, run "then" clause */
//         if (condition) { again; }  /* jump to attempt and run it again */
//         ... more code ...
//     }
//     then {  /* optional then clause */
//        ... code to run if no break happened ...
//     }
//     else {  /* optional else clause (must have then clause to use else) */
//        ... code to run if a break happened ...
//     }
//
// It doesn't do anything you couldn't do with defining some goto labels.
// But if you have B breaks and C continues and A agains, you don't have to
// type the label names ((B + 1) + (C + 1) + (A + 1)) times.  And you don't
// have to worry about coming up with the names for those labels!
//
// The `until` macro is a negated sense while loop that also is able to have
// compatibility with the `then` and `else` clauses.
//
// BUT NOTE: Since the macros define variables tracking whether the `then`
// clause should run or not, and whether an `again` should signal continuing
// to run...this can only be used in one scope at a time.  To use more than
// once in a function, define another scope.  Also, you can't use an `else`
// clause without a `then` clause.

#define attempt \
    bool run_then_ = false;  /* as long as run_then_ is false, keep going */ \
    bool run_again_ = false;  /* if run_again_, don't set run_then_ */ \
    for (; not run_then_; \
        run_again_ ? (run_again_ = false), true  /* again doesn't exit loop */ \
        : (run_then_ = true))  /* normal continue, exits the loop */

#define until(condition) \
    bool run_then_ = false; \
    bool run_again_ = false; \
    for (; run_again_ ? (run_again_ = false), true :  /* skip condition */ \
        (condition) ? (run_then_ = true, false) : true; )

#define then  if (run_then_)
#define again  { run_again_ = true; continue; }


//=//// C FUNCTION TYPE (__cdecl) /////////////////////////////////////////=//
//
// Note that you *CANNOT* cast something like a `void *` to (or from) a
// function pointer.  Pointers to functions are not guaranteed to be the same
// size as to data, in either C or C++.  A compiler might count the number of
// functions in your program, find less than 255, and use bytes for function
// pointers:
//
//   http://stackoverflow.com/questions/3941793/
//
// So if you want something to hold either a function pointer or a data
// pointer, you have to implement that as a union...and know what you're doing
// when writing and reading it.
//
// 1. __cdecl is a Microsoft-specific thing that probably isn't really needed
//   in modern compilers, that presume cdecl.  Consider just dropping it:
//
//     http://stackoverflow.com/questions/3404372/
//
//
#if defined(_WIN32)  // 32-bit or 64-bit windows
    typedef void (__cdecl CFunction)(void);  // __cdecl is kind of outdated [1]
#else
    typedef void (CFunction)(void);
#endif

#define Apply_Cfunc(cfunc, ...)  /* make calls clearer at callsites */ \
    (*(cfunc))(__VA_ARGS__)


//=//// TESTING IF A NUMBER IS FINITE /////////////////////////////////////=//
//
// C89 and C++98 had no standard way of testing for if a number was finite or
// not.  Windows and POSIX came up with their own methods.  Finally it was
// standardized in C99 and C++11:
//
// http://en.cppreference.com/w/cpp/numeric/math/isfinite
//
// The name was changed to `isfinite()`.  And conforming C99 and C++11
// compilers can omit the old versions, so one cannot necessarily fall back on
// the old versions still being there.  Yet the old versions don't have
// isfinite(), so those have to be worked around here as well.
//

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L // C99 or later
    #define FINITE isfinite
#elif CPLUSPLUS_11  // C++11 or later
    #define FINITE isfinite
#elif defined(__MINGW32__) || defined(__MINGW64__)
    #define FINITE isfinite // With --std==c++98 MinGW still has isfinite
#elif defined(_WIN32)  // 32-bit or 64-bit windows
    #define FINITE _finite // The usual answer for Windows
#else
    #define FINITE finite // The usual answer for POSIX
#endif


//=//// STATIC ASSERT LVALUE TO HELP EVIL MACRO USAGE /////////////////////=//
//
// Macros are generally bad, but especially bad if they use their arguments
// more than once...because if that argument has a side-effect, they will
// have that side effect more than once.
//
// However, checked builds will not inline functions.  Some code is run so
// often that not defining it in a macro leads to excessive cost in these
// checked builds, and "evil macros" which repeat arguments are a pragmatic
// solution to factoring code in these cases.  You just have to be careful
// to call them with simple references.
//
// Rather than need to give mean-sounding names like XXX_EVIL_MACRO() to
// these macros (which doesn't have any enforcement), this lets the C++
// build ensure the argument is assignable (an lvalue).
//
// 1. Double-parentheses needed to force reference qualifiers for decltype.
//
#if CPLUSPLUS_11
    #define STATIC_ASSERT_LVALUE(x) \
        static_assert( \
            std::is_lvalue_reference<decltype((x))>::value, /* [1] */ \
            "must be lvalue reference")
#else
    #define STATIC_ASSERT_LVALUE(x) NOOP
#endif


//=//// MEMORY POISONING and POINTER CORRUPTING ///////////////////////////=//
//
// If one wishes to indicate a region of memory as being "off-limits", modern
// tools like Address Sanitizer allow instrumented builds to augment reads
// from memory to check to see if that region is in a blacklist.
//
// These "poisoned" areas are generally sub-regions of valid malloc()'d memory
// that contain bad data.  Yet they cannot be free()d because they also
// contain some good data.  (Or it is merely desirable to avoid freeing and
// then re-allocating them for performance reasons, yet a checked build still
// would prefer to intercept accesses as if they were freed.)
//
// Also, in order to overwrite a pointer with garbage, the historical method
// of using 0xBADF00D or 0xDECAFBAD is formalized in Corrupt_Pointer_If_Debug.
// This makes the instances easier to find and standardizes how it is done.
// Special choices are made for 0xF4EEF4EE to indicate a freed thing, and
// 0x5AFE5AFE to indicate an allocated thing.
//
// 1. <IMPORTANT>: Address sanitizer's memory poisoning must not have two
//    threads both poisoning/unpoisoning the same addresses at the same time.
//
// 2. @HostileFork wrote a tiny C++ "poor man's memory poisoner" that uses
//    XOR to poison bits and then unpoison them back.  This might be useful
//    to instrument C++-based builds on platforms that did not have address
//    sanitizer (if that ever becomes interesting).
//
//        http://blog.hostilefork.com/poison-memory-without-asan/
//
#if __has_feature(address_sanitizer)
    #include <sanitizer/asan_interface.h>

    #define ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__ ((no_sanitize_address))

    #define Poison_Memory_If_Sanitize(reg, mem_size) \
        ASAN_POISON_MEMORY_REGION(reg, mem_size)  // one thread at a time [1]

    #define Unpoison_Memory_If_Sanitize(reg, mem_size) \
        ASAN_UNPOISON_MEMORY_REGION(reg, mem_size)  // one thread at a time [1]
#else
    #define ATTRIBUTE_NO_SANITIZE_ADDRESS  // cheap approaches possible [2]

    #define Poison_Memory_If_Sanitize(reg, mem_size)    NOOP
    #define Unpoison_Memory_If_Sanitize(reg, mem_size)  NOOP
#endif


#if NO_RUNTIME_CHECKS
    #define Corrupt_Pointer_If_Debug(p)                 NOOP
    #define Corrupt_Function_Pointer_If_Debug(p)        NOOP
#elif NO_CPLUSPLUS_11
    #define Corrupt_Pointer_If_Debug(p) \
        ((p) = p_cast(void*, cast(uintptr_t, 0xDECAFBAD)))

    #define Corrupt_Function_Pointer_If_Debug(p) \
        ((p) = 0)  // is there any way to do this generically in C?

    #define FreeCorrupt_Pointer_Debug(p) \
        ((p) = p_cast(void*, cast(uintptr_t, 0xF4EEF4EE)))

    #define Is_Pointer_Corrupt_Debug(p) \
        ((p) == p_cast(void*, cast(uintptr_t, 0xDECAFBAD)))
#else
    template<class T>
    INLINE void Corrupt_Pointer_If_Debug(T* &p)
      { p = p_cast(T*, cast(uintptr_t, 0xDECAFBAD)); }

    #define Corrupt_Function_Pointer_If_Debug Corrupt_Pointer_If_Debug

    template<class T>
    INLINE void FreeCorrupt_Pointer_Debug(T* &p)
      { p = p_cast(T*, cast(uintptr_t, 0xF4EEF4EEE)); }

    template<class T>
    INLINE bool Is_Pointer_Corrupt_Debug(T* p)
      { return (p == p_cast(T*, cast(uintptr_t, 0xDECAFBAD))); }
#endif


//=//// TYPE ENSURING HELPERS /////////////////////////////////////////////=//
//
// It's useful when building macros to make inline functions that just check
// a type, for example:
//
//      INLINE Foo* ensure_foo(Foo* f) { return f; }
//
// This has the annoying property that you have to write that function and
// put it somewhere.  But also, there's a problem with constness... if you
// want to retain it, you will need two overloads in C++ (and C does not
// have overloading).
//
// This introduces a simple `ensure` construct:
//
//      void *p = ensure(Flex*, s);
//
// 1. Because ensure is a no-op in non-checked builds, it does no casting in
//    the checked builds.  It only validates the type is convertible, and
//    then passes it through as its original self!  So if you say something
//    like `ensure(const foo*, bar)` and bar is a pointer to a mutable foo,
//    it will be valid...but pass the mutable bar as-is.
//
// 2. There was a macro for `ensureNullptr(p) = xxx` which did a runtime
//    check that a pointer was already nulled before assigning.  It turned
//    out templates specialize with nullptr, so `ensure(nullptr, p) = xxx`
//    happens to work out as an overloading of this construct, even though
//    it's rather different as a runtime value check instead of a compile-time
//    type check.  Avoids needing another name.
//
#if NO_CPLUSPLUS_11
    #define ensure(T,v) (v)
#else
    template<typename V, typename T>
    constexpr V const& ensure_impl(V const& v) {
        static_assert(
            std::is_convertible<V,T>::value, "ensure() failed"
        );
        return v;  // doesn't coerce to type T [1]
    }
    template<typename V, nullptr_t>  // runtime check of nullptr [2]
    V & ensure_impl(V & v) {
        assert(v == nullptr);
        return v;  // doesn't coerce to type T [1]
    }
    #define ensure(T,v) (ensure_impl<decltype(v),T>(v))
#endif


//=//// "POSSIBLY" NON-ASSERT, and "UNNECESSARY" //////////////////////////=//
//
// Comments often carry information about when something may be true:
//
//     int i = Get_Integer(...);  // i may be < 0
//
// `possibly` is a no-op construct which makes sure the expression you pass
// it compiles, but doesn't do anything with it.
//
//     int i = Get_Integer(...);
//     possibly(i < 0);
//
// Separating it out like that may provide a better visual flow (e.g. the
// comment might have made a line overlong), but also it's less likely to
// get out of date because it checks that the expression is well-formed.
//
// `unnecessary` is another strange construct of this sort, where you can
// put some code that people might think you have to write--but don't.  This
// helps cue people into realizing that the omission was intentional, with
// the advantage of showing the precise code they might think they need.
//
// `dont` exists to point out something you really *shouldn't* do, not
// because it's unnecessary or redundant, but because it would break things.
//
#if NO_CPLUSPLUS_11
    #define possibly(expr)  NOOP
    #define unnecessary(expr)  NOOP
    #define dont(expr)  NOOP
#else
    #define possibly(expr) \
        static_assert(std::is_convertible<decltype((expr)), bool>::value, \
            "possibly() expression must be convertible to bool")

    #define unnecessary(expr) \
        static_assert(std::is_same<decltype((void)(expr)), void>::value, "")

    #define dont(expr) \
        static_assert(std::is_same<decltype((void)(expr)), void>::value, "")
#endif


//=//// MARK UNUSED VARIABLES /////////////////////////////////////////////=//
//
// Used in coordination with the `-Wunused-variable` setting of the compiler.
// While a simple cast to void is what people usually use for this purpose,
// there's some potential for side-effects with volatiles:
//
// http://stackoverflow.com/a/4030983/211160
//
// The tricks suggested there for avoiding it seem to still trigger warnings
// as compilers get new ones, so assume that won't be an issue.  As an
// added check, this gives the UNUSED() macro "teeth" in C++11:
//
// http://codereview.stackexchange.com/q/159439
//
// Though the version here is more verbose, it uses the specializations to
// avoid excessive calls to memset() in the checked build.
//
// 1. We do not do Corrupt_If_Debug() with static analysis, because that would
//    make variables look like they had been assigned to the static analyzer.
//    It should use its own notion of when things are "garbage" (e.g. this
//    allows reporting of use of unassigned values from inline functions.)

#define USED(x) \
    ((void)(x))

#if NO_RUNTIME_CHECKS || DEBUG_STATIC_ANALYZING // [1]

    #define Corrupt_If_Debug(x)  NOOP

    #define UNUSED(x) \
        ((void)(x))

#elif NO_CPLUSPLUS_11
    #include <string.h>

    // See definition of Cell for why casting to void* is needed.
    // (Mem_Fill() macro that does this is not defined for %c-enhanced.h)
    //
    #define Corrupt_If_Debug(x) \
        memset(cast(void*, &(x)), 0xBD, sizeof(x));

    #define UNUSED(x) \
        ((void)(x))
#else
    #include <cstring>  // for memset

    // Introduce some variation in the runtimes based on something that is
    // deterministic about the build.  Whether you're using clang works.
    //
  #if defined(__clang__)
    #define CORRUPT_IF_DEBUG_SEED 5  // e.g. fifth corrupt pointer is zero
    #define CORRUPT_IF_DEBUG_DOSE 11
  #else
    #define CORRUPT_IF_DEBUG_SEED 0  // e.g. first corrupt pointer is zero
    #define CORRUPT_IF_DEBUG_DOSE 7
  #endif

    // Pointer, set to spam or 0 (faster than memset() template)
    //
    template<
        typename T,
        typename std::enable_if<
            std::is_pointer<T>::value
        >::type* = nullptr
    >
    void Corrupt_If_Debug(T& ref) {
        static uint_fast8_t countdown = CORRUPT_IF_DEBUG_SEED;
        if (countdown == 0) {
            ref = nullptr;  // nullptr occasionally, deterministic
            countdown = CORRUPT_IF_DEBUG_DOSE;
        }
        else {
            Corrupt_Pointer_If_Debug(ref); // corrupt other half of the time
            --countdown;
        }
    }

    // Integer/bool/float, set to spam or 0 (faster than memset() template)
    //
    template<
        typename T,
        typename std::enable_if<
            not std::is_pointer<T>::value
            and std::is_arithmetic<T>::value
        >::type* = nullptr
    >
    void Corrupt_If_Debug(T& ref) {
        static uint_fast8_t countdown = CORRUPT_IF_DEBUG_SEED;
        if (countdown == 0) {
            ref = static_cast<T>(0);  // false/0 occasionally, deterministic
            countdown = CORRUPT_IF_DEBUG_DOSE;
        }
        else {
            ref = static_cast<T>(12345678); // garbage the rest of the time
            --countdown;
        }
    }

    // Generalized memset() template for Corrupt_If_Debug()
    //
    // 1. This memset technique could be applied to all types, but we can
    //    dodge the cost of calling memset() with specializations above,
    //    and give easier-to-recognize "that's corrupt!" values as well.
    //
    // 2. It's unsafe to memory fill an arbitrary C++ class by value with
    //    garbage bytes, because they can have extra vtables and such--you
    //    can overwrite private compiler data.  But this is a C codebase
    //    which uses just a few C++ features.  If you don't have virtual
    //    methods then is_standard_layout<> should be true, and the memset()
    //    shouldn't be a problem...
    //
    // 3. See definition of Cell and Mem_Set() for why casting to void* is
    //    needed.  (Mem_Set() macro that is not defined for %c-enhanced.h)
    //
    template<
        typename T,
        typename std::enable_if<
            not std::is_pointer<T>::value  // could work, but see [1]
            and not std::is_arithmetic<T>::value  // could work, but see [1]
        >::type* = nullptr
    >
    void Corrupt_If_Debug(T& ref) {
        static_assert(
            std::is_standard_layout<T>::value,  // would break C++ [1]
            "Cannot memset() a C++ struct or class that's not standard layout"
        );
        static uint_fast8_t countdown = CORRUPT_IF_DEBUG_SEED;
        if (countdown == 0) {
            memset(cast(void*, &ref), 0, sizeof(T));  // void cast needed [1]
            countdown = CORRUPT_IF_DEBUG_DOSE;
        }
        else {
            memset(cast(void*, &ref), 189, sizeof(T));  // void cast needed [1]
            --countdown;
        }
    }

    // We want to be able to write UNUSED() for things that aren't used
    // even if they are RValues and can't be corrupted, like UNUSED(Bool_ARG(FOO)).
    //
    template<typename T>
    void Unused_Helper(const T& ref)
      { USED(ref); }

    // For UNUSED() on a mutable ref, we fall through to Corrupt_If_Debug().
    //
    template<typename T>
    void Unused_Helper(T& ref)
      { Corrupt_If_Debug(ref); }

    #define UNUSED Unused_Helper
#endif


//=//// SLIGHTLY SAFER MIN AND MAX MACROS IN C++ //////////////////////////=//
//
// The standard definition in C for MIN and MAX uses preprocessor macros, and
// this has fairly notorious problems of double-evaluating anything with
// side-effects:
//
// https://stackoverflow.com/a/3437484
//
// Sadly, there's no C++ template magic for detecting if expressions have side
// effects that we can use to verify that the callsites as compiled in C are
// safe usages MIN and MAX.  This boils down to the fact that in the C++
// function model, "an int is an int", whether it's a literal or if it's the
// result of a function call:
//
// https://stackoverflow.com/questions/50667501/
//
// But what we can do is make a function that takes the duplicated arguments
// and compares them with each other.
//
// 1. It is common for MIN and MAX to be defined in C to macros; and equally
//    common to assume that undefining them and redefining them to something
//    that acts as it does in most codebases is "probably ok".  :-/
//
// 2. As mentioned above, no magic exists at time of writing that can help
//    enforce T1 and T2 didn't arise from potential-side-effect expressions.
//    `consteval` in C++20 can force compile-time evaluation, but that would
//    only allow MIN(10, 20).  Putting this here in case some future trickery
//    comes along.
//
// 3. In order to make it as similar to the C as possible, we make MIN and
//    MAX macros so they can be #undef'd or redefined (as opposed to just
//    naming the helper templated functions MIN and MAX).
//
#undef MIN  // common for these to be defined [1]
#undef MAX
#if NO_CPLUSPLUS_11 || NO_RUNTIME_CHECKS
    #define MIN(a,b) (((a) < (b)) ? (a) : (b))
    #define MAX(a,b) (((a) > (b)) ? (a) : (b))
#else
    template <
        typename T1,
        typename T2,
        typename std::enable_if<true>::type* = nullptr  // no magic checks [2]
    >
    inline auto cpp_min_helper(  // Note: constexpr can't assert in C++11
        T1 a, T1 aa, T2 b, T2 bb
    ) -> typename std::common_type<T1, T2>::type{
        assert(a == aa);
        assert(b == bb);
        return (a < b) ? a : b;
    }

    template <
        typename T1,
        typename T2,
        typename std::enable_if<true>::type* = nullptr  // no magic checks [2]
    >
    inline auto cpp_max_helper(  // Note: constexpr can't assert in C++11
        T1 a, T1 aa, T2 b, T2 bb
    ) -> typename std::common_type<T1, T2>::type{
        assert(a == aa);
        assert(b == bb);
        return (a > b) ? a : b;
    }

    #define MIN(a,b)  cpp_min_helper((a), (a), (b), (b))  // use macros [3]
    #define MAX(a,b)  cpp_max_helper((a), (a), (b), (b))
#endif


//=//// BYTE STRINGS VS UNENCODED CHARACTER STRINGS ///////////////////////=//
//
// With UTF-8 Everywhere, the term "length" of a string refers to its number
// of codepoints, while "size" returns to the number of bytes (in the size_t
// sense of the word).  This makes the byte-based C function `strlen()`
// something of a misnomer.
//
// To address this issue, we define `strsize()`.  Besides having a name that
// helps emphasize it returns a byte count, it is also made polymorphic to
// accept unsigned character pointers as well as signed ones.  To do this in
// C it has to use a cast that foregoes type checking.  But the C++ build
// checks that only `const char*` and `const unsigned char*` are passed.
// (Strict aliasing rules permit casting between pointers to char types.)
//
// We also include some convenience functions for switching between char*
// and unsigned char*, from:
//
// http://blog.hostilefork.com/c-casts-for-the-masses/

#include <string.h>  // for strlen() etc, but also defines `size_t`

#if CPLUSPLUS_11
    INLINE size_t strsize(const char *cp)
      { return strlen(cp); }

    INLINE size_t strsize(const unsigned char *bp)
      { return strlen((const char*)bp); }
#else
    #define strsize(bp) \
        strlen((const char*)bp)
#endif

#if NO_RUNTIME_CHECKS
    /* These [S]tring and [B]inary casts are for "flips" between a 'char *'
     * and 'unsigned char *' (or 'const char *' and 'const unsigned char *').
     * Being single-arity with no type passed in, they are succinct to use:
     */
    #define s_cast(b)       ((char *)(b))
    #define cs_cast(b)      ((const char *)(b))
    #define b_cast(s)       ((unsigned char *)(s))
    #define cb_cast(s)      ((const unsigned char *)(s))
#else
    /* We want to ensure the input type is what we thought we were flipping,
     * particularly not the already-flipped type.  Instead of type_traits, 4
     * functions check in both C and C++ (here only during Debug builds):
     */
    INLINE unsigned char *b_cast(char *s)
      { return (unsigned char*)s; }

    INLINE const unsigned char *cb_cast(const char *s)
      { return (const unsigned char*)s; }

    INLINE char *s_cast(unsigned char *s)
      { return (char*)s; }

    INLINE const char *cs_cast(const unsigned char *s)
      { return (const char*)s; }
#endif


//=//// PREPROCESSOR ARGUMENT COUNT (w/MSVC COMPATIBILITY TWEAK) //////////=//
//
// While the external C API can be compiled with a C89 compiler (if you're
// willing to put rebEND at the tail of every variadic call), the core has
// committed to variadic macros.
//
// It can be useful to know the count of a __VA_ARGS__ list.  There are some
// techniques floating around that should work in MSVC, but do not.  This
// appears to work in MSVC.
//
// https://stackoverflow.com/a/5530998
//
// You can use this to implement optional parameters, e.g. the following will
// invoke F_1(), F_2(), F_3() etc. based on how many parameters it receives:
//
//    #define F(...) PP_CONCAT(SOMETHING_, PP_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define PP_EXPAND(x) x  // required for MSVC in optional args, see link above

#define PP_CONCAT_IMPL(A, B) A##B
#define PP_CONCAT(A, B) PP_CONCAT_IMPL(A, B)

#define PP_NARGS_IMPL(x1,x2,x3,x4,x5,x6,x7,x8,x9,x10,N,...) N  // 0 won't work
#define PP_NARGS(...) \
    PP_EXPAND(PP_NARGS_IMPL(__VA_ARGS__,10,9,8,7,6,5,4,3,2,1,0))

#endif  // !defined(C_ENHANCED_H)


//=//// PREVENT NULL ASSIGNMENTS /////////////////////////////////////////=//
//
// This came in handly for a debugging scenario.  But because it uses deep
// voodoo to accomplish its work (like overloading -> and &), it interferes
// with more important applications of that voodoo.  So it shouldn't be used
// on types that depend on that (like Cell pointers).
//
#if (! CHECK_NEVERNULL_TYPEMACRO)
    #define NeverNull(type) \
        type
#else
    template <typename P>
    class NeverNullEnforcer {  // named so error message hints what's wrong
        typedef typename std::remove_pointer<P>::type T;
        P p;

      public:
        NeverNullEnforcer () : p () {}
        NeverNullEnforcer (P & p) : p (p) {
            assert(p != nullptr);
        }
        T& operator*() { return *p; }
        P operator->() { return p; }
        operator P() { return p; }
        P operator= (const P rhs) {  // if it returned reference, loses check
            assert(rhs != nullptr);
            this->p = rhs;
            return p;
        }
    };

    #define NeverNull(type) \
        NeverNullEnforcer<type>

    template<class P>
    INLINE void Corrupt_Pointer_If_Debug(NeverNull(P) &nn)
        { Corrupt_Pointer_If_Debug(nn.p); }

    template<class P>
    INLINE bool Is_Pointer_Corrupt_Debug(NeverNull(P) &nn)
        { return Is_Pointer_Corrupt_Debug(nn.p); }

  #if (! DEBUG_STATIC_ANALYZING)
    template<class P>
    INLINE void Corrupt_If_Debug(NeverNull(P) &nn)
        { Corrupt_Pointer_If_Debug(nn.p); }
  #endif
#endif


//=//// OPTIONAL TRICK FOR BOOLEAN COERCIBLE TYPES ////////////////////////=//
//
// This is a light wrapper class that uses a trick to provide limited
// functionality in the vein of `std::optional` and Rust's `Option`:
//
//     Option(char*) abc = "abc";
//     Option(char*) xxx = nullptr;
//
//     if (abc)
//        printf("abc is truthy, so `unwrap abc` is safe!\n")
//
//     if (xxx)
//        printf("XXX is falsey, so don't `unwrap xxx`...\n")
//
//     char* s1 = abc;                  // **compile time error
//     Option(char*) s2 = abc;          // legal
//
//     char* s3 = unwrap xxx;           // **runtime error
//     char* s4 = maybe xxx;            // gets nullptr out
//
// The trick is that in a plain C build, it doesn't use a wrapper class at all.
// It falls back on the natural boolean coercibility of the standalone type.
// Hence you can only use this with things like pointers, integers or enums
// where 0 means no value.  If used in the C++ build with smart pointer
// classes, they must be boolean coercible, e.g. `operator bool() const {...}`
//
// Comparison is lenient, allowing direct comparison to the contained value.
//
// 1. Uppercase Option() is chosen vs. option(), to keep `option` available
//    as a variable name, and to better fit the new DataType NamingConvention.
//
// 2. This needs special handling in %make-headers.r to recognize the format.
//    See the `typemacro_parentheses` rule.
//
// 3. Because we want this to work in plain C, we can't take advantage of a
//    default construction to a zeroed value.  But we also can't disable the
//    default constructor, because we want to be able to default construct
//    structures with members that are Option().  :-(
//
// 4. While the combinatorics may seem excessive with repeating the equality
//    and inequality operators, this is the way std::optional does it too.
//
// 5. To avoid the need for parentheses and give a "keyword" look to the
//    `unwrap` and `maybe` operators they are defined as putting a global
//    variable on the left of an output stream operator.  The variable holds
//    a dummy class which only implements the extraction.
//
#if (! CHECK_OPTIONAL_TYPEMACRO)
    #define Option(T) T
    #define unwrap
    #define maybe
#else
    template<typename T>
    struct OptionWrapper {
        T wrapped;

        OptionWrapper () = default;  // garbage, or 0 if global [2]

        template <typename U>
        OptionWrapper (U something) : wrapped (something)
          {}

        template <typename X>
        OptionWrapper (const OptionWrapper<X>& other) : wrapped (other.wrapped)
          {}

        operator uintptr_t() const  // so it works in switch() statements
          { return cast(uintptr_t, wrapped); }

        explicit operator T()  // must be an *explicit* cast
          { return wrapped; }

        explicit operator bool() {
           // explicit exception in if https://stackoverflow.com/q/39995573/
           return wrapped ? true : false;
        }
    };

    template<typename L, typename R>
    bool operator==(const OptionWrapper<L>& left, const OptionWrapper<R>& right)
        { return left.wrapped == right.wrapped; }

    template<typename L, typename R>
    bool operator==(const OptionWrapper<L>& left, R right)
        { return left.wrapped == right; }

    template<typename L, typename R>
    bool operator==(L left, const OptionWrapper<R>& right)
        { return left == right.wrapped; }

    template<typename L, typename R>
    bool operator!=(const OptionWrapper<L>& left, const OptionWrapper<R>& right)
        { return left.wrapped != right.wrapped; }

    template<typename L, typename R>
    bool operator!=(const OptionWrapper<L>& left, R right)
        { return left.wrapped != right; }

    template<typename L, typename R>
    bool operator!=(L left, const OptionWrapper<R>& right)
        { return left != right.wrapped; }

    struct UnwrapHelper {};
    struct MaybeHelper {};

    template<typename T>
    T operator<<(  // [5]
        const UnwrapHelper& left,
        const OptionWrapper<T>& option
    ){
        UNUSED(left);
        assert(option.wrapped);  // non-null pointers or int/enum checks != 0
        return option.wrapped;
    }

    template<typename T>
    T operator<<(  // [5]
        const MaybeHelper& left,
        const OptionWrapper<T>& option
    ){
        UNUSED(left);
        return option.wrapped;
    }

    constexpr UnwrapHelper g_unwrap_helper = {};
    constexpr MaybeHelper g_maybe_helper = {};

    #define Option(T) OptionWrapper<T>
    #define unwrap g_unwrap_helper <<      // [5]
    #define maybe g_maybe_helper <<        // [5]

    template<class P>
    INLINE void Corrupt_Pointer_If_Debug(Option(P) &option)
      { Corrupt_Pointer_If_Debug(option.wrapped); }

    template<class P>
    INLINE bool Is_Pointer_Corrupt_Debug(Option(P) &option)
      { return Is_Pointer_Corrupt_Debug(option.wrapped); }

  #if (! DEBUG_STATIC_ANALYZING)
    template<class P>
    INLINE void Corrupt_If_Debug(Option(P) &option)
      { Corrupt_If_Debug(option.wrapped); }
  #endif
#endif


//=//// SINK (OUTPUT ARGUMENTS) AND "NEED" ////////////////////////////////=//
//
// The idea behind a Sink() is to be able to mark on a function's interface
// when a function argument passed by pointer is intended as an output.
//
// This has benefits of documentation, and can also be given some teeth by
// scrambling the memory that the pointer points at (so long as it isn't an
// "in-out" parameter).  But it also applied in CHECK_CELL_SUBCLASSES, by
// enforcing "covariance" for input parameters, and "contravariance" for
// output parameters.
//
// 1. If CHECK_CELL_SUBCLASSES is enabled, then the inheritance heirarchy has
//    Atom at the base, with Element at the top.  Since what Elements can
//    contain is more constrained than what Atoms can contain, this means
//    you can pass Atom* to Element*, but not vice-versa.
//
//    However, when you have a Sink(Element) parameter instead of an Element*,
//    the checking needs to be reversed.  You are -writing- an Element, so
//    the receiving caller can pass an Atom* and it will be okay.  But if you
//    were writing an Atom, then passing an Element* would not be okay, as
//    after the initialization the Element could hold invalid states.
//
//    We use "SFINAE" to selectively enable the upside-down hierarchy, based
//    on the `std::is_base_of<>` type trait.
//
// 2. The original implementation was simpler, by just doing the corruption
//    at the moment of construction.  But this faced a problem:
//
//        bool some_function(Sink(char*) out, char* in) { ... }
//
//        if (some_function(&ptr, ptr)) { ...}
//
//    If you corrupt the data at the address the sink points to, you can
//    actually be corrupting the value of a stack variable being passed as
//    another argument before it's calculated as an argument.  So deferring
//    the corruption after construction is necessary.  It's a bit tricky
//    in terms of the handoffs and such.
//
//    While this could be factored, function calls aren't inlined in the
//    debug build, so given the simplicity of the code it's just repeated.
//
// !!! Review: the copy-and-swap idiom doesn't seem to be very helpful here,
// as we aren't dealing with exceptions and self-assignment has to be handled
// manually due to the handoff of the corruption_pending flag.
//
//    https://stackoverflow.com/questions/3279543/
//
#if DEBUG_USE_SINKS
    template<typename T, bool sink>
    struct NeedWrapper {
        T* p;
        mutable bool corruption_pending;  // can't corrupt on construct [2]

      //=//// TYPE ALIASES ////////////////////////////////////////////////=//

        using MT = typename std::remove_const<T>::type;

        template<typename U>  // for CHECK_CELL_SUBCLASSES [1]
        using IfReverseInheritable = typename std::enable_if<
            std::is_same<U,T>::value or std::is_base_of<U,T>::value
        >::type;

      //=//// CONSTRUCTORS ////////////////////////////////////////////////=//

        NeedWrapper() = default;  // or MSVC warns making Option(Sink(Value))

        NeedWrapper(nullptr_t) {
            p = nullptr;
            corruption_pending = false;
        }

        NeedWrapper (const NeedWrapper<T,sink>& other) {
            p = other.p;
            corruption_pending = p and (other.corruption_pending or sink);
            other.corruption_pending = false;
        }

        template<typename U, IfReverseInheritable<U>* = nullptr>
        NeedWrapper(U* u) {
            p = u_cast(T*, u);
            corruption_pending = p and sink;
        }

        template<typename U, bool B, IfReverseInheritable<U>* = nullptr>
        NeedWrapper(const NeedWrapper<U, B>& other) {
            p = u_cast(T*, other.p);
            corruption_pending = p and (other.corruption_pending or sink);
            other.corruption_pending = false;
        }

      //=//// ASSIGNMENT //////////////////////////////////////////////////=//

        NeedWrapper& operator=(nullptr_t) {
            p = nullptr;
            corruption_pending = false;
            return *this;
        }

        NeedWrapper& operator=(const NeedWrapper<T,sink>& other) {
            if (this != &other) {  // self-assignment possible
                p = other.p;
                corruption_pending = p and (other.corruption_pending or sink);
                other.corruption_pending = false;
            }
            return *this;
        }

        template<typename U, IfReverseInheritable<U>* = nullptr>
        NeedWrapper& operator=(const NeedWrapper& other) {
            if (this != &other) {  // self-assignment possible
                p = other.p;
                corruption_pending = p and (other.corruption_pending or sink);
                other.corruption_pending = false;
            }
            return *this;
        }

        template<typename U, IfReverseInheritable<U>* = nullptr>
        NeedWrapper& operator=(U* other) {
            p = u_cast(T*, other);
            corruption_pending = p and sink;
            return *this;
        }

      //=//// OPERATORS ///////////////////////////////////////////////////=//

        operator bool () const { return p != nullptr; }

        operator T* () const {
            if (corruption_pending) {
                Corrupt_If_Debug(*const_cast<MT*>(p));
                corruption_pending = false;
            }
            return p;
        }

        T* operator->() const {
            if (corruption_pending) {
                Corrupt_If_Debug(*const_cast<MT*>(p));
                corruption_pending = false;
            }
            return p;
        }

      //=//// DESTRUCTOR //////////////////////////////////////////////////=//

        ~NeedWrapper() {
            if (corruption_pending)
                Corrupt_If_Debug(*const_cast<MT*>(p));
        }
    };

    template<typename T, bool B>
    void Corrupt_If_Debug(NeedWrapper<T, B>& wrapper) {
        Corrupt_If_Debug(wrapper.p);  // asked to corrupt the actual pointer
        wrapper.corruption_pending = false;  // destructor would crash
    }

    template<typename T, bool B>
    void Unused_Helper(NeedWrapper<T, B>& wrapper) {
        Corrupt_If_Debug(wrapper.p);  // asked to corrupt the actual pointer
        wrapper.corruption_pending = false;  // destructor would crash
    }

    template<typename T, bool B>
    void Unused_Helper(const NeedWrapper<T, B>& wrapper) {
        USED(wrapper.p);
    }

    #define SinkTypemacro(T) \
        NeedWrapper<T, true>

    #define NeedTypemacro(TP) \
        NeedWrapper<typename std::remove_pointer<TP>::type, false>
#else
    #define SinkTypemacro(T) T *
    #define NeedTypemacro(TP) TP
#endif


//=//// Init() Variant Of Sink() //////////////////////////////////////////=//
//
// When we write initialization routines, the output is technically a Sink(),
// in the sense that it's intended to be overwritten.  But Sink() has a cost
// since it corrupts the cell.  It's unlikely to help catch bugs with
// initialization, because Init_Xxx() routines are typically not code with
// any branches in it that might fail to overwrite the cell.
//
// This defines Init() as typically just being Need(), to check to make sure
// that the caller's pointer can store the cell subclass, without doing any
// corrupting of the cell.
//
// BUT if you want to double check the initializations, it should still work
// to make Init() equivalent to Sink() and corrupt the cell.  It's not likely
// to catch any bugs...but it's not likely to hurt either.
//
#if DEBUG_USE_SINKS
    #if DEBUG_CHECK_INIT_SINKS
        #define InitTypemacro(T) NeedWrapper<T, true>  // sink=true
    #else
        #define InitTypemacro(T) NeedWrapper<T, false>  // sink=false
    #endif
#else
    #define InitTypemacro(T) T *
#endif


//=//// CORRUPT UNUSED FIELDS /////////////////////////////////////////////=//
//
// It would seem that structs which don't use their payloads could just leave
// them uninitialized...saving time on the assignments.
//
// Unfortunately, this is a technically gray area in C.  If you try to
// copy the memory of that cell (as cells are often copied), it might be a
// "trap representation".  Reading such representations to copy them...
// even if not interpreted... is undefined behavior:
//
//   https://stackoverflow.com/q/60112841
//   https://stackoverflow.com/q/33393569/
//
// Odds are it would still work fine if you didn't zero them.  However,
// compilers will warn you--especially at higher optimization levels--if
// they notice uninitialized values being used in copies.  This is a bad
// warning to turn off, because it often points out defective code.
//
// So to play it safe and be able to keep warnings on, fields are zeroed out.
// But it's set up as its own independent flag, so that someone looking
// to squeak out a tiny bit more optimization could turn this off in a
// release build.  It would save on a few null assignments.
//
// (In release builds, the fields are assigned 0 because it's presumably a
// fast value to assign as an immediate.  In checked builds, they're assigned
// a corrupt value because it's more likely to cause trouble if accessed.)
//
#if ASSIGN_UNUSED_FIELDS
  #if RUNTIME_CHECKS
    #define Corrupt_Unused_Field(ref)  Corrupt_If_Debug(ref)
  #else
    #define Corrupt_Unused_Field(ref)  ((ref) = 0)
  #endif
#else
    #define Corrupt_Unused_Field(ref)  NOOP
#endif
