//
//  file: %needful-result.h
//  summary: "Simulate Rust's Result<T,E> And `?` w/o Exceptions or longjmp()"
//  homepage: <needful homepage TBD>
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License/
//
//=//// fail() ////////////////////////////////////////////////////////////=//
//
// Sets the global error state (`g_failure`) to the error pointer `p` and
// returns NEEDFUL_RESULT_0 from the current function, propagating th
// error up the call stack. This is for cooperative error signaling, and can
// be caught by except()...see panic() for divergent errors.
//
//      if (bad_condition)
//          return fail (Error_Bad_Thing());
//
//=//// panic() //////////////////////////////////////////////////////////=//
//
// Like `return fail`, but for non-cooperative, abrupt errors that should not
// be handled by normal except() handling or trap, but only propagated until
// they are ultimately `rescue`'d.  Sets the error state, marks the
// divergent flag, and returns NEEDFUL_RESULT_0.
//
//     if (catastrophic_condition)
//         panic (Error_Catastrophe());
//
//=//// trap(expr) ////////////////////////////////////////////////////////=//
//
// Evaluates `expr`, which should return a Result-like wrapper. If no error
// occurs, the result is extracted and execution continues. If an error is
// present in the global error state (`g_failure`), the current function
// returns a special zero value (NEEDFUL_RESULT_0), propagating the
// error up the call stack. This is analogous to Rust's `?` operator.
//
//     Result(int) foo() {
//         trap (bar());
//         // ... code continues if no error ...
//     }
//
//=//// require(expr) /////////////////////////////////////////////////////=//
//
// Like `trap`, but if an error is detected, it also sets the divergent flag
// (`g_divergent = true`) before returning `NEEDFUL_RESULT_0`. Used
// when a function must not continue after a failed operation, and signals
// that the error is not recoverable in the current context.  It means that
// constructs like except() will propagate the error vs. handle it.
//
//    require (bar());
//    // ... code continues only if no error ...
//
//=//// assume() //////////////////////////////////////////////////////////=//
//
// Optimized case for when you have inside knowledge that a Result()-bearing
// function call will not fail.  Needed to do compile-time unwrapping of
// the result container class.
//
//    assume (bar());
//    // ... code always continues ...
//
//
//=//// ...expr... except (decl) {...} ////////////////////////////////////=//
//
// Used after function calls that may have propagated a non-divergent error.
// If an error was propagated, `except` allows handling it.  This leverages
// a tricky standard C feature of for-loop constructs being able to scope
// declarations...`decl` is assigned the error and the error state is
// cleared before running the loop body just once.
//
// Examples:
//
//     Result(int) foo() {
//       bar() except(Error* err) {
//             // handle error in err
//         }
//         // ... code continues if no error ...
//     }
//
//     Result(int) foo() {
//         Option(Error*) err;
//         bar() except(err) {
//             // handle error in err
//         }
//         // code common to erroring and non erroring case
//         if (err)
//             fail (unwrap err);  // manual propagation
//         // ... code continues if no error ...
//     }
//
//=//// rescue (expr) (decl) {...} ////////////////////////////////////////=//
//
// Rescuing diverent failures uses a different syntax than except().
//
//     rescue (
//         target = Some_Result_Bearing_Function(args)
//     ) (Error* e) {
//         // handle error in e
//     }
//
// You should generally avoid handling divergent errors.  Experience has
// borne out that trying to handle generic exceptions from deep in stacks you
// don't understand is a nigh-impossible power to wield wisely.  Only very
// special cases (language REPLs, for example) should try to do this kind
// of recovery.
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// C. An attempt was made to actually subtype errors with Result(T,E) vs.
//    just Result(T), and enforce that you could only auto-propagate errors
//    out of compatible functions.  But injecting the type-awareness into
//    the body of the function is weird:
//
//       #define Result(T,E) /  /* Note: can't backslash in this comment */
//           template<typename RetError = E> /
//                ResultWrapper<T, E> /* function definition */
//
//    This way when you write `Result(T,E) Some_Func(...) {...}` you have
//    awareness of the return error type inside the body for `trap()` to use.
//
//    But it doesn't solve the issue for `except()` which has to telegraph
//    the error type of the called function out of an expression that has to
//    be parenthesized, which is impossible.  And that definition of Result
//    can't work in both a prototype and a definition, because it uses a
//    default template parameter that can only be defined once.  Also, if
//    you try to add inline like `INLINE Result(T, E) Some_Func(...) {...}`
//    that can't work because you can't put INLINE before the `template<>`
//
//    FURTHERMORE... there are limits to the ability to handle errors in a
//    polymorphic way that works in both C and C++.  C++ has inheritance and
//    that's the only way to beat strict aliasing, while C can use common
//    leading substructures which violate strict aliasing in C++.  Also, a
//    divergent error has to be handled via a superclass of some kind.
//
//    AND FINALLY... Needful arose specifically for implementing Rebol, and
//    unlike Rust, Rebol's own error handling lacks a notion of statically
//    subclassing in its `except` and `trap` features.  When all of this is
//    considered together, it explains why Result(T) is not parameterized
//    by an error type, and just assumes one common error.
//


