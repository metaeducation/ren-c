//
//  File: %reb-c.h
//  Summary: "General C definitions and constants"
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


//=////////////////////////////////////////////////////////////////////////=//
//
// CPLUSPLUS_11
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Because the goal of Ren-C is ultimately to be built with C, the C++ build
// is just for static analysis and debug checks.  This means there's not much
// value in trying to tailor reduced versions of the checks to old ANSI C++98
// compilers, so the "C++ build" is an "at least C++11 build".
//
// Besides being a little less verbose to use, it allows override when using
// with Microsoft Visual Studio via a command line definition.  For some
// reason they didn't bump the version number from 1997, even by MSVC 2017!!!
//
#if defined(__cplusplus) && __cplusplus >= 201103L
    #define CPLUSPLUS_11
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// FEATURE TESTING AND ATTRIBUTE MACROS
//
//=////////////////////////////////////////////////////////////////////////=//
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


//=////////////////////////////////////////////////////////////////////////=//
//
// INCLUDE TYPE_TRAITS IN C++11 AND ABOVE
//
//=////////////////////////////////////////////////////////////////////////=//
//
// One of the most powerful tools you can get from allowing a C codebase to
// compile as C++ comes from type_traits:
//
// http://en.cppreference.com/w/cpp/header/type_traits
//
// This is essentially an embedded query language for types, allowing one to
// create compile-time errors for any C construction that isn't being used
// in the way one might want.  While some static analysis tools for C offer
// their own plugins for such checks, the prevalance of the C++ standard
// and compilers that implement it make it a perfect tool for checking a C
// codebase on the fly to see if it follows certain rules.
//
#ifdef CPLUSPLUS_11
    #include <type_traits>
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// STATIC ASSERT FOR CHECKING COMPILE-TIME CONDITIONS IN C
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Some conditions can be checked at compile-time, instead of deferred to a
// runtime assert.  This macro triggers an error message at compile time.
// `static_assert` is an arity-2 keyword in C++11 (which was expanded in
// C++17 to have an arity-1 form).  This uses the name `static_assert_c` to
// implement a poor-man's version of the arity-1 form in C, that only works
// inside of function bodies.
//
// !!! This was the one being used, but review if it's the best choice:
//
// http://stackoverflow.com/questions/3385515/static-assert-in-c
// or http://stackoverflow.com/a/809465/211160
//
#ifdef CPLUSPLUS_11
    #define static_assert_c(e) \
        static_assert((e), "compile-time static assert failure")
#else
    #define static_assert_c(e) \
        do {(void)sizeof(char[1 - 2*!(e)]);} while(0)
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// CONDITIONAL C++ NAME MANGLING MACROS
//
//=////////////////////////////////////////////////////////////////////////=//
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


//=////////////////////////////////////////////////////////////////////////=//
//
// CASTING MACROS
//
//=////////////////////////////////////////////////////////////////////////=//
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
// as well as report any static analysis errors.
//
// !!! C++14 gcc release builds seem to trigger bad behavior on cast() to
// a CFUNC*, and non-C++14 builds are allowing cast of `const void*` to
// non-const `char` with plain `cast()`.  Investigate as time allows, but
// in the meantime SYM_FUNC() uses a plain C-style cast.

#if !defined(CPLUSPLUS_11) || !defined(NDEBUG)
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
#elif defined(__cplusplus) /* for gcc -Wundef */ && (__cplusplus < 201103L)
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
    /* reinterpret_cast for pointer to pointer casting (non-class source)*/
    template<typename T, typename V,
        typename std::enable_if<
            !std::is_class<V>::value
            && (std::is_pointer<V>::value || std::is_pointer<T>::value)
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


