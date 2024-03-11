//
//  File: %c-enhanced.h
//  Summary: "General C definitions and constants"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// be included before %c-enhanced.h is included.
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
//     #ifdef WEIRD_INT_64
//         typedef _int64 i64;
//         typedef unsigned _int64 u64;
//         #define I64_C(c) c ## I64
//         #define U64_C(c) c ## U64
//     #endif
//
// If %pstdint.h isn't trying hard enough for an unsupported platform of
// interest to get 64-bit integers, then patches should be made there.
//


//=//// CONFIGURATION /////////////////////////////////////////////////////=//

#if !defined(DEBUG_CHECK_OPTIONALS)
    #define DEBUG_CHECK_OPTIONALS 0
#endif


//=//// EXPECTS <stdbool.h> OR "pstdbool.h" SHIM INCLUDED /////////////////=//
//
// It's better than what was previously used, a (bool)cast_of_expression.
// And it makes it much safer to use ordinary `&` operations to test for
// flags, more succinctly even:
//
//     bool b = GET_FLAG(flags, SOME_FLAG_ORDINAL);
//     bool b = !GET_FLAG(flags, SOME_FLAG_ORDINAL);
//
// vs.
//
//     bool b = did (flags & SOME_FLAG_BITWISE); // 3 fewer chars
//     bool b = not (flags & SOME_FLAG_BITWISE); // 4 fewer chars
//
// (Bitwise vs. ordinal also permits initializing options by just |'ing them.)
//
// !!! Historically Rebol used TRUE and FALSE uppercase macros, but so long
// as C99 has added bool to the language, there's not much point in being
// compatible with codebases that have `char* true = "Spandau";` or similar
// in them.  So Rebol can use `true` and `false.
//
#ifdef __cplusplus
  #if defined(_MSC_VER)
    #include <iso646.h> // MSVC doesn't have `and`, `not`, etc. w/o this
  #else
    // legitimate compilers define them, they're even in the C++98 standard!
  #endif
#else
    // Since the C90 standard of C, iso646.h has been defined:
    //
    // https://en.wikipedia.org/wiki/C_alternative_tokens
    //
    // ...but TCC doesn't ship with it, and maybe other C's don't either.  The
    // issue isn't so much the file, as it is agreeing on the 11 macros, so
    // just define them here.
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

// A 12th macro: http://blog.hostilefork.com/did-programming-opposite-of-not/
//
#define did !!


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


//=//// NOOP a.k.a. VOID GENERATOR ////////////////////////////////////////=//
//
// VOID would be a more purposeful name, but Windows headers define that
// for the type (as used in types like LPVOID)
//
#ifndef NOOP
    #define NOOP \
        ((void)(0))
#endif


//=//// ALIGNMENT SIZE ////////////////////////////////////////////////////=//
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
#elif CPLUSPLUS_11 // C++11 or later
    #define FINITE isfinite
#elif defined(__MINGW32__) || defined(__MINGW64__)
    #define FINITE isfinite // With --std==c++98 MinGW still has isfinite
#elif defined(TO_WINDOWS)
    #define FINITE _finite // The usual answer for Windows
#else
    #define FINITE finite // The usual answer for POSIX
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
// of using 0xBADF00D or 0xDECAFBAD is formalized with Corrupt_Pointer_If_Debug.
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
    #define Corrupt_Pointer_If_Debug(p) \
        NOOP

    #define Corrupt_CFunction_If_Debug(p) \
        NOOP
#else
    #if defined(__cplusplus) // needed even if not C++11
        template<class T>
        INLINE void Corrupt_Pointer_If_Debug(T* &p) {
            p = reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD));
        }

        template<class T>
        INLINE void Corrupt_CFunction_If_Debug(T* &p) {
            p = reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD));
        }

        template<class T>
        INLINE bool Is_Pointer_Corrupt_Debug(T* p) {
            return (
                p == reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD))
            );
        }

        template<class T>
        INLINE bool Is_CFunction_Corrupt_Debug(T* p) {
            return (
                p == reinterpret_cast<T*>(static_cast<uintptr_t>(0xDECAFBAD))
            );
        }
    #else
        #define Corrupt_Pointer_If_Debug(p) \
            ((p) = cast(void*, cast(uintptr_t, 0xDECAFBAD)))

        #define Corrupt_CFunction_If_Debug(p) \
            ((p) = cast(CFUNC*, cast(uintptr_t, 0xDECAFBAD)))

        #define Is_Pointer_Corrupt_Debug(p) \
            ((p) == cast(void*, cast(uintptr_t, 0xDECAFBAD)))

        #define Is_CFunction_Corrupt_Debug(p) \
            ((p) == cast(CFUNC*, cast(uintptr_t, 0xDECAFBAD)))
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
        static bool zero = false;
        if (zero)
            v = nullptr; // do null half the time, deterministic
        else
            Corrupt_Pointer_If_Debug(v); // trash the other half of the time
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


//=//// BYTE STRINGS VS UNENCODED CHARACTER STRINGS ///////////////////////=//
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
#define strsize strlen
#if defined(NDEBUG)
    /* These [S]tring and [B]inary casts are for "flips" between a 'char *'
     * and 'unsigned char *' (or 'const char *' and 'const unsigned char *').
     * Being single-arity with no type passed in, they are succinct to use:
     */
    #define s_cast(b)       ((char *)(b))
    #define cs_cast(b)      ((const char *)(b))
    #define b_cast(s)       ((unsigned char *)(s))
    #define cb_cast(s)      ((const unsigned char *)(s))

    #define LEN_BYTES(s) \
        strlen((const char*)(s))

    #define COPY_BYTES(d,s,n) \
        strncpy((char*)(d), (const char*)(s), (n))

    #define COMPARE_BYTES(l,r) \
        strcmp((const char*)(l), (const char*)(r))

    INLINE unsigned char *APPEND_BYTES_LIMIT(
        unsigned char *dest, const unsigned char *src, size_t max
    ){
        size_t len = LEN_BYTES(dest);
        return b_cast(strncat(
            s_cast(dest), cs_cast(src), MAX(max - len - 1, 0)
        ));
    }
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

    // Debug build uses inline functions to ensure you pass in unsigned char *
    //
    INLINE unsigned char *COPY_BYTES(
        unsigned char *dest, const unsigned char *src, size_t count
    ){
        return b_cast(strncpy(s_cast(dest), cs_cast(src), count));
    }

    INLINE size_t LEN_BYTES(const unsigned char *str)
        { return strlen(cs_cast(str)); }

    INLINE int COMPARE_BYTES(
        const unsigned char *lhs, const unsigned char *rhs
    ){
        return strcmp(cs_cast(lhs), cs_cast(rhs));
    }

    INLINE unsigned char *APPEND_BYTES_LIMIT(
        unsigned char *dest, const unsigned char *src, size_t max
    ){
        size_t len = LEN_BYTES(dest);
        return b_cast(strncat(
            s_cast(dest), cs_cast(src), MAX(max - len - 1, 0)
        ));
    }
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