//=//// NEEDFUL_RESULT_0, More Lax Coercing Zero in C++ ////////////=//
//
// If you have code which wants to polymorphically be able to convert to an
// Option(SomeEnum) or SomePointer* or bool, etc. then this introduces a
// permissive notion of zero.  It lets you bring back some of the flexibility
// that C originally had with permissive 0 conversions, but more tightly
// controlled through a special type.
//
// 1. The main purpose of permissive zero is to be the polymorphic return
//    value of `fail(...)` used in `return fail (...)` that is able to make
//    the T in any Result(T) type.  Making it [[nodiscard]] helps catch
//    cases where someone omits the `return`, which would be a mistake
//    (easy to make, as `panic (...)` looks similar and takes an error but
//    is *not* used with return.)
//

struct NEEDFUL_NODISCARD Result0Struct {  // [[nodiscard]] is good [1]
   // no members or behaviors, should only initialize ResultWrapper<T>
};

#undef NEEDFUL_RESULT_0
#define NEEDFUL_RESULT_0  needful::Result0Struct{}


//=//// DETECT OPTION WRAPPER TRAIT //////////////////////////////////////=//
//
// Result(T) in particular wants to disable you from saying `return nullptr;`
// unless you specifically are using `Result(Option(T*))`.  This keeps you
// from mistakenly returning a nullptr to indicate failure, when you need
// to use `return fail(...)` to do so.
//
// This can only be tested for if you're using the OptionWrapper<T> type,
// so you don't get the check if you aren't doing a build with it.
//

template<typename T>
struct OptionWrapper;

#if NEEDFUL_OPTION_USES_WRAPPER
    template<typename>
    struct IsOptionWrapper : std::false_type {};
#endif


//=//// RESULT WRAPPER ////////////////////////////////////////////////////=//
//
// The Result type is trickery that mimics something like Rust's Result<T, E>
// type.  It is a wrapper that characterizes a function which may return
// a failure by means of a global variable, but will construct the result
// from zero in that case.
//
// 1. The error machinery hinges on the ability to return a zerolike state
//    for anything that is a Result(T) in the case of a failure.  But rather
//    than allow Result to be constructed from any integer in the C++
//    checked build, it's narrowly constructible from Result0Struct,
//    which is what `return fail(...)` returns.
//
// 2. It's important that functions that particpate in the Result(T) error
//    handling system don't return `nullptr` as a way of signaling failure.
//    Not going through `return fail(...)` is a mistake, since it skips
//    setting the global error state.  But since the error state is separate
//    from the return value, zerolike states can be legal returns...so we
//    allow it IF your return type is Result(Option(T*)) vs. Result(T*).
//
// 3. Attempts to generalize the construction of ResultWrapper<T> to wrappers
//    that are able to produce ResultWrapper<T> by means of conversion
//    operators seemed to fail.  DowncastHolder is a good example of what
//    didn't work and had to be overridden explicitly.  Someone who knows
//    more than me can look into this and see if it can generalize so that
//    DowncastHolder need not be mentioned explicitly here.
//

template<typename T>
struct Result0InitHelper {
    static T init() { return x_cast(T, 0); }
};

template<typename T>
struct NEEDFUL_NODISCARD ResultWrapper {
    NEEDFUL_DECLARE_WRAPPED_FIELD (T, r);

    ResultWrapper() = delete;

    ResultWrapper(Result0Struct&&)  // how failures are returned [1]
      : r {Result0InitHelper<T>::init()}
        {}

    ResultWrapper(const std::nullptr_t&)  // usually no `return nullptr;` [2]
        : r (nullptr)
    {
      #if NEEDFUL_OPTION_USES_WRAPPER
        static_assert(
            IsOptionWrapper<T>::value,
            "Use Result(Option(T*)) if `return nullptr`; is not a mistake"
        );
      #endif
    }

    template <
        typename U,
        typename = enable_if_t<
            not needful_is_convertible_v(decay_t<U>, ResultWrapper<T>)
            and not IsResultWrapper<decay_t<U>>::value
            and needful_is_convertible_v(U, T)  // implicit cast okay
        >
    >
    ResultWrapper(U&& something) : r {something} {}