//=////////////////////////////////////////////////////////////////////////=//
//
// NOOP a.k.a. VOID GENERATOR
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Creating a void value conveniently is useful for a few reasons.  One is
// that it can serve as a NO-OP and suppress a compiler warning you might
// get if you try to use just ';' to do it.  Another is that there is a
// difference between C and C++ in parenthesized expressions, where
// '(foo(), bar())' will return the result of bar in C++ but not in C.
// So such a macro could be written as '(foo(), bar(), NOOP)' to avoid
// leaking the result.
//
// VOID would be a more purposeful name, but Windows headers define that
// for the type (as used in types like LPVOID)
//
#ifndef NOOP
    #define NOOP \
        ((void)(0))
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// <stdint.h> INCLUDE -OR- SHIM FOR PRE-C99 COMPILERS THAT DON'T HAVE IT
//
//=////////////////////////////////////////////////////////////////////////=//
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
// could mimic the types (with the same names) on older compilers.
//

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    #include <stdint.h> // It's C99 or above, use as-is
#elif defined (CPLUSPLUS_11)
    #include <stdint.h> // should also work in conforming C++11 (or later)
#else
    #include "pstdint.h" // use "portable" standard int, by Paul Hsieh et al.

    // Note: INT32_MAX and INT32_C can be missing in C++ builds on some older
    // compilers without __STDC_LIMIT_MACROS and __STDC_CONSTANT_MACROS:
    //
    // https://sourceware.org/bugzilla/show_bug.cgi?id=15366
    //
    // You can run into this since pstdint.h falls back on stdint.h if it
    // thinks it can.  Put those on the command line if needed.

    // !!! One aspect of pstdint.h is that it considers 64-bit "optional".
    // Some esoteric platforms may have a more hidden form of 64-bit support,
    // e.g. this case from R3-Alpha for "Windows VC6 nonstandard typing":
    //
    //     #ifdef WEIRD_INT_64
    //         typedef _int64 i64;
    //         typedef unsigned _int64 u64;
    //         #define I64_C(c) c ## I64
    //         #define U64_C(c) c ## U64
    //     #endif
    //
    // If %pstdint.h isn't trying hard enough for an unsupported platform of
    // interest to get 64-bit integers, then patches should be made there.
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// ALIGNMENT SIZE
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Data alignment is a complex topic, which has to do with the fact that the
// following kind of assignment can be slowed down or fail entirely on
// some platforms:
//
//    char *cp = (char*)malloc(sizeof(double) + 1);
//    double *dp = (double*)(cp + 1);
//    *dp = 6.28318530718
//
// malloc() guarantees that the pointer it returns is aligned to store any
// fundamental type safely.  But skewing that pointer to not be aligned in
// a way for that type (e.g. by a byte above) means assignments and reads of
// types with more demanding alignment will fail.  e.g. a double expects to
// read/write to pointers where `((uintptr_t)ptr % sizeof(double)) == 0`
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


