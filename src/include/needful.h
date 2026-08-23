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
** (Out of the box the names are `NeedfulOption(Foo*)` and `needful_unwrap`,
** so dropping needful.h into code won't collide with things already called
** `Option`.  Short spellings are opt-in: use NEEDFUL_DEFINE_ALL_SHORTHANDS,
** or NEEDFUL_OPTION_SHORTHANDS, per step 2 below.)
**
** Your C code stays C.  A C++ compiler just *checks* it harder.
**
****[[ WHAT YOU GET ]]*********************************************************
**
**   Need(T)       Marks a value as *required* non-null/non-zero.  Blocks
**                 boolean coercion: `if (ptr)` on a Need(T) is a compile
**                 error--null-checking a guarantee signals a logic bug.
**
**   Option(T)     Need(T)'s dual: a value that legitimately may be absent.
**                 Rust-like optional using T's falsey state as the sentinel--
**                 same size as T, no extra bool!  `unwrap` extracts with a
**                 null-check panic; `opt` skips it (unsafe).  C++ enforces
**                 that Option(T) can't silently pass as a plain T.
**
**   Fallible(T)   Like Option(T) but "must use": you can't drop it on the
**                 floor.  Discharge it with `return_if_none`, `abort_if_none`,
**                 `assert_not_none` or `tolerate_none`: say which you meant.
**
**   Result(T)     Multiplexed error + return value, like Rust's Result<T,E>.
**                 `return_if_failed` auto-propagates; `catch_if_failed`
**                 supports scoped catch variables (and works with `else`).
**                 (standard C: it expands to a scoped `for` pattern)
**
**   Sink(T)       Output parameters, with the type rule running *backwards*
**   Init(T)       from an input's.  Sink(T) can also scramble the slot in
**   Contra(T)     debug builds, so reading before writing crashes loudly
**                 instead of picking up a stale value.  Init(T) promises to
**                 fill every field, so it skips that by default.  Contra(T)
**                 never scrambles under any setting--which is what makes it
**                 the one to reach for on an in/out parameter.
**
**   cast()        A family of visible, hookable casts (cast, raw_cast,
**                 m_cast, i_cast, ...) that replace C's invisible
**                 parenthesized casts, with optional debug validation hooks.
**
**   nocast        Restores C's implicit conversions (void* from malloc(),
**                 int to enum) inside a C++ build.  This is the macro that
**                 makes "just compile my C as C++" survive contact with an
**                 actual codebase, instead of drowning you in errors.
**
**   Comments      Annotations like `possibly()`, `dont()`, `unnecessary()`
**                 that compile-check the expression they wrap.  Rename a
**                 variable, and the stale comment mentioning it fails the
**                 build.  Nothing else here has that property--and nothing
**                 else costs so little to adopt, since it asks for no type
**                 changes and no refactoring at all.
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
**   2. If you like, enable shorthand aliases (`xxx`) for `needful_xxx` names.
**      `#define NEEDFUL_DEFINE_ALL_SHORTHANDS 1` for all aliases, or
**      set specific `NEEDFUL_*_SHORTHANDS` flags for selected groups.
**      Define these before including `needful.h`.
**
**   3. If you want the extra C++ checks, put needful-enhanced/ next to
**      needful.h.  That companion tree can stay out of your main repository
**      if you prefer (e.g. local clone + .gitignore):
**
**      https://github.com/metaeducation/needful-enhanced
**
**      Then `#define NEEDFUL_CPP_ENHANCED 1` and build as C++11+.
**      Same source, stricter checking.
**
**   4. CI can run both modes: C for production, C++ for stronger checks.
**
** The C definitions below are written out in full so you can see how simple
** they are.  (`#ifdef __cplusplus` is used only where *unavoidable*; C++
** variations are done by `#undef` and re-`#define` from the companion tree.)
** Adding Needful to a C project is one file, no dependencies, no magic.
**
****[[ BEFORE YOU INCLUDE ]]**************************************************
**
** A. Needful globally disables `-Wint-conversion` in C mode.  This is needed
**    because `make_failure(...)` and `none` expand to comma expressions, and
**    the comma operator strips the "null pointer constant" status of 0,
**    causing GCC/Clang to warn on every `return make_failure(...)` in a
**    pointer-returning function.  The C++ enhanced build catches real
**    type mistakes anyway.
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
**
**    Likewise `abort_if_none()` routes through `NEEDFUL_ABORT()`, which
**    defaults to `<stdlib.h>`'s `abort()`.  Define either macro beforehand and
**    Needful will not include the corresponding standard header at all.
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

