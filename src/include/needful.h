/*
**  file: %needful.h
**  summary: "Needful: Safer C without changing your C"
**  homepage: https://needful.metaeducation.com
**
******************************************************************************
**
** Copyright 2015-2026 metaeducation.com
**
** Licensed under the MIT License
**
** https://en.wikipedia.org/wiki/MIT_License
**
******************************************************************************
**
** Needful is a single-header library that adds typed options, typed results,
** visible casts, and compile-time intent checks to C.
**
** In plain C it stays a no-op; in enhanced builds it catches real mistakes.
**
** The key trick: every Needful construct compiles as a transparent macro
** in C.  But add the enhanced support files, `#define NEEDFUL_CPP_ENHANCED 1`,
** and rebuild as C++.  The same macros light up with compile-time enforcement
** that catches real bugs.
**
** Your C code stays C.  The C++ compiler just *checks* it harder.
**
****[[ WHAT YOU GET ]]*********************************************************
**
**   Need(T)       Marks a value as *required* non-null/non-zero.  Blocks
**                 boolean coercion: `if (ptr)` on a Need(T) is a compile
**                 error--null-checking a guarantee signals a logic bug.
**
**   Option(T)     Rust-like optional using T's falsey state as the sentinel.
**                 Same size as T--no extra bool!  `unwrap` extracts with a
**                 null-check panic; `opt` skips it (unsafe).  C++ enforces
**                 that Option(T) can't silently pass as a plain T.
**
**   Fallible(T)   Like Option(T) but "nodiscard": the compiler warns if you
**                 ignore the falsey/zero state.
**
**   Result(T)     Multiplexed error + return value, like Rust's Result<T,E>.
**                 `return_if_failed` auto-propagates, `catch_if_failed`
**                 catches with scoped error variables, and `else` attaches
**                 naturally:
**
**                     #define except  needful_catch_if_failed
**
**                     int x = Risky_Call(arg) except (Error* e) {
**                         printf("caught: %s\n", e->message);
**                     } else {
**                         printf("success!\n");
**                     }
**
**                 (yes, it's standard C! `except` is a macro that expands
**                 into a for() loop that can scope the declaration)
**
**   unreachable   Macro that gives the compiler hints ondivergent code paths,
**                 but also encodes a return from the containing function,
**                 which works polymophically across many return types.
**
**   cast()        A family of visible, hookable casts (cast, raw_cast,
**                 m_cast, i_cast, ...) that replace C's invisible
**                 parenthesized casts.  Can run debug-build validation
**                 hooks--even on raw pointer casts.
**
**   Sink(T)       Marks output parameters with contravariant type safety.
**   Init(T)       Contravariant output + corruption scrambling in debug.
**
**   known(T,expr) Compile-time type assertion inside macros.  Zero cost,
**                 even in debug builds--no function template overhead.
**
**   Comments      `possibly()`, `dont()`, `heeded()`, `unnecessary()`...
**                 annotations that document intent AND compile-check the
**                 expressions they wrap, keeping comments up-to-date.
**
****[[ GETTING STARTED ]]*****************************************************
**
**   1. Drop `needful.h` into your project.  #include it.  Done.
**      That is the default mode: single-header, no companion files, no build
**      system changes.
**
**   2. If you want the extra C++ checks, put needful-enhanced/ next to
**      needful.h.  That companion tree can stay out of your main repository
**      if you prefer--for example by cloning it locally and listing it in
**      .gitignore:
**
**      https://github.com/metaeducation/needful-enhanced
**
**      Then `#define NEEDFUL_CPP_ENHANCED 1` and build as C++11 (or even
**      better, C++17!)  Same source, stricter checking: type mismatches
**      become compile errors.
**
**   3. You can run both build modes in CI: the C build for production,
**      the C++ build to catch bugs.  No code changes needed between them.
**
** The C definitions in this file are intentionally written out in full so
** you can see how simple they are.  (Conditional code on `#ifdef __cplusplus`
** is only used when *absolutely necessary* in needful.h; C++ variations are
** done with `#undef` and then re-`#define`-ing.)  This helps you see that
** adding Needful to a C project is a low-impact proposition: one file,
** no dependencies, no magic.
**
****[[ NOTES ]]***************************************************************
**
** A. Needful globally disables `-Wint-conversion` in C mode.  This is needed
**    because `fail(...)` and `none` expand to comma expressions, and the
**    comma operator strips the "null pointer constant" status of 0, causing
**    GCC/Clang to warn on every `return fail(...)` in pointer-returning
**    functions.  The C++ enhanced build catches any real type mistakes.
**
**    See: https://needful.metaeducation.com/faq#int-conversion-warning
**
**    To restore the warning, set `#define NEEDFUL_DISABLE_INT_WARNING 0`.
**
** B. Needful's runtime invariant checks route through `NEEDFUL_ASSERT(expr)`.
**    If you don't define it, Needful includes `<assert.h>` and defaults it to
**    the platform `assert()`.  Projects that need debugger-friendlier
**    behavior can define `NEEDFUL_ASSERT(expr)` before including needful.h,
**    or include their own assert replacement header first.
*/

#ifndef NEEDFUL_H_INCLUDED  /* "include guard" allows multiple #includes */
#define NEEDFUL_H_INCLUDED

#if !defined(NEEDFUL_DISABLE_INT_WARNING) || NEEDFUL_DISABLE_INT_WARNING
  #if !defined(__cplusplus) && (defined(__GNUC__) || defined(__clang__))
    #pragma GCC diagnostic ignored "-Wint-conversion"  /* See [A] above */
  #endif
#endif

#if !defined(NEEDFUL_ASSERT)  /* See [B] above */
    #include <assert.h>
    #define NEEDFUL_ASSERT(expr)  assert(expr)
#endif


/***[[ Need(T): REQUIRED NON-NULL/NON-ZERO, BOOL-COERCION BLOCKED ]]*********
**
** Docs: https://needful.metaeducation.com/need
**
** Need(T) marks a pointer or value as guaranteed non-null/non-zero.  The
** core benefit: in NEEDFUL_CPP_ENHANCED builds, testing a Need(T) in a
** boolean context is a compile error.  Null-checking something known
** non-null reveals a misunderstanding in the surrounding code:
**
**    int value = 1020;
**    Need(int*) ptr = &value;
**
**    printf("%d\n", *ptr);  // safe to dereference, can't be null
**
**    if (ptr) { ... }  // ** COMPILE ERROR in C++ builds!
*/

