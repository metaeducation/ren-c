//
//  File: %reb-c.h
//  Summary: "General C definitions and constants"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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

#if !defined(DEBUG_CHECK_CASTS)
    #define DEBUG_CHECK_CASTS 0
#endif

#if !defined(DEBUG_CHECK_OPTIONALS)
    #define DEBUG_CHECK_OPTIONALS 0
#endif


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
    #include <stophere>  // https://stackoverflow.com/a/45661130
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

#if defined(__cplusplus) && (! TO_HAIKU)
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
#if CPLUSPLUS_11
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
// Along with the general philosophy that there is only the minimum special
// handling for C++ builds prior to C++11 (e.g. extern "C" stuff), this is
// only geared to use with C++11.

#if CPLUSPLUS_11
    #define const_if_c
#else
    #define const_if_c const
#endif


//=//// CASTING MACROS ////////////////////////////////////////////////////=//
//
// These macros are easier-to-spot variants of the parentheses cast.
//
//   * The 'm_cast' is when getting [M]utablity on a const is okay
//   * Plain 'cast' can do everything else (except remove const/volatile)
//   * The 'c_cast' helper ensures you're ONLY adding [C]onst to a value
//
// Additionally there is x_cast(), for cases where you don't know if your
// input pointer is const or not, and want to cast to a mutable pointer.
// C++ doesn't let you use old-style casts to accomplish this, so it has to
// be done using two casts and type_traits magic.
//
// (This code is based on ideas in "Casts for the Masses (in C)":
//
//   http://blog.hostilefork.com/c-casts-for-the-masses/
//
// However, due to the runtime costs of the "helper" functions in debug
// builds where inlining is not done...they have been simplified so as to
// compile away completely, whenever possible.)
//
// See also: DEBUG_CHECK_CASTS

#define cast(T,v)       ((T)(v))

#if (! CPLUSPLUS_11)
    #define x_cast(T,v)    ((T)(v))  /* pointer casts different in C++11 */

    #define m_cast(T,v)     ((T)(v))
    #define c_cast(T,v)     ((T)(v))
    /*
     * Q: Why divide roles?  A: Frequently, input to cast is const but you
     * "just forget" to include const in the result type, gaining mutable
     * access.  Stray writes to that can cause even time-traveling bugs, with
     * effects *before* that write is made...due to "undefined behavior".
     */

    #define mp_cast(T,v)     ((T)(v))
#else
    /* We build an arbitrary pointer cast out of two steps: one which adds
     * a const if it wasn't already there, and then a const_cast to the
     * desired type (which will remove the const if target type isn't const).
     * This has no runtime cost, so provides the desired efficiency.
     */
    #define x_cast(T,v) \
       (const_cast<T>( \
            ( \
                std::add_pointer< \
                    std::add_const< \
                        std::remove_pointer<T>::type \
                    >::type \
                >::type \
            )(v) /* old-style parentheses cast, "everything but" the const */ \
        ))

    #define c_cast(T,v) \
        (const_cast<std::conditional< \
            std::is_const<std::remove_pointer<T>::type>::value, \
            T, /* success case */ \
            void /* failure case--you get this if c_cast to mutable type */ \
        >::type>(v))

    #define m_cast(T,v) \
        (const_cast<std::conditional< \
            ! std::is_const<std::remove_pointer<T>::type>::value, \
            T, /* success case */ \
            void /* failure case--you get this if m_cast to const type */ \
        >::type>(v))

    /*
     * NOTE: mp_cast_helper() is needed vs. plain const_cast<> in all C++ builds
     * as a hook point to overload casting with smart pointer types, e.g.
     * `mp_cast(Utf8(*), some_const_rebchr)`.  Search for overloads of the
     * mp_cast_helper() in certain builds before deciding to simplify this.
     * (C++ smart pointers have this problem too, see `const_cast_pointer<>`)
     */
    template<typename T, typename V>
    T mp_cast_helper(V v) {
        static_assert(
            !std::is_const<typename std::remove_pointer<T>::type>::value,
            "invalid mp_cast() - requested a const type for output result"
        );
        return const_cast<T>(v);
    }

    #define mp_cast(T,v)     mp_cast_helper<T>(v)
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
// Since libRebol hinges on a premise of making the internal ~null~ signifier
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

#elif CPLUSPLUS_11 || defined(_MSC_VER)  // C++11 or above (MSVC has no C++98)
    //
    // Note that MSVC 2019 (at least) does not offer an option of building
    // with C++98.  Hence nullptr_t is defined, no matter what you do.
    //
    // http://en.cppreference.com/w/cpp/language/nullptr
    // is defined as `using nullptr_t = decltype(nullptr);` in <cstddef>
    //
    #include <cstddef>
    using std::nullptr_t;

