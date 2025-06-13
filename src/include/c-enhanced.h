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


//=//// NOOP a.k.a. VOID GENERATOR ////////////////////////////////////////=//
//
// VOID would be a more purposeful name, but Windows headers define that
// for the type (as used in types like LPVOID)
//
#ifndef NOOP
    #define NOOP ((void)0)
#endif


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


//=//// TYPE_TRAITS IN C++11 AND ABOVE ///////////////////////////////////=//
//
// One of the most powerful tools you can get from allowing a C codebase to
// compile as C++ comes from type_traits:
//
// http://en.cppreference.com/w/cpp/header/type_traits
//
// This is essentially an embedded query language for types, allowing one to
// create compile-time errors for any C construction that isn't being used
// in the way one might want.
//
// 1. The type trait is_explicitly_convertible() is useful, but it was taken
//    out of GCC.  This uses a simple implementation that was considered to
//    be buggy for esoteric reasons, but is good enough for our purposes.
//
//    https://stackoverflow.com/a/16944130
//
//    Note this is not defined in the `std::` namespace since it is a shim.
//
#if CPLUSPLUS_11
    #include <type_traits>

  namespace shim {  // [1]
    template<typename _From, typename _To>
    struct is_explicitly_convertible : public std::is_constructible<_To, _From>
      { };
  }
#endif


//=//// INCLUDE NEEDFUL LIBRARY HEADERS //////////////////////////////////=//
//
// The "needful" library is being factored out of the Ren-C codebase as an
// independent library.  It's still being worked out what parts of this file
// will wind up in it, but these are files that are likely to be in it.
//
// NOTE: Needful introduces creative TypeMacros like Option(T).  So we need
// special handling in %make-headers.r to recognize the format...
//
// ...see the `typemacro_parentheses` rule.
//

#include "needful/configure-needful.h"

#include "needful/needful-asserts.h"

#include "needful/needful-casts.h"

#include "needful/needful-ensure.h"

#include "needful/needful-poison.h"

#include "needful/needful-corruption.h"

#include "needful/needful-option.h"

#include "needful/needful-sinks.h"

#include "needful/needful-loops.h"

#include "needful/needful-nevernull.h"


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


//=//// TYPE LIST HELPER //////////////////////////////////////////////////=//
//
// Type lists allow checking if a type is in a list of types at compile time:
//
//     template<typename T>
//     void process(T value) {
//         using NumericTypes = c_type_list<int, float, double>;
//         static_assert(NumericTypes::contains<T>(), "T must be numeric");
//         // ...
//     }
//
// 1. Due to wanting C++11 compatibility, it must be `List::contains<T>()` with
//    the parentheses, which is a bit of a wart.  C++14 or higher is needed
//    for variable templates, which allows `List::contains<T>` without parens:
//
//        struct contains_impl {  /* instead of calling this `contains` */
//            enum { value = false };
//        };
//        template<typename T>
//        static constexpr bool contains = contains_impl<T>::value;
//
//    Without that capability, best we can do is to construct an instance via
//    a default constructor (the parentheses), and then have a constexpr
//    implicit boolean coercion for that instance.
//
#if CPLUSPLUS_11
    template<typename... Ts>
    struct c_type_list {
        template<typename T>
        struct contains {
            enum { value = false };

            // Allow usage without ::value in most contexts [1]
            constexpr operator bool() const { return value; }
        };
    };

    template<typename T1, typename... Ts>
    struct c_type_list<T1, Ts...> {  // Specialization for non-empty lists
        template<typename T>
        struct contains {
            enum { value = std::is_same<T, T1>::value or
                        typename c_type_list<Ts...>::template contains<T>() };

            // Allow usage without ::value in most contexts [1]
            constexpr operator bool() const { return value; }
        };
    };
#endif


#endif  // !defined(C_ENHANCED_H)
