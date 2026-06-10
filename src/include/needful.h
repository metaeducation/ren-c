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
**   Result(T)     Multiplexed error + return value, like Rust's Result<T,E>.
**                 `trap` auto-propagates, `except` catches with scoped
**                 error variables, and `else` attaches naturally:
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
**      Then `#define NEEDFUL_CPP_ENHANCED 1` and build as C++11 (or later).
**      Same source, stricter checking: type mismatches become compile errors.
**
**   3. You can run both build modes in CI: the C build for production,
**      the C++ build to catch bugs.  No code changes needed between them.
**
** The C definitions in this file are intentionally written out in full so
** you can see how simple they are.  Adding Needful to a C project is a
** low-impact proposition: one file, no dependencies, no magic.
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
**
** While compile-time checks are the primary purpose, runtime checks can
** also be enabled to catch nulls or zeroes passed to a Need(T) parameter.
*/

#define NeedfulNeed(T)  T
#define needful_unwrap
#define needful_needed


/****[[ nocast: VOID* IMPLICIT COERCION FOR C/C++ COMPATIBILITY ]]************
**
** Docs: https://needful.metaeducation.com/nocast
**
** C allows void* to coerce implicitly to any pointer type; C++ does not.
** `nocast` bridges this for malloc() results, enum initialization, and other
** places where C's implicit coercions are needed in a C++ build:
**
**     SomeType* ptr = nocast malloc(sizeof(SomeType));  // C and C++ both
**     SomeEnum  e   = nocast 0;                         // C and C++ both
**
** In C the macro is empty.  In C++ it expands to a helper object using
** operator+ (lower precedence than %, which appears in Option/Result) that
** injects the appropriate static_cast to the assignment target's type.
**
** Note: nocast requires C++ mechanics even without NEEDFUL_CPP_ENHANCED,
** hence the unconditional #ifdef __cplusplus block below.
**
** 1. NocastConvert: two cases need special handling.  static_cast
**    already handles void* -> T*, int -> enum, and same-type conversions.
**    The exception: int -> T* must substitute nullptr (C++ forbids
**    implicit int-to-pointer conversion, even for literal 0).  Also,
**    pointer-to-pointer casts (e.g. Derived** -> Base**) fail with
**    static_cast because C++ doesn't support covariant multi-level
**    pointer conversions; a C-style cast replicates C's behavior.
**
** 2. The choice of `+` as the operator to use is intentional due to wanting
**    something with lower precedence than `%` (used in Result and Optional)
**/
#ifndef __cplusplus
    #define needful_nocast
#else
    #include <type_traits>

  namespace needful {
    struct NocastMaker {};

    template<
        class To, class From,
        bool IntToPtr =  /* only one case needs special handling [1] */
            std::is_pointer<To>::value &&
            std::is_integral<
                typename std::remove_reference<From>::type
            >::value
    >
    struct NocastConvert {
        static To Do_Conversion(From f) { return static_cast<To>(f); }
    };

    template<class To, class From>
    struct NocastConvert<To, From, /*IntToPtr*/ true> {
        static To Do_Conversion(From) { return static_cast<To>(nullptr); }
    };

    template<class To, class From>  /* pointer-to-pointer via C-style [1] */
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

    template<class T>  // choice of operator+ is meaningful [2]
    inline NocastHolder<T> operator+(NocastMaker, T v) { return { v }; }

    constexpr NocastMaker g_nocast_maker{};
  }

    #define needful_nocast    needful::g_nocast_maker +
#endif


/***[[ nocast_0: Generically Make 0 Of Any Type ]]****************************
**
** Docs: https://needful.metaeducation.com/nocast#needful_nocast_0
**
** Internal helper that produces a zero coercible to any type T.  Needed so
** that user-defined wrapped types in Option(T) and Result(T) work.
**/

#if !defined(__cplusplus)
    #define needful_nocast_0  0  /* may need warning disablement [A] */
