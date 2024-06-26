//
//  File: %c-enhanced.h
//  Summary: "Tools for C with enhanced features if built as C++11 or higher"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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

#if !defined(DEBUG_CHECK_OPTIONALS)
    #define DEBUG_CHECK_OPTIONALS 0
#endif

#if !defined(DEBUG_CHECK_NEVERNULL)
    #define DEBUG_CHECK_NEVERNULL 0
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

#if defined(__cplusplus) && (! defined(__HAIKU__))
    //
    // HaikuOS's GCC 2.95 apparently not only doesn't define the keywords for
    // C++, but #include <iso646.h> has no effect.
    //
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
    #define CPLUSPLUS_11 1
  #else
    #define CPLUSPLUS_11 0
  #endif
#endif

#if !defined(C_11)
  #if defined(STDC_VERSION) && STDC_VERSION >= 201112L
    #define C_11 1
  #else
    #define C_11 0
  #endif
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

  namespace shim {
    template<typename _From, typename _To>
    struct is_explicitly_convertible : public std::is_constructible<_To, _From>
      { };
  }
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
//         fail ("x is too big"); // compiler may warn about no return value
//     }
//
// One way of annotating to say this is okay is on the caller, with DEAD_END:
//
//     int foo(int x) {
//         if (x < 1020)
//             return x + 304;
//         fail ("x is too big");
//         DEAD_END; // our warning-suppression macro for applicable compilers
//     }
//
// DEAD_END is just a no-op in compilers that don't have the feature of
// suppressing the warning--which can often mean they don't have the warning
// in the first place.
//
// Another macro we define is ATTRIBUTE_NO_RETURN.  This can be put on the
// declaration site of a function like `fail()` itself, so the callsites don't
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
//    Member* Get_Member(const_if_c Object* o) {...}
//
//    #if CPLUSPLUS_11
//        // C++ build adds protection to the const input case
//        const Member* Get_Member(const Object *o) {...}
//    #endif
//
// Note: If writing a simple wrapper like this whose only purpose is to
// pipe the const-correct output result from the input's constness, another
// trick is to use `c_cast()` which is a "const-preserving cast".
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


//=//// CASTING MACROS ////////////////////////////////////////////////////=//
//
// This code is based on ideas in "Casts for the Masses (in C)":
//
//   http://blog.hostilefork.com/c-casts-for-the-masses/
//
// It provides easier-to-spot variants of the parentheses cast, which when
// built under C++ can be made to implement the macros with safer and
// narrower implementations:
//
//   * Plain 'cast' is reinterpret_cast for pointers, static_cast otherwise
//   * The 'm_cast' is when getting [M]utablity on a const is okay
//   * The 'c_cast' helper ensures you're ONLY adding [C]onst to a value
//
// Additionally there is x_cast(), for cases where you don't know if your
// input pointer is const or not, and want to cast to a mutable pointer.
// C++ doesn't let you use old-style casts to accomplish this, so it has to
// be done using two casts and type_traits magic.
//
// These casts should not cost anything at runtime--unless non-constexpr
// helpers are invoked.  Those are only used in the codebase for debug
// features in the C++ builds, and release builds do not use them.
//
// 1. The C preprocessor doesn't know about templates, so it parses things
//    like FOO(something<a,b>) as taking "something<a" and "b>".  This is a
//    headache for implementing the macros, but also if a macro produces a
//    comma and gets passed to another macro.  To work around it, we wrap
//    the product of the macro containing commas in parentheses.
//
#define u_cast(T,v) \
    ((T)(v))  // unchecked cast, use e.g. when casting a fresh allocation

#if (! CPLUSPLUS_11)
    #define cast(T,v)       ((T)(v))  /* pointer-to-ptr, integral-to-int */
    #define m_cast(T,v)     ((T)(v))  /* add mutability to pointer type only */
    #define x_cast(T,v)     ((T)(v))  /* pointer cast that drops mutability */
    #define c_cast(T,v)     ((T)(v))  /* mirror constness of input on output */
    #define p_cast(T,v)     ((T)(v))  /* non-pointer to pointer */
    #define i_cast(T,v)     ((T)(v))  /* non-integral to integral */
    #define rr_cast(T,v)    ((T)(v))  /* simplifying remove-reference cast */
