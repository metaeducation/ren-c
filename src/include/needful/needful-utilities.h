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
// 2. always_false<T> is a template that always yields false, but is dependent
//    on T.  This works around the problem of static_assert()s inside of
//    SFINAE'd functions, which would fail even if the SFINAE conditions
//    were not met:
//
//        static_assert(false, "Always fails, even if not SFINAE'd");
//        static_assert(always_false<T>::value, "Only fails if SFINAE'd");
//
#if CPLUSPLUS_11
    #include <type_traits>
    #include <utility>  // for std::forward()

  namespace shim {  // [1]
    template<typename _From, typename _To>
    struct is_explicitly_convertible : public std::is_constructible<_To, _From>
      { };
  }

    template<typename>
    struct always_false : std::false_type {};  // for SFINAE static_assert [2]
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


//=//// rr_decltype(): REMOVE REFERENCE DECLTYPE //////////////////////////=//
//
// decltype(v) when v is a variable and not an expression will not be a
// reference type.  decltype((v)) will be a reference if v is a lvalue.
//
// When decltype() is used in a macro like cast(), we don't want there to be
// a difference between `cast(T, v)` and `cast(T, (v))`...so we have to decide
// to either remove the reference from the type or always have the reference.
// It's best to remove it, and this makes that a bit easier, as it works for
// either reference or non-reference types.
//
#if CPLUSPLUS_11
    template<typename V>
    struct RemoveReferenceDecltypeHelper {
        typedef typename std::conditional<
            std::is_reference<V>::value,
            typename std::remove_reference<V>::type,
            V
        >::type type;
    };

    #define rr_decltype(v) \
        typename RemoveReferenceDecltypeHelper<decltype(v)>::type
#endif
