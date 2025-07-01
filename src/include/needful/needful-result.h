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
// handling errors in a style similar to Rust's `Result<T, E>` and the `?`
// operator, but without requiring exceptions or setjmp/longjmp in C++ builds.
//
// They multiplex error and value returns using a global error state and
// special return values, allowing for expressive, type-safe error handling.
//
// Thee macros enable a style of programming that is portable between C and
// C++, avoids exceptions and longjmps, and provides clear, explicit error
// handling and propagation.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// A. In order for these macros to work, they need to be able to test and
//    clear the global error state...as well as a flag as to whether the
//    failure is divergent or not.  Hence you have to define:
//
//        Needful_Test_And_Clear_Failure()
//        Needful_Get_Failure()
//        Needful_Get_Failure_Divergence()
//


//=//// PERMISSIVE_ZERO, More Lax Coercing Zero in C++ ////////////////////=//
//
// If you have code which wants to polymorphically be able to convert to an
// Option(SomeEnum) or SomePointer* or bool, etc. then this introduces a
// permissive notion of zero.  It lets you bring back some of the flexibility
// that C originally had with permissive 0 conversions, but more tightly
// controlled through a special type.
//
// 1. While it is legal in the C standard to cast 0 to a pointer (or to an
//    enum regardless of whether the enum contains a zero state), compilers
//    still may warn about it.  We rely on the behavior, so you may have
//    to disable warnings, such as -Wno-int-conversion for GCC.
//

#if NO_CPLUSPLUS_11
    #define PERMISSIVE_ZERO  0  // likely must disable warnings in C [1]
#else
    struct PermissiveZero {
        template<typename T>
        operator T() const {
            return u_cast(T, 0);
        }
    };
    #define PERMISSIVE_ZERO  PermissiveZero{}
#endif


//=//// RESULT TYPE ///////////////////////////////////////////////////////=//
//
// The Result type is trickery that mimics something like Rust's Result<T, E>
// type.  It is a wrapper that characterizes a function which may return
// a failure by means of a global variable, but will construct the result
// from zero in that case.
//

#if NO_CPLUSPLUS_11
    #define Result(T)   T
    #define Costless_Extract_Result(result)  result
    #define Then_Costless_Extract_Result
#else
    template<typename T>
    struct ResultWrapper {
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

        T& extract() {
            return p;
        }
    };

    #define Result(T) \
        ResultWrapper<T>  // C++11 version of Result, which is a wrapper

    #define Costless_Extract_Result(result)  result.extract()

    struct ResultExtractor {};

    template<typename T>
    T& operator>>(
        ResultWrapper<T>&& result,
        const ResultExtractor& right
    ){
        UNUSED(right);
        return result.extract();
    }

    constexpr ResultExtractor g_result_extractor = {};

    #define Then_Costless_Extract_Result  >> g_result_extractor
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

        void extract() {}  // less codegen in debug than `Nothing& extract()`
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
    (assert(not g_failure and not g_divergent), \
        g_failure = Derive_Error_From_Pointer(p), \
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

#define needful_panic(p) \
    return (assert(not g_failure and not g_divergent), \
        Panic_Prelude_File_Line_Tick(__FILE__, __LINE__, TICK), \
        g_failure = Derive_Error_From_Pointer(p), \
        g_divergent = true, \
        PERMISSIVE_ZERO)


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
    (assert(not g_failure), Costless_Extract_Result(expr)); \
    if (g_failure) \
      { possibly(g_divergent); return PERMISSIVE_ZERO; } \
    NOOP


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
    (assert(not g_failure), Costless_Extract_Result(expr)); \
    if (g_failure) \
      { possibly(g_divergent); g_divergent = true; return PERMISSIVE_ZERO; } \
    NOOP


//=//// wont_fail() ///////////////////////////////////////////////////////=//
//
// Optimized case for when you have inside knowledge that a Result()-bearing
// function call will not fail.  Needed to do compile-time unwrapping of
// the result container class.
//
//    wont_fail (bar());
//    // ... code always continues ...
//

#define needful_wont_fail(expr) \
    (assert(not g_failure), Costless_Extract_Result(expr)); \
        assert(not g_failure)


//=//// ...expr... except(decl) {...} /////////////////////////////////////=//
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

#define needful_except(decl) \
    /* expression */ Then_Costless_Extract_Result; \
        if (g_divergent) { return PERMISSIVE_ZERO; } \
        for (decl = g_failure; Needful_Test_And_Clear_Failure(); )
           /* implicitly takes code block after macro as except()-body */


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
#define wont_fail(expr)     needful_wont_fail(expr)

#define /* expr */ except(decl) /* {body} */ \
    needful_except(decl)