#elif defined(nullptr)
    //
    // If you try something like building with Emscripten in C++98, it will
    // shim nullptr itself...trying to override it will cause an error.
    //
#else
    // C++98 shim from "Effective C++": https://stackoverflow.com/a/44517878
    //
    // Note: Some "newer old" C++ compilers had awareness that nullptr would
    // be added to the standard, and raise warnings by default if one tries
    // to make this definition, even when building as C++98.  To disable such
    // warnings, you would need something like `-Wno-c++0x-compat`
    //
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
    };

    /* The original shim declared the object as a const instance, but that
       seems to cause linker errors.  So instead, you must define the
       nullptr instance somewhere if using this shim.  :-(  */

    #define REBOL_USE_NULLPTR_SHIM  // currently instance is in %a-lib.c
    extern const nullptr_t nullptr;
#endif


//=//// NULL ENSURER FOR C++ ///////////////////////////////////////////////=//
//
// No-op in C builds, but in C++ build helps to ensure the given variable is
// null, while still allowing the reference to be used as an lvalue.
//
//
#if CPLUSPLUS_11
    template<class T>
    inline static T*& ensureNullptr(T*& p) {
        assert(p == nullptr);
        return p;
    }
#else
    #define ensureNullptr(p) p
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

#if TO_HAIKU
#undef ALIGN
#endif
#define ALIGN(s,a) \
    (((s) + (a) - 1) & ~((a) - 1))


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
#if TO_WINDOWS
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
#elif CPLUSPLUS_11  // C++11 or later
    #define FINITE isfinite
#elif defined(__MINGW32__) || defined(__MINGW64__)
    #define FINITE isfinite // With --std==c++98 MinGW still has isfinite
#elif TO_WINDOWS
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

#if (! CPLUSPLUS_11) || defined(NDEBUG)
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
#if DEBUG_CHECK_OPTIONALS
    template<typename T>
    struct OptionWrapper {
        T wrapped;

        OptionWrapper () = default;  // garbage, or 0 if global, see [2]

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
#else
    #define Option(T) T
    #define unwrap(v) (v)
    #define try_unwrap(v) (v)
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
// of using 0xBADF00D or 0xDECAFBAD is formalized with Trash_Pointer_If_Debug.
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
    #define Trash_Pointer_If_Debug(p)       NOOP
    #define Trash_Cfunc_If_Debug(T,p)       NOOP
#else
    #if defined(__cplusplus) // needed even if not C++11
        template<class T>
        inline static void Trash_Pointer_If_Debug(T* &p) {
            p = reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD));
        }

        template<class T>
        inline static void SafeTrash_Pointer_If_Debug(T* &p) {
            p = reinterpret_cast<T*>(static_cast<uintptr_t>(0x5AFE5AFE));
        }

        template<class T>
        inline static void FreeTrash_Pointer_If_Debug(T* &p) {
            p = reinterpret_cast<T*>(static_cast<uintptr_t>(0xF4EEF4EEE));
        }

        template<class T>
        inline static bool Is_Pointer_Trash_Debug(T* p) {
            return (
                p == reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD))
            );
        }

        template<class T>
        inline static bool Is_Pointer_SafeTrash_Debug(T* p) {
            return (
                p == reinterpret_cast<T*>(static_cast<uintptr_t>(0x5AFE5AFE))
            );
        }

        template<class T>
        inline static bool Is_Pointer_FreeTrash_Debug(T* p) {
            return (
                p == reinterpret_cast<T*>(static_cast<uintptr_t>(0xF4EEF4EE))
            );
        }

    #if DEBUG_CHECK_OPTIONALS
        template<class P>
        inline static void Trash_Pointer_If_Debug(OptionWrapper<P> &p) {
            p.wrapped = reinterpret_cast<P>(static_cast<uintptr_t>(0xDECAFBAD));
        }

        template<class P>
        inline static bool Is_Pointer_Trash_Debug(OptionWrapper<P> &p) {
            return p.wrapped == (
                reinterpret_cast<P>(static_cast<uintptr_t>(0xDECAFBAD))
            );
        }
    #endif

    #if CPLUSPLUS_11
        template<class P>
        inline static void Trash_Pointer_If_Debug(NeverNullEnforcer<P> &p) {
            p = reinterpret_cast<P>(static_cast<uintptr_t>(0xDECAFBAD));
        }

        template<class P>
        inline static bool Is_Pointer_Trash_Debug(NeverNullEnforcer<P> &p) {
            return (
                p == reinterpret_cast<P>(static_cast<uintptr_t>(0xDECAFBAD))
            );
        }
      #endif
    #else
        #define Trash_Pointer_If_Debug(p) \
            ((p) = cast(void*, cast(uintptr_t, 0xDECAFBAD)))

        #define SafeTrash_Pointer_If_Debug(p) \
            ((p) = cast(void*, cast(uintptr_t, 0x5AFE5AFE)))

        #define FreeTrash_Pointer_If_Debug(p) \
            ((p) = cast(void*, cast(uintptr_t, 0xF4EEF4EE)))

        #define Is_Pointer_Trash_Debug(p) \
            ((p) == cast(void*, cast(uintptr_t, 0xDECAFBAD)))

        #define Is_Pointer_SafeTrash_Debug(p) \
            ((p) == cast(void*, cast(uintptr_t, 0x5AFE5AFE)))

        #define Is_Pointer_FreeTrash_Debug(p) \
            ((p) == cast(void*, cast(uintptr_t, 0xF4EEF4EE)))
    #endif

    // C functions must be cast to the right type, even in C (no void*)

    #define Trash_Cfunc_If_Debug(T,p) \
        ((p) = cast(T, cast(uintptr_t, 0xDECAFBAD)))

    #define IS_CFUNC_TRASH_DEBUG(T,p) \
        ((p) == cast(T, cast(uintptr_t, 0xDECAFBAD)))