#else
  namespace needful {
    struct Nocast0Struct {
        template<class To>
        constexpr operator To() const {
            return NocastConvert<To, int>::Do_Conversion(0);
        }
    };
  }

    #define needful_nocast_0  needful::Nocast0Struct{}
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
**
** In C this is all a no-op.  In C++ builds a wrapper class enforces that
** Option(T) can't pass where T is expected without explicit unwrapping.  A
** debug runtime check can also be enabled that panics if `unwrap` is called
** on a null/zero Option.
*/

typedef enum {
    NEEDFUL_NONE_ENUM_0 = 0
} NeedfulNoneEnum;

#define NeedfulNone  NeedfulNoneEnum  /* unique type if C++ enhanced */
#define needful_none  needful_nocast_0  /* unique type if C++ enhanced */

#define NeedfulOption(T)  T

#define needful_opt


/****[[ SCOPE_GUARD: PROTECT FROM UNSAFE MACRO USAGES ]]**********************
**
** Creates a unique-named unused variable so that macros like trap/require/
** assume produce a compile error if used in an unbraced branch slot.
**
** 1. Clang 15 makes this more aggressive than necessary (e.g. forbids use
**    in a switch case without braces).
*/

#define NEEDFUL_NOOP  ((void)0)

#if defined(NDEBUG)
    #define NEEDFUL_SCOPE_GUARD  NEEDFUL_NOOP
#else
    #define NEEDFUL_PASTE2(a, b)  a##b
    #define NEEDFUL_PASTE1(a, b)  NEEDFUL_PASTE2(a, b)

    #define NEEDFUL_UNIQUE_NAME(base)  NEEDFUL_PASTE1(base, __LINE__)

    #define NEEDFUL_SCOPE_GUARD /* Clang v15 has some trouble [1] */ \
        int NEEDFUL_UNIQUE_NAME(_statement_must_be_in_braces_); \
        NEEDFUL_UNUSED(NEEDFUL_UNIQUE_NAME(_statement_must_be_in_braces_))
#endif


/****[[ Result(T): MULTIPLEXED ERROR AND RETURN RESULT ]]*********************
**
** Docs: https://needful.metaeducation.com/result
**
** These macros provide a C/C++-compatible mechanism for propagating and
** handling errors in a style similar to Rust's `Result<T, E>`, all without
** requiring exceptions or setjmp/longjmp in C++ builds.
**
** The multiplexing of an Error with the return value type is done with a
** global (or more generally, thread-local) error state.
**
** A key feature is the ability to propagate errors automatically.  So
** instead of having to laboriously write things like:
**
**     Error* Some_Func(int* result, int x) {
**         if (x < 304)
**             return fail ("the value is too small");
**         *result = x + 20;
**         return nullptr;  // no error
**     }
**
**     Error* Other_Func(int* result) {
**         int y;
**         Error* e = Some_Func(&y, 1000);
**         if (e)
**             return e;
**         assert(y == 1020);
**
**         int z;
**         Error* e = Some_Func(&z, 10);
**         if (e)
**             return e;
**         printf("this would never be reached...");
**
**         *result = z;
**         return nullptr;  // no error
**     }
**
** You can write it like this:
**
**     Result(int) Some_Func(int x) {
**         if (x < 304)
**             return fail ("the value is too small");
**         return x + 20;
**     }
**
**     Result(int) Other_Func(void) {
**         trap (
**           int y = Some_Func(1000)
**         );
**         assert(y == 1020);
**
**         trap (
**           int z = Some_Func(10)
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
**         // e scoped to the block
**         printf("caught an error: %s\n", e->message);
**     }
**     else {
**         printf("didn't have an error, and else clause works!!");
**     }
**
** So the macros enable a shockingly literate style of programming that is
** portable between C and C++, avoids exceptions and longjmps, and provides
** clear, explicit error handling and propagation.
**
** In order for these macros to work, they need to be able to test and clear
** the global error state...as well as a flag as to whether the failure is
** divergent or not.  Hence you have to define:
**
**     ErrorType* Needful_Test_And_Clear_Failure()
**     ErrorType* Needful_Get_Failure()
**     void Needful_Set_Failure(ErrorType* error)
**     void Needful_Panic_Abruptly()
**     void Needful_Assert_Not_Failing()  // avoids assert() dependency
**
** These can be functions or macros with the same signature.  They should use
** thread-local state if they're to work in multi-threaded code.
**
** 1. It bears some explanation that the trick to get except() to be able to
**    take an else() clause involves a for loop that runs exactly once.  It
**    accomplishes this using the C99 feature allowing you do declare multiple
**    variables scoped to a for loop *if* they are of the same type.  If we
**    assume your error type is a pointer, then we can declare both the error
**    variable and a dummy pointer `_once` in the loop, and use a pointer
**    increment to ensure the loop only runs once.  :-)
*/