#define NeedfulNeed(T)  T
#define needful_unwrap  /* no-op in C build */
#define needful_needed  /* no-op in C build */


/****[[ nocast: IMPLICIT COERCION FOR C/C++ COMPATIBILITY ]]*****************
**
** Docs: https://needful.metaeducation.com/nocast
**
** Bridges C's implicit void* and enum coercions into C++ builds:
**
**     SomeType* ptr = nocast malloc(sizeof(SomeType));  // works in C++...
**     SomeEnum  e   = nocast some_int_value;            // ...as does this!
**
** Empty macro in C.  In C++, generates a proxy object using `operator+` to
** implicitly trigger the correct static_cast (or C-cast for deep pointers).
**/

#if !defined(__cplusplus)  // need C++ mechanics even w/o NEEDFUL_CPP_ENHANCED
    #define needful_nocast
#else
    #include <type_traits>

  namespace needful {
    struct NocastMaker {};

    template<
        class To, class From,  // `From` pass-by-value, (no remove_reference)
        bool IntToPtr =  /* only one case needs special handling [1] */
            std::is_pointer<To>::value && std::is_integral<From>::value
    >
    struct NocastConvert {
        static To Do_Conversion(From f) { return static_cast<To>(f); }
    };

    template<class To, class From>
    struct NocastConvert<To, From, /*IntToPtr*/ true> {
        static To Do_Conversion(From) { return static_cast<To>(nullptr); }
    };

    template<class To, class From>  /* pointer-to-pointer via C-style */
    struct NocastConvert<To*, From*, /*IntToPtr*/ false> {
        static To* Do_Conversion(From* f) { return (To*)(f); }
    };

    template<class From>
    struct NocastHolder {
        From f;

        template<class To>
        operator To() const {
            return NocastConvert<To, From>::Do_Conversion(f);
        }
    };

    template<class T>  /* `+` for lower precedence than `%` for Result(T)  */
    inline NocastHolder<T> operator+(NocastMaker, T v) { return { v }; }

    constexpr NocastMaker g_nocast_maker{};
  }

    #define needful_nocast  needful::g_nocast_maker +
#endif


/***[[ nocast_0, struct_0, array_0: POLYMORPHIC ZERO-INITIALIZERS ]]**********
**
** Docs: https://needful.metaeducation.com/nocast#needful_nocast_0
**
** `nocast_0` has the same meaning as `nocast 0` but just compiles faster.
** It is required for macros used with Option(T) and Result(T) when compiled
** in C++ environments w/o enabling the NEEDFUL_CPP_ENHANCED files.
**
** (These also have general utility in any code that targets both C and C++!)
**/

#if !defined(__cplusplus)
    #define needful_nocast_0    0    /* may need warning disablement [A] */
    #define needful_struct_0  { 0 }  /* {} works only in C23 or later... */
    #define needful_array_0   { 0 }

    #define needful_struct_default  { 0 }  /* same as struct_0 in C */
#else
  namespace needful {
    struct Nocast0Struct {  /* same meaning as `nocast 0`, compiles faster */
        template<class To>
        constexpr operator To() const {
            return NocastConvert<To, int>::Do_Conversion(0);
        }
    };

    struct Struct0Struct {  /* more safe than `#define needful_struct0 {}` */
        template <typename T>
        constexpr operator T() const {
            static_assert(std::is_class<T>::value,
                "struct_0 ONLY used on structs, not scalar types!");

            static_assert(std::is_trivially_default_constructible<T>::value,
                "Zero-init with {} only guaranteed in C-compatible structs!");

            return T{};  /* guarantees recursive zero-initialization */
        }
    };

    struct StructDefaultStruct {  /* more lenient, allows user constructors */
        template <typename T>
        constexpr operator T() const {
            static_assert(std::is_class<T>::value,
                "struct_default ONLY used on structs, not scalar types!");

            return T{};  /* zero-initialization not guaranteed */
        }
    };
  }

    #define needful_nocast_0  needful::Nocast0Struct{}
    #define needful_struct_0  needful::Struct0Struct{}
    #define needful_array_0   {}  /* can't promise usage only w/arrays :-( */

    #define needful_struct_default  needful::StructDefaultStruct{}
#endif


/***[[ Option(T): EXPLICITLY DISENGAGE-ABLE TYPE ]]***************************
**
** Docs: https://needful.metaeducation.com/option
**
** Option() provides targeted functionality in the vein of Rust's `Option`
** and C++'s `std::optional`:
**
**     Option(char*) abc = "abc";
**     Option(char*) xxx = none;  // nullptr is none synonym if pointer type
**
**     if (abc)
**        printf("abc is truthy, so `unwrap abc` is safe!\n")
**
**     if (! xxx)
**        printf("XXX is falsey, so don't `unwrap xxx`...\n")
**
**     char* s1 = abc;                  // ! compile-time error !
**     Option(char*) s2 = abc;          // legal
**
**     char* s3 = unwrap xxx;           // ! runtime error (if debug build) !
**     char* s4 = opt xxx;              // gets nullptr out (no null-check)
**
** It leverages the natural boolean coercibility of the contained type.  So
** you can use it with things like pointers, integers or enums...anywhere the
** C build can treat 0 as a "falsey" state.
*/

typedef enum {
    NEEDFUL_NONE_ENUM_0 = 0
} NeedfulNoneEnum;

#define NeedfulNone  NeedfulNoneEnum  /* unique type if C++ enhanced */
#define needful_none  needful_nocast_0  /* unique type if C++ enhanced */

#define NeedfulOption(T)  T

#define needful_opt  /* no-op in C build */

#define needful_postfix_extract_option  /* no-op in C build */


/****[[ Fallible(T): LIKE Option(T) BUT RESULT MUST BE USED ]]****************
**
** Needful's Result(T) uses thread-global variables to multiplex an error on
** top of an arbitrary return value.  Fallible(T) does something simpler: it
** just lets you mark a return value as Fallible(T) to be a [[nodiscard]]
** version of an Option(T) (like Rust's #[must_use]).
**/

