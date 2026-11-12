/*
**  file: %needful.h
**  summary: "Needful: Stricter C checks without changing your C"
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
** The key trick: every Needful construct compiles as a transparent macro in C.
** But add companion needful-enhanced/ files, `#define NEEDFUL_CPP_ENHANCED 1`,
** and rebuild as C++.  The same macros light up with compile-time enforcement
** that catches real bugs.  For example:
**
**     Option(Foo*) Find_Foo(int id);      // in C, Option(Foo*) IS just `Foo*`
**
**     Foo* foo = Find_Foo(id);            // enhanced C++: compile error
**
**     Option(Foo*) foo = Find_Foo(id);    // ok
**     if (foo)
**         Use_Foo(unwrap foo);            // ok, and null-checked
**
** (Those short names are opt-in, see step 2 below.  Out of the box, they're
** `NeedfulOption(Foo*)` and `needful_unwrap`, so nothing collides.)
**
** Your C code stays C.  A C++ compiler can just *check* it harder.
**
****[[ WHAT YOU GET ]]*********************************************************
**
**   Need(T)       Marks a value *required* (non-null/non-zero), so `if (ptr)`
**                 on one is a compile error: checking a guarantee is a bug.
**
**   Option(T)     Rust-like optional using T's falsey state as the sentinel--
**                 same size as T, no extra bool!  Can't pass as a plain T:
**                 extract with `unwrap` (null-checked) or `opt` (unchecked).
**
**   Fallible(T)   An Option(T) you can't drop on the floor.  Discharge it
**                 with `return_if_none`, `abort_if_none`, etc.: say which.
**
**   Result(T)     Error + return value, like Rust's Result<T,E>, in plain C.
**                 `return_if_failed` propagates; `catch_if_failed` scopes a
**                 catch variable (and works with `else`).
**
**   Sink(T)       Output parameters, type-checked *backwards* from inputs.
**   Init(T)       Sink(T) scrambles the slot in debug builds, so reading
**   Contra(T)     before writing crashes; Init(T) promises to fill it all;
**                 Contra(T) never scrambles (for in/out parameters).
**
**   cast()        Visible, hookable casts (cast, raw_cast, m_cast, i_cast...)
**                 replacing C's invisible parenthesized ones.
**
**   nocast        Restores C's implicit conversions (void* from malloc(), int
**                 to enum) in C++, so "just compile my C as C++" survives
**                 contact with an actual codebase.
**
**   Comments      `possibly()`, `dont()`, `unnecessary()`... compile-check
**                 what they wrap, so renaming a variable breaks a stale
**                 comment.  The cheapest thing here to adopt: no type changes.
**
**   known(T,expr) Compile-time type assertion for use *inside macros*.  Zero
**                 cost even in debug builds--no function template overhead.
**
** Plus the usual portability shims a C project ends up writing anyway:
** `unreachable`, `dead_end`, STATIC_ASSERT(), USED()/UNUSED(), NODISCARD,
** and ALWAYS_INLINE.  They live in PART 2, below the vocabulary.
**
****[[ GETTING STARTED ]]*****************************************************
**
**   1. Drop `needful.h` into your project.  #include it.  Done.
**      Default mode is single-header: no companion files or build changes.
**
**   2. For `xxx` aliases of the `needful_xxx` names, define
**      NEEDFUL_DEFINE_ALL_SHORTHANDS (or per-group NEEDFUL_*_SHORTHANDS)
**      before including `needful.h`.
**
**   3. For the extra C++ checks, put needful-enhanced/ next to needful.h (it
**      can stay out of your repository, e.g. a local clone):
**
**      https://github.com/metaeducation/needful-enhanced
**
**      Then `#define NEEDFUL_CPP_ENHANCED 1` and build as C++11+.  Same
**      source, stricter checking.  Use `-Werror` / `/WX` there: must-use
**      diagnostics (Fallible/Result) are otherwise only warnings.
**
**   4. CI can run both modes: C for production, C++ for stronger checks.
**      But don't link enhanced and plain objects together: the wrappers
**      change the ABI (e.g. how MSVC returns them).  /setup#abi
**
** The C definitions below are written out in full so you can see how simple
** they are.  (`#ifdef __cplusplus` is used only where *unavoidable*; C++
** variations are done by `#undef` and re-`#define` from the companion tree.)
** Adding Needful to a C project is one file, no dependencies, no magic.
**
****[[ BEFORE YOU INCLUDE ]]**************************************************
**
** [A] Define `NEEDFUL_ASSERT(expr)` and/or `NEEDFUL_ABORT()` to reroute them.
**     Otherwise they default to `assert()` and `abort()`--and only then are
**     <assert.h> and <stdlib.h> included.
*/

