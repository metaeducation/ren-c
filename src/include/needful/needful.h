//
//  file: %needful.h
//  summary: "Trivial C macros that have powerful features in C++11 or higher"
//  homepage: <needful homepage TBD>
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Needful is a header-only library, containing various definitions for
// constructs that will behave in more "interesting" ways when built as C++.
//
// In order to convince you of Needful's completely non-invasive and light
// nature in C builds, it's written with all the C #defines up front in this
// one file--you can see just how trivial they are.  Adding it to a C project
// is a very low-impact proposition... you only have to add *one* file to
// your project to get it to compile with Needful.
//
// The C++ definitions are optionally included at the end of the file.  They
// will #undef the simple definitions, #define them as more powerful ones.
// This is what gives the powerful compile-time checks.  You can enlist in
// the Needful project separately in your continuous integration or whatever
// build system you have, and the extra files will only be included when you
// ask to build with the extra features enabled.
//
//=//// SERIOUSLY, FOLKS... ///////////////////////////////////////////////=//
//
// *It's just one file*, with insanely powerful validation for your plain C
// codebase to get for cheap-as-free, with no extra tools needed but the
// compiler you already have.  What have you got to lose?  :-)
//


#ifndef NEEDFUL_H  // "include guard" allows multiple #includes
#define NEEDFUL_H


//=//// Option(T): EXPLICITLY DISENGAGE-ABLE TYPE /////////////////////////=//
//
// Option() provides targeted functionality in the vein of Rust's `Option`
// and C++'s `std::optional`:
//
//     Option(char*) abc = "abc";
//     Option(char*) xxx = none;  // nullptr also legal for pointer types
//
//     if (abc)
//        printf("abc is truthy, so `unwrap abc` is safe!\n")
//
//     if (xxx)
//        printf("XXX is falsey, so don't `unwrap xxx`...\n")
//
//     char* s1 = abc;                  // **compile time error
//     Option(char*) s2 = abc;          // legal
//
//     char* s3 = unwrap xxx;           // **runtime error
//     char* s4 = maybe xxx;            // gets nullptr out
//
// Option() leverages the natural boolean coercibility of the contained type.
// Hence you can use it with things like pointers, integers or enums where 0
// can be coerced to a "non-valued" state.
//
// While a no-op in C, in C++ builds a wrapper class can give compile-time
// enforcement that you don't pass an Option() to a function that expects the
// wrapped type without first unwrapping it.  You can also choose to have a
// runtime check that `unwrap` never happens on a zero-containing Option().
//

#define NeedfulOption(T)  T

#define needful_none  0  // polymorphic for all legal T for Option(T)

#define needful_unwrap
#define needful_maybe


//=//// Result(T): MULTIPLEXED ERROR AND RETURN RESULT ////////////////////=//
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
//    a "PermissiveZeroStruct" object to more precisely capture the intent.)
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
//        void Needful_Assert_Not_Failing()  // avoids assert() dependency
//        void Needful_Terminate_On_Bad_Result(...)  // variadic
//
//    These can be functions or macros with the same signature.  They should
//    use thread-local state if they're to work in multi-threaded code.
//

#define NeedfulResult(T)  T

#define NEEDFUL_PERMISSIVE_ZERO  0  // likely must disable warnings in C [A]

#define NEEDFUL_NOOP  ((void)0)

#define needful_fail(...) \
    (assert((! Needful_Get_Failure()) && (! Needful_Get_Divergence())), \
    Needful_Set_Failure(__VA_ARGS__), \
    NEEDFUL_PERMISSIVE_ZERO)

#define needful_panic(...) do { \
    assert((! Needful_Get_Failure()) && (! Needful_Get_Divergence())); \
    Needful_Set_Failure(__VA_ARGS__); \
    Needful_Force_Divergent(); \
    return NEEDFUL_PERMISSIVE_ZERO; \
} while (0)

