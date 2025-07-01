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
//=/////////////////////////////////////////////////////////////////////////=//
//
// These macros provide a C/C++-compatible mechanism for propagating and
// handling errors in a style similar to Rust's `Result<T, E>`, all without
// requiring exceptions or setjmp/longjmp in C++ builds.
//
// Instead, the multiplexing of an Error with the return value type is done
// with a global (or more generally, thread-local) error state.
//
// A key feature is the ability to propagate errors automatically.  So
// instead of having to laboriously write things like:
//
//     Error* Some_Func(int* result, int x) {
//         if (x < 304)
//             return fail ("the value is too small");
//         *result = x + 20;
//         return nullptr;  // no error
//     }
//
//     Error* Other_Func(int* result) {
//         int y;
//         Error* e = Some_Func(&y, 1000);
//         if (e)
//             return e;
//         assert(y == 1020);
//
//         int z;
//         Error* e = Some_Func(&z, 10);
//         if (e)
//             return e;
//         printf("this would never be reached...");
//
//         *result = z;
//         return nullptr;  // no error
//     }
//
// You can write it like this:
//
//     Result(int) Some_Func(int x) {
//         if (x < 304)
//             return fail ("the value is too small");
//         return x + 20;
//     }
//
//     Result(int) Other_Func(void) {
//         int y = trap (Some_Func(1000));
//         assert(y == 1020);
//
//         int z = trap (Some_Func(10);
//         printf("this would never be reached...");
//
//         return z;
//     }
//
// Also of particular note is the syntax for catching "exceptional" cases
// (though again, not C++ exceptions and not longjmps).  This syntax looks
// particularly natural due to clever use of a `for` loop to get a scope:
//
//     int result = Some_Func(10 + 20) except (Error* e) {
//         /* e scoped to the block */
//         printf("caught an error: %s\n", e->message);
//     }
//
// So the macros enable a shockingly literate style of programming that is
// portable between C and C++, avoids exceptions and longjmps, and provides
// clear, explicit error handling and propagation.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// A. As long as a datatype can be constructed from 0 within the rules of the
//    C standard (pointers, enums, integers) it can be used with Result().
//    You may have to disable compiler warnings related to the enum or
//    pointer casts of 0, but it is legal so you should be able to do it.
//
//    In C builds with GCC/Clang, the flag you want is `-Wno-int-conversion`
//
//    (The C++ build doesn't require disabling the warnings because it uses
//    a special "PermissiveZero" class to more precisely capture the intent.)
//
// B. In order for these macros to work, they need to be able to test and
//    clear the global error state...as well as a flag as to whether the
//    failure is divergent or not.  Hence you have to define:
//
//        ErrorType* Needful_Test_And_Clear_Failure()
//        ErrorType* Needful_Get_Failure()
//        void Needful_Set_Failure(ErrorType* error)
//        bool Needful_Get_Divergence()
//        void Needful_Force_Divergent()
//
//    These can be functions or macros with the same signature.  They should
//    use thread-local state if they're to work in multi-threaded code.
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


//=//// PERMISSIVE_ZERO, More Lax Coercing Zero in C++ ////////////////////=//
//
// If you have code which wants to polymorphically be able to convert to an
// Option(SomeEnum) or SomePointer* or bool, etc. then this introduces a
// permissive notion of zero.  It lets you bring back some of the flexibility
// that C originally had with permissive 0 conversions, but more tightly
// controlled through a special type.
//

#if NO_CPLUSPLUS_11
    #define PERMISSIVE_ZERO  0  // likely must disable warnings in C [A]
#else
    struct PermissiveZero {
        template<typename T>
        operator T() const {
            return u_cast(T, 0);
        }
    };
    #define PERMISSIVE_ZERO  PermissiveZero{}
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
//         trap(Some_Result_Bearing_Function(args));  // warning on discard
//
// This hot potato then has a specialized discarding operation:
//
//     if (condition)
//         discarded(trap(Some_Result_Bearing_Function(args)));
//
// It's a bit of a mouthful, but in practice it is very easy for mistakes
// to be made without the protections.
//
#if CPLUSPLUS_11
    template<typename T>
    struct NEEDFUL_NODISCARD ExtractedHotPotato {
        T p;

        ExtractedHotPotato(const T& something)
            : p {something} {}

        operator T() const {
            return p;
        }
    };
#endif


//=//// RESULT TYPE ///////////////////////////////////////////////////////=//
//
// The Result type is trickery that mimics something like Rust's Result<T, E>
// type.  It is a wrapper that characterizes a function which may return
// a failure by means of a global variable, but will construct the result
// from zero in that case.
//

#if NO_CPLUSPLUS_11
    #define Result(T)  T  // not Result(T,E)... see [C]

    #define Needful_Extract_Result(result)  result
    #define Needful_Then_Extract_Result
