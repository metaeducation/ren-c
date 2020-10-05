//
//  File: %reb-c.h
//  Summary: "General C definitions and constants"
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
// This is a set of definitions and helpers which are generically useful for
// any project which is trying to implement portable C across a variety of
// old and new compilers/systems.
//
// Though R3-Alpha was written to mostly comply with ANSI C89, it needs 64-bit
// integers, and used the `long long` data type.  To suppress warnings in a
// C89 build related to this, use `-Wno-long-long`.  Additionally, `//` style
// comments are used, which were commonly supported by C compilers even before
// the C99 standard.  But that means this code can't be used with the switches
// `--pedantic --std=c89` (unless you convert or strip out all the comments).
//
// The Ren-C branch advanced Rebol to be able to build under C99=>C11 and
// C++98=>C++17 as well.  Some extended checks are provided for these macros
// if building under various versions of C++.  Also, C99 definitions are
// taken advantage of if they are available.
//

#ifndef REB_C_H_1020_0304  // "include guard" allows multiple #includes
#define REB_C_H_1020_0304  // numbers in case REB_C_H defined elsewhere


//=//// EXPECTS <stdint.h> OR "pstdint.h" SHIM INCLUDED ///////////////////=//
//
// Rebol's initial design targeted C89 and old-ish compilers on a variety of
// systems.  A comment here said:
//
//     "One of the biggest flaws in the C language was not
//      to indicate bitranges of integers. So, we do that here.
//      You cannot 'abstractly remove' the range of a number.
//      It is a critical part of its definition."
//
// Once C99 arrived, the file <stdint.h> offered several basic types, and
// basically covered the needs:
//
// http://en.cppreference.com/w/c/types/integer
//
// The code was changed to use either the C99 types -or- a portable shim that
// could mimic the types (with the same names) on older compilers.  It should
// be included before %reb-c.h is included.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Note: INT32_MAX and INT32_C can be missing in C++ builds on some older
// compilers without __STDC_LIMIT_MACROS and __STDC_CONSTANT_MACROS:
//
// https://sourceware.org/bugzilla/show_bug.cgi?id=15366
//
// You can run into this since pstdint.h falls back on stdint.h if it
// thinks it can.  Put those on the command line if needed.
//
// !!! One aspect of pstdint.h is that it considers 64-bit "optional".
// Some esoteric platforms may have a more hidden form of 64-bit support,
// e.g. this case from R3-Alpha for "Windows VC6 nonstandard typing":
//
//     #if (defined(_MSC_VER) && (_MSC_VER <= 1200))
//         typedef _int64 i64;
//         typedef unsigned _int64 u64;
//         #define I64_C(c) c ## I64
//         #define U64_C(c) c ## U64
//     #endif
//
// If %pstdint.h isn't trying hard enough for an unsupported platform of
// interest to get 64-bit integers, then patches should be made there.
//


//=//// EXPECTS <stdbool.h> OR "pstdbool.h" SHIM INCLUDED /////////////////=//
//
// Historically Rebol used TRUE and FALSE uppercase macros, but so long as
// C99 has added bool to the language, there's not much point in being
// compatible with codebases that have `char* true = "Spandau";` or similar
// in them.  So Rebol can use `true` and `false.
//


//=//// EXPECTS <assert.h> TO BE INCLUDED, PATCH BUG //////////////////////=//
//
// There is a bug in older GCC where the assert macro expands arguments
// unnecessarily.  Since Rebol tries to build on fairly old systems, this
// patch corrects the issue:
//
// https://sourceware.org/bugzilla/show_bug.cgi?id=18604
//

#if !defined(assert)
    #error "Include <assert.h> before including reb-c.h"
#endif