#define needful_trap_core(expr, prefix_extractor) \
    /* var = */ (Needful_Assert_Not_Failing(), prefix_extractor(expr)); \
    if (Needful_Get_Failure()) { \
        /* possibly(Needful_Get_Divergence()); */ \
        return NEEDFUL_PERMISSIVE_ZERO; \
    } NEEDFUL_NOOP  /* force require semicolon at callsite */

#define needful_require_core(expr, prefix_extractor) \
    /* var = */ (Needful_Assert_Not_Failing(), prefix_extractor(expr)); \
    if (Needful_Get_Failure()) { \
        /* possibly(Needful_Get_Divergence()); */ \
        Needful_Force_Divergent(); \
        return NEEDFUL_PERMISSIVE_ZERO; \
    } NEEDFUL_NOOP  /* force require semicolon at callsite */

#define needful_expect_core(expr, prefix_extractor, ...) \
    /* var = */ (Needful_Assert_Not_Failing(), prefix_extractor(expr)); \
    if (Needful_Get_Failure()) { \
        /* possibly(Needful_Get_Divergence()); */ \
        Needful_Terminate_On_Bad_Result(__VA_ARGS__); \
    } NEEDFUL_NOOP  /* force require semicolon at callsite */

#define needful_guarantee_core(expr, prefix_extractor) \
    /* var = */ (Needful_Assert_Not_Failing(), prefix_extractor(expr)); \
    Needful_Assert_Not_Failing();

#define needful_except_core(decl,postfix_extractor) \
    /* expression */ postfix_extractor; \
    if (Needful_Get_Divergence()) \
        { return NEEDFUL_PERMISSIVE_ZERO; } \
    for (decl = Needful_Get_Failure(); Needful_Test_And_Clear_Failure(); )
        /* implicitly takes code block after macro as except()-body */

#define needful_rescue_core(expr,postfix_extractor) \
    expr postfix_extractor needful_rescue_then_internal /* (decl) {body} */

#define /* rescue (expr) */ needful_rescue_then_internal(decl) /* {body} */ \
    ; /* semicolon required */ \
    /* possibly(Needful_Get_Divergence()); */ \
    for (decl = Needful_Get_Failure(); Needful_Test_And_Clear_Failure(); )
        /* { ...implicit code block after macro is except()-body ... } */

//=//// "Hot Potato" Versions ////////////////////////////////////////////=//

#define Needful_Prefix_Extract_Hot(expr)  (expr)
#define Needful_Postfix_Extract_Hot

#define needful_trap(expr) \
    needful_trap_core(expr, Needful_Prefix_Extract_Hot)

#define needful_require(expr) \
    needful_require_core(expr, Needful_Prefix_Extract_Hot)

#define needful_expect(expr,...) \
    needful_expect_core(expr, Needful_Prefix_Extract_Hot, __VA_ARGS__)

#define needful_guarantee(expr) \
    needful_guarantee_core(expr, Needful_Prefix_Extract_Hot)

#define /* expr */ needful_except(decl) /* {body} */ \
    needful_except_core(decl, Needful_Postfix_Extract_Hot)

#define needful_rescue(expr) /* (decl) {body} */ \
    needful_rescue_core(expr, Needful_Postfix_Extract_Hot)

//=//// Discarded Result Versions /////////////////////////////////////////=//

#define Needful_Prefix_Discard_Result(expr)  (expr)
#define Needful_Postfix_Discard_Result

#define needful_trapped(expr) \
    needful_trap_core(expr, Needful_Prefix_Discard_Result)

#define needful_required(expr) \
    needful_require_core(expr, Needful_Prefix_Discard_Result)

#define needful_expected(expr,...) \
    needful_expect_core(expr, Needful_Prefix_Discard_Result, __VA_ARGS__)

#define needful_guaranteed(expr) \
    needful_guarantee_core(expr, Needful_Prefix_Discard_Result)

#define /* expr */ needful_excepted(decl) /* {body} */ \
    needful_except_core(decl, Needful_Postfix_Discard_Result)