#define NeedfulFallible(T)  T
#define needful_unwrap_fallible  /* no-op in C build */
#define needful_infallible  /* no-op in C build */


/****[[ SCOPE_GUARD: PROTECT FROM UNSAFE MACRO USAGES ]]**********************
**
** Creates a unique-named unused variable so that macros like trap/require/
** assume produce a compile error if used in an unbraced branch slot.
*/

#define NEEDFUL_NOOP  ((void)0)

#define NEEDFUL_PASTE2(a, b)  a##b
#define NEEDFUL_PASTE1(a, b)  NEEDFUL_PASTE2(a, b)

#define NEEDFUL_UNIQUE_NAME(base)  NEEDFUL_PASTE1(base, __LINE__)

#if defined(NDEBUG)
    #define NEEDFUL_SCOPE_GUARD  NEEDFUL_NOOP
#else
    #define NEEDFUL_SCOPE_GUARD /* Clang v15 needs braces in switch cases */ \
        int NEEDFUL_UNIQUE_NAME(_statement_must_be_in_braces_); \
        NEEDFUL_UNUSED(NEEDFUL_UNIQUE_NAME(_statement_must_be_in_braces_))
#endif


/****[[ unreachable: INDICATE DIVERGENCE ]]***********************************
**
** Docs: https://needful.metaeducation.com/unreachable
**
** In Release mode (-O3/NDEBUG),the compiler aggressively deletes the dead
** branch and optimizes switch jump tables. Triggers an assert in Debug mode.
**
** Uses `needful_nocast_0` and `needful_struct_0` to polymorphically satisfy
** function return signatures without manual type boilerplate.  Depending
** on how much compatibility you need in C compilers, you could use the
** needful_dead_end
**
** (Note you can `#undef needful_builtin_unreachable` and redefine if needed.)
**/

#if defined(_MSC_VER)  /* MSVC __assume(0) even works with /Ob0 (no-inline) */
    #define needful_builtin_unreachable  __assume(0)
#elif defined(__GNUC__) || defined(__clang__)  /* GCC/Clang take the hint */
    #define needful_builtin_unreachable  __builtin_unreachable()
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
    #include <stddef.h>  /* C23 has standard unreachable() in <stddef.h> */
    #define needful_builtin_unreachable  unreachable()
#else
    #define needful_builtin_unreachable  ((void)0)  /* fall back to a no-op */
#endif

#define needful_unreachable  do { \
    NEEDFUL_ASSERT(false); \
    needful_builtin_unreachable; \
    return needful_nocast_0; \
  } while (0)

#define needful_unreachable_void  do { \
    NEEDFUL_ASSERT(false); \
    needful_builtin_unreachable; \
    return; \
  } while (0)

#define needful_unreachable_struct(T)  do { \
    NEEDFUL_ASSERT(false); \
    needful_builtin_unreachable; \
    return (T)needful_struct_0; \
  } while (0)

  #define needful_unreachable_array  do { \
    NEEDFUL_ASSERT(false); \
    needful_builtin_unreachable; \
    return needful_array_0; \
  } while (0)


/****[[ NORETURN shim ]]******************************************************
**
** The Result(T) macros use noreturn in Needful_Panic_Abruptly(), so we define
** it here in a way that works in both C and C++.
**
** `needful_dead_end;` can dodge using return-type-correct unreachable forms
** (e.g. needful_unreachable_struct(T)) in *MOST* C compilers.  But some will
** still warn about "control reaches end of non-void function".  If you seek
** maximum generality in C environments, use `needful_unreachable_*` macros.
*/

 #if !defined(__cplusplus)
  #if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define NEEDFUL_NORETURN  _Noreturn  /* C99 and higher */
  #else
    #define NEEDFUL_NORETURN  /* no-op in C pre-C11 */
  #endif
#else
    #define NEEDFUL_NORETURN  [[noreturn]]  /* C++11 and higher */
#endif

NEEDFUL_NORETURN static inline void needful_dead_end_inline(void) {
    NEEDFUL_ASSERT(false);
    needful_builtin_unreachable;
}

#define needful_dead_end  needful_dead_end_inline()


/****[[ Result(T): MULTIPLEXED ERROR AND RETURN RESULT ]]*********************
**
** Docs: https://needful.metaeducation.com/result
**
** These macros provide a C/C++-compatible mechanism for propagating and
** handling errors in a style similar to Rust's `Result<T, E>`, all without
** requiring exceptions or setjmp/longjmp in C++ builds.
**
** You can write code like this:
**
**     Result(int) Some_Func(int x) {
**         if (x < 304)
**             return fail ("the value is too small");
**         return x + 20;  // ^-- sets thread-local state
**     }
**
**     Result(int) Other_Func(void) {
**         trap (
**           int y = Some_Func(1000)
**         );
**         assert(y == 1020);
**
**         trap (
**           int z = Some_Func(10)  // embedded `return` bubbles the failure
**         );
**         printf("this would never be reached...");
**
**         return z;
**     }
**
** Also of particular note is the syntax for catching "exceptional" cases
** (though again, not C++ exceptions and not longjmps).  This syntax looks
** particularly natural due to clever use of a `for` loop to get a scope:
**
**     int result = Some_Func(10 + 20) except (Error* e) {
**         printf("caught an error: %s\n", e->message);  // e scoped to block
**     }
**     else {
**         printf("didn't have an error, and else clause works!!");
**     }
**
** So the macros enable a shockingly literate style of programming that is
** portable between C and C++, avoids exceptions and longjmps, and provides
** clear, explicit error handling and propagation.
*/

#define NeedfulResult(T)  T

#define NEEDFUL_RESULT_0  needful_nocast_0  /* unique type if C++ enhanced */

#define needful_make_failure(...) \
    (Needful_Assert_Not_Failing(), \
        Needful_Set_Failure(__VA_ARGS__), \
        NEEDFUL_RESULT_0)

#define needful_panic(...) do { \
    Needful_Assert_Not_Failing(); \
    Needful_Panic_Abruptly(__VA_ARGS__); \
} while (0)

