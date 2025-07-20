#ifndef C_EXTRAS_H
#define C_EXTRAS_H


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
// of using 0xBADF00D or 0xDECAFBAD is formalized in Corrupt_If_Needful().
// This makes the instances easier to find and standardizes how it is done.
//
// 1. <IMPORTANT>: Address sanitizer's memory poisoning must not have two
//    threads both poisoning/unpoisoning the same addresses at the same time.
//
// 2. @HostileFork wrote a tiny C++ "poor man's memory poisoner" that uses
//    XOR to poison bits and then unpoison them back.
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
#undef MIN  // common for these to be defined [1]
#undef MAX
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))


//=//// ensure_nullptr() //////////////////////////////////////////////////=//
//
// At one time, ensure_nullptr(p) was implemented as ensure(nullptr, p).
// However, this forced ensure() to use a function template in order to be
// able to have runtime code ensuring the nullness of a pointer.  That meant
// some overhead in debug builds, and ensure() wanted to be a compile-time
// only check... even in debug builds.  So ensure_nullptr() became its own
// separate construct.
//
#if NO_CPLUSPLUS_11 || NO_RUNTIME_CHECKS
    #define ensure_nullptr(expr)  (expr)
#else
    template<typename T>
    INLINE T*& Ensure_Nullptr(T*& v) {
        assert(v == nullptr);
        return v;
    }

    #define ensure_nullptr  Ensure_Nullptr
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
#include <string.h>  // for strlen() etc, but also defines `size_t`

#if CPLUSPLUS_11
    inline size_t strsize(const char *cp)
      { return strlen(cp); }

    inline size_t strsize(const unsigned char *bp)
      { return strlen(x_cast(const char*, bp)); }
#else
    #define strsize(bp) \
        strlen(x_cast(const char*, (bp)))
#endif


//=//// Byte* casts to char* /////////////////////////////////////////////=//
//
// These were once part of the Needful library, but they're so simple to
// define for anyone who wants them, and tough to explain.
//
// The idea is just to make a narrow conversion so if all you're doing is
// switching from signed to unsigned char, this cast does *only* that.
//

#define s_cast(bytes)   u_cast(char*, ensure(unsigned char*, (bytes)))
#define b_cast(chars)   u_cast(unsigned char*, ensure(char*, (chars)))


//=//// CONST PROPAGATION TOOLS ///////////////////////////////////////////=//
//
// !!! This is a very wacky idea that was struck from Needful for the time
// being due to its weirdness and possibility to be used incorrectly.  But
// it is used in Ren-C.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// C lacks overloading, which means that having one version of code for const
// input and another for non-const input requires two entirely different names
// for the function variations.  That can wind up seeming noisier than is
// worth it for a compile-time check.
//
//    const Member* Get_Member_Const(const Object* ptr) { ... }
//
//    Member* Get_Member(Object *ptr) { ... }
//
// Needful provides a way to use a single name and avoid code duplication.
// It's a little tricky, but looks like this:
//
//     MUTABLE_IF_C(Member*) Get_Member(CONST_IF_C(Object*) ptr_) {
//         CONSTABLE(Object*) ptr = m_cast(Object*, ptr_);
//         ...
//     }
//
// As the macro names suggest, the C build will behave in such a way that
// the input argument will always appear to be const, and the output argument
// will always appear to be mutable.  So it will compile permissively with
// no const const checking in the C build.. BUT the C++ build synchronizes
// the constness of the input and output arguments (though you have to use
// a proxy variable in the body if you want mutable access).
//
// 1. If writing a simple wrapper whose only purpose is to pipe const-correct
//    output results from the input's constness, a trick is to use `cast()`
//    which is a "const-preserving cast".
//
//    #define Get_Member_As_Foo(ptr)  cast(Foo*, Get_Member(ptr))
//
// 2. The C++ version of MUTABLE_IF_C() actually spits out a `template<>`
//    prelude.  If we didn't offer a "hook" to that, then if you wrote:
//
//        INLINE MUTABLE_IF_C(Type) Some_Func(...) {...}
//
//    You would get:
//
//        INLINE template<...> Some_Func(...) {...}
//
//    Since that would error, provide a generalized mechanism for optionally
//    slipping decorators before the template<> definition.
//

#if NO_CPLUSPLUS_11
    #define CONST_IF_C(param_type) \
        const param_type  // Note: use cast() macros instead, if you can [1]

    #define MUTABLE_IF_C(return_type, ...) \
        __VA_ARGS__ return_type  // __VA_ARGS__ needed for INLINE etc. [2]

    #define CONSTABLE(param_type)  param_type  // use m_cast() on assignment
#else
    #undef MUTABLE_IF_C
    #define MUTABLE_IF_C(ReturnType, ...) \
        template<typename T> \
        __VA_ARGS__ needful_mirror_const_t(T, ReturnType)

    #undef CONST_IF_C
    #define CONST_IF_C(ParamType) /* !!! static_assert ParamType somehow? */ \
        T&&  // universal reference to arg

    #undef CONSTABLE
    #define CONSTABLE(ParamType) \
        needful_mirror_const_t(T, ParamType)
#endif


#endif