#define needful_rescued(expr) /* (decl) {body} */ \
    needful_rescue_core(expr, Needful_Postfix_Discard_Result)


//=//// Sink(T): INDICATE FUNCTION OUTPUT PARAMETERS //////////////////////=//
//
// The idea behind a Sink() is to be able to mark on a function's interface
// when a function argument passed by pointer is intended as an output.
// This has benefits of documentation, and can also be given some teeth by
// scrambling the memory that the pointer points at (so long as it isn't an
// "in-out" parameter).
//
// But there's another feature implemented here, which is *covariance* for
// input parameters, and "contravariance" for output parameters.  This only
// matters if you're applying inheritance selectively to datatypes in C++
// builds to add checking to your C codebase.  See the implementation of
// the contravariance in %needful-sinks.h for more details.
//
// 1. Historical note: NeedfulNeed() might seem like a funny name, but the
//    library's name was actually inspired by the `Need` wrapper type, so
//    it falls out that the "scoped" name for it would look a bit strange.
//

#define NeedfulSink(T)  T *
#define NeedfulInit(T)  T *
#define NeedfulNeed(TP)  TP  // Need(TP) inspired Needful's name [1]


//=//// ensure(T,expr): CHEAP COMPILE-TIME MACRO TYPE ASSURANCE ///////////=//
//
// Macros do not have type checking, which makes them dangerous.  So it is
// considered better to use inline functions if something is conceptually
// a function.
//
// But while inline functions are promoted as being "zero cost" compared to
// an equivalent macro, this isn't always the case.  In debug builds, an
// inline function is typically not inlined--and will slow down the runtime
// in what are potentially very hot paths.
//
// There's also a problem in C with const propagation.  A choice must be
// made about whether inline functions will take const or non-const pointers.
// So unlike a macro, it can't "do the right thing" in terms of behaving
// as const for a const input, and mutable for a mutable input.
//
// Thirdly, inline functions don't have the same powers as macros...so if you
// are writing a macro and want to have type checking for some portion of it,
// you have to split your definition so that it's part macro, part inline
// function.  This can lead code to being less clear.
//
// ensure() is a simple tool whose C++ override addresses all three points:
//
//      int* ptr = ...;
//      void *p = ensure(int*, ptr);  // succeeds at compile-time
//
//      char* ptr = ...;
//      void *p = ensure(int*, ptr);   // fails at compile-time
//
// It does not cost anything at runtime--even in debug builds--because it
// doesn't rely on a function template in the C++ override.
//

#define ensure(T,expr)  (expr)


//=//// CONST PROPAGATION TOOLS ///////////////////////////////////////////=//
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
//    output results from the input's constness, a trick is to use `c_cast()`
//    which is a "const-preserving cast".
//
//    #define Get_Member_As_Foo(ptr)  c_cast(Foo*, Get_Member(ptr))
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

#define CONST_IF_C(param_type) \
    const param_type  // Note: use c_cast() macros instead, if you can [1]

#define MUTABLE_IF_C(return_type, ...) \
    __VA_ARGS__ return_type  // __VA_ARGS__ needed for INLINE etc. [2]

#define CONSTABLE(param_type)  param_type  // use m_cast() on assignment