    template <
        typename U,
        typename = enable_if_t<
            not needful_is_convertible_v(decay_t<U>, ResultWrapper<T>)
            and not IsResultWrapper<decay_t<U>>::value
            and not needful_is_convertible_v(U, T)  // must cast explicitly
        >,
        typename = void
    >
    explicit ResultWrapper(U&& something)
        : r {needful_xtreme_cast(T, something)}
    {}

    template <
        typename X,
        typename = enable_if_t<needful_is_convertible_v(X, T)>
    >
    ResultWrapper (const ResultWrapper<X>& result)
        : r {result.r}
    {}

    template <
        typename X,
        typename = enable_if_t<not needful_is_convertible_v(X, T)>,
        typename = void
    >
    explicit ResultWrapper (const ResultWrapper<X>& result)
        : r {needful_xtreme_cast(T, result.r)}
    {}

    template<
        typename X,
        typename = enable_if_t<needful_is_convertible_v(T, X)>
    >
    ResultWrapper (const UnhookableDowncastHolder<X> down)  // generalize? [3]
        : r {needful_xtreme_cast(T, down.f)}
    {
    }

    template<
        typename X,
        typename = enable_if_t<needful_is_convertible_v(T, X)>
    >
    ResultWrapper (const HookableDowncastHolder<X> down)  // generalize? [3]
        : r {needful_lenient_hookable_cast(T, down.f)}
    {
    }
};

#undef NeedfulResult
#define NeedfulResult(T) /* not Result(T,E)... see [C] */ \
    needful::ResultWrapper<T>

template<typename X>
struct IsResultWrapper<ResultWrapper<X>> : std::true_type {};


//=//// ZERO (METAPROGRAMMING SURROGATE FOR LAME `void`) //////////////////=//
//
// 1. This could be called ZERO in all caps to be more consistent with
//    enum naming, but the enum is just an implementation detail.  This is
//    not like the all-caps TRASH or VOID that suggest Rebol types, it's
//    specifically an implementation detail in C.  Lowercase seems better.
//
// 2. When building as C++, when a `fail` produces NEEDFUL_RESULT_0
//    the ResultWrapper<> will typically try to construct its contents with
//    a cast to zero.  Rather than make the Zero type support this generic
//    methodology, we make a specialization of ResultWrapper<Zero>.
//
//    Besides stopping constructions of Zero from random integers, having a
//    specialization may make it more efficient--if only in debug builds.
//    Less code should be generated since the ResultWrapper<Zero> is a
//    completely empty class with no `Zero` member.
//

struct ZeroStruct {};

#undef NeedfulZero
#define NeedfulZero  needful::ZeroStruct  // type in caps, instance lower [1]

#undef needful_zero
#define needful_zero  needful::ZeroStruct{}  // instantiate {} zero instance

template<>
struct NEEDFUL_NODISCARD ResultWrapper<Zero> {
    ResultWrapper() = delete;

    ResultWrapper(Result0Struct&&) {}
    ResultWrapper(Zero&&) {}
};


//=//// RESULT EXTRACTOR //////////////////////////////////////////////////=//
//
// 1. The error is a bit opaque if you write:
//
//        trap (
//           Some_Function();
//        );
//
//    ignoring returned value of type needful::ResultWrapper<RebolValueStruct*>
//    declared with attribute 'nodiscard'
//
//    We try to give you a hint what's going on with the comment, if you
//    read on to the error about the operator not getting its left side.
//

struct ResultExtractor {};

template<typename T>
inline T operator>>(
    const ResultWrapper<T>& result,
    const ResultExtractor&
){
    return result.r;
}

inline void operator>>(
    const ResultWrapper<ZeroStruct>&,
    const ResultExtractor&
){
}

template<typename T>
inline UnhookableDowncastHolder<T> operator>>(
    const UnhookableDowncastHolder<ResultWrapper<T>>& down,
    const ResultExtractor&
){
    return UnhookableDowncastHolder<T> {down.f.r};
}

template<typename T>
inline HookableDowncastHolder<T> operator>>(
    const HookableDowncastHolder<ResultWrapper<T>>& down,
    const ResultExtractor&
){
    return HookableDowncastHolder {down.f.r};
}

static constexpr ResultExtractor g_result_extractor{};

#undef needful_postfix_extract_result
#define needful_postfix_extract_result \
    /* ; <-- ERROR? DON'T PUT SEMICOLON! [1] */ >> needful::g_result_extractor
