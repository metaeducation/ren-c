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
// returns NEEDFUL_PERMISSIVE_ZERO from the current function, propagating th
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
// divergent flag, and returns NEEDFUL_PERMISSIVE_ZERO.
//
//     if (catastrophic_condition)
//         panic (Error_Catastrophe());
//
//=//// trap(expr) ////////////////////////////////////////////////////////=//
//
// Evaluates `expr`, which should return a Result-like wrapper. If no error
// occurs, the result is extracted and execution continues. If an error is
// present in the global error state (`g_failure`), the current function
// returns a special zero value (NEEDFUL_PERMISSIVE_ZERO), propagating the
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
// (`g_divergent = true`) before returning `NEEDFUL_PERMISSIVE_ZERO`. Used
// when a function must not continue after a failed operation, and signals
// that the error is not recoverable in the current context.  It means that
// constructs like except() will propagate the error vs. handle it.
//
//    require (bar());
//    // ... code continues only if no error ...
//
//=//// guarantee() ///////////////////////////////////////////////////////=//
//
// Optimized case for when you have inside knowledge that a Result()-bearing
// function call will not fail.  Needed to do compile-time unwrapping of
// the result container class.
//
//    guarantee (bar());
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
//=//// DISCARDING VARIANTS ///////////////////////////////////////////////=//
//
// `trap` and `require` have conflicting requirements: they need to be able
// to execute return statements from within the expanded macro, but also
// want to be usable as expressions on the right hand of an assignment.  This
// forces them to expand into sequential statements that are not enclosed in
// parentheses or a group.
//
// Given that reality, it's not safe to use `trap` or `require` as the branch
// of something like an `if` or `while` without a block around it:
//
//      if (condition)
//          trap (Some_Result_Bearing_Function(args));  // bad usage
//
// The expression portion of the `trap` expansion will be underneath the `if`
// while the subsequent for reacting to failures and potentially executing
// a `return` will be outside the `if`.
//
// Use of [[nodiscard]] and the "hot potato" extraction mechanism helps turn
// these usages into compile-time errors.  But once you get the error, you
// need some way to suppress it.
//
// There's a discarded() macro you could use, which puts the code in a
// `do {...} while (0)` block and also suppresses the [[nodiscard]] warning.
// It looks a little wordy in use:
//
//      if (condition)
//          discarded(trap (Some_Result_Bearing_Function(args)));
//
// So macros that do this for you are named e.g. `trapped` and `required`:
//
//      if (condition)
//          trapped (Some_Result_Bearing_Function(args));
//
// 1. `excepted` is weirder than `trapped` and `required`, but fits the
//    pattern adn it's hard to think of what else to call it.
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


//=//// NEEDFUL_PERMISSIVE_ZERO, More Lax Coercing Zero in C++ ////////////=//
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

struct NEEDFUL_NODISCARD PermissiveZeroStruct {  // [[nodiscard]] is good [1]
    template<typename T>
    operator T() const {
        return x_cast(T, 0);
    }
};

#undef NEEDFUL_PERMISSIVE_ZERO
#define NEEDFUL_PERMISSIVE_ZERO  needful::PermissiveZeroStruct{}


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

#if NEEDFUL_OPTION_USES_WRAPPER
    template<typename>
    struct IsOptionWrapper : std::false_type {};
#endif


//=//// EXTRACTED RESULT "HOT POTATO" /////////////////////////////////////=//
//
// The Result(T) type is [[nodiscard]] (C++17 feature, with some pre-C++17
// support in MSVC and GCC).  That protects against:
//
//     Some_Result_Bearing_Function(args);  // no trap, no require, no except
//
// You'll get an error from your C++ builds because the Result(T) is not used,
// guiding to the need for triage.  But due to the design of the macros and
// language limitations, there's a problem with:
//
//      if (condition)
//         trap(Some_Result_Bearing_Function(args));  // no warning
//
// Because the trap macro has to embed `return` statements -and- wants to
// be used on the right hand side of assignments, it can't be wrapped up in
// `do {...} while (0)` or parentheses to make it "safe" when used as a
// branch. It expands to one expression that's inside the branch and then
// subsequent lines that aren't.
//
// Since the Result(T) has already been "triaged" by the trap macro, its
// [[nodiscard]] can't help.  So what's done is instead to use a 2-step
// process...where an ExtractedHotPotato<T> is made, as another [[nodiscard]]
// type that covers the case of a missing assignment on the left hand side
// of the trap.  (If the assignment were present, it would naturally disallow
// use as a branch or a loop body.)
//
// With this you have:
//
//      if (condition)
//         trap (Some_Result_Bearing_Function(args));  // warning on discard
//
// This hot potato then has specialized discarding operations, e.g.` trapped`:
//
//     if (condition)
//         trapped (Some_Result_Bearing_Function(args)));
//
// It's unfortunate to need another name for this, but in practice it is very
// easy for mistakes to be made without the protections.
//