#else
    template<typename T>
    struct NEEDFUL_NODISCARD ResultWrapper {
        T p;  // not always pointer, but use common convention with Sink/Need

        ResultWrapper() = delete;

        ResultWrapper(const PermissiveZero&)
            : p (u_cast(T, PERMISSIVE_ZERO))
          {}

        template <typename U>
        ResultWrapper(const U& other) : p {other} {}

        template <typename X>
        ResultWrapper (const ResultWrapper<X>& other)
            : p (other.p)
        {}

        template <typename U>
        explicit ResultWrapper(U&& something)  // see OptionWrapper
            : p (u_cast(T, std::forward<U>(something)))
        {}

        ExtractedHotPotato<T> extract() {
            return p;
        }
    };

    #define Result(T) /* not Result(T,E)... see [C] */ \
        ResultWrapper<T>

    #define Needful_Extract_Result(result)  result.extract()

    struct ResultExtractor {};

    template<typename T>
    ExtractedHotPotato<T> operator>>(
        ResultWrapper<T>&& result,
        const ResultExtractor& right
    ){
        UNUSED(right);
        return result.extract();
    }

    constexpr ResultExtractor g_needful_result_extractor = {};

    #define Needful_Then_Extract_Result  >> g_needful_result_extractor
#endif


//=//// "NOTHING" TYPE (RETURN-ABLE `void` SURROGATE) /////////////////////=//
//
// When using wrappers like Result(T) to do their magic, void can't be used
// as something that is able to be constructed from 0 when trying to do
// something like `return fail (...);`  Result(void) would just be void in
// a C build, and you can't pass any parameter to the `return` of a function
// that has a void return type (not even void itself, as `return void;`)
//
// Proposals to change this have been rejected, e.g. this from 2015:
//
//   https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/p0146r0.html
//
// So instead of Result(void) we use Result(Nothing).  This is like what Rust
// would call the "unit type" (which they write as `()`).  But "Unit" doesn't
// seem like a great name for an empty type in C++.
//
// To make it work in C, it's just an enum with a single zero value.
//
// 1. This could be called NOTHING in all caps to be more consistent with
//    enum naming, but the enum is just an implementation detail.  This is
//    not like the all-caps TRASH or VOID that suggest Rebol types, it's
//    specifically an implementation detail in C.  Lowercase seems better.
//
// 2. When building as C++, in the case of a `fail` producing PERMISSIVE_ZERO
//    the ResultWrapper<> will typically try to construct its contents with
//    an cast to zero.  Rather than make the None type support this generic
//    methodology, we make a specialization of ResultWrapper<None>.
//
//    Besides stopping constructions of None from random integers, having a
//    specialization may make it more efficient--if only in debug builds.
//    Less code should be generated since the ResultWrapper<None> is a
//    completely empty class with no `None` member.
//
#if NO_CPLUSPLUS_11
    typedef enum {
        nothing = 0  // use lowercase for the constant [1]
    } Nothing;
#else
    struct Nothing {
        Nothing () = default;  // handle `return nothing;` case
        /* Nothing(int) {} */  // don't allow construction from int [2]
    };
    #define nothing  Nothing{}  // construct a Nothing instance

    template<>
    struct ResultWrapper<Nothing> {
        ResultWrapper() = delete;

        ResultWrapper(const PermissiveZero&) {}
        ResultWrapper(const Nothing&) {}

        ExtractedHotPotato<Nothing> extract() {
            return ExtractedHotPotato<Nothing>(nothing);
        }
    };

    INLINE void operator>>(
        ResultWrapper<Nothing>&& result,
        const ResultExtractor& right
    ){
        UNUSED(result);
        UNUSED(right);
    }
#endif


//=//// fail() ////////////////////////////////////////////////////////////=//
//
// Sets the global error state (`g_failure`) to the error pointer `p` and
// returns `PERMISSIVE_ZERO` from the current function, propagating the error
// up the call stack. This is for cooperative error signaling, and can be
// caught by except()...see panic() for divergent errors.
//
//      if (bad_condition)
//          return fail (Error_Bad_Thing());
//

#define needful_fail(p) \
    (assert(not Needful_Get_Failure() and not Needful_Get_Divergence()), \
    Needful_Set_Failure(p), \
    PERMISSIVE_ZERO)


//=//// panic() //////////////////////////////////////////////////////////=//
//
// Like `return fail`, but for non-cooperative, abrupt errors that should not
// be handled by normal error propagation. Sets the error state, marks the
// divergent flag, and returns `PERMISSIVE_ZERO`. In builds with exception
// or longjmp support, this may also trigger a non-local jump or throw.
//
//     if (catastrophic_condition)
//         panic (Error_Catastrophe());
//

#define needful_panic(p) do { \
    assert(not Needful_Get_Failure() and not Needful_Get_Divergence()); \
    Needful_Set_Failure(p); \
    Needful_Force_Divergent(); \
    return PERMISSIVE_ZERO; \
} while (0)


//=//// trap(expr) ////////////////////////////////////////////////////////=//
//
// Evaluates `expr`, which should return a Result-like wrapper. If no error
// occurs, the result is extracted and execution continues. If an error is
// present in the global error state (`g_failure`), the current function
// returns a special zero value (`PERMISSIVE_ZERO`), propagating the error
// up the call stack. This is analogous to Rust's `?` operator.
//
//     Result(int) foo() {
//         trap (bar());
//         // ... code continues if no error ...
//     }
//