#endif


//=//// TYPE ENSURING HELPERS /////////////////////////////////////////////=//
//
// It's useful when building macros to make inline functions that just check
// a type, for example:
//
//      inline static Foo* ensure_foo(Foo* f) { return f; }
//
// This has the annoying property that you have to write that function and
// put it somewhere.  But also, there's a problem with constness... if you
// want to retain it, you will need two overloads in C++ (and C does not
// have overloading).
//
// This introduces some tools that are no-ops in C.  One is a simple type
// `ensure` construct:
//
//      void *p = ensure(Series(*), s);
//
// Another called `ensurer` lets you put it in the stream of execution without
// being inside parentheses (it does this with operator `<<` magic):
//
//      int x = ensurer(int) "this would fail in a C++ build, for instance";
//
// And yet another called `ensured` lets you check an assignment.  This is
// helpful when you need to do something like assign to a void* and can't
// do weird cast dereferencing or you'll violate strict aliasing.
//
#if (! CPLUSPLUS_11)
    #define ensure(T,v) (v)
    #define ensurer(T)
    #define ensured(T,L,left) (left)
#else
    #if (! DEBUG_CHECK_CASTS)
        #define ensure(T,v) \
            static_cast<T>(v)  // permits downcasts, but better than nothing

        #define ensurer(T)
        #define ensured(T,L,left) (left)
    #else
        template<typename T>
        struct ensure_reader {
            template<typename U>
            T operator<< (U u) { return u; }

            template<typename U>
            static T check(U u) { return u; }
        };
        #define ensure(T,v) ensure_reader<T>::check(v)
        #define ensurer(T) ensure_reader<T>() <<

        template<typename T, typename L>
        struct ensure_writer {
            L &left;
            ensure_writer(L &left) : left (left) {}
            void operator=(const T &right) {
                left = right;
            }
        };
        #define ensured(T,L,left) (ensure_writer<T,L>{(left)})
            //
            // ^-- Note: C++17 should be able to infer template arguments from
            // constructors, so this could just be `ensurer(T,left)`.  There
            // aren't enough of these to worry about it, yet.
            //
            // https://stackoverflow.com/a/984597/
  #endif
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

    #define Trash_If_Debug(x) \
        memset(&(x), 0xBD, sizeof(x));

    #define UNUSED(x) \
        ((void)(x))
#else
    #define UNUSED Trash_If_Debug

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
    void Trash_If_Debug(T && v) {
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
    void Trash_If_Debug(T && v) {
        static bool zero = false;
        if (zero)
            v = nullptr; // do null half the time, deterministic
        else
            Trash_Pointer_If_Debug(v); // trash the other half of the time
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
    void Trash_If_Debug(T && v) {
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
    void Trash_If_Debug(T && v) {
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
    inline static size_t strsize(const char *cp)
        { return strlen(cp); }

    inline static size_t strsize(const unsigned char *bp)
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

#endif  // !defined(REB_C_H_1020_0304)