#define NeedfulResult(T)  T

#define NEEDFUL_RESULT_0  needful_nocast_0  /* unique type if C++ enhanced */

#define needful_fail(...) \
    (Needful_Assert_Not_Failing(), \
        Needful_Set_Failure(__VA_ARGS__), \
        NEEDFUL_RESULT_0)

#define needful_panic(...) do { \
    Needful_Assert_Not_Failing(); \
    Needful_Panic_Abruptly(__VA_ARGS__); \
    /* DEAD_END; */ \
} while (0)

#define needful_postfix_extract_result  /* no-op in C build */

#define needful_trap(_stmt_) \
    NEEDFUL_SCOPE_GUARD; \
    Needful_Assert_Not_Failing(); \
    _stmt_  needful_postfix_extract_result; \
    if (Needful_Get_Failure()) { \
        return NEEDFUL_RESULT_0; \
    } NEEDFUL_NOOP  /* force require semicolon at callsite */

#define needful_require(_stmt_) \
    NEEDFUL_SCOPE_GUARD; \
    Needful_Assert_Not_Failing(); \
    _stmt_ needful_postfix_extract_result; \
    if (Needful_Get_Failure()) { \
        Needful_Panic_Abruptly(Needful_Test_And_Clear_Failure()); \
        /* DEAD_END; */ \
    } NEEDFUL_NOOP  /* force require semicolon at callsite */

#define needful_assume(_stmt_) \
    NEEDFUL_SCOPE_GUARD; \
    Needful_Assert_Not_Failing(); \
    _stmt_ needful_postfix_extract_result; \
    Needful_Assert_Not_Failing()

#define needful_except(_decl_) \
    /* _stmt_ */ needful_postfix_extract_result; /* v-- see [1] */ \
    for (_decl_ = Needful_Get_Failure(), *_once = nullptr; !_once; ++_once) \
      if (Needful_Test_And_Clear_Failure()) /* allow else clause to attach */
        /* {body} implicitly picked up after macro by for, decl is scoped */

#define needful_rescue(_expr_) \
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

#if __cplusplus
[[noreturn]]  /* C++11 and higher */
#else
_Noreturn  /* C99 and higher */
#endif
void Needful_Panic_Abruptly(const char* error) {
    fprintf(stderr, "Panic: %s\n", error);
    fflush(stderr);
    exit(1);
}

#define Needful_Assert_Not_Failing() \
    NEEDFUL_ASSERT(g_needful_failure == (const char*)0)

#endif  /* NEEDFUL_DECLARE_RESULT_HOOKS */


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
** the contravariance in %needful-sinks.h for more details.
*/

#define NeedfulSink(T)  T *
#define NeedfulInit(T)  T *

#define NeedfulContra(T)  T *
#define NeedfulExact(T)  T  // precise type


/****[[ known(T,expr): COMPILE-TIME TYPE ASSERTION INSIDE MACROS ]]**********
**
** Docs: https://needful.metaeducation.com/known
**
** Type-checks an expression against T at compile-time with zero runtime
** cost--uses no function template, so it's never a call even in unoptimized
** debug builds:
**
**      int* ptr = ...;
**      void *p = known(int*, ptr);  // succeeds at compile-time
**
**      char* ptr = ...;
**      void *p = known(int*, ptr);   // fails at compile-time
**
** 1. The default (lenient) form passes through a const T* if the input is
**    const, rather than requiring `const T*` at the callsite.  Use
**    `rigid_known()` to enforce mutability.
*/