//=////////////////////////////////////////////////////////////////////////=//
//
// BOOLEAN DEFINITION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The C language defines the value 0 as false, while all non-zero things are
// considered logically true.  Yet the language standard mandates that the
// comparison operators (==, !=, >, <, etc) will return either 0 or 1, and
// the C++ language standard defines conversion of its built-in boolean type
// to an integral value as either 0 or 1.
//
// This could be exploited by optimized code *IF* it could truly trust a true
// "boolean" is exactly 0 or 1. But unfortunately, C only standardized an
// actual boolean type in C99 with <stdbool.h>.  Older compilers have to use
// integral types for booleans, and may wind up in situations like this:
//
//     #define REBOOL int
//     int My_Optimized_Function(REBOOL logic) {
//         return logic << 4; // should be 16 if logic is TRUE, 0 if FALSE
//     }
//     int zero_or_sixteen = My_Optimized_Function(flags & SOME_BIT_FLAG);
//
// The caller may feel they are passing something that is validly "truthy" or
// "falsey", yet if the bit flag is shifted at all then the optimization won't
// be able to work.  The type system will not catch the mistake, and hence
// anyone who needs logics to be 0 or 1 must inject code to enforce that
// translation, which the optimizer cannot leave out.
// 
// This code takes advantage of the custom definition with a mode to build in
// that makes assignments to REBOOL reject integers entirely.  It still
// allows testing via if() and the logic operations, but merely disables
// direct assignments or passing integers as parameters to bools:
//
//     REBOOL b = 1 > 2;            // illegal: 1 > 2 is 0 (integer) in C
//     REBOOL b = LOGICAL(1 > 2);   // Ren-C legal form of assignment
//
// The macro LOGICAL() lets you convert any truthy C value to a REBOOL, and
// NOT() lets you do the inverse.  This is better than what was previously
// used often, a (REBOOL)cast_of_expression.  And it makes it much safer to
// use ordinary `&` operations to test for flags, more succinctly even:
//
//     REBOOL b = GET_FLAG(flags, SOME_FLAG_ORDINAL);
//     REBOOL b = !GET_FLAG(flags, SOME_FLAG_ORDINAL);
//
// vs.
//
//     REBOOL b = LOGICAL(flags & SOME_FLAG_BITWISE); // same
//     REBOOL b = NOT(flags & SOME_FLAG_BITWISE);     // 5 less chars
//
// (Bitwise vs. ordinal also permits initializing options by just |'ing them.)
//
// The compile-time checks for enforcing this don't lead to a working binary
// being built.  Hence the source will get out of sync with the check, so a
// CI build should be added to confirm STRICT_BOOL_COMPILER_TEST works.
//

#ifndef HAS_BOOL
    //
    // Some systems define a cpu-optimal BOOL already.  (Of course, all of
    // this should have been built into C in 1970.)  But if they don't, go
    // with whatever the compiler decides `int` is, as it is the default
    // "speedy choice" for modern CPUs
    //
    typedef int BOOL;
#endif

#ifdef STRICT_BOOL_COMPILER_TEST
    //
    // Force type errors on direct assignments of integers to booleans or
    // vice-versa (leading to a broken executable in the process).  Although
    // this catches some errors that wouldn't be caught otherwise, it notably
    // does not notice when a literal 0 is passed to a REBOOL, because that
    // is a valid pointer value.  However, this case is tested for by the
    // enum method of declaration in ordinary non-Windows builds.
    //
    // Use a #define and not a typedef so it can be selectively overridden.
    //
    typedef struct Bool_Dummy { int dummy; } * DUMMYBOOL;
    #define REBOOL DUMMYBOOL
    #define FALSE cast(struct Bool_Dummy*, 0x6466AE99)
    #define TRUE cast(struct Bool_Dummy*, 0x0421BD75)

    #ifdef CPLUSPLUS_11
        //
        // In the C++ build, we can help reduce confusion by making sure that
        // LOGICAL and NOT are only applied to integral types.  Using it on
        // pointers to test for NULL is somewhat unclear for readability
        // at the callsite, and one doesn't want it on floating point.
        //
        // Note: This winds up taking a significant percentage of the time
        // if used in all C++ debug builds, so only enforce it when doing the
        // full STRICT_BOOL_COMPILER_TEST
        //
        template <typename T>
        inline static REBOOL LOGICAL(T x) {
            static_assert(
                std::is_same<T, REBOOL>::value || std::is_integral<T>::value,
                "LOGICAL(x) can only be used on integral types"
            );
            return x ? TRUE : FALSE;
        }
        template <typename T>
        inline static REBOOL NOT(T x) {
            static_assert(
                std::is_same<T, REBOOL>::value || std::is_integral<T>::value,
                "NOT(x) can only be used on integral types"
            );
            return x ? FALSE : TRUE;
        }
    #else
        // dummy substitutions
        #define LOGICAL(x) cast(struct Bool_Dummy*, 0x10200304)
        #define NOT(x) cast(struct Bool_Dummy*, 0x03041020)
    #endif