#define needful_postfix_extract_result  /* no-op in C build */

#define needful_return_if_failed(_stmt_) \
    NEEDFUL_SCOPE_GUARD; \
    Needful_Assert_Not_Failing(); \
    _stmt_  needful_postfix_extract_result; \
    if (Needful_Get_Failure()) { \
        return NEEDFUL_RESULT_0; \
    } NEEDFUL_NOOP  /* force require semicolon at callsite */

#define needful_abort_if_failed(_stmt_) \
    NEEDFUL_SCOPE_GUARD; \
    Needful_Assert_Not_Failing(); \
    _stmt_ needful_postfix_extract_result; \
    if (Needful_Get_Failure()) { \
        Needful_Panic_Abruptly(Needful_Test_And_Clear_Failure()); \
        needful_builtin_unreachable; \
    } NEEDFUL_NOOP  /* force require semicolon at callsite */

#define needful_assert_not_failed(_stmt_) \
    NEEDFUL_SCOPE_GUARD; \
    Needful_Assert_Not_Failing(); \
    _stmt_ needful_postfix_extract_result; \
    Needful_Assert_Not_Failing()

#define needful_catch_if_failed(_decl_) \
    /* _stmt_ */ needful_postfix_extract_result; /* v-- see docs RE:_once */ \
    for (_decl_ = Needful_Get_Failure(), *_once = nullptr; !_once; ++_once) \
      if (Needful_Test_And_Clear_Failure()) /* allow else clause to attach */
        /* {body} implicitly picked up after macro by for, decl is scoped */

#define needful_extract_failure(_expr_) \
    (Needful_Assert_Not_Failing(), _expr_ needful_postfix_extract_result, \
        Needful_Test_And_Clear_Failure())

#if defined(NEEDFUL_DECLARE_RESULT_HOOKS) && NEEDFUL_DECLARE_RESULT_HOOKS

#include <stdio.h>   /* fprintf, fflush, stderr */
#include <stdlib.h>  /* exit */

const char* g_needful_failure;  /* can only define once in project */

const char* Needful_Test_And_Clear_Failure() {
    const char* e = g_needful_failure;
    g_needful_failure = (const char*)0;
    return e;
}

#define Needful_Get_Failure() \
    g_needful_failure

#define Needful_Set_Failure(error) \
   (g_needful_failure = error)

NEEDFUL_NORETURN void Needful_Panic_Abruptly(const char* error) {
    fprintf(stderr, "Panic: %s\n", error);
    fflush(stderr);
    exit(1);
}

#define Needful_Assert_Not_Failing() \
    NEEDFUL_ASSERT(g_needful_failure == (const char*)0)

#endif  /* NEEDFUL_DECLARE_RESULT_HOOKS */


/****[[ NULLPTR-REACTIVE MACROS ]]********************************************
**
** These macros react to the presence of a nullptr in the expression, and can
** also deal with Option(T) expressions.
**/

#define needful_is_nullptr(_expr_) \
    ((_expr_ needful_postfix_extract_option) == nullptr)

#define needful_return_if_nullptr(_expr_) \
    do { if (needful_is_nullptr(_expr_)) { return needful_none; } } while (0)

#define needful_abort_if_nullptr(_expr_) \
    do { if (needful_is_nullptr(_expr_)) { abort(); } } while (0)

#define needful_tolerate_if_nullptr(_expr_) \
    NEEDFUL_USED(needful_is_nullptr(_expr_))


/****[[ Sink(T) / Init(T): INDICATE FUNCTION OUTPUT PARAMETERS ]]***********
**
** Docs: https://needful.metaeducation.com/contra
**
** The idea behind a Sink() is to be able to mark on a function's interface
** when a function argument passed by pointer is intended as an output.
** This has benefits of documentation, and can also be given some teeth by
** scrambling the memory that the pointer points at (so long as it isn't an
** "in-out" parameter).
**
** But there's another feature implemented here, which is *covariance* for
** input parameters, and "contravariance" for output parameters.  This only
** matters if you're applying inheritance selectively to datatypes in C++
** builds to add checking to your C codebase.  See the implementation of
** contravariance in /needful-enhanced/needful-contra.hpp for more details.
*/

#define NeedfulSink(T)  T *
#define NeedfulInit(T)  T *

#define NeedfulContra(T)  T *
#define NeedfulExact(T)  T  /* precise type */


/****[[ known(T,expr): COMPILE-TIME TYPE ASSERTION INSIDE MACROS ]]**********
**
** Docs: https://needful.metaeducation.com/known
**
** Type-checks an expression against T at compile-time without runtime cost.
** Uses no function template, so not a call even in unoptimized debug builds!
**
**      int* ptr = ...;
**      void *p = known(int*, ptr);  // succeeds at compile-time
**
**      char* ptr = ...;
**      void *p = known(int*, ptr);   // ERROR: fails at compile-time
**
** As with casts, lenient forms pass through a const T* if the input is const,
** vs. needing `const T*` at callsites.  `rigid_known()` enforces mutability.
*/

#define needful_lenient_known(T,expr)        (expr)
#define needful_rigid_known(T,expr)          (expr)

#define needful_rigid_known_not(T,expr)      (expr)
#define needful_lenient_known_not(T,expr)    (expr)

#define needful_rigid_known_any(TLIST,expr)  (expr)
/* no needful_lenient_known_any yet */

#define needful_known_lvalue(variable)  (*&variable)

#define needful_lenient_exactly(T,expr)      (expr)
#define needful_rigid_exactly(T,expr)        (expr)

#define needful_known_literal(T,expr)        (expr)


/****[[ ENABLEABLE: Argument Type Subsetting ]]*******************************
*/

#define ENABLE_IF_EXACT_ARG_TYPE(...)
#define DISABLE_IF_EXACT_ARG_TYPE(...)
#define ENABLEABLE(T, name)  T name