#ifndef NEEDFUL_H_INCLUDED  /* "include guard" allows multiple #includes */
#define NEEDFUL_H_INCLUDED

#if !defined(NEEDFUL_ASSERT)  /* See [A] above */
    #include <assert.h>
    #define NEEDFUL_ASSERT(expr)  assert(expr)
#endif

#if !defined(NEEDFUL_ABORT)  /* See [A] above */
    #include <stdlib.h>
    #define NEEDFUL_ABORT()  abort()
#endif


/*****************************************************************************
**
**  PART 1: THE VOCABULARY (CONSTRUCTS YOU USE IN YOUR OWN CODE)
**
*****************************************************************************/


/***[[ Need(T): REQUIRED NON-NULL/NON-ZERO, BOOL-COERCION BLOCKED ]]*********
**
** Docs: https://needful.metaeducation.com/need
**
** Marks a pointer or value as guaranteed non-null/non-zero.  In enhanced C++
** builds, testing a Need(T) in a boolean context is a compile error.  It
** converts to T implicitly; `needed` extracts explicitly, and errors on an
** Option(T) (useful in macros that must reject optional types).
*/

#define NeedfulNeed(T)  T
#define needful_needed  /* no-op in C build; Need(T) only, errors on Option */


/***[[ Option(T): EXPLICITLY DISENGAGE-ABLE TYPE ]]***************************
**
** Docs: https://needful.metaeducation.com/option
**
** A lightweight maybe-value in the vein of `std::optional` or Rust's `Option`
** using T's falsey state as disengaged.  In enhanced C++ builds, Option(T)
** does not silently coerce to T; use `unwrap` (checked) or `opt` (unchecked).
*/

typedef enum {
    NEEDFUL_NONE_ENUM_0 = 0
} NeedfulNoneEnum;

#define NeedfulNone  NeedfulNoneEnum  /* unique type if C++ enhanced */
#define needful_none  needful_nocast_0  /* unique type if C++ enhanced */

#define NeedfulOption(T)  T

#define needful_unwrap  /* no-op in C build; Option(T) only, errors on Need */
#define needful_opt  /* no-op in C build */

#define needful_postfix_extract_option  /* no-op in C build */


/****[[ Fallible(T): LIKE Option(T) BUT RESULT MUST BE USED ]]****************
**
** Docs: https://needful.metaeducation.com/fallible
**
** An Option(T) whose absence means *failure*, so it must be handled (like
** Rust's #[must_use]).  Discharge it with the macros below, or assert it:
**
**     char* buf = infallible Try_Allocate(size);  // asserts in debug
**
** Option(T) converts implicitly to Fallible(T); `fallible` converts back.
** FallibleVar(T) is the same type for non-return positions (the must-use
** annotation is only legal on return types).  Enforcement varies by
** compiler: /fallible#enforcement
*/

#if !defined(NEEDFUL_PRECPP17_NODISCARD)
    #define NEEDFUL_PRECPP17_NODISCARD  0  /* [[nodiscard]] below C++17 */
#endif

/* The only diagnostic Needful ever touches: opt-in, C++-only, and unpopped
** as the attribute is emitted at callsites (pragmas can't ride in macros).
*/
#if NEEDFUL_PRECPP17_NODISCARD && defined(__cplusplus) \
        && __cplusplus < 201703L && !defined(_MSC_VER)  /* MSVC is quiet */
  #if defined(__clang__)
    #pragma clang diagnostic ignored "-Wc++17-extensions"
  #elif defined(__GNUC__)
    #pragma GCC diagnostic ignored "-Wc++17-extensions"
  #endif
#endif