#if !defined(NEEDFUL_ABORT)  /* See [B] above */
    #include <stdlib.h>
    #define NEEDFUL_ABORT()  abort()
#endif


/*****************************************************************************
**
**  PART 1: THE VOCABULARY
**
******************************************************************************
**
** These are the constructs you write in your own code.  Each section links to
** its documentation page--the rationale, worked examples, and the details of
** what each compilation mode enforces--all live there rather than here.
**
** The utilities these are built from (unreachable, SCOPE_GUARD, the static
** assert family, the shims) are in PART 2, out of the way.
*/


/***[[ Need(T): REQUIRED NON-NULL/NON-ZERO, BOOL-COERCION BLOCKED ]]*********
**
** Docs: https://needful.metaeducation.com/need
**
** Need(T) marks a pointer or value as guaranteed non-null/non-zero.  The
** core benefit: in NEEDFUL_CPP_ENHANCED builds, testing a Need(T) in a
** boolean context is a compile error.
**
** Two prefix "keywords" get their C definitions here.  `needed` extracts the
** raw value from a Need(T), and errors on an Option(T)--which makes it useful
** for writing macros that must reject optional types.  `unwrap` is the shared
** one: it works on Need(T) *and* on Option(T), where it also null-checks.
** (They are ordinary operators in disguise; see /precedence for when that
** disguise slips and you need parentheses.)
*/

#define NeedfulNeed(T)  T
#define needful_unwrap  /* no-op in C build; legal on Need(T) and Option(T) */
#define needful_needed  /* no-op in C build; Need(T) only, errors on Option */


/***[[ Option(T): EXPLICITLY DISENGAGE-ABLE TYPE ]]***************************
**
** Docs: https://needful.metaeducation.com/option
**
** Option() is a lightweight maybe-value in the vein of Rust `Option` or
** C++ `std::optional`, using T's falsey state as disengaged.
**
** In enhanced C++ builds, Option(T) does not silently coerce to T; use
** `unwrap` (checked) or `opt` (unchecked).
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
** Docs: https://needful.metaeducation.com/fallible
**
** Fallible(T) is a "must-use" Option(T)--like Rust's #[must_use].  Where
** Result(T) multiplexes an error onto a return value through thread-global
** state, Fallible(T) does something simpler: it just says you may not drop
** this value on the floor without saying what you meant by that.
**
** The none-reactive macros in the next section (return_if_none, and friends)
** discharge that obligation as a statement.  `infallible` is the expression
** form, for when you know this particular call cannot come back disengaged:
**
**     char* buf = infallible Try_Allocate(size);  // asserts in debug
**
** Fallible(T) is for RETURN TYPES; FallibleVar(T) is the same type for every
** other position--locals, parameters, struct members, typedefs.  The split
** exists because the must-use annotation is diagnosed anywhere but a return
** type, which is a feature: it means a value that must be checked cannot be
** quietly parked in a struct field where nothing will ever look at it.
**
**     https://needful.metaeducation.com/fallible#enforcement
**
** ...documents which compilers check this, why the GNU spelling is preferred
** over [[nodiscard]] (it may appear anywhere among the declaration specifiers,
** which is what makes `static Fallible(int*) helper(void)` legal), and what
** the two opt-in switches below buy you.
*/

#if !defined(NEEDFUL_PRECPP17_NODISCARD)
    #define NEEDFUL_PRECPP17_NODISCARD  0  /* [[nodiscard]] below C++17 */
#endif