//=//// cast() VARIATIONS: VISIBLE (AND HOOKABLE!) SEMANATIC CASTS ////////=//
//
// These macros for casting provide *easier-to-spot* variants of parentheses
// cast (so you can see where the casts are in otherwise-parenthesized
// expressions).  They also help document at the callsite what the semantic
// purpose of the cast is.
//
// The definitions in C are trivial--they just act like a parenthesized cast.
// But there are enhanced features when you build as C++11 or higher, where
// the casts can implement their narrower policies and validation logic.
//
// Also, the casts are designed to be "hookable" so that customized checks
// can be added in C++ builds.  These can be compile-time checks (to limit
// what types can be cast to what)...as well as runtime checks in your debug
// builds, that can actually validate that the data being cast is legal for
// the target type.  This even works for casts of raw pointers to types!
//
// All casts are designed to:
//
// * Be visually distinct from parentheses casts
// * Document intent at the callsite
// * Provide compile-time checks in C++ builds
// * Have zero overhead in release builds
//
//=//// CAST SELECTION GUIDE ///////////////////////////////////////////////=//
//
//        PRO-TIP: #define cast() as h_cast() in your codebase!!! [1]
//
// SAFETY LEVEL
//    * Hookable cast:            h_cast()    // safe default, runs hooks
//    * Unhooked/unchecked cast:  u_cast()    // use with fresh malloc()s
//                                               // ...or critical debug paths
//                                               // ...!!! or va_lists !!! [2]
//
// POINTER CONSTNESS
//    * Adding mutability:         w_cast()    // const T* => T*
//    * Type AND mutability:       m_cast()    // const T1* => T2*
//    * Preserving constness:      c_cast()    // T1* => T2* ...or...
//                                               // const T1* => const T2*
//    * Unhookable c_cast():     u_cast()    // c_cast() w/no h_cast() hooks
//
// TYPE CONVERSIONS
//    * Non-pointer to pointer:    p_cast()    // intptr_t => T*
//    * Non-integral to integral:  i_cast()    // T* => intptr_t
//    * Function to function:      f_cast()    // ret1(*)(...) => ret2(*)(...)
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// 1. Because `cast` has a fair likelihood of being defined as the name of a
//    function or variable in C codebases, Needful does not force a definition
//    of `cast`.  But in an ideal situation, you could adapt your codebase
//    such that cast() can be defined, and defined as h_cast().
//
//    It's also potentially the case that you might want to start it out as
//    meaning u_cast()...especially if gradually adding Needful to existing
//    code.  You could start by turning all the old (T)(V) casts into cast()
//    defined as u_cast()...and redefine it as h_cast() after a process over
//    the span of time, having figured out which needed to be other casts.
//
// 2. The va_list type is compiler magic, and the standard doesn't even
//    guarantee you can pass a `va_list*` through a `void*` and cast it back
//    to `va_list*`!  But in practice, that works on most platforms—**as long
//    as you are only passing the `va_list` object by address and not copying
//    or dereferencing it in a way that violates its ABI requirements**.
//
//    But since `const va_list*` MAY be illegal, and va_list COULD be any type
//    (including fundamentals like `char`), the generic machinery behind
//    cast() could be screwed up if you ever use va_list* with it.  We warn
//    you to use u_cast() if possible--but it's not always possible, since
//    it might look like a completely mundane type.  :-(
//
// 3. <write note>

#define u_cast(T,expr)    ((T)(expr))
#define h_cast(T,expr)    ((T)(expr))

#define c_cast(T,expr)    ((T)(expr))
#define m_cast(T,expr)    ((T)(expr))
#define w_cast(T,expr)    ((T)(expr))

#define s_cast(bytes)   u_cast(char*, ensure(const unsigned char*, (bytes)))
#define b_cast(chars)   u_cast(unsigned char*, ensure(const char*, (chars)))

#define p_cast(T,expr)    ((T)(expr))
#define i_cast(T,expr)    ((T)(expr))
#define f_cast(T,expr)    ((T)(expr))

// Strict cast alternatives because GCC doesn't follow the C++ standard [3]

#define strict_u_cast(T,expr)    u_cast(T,(expr))
#define strict_h_cast(T,expr)    h_cast(T,(expr))

#define strict_c_cast(T,expr)    c_cast(T,(expr))
#define strict_u_cast(T,expr)  u_cast(T,(expr))

#define strict_cast(T,expr)      cast(T,(expr))  // however you define cast()

#define downcast(T,expr)    ((T)(expr))
#define upcast(expr)        ((void*)(expr))