#if defined(__GNUC__) || defined(__clang__)
    #define NEEDFUL_MUSTUSE  __attribute__((warn_unused_result))
#elif defined(__cplusplus) && __cplusplus >= 201703L
    #define NEEDFUL_MUSTUSE  [[nodiscard]]  /* `static` is fine in C++ */
#elif defined(_MSC_VER) && defined(_MSVC_LANG) && _MSVC_LANG >= 201703L
    #define NEEDFUL_MUSTUSE  [[nodiscard]]  /* MSVC w/o /Zc:__cplusplus */
#elif NEEDFUL_PRECPP17_NODISCARD && defined(__cplusplus)
    #define NEEDFUL_MUSTUSE  [[nodiscard]]  /* opt-in, C++11/14 */
#elif defined(NEEDFUL_FALLIBLE_C23_MUSTUSE) && NEEDFUL_FALLIBLE_C23_MUSTUSE
    #define NEEDFUL_MUSTUSE  [[nodiscard]]  /* opt-in: breaks `static` */
#else
    #define NEEDFUL_MUSTUSE  /* no spelling available in this configuration */
#endif

#define NeedfulFallible(T)     NEEDFUL_MUSTUSE T  /* return position only */
#define NeedfulFallibleVar(T)  T  /* locals, parameters, members, typedefs */

#define needful_infallible  /* no-op in C build */
#define needful_fallible  /* no-op in C build; Fallible(T) => Option(T) */


/****[[ NONE-REACTIVE MACROS: THE Fallible(T) DISCHARGE VOCABULARY ]]*********
**
** Docs: https://needful.metaeducation.com/fallible#discharge-vocabulary
**
** Handle an optional value's `none` state. These also discharge Fallible(T)'s
** must-use obligation and parallel the Result(T) vocabulary:
**
**     propagate it        return_if_failed     return_if_none
**     die on it           panic_if_failed      abort_if_none
**     assert impossible   assert_not_failed    assert_not_none
**     ignore it           extract_failure      tolerate_none
**
** The pairs are not interchangeable: one tests the error state, the other
** the value, so the wrong one silently does nothing.
** (Why they can't merge:  /faq#result-fallible-unification)
*/

#define needful_is_none(_expr_) /* doubled parens: callers pass assignments */ \
    (((_expr_ needful_postfix_extract_option)) == needful_none)

#define needful_return_if_none(_expr_) \
    do { if (needful_is_none(_expr_)) { return needful_none; } } while (0)

#define needful_abort_if_none(_expr_) \
    do { if (needful_is_none(_expr_)) { NEEDFUL_ABORT(); } } while (0)

/* NDEBUG compiles asserts away, so _expr_ is evaluated into a local instead of
** sitting inside NEEDFUL_ASSERT()--callers write assignments in it.
*/
#define needful_assert_not_none(_expr_) do { \
    int _needful_was_none_ = needful_is_none(_expr_) ? 1 : 0; \
    NEEDFUL_ASSERT(! _needful_was_none_); \
    NEEDFUL_UNUSED(_needful_was_none_); \
} while (0)

#define needful_tolerate_none(_expr_) \
    NEEDFUL_USED(needful_is_none(_expr_))


/****[[ Result(T): MULTIPLEXED ERROR AND RETURN RESULT ]]*********************
**
** Docs: https://needful.metaeducation.com/result
**
** Error propagation like Rust's `Result<T,E>`, in plain C (no exceptions or
** setjmp/longjmp).  return_if_failed() propagates; catch_if_failed() scopes
** a catch variable and supports `else`.
**
** Your project supplies the failure storage, as four hooks:
**
**     Needful_Get_Failure()             is a failure pending?
**     Needful_Set_Failure(error)        record one
**     Needful_Test_And_Clear_Failure()  take it, leaving none pending
**     Needful_Panic_Abruptly(error)     report it and do not come back
**
** Or use `needful-result-hooks-{singlethreaded,threadlocal}.h/.c` from
** https://github.com/metaeducation/needful.
*/

#define NeedfulResult(T)  T

#define NEEDFUL_RESULT_0  needful_nocast_0  /* unique type if C++ enhanced */