/* This suppression is file-scope and unpopped, like the one in note [A].  It
** has to be: the attribute is emitted at each callsite rather than here, and
** a pragma cannot ride inside the macro, since pragmas may not appear in the
** middle of a declaration.
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


/****[[ NONE-REACTIVE MACROS: THE Fallible(T) DISCHARGE VOCABULARY ]]*********
**
** Docs: https://needful.metaeducation.com/fallible#discharge-vocabulary
**
** These macros react to an expression coming back in its disengaged state.
** They are how you satisfy a Fallible(T)'s must-use obligation, and they work
** on a plain Option(T) as well.
**
** They mirror the Result(T) vocabulary one-for-one:
**
**     propagate it        return_if_failed     return_if_none
**     die on it           panic_if_failed      abort_if_none
**     assert impossible   assert_not_failed    assert_not_none
**     ignore it           extract_failure      tolerate_none
**
** What they do NOT do is interoperate.  return_if_failed tests the global
** failure state, while return_if_none tests the value itself...so reaching
** for the wrong one silently does nothing.  The two families cannot be
** merged into one; the reasons are worth reading before proposing it:
**
**   https://needful.metaeducation.com/faq#result-fallible-unification
**
** The test compares against `none`, not against a null pointer.  That is not
** a stylistic choice; it is what keeps Option(SomeEnum) working:
**
**   https://needful.metaeducation.com/design#worked-example
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
** These macros provide a C/C++-compatible mechanism for propagating and
** handling errors in a style similar to Rust's `Result<T, E>`, without
** requiring exceptions or setjmp/longjmp in C++ builds.
**
** Catch syntax remains plain C: catch_if_failed() uses a scoped `for` pattern
** so catch variables can be introduced in a local scope and still pair with
** an `else` clause naturally.  Full usage examples are in the docs.
**
** Your project supplies the failure storage, as four hooks:
**
**     Needful_Get_Failure()             is a failure pending?
**     Needful_Set_Failure(error)        record one
**     Needful_Test_And_Clear_Failure()  take it, leaving none pending
**     Needful_Panic_Abruptly(error)     report it and do not come back
**
** Or define NEEDFUL_DECLARE_RESULT_HOOKS for a single-threaded default set.
*/

#define NeedfulResult(T)  T

#define NEEDFUL_RESULT_0  needful_nocast_0  /* unique type if C++ enhanced */

/* Derived from the hooks above rather than being a fifth one, so that it
** honors a project's NEEDFUL_ASSERT() like every other Needful check does.
** (`#undef` and redefine if you want the diagnostic to name the pending
** error, which this spelling cannot do.)
*/
#define Needful_Assert_Not_Failing() \
    NEEDFUL_ASSERT(! Needful_Get_Failure())

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
** These mark pointer outputs in function interfaces.  Enhanced builds check
** that the type rule runs *backwards* for an output slot: a function writing
** Base bits may be handed a Derived slot, not the other way around.
**
** They differ only in what they do to the slot's prior contents:
**
**     Sink(T)     scrambles it in debug builds (with corruptions enabled),
**                 so reading before writing crashes instead of lurking
**     Init(T)     skips that, promising to fill every field itself
**     Contra(T)   never scrambles, under any setting--the one to use when
**                 the existing value has to survive, as for an in/out param
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
** Bridges C's implicit void* and enum coercions into C++ builds:
**
**     SomeType* ptr = nocast malloc(sizeof(SomeType));  // works in C++...
**     SomeEnum  e   = nocast some_int_value;            // ...as does this!
**
** Empty macro in C.  In C++, generates a proxy object using `operator+` to
** implicitly trigger the correct static_cast (or C-cast for deep pointers).
** For why `+` (and not a tighter prefix operator), see:
**
**     https://needful.metaeducation.com/precedence
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
**
** The comparison operators below are what make `x == none` mean the same thing
** in a C++ build with no enhancement as it does in the other two build modes:
**
**     https://needful.metaeducation.com/design#worked-example
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
** These macros make casts easy to spot and label their intent.
**
** The C definitions are trivial: they all act like a parenthesized cast.  But
** NEEDFUL_CPP_ENHANCED builds enforce narrower policies.  In C++, casts are
** also hookable for compile-time and runtime validation.
**
** Release builds are zero-overhead; debug builds are structured to minimize
** non-inlined helper overhead.
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