#if !defined(NDEBUG) && defined(__GLIBC__) && defined(__GLIBC_MINOR__) \
    && (__GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 23))

    #undef assert
  #if !defined(__GNUC__) || defined (__STRICT_ANSI__)
    #define assert(expr) \
        ((expr) \
            ? __ASSERT_VOID_CAST (0) \
            : __assert_fail (#expr, __FILE__, __LINE__, __ASSERT_FUNCTION))
  #else
    #define assert(expr) \
        ({ \
          if (expr) \
            ; /* empty */ \
          else \
            __assert_fail (#expr, __FILE__, __LINE__, __ASSERT_FUNCTION); \
        })
    #endif

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

#ifdef __cplusplus
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
// Because the goal of Ren-C is ultimately to be built with C, the C++ build
// is just for static analysis and debug checks.  This means there's not much
// value in trying to tailor reduced versions of the checks to old ANSI C++98
// compilers, so the "C++ build" is an "at least C++11 build".
//
// Besides being a little less verbose to use, testing via a #define allows
// override when using with Microsoft Visual Studio via a command line
// definition.  For some reason they didn't bump the version number from 1997
// (even by MSVC 2017!!!)
//
#if defined(__cplusplus) && __cplusplus >= 201103L
    #define CPLUSPLUS_11
#elif defined(STDC_VERSION) && STDC_VERSION >= 201112L
    #define C_11
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
#ifdef CPLUSPLUS_11
    #include <type_traits>
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
#ifdef CPLUSPLUS_11
    #define STATIC_ASSERT(cond) \
        static_assert((cond), #cond) // callsite has semicolon, see C trick
#else
    #define STATIC_ASSERT(cond) \
        struct GlobalScopeNoopTrick // https://stackoverflow.com/q/53923706
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
//    #ifdef __cplusplus
//        // C++ build adds protection to the const input case
//        const Member* Get_Member(const Object *o) {...}
//    #endif
//

#ifdef __cplusplus
    #define const_if_c
#else
    #define const_if_c const
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
//    #ifdef __cplusplus
//        // C++ build adds protection to the const input case
//        const Member* Get_Member(const Object *o) {...}
//    #endif
//

#ifdef __cplusplus
    #define const_if_c
#else
    #define const_if_c const
#endif


//=//// CASTING MACROS ////////////////////////////////////////////////////=//
//
// The following code and explanation is from "Casts for the Masses (in C)":
//
// http://blog.hostilefork.com/c-casts-for-the-masses/
//
// But debug builds don't inline functions--not even no-op ones whose sole
// purpose is static analysis.  This means the cast macros add a headache when
// stepping through the debugger, and also they consume a measurable amount
// of runtime.  Hence we sacrifice cast checking in the debug builds...and the
// release C++ builds on Travis are relied upon to do the proper optimizations
// as well as report any static analysis errors.  See DEBUG_CHECK_CASTS.
//
// !!! C++14 gcc release builds seem to trigger bad behavior on cast() to
// a CFUNC*, and non-C++14 builds are allowing cast of `const void*` to
// non-const `char` with plain `cast()`.  Investigate as time allows.

#if !defined(__cplusplus)
    /* These macros are easier-to-spot variants of the parentheses cast.
     * The 'm_cast' is when getting [M]utablity on a const is okay (RARELY!)
     * Plain 'cast' can do everything else (except remove volatile)
     * The 'c_cast' helper ensures you're ONLY adding [C]onst to a value
     */
    #define m_cast(t,v)     ((t)(v))
    #define cast(t,v)       ((t)(v))
    #define c_cast(t,v)     ((t)(v))
    /*
     * Q: Why divide roles?  A: Frequently, input to cast is const but you
     * "just forget" to include const in the result type, gaining mutable
     * access.  Stray writes to that can cause even time-traveling bugs, with
     * effects *before* that write is made...due to "undefined behavior".
     */
#elif !defined(CPLUSPLUS_11)
    /* Well-intentioned macros aside, C has no way to enforce that you can't
     * cast away a const without m_cast. C++98 builds can do that, at least:
     */
    #define m_cast(t,v)     const_cast<t>(v)
    #define cast(t,v)       ((t)(v))
    #define c_cast(t,v)     const_cast<t>(v)
#else
    /* __cplusplus >= 201103L has C++11's type_traits, where we get some
     * actual power.  cast becomes a reinterpret_cast for pointers and a
     * static_cast otherwise.  We ensure c_cast added a const and m_cast
     * removed one, and that neither affected volatility.
     */
    template<typename T, typename V>
    T m_cast_helper(V v) {
        static_assert(!std::is_const<T>::value,
            "invalid m_cast() - requested a const type for output result");
        static_assert(std::is_volatile<T>::value == std::is_volatile<V>::value,
            "invalid m_cast() - input and output have mismatched volatility");
        return const_cast<T>(v);
    }
    /* construct if possible for ptr to ptr casting (non-class source) */
    template<typename T, typename V,
        typename std::enable_if<
            !std::is_class<V>::value
            && (std::is_pointer<V>::value || std::is_pointer<T>::value)
            && std::is_constructible<T, V>::value
        >::type* = nullptr>
                T cast_helper(V v) { return T {v}; }
    /* reinterpret_cast for all other ptr-to-ptr (non-class source) */
    template<typename T, typename V,
        typename std::enable_if<
            !std::is_class<V>::value
            && (std::is_pointer<V>::value || std::is_pointer<T>::value)
            && !std::is_constructible<T, V>::value
        >::type* = nullptr>
                T cast_helper(V v) { return reinterpret_cast<T>(v); }
    /* static_cast for non-pointer to non-pointer casting (non-class source) */
    template<typename T, typename V,
        typename std::enable_if<
            !std::is_class<V>::value
            && (!std::is_pointer<V>::value && !std::is_pointer<T>::value)
        >::type* = nullptr>
                T cast_helper(V v) { return static_cast<T>(v); }
    /* use static_cast on all classes, to go through their cast operators */
    template<typename T, typename V,
        typename std::enable_if<
            std::is_class<V>::value
        >::type* = nullptr>
                T cast_helper(V v) { return static_cast<T>(v); }
    template<typename T, typename V>
    T c_cast_helper(V v) {
        static_assert(!std::is_const<T>::value,
            "invalid c_cast() - did not request const type for output result");
        static_assert(std::is_volatile<T>::value == std::is_volatile<V>::value,
            "invalid c_cast() - input and output have mismatched volatility");
        return const_cast<T>(v);
    }
    #define m_cast(t, v)    m_cast_helper<t>(v)
    #define cast(t, v)      cast_helper<t>(v)
    #define c_cast(t, v)    c_cast_helper<t>(v)
#endif


//=//// nullptr SHIM FOR C, C++98 /////////////////////////////////////////=//
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
// Because libRebol hinges on a premise of making the internal NULL signifier
// interface as a C NULL pointer, and hinges on variadics, this is a problem.
// Rather than introduce a "new" abstraction or macro, this adds a shim of
// C++11's `nullptr` to C++98, and a simple macro to C.
//
// This also means that NULL can be used in comments for the Rebol concept,
// as opposed to the C idea (though NULLED may be clearer, depending on
// context).  Either way, when discussing C's "0 pointer", say `nullptr`.
//

#if !defined(__cplusplus)
    //
    // Plain C, nullptr is not a keyword.
    //
    #define nullptr cast(void*, 0)

#elif defined(CPLUSPLUS_11) //...or above
    //
    // http://en.cppreference.com/w/cpp/language/nullptr
    // is defined as `using nullptr_t = decltype(nullptr);` in <cstddef>
    //
    #include <cstddef>
    using std::nullptr_t;
#else
    // C++98 shim from "Effective C++": https://stackoverflow.com/a/44517878
    //
    // Note: Some "newer old" C++ compilers had awareness that nullptr would
    // be added to the standard, and raise warnings by default if one tries
    // to make this definition, even when building as C++98.  To disable such
    // warnings, you would need something like `-Wno-c++0x-compat`
    //
    const                         /* this is a const object...     */
    class nullptr_t
    {
    public:
       template<class T>          /* convertible to any type       */
       operator T*() const        /* of null non-member            */
          { return 0; }           /* pointer...                    */

       template<class C, class T> /* or any type of null           */
          operator T C::*() const /* member pointer...             */
          { return 0; }

    private:
       void operator&() const;    /* Can't take address of nullptr */

    } nullptr = {};               /* and whose name is nullptr     */
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


//=//// ALIGNMENT SIZE ////////////////////////////////////////////////////=//
//
// Data alignment is a complex topic, which has to do with the fact that the
// following kind of assignment can be slowed down or fail entirely on
// many platforms:
//
//    char *cp = (char*)malloc(sizeof(double) + 1);
//    double *dp = (double*)(cp + 1);
//    *dp = 6.28318530718
//
// malloc() guarantees that the pointer it returns is aligned to store any
// fundamental type safely.  But skewing that pointer to not be aligned in
// a way for that type (e.g. by a byte above) means assignments and reads of
// types with more demanding alignment will fail.  e.g. a double often needs
// to read/write to pointers where `((uintptr_t)ptr % sizeof(double)) == 0`
//
// (Note: Often, not always.  For instance, Linux systems with System V ABI
// for i386 are permitted to use 4 byte boundaries instead of 8 byte for
// doubles unless you use `-malign-double`.  See page 28 of the spec:
//
// http://www.uclibc.org/docs/psABI-i386.pdf
//
// Windows 32-bit compilers seem to also permit 4 bytes.  WebAssembly does
// not seem to work when doubles are on 4 byte boundaries, however.)
// 
// The C standard does not provide a way to know what the largest fundamental
// type is, even though malloc() must be compatible with it.  So if one is
// writing one's own allocator to give back memory blocks, it's necessary to
// guess.  We guess the larger of size of a double and size of a void*, though
// note this may not be enough for absolutely any type in the compiler:
//
//    "In Visual C++, the fundamental alignment is the alignment that's
//    required for a double, or 8 bytes. In code that targets 64-bit
//    platforms, it's 16 bytes.)
//

#define ALIGN_SIZE \
    (sizeof(double) > sizeof(void*) ? sizeof(double) : sizeof(void*))

#define ALIGN(s,a) \
    (((s) + (a) - 1) & ~((a) - 1)) // !!! this macro not used anywhere ATM


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
#ifdef TO_WINDOWS
    typedef void (__cdecl CFUNC)(void);
#else
    typedef void (CFUNC)(void);
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
#elif defined(CPLUSPLUS_11) // C++11 or later
    #define FINITE isfinite
#elif defined(__MINGW32__) || defined(__MINGW64__)
    #define FINITE isfinite // With --std==c++98 MinGW still has isfinite
#elif defined(TO_WINDOWS)
    #define FINITE _finite // The usual answer for Windows
#else
    #define FINITE finite // The usual answer for POSIX
#endif


//=//// PREVENT NULL ASSIGNMENTS /////////////////////////////////////////=//
//
// This came in handly for a debugging scenario.  But because it uses deep
// voodoo to accomplish its work (like overloading -> and &), it interferes
// with more important applications of that voodoo.  So it shouldn't be used
// on types that depend on that (like REBVAL pointers).
//

#if !defined(CPLUSPLUS_11) || defined(NDEBUG)
    #define NEVERNULL(type) \
        type
#else
    template <typename P>
    class Never_Null {  // named so error message hints what's wrong
        typedef typename std::remove_pointer<P>::type T;
        P p;

      public:
        Never_Null () : p () {}
        Never_Null (P & p) : p (p) {
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

    #define NEVERNULL(type) \
        Never_Null<type>
#endif


//=//// MEMORY POISONING and POINTER TRASHING /////////////////////////////=//
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
// of using 0xBADF00D or 0xDECAFBAD is formalized with TRASH_POINTER_IF_DEBUG.
// This makes the instances easier to find and standardizes how it is done.
// Special choices are made for 0xF4EEF4EE to indicate a freed thing, and
// 0x5AFE5AFE to indicate an allocated thing.

#if __has_feature(address_sanitizer)
    #include <sanitizer/asan_interface.h>

    #define ATTRIBUTE_NO_SANITIZE_ADDRESS __attribute__ ((no_sanitize_address))

    // <IMPORTANT> Address sanitizer's memory poisoning must not have two
    // threads both poisoning/unpoisoning the same addresses at the same time.

    #define POISON_MEMORY(reg, mem_size) \
        ASAN_POISON_MEMORY_REGION(reg, mem_size)

    #define UNPOISON_MEMORY(reg, mem_size) \
        ASAN_UNPOISON_MEMORY_REGION(reg, mem_size)
#else
    // !!! @HostileFork wrote a tiny C++ "poor man's memory poisoner" that
    // uses XOR to poison bits and then unpoison them back.  This might be
    // useful to instrument C++-based DEBUG builds on platforms that did not
    // have address sanitizer (if that ever becomes interesting).
    //
    // http://blog.hostilefork.com/poison-memory-without-asan/

    #define ATTRIBUTE_NO_SANITIZE_ADDRESS

    #define POISON_MEMORY(reg, mem_size) \
        NOOP

    #define UNPOISON_MEMORY(reg, mem_size) \
        NOOP
#endif

#ifdef NDEBUG
    #define TRASH_POINTER_IF_DEBUG(p) \
        NOOP

    #define TRASH_CFUNC_IF_DEBUG(T,p) \
        NOOP
#else
    #if defined(__cplusplus) // needed even if not C++11
        template<class T>
        inline static void TRASH_POINTER_IF_DEBUG(T* &p) {
            p = reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD));
        }

        template<class T>
        inline static void SAFETRASH_POINTER_IF_DEBUG(T* &p) {
            p = reinterpret_cast<T*>(static_cast<uintptr_t>(0x5AFE5AFE));
        }

        template<class T>
        inline static void FREETRASH_POINTER_IF_DEBUG(T* &p) {
            p = reinterpret_cast<T*>(static_cast<uintptr_t>(0xF4EEF4EEE));
        }

        template<class T>
        inline static bool IS_POINTER_TRASH_DEBUG(T* p) {
            return (
                p == reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD))
            );
        }

        template<class T>
        inline static bool IS_POINTER_SAFETRASH_DEBUG(T* p) {
            return (
                p == reinterpret_cast<T*>(static_cast<uintptr_t>(0x5AFE5AFE))
            );
        }

        template<class T>
        inline static bool IS_POINTER_FREETRASH_DEBUG(T* p) {
            return (
                p == reinterpret_cast<T*>(static_cast<uintptr_t>(0xF4EEF4EE))
            );
        }

      #if defined(CPLUSPLUS_11)
        template<class P>
        inline static void TRASH_POINTER_IF_DEBUG(Never_Null<P> &p) {
            p = reinterpret_cast<P>(static_cast<uintptr_t>(0xDECAFBAD));
        }

        template<class P>
        inline static bool IS_POINTER_TRASH_DEBUG(Never_Null<P> &p) {
            return (
                p == reinterpret_cast<P>(static_cast<uintptr_t>(0xDECAFBAD))
            );
        }
      #endif
    #else
        #define TRASH_POINTER_IF_DEBUG(p) \
            ((p) = cast(void*, cast(uintptr_t, 0xDECAFBAD)))

        #define SAFETRASH_POINTER_IF_DEBUG(p) \
            ((p) = cast(void*, cast(uintptr_t, 0x5AFE5AFE)))

        #define FREETRASH_POINTER_IF_DEBUG(p) \
            ((p) = cast(void*, cast(uintptr_t, 0xF4EEF4EE)))

        #define IS_POINTER_TRASH_DEBUG(p) \
            ((p) == cast(void*, cast(uintptr_t, 0xDECAFBAD)))

        #define IS_POINTER_SAFETRASH_DEBUG(p) \
            ((p) == cast(void*, cast(uintptr_t, 0x5AFE5AFE)))

        #define IS_POINTER_FREETRASH_DEBUG(p) \
            ((p) == cast(void*, cast(uintptr_t, 0xF4EEF4EE)))
    #endif

    // C functions must be cast to the right type, even in C (no void*)

    #define TRASH_CFUNC_IF_DEBUG(T,p) \
        ((p) = cast(T, cast(uintptr_t, 0xDECAFBAD)))

    #define IS_CFUNC_TRASH_DEBUG(T,p) \
        ((p) == cast(T, cast(uintptr_t, 0xDECAFBAD)))
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

#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    #define UNUSED(x) \
        ((void)(x))
#else
    #include <cstring>  // for memset

    // Can't trash the variable if it's not an lvalue.  So for the basic
    // SFINAE overload, just cast void.  Do this also for cases that are
    // lvalues, but we don't really know how to "trash" them.
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
    void UNUSED(T && v) {
        ((void)(v));
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
    void UNUSED(T && v) {
        static bool zero = false;
        if (zero)
            v = nullptr; // do null half the time, deterministic
        else
            TRASH_POINTER_IF_DEBUG(v); // trash the other half of the time
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
    void UNUSED(T && v) {
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
    void UNUSED(T && v) {
        memset(&v, 123, sizeof(TRR));
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


//=//// SHORTHAND FOR DEEPLY NESTED FIELDS ////////////////////////////////=//
//
// If a field is deeply nested in a structure and referred to many times in
// C, it is possible to create a local pointer to that field and then use
// a dereference to that pointer (which works for both assigning and reads).
// Since it's a `const` pointer, the compiler should optimize it out.
//
// But there's a risk that one might say `if (shorthand)` and test for the
// nullness or non-nullness of the shorthand vs. of the thing pointed to.
// This adds a check in the C++ build that you always say `if (*shorthand)`
//

#if !defined(CPLUSPLUS_11)
    #define SHORTHAND(name,ref,type) \
        type* const name = &ref
#else
    template <typename T>
    class Must_Dereference {  // named so error message hints what's wrong
        T &ref;

      public:
        Must_Dereference (T& ref) : ref (ref) {}
        T & operator*() { return ref; }
        operator bool () = delete;  // !!! Could this static_assert()?
    };

    #define SHORTHAND(name,ref,type) \
        Must_Dereference<type> name = ref
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
// checks via templates that only `const char*` and `const unsigned char*`
// are passed.  (Note that strict aliasing rules permit casting between
// pointers to char types.)
//
// We also include some convenience functions for switching between char*
// and unsigned char*, from:
//
// http://blog.hostilefork.com/c-casts-for-the-masses/

#include <string.h>  // for strlen() etc, but also defines `size_t`

#ifdef CPLUSPLUS11
    template<
        typename T,
        typename std::enable_if<
            std::is_same<T, char>::value
            || std::is_same<T, unsigned char>::value
        >
    >
    size_t strsize(const T *bp)
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
    inline static unsigned char *b_cast(char *s)
        { return (unsigned char*)s; }

    inline static const unsigned char *cb_cast(const char *s)
        { return (const unsigned char*)s; }

    inline static char *s_cast(unsigned char *s)
        { return (char*)s; }

    inline static const char *cs_cast(const unsigned char *s)
        { return (const char*)s; }
#endif


#endif  // !defined(REB_C_H_1020_0304)