#define Needful_Assert_Not_Failing() /* derived, so honors NEEDFUL_ASSERT */ \
    NEEDFUL_ASSERT(! Needful_Get_Failure())

/* Failing is a STATEMENT; there's deliberately no expression form (see
** /result#no-make-failure before writing one).
*/
#define needful_return_fail(...) do { \
    Needful_Assert_Not_Failing(); \
    Needful_Set_Failure(__VA_ARGS__); \
    return NEEDFUL_RESULT_0; \
} while (0)

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

#define needful_panic_if_failed(_stmt_) /* reports the error, not abort() */ \
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
    for (_decl_ = Needful_Get_Failure(), *_once = NEEDFUL_NULLPTR; \
            !_once; ++_once) \
      if (Needful_Test_And_Clear_Failure()) /* allow else clause to attach */
        /* {body} implicitly picked up after macro by for, decl is scoped */

#define needful_extract_failure(_expr_) \
    (Needful_Assert_Not_Failing(), _expr_ needful_postfix_extract_result, \
        Needful_Test_And_Clear_Failure())


/****[[ Sink(T) / Init(T) / Contra(T): FUNCTION OUTPUT PARAMETERS ]]*********
**
** Docs: https://needful.metaeducation.com/contra
**
** Pointer output parameters.  Enhanced builds check the type rule *backwards*:
** a function writing Base bits may be handed a Derived slot, not vice versa.
** They differ in what happens to the slot's prior contents:
**
**     Sink(T)     scrambled in debug builds (with corruptions enabled)
**     Init(T)     not scrambled; promises to fill every field itself
**     Contra(T)   never scrambled, under any setting (for in/out params)
**
** Exact(T) is the escape hatch: precisely T, with no variance either way.
*/

#define NeedfulSink(T)  T *
#define NeedfulInit(T)  T *

#define NeedfulContra(T)  T *
#define NeedfulExact(T)  T  /* precise type */


/****[[ nocast: IMPLICIT COERCION FOR C/C++ COMPATIBILITY ]]*****************
**
** Docs: https://needful.metaeducation.com/nocast
**
** Restores C's implicit void* and integer-to-enum conversions in C++:
**
**     SomeType* ptr = nocast malloc(sizeof(SomeType));  // works in C++...
**     SomeEnum  e   = nocast some_int_value;            // ...as does this!
**
** Empty in C; in C++, a proxy restores those conversions. Details:
** /nocast and /precedence.
**/

#if !defined(__cplusplus)  /* need C++ variant even w/o NEEDFUL_CPP_ENHANCED */
    #define needful_nocast
#else
    #include <type_traits>

  namespace needful {
    struct NocastMaker {};

    template<
        class To, class From,  /* `From` pass-by-value (no remove_reference) */
        bool IntToPtr =  /* only one case needs special handling */
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

  #if defined(_MSC_VER)  /* aggregate, so no ctor: /setup#c4623 */
    #pragma warning(push)
    #pragma warning(disable: 4623)  /* "default ctor implicitly deleted" */
  #endif
    template<class From>
    struct NocastHolder {
        From f;

        template<class To>
        operator To() const {
            return NocastConvert<To, From>::Do_Conversion(f);
        }
    };
  #if defined(_MSC_VER)
    #pragma warning(pop)
  #endif

    template<class T>  /* `*` is lower precedence than Result(T) `->*` */
    inline NocastHolder<T> operator*(NocastMaker, T v) { return { v }; }

    constexpr NocastMaker g_nocast_maker{};
  }

    #define needful_nocast  needful::g_nocast_maker *
#endif


/***[[ nocast_0, struct_0, array_0: POLYMORPHIC ZERO-INITIALIZERS ]]**********
**
** Docs: https://needful.metaeducation.com/nocast#needful_nocast_0
**
** `nocast_0` means `nocast 0` but compiles faster.  Option(T)/Result(T)
** macros need it in C++ without the enhanced files, where its comparison
** operators keep `x == none` working.
**/