/****[[ known(T,expr): COMPILE-TIME TYPE ASSERTION INSIDE MACROS ]]**********
**
** Docs: https://needful.metaeducation.com/known
**
** Type-checks an expression against T at compile-time without runtime cost.
** Uses no function template, so not a call even in unoptimized debug builds!
**
** As with casts, lenient forms pass through a const T* if the input is const,
** vs. needing `const T*` at callsites.  `rigid_known()` enforces mutability.
**
** `c_cast_known` exists because `cast(T, known(T, expr))` has macro-expansion
** issues when T contains commas; the combined form sidesteps it.
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
** A prose comment mentioning a variable goes stale the moment somebody
** renames that variable, and nothing tells you.  These macros wrap the
** expression instead of describing it, so enhanced C++ builds verify that it
** still compiles--while C builds discard it entirely.
**
**     int i = Get_Integer(...);
**     possibly(i < 0);   // rename `i` and this fails the build
**
** Uppercase versions can be used at global scope (more limited abilities).
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
** Scrambling a variable when it is meant to be dead turns "reads a stale
** value and misbehaves later" into "crashes here."  Enhanced builds apply it
** to Sink(T) output slots on first use; Corrupt_If_Needful() lets you do it by
** hand.  Off by default, since it costs real cycles.
**
** Do not enable it under static analysis: memset makes variables look assigned
** to the analyzer, defeating its own uninitialized-use reporting.
*/

#if !defined(NEEDFUL_DOES_CORRUPTIONS)
   #define NEEDFUL_DOES_CORRUPTIONS  0
#endif

#if (! NEEDFUL_DOES_CORRUPTIONS)
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
**  PART 2: THE PLUMBING
**
******************************************************************************
**
** Portability shims and small utilities that PART 1 is built out of.  There
** is nothing here you have to read to use Needful; the vocabulary above is
** the whole interface.  These are `#define`s, so the vocabulary macros may
** reference them despite appearing earlier in the file.
*/


/****[[ NEEDFUL_NULLPTR: INTERNAL SPELLING OF THE NULL POINTER ]]*************
**
** Needful's own machinery needs a null pointer constant whether or not you
** asked for the cosmetic `nullptr` shim at the bottom of this file.  This is
** one of the few places an `#ifdef __cplusplus` is unavoidable: C++ will not
** implicitly convert `(void*)0` to an arbitrary `T*`.
*/

#if !defined(__cplusplus)
    #define NEEDFUL_NULLPTR  ((void*)0)
#else
    #define NEEDFUL_NULLPTR  nullptr
#endif


/****[[ NOOP, UNIQUE_NAME, USED/UNUSED, SCOPE_GUARD ]]***********************
**
** NEEDFUL_USED() marks a variable as used so `-Wunused-variable` stays quiet.
** In NEEDFUL_CPP_ENHANCED builds NEEDFUL_UNUSED() can be redefined to actually
** randomize non-const contents, to catch use of something meant to be unused.
**
** NEEDFUL_SCOPE_GUARD creates a uniquely-named unused variable, so that
** statement macros like return_if_failed produce a compile error if used in
** an unbraced branch slot.
*/

#define NEEDFUL_NOOP  ((void)0)

#define NEEDFUL_PASTE2(a, b)  a##b
#define NEEDFUL_PASTE1(a, b)  NEEDFUL_PASTE2(a, b)

#define NEEDFUL_UNIQUE_NAME(base)  NEEDFUL_PASTE1(base, __LINE__)

#define NEEDFUL_USED(...)    ((void)(__VA_ARGS__))
#define NEEDFUL_UNUSED(...)  ((void)(__VA_ARGS__))

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
** In Release mode (-O3/NDEBUG), the compiler aggressively deletes the dead
** branch and optimizes switch jump tables.  Triggers an assert in Debug mode.
**
** Uses `needful_nocast_0` and `needful_struct_0` to polymorphically satisfy
** function return signatures without manual type boilerplate.
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

/* (There is deliberately no `needful_unreachable_array`.  C cannot return an
   array type at all, so no C caller could exist--and the C expansion
   `return { 0 };` was a syntax error.  Aggregates go through
   needful_unreachable_struct(T), which forms a C99 compound literal.) */


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
    NEEDFUL_ASSERT(!"dead_end");
    needful_builtin_unreachable;
}

#define needful_dead_end  needful_dead_end_inline()