//=//// attempt, until, whilst: ENHANCED LOOP MACROS //////////////////////=//
//
// This is a fun trick that brings a little bit of the ATTEMPT and UNTIL loop
// functionality from Ren-C into C.
//
// The `attempt` macro is a loop that runs its body just once, and then
// evaluates the `then` or `else` clause (if present):
//
//     attempt {
//         ... some code ...
//         if (condition) { break; }  /* exit attempt, run "else" clause */
//         if (condition) { continue; }  /* exit attempt, run "then" clause */
//         if (condition) { again; }  /* jump to attempt and run it again */
//         ... more code ...
//     }
//     then {  /* optional then clause */
//        ... code to run if no break happened ...
//     }
//     else {  /* optional else clause (must have then clause to use else) */
//        ... code to run if a break happened ...
//     }
//
// It doesn't do anything you couldn't do with defining some goto labels.
// But if you have B breaks and C continues and A agains, you don't have to
// type the label names ((B + 1) + (C + 1) + (A + 1)) times.  And you don't
// have to worry about coming up with the names for those labels!
//
// Since `while` is taken, the corresponding enhanced version of while that
// supports `then` and `else` clauses is called `whilst`.  But for a better
// name, the `until` macro is a negated sense of the whilst loop.
//
// 1. Since the macros define variables tracking whether the `then` clause
//    should run or not, and whether an `again` should signal continuing to
//    run...these loops can only be used in one scope at a time.  To use more
//    than once in a function, define another scope.
//
// 2. Due to limits of the trick, you can't use an `else` clause without at
//    least a minimal `then {}` clause.
//

#define needful_attempt /* {body} */ \
    bool run_then_ = false;  /* as long as run_then_ is false, keep going */ \
    bool run_again_ = false;  /* if run_again_, don't set run_then_ */ \
    for (; (! run_then_); \
        run_again_ ? (run_again_ = false), true  /* again keeps looping */ \
        : (run_then_ = true))  /* continue exits the attempt "loop" */

#define needful_until(condition) \
    bool run_then_ = false; \
    bool run_again_ = false; \
    for (; run_again_ ? (run_again_ = false), true :  /* skip condition */ \
        (condition) ? (run_then_ = true, false) : true; )

#define needful_whilst(condition) /* shorthand can't be `while` [1] */ \
    bool run_then_ = false; \
    bool run_again_ = false; \
    for (; run_again_ ? (run_again_ = false), true :  /* skip condition */ \
        (! condition) ? (run_then_ = true, false) : true; )

#define needful_then /* {branch} */ \
    if (run_then_)

#define needful_again \
    { run_again_ = true; continue; }


//=//// ZERO TYPE (RETURN-ABLE `void` SURROGATE) /////////////////////////=//
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
// So instead of Result(void) we use Result(Zero).  This is like what Rust
// would call the "unit type" (which they write as `()`).
//
// To make it work in C, it's just an enum with a single zero value.  But
// the C++ version is more complex.
//
// 1. The name "Zero" is chosen because "Nothing" is too close to "None"
//    which is being used for the `Option()` type's disengaged state.
//
// 2. There is no way the C++ enhancements can statically assert at
//    compile-time that a Zero is only being constructed from the 0 literal.
//    Because of this, you have to say `return zero` instead of `return 0`
//    when using the Zero type to pass the C++ static analysis.
//

typedef enum {
    NEEDFUL_ZERO_ENUM_0 = 0  // use lowercase for the constant [1]
} NeedfulZeroEnum;

#define NeedfulZero  NeedfulZeroEnum  // rename to Zero [1]
#define needful_zero  NEEDFUL_ZERO_ENUM_0  // use instead of 0 literal [2]


//=//// NEEDFUL_DOES_CORRUPTIONS + CORRUPTION SEED/DOSE ///////////////////=//
//
// See Corrupt_If_Needful() for more information.
//
// 1. We do not do Corrupt_If_Needful() with static analysis, because tha
//    makes variables look like they've been assigned to the static analyzer.
//    It should use its own notion of when things are "garbage" (e.g. this
//    allows reporting of use of unassigned values from inline functions.)
//
// 2. Generate some variability, but still deterministic.
//