#else
    template<typename V, typename T>
    struct cast_helper {
        template<typename V_ = V, typename T_ = T>
        static constexpr typename std::enable_if<
            not shim::is_explicitly_convertible<V_,T_>::value and (
                (std::is_arithmetic<V_>::value or std::is_enum<V_>::value)
                and (std::is_arithmetic<T_>::value or std::is_enum<T_>::value)
            ),
        T>::type convert(V_ v) { return static_cast<T>(v); }

        template<typename V_ = V, typename T_ = T>
        static constexpr typename std::enable_if<
            not shim::is_explicitly_convertible<V_,T_>::value and (
                std::is_pointer<V_>::value and std::is_pointer<T_>::value
            ),
        T>::type convert(V_ v) { return reinterpret_cast<T>(v); }

        template<typename V_ = V, typename T_ = T>
        static constexpr typename std::enable_if<
            shim::is_explicitly_convertible<V_,T_>::value,
        T>::type convert(V_ v) { return static_cast<T>(v); }
    };

    template<typename V>
    struct cast_helper<V,void>
      { static void convert(V v) { (void)(v);} };  // void can't be constexpr

    #define cast(T,v) \
        (cast_helper<typename std::remove_reference< \
            decltype(v)>::type, T>::convert(v))  // outer parens [1]

    template<typename T, typename V>
    constexpr T m_cast_helper(V v) {
        static_assert(not std::is_const<T>::value,
            "invalid m_cast() - requested a const type for output result");
        static_assert(std::is_volatile<T>::value == std::is_volatile<V>::value,
            "invalid m_cast() - input and output have mismatched volatility");
        return const_cast<T>(v);
    }

    #define m_cast(T,v) \
        m_cast_helper<T>(v)

    template<typename TQP>
    struct x_cast_pointer_helper {
        typedef typename std::remove_pointer<TQP>::type TQ;
        typedef typename std::add_const<TQ>::type TC;
        typedef typename std::add_pointer<TC>::type type;
    };

    template<typename T>
    struct x_cast_helper {
        typedef typename std::conditional<
            std::is_pointer<T>::value,
            typename x_cast_pointer_helper<T>::type,
            T
        >::type type;
    };

    #define x_cast(T,v) \
       (const_cast<T>((typename x_cast_helper<T>::type)(v)))

    template<typename TP, typename VQPR>
    struct c_cast_helper {
        typedef typename std::remove_reference<VQPR>::type VQP;
        typedef typename std::remove_pointer<VQP>::type VQ;
        typedef typename std::remove_pointer<TP>::type T;
        typedef typename std::add_const<T>::type TC;
        typedef typename std::add_pointer<TC>::type TCP;
        typedef typename std::conditional<
            std::is_const<VQ>::value,
            TCP,
            TP
        >::type type;
    };

    #define c_cast(TP,v) \
        (cast_helper< \
            decltype(v), typename c_cast_helper<TP,decltype(v)>::type \
        >::convert(v))  // outer parens [1]

    template<typename TP, typename V>
    constexpr TP p_cast_helper(V v) {
        static_assert(std::is_pointer<TP>::value,
            "invalid p_cast() - target type must be pointer");
        static_assert(not std::is_pointer<V>::value,
            "invalid p_cast() - source type can't be pointer");
        return reinterpret_cast<TP>(static_cast<uintptr_t>(v));
    }

    #define p_cast(TP,v) \
        p_cast_helper<TP>(v)

    template<typename T, typename V>
    constexpr T i_cast_helper(V v) {
        static_assert(std::is_integral<T>::value,
            "invalid i_cast() - target type must be integral");
        static_assert(not std::is_integral<V>::value,
            "invalid i_cast() - source type can't be integral");
        return reinterpret_cast<T>(v);
    }

    #define i_cast(T,v) \
        i_cast_helper<T>(v)

    template<typename V>
    struct rr_cast_helper {
        typedef typename std::conditional<
            std::is_reference<V>::value,
            typename std::remove_reference<V>::type,
            V
        >::type type;
    };

    #define rr_cast(v) \
        static_cast<typename rr_cast_helper<decltype(v)>::type>(v)
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