#define needful_lenient_known(T,expr)        (expr)
#define needful_rigid_known(T,expr)          (expr)

#define needful_rigid_known_not(T,expr)      (expr)
#define needful_lenient_known_not(T,expr)    (expr)

#define needful_rigid_known_any(TLIST,expr)  (expr)
/* no neeedful_lenient_known_any yet */

#define needful_known_lvalue(variable)  (*&variable)

#define needful_lenient_exactly(T,expr)      (expr)
#define needful_rigid_exactly(T,expr)        (expr)

#define needful_known_literal(T,expr)        (expr)


/****[[ ENABLEABLE: Argument Type Subsetting ]]*******************************
*/

#define ENABLE_IF_EXACT_ARG_TYPE(...)
#define DISABLE_IF_EXACT_ARG_TYPE(...)
#define ENABLEABLE(T, name) T name


/****[[ VISIBLE (AND HOOKABLE!) ERGONOMIC CASTS ]]****************************
**
** Docs: https://needful.metaeducation.com/cast
**
** These macros for casting provide *easier-to-spot* variants of parentheses
** cast (so you can see where the casts are in otherwise-parenthesized
** expressions).  They also help document at the callsite what the semantic
** purpose of the cast is.
**
** The definitions in C are trivial--they just act like a parenthesized cast.
** But NEEDFUL_CPP_ENHANCED builds let the casts enforce narrower policies and
** validation logic.  In release builds, the casts have zero overhead.
**
** Also, the casts are designed to be "hookable" so that customized checks
** can be added in C++ builds.  These can be compile-time checks (to limit
** what types can be cast to what)...as well as runtime checks in your debug
** builds, that can actually validate that the data being cast is legal for
** the target type!  ("raw_cast" skips this validation)
**
*****[[ CAST SELECTION GUIDE ]]***********************************************
**
** SAFETY LEVEL
**    - Hookable cast:            cast()      // safe default w/debug hooks
**    - Unhooked/unchecked cast:  raw_cast()  // use with fresh malloc()s
**                                               // ...or critical debug paths
**
** POINTER CONSTNESS
**    - Adding mutability:         m_cast()    // const T* => T*
**    - Preserve mutability:       cast()      // TA* => TB* ...or...
**                               & raw_cast()    // const TA* => const TB*
**
** TYPE CONVERSIONS
**    - intlike to intlike:        i_cast()    // enum E => int, int => enum E
**    - i_cast unwrap optimize:    ii_cast()   // Option(int) => int
**    - Non-integral to integral:  p_cast()    // T* => intptr_t
**    - Function to function:      f_cast()    // ret1(*)(...) => ret2(*)(...)
**    - va_list to void*:          v_cast()    // va_list* <=> void*
*/


/****[[ cast() and raw_cast(): MAIN CAST ]]***********************************
**
** 1. As with all needful macros, we don't force short names on clients.
**    You may have a `cast()` function or variable in your codebase, and if
**    that's more important than having the macro be named cast() you can
**    define it some other way.  But a short name like cast() or coerce()
**    is certainly recommended to get the maximum benefit.
**
** 2. You don't always want to run validation hooks when casting that make
**    sure the data is valid for the target type.  For example, if you are
**    casting a fresh malloc(), the data won't be initialized yet.  It may
**    also be that performance critical code wants to avoid the overhead
**    of validation--even in debug builds.
**
** 3. By default the casts are "lenient" in terms of constness, in the sense
**    that if you try to cast a const pointer to a non-const pointer, it
**    won't error...but will pass through a const version of the target type.
**    This makes casts briefer, e.g. you don't have to be redundant:
**
**       void Some_Func(const Base* base) {
**           const Derived* derived = cast(const Derived*, base);
**              // why not just Derived*? --^
**       }
**
**    If you just do `cast(Derived*, base)` the C build would just do a cast
**    to the mutable Derived*, but you'd get the const correctness in the C++
**    build with less typing.  In any case, the "rigid" casts don't do this
**    passthru, so you get an error omitting const in such cases.
*/