#if !defined(NEEDFUL_DOES_CORRUPTIONS)
   #define NEEDFUL_DOES_CORRUPTIONS  0
#endif

#if (! NEEDFUL_DOES_CORRUPTIONS)
    #define Corrupt_If_Needful(x)  NEEDFUL_NOOP
#else
    STATIC_ASSERT(! DEBUG_STATIC_ANALYZING);  // [1]

    #include <cstring>  // for memset

    #define Corrupt_If_Needful(x) \
        memset(&(x), 0xBD, sizeof(x))  // C99 fallback mechanism
#endif

#define NEEDFUL_USES_CORRUPT_HELPER  0


//=//// MARK UNUSED VARIABLES /////////////////////////////////////////////=//
//
// Used in coordination with the `-Wunused-variable` setting of the compiler.
// While a simple cast to void is what people usually use for this purpose,
// there's some potential for side-effects with volatiles:
//
//   http://stackoverflow.com/a/4030983/211160
//
// The tricks suggested there for avoiding it seem to still trigger warnings
// as compilers get new ones, so assume that won't be an issue.  As an
// added check, this gives the UNUSED() macro "teeth" in C++11:
//
//   http://codereview.stackexchange.com/q/159439
//

#define NEEDFUL_USED(expr)    ((void)(expr))
#define NEEDFUL_UNUSED(expr)  ((void)(expr))


//=//// STATIC_ASSERT, STATIC_IGNORE, STATIC_FAIL /////////////////////////=//
//
// 1. We don't do a "poor man's" static assert in C, but make it a no-op.
//    The reason is that there are too many limitations of a C shim:
//
//      http://stackoverflow.com/questions/3385515/static-assert-in-c
//
//    So trust the C++ overloaded definition to enforce the asserts when you
//    build as C++.
//

#define NEEDFUL_STATIC_IGNORE(expr) /* uses trick for callsite semicolons */ \
    struct GlobalScopeNoopTrick  // https://stackoverflow.com/q/53923706

#define NEEDFUL_STATIC_ASSERT(cond) \
    STATIC_IGNORE(cond)  // C version is noop [2]

#define NEEDFUL_STATIC_FAIL(msg) \
    typedef int static_fail_##msg[-1]  // message has to be a C identifier


//=//// STATIC ASSERT LVALUE TO HELP EVIL MACRO USAGE /////////////////////=//
//
// Macros are generally bad, but especially bad if they use their arguments
// more than once...because if that argument has a side-effect, they will
// have that side effect more than once.
//
// However, checked builds will not inline functions.  Some code is run so
// often that not defining it in a macro leads to excessive cost in these
// checked builds, and "evil macros" which repeat arguments are a pragmatic
// solution to factoring code in these cases.  You just have to be careful
// to call them with simple references.
//
// Rather than need to give mean-sounding names like XXX_EVIL_MACRO() to
// these macros (which doesn't have any enforcement), this lets the C++
// build ensure the argument is assignable (an lvalue).
//

#define ENSURE_LVALUE(variable)  (*&variable)

#define STATIC_ASSERT_LVALUE(x)  NEEDFUL_NOOP


//=//// NO-OP STATIC_ASSERTS THAT VALIDATE EXPRESSIONS ////////////////////=//
//
// These are utilized by the commentary macros.  They are no-ops in C, but
// the C++ overrides can help keep comments current by ensuring the
// expressions they take will compile (hence variables named by them are
// valid, etc.)
//

#define NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL(expr)  NEEDFUL_NOOP

#define NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID(expr) NEEDFUL_NOOP


//=//// NODISCARD shim ////////////////////////////////////////////////////=//
//
// NODISCARD is a C++17 feature, but some compilers offer it as something
// you can use in C code as well.  It's useful enough in terms of warning
// of unused results that a macro is offered to generalize it.
//
// (The pre-[[nodiscard]] hacks only work on functions, not types.  They
// can be applied to structs, but will have no effect.  [[nodiscard]] from
// C++17 works on types as well as functions, so it is more powerful.)
//