template<typename T>
struct NEEDFUL_NODISCARD ExtractedHotPotato {
    T x;

    ExtractedHotPotato(const T& something)
        : x {something} {}

    operator T() const {
        return x;
    }
};


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
//    checked build, it's narrowly constructible from PermissiveZeroStruct,
//    which is what `return fail(...)` returns.
//
// 2. It's important that functions that particpate in the Result(T) error
//    handling system don't return `nullptr` as a way of signaling failure.
//    Not going through `return fail(...)` is a mistake, since it skips
//    setting the global error state.  But since the error state is separate
//    from the return value, zerolike states can be legal returns...so we
//    allow it IF your return type is Result(Option(T*)) vs. Result(T*).
//

template<typename T>
struct NEEDFUL_NODISCARD ResultWrapper {
    NEEDFUL_DECLARE_WRAPPED_FIELD (T, r);

    ResultWrapper() = delete;

    ResultWrapper(PermissiveZeroStruct&&)  // how failures are returned [1]
        : r (u_cast(T, NEEDFUL_PERMISSIVE_ZERO))
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

    template <typename U>
    ResultWrapper(const U& other) : r {other} {}

    template <typename X>
    ResultWrapper (const ResultWrapper<X>& other)
        : r {other.r}
    {}

    template <typename U>
    explicit ResultWrapper(U&& something)  // see OptionWrapper
        : r {needful_xtreme_cast(T, std::forward<U>(something))}
    {}

    ExtractedHotPotato<T> Extract_Hot() const  // [[nodiscard]] version
      { return r; }

    T Extract_Cold() const  // plain type, discardable
      { return r; }  // (not used at the moment, only extract hot vs. discard)
};

#undef NeedfulResult
#define NeedfulResult(T) /* not Result(T,E)... see [C] */ \
    needful::ResultWrapper<T>

#undef Needful_Prefix_Extract_Hot
#define Needful_Prefix_Extract_Hot(expr)  (expr).Extract_Hot()

#undef Needful_Prefix_Discard_Result
#define Needful_Prefix_Discard_Result(expr)  USED(expr)

struct ResultExtractor {};

template<typename T>
ExtractedHotPotato<T> operator>>(
    ResultWrapper<T>&& result,
    const ResultExtractor& right
){
    UNUSED(right);
    return result.Extract_Hot();
}

constexpr ResultExtractor g_result_extractor = {};

#undef Needful_Postfix_Extract_Hot
#define Needful_Postfix_Extract_Hot  >> needful::g_result_extractor


// 1. This could be called ZERO in all caps to be more consistent with
//    enum naming, but the enum is just an implementation detail.  This is
//    not like the all-caps TRASH or VOID that suggest Rebol types, it's
//    specifically an implementation detail in C.  Lowercase seems better.
//
// 2. When building as C++, when a `fail` produces NEEDFUL_PERMISSIVE_ZERO
//    the ResultWrapper<> will typically try to construct its contents with
//    a cast to zero.  Rather than make the Zero type support this generic
//    methodology, we make a specialization of ResultWrapper<Zero>.
//
//    Besides stopping constructions of Zero from random integers, having a
//    specialization may make it more efficient--if only in debug builds.
//    Less code should be generated since the ResultWrapper<Zero> is a
//    completely empty class with no `Zero` member.
//

struct ZeroStruct {
    ZeroStruct () = default;  // handle `return zero;` case
    /* ZeroStruct(int) {} */  // don't allow construction from int [2]
};

#undef NeedfulZero
#define NeedfulZero  needful::ZeroStruct  // type in caps, instance lower [1]

#undef needful_zero
#define needful_zero  needful::ZeroStruct{}  // instantiate {} zero instance

template<>
struct ResultWrapper<Zero> {
    ResultWrapper() = delete;

    ResultWrapper(PermissiveZeroStruct&&) {}
    ResultWrapper(Zero&&) {}

    ExtractedHotPotato<Zero> extract() {
        return ExtractedHotPotato<Zero>{zero};
    }
};

inline void operator>>(
    ResultWrapper<Zero>&& result,
    const ResultExtractor& right
){
    UNUSED(result);
    UNUSED(right);
}

//=//// RESULT DISCARDER //////////////////////////////////////////////////=//
//
// The Result(T) type is [[nodiscard]], redefine macros to discard it.
//

struct ResultDiscarder {};

template<typename T>
inline T operator|(  // using `|` for precedence lower than `>>`
    const ResultDiscarder&,
    const ExtractedHotPotato<T>& extraction
){
    USED(extraction);  // mark as used, do nothing
}

template<typename T>
inline void operator>>(
    const ResultWrapper<T>& result,
    const ResultDiscarder&
){
    USED(result);  // mark as used, do nothing
}

static constexpr ResultDiscarder g_result_discarder{};

#undef Needful_Postfix_Discard_Result
#define Needful_Postfix_Discard_Result  >> needful::g_result_discarder