#if !defined(__cplusplus)
    #define needful_nocast_0    0    /* a genuine null pointer constant */
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

    template<class T>  /* see comparison note above */
    constexpr auto operator==(const T& v, Nocast0Struct) -> decltype(! v)
      { return ! v; }

    template<class T>
    constexpr auto operator==(Nocast0Struct, const T& v) -> decltype(! v)
      { return ! v; }

    template<class T>
    constexpr auto operator!=(const T& v, Nocast0Struct) -> decltype(!! v)
      { return !! v; }

    template<class T>
    constexpr auto operator!=(Nocast0Struct, const T& v) -> decltype(!! v)
      { return !! v; }
  }

    #define needful_nocast_0  needful::Nocast0Struct{}
    #define needful_struct_0  needful::Struct0Struct{}
    #define needful_array_0   {}  /* can't promise usage only w/arrays :-( */

    #define needful_struct_default  needful::StructDefaultStruct{}
#endif


/****[[ VISIBLE (AND HOOKABLE!) ERGONOMIC CASTS ]]****************************
**
** Docs: https://needful.metaeducation.com/cast
**
** Casts that are easy to spot and label their intent.  In C they're all a
** parenthesized cast; enhanced builds enforce narrower policies, and casts
** become hookable for compile-time and runtime validation.  Zero-overhead in
** release builds.
**
*****[[ CAST SELECTION GUIDE ]]***********************************************
**
** SAFETY LEVEL
**    - Hookable cast:            cast()       // safe default w/debug hooks
**    - ...in generic macros:     generic_cast()  // cast() + allows upcasts
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
#define needful_generic_cast(T,expr)          ((T)(expr))
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


/****[[ known(T,expr): COMPILE-TIME TYPE ASSERTION INSIDE MACROS ]]**********
**
** Docs: https://needful.metaeducation.com/known
**
** Type-checks an expression against T at compile time, with no function
** template--so not even a call in unoptimized debug builds.  As with casts,
** lenient forms pass a const T* through; rigid ones enforce mutability.
** (`c_cast_known` exists because `cast(T, known(T, expr))` breaks if T
** contains commas.)
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

#define needful_rigid_c_cast_known(T,expr) \
    needful_c_cast(T,expr)

#define needful_lenient_c_cast_known(T,expr) /* const passthru const */ \
    needful_c_cast(T,expr)


/****[[ COMMENTS WITH TEETH ]]************************************************
**
** Docs: https://needful.metaeducation.com/comments
**
** Prose comments go stale silently when a variable is renamed.  These wrap
** the expression instead, so enhanced builds check it still compiles (C
** builds discard it).  Uppercase versions work at global scope.
**
**     int i = Get_Integer(...);
**     possibly(i < 0);   // rename `i` and this fails the build
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


/****[[ CORRUPTION: MAKE READ-BEFORE-WRITE CRASH INSTEAD OF LURK ]]**********
**
** Docs: https://needful.metaeducation.com/contra
**
** Scrambling dead variables turns "reads a stale value, misbehaves later"
** into "crashes here."  Enhanced builds do it to Sink(T) slots; these helpers
** do it by hand.  Off by default (costs cycles); keep it off under static
** analysis, where memset hides uninitialized-use bugs.
*/

#if !defined(NEEDFUL_DOES_CORRUPTIONS)
   #define NEEDFUL_DOES_CORRUPTIONS  0
#endif

#if !NEEDFUL_DOES_CORRUPTIONS
    #define Corrupt_If_Needful(var)  NEEDFUL_NOOP
    #define Assert_Corrupted_If_Needful(ptr)  NEEDFUL_NOOP
#else
    #include <string.h>  /* for memset */

    #define Corrupt_If_Needful(var) \
        memset(&(var), 0xBD, sizeof(var))  /* C99 fallback mechanism */

    #define Assert_Corrupted_If_Needful(var) do { \
        if (*(unsigned char*)(&(var)) != 0xBD)  /* cheap check vs. loop */ \
            NEEDFUL_ASSERT(!"Expected variable to be corrupt and it was not"); \
    } while (0)
#endif

#define NEEDFUL_USES_CORRUPT_HELPER  0


/*****************************************************************************
**
**  PART 2: THE PLUMBING (PORTABILITY SHIMS AND UTILITIES USED BY PART 1)
**
*****************************************************************************/