/****[[ VISIBLE (AND HOOKABLE!) ERGONOMIC CASTS ]]****************************
**
** Docs: https://needful.metaeducation.com/cast
**
** These macros for casting provide *easier-to-spot* variants of parentheses
** cast (so you can see where the casts are in otherwise-parenthesized
** expressions).  They also document the semantic purpose of the cast.
**
** The C definitions are trivial: they all act like a parenthesized cast.  But
** NEEDFUL_CPP_ENHANCED builds enforce narrower policies.  Also, the casts are
** designed to be "hookable" in C++.  These can be compile-time checks (to
** limit what types can be cast to what), as well as runtime checks that can
** actually validate the bits being cast are legal for the target type!
**
** All casts have zero overhead in release builds.  And Needful bends over
** backwards so that debug builds (which won't inline functions) avoid using
** functions where possible--almost everything is done at compile-time.
**
*****[[ CAST SELECTION GUIDE ]]***********************************************
**
** SAFETY LEVEL
**    - Hookable cast:            cast()       // safe default w/debug hooks
**    - Unhooked/unchecked cast:  raw_cast()   // e.g. for fresh malloc()s
**    - Raw cast of valid data:   fast_cast()  // to avoid hooks on hot paths
**
** POINTER CONSTNESS
**    - Adding mutability:         m_cast()    // const T* => T*
**    - Preserve mutability:       <lenient>   // "to TB*" is TA* => TB* or...
**                                               // const TA* => const TB*
**    - Enforce mutability:        <rigid>     // "to const TB*" is const TB*
**                                               // "to TB*" needs mutable TA*
**
** TYPE CONVERSIONS
**    - intlike to intlike:        i_cast()    // enum E => int, int => enum E
**    - i_cast unwrap optimize:    ii_cast()   // Option(int) => int
**    - Non-integral to integral:  p_cast()    // T* => intptr_t
**    - Function to function:      f_cast()    // ret1(*)(...) => ret2(*)(...)
**    - va_list to void*:          v_cast()    // va_list* <=> void*
**
** HIERARCHICAL CASTS
**    - Up (Derived* => Base*):    upcast()     // ensures types are related
**    - Down (Base* => Derived*):  downcast     // operator: `downcast expr`
**
** LAST RESORT
**    - Higher visibility C cast:  c_cast()     // same as parentheses cast
*/

#define needful_lenient_hookable_cast(T,expr)       ((T)(expr))
#define needful_lenient_unhookable_cast(T,expr)     ((T)(expr))

#define needful_cast /* (T,expr) */           needful_lenient_hookable_cast
#define needful_raw_cast /* (T,expr) */       needful_lenient_unhookable_cast

#if defined(NEEDFUL_FAST_CAST_IS_SLOW)
    #define needful_fast_cast /* (T,expr) */   needful_lenient_hookable_cast
#else
    #define needful_fast_cast /* (T,expr) */   needful_lenient_unhookable_cast
#endif

#define needful_rigid_hookable_cast(T,expr)    ((T)(expr))
#define needful_rigid_unhookable_cast(T,expr)  ((T)(expr))

#if !defined(__cplusplus)
    #define needful_mutable_cast(T,expr) \
        ((T)(expr))  /* C allows const to be cast away via parentheses */
#else
    #define needful_mutable_cast(T,expr) \
        const_cast<T>((const T)(expr))  /* C++ mandates a const_cast<> */
#endif

#define needful_pointer_cast(T,expr)    ((T)(expr))
#define needful_integer_cast(T,expr)    ((T)(expr))
#define needful_function_cast(T,expr)   ((T)(expr))
#define needful_valist_cast(T,expr)     ((T)(expr))

#if !defined(__cplusplus)
    #define needful_hookable_downcast    (void*)
    #define needful_unhookable_downcast  (void*)
#else
    #define needful_hookable_downcast    needful_nocast
    #define needful_unhookable_downcast  needful_nocast
#endif

#define needful_downcast /* (T,expr) */        needful_hookable_downcast
#define needful_raw_downcast /* (T,expr) */    needful_unhookable_downcast
#define needful_upcast /* (T,expr) */          needful_c_cast

#if defined(NEEDFUL_FAST_CAST_IS_SLOW)
    #define needful_fast_downcast /* (T,expr) */   needful_hookable_downcast
#else
    #define needful_fast_downcast /* (T,expr) */   needful_unhookable_downcast
#endif

#define needful_c_cast(T,...) \
    ((T)(__VA_ARGS__))


/****[[ c_cast_known: WORKAROUND FOR cast(T, known(T, ...)) ]]****************
**
** `cast(T, known(T, expr))` has macro-expansion issues when T contains
** commas.  This combined form sidesteps the problem using c_cast() directly.
**/

#define needful_rigid_c_cast_known(T,expr) \
    needful_c_cast(T,expr)

#define needful_lenient_c_cast_known(T,expr) /* const passthru const [1] */ \
    needful_c_cast(T,expr)


/****[[ NEEDFUL_DOES_CORRUPTIONS + CORRUPTION SEED/DOSE ]]********************
**
** See Corrupt_If_Needful() for more information.
**
** 1. We do not do Corrupt_If_Needful() with static analysis, because tha
**    makes variables look like they've been assigned to the static analyzer.
**    It should use its own notion of when things are "garbage" (e.g. this
**    allows reporting of use of unassigned values from inline functions.)
**
** 2. Generate some variability, but still deterministic.
*/

#if !defined(NEEDFUL_DOES_CORRUPTIONS)
   #define NEEDFUL_DOES_CORRUPTIONS  0
#endif

#if (! NEEDFUL_DOES_CORRUPTIONS)
    #define Corrupt_If_Needful(var)  NEEDFUL_NOOP
    #define Assert_Corrupted_If_Needful(ptr)  NEEDFUL_NOOP
#else
    /* STATIC_ASSERT(! DEBUG_STATIC_ANALYZING); */  /* [1] */

    #include <string.h>  /* for memset */

    #define Corrupt_If_Needful(var) \
        memset(&(var), 0xBD, sizeof(var))  /* C99 fallback mechanism */

    #define Assert_Corrupted_If_Needful(var) do { \
        if (*(unsigned char*)(&(var)) != 0xBD)  /* cheap check vs. loop */ \
            NEEDFUL_ASSERT("Expected variable to be corrupt and it was not"); \
    } while (0)
#endif

#define NEEDFUL_USES_CORRUPT_HELPER  0