#define needful_lenient_hookable_cast(T,expr)       ((T)(expr))  /* [1] */

#define needful_lenient_unhookable_cast(T,expr)     ((T)(expr))  /* [1] */

#define needful_cast /* (T,expr) */  needful_lenient_hookable_cast

#define needful_raw_cast /* (T,expr) [2] */    needful_lenient_unhookable_cast

#if defined(NEEDFUL_FAST_CAST_IS_SLOW)
    #define needful_fast_cast /* (T,expr) */   needful_lenient_hookable_cast
#else
    #define needful_fast_cast /* (T,expr) */   needful_lenient_unhookable_cast
#endif

#define needful_rigid_hookable_cast(T,expr)         ((T)(expr))  /* [3] */

#define needful_rigid_unhookable_cast(T,expr)       ((T)(expr))


/****[[ m_cast(): MUTABILITY CAST ]]******************************************
**
** The mutable cast removes constness from a type.  It's another case where
** a C++ compiler will not allow you to do what C allows with a parentheses
** cast, so this macro has to be conditional on __cplusplus.
**
** m_cast() is friendlier than C++'s const_cast<> in that you can use it to
** cast away constness along with casting to any needful_upcast()-able type.
** So you can do things like:
**
**     const Derived* d = ...;
**     Base* b = m_cast(Base*, d);
**
** The enhanced version in C++ builds enforces the Base/Derived relationship,
** while this crude version just matches the C semaantics and does not.
**
** If using NEEDFUL_CPP_ENHANCED, the mutable cast can work on "wrapped"
** types as well, removing constness through the wrapper.  See the enhanced
** feature NEEDFUL_DECLARE_WRAPPED_FIELD() for more details.
*/

#if !defined(__cplusplus)
    #define needful_mutable_cast(T,expr) \
        ((T)(expr))  // C allows const to be cast away via parentheses
#else
    #define needful_mutable_cast(T,expr) \
        const_cast<T>((const T)(expr))
#endif


/****[[ i_cast(), p_cast(), f_cast(): NARROWED CASTS ]]***********************
**
** Narrow casts for cross-domain conversions (int<->enum, pointer<->integer,
** function pointer reinterpretation).  Calling these out separately makes
** them visible in source; C++ builds can give them stricter policies.
*/

#define needful_pointer_cast(T,expr)    ((T)(expr))

#define needful_integer_cast(T,expr)   ((T)(expr))

#define needful_function_cast(T,expr)   ((T)(expr))


/****[[ v_cast(): VA_LIST CAST ]]*********************************************
**
** `va_list*` is compiler-specific enough that regular casts aren't safe.
** C++ builds enforce only `va_list* <-> void*` is permitted.
** Opt out of the <stdarg.h> include with NEEDFUL_DONT_INCLUDE_STDARG_H.
*/

#define needful_valist_cast(T,expr)     ((T)(expr))


/****[[ downcast: "INHERITANCE" CASTING ]]************************************
**
** Casts downward in a C-style type hierarchy (base -> derived).  C++
** enhanced builds enforce the relationship; plain C and unenhanced C++
** treat it as void*.  Requires nocast to compile in unenhanced C++ builds.
*/

#if !defined(__cplusplus)
    #define needful_hookable_downcast  (void*)
    #define needful_unhookable_downcast  (void*)
#else
    #define needful_hookable_downcast  needful_nocast
    #define needful_unhookable_downcast  needful_nocast
#endif

#define needful_downcast /* (T,expr) */  needful_hookable_downcast

#define needful_raw_downcast /* (T,expr) */    needful_unhookable_downcast

#if defined(NEEDFUL_FAST_CAST_IS_SLOW)
    #define needful_fast_downcast /* (T,expr) */   needful_hookable_downcast
#else
    #define needful_fast_downcast /* (T,expr) */   needful_unhookable_downcast
#endif