/****[[ STATIC_ASSERT, STATIC_IGNORE, STATIC_FAIL ]]**************************
**
** C11 added _Static_assert(), so use that when available in C builds.  Pre-C11
** C has too many edge cases for a portable shim, so STATIC_ASSERT remains a
** no-op there.  The C++ enhanced build enforces it in all standards.
**
** NEEDFUL_STATIC_FAIL(msg) takes a STRING LITERAL in every build mode, which
** matches static_assert convention and lets the message contain spaces.  Pre-
** C11 C falls back to a negative-size array typedef: still a hard error, but
** the message is lost, as there is nowhere to put it.
**
** NEEDFUL_STATIC_ASSERT_LVALUE() rules out temporaries and most side-effecting
** expressions at the callsite, which makes argument-repeating macros safe.
**
** The DECLTYPE ones are the plumbing under the comment macros; they are no-ops
** in C, and in C++ they check that the expression is well-formed.
*/

#define NEEDFUL_STATIC_IGNORE(expr) /* https://stackoverflow.com/q/53923706 */ \
    struct NEEDFUL_UNIQUE_NAME(needful_noop_)  /* callsite semicolon trick */

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
** NEEDFUL_NODISCARD is redefined by the C++ enhancements as `[[nodiscard]]`.
** It is what makes an unhandled Result(T) return value visible, and catches
** things like writing `make_failure(...)` instead of `return make_failure(...)`.
**
** NEEDFUL_ALWAYS_INLINE hints that a function should be inlined even in
** non-optimized builds.  Its primary use is marking the trivial no-op default
** CastHook::Validate_Bits, so debug builds pay zero cost with no hook set.
*/

#define NEEDFUL_NODISCARD  /* default to no-op in C or pre-C++17 */

#if defined(__GNUC__) || defined(__clang__)
    #define NEEDFUL_ALWAYS_INLINE  __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
    #define NEEDFUL_ALWAYS_INLINE  __forceinline
#else
    #define NEEDFUL_ALWAYS_INLINE  inline
#endif


/****[[ OPTIONAL: SINGLE-THREADED DEFAULT Result(T) HOOKS ]]******************
**
** Docs: https://needful.metaeducation.com/result#setup-result-hooks
**
** Result(T) needs somewhere to store a pending failure, and your project
** normally supplies that.  Defining NEEDFUL_DECLARE_RESULT_HOOKS in exactly
** one translation unit gets a minimal single-threaded set, for prototyping
** and for the test suite.
*/

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

/* (Needful_Assert_Not_Failing() is derived in the Result(T) section, not
** supplied here -- it is not one of the four hooks.)
*/

#endif  /* NEEDFUL_DECLARE_RESULT_HOOKS */


/*****************************************************************************
**
**  OPTIONAL SHORTHANDS FOR THE `NEEDFUL_XXX` MACROS AS JUST `XXX` MACROS
**
******************************************************************************
**
** These are simple aliases; signatures are documented in comments.
** Avoiding variadic forwarding here sidesteps comma-in-angle-bracket macro
** breakage in C++ and keeps compile times predictable.
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

    #define make_failure /* (...) */     needful_make_failure
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

    /* The unprefixed spellings below pick the *lenient* variant, which passes
    ** a const T* through when the input is const.  Say `rigid_` when you want
    ** mutability enforced at the callsite instead.
    */
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
**  OPTIONAL C++ OVERRIDES FOR ABOVE DEFINITIONS TO BRING THE MACROS TO LIFE
**
******************************************************************************
**
** When NEEDFUL_CPP_ENHANCED builds are activated, then supplemental C++
** headers will #undef the simple definitions given above, and redefine them
** with elaborately-designed machinery!
*/

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


/*****************************************************************************
**
** You can't do things like return C's `NULL` for an Option(T*).  So nearly
** any Needful client will want a nullptr definition for C.  This is cosmetic
** and independent of the Fallible(T) vocabulary; NEEDFUL_NULLPTR above is
** what Needful's own machinery uses.
*/

#if !defined(NEEDFUL_NULLPTR_SHIM)
    #define NEEDFUL_NULLPTR_SHIM  NEEDFUL_DEFINE_ALL_SHORTHANDS
#endif
#if NEEDFUL_NULLPTR_SHIM
  #ifdef __cplusplus
    #include <cstddef>  /* defines `using nullptr_t = decltype(nullptr);` */
  #else
    #if !defined(nullptr)
      #define nullptr  (void*)0
    #endif
  #endif
#endif


#endif  /* !defined(NEEDFUL_H_INCLUDED) */