#if (! CPLUSPLUS_11)
    #define nullptr cast(void*, 0)
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
    #define NOOP \
        ((void)(0))
#endif
#define blockscope NOOP;


//=//// C FUNCTION TYPE (__cdecl) /////////////////////////////////////////=//
//
// Note that you *CANNOT* cast something like a `void *` to (or from) a
// function pointer.  Pointers to functions are not guaranteed to be the same
// size as to data, in either C or C++.  A compiler might count the number of
// functions in your program, find less than 255, and use bytes for function
// pointers:
//
// http://stackoverflow.com/questions/3941793/
//
// So if you want something to hold either a function pointer or a data
// pointer, you have to implement that as a union...and know what you're doing
// when writing and reading it.
//
// For info on the difference between __stdcall and __cdecl:
//
// http://stackoverflow.com/questions/3404372/
//
//
#if defined(_WIN32)  // 32-bit or 64-bit windows
    typedef void (__cdecl CFunction)(void);
#else
    typedef void (CFunction)(void);
#endif


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


//=//// PREVENT NULL ASSIGNMENTS /////////////////////////////////////////=//
//
// This came in handly for a debugging scenario.  But because it uses deep
// voodoo to accomplish its work (like overloading -> and &), it interferes
// with more important applications of that voodoo.  So it shouldn't be used
// on types that depend on that (like Cell pointers).
//

#if (! DEBUG_CHECK_NEVERNULL)
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
#endif


//=//// STATIC ASSERT LVALUE TO HELP EVIL MACRO USAGE /////////////////////=//
//
// Macros are generally bad, but especially bad if they use their arguments
// more than once...because if that argument has a side-effect, they will
// have that side effect more than once.
//
// However, debug builds will not inline functions.  Some code is run so
// often that not defining it in a macro leads to excessive cost in these
// debug builds, and "evil macros" which repeat arguments are a pragmatic
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


//=//// OPTIONAL TRICK FOR BOOLEAN COERCIBLE TYPES ////////////////////////=//
//
// This is a light wrapper class that uses a trick to provide limited
// functionality in the vein of `std::optional` and Rust's `Option`:
//
//     Option(char*) abc = "abc";
//     Option(char*) xxx = nullptr;
//
//     if (abc)
//        printf("abc is truthy, so unwrap(abc) is safe!\n")
//
//     if (xxx)
//        printf("XXX is falsey, so don't unwrap(xxx)...\n")
//
//     char* s1 = abc;                  // **compile time error
//     Option(char*) s2 = abc;          // legal
//
//     char* s3 = unwrap(xxx);          // **runtime error
//     char* s4 = try_unwrap(xxx);      // gets nullptr out
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
#if (! DEBUG_CHECK_OPTIONALS)
    #define Option(T) T
    #define unwrap(v) (v)
    #define try_unwrap(v) (v)