#if defined(__GNUC__) || defined(__clang__)
    #define NEEDFUL_NODISCARD  __attribute__((warn_unused_result))
#elif defined(_MSC_VER)
    #define NEEDFUL_NODISCARD _Check_return_
#else
    #define NEEDFUL_NODISCARD  // C++ overloads may redefine to [[nodiscard]]
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  OPTIONAL SHORHANDS FOR THE `NEEDFUL_XXX` MACROS AS JUST `XXX` MACROS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// If you use these words for things like variable names, you will have
// problems if they are defined as macros.  You can pick your own terms but
// these are the ones that were used in Needful's original client.
//
// 1. A quick and dirty way to write `return failed;` and not have to come
//    up with an error might be useful in some codebases.  We don't try to
//    define that here, because it's open ended as to what you'd use for
//    your error value type.
//
// 2. It should be very rare for code to capture divergent errors.  This
//    aligns with a Ren-C convention of making the divergent error handler
//    look "less casual" by hiding it in the system.utilities namespace, vs.
//    lookin more like a "keyword".
//
// 3. The idea beind shorthands like `possibly()` is to replace comments that
//    are carrying information about something that *might* be true:
//
//        int i = Get_Integer(...);  // i may be < 0
//
//    Even the C no-op version of `possibly()` lets you break it out so the
//    visual flow is better, and less likely to overflow a line:
//
//        int i = Get_Integer(...);
//        possibly(i < 0);
//
//    But the C++ overload of STATIC_ASSERT_DECLTYPE_BOOL() allows it to
//    make sure your expression is well-formed at compile-time.  It still
//    does nothing at run-time, but causes a compile-time error if the
//    variable is renamed and not updated, etc.  `impossible()` is a similar
//    trick, but for documenting things that are invariants, but that aren't
//    worth paying for a runtime assert.
//
//    `unnecessary()` and `dont()` aren't boolean-constrained, and help
//    document lines of code that are not needed (or would actively break
//    things), while ensuring the expressions are up-to-date as valid for
//    the compiler.  `heeded()` marks things that look stray or like they
//    would have effect, but their side-effect is intentional (perhaps only
//    in debug builds, that check for what they did).

#if !defined(NEEDFUL_DONT_DEFINE_CAST_SHORTHANDS)
    #define cast(T,expr)            h_cast(T,expr)
#endif

#if !defined(NEEDFUL_DONT_DEFINE_OPTION_SHORTHANDS)
    #define Option(T)               NeedfulOption(T)
    #define none                    needful_none

    #define unwrap                  needful_unwrap
    #define maybe                   needful_maybe
#endif

#if !defined(NEEDFUL_DONT_DEFINE_RESULT_SHORTHANDS)
    #define Result(T)               NeedfulResult(T)

    #define fail(p)                 needful_fail(p)
    /* #define failed               needful_fail("generic failure");  [1] */
    #define panic(p)                needful_panic(p)

    #define trap(expr)              needful_trap(expr)
    #define require(expr)           needful_require(expr)
    #define expect(decl)            needful_expect(decl)
    #define guarantee(expr)         needful_guarantee(expr)
    #define except(decl)            needful_except(decl)
    #define sys_util_rescue(expr)   needful_rescue(expr)  // stigmatize [2]

    #define trapped(expr)           needful_trapped(expr)
    #define required(expr)          needful_required(expr)
    #define expected(decl)          needful_expected(decl)
    #define guaranteed(expr)        needful_guaranteed(expr)
    #define excepted(decl)          needful_excepted(decl)
    #define sys_util_rescued(expr)  needful_rescued(expr)  // stigmatize [2]
#endif