#else
    #if (defined(FALSE) && (!FALSE)) && (defined(TRUE) && TRUE)

        #if defined(TO_WINDOWS) && !((FALSE == 0) && (TRUE == 1))
            //
            // The Windows API specifically mandates the value of TRUE as 1.
            // If you are compiling on Windows with something that has
            // predefined the constant as some other value, it will be
            // inconsistent...and won't work out.
            //
            #error "Compiler's FALSE != 0 or TRUE != 1, invalid for Win32"
        #else
            // Outside of Win32, assume any C truthy/falsey definition that
            // the compiler favors is all right.
        #endif

        // There's a FALSE and TRUE defined and they are logically false and
        // true respectively, so just use those definitions but make REBOOL
        //
        typedef BOOL REBOOL;

    #elif !defined(FALSE) && !defined(TRUE)
        //
        // An enum-based definition would prohibit the usage of TRUE and FALSE
        // in preprocessor macros, but offer some amount of type safety.
        //
        // http://stackoverflow.com/a/23666263/211160
        //
        // The tradeoff is worth it, but interferes with the hardcoded Windows
        // definitions of TRUE as 1 and FALSE as 0.  So only use it outside
        // of Windows.
        //
        #ifdef TO_WINDOWS
            #define FALSE 0
            #define TRUE 1

            typedef BOOL REBOOL;
        #else
            typedef enum {FALSE = 0, TRUE = !FALSE} REBOOL;
        #endif

    #else
        // TRUE and FALSE are defined but are not their logic meanings.
        //
        #error "Bad TRUE and FALSE definitions in compiler environment"
    #endif

    #define LOGICAL(x) \
        ((x) ? TRUE : FALSE)
    #define NOT(x) \
        ((x) ? FALSE : TRUE)
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// "WIDE" CHARACTER DEFINITION (UCS2)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Consensus about the wchar_t datatype is generally that it's a pre-Unicode
// abstraction that should be avoided unless you absolutely need it.  It
// varies in size by platform...though it is standardized to 2 bytes in size
// on Windows, where there is `#define WCHAR wchar_t`
//
// Some APIs (such as unixodbc) use UCS2 for wide character handling in order
// to be compatible with Windows, vs. using the native wchar_t type.  It thus
// defines SQLWCHAR as an unsigned short integer (itself not *guaranteed* to
// be 16-bits in size).  However, such a definition cannot be used if
// compiling as C++ and be compatible with Windows's #define:
//
// https://stackoverflow.com/q/1238609
//
// The primary focus of Ren-C is on UTF-8, but it does grudgingly provide
// some UCS2 APIs.  To avoid duplicating a u16-based "UCS2" API and a wchar_t
// API, the API is exposed as being REBWCHAR based, which does a #define
// based on the platform.
//
// *** However, don't use REBWCHAR in client code.  If the client code is
// on Windows, use WCHAR.  If it's in a unixodbc client use SQLWCHAR.  In
// general, try and use UTF8 if you possibly can. ***
//

#ifdef TO_WINDOWS
    #define REBWCHAR wchar_t
#else
    #define REBWCHAR uint16_t
#endif



//=////////////////////////////////////////////////////////////////////////=//
//
// C FUNCTION TYPE (__cdecl)
//
//=////////////////////////////////////////////////////////////////////////=//
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


//=////////////////////////////////////////////////////////////////////////=//
//
// UNREACHABLE CODE ANNOTATIONS
//
//=////////////////////////////////////////////////////////////////////////=//
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

#if defined(__clang__) || GCC_VERSION_AT_LEAST(2, 5)
    #define ATTRIBUTE_NO_RETURN __attribute__ ((noreturn))
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
    #define ATTRIBUTE_NO_RETURN _Noreturn
#elif defined(_MSC_VER)
    #define ATTRIBUTE_NO_RETURN __declspec(noreturn)