#else
    template<typename T>
    struct OptionWrapper {
        T wrapped;

        OptionWrapper () = default;  // garbage, or 0 if global [2]

        template <typename U>
        OptionWrapper (U something) : wrapped (something) {}

        template <typename X>
        OptionWrapper (OptionWrapper<X> other) : wrapped (other.wrapped) {}

        T unwrap_helper() const {
            assert(wrapped);  // non-null pointers or int/enum checks != 0
            return wrapped;
        }

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
    bool operator==(OptionWrapper<L> left, OptionWrapper<R> right)
        { return left.wrapped == right.wrapped; }

    template<typename L, typename R>
    bool operator==(OptionWrapper<L> left, R right)
        { return left.wrapped == right; }

    template<typename L, typename R>
    bool operator==(L left, OptionWrapper<R> right)
        { return left == right.wrapped; }

    template<typename L, typename R>
    bool operator!=(OptionWrapper<L> left, OptionWrapper<R> right)
        { return left.wrapped != right.wrapped; }

    template<typename L, typename R>
    bool operator!=(OptionWrapper<L> left, R right)
        { return left.wrapped != right; }

    template<typename L, typename R>
    bool operator!=(L left, OptionWrapper<R> right)
        { return left != right.wrapped; }

    #define Option(T) OptionWrapper<T>
    #define unwrap(v) (v).unwrap_helper()
    #define try_unwrap(v) (v).wrapped
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
// then re-allocating them for performance reasons, yet a debug build still
// would prefer to intercept accesses as if they were freed.)
//
// Also, in order to overwrite a pointer with garbage, the historical method
// of using 0xBADF00D or 0xDECAFBAD is formalized in Corrupt_Pointer_If_Debug.
// This makes the instances easier to find and standardizes how it is done.
// Special choices are made for 0xF4EEF4EE to indicate a freed thing, and
// 0x5AFE5AFE to indicate an allocated thing.

#if __has_feature(address_sanitizer)
    #include <sanitizer/asan_interface.h>

    #define ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__ ((no_sanitize_address))

    // <IMPORTANT> Address sanitizer's memory poisoning must not have two
    // threads both poisoning/unpoisoning the same addresses at the same time.

    #define Poison_Memory_If_Sanitize(reg, mem_size) \
        ASAN_POISON_MEMORY_REGION(reg, mem_size)

    #define Unpoison_Memory_If_Sanitize(reg, mem_size) \
        ASAN_UNPOISON_MEMORY_REGION(reg, mem_size)
#else
    // !!! @HostileFork wrote a tiny C++ "poor man's memory poisoner" that
    // uses XOR to poison bits and then unpoison them back.  This might be
    // useful to instrument C++-based DEBUG builds on platforms that did not
    // have address sanitizer (if that ever becomes interesting).
    //
    // http://blog.hostilefork.com/poison-memory-without-asan/

    #define ATTRIBUTE_NO_SANITIZE_ADDRESS

    #define Poison_Memory_If_Sanitize(reg, mem_size) \
        NOOP

    #define Unpoison_Memory_If_Sanitize(reg, mem_size) \
        NOOP
#endif


#ifdef NDEBUG
    #define Corrupt_Pointer_If_Debug(p)                 NOOP
    #define Corrupt_Function_Pointer_If_Debug(p)        NOOP
#elif (! CPLUSPLUS_11)
    #define Corrupt_Pointer_If_Debug(p) \
        ((p) = p_cast(void*, cast(uintptr_t, 0xDECAFBAD)))

    #define Corrupt_Function_Pointer_If_Debug(p) \
        ((p) = 0)  // is there any way to do this generically in C?

    #define SafeCorrupt_Pointer_Debug(p) \
        ((p) = p_cast(void*, cast(uintptr_t, 0x5AFE5AFE)))

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
    INLINE void SafeCorrupt_Pointer_Debug(T* &p)
      { p = p_cast(T*, cast(uintptr_t, 0x5AFE5AFE)); }

    template<class T>
    INLINE void FreeCorrupt_Pointer_Debug(T* &p)
      { p = p_cast(T*, cast(uintptr_t, 0xF4EEF4EEE)); }

    template<class T>
    INLINE bool Is_Pointer_Corrupt_Debug(T* p)
      { return (p == p_cast(T*, cast(uintptr_t, 0xDECAFBAD))); }

    #if DEBUG_CHECK_OPTIONALS
        template<class P>
        INLINE void Corrupt_Pointer_If_Debug(Option(P) &option)
          { Corrupt_Pointer_If_Debug(option.wrapped); }

        template<class P>
        INLINE bool Is_Pointer_Corrupt_Debug(Option(P) &option)
          { return Is_Pointer_Corrupt_Debug(option.wrapped); }
    #endif

    #if DEBUG_CHECK_NEVERNULL
        template<class P>
        INLINE void Corrupt_Pointer_If_Debug(NeverNull(P) &nn)
          { Corrupt_Pointer_If_Debug(nn.p); }

        template<class P>
        INLINE bool Is_Pointer_Corrupt_Debug(NeverNull(P) &nn)
          { return Is_Pointer_Corrupt_Debug(nn.p); }
    #endif
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
//      void *p = ensure(Series*, s);
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
#if (! CPLUSPLUS_11)
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
// avoid excessive calls to memset() in the debug build.
//

#define USED(x) \
    ((void)(x))

#if defined(NDEBUG) || (! CPLUSPLUS_11)
    #include <string.h>

    // See definition of Cell for why casting to void* is needed.
    // (Mem_Fill() macro that does this is not defined for %c-enhanced.h)
    //
    #define Corrupt_If_Debug(x) \
        memset(cast(void*, &(x)), 0xBD, sizeof(x));

    #define UNUSED(x) \
        ((void)(x))
#else
    #define UNUSED Corrupt_If_Debug

    #include <cstring>  // for memset

    // Can't corrupt the variable if it's not an lvalue.  So for the basic
    // SFINAE overload, just cast void.  Do this also for cases that are
    // lvalues, but we don't really know how to "corrupt" them.
    //
    template<
        typename T,
        typename TRR = typename std::remove_reference<T>::type,
        typename std::enable_if<
            !std::is_lvalue_reference<T &&>::value
            || std::is_const<TRR>::value
            || (
                !std::is_pointer<TRR>::value
                && !std::is_arithmetic<TRR>::value
                && !std::is_pod<TRR>::value
            )
        >::type* = nullptr
    >
    void Corrupt_If_Debug(T && v) {
        USED(v);
    }

    // For example: if you have an lvalue reference to a pointer, you can
    // set it to DECAFBAD...which will likely be caught if it's a lie and it
    // is getting used in the debug build.
    //
    template<
        typename T,
        typename TRR = typename std::remove_reference<T>::type,
        typename std::enable_if<
            std::is_lvalue_reference<T &&>::value
            && !std::is_const<TRR>::value
            && std::is_pointer<TRR>::value
        >::type* = nullptr
    >
    void Corrupt_If_Debug(T && v) {
        static bool zero = false;
        if (zero)
            v = nullptr; // do null half the time, deterministic
        else
            Corrupt_Pointer_If_Debug(v); // corrupt the other half of the time
        zero = not zero;
    }

    // Any integral or floating type, set to a spam number.
    //
    template<
        typename T,
        typename TRR = typename std::remove_reference<T>::type,
        typename std::enable_if<
            std::is_lvalue_reference<T &&>::value
            && !std::is_const<TRR>::value
            && std::is_arithmetic<TRR>::value
            && !std::is_pointer<TRR>::value
        >::type* = nullptr
    >
    void Corrupt_If_Debug(T && v) {
        static bool zero = false;
        if (zero)
            v = false; // false/0 half the time, deterministic
        else
            v = true; // true/1 other half the time
        zero = not zero;
    }

    // It's unsafe to memory fill an arbitrary C++ class by value with
    // garbage bytes, because of all the "extra" stuff in them.  You can
    // crash the destructor.  But this is a C codebase which only occasionally
    // uses C++ features in the C++ build.  Most will be "Plain Old Data",
    // so fill those with garbage as well.
    //
    // (Note: this one methodology could be applied to all pod types,
    // including arithmetic and pointers, but this shows how to do it
    // with custom ways and avoids function calls to memset in non-optimized
    // debug builds for most cases.)
    //
    template<
        typename T,
        typename TRR = typename std::remove_reference<T>::type,
        typename std::enable_if<
            std::is_lvalue_reference<T &&>::value
            && !std::is_const<TRR>::value
            && std::is_pod<TRR>::value
            && (
                !std::is_pointer<TRR>::value
                && !std::is_arithmetic<TRR>::value
            )
        >::type* = nullptr
    >
    void Corrupt_If_Debug(T && v) {
        //
        // See definition of Cell for why casting to void* is needed.
        // (Mem_Set() macro that does this is not defined for %c-enhanced.h)
        //
        memset(cast(void*, &v), 123, sizeof(TRR));
    }
#endif


//=//// MIN AND MAX ///////////////////////////////////////////////////////=//
//
// The standard definition in C for MIN and MAX uses preprocessor macros, and
// this has fairly notorious problems of double-evaluating anything with
// side-effects:
//
// https://stackoverflow.com/a/3437484/211160
//
// It is common for MIN and MAX to be defined in C to macros; and equally
// common to assume that undefining them and redefining them to something
// that acts as it does in most codebases is "probably ok".  :-/
//
#undef MIN
#undef MAX
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))


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

#if defined(NDEBUG)
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