/****[[ c_cast(): "WHAT PARENTHESES-CAST WOULD DO" ]]*************************
**
** The parentheses-cast is the only cast in C, so it is maximally permissive.
** In C++, it defaults to giving warnings if you cast away constness...but
** any standards-compliant compiler must let you disable that warning.
**
** If you are in a situation where the Needful casts are not working for you,
** the c_cast() is a way to fall back on the C cast while still being
** more visible as a cast in a code than parentheses.
*/

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


/****[[ upcast: CAST SAFELY UPWARD ]]*****************************************
**
** upcast() is useful when defining macros, where you expect what you're
** doing to be safe.  The default definition doesn't enforce that it's an
** upcast, but the C++ enhancement will do that enforcement.
*/

#define needful_upcast /* (T,expr) */  needful_c_cast



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


/****[[ MARK UNUSED VARIABLES ]]**********************************************
**
** Used in coordination with the `-Wunused-variable` setting of the compiler.
** While a simple cast to void is what people usually use for this purpose,
** there's some potential for side-effects with volatiles:
**
**   https://stackoverflow.com/a/4030983/211160
**
** The tricks suggested there for avoiding it seem to still trigger warnings
** as compilers get new ones, so assume that won't be an issue.  As an
** added check, this gives the UNUSED() macro "teeth" in C++11:
**
**   https://codereview.stackexchange.com/q/159439
**
** 1. Putting the (void)0 at the head of PASSTHRU may seem superfluous, but
**    if you just define a macro as the plain parenthesization of its args,
**    Clang will complain about unused results if you pass the product of
**    a cast.  If you want the warning, just use parentheses, not PASSTHRU.
*/

#define NEEDFUL_USED(...)    ((void)(__VA_ARGS__))

#define NEEDFUL_UNUSED(...)  ((void)(__VA_ARGS__))

#define NEEDFUL_PASSTHRU(...)  ((void)0, __VA_ARGS__)  /* see [1] */


/****[[ STATIC_ASSERT, STATIC_IGNORE, STATIC_FAIL ]]**************************
**
** 1. C11 added _Static_assert(), so use that when available in C builds.
**    Pre-C11 C has too many edge cases for a portable shim, so it remains
**    a no-op there.  The C++ enhanced build enforces STATIC_ASSERT in all
**    standards modes.
*/

#define NEEDFUL_STATIC_IGNORE(expr) /* uses trick for callsite semicolons */ \
    struct GlobalScopeNoopTrick  /* https://stackoverflow.com/q/53923706 */

#if !defined(__cplusplus) \
    && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define NEEDFUL_STATIC_ASSERT(cond) \
        _Static_assert((cond), "NEEDFUL_STATIC_ASSERT failed")
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
** sure your expression is well-formed at compile-time.  It still does nothing
** at run-time, but causes a compile-time error if the variable is renamed and
** not updated, etc.  `impossible()` is a similar trick, but for documenting
** things that are invariants, but not worth paying for a runtime assert.
**
** `unnecessary()` and `dont()` aren't boolean-constrained, and help document
** lines of code that are not needed (or would actively break things), while
** ensuring the expressions are up-to-date and valid.  `cant()` is for things
** you might like to do, but some current limitation prevents it.
**
** `heeded()` marks things that look stray or like they have no effect, but
** their side-effect is intentional (perhaps only in debug builds, that check
** for what they did).
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
** If you use these words for things like variable names, you will have
** problems if they are defined as macros.  You can pick your own terms but
** these are the ones that were used in Needful's original client.
**
** These are SIMPLE ALIASES, and the parameterization is given as a comment
** for documentation purposes.  There can be big breakages when an expansion
** might produce commas inside angle brackets in C++ builds, and variadic
** forwarding doesn't always work around that.  It's cleaner and safer (and
** faster at compile time) to do it this way.
**
** 1. needful_integer_cast() has been honed to slow down build times about as
**    much as possible, while having zero runtime cost even in unoptimized
**    builds (an important property one seeks for something as fundamental as
**    an integer cast).  But it's still slow--enough so that replacing all
**    integer casts with i_cast() in one codebase made it take 2x as long to
**    compile.  So day-to-day dev builds should probably leave i_cast() as
**    a macro for the plain c_cast().  However if you're using wrapper
**    classes the needful_integer_cast() can actually be zero cost where a
**    C cast would not be; so judicious use of ii_cast() on hot paths in
**    debug builds extracting integers from wrappers can be a good idea.
**
** 2. A quick and dirty way to write `return failed;` and not have to come
**    up with an error might be useful in some codebases.  We don't try to
**    define that here, because it's open ended as to what you'd use for
**    your error value type.
**
** 3. The lenient form of known_cast() is quite useful for writing polymorphic
**    macros which are const-in => const-out and mutable-in => mutable-out.
**    This tends to be more useful than wanting to enforce that only mutable
**    pointers can be passed into a macro (the bulk of macros are reading
**    operations, anyway).  So lenient defaults the short name `known_cast()`.
*/