/****[[ NEEDFUL_NULLPTR AND OPTIONAL nullptr SHIM ]]**************************
**
** Pre-C23 C may define NULL as integer 0, which is unsafe in variadic pointer
** calls and rejected by Option(T*). Needful supplies a `nullptr` shim;
** internal macros use NEEDFUL_NULLPTR. /setup#nullptr
*/

#if !defined(__cplusplus) && defined(__STDC_VERSION__) \
    && __STDC_VERSION__ >= 202311L \
    && (!defined(_MSC_VER) || defined(__clang__))  /* MSVC lacks it so far */
    #define NEEDFUL_C23_NULLPTR  1  /* a real keyword (not a macro) */
#else
    #define NEEDFUL_C23_NULLPTR  0
#endif

#if defined(__cplusplus) || NEEDFUL_C23_NULLPTR
    #define NEEDFUL_NULLPTR  nullptr
#else
    #define NEEDFUL_NULLPTR  ((void*)0)
#endif

#if !defined(NEEDFUL_NO_NULLPTR_SHIM)
    #define NEEDFUL_NO_NULLPTR_SHIM  0
#endif
#if !NEEDFUL_NO_NULLPTR_SHIM
  #if defined(__cplusplus)
    #include <cstddef>  /* defines `using nullptr_t = decltype(nullptr);` */
  #elif !NEEDFUL_C23_NULLPTR && !defined(nullptr)  /* don't shadow keyword */
    #define nullptr  ((void*)0)
  #endif
#endif


/****[[ NOOP, UNIQUE_NAME, USED/UNUSED, SCOPE_GUARD ]]***********************
**
** NEEDFUL_USED() quiets `-Wunused-variable`.  Enhanced builds can make
** NEEDFUL_UNUSED() scramble non-const contents, to catch later use.
** NEEDFUL_SCOPE_GUARD makes statement macros like return_if_failed a compile
** error in an unbraced branch slot, at no runtime cost, in all builds.
** (How: /result#scope-guard)
*/

#define NEEDFUL_NOOP  ((void)0)

#define NEEDFUL_PASTE2(a, b)  a##b
#define NEEDFUL_PASTE1(a, b)  NEEDFUL_PASTE2(a, b)

#define NEEDFUL_UNIQUE_NAME(base)  NEEDFUL_PASTE1(base, __LINE__)

#define NEEDFUL_USED(...)    ((void)(__VA_ARGS__))
#define NEEDFUL_UNUSED(...)  ((void)(__VA_ARGS__))

#define NEEDFUL_SCOPE_GUARD /* typedef: a declaration w/no object or code */ \
    typedef int NEEDFUL_UNIQUE_NAME(_statement_must_be_in_braces_); \
    NEEDFUL_USED(sizeof(NEEDFUL_UNIQUE_NAME(_statement_must_be_in_braces_)))


/****[[ unreachable: INDICATE DIVERGENCE ]]***********************************
**
** Docs: https://needful.metaeducation.com/unreachable
**
** Asserts if reached in debug; in release, tells the compiler the path is
** impossible. Use the return-specific forms for void and struct functions.
** `needful_builtin_unreachable` may be redefined.
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
    NEEDFUL_ASSERT(!"unreachable"); \
    needful_builtin_unreachable; \
    return needful_nocast_0; \
  } while (0)

#define needful_unreachable_void  do { \
    NEEDFUL_ASSERT(!"unreachable"); \
    needful_builtin_unreachable; \
    return; \
  } while (0)

#define needful_unreachable_struct(T)  do { \
    NEEDFUL_ASSERT(!"unreachable"); \
    needful_builtin_unreachable; \
    return (T)needful_struct_0; \
  } while (0)

/* (No unreachable_array: C functions can't return arrays.) */


/****[[ NORETURN shim ]]******************************************************
**
** Mark non-returning functions.  `needful_dead_end;` ends a path without a
** return value, but some C compilers still warn "control reaches end of
** non-void function"; `needful_unreachable_*` forms are fully portable.
*/

 #if !defined(__cplusplus)
  #if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define NEEDFUL_NORETURN  _Noreturn  /* C11 and higher */
  #else
    #define NEEDFUL_NORETURN  /* no-op in C pre-C11 */
  #endif