#if !defined(NEEDFUL_DONT_DEFINE_SINK_SHORTHANDS)
    #define Sink(T)                 NeedfulSink(T)
    #define Init(T)                 NeedfulInit(T)
    #define Need(TP)                NeedfulNeed(TP)
#endif

#if !defined(NEEDFUL_DONT_DEFINE_LOOP_SHORTHANDS)
    #define attempt                 needful_attempt
    #define until(condition)        needful_until(condition)
    #define whilst(condition)       needful_whilst(condition)
    #define then                    needful_then
    #define again                   needful_again
#endif

#if !defined(NEEDFUL_DONT_DEFINE_ZERO_SHORTHANDS)
    #define Zero                    NeedfulZero
    #define zero                    needful_zero
#endif

#if !defined(NEEDFUL_DONT_DEFINE_COMMENT_SHORTHANDS)  // informative! [3]
    #define possibly(expr)        NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL(expr)
    #define impossible(expr)      NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL(expr)

    #define unnecessary(expr)     NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID(expr)
    #define dont(expr)            NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID(expr)
    #define heeded(expr)          (expr)

    // Uppercase versions, for use in global scope (more limited abilities)

    #define POSSIBLY(expr)        NEEDFUL_STATIC_IGNORE(expr)
    #define IMPOSSIBLE(expr)      NEEDFUL_STATIC_ASSERT(! (expr))

    #define UNNECESSARY(expr)     NEEDFUL_STATIC_IGNORE(expr)
    #define DONT(expr)            NEEDFUL_STATIC_IGNORE(expr)
    #define HEEDED(expr)          NEEDFUL_STATIC_IGNORE(expr)
#endif

#if !defined(NEEDFUL_DONT_DEFINE_STATIC_ASSERT_SHORTHANDS)
    #if !defined(STATIC_ASSERT)
        #define STATIC_ASSERT(expr)  NEEDFUL_STATIC_ASSERT(expr)
    #endif

    #if !defined(STATIC_IGNORE)
        #define STATIC_IGNORE(expr)  NEEDFUL_STATIC_IGNORE(expr)
    #endif

    #if !defined(STATIC_FAIL)
        #define STATIC_FAIL(msg)  NEEDFUL_STATIC_FAIL(msg)
    #endif
#endif

#if !defined(NEEDFUL_DONT_DEFINE_USAGE_SHORTHANDS)
  #if !defined(USED)
    #define USED(expr)  NEEDFUL_USED(expr)
  #endif

  #if !defined(UNUSED)
    #define UNUSED(expr)  NEEDFUL_UNUSED(expr)
  #endif

  #if !defined(NOOP)
    #define NOOP  NEEDFUL_NOOP
  #endif

  #if !defined(NODISCARD)
    #define NODISCARD  NEEDFUL_NODISCARD
  #endif
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  OPTIONAL C++ OVERRIDES FOR ABOVE DEFINITIONS TO BRING THE MACROS TO LIFE
//
//=////////////////////////////////////////////////////////////////////////=//
//
// needful.h is written out with all the "noop" C definitions first, to help
// give a clear sense to people how trivial and non-invasive the library can
// be for C programs--introducing no dependencies or complexity.  There's
// non-zero value to the documentation that these definitions provide, even
// with minimal behavior (and the Result(T) handling still has runtime
// benefits).
//
// But the *REAL* power of Needful comes from C++ builds, performing powerful
// compile-time checks (and optional runtime validations).
//
// When these headers are activated, they will #undef the simple definitions
// given above, and redefine them with actual machinery to give them teeth!
//

#if defined(__cplusplus)
    #include "cplusplus/cplusplus-needfuls.hpp"
#endif


//=//// TEMPORARY: NEEDFUL TESTS //////////////////////////////////////////=//
//
// This is a temporary measure to allow the tests to be run without having
// a separate build step to do so, just to start getting the tests written.
//

#if defined(__cplusplus) && !defined(NDEBUG)
    #include "tests/all-needful-tests.hpp"
#endif

#endif  // !defined(NEEDFUL_H)