#else
    #define ATTRIBUTE_NO_RETURN
#endif

#if __has_builtin(__builtin_unreachable) || GCC_VERSION_AT_LEAST(4, 5)
    #define DEAD_END __builtin_unreachable()
#elif defined(_MSC_VER)
    __declspec(noreturn) static inline void msvc_unreachable(void) {
        while (TRUE) { }
    }
    #define DEAD_END msvc_unreachable()
#else
    #define DEAD_END
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// TESTING IF A NUMBER IS FINITE
//
//=////////////////////////////////////////////////////////////////////////=//
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
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
    // C99 or later
    #define FINITE isfinite
#elif defined(CPLUSPLUS_11)
    // C++11 or later
    #define FINITE isfinite
#else
    // Other fallbacks...
    #ifdef TO_WINDOWS
        #define FINITE _finite // The usual answer for Windows
    #else
        #define FINITE finite // The usual answer for POSIX
    #endif
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// MEMORY POISONING and POINTER TRASHING
//
//=////////////////////////////////////////////////////////////////////////=//
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
//
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

    #define TRASH_CFUNC_IF_DEBUG(p) \
        NOOP
#else
    #if defined(__cplusplus) // needed even if not C++11
        template<class T>
        inline static void TRASH_POINTER_IF_DEBUG(T* &p) {
            p = reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD));
        }

        template<class T>
        inline static void TRASH_CFUNC_IF_DEBUG(T* &p) {
            p = reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD));
        }

        template<class T>
        inline static REBOOL IS_POINTER_TRASH_DEBUG(T* p) {
            return LOGICAL(
                p == reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD))
            );
        }

        template<class T>
        inline static REBOOL IS_CFUNC_TRASH_DEBUG(T* p) {
            return LOGICAL(
                p == reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD))
            );
        }
    #else
        #define TRASH_POINTER_IF_DEBUG(p) \
            ((p) = cast(void*, cast(uintptr_t, 0xDECAFBAD)))

        #define TRASH_CFUNC_IF_DEBUG(p) \
            ((p) = cast(CFUNC*, cast(uintptr_t, 0xDECAFBAD)))
            
        #define IS_POINTER_TRASH_DEBUG(p) \
            LOGICAL((p) == cast(void*, cast(uintptr_t, 0xDECAFBAD)))

        #define IS_CFUNC_TRASH_DEBUG(p) \
            LOGICAL((p) == cast(CFUNC*, cast(uintptr_t, 0xDECAFBAD)))
    #endif
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// MARK UNUSED VARIABLES
//
//=////////////////////////////////////////////////////////////////////////=//
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
#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    #define UNUSED(x) \
        ((void)(x))