#if !defined(NEEDFUL_DONT_DEFINE_OPTION_SHORTHANDS)
    #define Need /* (T) */          NeedfulNeed
    #define unwrap /* ... */        needful_unwrap
    #define needed /* ... */        needful_needed

    #define None                    NeedfulNone
    #define none                    needful_none

    #define Option /* (T) */        NeedfulOption
    #define opt /* ... */           needful_opt
#endif

#if !defined(NEEDFUL_DONT_DEFINE_CAST_SHORTHANDS)
    #define cast /* (T,...) */      needful_cast
    #define raw_cast /* (T,...) */  needful_raw_cast
    #define fast_cast /* (T,...) */  needful_fast_cast

    #define c_cast /* (T,...) */    needful_c_cast

    #define m_cast /* (T,...) */    needful_mutable_cast

  #if defined(NEEDFUL_ICAST_SLOW_BUILD)  /* default off for fast builds [1] */
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

#if !defined(NEEDFUL_DONT_DEFINE_RESULT_SHORTHANDS)
    #define Result /* (T) */        NeedfulResult

    #define fail /* (...) */        needful_fail
    /* #define failed               needful_fail("generic failure");  [2] */
    #define panic /* (...) */       needful_panic

    #define trap /* (expr) */               needful_trap
    #define require /* (expr) */            needful_require
    #define except /* (decl) { code } */    needful_except

    #define assume /* (expr) */             needful_assume

    #define rescue /* (expr) */             needful_rescue
#endif

#if !defined(NEEDFUL_DONT_DEFINE_SINK_SHORTHANDS)
    #define Sink /* (T) */          NeedfulSink
    #define Init /* (T) */          NeedfulInit
    #define Contra /* (T) */        NeedfulContra
    #define Exact /* (T) */         NeedfulExact
#endif

#if !defined(NEEDFUL_DONT_DEFINE_KNOWN_SHORTHANDS)
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

#if !defined(NEEDFUL_DONT_DEFINE_COMMENT_SHORTHANDS)
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

#if !defined(NEEDFUL_DONT_DEFINE_STATIC_ASSERT_SHORTHANDS)
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

#if !defined(NEEDFUL_DONT_DEFINE_USAGE_SHORTHANDS)
  #if !defined(USED)
    #define USED /* (...) */  NEEDFUL_USED
  #endif

  #if !defined(UNUSED)
    #define UNUSED /* (...) */  NEEDFUL_UNUSED
  #endif

  #if !defined(NOOP)
    #define NOOP  NEEDFUL_NOOP
  #endif

  #if !defined(PASSTHRU)
    #define PASSTHRU  NEEDFUL_PASSTHRU
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
** needful.h is written out with all the "noop" definitions first, to help
** give a clear sense to people how trivial and non-invasive the library can
** be for C programs--introducing no dependencies or complexity.  There's
** non-zero value to the documentation that these definitions provide, even
** with minimal behavior (and the Result(T) handling still has runtime
** benefits).
**
** But the *REAL* power of Needful comes from C++ builds, performing powerful
** compile-time checks (and optional runtime validations).
**
** When these headers are activated, they will #undef the simple definitions
** given above, and redefine them with actual machinery to give them teeth!
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


#endif  /* !defined(NEEDFUL_H_INCLUDED) */