/****[[ MARK USED/UNUSED VARIABLES ]]*****************************************
**
** Used in coordination with the `-Wunused-variable` setting of the compiler.
**
** NEEDFUL_USED() marks a variable as used, so the compiler doesn't warn.
**
** In the NEEDFUL_CPP_ENHANCED builds, NEEDFUL_UNUSED() can be redefined to
** actually randomize non-const variable contents to help detect use of
** something that was meant to be unused.
*/

#define NEEDFUL_USED(...)    ((void)(__VA_ARGS__))
#define NEEDFUL_UNUSED(...)  ((void)(__VA_ARGS__))


/****[[ STATIC_ASSERT, STATIC_IGNORE, STATIC_FAIL ]]**************************
**
** 1. C11 added _Static_assert(), so use that when available in C builds.
**    Pre-C11 C has too many edge cases for a portable shim, so it remains
**    a no-op there.  The C++ enhanced build enforces STATIC_ASSERT in all
**    standards (we avoid non-essential __cplusplus #ifdefs in needful.h)
*/

#define NEEDFUL_STATIC_IGNORE(expr) /* https://stackoverflow.com/q/53923706 */ \
    struct NEEDFUL_UNIQUE_NAME(needful_noop_)  /* callsite semicolon trick */

#if !defined(__cplusplus) \
    && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define NEEDFUL_STATIC_ASSERT(cond) \
        _Static_assert((cond), #cond)
#else
    #define NEEDFUL_STATIC_ASSERT(cond) \
        NEEDFUL_STATIC_IGNORE(cond)  /* pre-C11 C version is noop [1] */
#endif

#define NEEDFUL_STATIC_ASSERT_NOT(cond) \
    NEEDFUL_STATIC_ASSERT(! (cond))

#define NEEDFUL_STATIC_FAIL(msg) \
    typedef int static_fail_##msg[-1]  /* message has to be a C identifier */


/****[[ STATIC ASSERT LVALUE TO HELP EVIL MACRO USAGE ]]**********************
**
** Macros that repeat an argument are dangerous if it has a side-effect.
** Asserting the argument is an lvalue (assignable) rules out temporaries
** and most side-effecting expressions at the callsite.
*/

#define NEEDFUL_STATIC_ASSERT_LVALUE(variable) \
    NEEDFUL_USED(*&variable)


/****[[ NO-OP STATIC_ASSERTS THAT VALIDATE EXPRESSIONS ]]*********************
**
** These are utilized by the commentary macros.  They are no-ops in C, but
** the C++ overrides can help keep comments current by ensuring the
** expressions they take will compile (hence variables named by them are
** valid, etc.)
*/

#define NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL(expr)  NEEDFUL_NOOP

#define NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID(expr) NEEDFUL_NOOP


/****[[ COMMENTS WITH TEETH ]]************************************************
**
** Docs: https://needful.metaeducation.com/comments
**
** The idea beind shorthands like `possibly()` is to replace comments that are
** carrying information about something that *might* be true:
**
**     int i = Get_Integer(...);  // i may be < 0
**
** Even the C no-op version of `possibly()` lets you break it out so the
** visual flow is better, and less likely to overflow a line:
**
**     int i = Get_Integer(...);
**     possibly(i < 0);
**
** But the C++ overload of STATIC_ASSERT_DECLTYPE_BOOL() allows it to make
** sure your expression is well-formed at compile-time.  This pattern is
** applied to the others as well, keeping your identifiers up to date.
**
** Uppercase versions, can be used in global scope (more limited abilities)
*/

#define needful_possibly /* (cond) */     NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL
#define needful_impossible /* (cond) */   NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL
#define needful_definitely /* (cond) */   NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL

#define needful_inapplicable /* (expr) */ NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID
#define needful_unnecessary /* (expr) */  NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID
#define needful_dont /* (expr) */         NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID
#define needful_cant /* (expr) */         NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID
#define needful_heeded /* (expr) */       NEEDFUL_USED

#define NEEDFUL_POSSIBLY /* (cond) */        NEEDFUL_STATIC_IGNORE
#define NEEDFUL_IMPOSSIBLE /* (cond) */      NEEDFUL_STATIC_ASSERT_NOT
#define NEEDFUL_DEFINITELY /* (cond) */      NEEDFUL_STATIC_ASSERT

#define NEEDFUL_UNNECESSARY /* (expr) */     NEEDFUL_STATIC_IGNORE
#define NEEDFUL_DONT /* (expr) */            NEEDFUL_STATIC_IGNORE
#define NEEDFUL_CANT /* (expr) */            NEEDFUL_STATIC_IGNORE
/* NEEDFUL_HEEDED makes no sense in global scope, right? */


/****[[ NODISCARD shim ]]*****************************************************
**
** If you are using a C++17 compiler or higher, this will be redefined by the
** C++ enhancements as `[[nodiscard]]`.  It's very important for noticing
** that you haven't handled a Result(T) return value from a function with
** some kind of `trap`/`require`/`except`/`assume`/`rescue`.  This also helps
** catch things like writing `fail(...)` instead of `return fail (...)`.
*/

#define NEEDFUL_NODISCARD  /* default to no-op in C or pre-C++17 */


/****[[ ALWAYS_INLINE shim ]]**************************************************
**
** Hints to the compiler to inline a function even in non-optimized builds.
** In GCC/Clang this is __attribute__((always_inline)) and in MSVC it is
** __forceinline.  When neither is available it falls back to plain `inline`.
**
** The primary use is marking the trivial no-op default CastHook::Validate_Bits
** so that debug builds pay zero cost when no hook has been registered.
*/

#if defined(__GNUC__) || defined(__clang__)
    #define NEEDFUL_ALWAYS_INLINE  __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
    #define NEEDFUL_ALWAYS_INLINE  __forceinline
#else
    #define NEEDFUL_ALWAYS_INLINE  inline
#endif


/*****************************************************************************
**
**  OPTIONAL SHORHANDS FOR THE `NEEDFUL_XXX` MACROS AS JUST `XXX` MACROS
**
******************************************************************************
**
** These are SIMPLE ALIASES, and the parameterization is given as a comment
** for documentation purposes.  There can be big breakages when an expansion
** might produce commas inside angle brackets in C++ builds, and variadic
** forwarding doesn't always work around that.  It's cleaner and safer (and
** faster at compile time) to do it this way.
*/

#if !defined(NEEDFUL_DEFINE_ALL_SHORTHANDS)
    #define NEEDFUL_DEFINE_ALL_SHORTHANDS  0
#endif

#if !defined(NEEDFUL_OPTION_SHORTHANDS)
    #define NEEDFUL_OPTION_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_OPTION_SHORTHANDS
    #define Need /* (T) */          NeedfulNeed
    #define unwrap /* ... */        needful_unwrap
    #define needed /* ... */        needful_needed

    #define None                    NeedfulNone
    #define none                    needful_none

    #define Option /* (T) */        NeedfulOption
    #define opt /* ... */           needful_opt
#endif

#if !defined(NEEDFUL_CAST_SHORTHANDS)
    #define NEEDFUL_CAST_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_CAST_SHORTHANDS
    #define cast /* (T,...) */      needful_cast
    #define raw_cast /* (T,...) */  needful_raw_cast
    #define fast_cast /* (T,...) */  needful_fast_cast

    #define c_cast /* (T,...) */    needful_c_cast

    #define m_cast /* (T,...) */    needful_mutable_cast

    #if !defined(NEEDFUL_ICAST_SLOW_BUILD)  /* default off, fast builds [1] */
        #define NEEDFUL_ICAST_SLOW_BUILD  0
    #endif
    #if NEEDFUL_ICAST_SLOW_BUILD
      #define i_cast /* (T,...) */    needful_integer_cast
    #else
      #define i_cast /* (T,...) */    needful_c_cast
    #endif
    #define ii_cast /* (T,...) */   needful_integer_cast  /* see [1] */

    #define p_cast /* (T,...) */    needful_pointer_cast
    #define f_cast /* (T,...) */    needful_function_cast
    #define v_cast /* (T,...) */    needful_valist_cast

    #define nocast /* ... */        needful_nocast

    #define downcast /* ... */      needful_downcast
    #define raw_downcast /* ... */  needful_raw_downcast
    #define fast_downcast /* ... */ needful_fast_downcast

    #define upcast /* (T,...) */    needful_upcast
#endif

#if !defined(NEEDFUL_RESULT_SHORTHANDS)
    #define NEEDFUL_RESULT_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_RESULT_SHORTHANDS
    #define Result /* (T) */             NeedfulResult

    #define make_failure /* (...) */     needful_make_failure
    #define panic /* (...) */            needful_panic

    #define return_if_failed /* (stmt) */    needful_return_if_failed
    #define abort_if_failed /* (stmt) */     needful_abort_if_failed
    #define assert_not_failed /* (stmt) */   needful_assert_not_failed
    #define catch_if_failed /* (decl) {} */  needful_catch_if_failed
    #define extract_failure /* (expr) */     needful_extract_failure
#endif

#if !defined(NEEDFUL_NULLPTR_SHORTHANDS)
    #define NEEDFUL_NULLPTR_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_NULLPTR_SHORTHANDS
    #define is_nullptr /* (_expr) */            needful_is_nullptr
    #define return_if_nullptr /* (_expr_) */    needful_return_if_nullptr
    #define abort_if_nullptr /* (_expr_) */     needful_abort_if_nullptr
    #define tolerate_if_nullptr /* (_expr_) */  needful_tolerate_if_nullptr
#endif

#if !defined(NEEDFUL_CONTRA_SHORTHANDS)
    #define NEEDFUL_CONTRA_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_CONTRA_SHORTHANDS
    #define Sink /* (T) */          NeedfulSink
    #define Init /* (T) */          NeedfulInit
    #define Contra /* (T) */        NeedfulContra
    #define Exact /* (T) */         NeedfulExact
#endif

#if !defined(NEEDFUL_KNOWN_SHORTHANDS)
    #define NEEDFUL_KNOWN_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_KNOWN_SHORTHANDS
    #define rigid_known /* (T,expr) */           needful_rigid_known
    #define rigid_known_not /* (T,expr) */       needful_rigid_known_not
    #define rigid_known_any /* ((T,...),expr) */ needful_rigid_known_any

    #define lenient_known /* (T,expr) */         needful_lenient_known
    #define lenient_known_not /* (T,expr) */     needful_lenient_known_not
    /*  no lenient_known_any at this time */

    #define known /* (T,expr) [3] */             needful_lenient_known
    #define known_not /* (T,expr) [3] */         needful_lenient_known_not
    #define known_any /* ((T,...),expr) */       needful_rigid_known_any

    #define rigid_c_cast_known /* (T,expr) */    needful_rigid_c_cast_known
    #define lenient_c_cast_known /* (T,expr) */  needful_lenient_c_cast_known

    #define c_cast_known /* (T,expr) [2] */      needful_lenient_c_cast_known

    #define known_lvalue /* (var) */             needful_known_lvalue

    #define lenient_exactly /* (T,expr) [3] */   needful_lenient_exactly
    #define rigid_exactly /* (T,expr) [3] */     needful_rigid_exactly
    #define exactly /* (T,expr) [3] */           needful_lenient_exactly

    #define known_literal /* (T,expr) [3] */     needful_known_literal
#endif

#if !defined(NEEDFUL_COMMENT_SHORTHANDS)
    #define NEEDFUL_COMMENT_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_COMMENT_SHORTHANDS
    #define possibly /* (cond) */    needful_possibly
    #define impossible /* (cond) */  needful_impossible
    #define definitely /* (cond) */  needful_definitely

    #define inapplicable /* (expr) */ needful_inapplicable
    #define unnecessary /* (expr) */  needful_unnecessary
    #define dont /* (expr) */         needful_dont
    #define cant /* (expr) */         needful_cant
    #define heeded /* (expr) */       needful_heeded

    #define POSSIBLY /* (cond) */        NEEDFUL_POSSIBLY
    #define IMPOSSIBLE /* (cond) */      NEEDFUL_IMPOSSIBLE
    #define DEFINITELY /* (cond) */      NEEDFUL_DEFINITELY

    #define UNNECESSARY /* (expr) */     NEEDFUL_UNNECESSARY
    #define DONT /* (expr) */            NEEDFUL_DONT
    #define CANT /* (expr) */            NEEDFUL_CANT
#endif

#if !defined(NEEDFUL_STATIC_ASSERT_SHORTHANDS)
    #define NEEDFUL_STATIC_ASSERT_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_STATIC_ASSERT_SHORTHANDS
    #if !defined(STATIC_ASSERT)
        #define STATIC_ASSERT /* (...) */  NEEDFUL_STATIC_ASSERT
    #endif

    #if !defined(STATIC_ASSERT_LVALUE)
        #define STATIC_ASSERT_LVALUE /* (var) */ NEEDFUL_STATIC_ASSERT_LVALUE
    #endif

    #if !defined(STATIC_IGNORE)
        #define STATIC_IGNORE /* (...) */  NEEDFUL_STATIC_IGNORE
    #endif

    #if !defined(STATIC_FAIL)
        #define STATIC_FAIL /* (...) */  NEEDFUL_STATIC_FAIL
    #endif
#endif

#if !defined(NEEDFUL_USAGE_SHORTHANDS)
    #define NEEDFUL_USAGE_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_USAGE_SHORTHANDS
  #if !defined(USED)
    #define USED /* (...) */  NEEDFUL_USED
  #endif

  #if !defined(UNUSED)
    #define UNUSED /* (...) */  NEEDFUL_UNUSED
  #endif

  #if !defined(NOOP)
    #define NOOP  NEEDFUL_NOOP
  #endif

  #if !defined(NODISCARD)
    #define NODISCARD  NEEDFUL_NODISCARD
  #endif

  #if !defined(ALWAYS_INLINE)
    #define ALWAYS_INLINE  NEEDFUL_ALWAYS_INLINE
  #endif
#endif


/*****************************************************************************
**
**  OPTIONAL C++ OVERRIDES FOR ABOVE DEFINITIONS TO BRING THE MACROS TO LIFE
**
******************************************************************************
**
** When NEEDFUL_CPP_ENHANCED builds are activated, then supplemental C++
** headers will #undef the simple definitions given above, and redefine them
** with elaborately-designed machinery!
*/

#if !defined(NEEDFUL_CPP_ENHANCED)
    #define NEEDFUL_CPP_ENHANCED  0  // Note: can still be compiled as C++
#endif

#define NEEDFUL_VERSION_MAJOR  0
#define NEEDFUL_VERSION_MINOR  0
#define NEEDFUL_VERSION_PATCH  0

#define NEEDFUL_VERSION_ENCODE(major, minor, patch) \
    (((major) * 10000) + ((minor) * 100) + (patch))

#define NEEDFUL_VERSION \
    NEEDFUL_VERSION_ENCODE( \
        NEEDFUL_VERSION_MAJOR, \
        NEEDFUL_VERSION_MINOR, \
        NEEDFUL_VERSION_PATCH \
    )

#if !defined(NEEDFUL_NEED_USES_WRAPPER)
  #define NEEDFUL_NEED_USES_WRAPPER  NEEDFUL_CPP_ENHANCED
#endif

#if !defined(NEEDFUL_OPTION_USES_WRAPPER)
  #define NEEDFUL_OPTION_USES_WRAPPER  NEEDFUL_CPP_ENHANCED
#endif

#if !defined(NEEDFUL_RESULT_USES_WRAPPER)
  #define NEEDFUL_RESULT_USES_WRAPPER  NEEDFUL_CPP_ENHANCED
#endif

#if !defined(NEEDFUL_CONTRAS_USE_WRAPPER)
  #define NEEDFUL_CONTRAS_USE_WRAPPER  NEEDFUL_CPP_ENHANCED
#endif

#if !defined(NEEDFUL_CAST_CALLS_HOOKS)
  #define NEEDFUL_CAST_CALLS_HOOKS  NEEDFUL_CPP_ENHANCED
#endif

#if NEEDFUL_CPP_ENHANCED
    #if !defined(__cplusplus)
        #error "NEEDFUL_CPP_ENHANCED requires building your code as C++"
    #elif (__cplusplus < 201103L) && (!defined(_MSC_VER) || _MSC_VER < 1900)
        #error "NEEDFUL_CPP_ENHANCED requires C++11 or later"
    #endif

    #include "needful-enhanced/cplusplus-needfuls.hpp"
#else
    #if NEEDFUL_OPTION_USES_WRAPPER
        #error "NEEDFUL_OPTION_USES_WRAPPER requires NEEDFUL_CPP_ENHANCED"
    #endif
    #if NEEDFUL_RESULT_USES_WRAPPER
        #error "NEEDFUL_RESULT_USES_WRAPPER requires NEEDFUL_CPP_ENHANCED"
    #endif
    #if NEEDFUL_CONTRAS_USE_WRAPPER
        #error "NEEDFUL_CONTRAS_USE_WRAPPER requires NEEDFUL_CPP_ENHANCED"
    #endif
    #if NEEDFUL_CAST_CALLS_HOOKS
        #error "NEEDFUL_CAST_CALLS_HOOKS requires NEEDFUL_CPP_ENHANCED"
    #endif
#endif


/*****************************************************************************
**
** You can't do things like return C's `NULL` for an Option(T*).  So nearly
** any Needful client will want a nullptr definition for C.
*/

#if !defined(NEEDFUL_NULLPTR_SHIM)
  #if defined(NEEDFUL_NULLPTR_SHORTHANDS)
    #define NEEDFUL_NULLPTR_SHIM  NEEDFUL_NULLPTR_SHORTHANDS
  #else
    #define NEEDFUL_NULLPTR_SHIM  NEEDFUL_DEFINE_ALL_SHORTHANDS
  #endif
#endif
#if NEEDFUL_NULLPTR_SHIM
  #ifdef __cplusplus
    #include <cstddef>  // defines `using nullptr_t = decltype(nullptr);`
  #else
    #if !defined(nullptr)
      #define nullptr  (void*)0
    #endif
  #endif
#endif


#endif  /* !defined(NEEDFUL_H_INCLUDED) */