#else
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
        TRASH_POINTER_IF_DEBUG(v);
    }

    // Any integral or floating type, set to a spam number.  Use 123 just to
    // avoid having to write separate handlers for all arithmetic types, as
    // it fits in a signed char (but not 127), and looks a bit unnatural.
    //
    template<
        typename T,
        typename TRR = typename std::remove_reference<T>::type,
        typename std::enable_if<
            std::is_lvalue_reference<T &&>::value
            && !std::is_const<TRR>::value
            && std::is_arithmetic<TRR>::value
        >::type* = nullptr
    >
    void UNUSED(T && v) {
        v = 123;
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


//=////////////////////////////////////////////////////////////////////////=//
//
// BYTE-ORDER SENSITIVE BIT FLAGS & MASKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These macros are for purposefully arranging bit flags with respect to the
// "leftmost" and "rightmost" bytes of the underlying platform, when encoding
// them into an unsigned integer the size of a platform pointer:
//
//     uintptr_t flags = FLAGIT_LEFT(0);
//     unsigned char *ch = (unsigned char*)&flags;
//
// In the code above, the leftmost bit of the flags has been set to 1,
// resulting in `ch == 128` on all supported platforms.
//
// Quantities smaller than a byte can be mixed in on the right with flags
// from the left.  These form single optimized constants, which can be
// assigned to an integer.  They can be masked or shifted out efficiently:
//
//    uintptr_t flags = FLAGIT_LEFT(0) | FLAGIT_LEFT(1) | FLAGBYTE_RIGHT(13);
//
//    unsigned int left = LEFT_N_BITS(flags, 3); // == 6 (binary `110`)
//    unsigned int right = RIGHT_N_BITS(flags, 3); // == 5 (binary `101`)
//
// `left` gets `110` because it asked for the left 3 bits, of which only
// the first and the second had been set.
//
// `right` gets `101` because 13 was binary `1101` that was added into the
// value.  Yet only the rightmost 3 bits were requested.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Note: It is simpler to not worry about the underlying bytes and just use
// ordinary bit masking.  But this is used for an important feature (the
// discernment of a `void*` to a REBVAL from that of a valid UTF-8 string).
// Other tools that might be tried with this all have downsides:
//
// * bitfields arranged in a `union` with integers have no layout guarantee
// * `#pragma pack` is not standard C98 or C99...nor is any #pragma
// * `char[4]` or `char[8]` can't generally be assigned in one instruction
//

#if defined(__LP64__) || defined(__LLP64__)
    #define PLATFORM_BITS 64
#else
    #define PLATFORM_BITS 32
#endif

#if defined(ENDIAN_BIG) // Byte w/most significant bit first

    #define FLAGIT_LEFT(n) \
        ((uintptr_t)1 << (PLATFORM_BITS - (n) - 1)) // 63,62,61..or..32,31,30

    #define FLAGBYTE_FIRST(val) \
        ((uintptr_t)val << (PLATFORM_BITS - 8)) // val <= 255

    #define FLAGBYTE_RIGHT(val) \
        ((uintptr_t)val) // little endian needs val <= 255

    #define FLAGBYTE_MID(val) \
        (((uintptr_t)val) << 8) // little endian needs val <= 255

    #define FLAGUINT16_RIGHT(val) \
        ((uintptr_t)val) // litte endian needs val <= 65535

    #define RIGHT_16_BITS(flags) \
        ((flags) & 0xFFFF)

#elif defined(ENDIAN_LITTLE) // Byte w/least significant bit first (e.g. x86)

    #define FLAGIT_LEFT(n) \
        ((uintptr_t)1 << (7 + ((n) / 8) * 8 - (n) % 8)) // 7,6,..0|15,14..8|..

    #define FLAGBYTE_FIRST(val) \
        ((uintptr_t)val) // val <= 255

    #define FLAGBYTE_RIGHT(val) \
        ((uintptr_t)(val) << (PLATFORM_BITS - 8)) // val <= 255

    #define FLAGBYTE_MID(val) \
        ((uintptr_t)(val) << (PLATFORM_BITS - 16)) // val <= 255

    #define FLAGUINT16_RIGHT(val) \
        ((uintptr_t)(val) << (PLATFORM_BITS - 16))

    #define RIGHT_16_BITS(flags) \
        ((flags) >> (PLATFORM_BITS - 16)) // unsigned, should zero fill left
#else
    // !!! There are macro hacks which can actually make reasonable guesses
    // at endianness, and should probably be used in the config if nothing is
    // specified explicitly.
    //
    // http://stackoverflow.com/a/2100549/211160
    //
    #error "ENDIAN_BIG or ENDIAN_LITTLE must be defined"
#endif

// These specialized extractions of N bits out of the leftmost, rightmost,
// or "middle" byte (one step to the left of rightmost) can be expressed in
// a platform-agnostic way.  The constructions by integer to establish these
// positions are where the the difference is.
//
// !!! It would be possible to do this with integer shifting on big endian
// in a "simpler" way, e.g.:
//
//    #define LEFT_N_BITS(flags,n) ((flags) >> PLATFORM_BITS - (n))
//
// But in addition to big endian platforms being kind of rare, it's not clear
// that would be faster than a byte operation, especially with optimization.
//

#define LEFT_8_BITS(flags) \
    (((const uint8_t*)&flags)[0]) // reminder: 8 is faster

#define LEFT_N_BITS(flags,n) /* n <= 8 */ \
    (((const uint8_t*)&flags)[0] >> (8 - (n)))

#define RIGHT_N_BITS(flags,n) /* n <= 8 */ \
    (((const uint8_t*)&flags)[sizeof(uintptr_t) - 1] & ((1 << (n)) - 1))

#define RIGHT_8_BITS(flags) \
    (((const uint8_t*)&flags)[sizeof(uintptr_t) - 1]) // reminder: 8 is faster

#define CLEAR_N_RIGHT_BITS(flags,n) /* n <= 8 */ \
    (((uint8_t*)&flags)[sizeof(uintptr_t) - 1] &= ~((1 << (n)) - 1))

#define CLEAR_8_RIGHT_BITS(flags) \
    (((uint8_t*)&flags)[sizeof(uintptr_t) - 1] = 0) // reminder: 8 is faster

#define MID_N_BITS(flags,n) /* n <= 8 */ \
    (((const uint8_t*)&flags)[sizeof(uintptr_t) - 2] & ((1 << (n)) - 1))

#define MID_8_BITS(flags) \
    (((const uint8_t*)&flags)[sizeof(uintptr_t) - 2]) // reminder: 8 is faster

#define CLEAR_N_MID_BITS(flags,n) /* n <= 8 */ \
    (((uint8_t*)&flags)[sizeof(uintptr_t) - 2] &= ~((1 << (n)) - 1))

#define CLEAR_8_MID_BITS(flags) \
    (((uint8_t*)&flags)[sizeof(uintptr_t) - 2] = 0) // reminder: 8 is faster

#define CLEAR_16_RIGHT_BITS(flags) \
    (((uint8_t*)&flags)[sizeof(uintptr_t) - 1] = \
        ((uint8_t*)&flags)[sizeof(uintptr_t) - 2] = 0)


//=////////////////////////////////////////////////////////////////////////=//
//
// MIN AND MAX
//
//=////////////////////////////////////////////////////////////////////////=//
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


//=////////////////////////////////////////////////////////////////////////=//
//
// BYTE STRINGS VS UNENCODED CHARACTER STRINGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Use these when you semantically are talking about unsigned characters as
// bytes.  For instance: if you want to count unencoded chars in 'char *' us
// strlen(), and the reader will know that is a count of letters.  If you have
// something like UTF-8 with more than one byte per character, use LEN_BYTES.
// The casting macros are derived from "Casts for the Masses (in C)":
//
// http://blog.hostilefork.com/c-casts-for-the-masses/
//
// For APPEND_BYTES_LIMIT, m is the max-size allocated for d (dest)
//
#include <string.h> // for strlen() etc, but also defines `size_t`
#if defined(NDEBUG)
    /* These [S]tring and [B]inary casts are for "flips" between a 'char *'
     * and 'unsigned char *' (or 'const char *' and 'const unsigned char *').
     * Being single-arity with no type passed in, they are succinct to use:
     */
    #define s_cast(b)       ((char *)(b))
    #define cs_cast(b)      ((const char *)(b))
    #define b_cast(s)       ((uint8_t *)(s))
    #define cb_cast(s)      ((const uint8_t *)(s))

    #define LEN_BYTES(s) \
        strlen((const char*)(s))
    #define COPY_BYTES(d,s,n) \
        strncpy((char*)(d), (const char*)(s), (n))
    #define COMPARE_BYTES(l,r) \
        strcmp((const char*)(l), (const char*)(r))
    #define APPEND_BYTES_LIMIT(d,s,m) \
        strncat((char*)d, (const char*)s, MAX((m) - strlen((char*)d) - 1, 0))
#else
    /* We want to ensure the input type is what we thought we were flipping,
     * particularly not the already-flipped type.  Instead of type_traits, 4
     * functions check in both C and C++ (here only during Debug builds):
     */
    inline static uint8_t *b_cast(char *s)
        { return (uint8_t*)s; }

    inline static const uint8_t *cb_cast(const char *s)
        { return (const uint8_t*)s; }

    inline static char *s_cast(uint8_t *s)
        { return (char*)s; }

    inline static const char *cs_cast(const uint8_t *s)
        { return (const char*)s; }

    // Debug build uses inline function stubs to ensure you pass in uint8_t *
    //
    inline static uint8_t *COPY_BYTES(
        uint8_t *dest, const uint8_t *src, size_t count
    ){
        return b_cast(strncpy(s_cast(dest), cs_cast(src), count));
    }

    inline static size_t LEN_BYTES(const uint8_t *str)
        { return strlen(cs_cast(str)); }

    inline static int COMPARE_BYTES(const uint8_t *lhs, const uint8_t *rhs)
        { return strcmp(cs_cast(lhs), cs_cast(rhs)); }

    inline static uint8_t *APPEND_BYTES_LIMIT(
        uint8_t *dest, const uint8_t *src, size_t max
    ){
        return b_cast(strncat(
            s_cast(dest), cs_cast(src), MAX(max - LEN_BYTES(dest) - 1, 0)
        ));
    }
#endif


// Global pixel format setup for REBOL image!, image loaders, color handling,
// tuple! conversions etc.  The graphics compositor code should rely on this
// setting(and do specific conversions if needed)
//
// TO_RGBA_COLOR always returns 32bit RGBA value, converts R,G,B,A
// components to native RGBA order
//
// TO_PIXEL_COLOR must match internal image! datatype byte order, converts
// R,G,B,A components to native image format
//
// C_R, C_G, C_B, C_A Maps color components to correct byte positions for
// image! datatype byte order

#ifdef ENDIAN_BIG // ARGB pixel format on big endian systems
    #define TO_RGBA_COLOR(r,g,b,a) \
        (cast(uint32_t, (r)) << 24 \
        | cast(uint32_t, (g)) << 16 \
        | cast(uint32_t, (b)) << 8 \
        | cast(uint32_t, (a)))

    #define C_A 0
    #define C_R 1
    #define C_G 2
    #define C_B 3

    #define TO_PIXEL_COLOR(r,g,b,a) \
        (cast(uint32_t, (a)) << 24 \
        | cast(uint32_t, (r)) << 16 \
        | cast(uint32_t, (g)) << 8 \
        | cast(uint32_t, (b)))
#else
    #define TO_RGBA_COLOR(r,g,b,a) \
        (cast(uint32_t, (a)) << 24 \
        | cast(uint32_t, (b)) << 16 \
        | cast(uint32_t, (g)) << 8 \
        | cast(uint32_t, (r)))

    #ifdef TO_ANDROID_ARM // RGBA pixel format on Android
        #define C_R 0
        #define C_G 1
        #define C_B 2
        #define C_A 3

        #define TO_PIXEL_COLOR(r,g,b,a) \
            (cast(uint32_t, (a)) << 24 \
            | cast(uint32_t, (b)) << 16 \
            | cast(uint32_t, (g)) << 8 \
            | cast(uint32_t, (r)))

    #else // BGRA pixel format on Windows
        #define C_B 0
        #define C_G 1
        #define C_R 2
        #define C_A 3

        #define TO_PIXEL_COLOR(r,g,b,a) \
            (cast(uint32_t, (a)) << 24 \
            | cast(uint32_t, (r)) << 16 \
            | cast(uint32_t, (g)) << 8 \
            | cast(uint32_t, (b)))
    #endif
#endif