#define needful_trap(expr) \
    (assert(not Needful_Get_Failure()), Needful_Extract_Result(expr)); \
    if (Needful_Get_Failure()) { \
        possibly(Needful_Get_Divergence()); \
        return PERMISSIVE_ZERO; \
    } NOOP  /* force require semicolon at callsite */


//=//// require(expr) /////////////////////////////////////////////////////=//
//
// Like `trap`, but if an error is detected, it also sets the divergent flag
// (`g_divergent = true`) before returning `PERMISSIVE_ZERO`. This is used
// when a function must not continue after a failed operation, and signals
// that the error is not recoverable in the current context.  It means that
// constructs like except() will propagate the error vs. handle it.
//
//    require (bar());
//    // ... code continues only if no error ...

#define needful_require(expr) \
    (assert(not Needful_Get_Failure()), Needful_Extract_Result(expr)); \
    if (Needful_Get_Failure()) { \
        possibly(Needful_Get_Divergence()); \
        Needful_Force_Divergent(); \
        return PERMISSIVE_ZERO; \
    } NOOP  /* force require semicolon at callsite */


//=//// guarantee() ///////////////////////////////////////////////////////=//
//
// Optimized case for when you have inside knowledge that a Result()-bearing
// function call will not fail.  Needed to do compile-time unwrapping of
// the result container class.
//
//    guarantee (bar());
//    // ... code always continues ...
//

#define needful_guarantee(expr) \
    (assert(not Needful_Get_Failure()), Needful_Extract_Result(expr)); \
    assert(not Needful_Get_Failure())


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
// 1. There's no reason to involve an Option() type here, because the code
//    is not user-exposed.

#define needful_except_core(decl,extractor) \
    /* expression */ extractor; \
    if (Needful_Get_Divergence()) \
        { return PERMISSIVE_ZERO; } \
    for (decl = Needful_Get_Failure(); Needful_Test_And_Clear_Failure(); )
        /* implicitly takes code block after macro as except()-body */

#define needful_except(decl) \
    needful_except_core(decl, Needful_Then_Extract_Result)


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

#define needful_rescue_core(expr,extractor) \
    expr extractor needful_rescue_then

#define /* rescue (expr) */ needful_rescue_then(decl) \
    ; /* semicolon required */ possibly(Needful_Get_Divergence()); \
    for (decl = Needful_Get_Failure(); Needful_Test_And_Clear_Failure(); )
        /* implicitly takes code block after macro as except()-body */

#define needful_rescue(expr) \
    needful_rescue_core(expr, Needful_Then_Extract_Result)


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
#if NO_CPLUSPLUS_11
    #define Needful_Then_Consume_Result

    #define needful_discarded(expr) do { expr; } while (0)
#else
    struct ExtractionDiscarder {};

    template<typename T>
    INLINE void operator|(  // using `|` for precedence lower than `>>`
        const ExtractionDiscarder&,
        const ExtractedHotPotato<T>& extraction
    ){
        USED(extraction);  // mark as used, do nothing
    }

    template<typename T>
    INLINE void operator>>(
        const ResultWrapper<T>& result,
        const ExtractionDiscarder&
    ){
        USED(result);  // mark as used, do nothing
    }

    static constexpr ExtractionDiscarder g_needful_extraction_discarder{};

    #define needful_discarded(expr) do { \
        g_needful_extraction_discarder | /* <- overloaded */ expr; \
    } while (0)

    #define Needful_Then_Consume_Result  >> g_needful_extraction_discarder
#endif


#define needful_trapped(expr)       needful_discarded(needful_trap(expr))
#define needful_required(expr)      needful_discarded(needful_require(expr))
#define needful_guaranteed(expr)    needful_discarded(needful_guarantee(expr))

#define needful_excepted(decl) /* weird name [1] */ \
    needful_except_core(decl, Needful_Then_Consume_Result)

#define needful_rescued(expr)  /* weird name [1] */ \
    needful_rescue_core(expr, Needful_Then_Extract_Result)


//=//// SHORTHAND MACROS //////////////////////////////////////////////////=//
//
// If you use these words for things like variable names, you will have
// problems if they are defined as macros.  You can pick your own terms but
// these are the ones that were used in Needful's original client.
//

#define fail(p)             needful_fail(p)
#define panic(p)            needful_panic(p)

#define trap(expr)          needful_trap(expr)
#define require(expr)       needful_require(expr)
#define guarantee(expr)     needful_guarantee(expr)

#define /* expr */ except(decl) /* {body} */ \
    needful_except(decl)

#define sys_util_rescue(expr) /* (decl) {body} */ \
    needful_rescue(expr)

#define discarded(expr)     needful_discarded(expr)
#define trapped(expr)       needful_trapped(expr)
#define required(expr)      needful_required(expr)
#define guaranteed(expr)    needful_guaranteed(expr)

#define /* expr */ excepted(decl) /* {body} */ \
    needful_excepted(decl)

#define sys_util_rescued(expr) /* (decl) {body} */ \
    needful_rescued(expr)