#else
    #define NEEDFUL_NORETURN  [[noreturn]]  /* C++11 and higher */
#endif

NEEDFUL_NORETURN static inline void needful_dead_end_inline(void) {
    NEEDFUL_ASSERT(!"dead_end");
    needful_builtin_unreachable;
}

#define needful_dead_end  needful_dead_end_inline()


/****[[ STATIC_ASSERT, STATIC_IGNORE, STATIC_FAIL ]]**************************
**
** STATIC_ASSERT uses _Static_assert in C11 and enhanced C++; it is a no-op in
** earlier C. STATIC_FAIL still fails earlier C builds, without a message.
** ASSERT_LVALUE rejects temporaries; DECLTYPE checks support comment macros.
*/

#define NEEDFUL_STATIC_IGNORE(expr) /* https://stackoverflow.com/q/53923706 */ \
    struct NEEDFUL_UNIQUE_NAME(needful_noop_)  /* caller supplies semicolon */

#if !defined(__cplusplus) \
    && defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define NEEDFUL_STATIC_ASSERT(cond) \
        _Static_assert((cond), #cond)
#else
    #define NEEDFUL_STATIC_ASSERT(cond) \
        NEEDFUL_STATIC_IGNORE(cond)  /* pre-C11 C version is a no-op */
#endif

#define NEEDFUL_STATIC_ASSERT_NOT(cond) \
    NEEDFUL_STATIC_ASSERT(! (cond))

#if defined(__cplusplus)
    #define NEEDFUL_STATIC_FAIL(msg)  static_assert(0, msg)
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    #define NEEDFUL_STATIC_FAIL(msg)  _Static_assert(0, msg)
#else
    #define NEEDFUL_STATIC_FAIL(msg) /* message unavailable pre-C11 */ \
        typedef int NEEDFUL_UNIQUE_NAME(needful_static_fail_)[-1]
#endif

#define NEEDFUL_STATIC_ASSERT_LVALUE(variable) \
    NEEDFUL_USED(*&variable)

#define NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL(expr)  NEEDFUL_NOOP

#define NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID(expr) NEEDFUL_NOOP


/****[[ ENABLEABLE: ARGUMENT TYPE SUBSETTING ]]*******************************
**
** Hooks for enhanced builds to SFINAE-constrain which argument types a
** function accepts.  All no-ops in C: a parameter declared with ENABLEABLE()
** is just `T name`, and the ENABLE_IF/DISABLE_IF markers vanish.
*/

#define ENABLE_IF_EXACT_ARG_TYPE(...)
#define DISABLE_IF_EXACT_ARG_TYPE(...)

#define ENABLE_IF_ARG_CONVERTIBLE_TO(...)   /* C++ override in needful-known */
#define DISABLE_IF_ARG_CONVERTIBLE_TO(...)  /* C++ override in needful-known */

#define ENABLEABLE(T, name)  T name


/****[[ NODISCARD and ALWAYS_INLINE shims ]]**********************************
**
** NEEDFUL_NODISCARD becomes `[[nodiscard]]` in enhanced builds, which is what
** flags a Result(T) dropped on the floor.  NEEDFUL_ALWAYS_INLINE inlines even
** in unoptimized builds (so e.g. a no-op cast hook costs nothing in debug).
*/

#define NEEDFUL_NODISCARD  /* default to no-op in C or pre-C++17 */

#if defined(__GNUC__) || defined(__clang__)
    #define NEEDFUL_ALWAYS_INLINE  __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
    #define NEEDFUL_ALWAYS_INLINE  __forceinline
#else
    #define NEEDFUL_ALWAYS_INLINE  inline
#endif


/*****************************************************************************
**
**  PART 3: OPTIONAL ALIASES FOR `NEEDFUL_XXX` MACROS AS JUST `XXX` MACROS
**
*****************************************************************************/

#if !defined(NEEDFUL_DEFINE_ALL_SHORTHANDS)
    #define NEEDFUL_DEFINE_ALL_SHORTHANDS  0
#endif

#if !defined(NEEDFUL_OPTION_SHORTHANDS)
    #define NEEDFUL_OPTION_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_OPTION_SHORTHANDS
    #define Need /* (T) */          NeedfulNeed
    #define needed /* ... */        needful_needed

    #define None                    NeedfulNone
    #define none                    needful_none

    #define Option /* (T) */        NeedfulOption
    #define unwrap /* ... */        needful_unwrap
    #define opt /* ... */           needful_opt
#endif

#if !defined(NEEDFUL_CAST_SHORTHANDS)
    #define NEEDFUL_CAST_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_CAST_SHORTHANDS
    #define cast /* (T,...) */          needful_cast
    #define generic_cast /* (T,...) */  needful_generic_cast
    #define raw_cast /* (T,...) */      needful_raw_cast
    #define fast_cast /* (T,...) */     needful_fast_cast

    #define c_cast /* (T,...) */    needful_c_cast

    #define m_cast /* (T,...) */    needful_mutable_cast

    #if !defined(NEEDFUL_ICAST_SLOW_BUILD)  /* i_cast checking is off by */
        #define NEEDFUL_ICAST_SLOW_BUILD  0  /* default, to keep builds fast */
    #endif
    #if NEEDFUL_ICAST_SLOW_BUILD
      #define i_cast /* (T,...) */    needful_integer_cast
    #else
      #define i_cast /* (T,...) */    needful_c_cast
    #endif
    #define ii_cast /* (T,...) */   needful_integer_cast  /* always checked */

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

    #define return_fail /* (...) */      needful_return_fail
    #define panic /* (...) */            needful_panic

    #define return_if_failed /* (stmt) */    needful_return_if_failed
    #define panic_if_failed /* (stmt) */     needful_panic_if_failed
    #define assert_not_failed /* (stmt) */   needful_assert_not_failed
    #define catch_if_failed /* (decl) {} */  needful_catch_if_failed
    #define extract_failure /* (expr) */     needful_extract_failure
#endif

#if !defined(NEEDFUL_FALLIBLE_SHORTHANDS)
    #define NEEDFUL_FALLIBLE_SHORTHANDS  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_FALLIBLE_SHORTHANDS
    #define Fallible /* (T) */              NeedfulFallible
    #define FallibleVar /* (T) */           NeedfulFallibleVar
    #define infallible /* ... */            needful_infallible
    #define fallible /* ... */              needful_fallible

    #define is_none /* (expr) */            needful_is_none
    #define return_if_none /* (expr) */     needful_return_if_none
    #define abort_if_none /* (expr) */      needful_abort_if_none
    #define assert_not_none /* (expr) */    needful_assert_not_none
    #define tolerate_none /* (expr) */      needful_tolerate_none
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

    /* Unprefixed spellings are the lenient variants (const T* passes thru) */
    #define known /* (T,expr) */                 needful_lenient_known
    #define known_not /* (T,expr) */             needful_lenient_known_not
    /* (known_any is the exception: it is rigid, as there is no lenient one) */
    #define known_any /* ((T,...),expr) */       needful_rigid_known_any

    #define rigid_c_cast_known /* (T,expr) */    needful_rigid_c_cast_known
    #define lenient_c_cast_known /* (T,expr) */  needful_lenient_c_cast_known

    #define c_cast_known /* (T,expr) */          needful_lenient_c_cast_known

    #define known_lvalue /* (var) */             needful_known_lvalue

    #define lenient_exactly /* (T,expr) */       needful_lenient_exactly
    #define rigid_exactly /* (T,expr) */         needful_rigid_exactly
    #define exactly /* (T,expr) */               needful_lenient_exactly

    #define known_literal /* (T,expr) */         needful_known_literal
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
        #define STATIC_FAIL /* ("msg") */  NEEDFUL_STATIC_FAIL
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
**  PART 4: OPTIONALLY HAVE NEEDFUL_CPP_ENHANCED #undef AND re-#define MACROS
**
*****************************************************************************/

#if !defined(NEEDFUL_CPP_ENHANCED)
    #define NEEDFUL_CPP_ENHANCED  0  /* Note: can still be compiled as C++ */
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
    #if NEEDFUL_NEED_USES_WRAPPER
        #error "NEEDFUL_NEED_USES_WRAPPER requires NEEDFUL_CPP_ENHANCED"
    #endif
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
