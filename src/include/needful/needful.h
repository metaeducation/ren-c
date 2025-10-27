/*
**  file: %needful.h
**  summary: "C macros that offer powerful features in C++11 or higher"
**  homepage: <needful homepage TBD>
**
******************************************************************************
**
** Copyright 2015-2025 hostilefork.com
**
** Licensed under the MIT License
**
** https://en.wikipedia.org/wiki/MIT_License
**
******************************************************************************
**
** Needful is a header-only library, containing various definitions for C
** constructs that can behave in more "interesting" ways when built as C++.
**
** To help convince you of Needful's "light" nature in C builds, it's written
** with all the C #defines up front in this one file--you can see how simple
** they (mostly) are.  Adding it to a C project is a low-impact proposition...
** you only have to add *one* file to your project, and it can be built in
** either C or C++ mode, standalone.
**
** But if you `#define NEEDFUL_CPP_ENHANCEMENTS 1` (and have the additional
** files implementing the enhancements in Needful's directory) then they'll be
** included at the end of the file.  This will #undef the simple definitions,
** and #define them as more complex ones.  This is what gives the powerful
** compile-time checks.  You can enlist in the Needful project separately in
** your continuous integration or whatever build system you have, and the extra
** files will only be included when you ask to build with the extra features.
**
** It helps document your code even if you just use the one `needful.h` file.
** But when you do build with the C++ enhancements, you get powerful checks,
** with no extra tools needed but the compiler you already have.
**
****[[ NOTES ]]***************************************************************
**
** A. When built in C language mode, the Result() and Option() types rely on
**    the polymorphic construction from 0 for enums, pointers and integers.
**    Though legal, GCC and Clang give warnings about such conversions if they
**    produce a pointer -AND- come from a compound expression:
**
**     SomeEnum Make_Enum(...) { return 0; }  // no warning
**     Something* Make_Pointer(...) { return 0; }  // no warning
**
**     SomeEnum Make_Enum(...) { return (..., 0); }  // no warning
**     Something* Make_Pointer(...) { return (..., 0); }  // GCC + Clang warn
**
**   If you want to use Result(Something*) in C builds, `return fail (...);`
**   requires the compound construction from 0, and Option(Something*) may
**   need this if you conditionally produce `none`.  Needful disables the
**   warning for convenience, so you don't need `-Wno-int-conversion`.  But
**   don't worry: the C++ enhanced build will catch any mistakes.
**
**   (C++ is more strict than C about what can be converted from 0, and would
**   have errors--not warnings.  Tools like `nocast` are used to work around
**   this if just using the skeletal file, with more targeted behavior
**   in the enhanced definitions.)
*/

#ifndef NEEDFUL_H_INCLUDED  /* "include guard" allows multiple #includes */
#define NEEDFUL_H_INCLUDED

#if !defined(NEEDFUL_DISABLE_INT_WARNING) || NEEDFUL_DISABLE_INT_WARNING
  #if !defined(__cplusplus) && (defined(__GNUC__) || defined(__clang__))
    #pragma GCC diagnostic ignored "-Wint-conversion"  /* See [A] above */
  #endif
#endif


/****[[ nocast: LEAST-ANNOYING-TO-C-PROGRAMMERS CAST OF malloc() etc. ]]******
**
** If you wish to build C codebases as C++, crucial incompatibilities exist
** because C allows void* to be assigned to any pointer type:
**
**     SomeType* ptr = malloc(sizeof(SomeType));  // works in C, not in C++
**
** Codebases that want compatibility between C and C++ wind up having to
** repeat the type on both sides, or invent wrappers around malloc:
**
**     SomeType* ptr = (SomeType*)malloc(sizeof(SomeType));  // yuck
**
**     SomeType* ptr = MALLOC_WRAPPER_MACRO(SomeType);  // extra definitions
**
** nocast is a tool for addressing this issue in a clean and generic way:
**
**     SomeType* ptr = nocast malloc(sizeof(SomeType));
**
** In C the macro vanishes.  But in C++ it expands to a proxy cast object and
** an addition operator overload, which enables the desired syntax:
**
**     SomeType* ptr = malloc(sizeof(SomeType));  // C version
**     SomeType* ptr = <helper> + malloc(sizeof(SomeType));  // C++ version
**
** It also allows C++ builds to coerce 0 to enum types, which is useful in
** basic compilability of Option(T) and Result(T):
**
**     SomeEnum e = 0;  // works in C, not in C++
**
**     SomeEnum e = nocast 0;  // works in both C and C++
**
** NOTE: When not using NEEDFUL_CPP_ENHANCEMENTS, compiling needful.h with C++
** can typically just use the C versions of the definitions.  But C++ compilers
** can't accomplish the behavior of nocast without some special mechanics, so
** we have to #ifdef __cplusplus here.
**
** 1. The choice of `+` as the operator to use is intentional due to wanting
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
        class FromNoRef = typename std::remove_reference<From>::type,
        bool ToIsPtr = std::is_pointer<To>::value,
        bool FromIsInt = std::is_integral<FromNoRef>::value,
        bool FromIsVoidPtr =
            std::is_pointer<FromNoRef>::value &&
            std::is_void<typename std::remove_pointer<FromNoRef>::type>::value
    >
    struct NocastConvert {  // enums, same-type casts, etc.
        static To Do_It(From f) { return static_cast<To>(f); }
    };

    // Case 1: (nocast 0) -> T*   (avoid int->ptr cast error; use nullptr)
    template<class To, class From>
    struct NocastConvert<
        To, From,
        typename std::remove_reference<From>::type,
        /*ToIsPtr*/ true, /*FromIsInt*/ true, /*FromIsVoidPtr*/ false
    >{
        static To Do_It(From) { return static_cast<To>(nullptr); }
    };

    // Case 2: (nocast void_ptr) -> T*   (e.g. from malloc)
    template<class To, class From>
    struct NocastConvert<
        To, From,
        typename std::remove_reference<From>::type,
        /*ToIsPtr*/ true, /*FromIsInt*/ false, /*FromIsVoidPtr*/ true
    >{
        static To Do_It(From f) { return static_cast<To>(f); }
    };

    template<class From>
    struct NocastHolder {
        From f;

        template<class To>
        operator To() const {
            return NocastConvert<To, From>::Do_It(f);
        }
    };

    template<class T>  // choice of operator+ is meaningful [1]
    inline NocastHolder<T> operator+(NocastMaker, T v) { return { v }; }

    constexpr NocastMaker g_nocast_maker{};
  }

    #define needful_nocast    needful::g_nocast_maker +
#endif


/***[[ nocast_0: Generically Make 0 Of Any Type ]]****************************
**
** Parts of Needful rely on being able to create a zero of any type T.  For
** primitive types this can be done with template magic.  But if you make any
** C++ classes to instrument your C code that use things like Option(MyType)
** or Result(MyType), those classes must be constructible from generic zero.
**/

#if !defined(__cplusplus)
    #define needful_nocast_0  0  /* may need warning disablement [A] */
#else
  namespace needful {
    struct Nocast0Struct {
        template<class To>
        constexpr operator To() const {
            return NocastConvert<To, int>::Do_It(0);  // literal 0, no member
        }
    };
  }

    #define needful_nocast_0  needful::Nocast0Struct{}
#endif


/***[[ Option(T): EXPLICITLY DISENGAGE-ABLE TYPE ]]***************************
**
** Option() provides targeted functionality in the vein of Rust's `Option`
** and C++'s `std::optional`:
**
**     Option(char*) abc = "abc";
**     Option(char*) xxx = none;  // nullptr is synonym for pointer types
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
**     char* s4 = opt xxx;              // gets nullptr out
**
** It leverages the natural boolean coercibility of the contained type.  So
** you can use it with things like pointers, integers or enums...anywhere the
** C build can treat 0 as a "falsey" state.
**
** While a no-op in C, in C++ builds a wrapper class that gives compile-time
** enforcement that you don't pass an Option() to a function that expects the
** wrapped type without first unwrapping it.  You can also choose to add a
** runtime check that `unwrap` never happens on a zero-containing Option().
*/

#define NeedfulOption(T)  T

#define needful_none  needful_nocast_0  /* unique type if C++ enhanced */

#define needful_unwrap
#define needful_opt


/****[[ SCOPE_GUARD: PROTECT FROM UNSAFE MACRO USAGES ]]**********************
**
** Macros that expand into multiple statements can be dangerous if used in
** a context where a single statement is expected.  This is typically handled
** in C/C++ with a `do { ... } while (0)` construct, but trickier macros may
** not be able to do their work inside a block.
**
** This SCOPE_GUARD is a trick which makes an unused variable with a unique
** name, that will cause a compile error if used in something like the branch
** slot of an `if` statement.
**
** 1. There is an issue in macro expansions that was seen in Clang 15, which
**    makes the scope guard more aggressive than it needs to be (e.g. not
**    allowing use in a switch case statement without braces).
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
** These macros provide a C/C++-compatible mechanism for propagating and
** handling errors in a style similar to Rust's `Result<T, E>`, all without
** requiring exceptions or setjmp/longjmp in C++ builds.
**
** Instead, the multiplexing of an Error with the return value type is done
** with a global (or more generally, thread-local) error state.
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
    /* _stmt_ */ needful_postfix_extract_result; \
    for (_decl_ = Needful_Get_Failure(); Needful_Test_And_Clear_Failure(); )
        /* {body} implicitly picked up after macro by for, decl is scoped */

#define needful_rescue(_expr_) \
    (Needful_Assert_Not_Failing(), _expr_ needful_postfix_extract_result, \
        Needful_Test_And_Clear_Failure())

#if defined(NEEDFUL_DECLARE_RESULT_HOOKS) && NEEDFUL_DECLARE_RESULT_HOOKS

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
   assert(g_needful_failure == (const char*)0)

#endif  /* NEEDFUL_DECLARE_RESULT_HOOKS */


/****[[ Sink(T): INDICATE FUNCTION OUTPUT PARAMETERS ]]***********************
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
**
** 1. Historical note: NeedfulNeed() might seem like a funny name, but the
**    library's name was actually inspired by the `Need` wrapper type, so
**    it falls out that the "scoped" name for it would look a bit strange.
*/

#define NeedfulSink(T)  T *
#define NeedfulInit(T)  T *
#define NeedfulNeed(TP)  TP  /* Need(TP) inspired Needful's name [1] */


/****[[ ensure(T,expr): CHEAP COMPILE-TIME MACRO TYPE ASSURANCE ]]************
**
** Macros do not have type checking, which makes them dangerous.  So it is
** considered better to use inline functions if something is conceptually
** a function.
**
** But while inline functions are promoted as being "zero cost" compared to
** an equivalent macro, this isn't always the case.  In debug builds, an
** inline function is typically not inlined--and will slow down the runtime
** in what are potentially very hot paths.
**
** There's also a problem in C with const propagation.  A choice must be
** made about whether inline functions will take const or non-const pointers.
** So unlike a macro, it can't "do the right thing" in terms of behaving
** as const for a const input, and mutable for a mutable input.
**
** Thirdly, inline functions don't have the same powers as macros...so if you
** are writing a macro and want to have type checking for some portion of it,
** you have to split your definition so that it's part macro, part inline
** function.  This can lead code to being less clear.
**
** ensure() is a simple tool whose C++ override addresses all three points:
**
**      int* ptr = ...;
**      void *p = ensure(int*, ptr);  // succeeds at compile-time
**
**      char* ptr = ...;
**      void *p = ensure(int*, ptr);   // fails at compile-time
**
** It does not cost anything at runtime--even in debug builds--because it
** doesn't rely on a function template in the C++ override.
**
** 1. The Rigid form of Ensure will error if you try to pass a const pointer
**    in when a non-const pointer was specified.  But the lenient form will
**    match as a const pointer, and pass through the const pointer.  This
**    turns out to be more useful in most cases than enforcing mutability,
**    and it also is briefer to read at the callsite.
*/

#define needful_rigid_ensure(T,expr) \
    needful_xtreme_cast(T,expr)

#define needful_lenient_ensure(T,expr) /* const passthru const [1] */ \
    needful_xtreme_cast(T,expr)

#define needful_ensure_any(TLIST,expr)  (expr)  /* doesn't change type */

#define needful_ensure_lvalue(variable)  (*&variable)


/****[[ ENABLEABLE: Argument Type Subsetting ]]*******************************
*/

#define ENABLE_IF_EXACT_ARG_TYPE(...)
#define DISABLE_IF_EXACT_ARG_TYPE(...)
#define ENABLEABLE(T, name) T name


/****[[ cast(): VISIBLE (AND HOOKABLE!) ERGONOMIC CASTS ]]********************
**
** These macros for casting provide *easier-to-spot* variants of parentheses
** cast (so you can see where the casts are in otherwise-parenthesized
** expressions).  They also help document at the callsite what the semantic
** purpose of the cast is.
**
** The definitions in C are trivial--they just act like a parenthesized cast.
** But there are enhanced features when you build as C++11 or higher, where
** the casts can implement their narrower policies and validation logic.
** In release builds, the casts have zero overhead.
**
** Also, the casts are designed to be "hookable" so that customized checks
** can be added in C++ builds.  These can be compile-time checks (to limit
** what types can be cast to what)...as well as runtime checks in your debug
** builds, that can actually validate that the data being cast is legal for
** the target type.  This even works for casts of raw pointers to types!
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

#define needful_lenient_unhookable_cast(T,expr)     ((T)(expr))  /* [2] */

#define needful_rigid_hookable_cast(T,expr)         ((T)(expr))  /* [3] */

#define needful_rigid_unhookable_cast(T,expr)       ((T)(expr))


/****[[ m_cast(): MUTABILITY CASTS ]]*****************************************
*/

#define needful_mutable_cast(T,expr)                ((T)(expr))


/****[[ p_cast(), i_cast(), f_cast(): NARROWED CASTS ]]***********************
**
** Casts like those that turn pointers into integers are weird.  It would
** create complexity to make the plain cast() macro handle them... but also,
** the C++ build would be forced to do any reinterpret_cast<> through a
** function template that can't be constexpr.  Optimizers would inline it,
** but it would still be a function call in debug builds.
**
** It makes the source more communicative for these weird casts to stand out
** anyway.  So these narrowed casts are provided.
*/

#define needful_pointer_cast(T,expr)    ((T)(expr))

#define needful_integral_cast(T,expr)   ((T)(expr))

#define needful_function_cast(T,expr)   ((T)(expr))


/****[[ v_cast(): VA_LIST CAST ]]*********************************************
**
** The va_list type is compiler magic (const va_list* may be a compiler
** error) and the standard doesn't even guarantee you can pass a `va_list*`
** through a `void*` and cast it back to `va_list*`!  But in practice, that
** works on most platforms, as long as you are only passing the `va_list`
** object by address and not copying or dereferencing it in a way that
** violates its ABI requirements.
**
** If possible, the C++ build will stop you from using other casts on a
** va_list* and force you to use this narrow cast that only allows mutable
** va_list* to be cast to void*, and vice-versa.  If you don't want to
** include <stdarg.h> to do the checks, see NEEDFUL_DONT_INCLUDE_STDARG_H.
*/

#define needful_valist_cast(T,expr)     ((T)(expr))


/****[[ downcast: "INHERITANCE" CASTING ]]************************************
**
** One of Needful's features is to facilitate using type hiearchies in C.
** You can frame your data types as having inheritance in the sense that
** derived types can be passed to functions taking base types but not vice
** versa.  C++ checks it, but C does not.
**
** In order to build downcasts in a C++ compiler without including the C++
** enhancements, a small amount of permissive C++ code is needed...which is
** the same code used for nocast.  This doesn't check that you are correctly
** casting down the hierarchy, but won't compile without at least nocast.
*/

#if !defined(__cplusplus)
    #define needful_hookable_downcast  (void*)
    #define needful_unhookable_downcast  (void*)
#else
    #define needful_hookable_downcast  needful_nocast
    #define needful_unhookable_downcast  needful_nocast
#endif


/****[[ x_cast(): "WHAT PARENTHESES-CAST WOULD DO" ]]*************************
**
** The parentheses-cast is the only cast in C, so it is maximally permissive.
** In C++, it defaults to giving warnings if you cast away constness...but
** any standards-compliant compiler must let you disable that warning.
**
** If you are in a situation where the Needful casts are not working for you,
** the "xtreme" cast is a way to fall back on the C cast while still being
** more visible as a cast in a code than parentheses.
**
** 1. The choice of the name "xtreme" is because `x_cast()` seems like the
**    right name for it, whereas `c_cast()` would suggest something with
**    constness (if m_cast() is for mutability).
*/

#define needful_xtreme_cast(T,...) \
    ((T)(__VA_ARGS__))


/****[[ upcast: CAST SAFELY UPWARD ]]*****************************************
**
** upcast() is useful when defining macros, where you expect what you're
** doing to be safe.  The default definition doesn't enforce that it's an
** upcast, but the C++ enhancement will do that enforcement.
*/

#define needful_upcast /* (T,expr) */  needful_xtreme_cast


/****[[ CAST SELECTION GUIDE ]]***********************************************
**
**        PRO-TIP: #define cast() as h_cast() in your codebase!!! [1]
**
** SAFETY LEVEL
**    - Hookable cast:            h_cast()    // safe default w/debug hooks
**    - Unhooked/unchecked cast:  u_cast()    // use with fresh malloc()s
**                                               // ...or critical debug paths
**
** POINTER CONSTNESS
**    - Adding mutability:         m_cast()    // const T* => T*
**    - Preserve mutability:       h_cast()    // TA* => TB* ...or...
**                               & u_cast()      // const TA* => const TB*
**
** TYPE CONVERSIONS
**    - Non-pointer to pointer:    p_cast()    // intptr_t => T*
**    - Non-integral to integral:  i_cast()    // T* => intptr_t
**    - Function to function:      f_cast()    // ret1(*)(...) => ret2(*)(...)
**    - va_list to void*:          v_cast()    // va_list* <=> void*
**
*****[[ NOTES ]]**************************************************************
**
** 1. Because `cast` has a fair likelihood of being defined as the name of a
**    function or variable in C codebases, Needful does not force a definition
**    of `cast`.  But in an ideal situation, you could adapt your codebase
**    such that cast() can be defined, and defined as h_cast().
**
**    It's also potentially the case that you might want to start it out as
**    meaning u_cast()...especially if gradually adding Needful to existing
**    code.  You could start by turning all the old (T)(V) casts into cast()
**    defined as u_cast()...and redefine it as h_cast() after a process over
**    the span of time, having figured out which needed to be other casts.
*/

/***[[ attempt, until, whilst: ENHANCED LOOP MACROS ]]************************
**
** This is a fun trick that brings a little bit of the ATTEMPT and UNTIL loop
** functionality from Ren-C into C.
**
** The `attempt` macro is a loop that runs its body just once, and then
** evaluates the `then` or `else` clause (if present):
**
**     attempt {
**         ... some code ...
**         if (condition) { break; }  // exit attempt, run "else" clause
**         if (condition) { continue; }  // exit attempt, run "then" clause
**         if (condition) { again; }  // jump to attempt and run it again
**         ... more code ...
**     }
**     then {  // optional then clause
**        ... code to run if no break happened ...
**     }
**     else {  // optional else clause (must have then clause to use else)
**        ... code to run if a break happened ...
**     }
**
** It doesn't do anything you couldn't do with defining some goto labels.
** But if you have B breaks and C continues and A agains, you don't have to
** type the label names ((B + 1) + (C + 1) + (A + 1)) times.  And you don't
** have to worry about coming up with the names for those labels!
**
** Since `while` is taken, the corresponding enhanced version of while that
** supports `then` and `else` clauses is called `whilst`.  But for a better
** name, the `until` macro is a negated sense of the whilst loop.
**
** 1. Since the macros define variables tracking whether the `then` clause
**    should run or not, and whether an `again` should signal continuing to
**    run...these loops can only be used in one scope at a time.  To use more
**    than once in a function, define another scope.
**
** 2. Due to limits of the trick, you can't use an `else` clause without at
**    least a minimal `then {}` clause.
*/

#define needful_attempt /* {body} */ \
    bool run_then_ = false;  /* as long as run_then_ is false, keep going */ \
    bool run_again_ = false;  /* if run_again_, don't set run_then_ */ \
    for (; (! run_then_); \
        run_again_ ? (run_again_ = false), true  /* again keeps looping */ \
        : (run_then_ = true))  /* continue exits the attempt "loop" */

#define needful_until(cond) /* {body} */ \
    bool run_then_ = false; \
    bool run_again_ = false; \
    for (; run_again_ ? (run_again_ = false), true :  /* skip condition */ \
        (cond) ? (run_then_ = true, false) : true; )

#define needful_whilst(cond) /* {body} */ /* can't be while [1] */ \
    bool run_then_ = false; \
    bool run_again_ = false; \
    for (; run_again_ ? (run_again_ = false), true :  /* skip condition */ \
        (! cond) ? (run_then_ = true, false) : true; )

#define needful_then /* {branch} */ \
    if (run_then_)

#define needful_again \
    { run_again_ = true; continue; }


/****[[ ZERO TYPE (RETURN-ABLE `void` SURROGATE) ]]***************************
**
** When using wrappers like Result(T) to do their magic, void can't be used
** as something that is able to be constructed from 0 when trying to do
** something like `return fail (...);`  Result(void) would just be void in
** a C build, and you can't pass any parameter to the `return` of a function
** that has a void return type (not even void itself, as `return void;`)
**
** Proposals to change this have been rejected, e.g. this from 2015:
**
**   https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2015/p0146r0.html
**
** So instead of Result(void) we use Result(Zero).  This is like what Rust
** would call the "unit type" (which they write as `()`).
**
** To make it work in C, it's just an enum with a single zero value.  But
** the C++ version is more complex.
**
** 1. The name "Zero" is chosen because "Nothing" is too close to "None"
**    which is being used for the `Option()` type's disengaged state.
**
** 2. There is no way the C++ enhancements can statically assert at
**    compile-time that a Zero is only being constructed from the 0 literal.
**    Because of this, you have to say `return zero` instead of `return 0`
**    when using the Zero type to pass the C++ static analysis.
*/

typedef enum {
    NEEDFUL_ZERO_ENUM_0 = 0  /* use lowercase for the constant [1] */
} NeedfulZeroEnum;

#define NeedfulZero  NeedfulZeroEnum
#define needful_zero  NEEDFUL_ZERO_ENUM_0  /* use instead of 0 literal [2] */


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
            assert("Expected variable to be corrupt and it was not"); \
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
*/

#define NEEDFUL_USED(...)    ((void)(__VA_ARGS__))

#define NEEDFUL_UNUSED(...)  ((void)(__VA_ARGS__))

#define NEEDFUL_PASSTHRU(...)  (__VA_ARGS__)


/****[[ STATIC_ASSERT, STATIC_IGNORE, STATIC_FAIL ]]**************************
**
** 1. We don't do a "poor man's" static assert in C, but make it a no-op.
**    The reason is that there are too many limitations of a C shim:
**
**      https://stackoverflow.com/questions/3385515/static-assert-in-c
**
**    So trust the C++ overloaded definition to enforce the asserts when you
**    build as C++.
*/

#define NEEDFUL_STATIC_IGNORE(expr) /* uses trick for callsite semicolons */ \
    struct GlobalScopeNoopTrick  /* https://stackoverflow.com/q/53923706 */

#define NEEDFUL_STATIC_ASSERT(cond) \
    NEEDFUL_STATIC_IGNORE(cond)  /* C version is noop [1] */

#define NEEDFUL_STATIC_ASSERT_NOT(cond) \
    NEEDFUL_STATIC_ASSERT(! (cond))

#define NEEDFUL_STATIC_FAIL(msg) \
    typedef int static_fail_##msg[-1]  /* message has to be a C identifier */


/****[[ STATIC ASSERT LVALUE TO HELP EVIL MACRO USAGE ]]**********************
**
** Macros are generally bad, but especially bad if they use their arguments
** more than once...because if that argument has a side-effect, they will
** have that side effect more than once.
**
** However, checked builds will not inline functions.  Some code is run so
** often that not defining it in a macro leads to excessive cost in these
** checked builds, and "evil macros" which repeat arguments are a pragmatic
** solution to factoring code in these cases.  You just have to be careful
** to call them with simple references.
**
** Rather than need to give mean-sounding names like XXX_EVIL_MACRO() to
** these macros (which doesn't have any enforcement), this lets you ensure
** the argument is assignable (an lvalue).
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
** ensuring the expressions are up-to-date as valid for the compiler.
**
** `heeded()` marks things that look stray or like they have no effect, but
** their side-effect is intentional (perhaps only in debug builds, that check
** for what they did).
**
** Uppercase versions, can be used in global scope (more limited abilities)
*/

#define needful_possibly /* (cond) */     NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL
#define needful_impossible /* (cond) */   NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL

#define needful_unnecessary /* (expr) */  NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID
#define needful_dont /* (expr) */         NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID
#define needful_heeded /* (expr) */       NEEDFUL_PASSTHRU

#define NEEDFUL_POSSIBLY /* (cond) */        NEEDFUL_STATIC_IGNORE
#define NEEDFUL_IMPOSSIBLE /* (cond) */      NEEDFUL_STATIC_ASSERT_NOT

#define NEEDFUL_UNNECESSARY /* (expr) */     NEEDFUL_STATIC_IGNORE
#define NEEDFUL_DONT /* (expr) */            NEEDFUL_STATIC_IGNORE
/* NEEDFUL_HEEDED makes no sense in global scope, right? */


/****[[ NODISCARD shim ]]*****************************************************
**
** [[nodiscard]] is a C++17 feature, but some compilers offer it as something
** you can use in C code as well.  It's useful enough in terms of warning
** of unused results that a macro is offered to generalize it.
**
** (The pre-[[nodiscard]] hacks only work on functions, not types.  They
** can be applied to structs, but will have no effect.  [[nodiscard]] from
** C++17 works on types as well as functions, so it is more powerful.)
*/

#if defined(__GNUC__) || defined(__clang__)
    #define NEEDFUL_NODISCARD  __attribute__((warn_unused_result))
#elif defined(_MSC_VER)
    #define NEEDFUL_NODISCARD _Check_return_
#else
    #define NEEDFUL_NODISCARD  /* C++ overload may define as [[nodiscard]] */
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
** 1. A quick and dirty way to write `return failed;` and not have to come
**    up with an error might be useful in some codebases.  We don't try to
**    define that here, because it's open ended as to what you'd use for
**    your error value type.
**
** 2. The lenient form of ensure() is quite useful for writing polymorphic
**    macros which are const-in => const-out and mutable-in => mutable-out.
**    This tends to be more useful than wanting to enforce that only mutable
**    pointers can be passed into a macro (the bulk of macros are reading
**    operations, anyway).  So lenient defaults to the short name `ensure()`.
*/

#if !defined(NEEDFUL_DONT_DEFINE_CAST_SHORTHANDS)
    #define cast /* (T,...) */      needful_lenient_hookable_cast

    #define x_cast /* (T,...) */    needful_xtreme_cast

    #define u_cast /* (T,...) */    needful_lenient_unhookable_cast
    #define h_cast /* (T,...) */    needful_lenient_hookable_cast

    #define m_cast /* (T,...) */    needful_mutable_cast

    #define p_cast /* (T,...) */    needful_pointer_cast
    #define i_cast /* (T,...) */    needful_integral_cast
    #define f_cast /* (T,...) */    needful_function_cast
    #define v_cast /* (T,...) */    needful_valist_cast

    #define nocast /* ... */        needful_nocast

    #define downcast /* ... */      needful_hookable_downcast
    #define u_downcast /* ... */    needful_unhookable_downcast

    #define upcast /* (T,...) */    needful_upcast
#endif

#if !defined(NEEDFUL_DONT_DEFINE_OPTION_SHORTHANDS)
    #define Option /* (T) */        NeedfulOption
    #define none                    needful_none

    #define unwrap /* ... */        needful_unwrap
    #define opt /* ... */           needful_opt
#endif

#if !defined(NEEDFUL_DONT_DEFINE_RESULT_SHORTHANDS)
    #define Result /* (T) */        NeedfulResult

    #define fail /* (...) */        needful_fail
    /* #define failed               needful_fail("generic failure");  [1] */
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
    #define Need /* (TP) */         NeedfulNeed
#endif

#if !defined(NEEDFUL_DONT_DEFINE_ENSURE_SHORTHANDS)
    #define rigid_ensure /* (T,expr) */           needful_rigid_ensure
    #define lenient_ensure /* (T,expr) */         needful_lenient_ensure

    #define ensure /* (T,expr) */ /* [2] */       needful_lenient_ensure
    #define ensure_any /* ((T1,T2,...),expr) */   needful_ensure_any

    #define ensure_lvalue /* (var) */             needful_ensure_lvalue
#endif

#if !defined(NEEDFUL_DONT_DEFINE_LOOP_SHORTHANDS)
    #define attempt /* {body} */            needful_attempt
    #define until /* (cond) {body} */       needful_until
    #define whilst /* (cond) {body} */      needful_whilst
    #define then /* {branch} */             needful_then
    #define again                           needful_again
#endif

#if !defined(NEEDFUL_DONT_DEFINE_ZERO_SHORTHANDS)
    #define Zero                    NeedfulZero
    #define zero                    needful_zero
#endif

#if !defined(NEEDFUL_DONT_DEFINE_COMMENT_SHORTHANDS)
    #define possibly /* (cond) */    needful_possibly
    #define impossible /* (cond) */  needful_impossible

    #define unnecessary /* (expr) */  needful_unnecessary
    #define dont /* (expr) */         needful_dont
    #define heeded /* (expr) */       needful_heeded

    #define POSSIBLY /* (cond) */        NEEDFUL_POSSIBLY
    #define IMPOSSIBLE /* (cond) */      NEEDFUL_IMPOSSIBLE

    #define UNNECESSARY /* (expr) */     NEEDFUL_UNNECESSARY
    #define DONT /* (expr) */            NEEDFUL_DONT
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

  #if !defined(NODISCARD)
    #define NODISCARD  NEEDFUL_NODISCARD
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

#if !defined(NEEDFUL_CPP_ENHANCEMENTS)
    #define NEEDFUL_CPP_ENHANCEMENTS  0  // Note: can still be compiled as C++
#endif

#if !defined(NEEDFUL_OPTION_USES_WRAPPER)
  #define NEEDFUL_OPTION_USES_WRAPPER  NEEDFUL_CPP_ENHANCEMENTS
#endif

#if !defined(NEEDFUL_RESULT_USES_WRAPPER)
  #define NEEDFUL_RESULT_USES_WRAPPER  NEEDFUL_CPP_ENHANCEMENTS
#endif

#if !defined(NEEDFUL_SINK_USES_WRAPPER)
  #define NEEDFUL_SINK_USES_WRAPPER  NEEDFUL_CPP_ENHANCEMENTS
#endif

#if !defined(NEEDFUL_CAST_CALLS_HOOKS)
  #define NEEDFUL_CAST_CALLS_HOOKS  NEEDFUL_CPP_ENHANCEMENTS
#endif

#if NEEDFUL_CPP_ENHANCEMENTS
    #if !defined(__cplusplus)
        #error "NEEDFUL_CPP_ENHANCEMENTS requires building your code as C++"
    #elif (__cplusplus < 201103L) && (!defined(_MSC_VER) || _MSC_VER < 1900)
        #error "NEEDFUL_CPP_ENHANCEMENTS requires C++11 or later"
    #endif

    #include "cplusplus/cplusplus-needfuls.hpp"
#else
    #if NEEDFUL_OPTION_USES_WRAPPER
        #error "NEEDFUL_OPTION_USES_WRAPPER requires NEEDFUL_CPP_ENHANCEMENTS"
    #endif
    #if NEEDFUL_RESULT_USES_WRAPPER
        #error "NEEDFUL_RESULT_USES_WRAPPER requires NEEDFUL_CPP_ENHANCEMENTS"
    #endif
    #if NEEDFUL_SINK_USES_WRAPPER
        #error "NEEDFUL_SINK_USES_WRAPPER requires NEEDFUL_CPP_ENHANCEMENTS"
    #endif
    #if NEEDFUL_CAST_CALLS_HOOKS
        #error "NEEDFUL_CAST_CALLS_HOOKS requires NEEDFUL_CPP_ENHANCEMENTS"
    #endif
#endif


/****[[ TEMPORARY: NEEDFUL TESTS ]]*******************************************
**
** This is a temporary measure to allow the tests to be run without having
** a separate build step to do so, just to start getting the tests written.
*/

#if NEEDFUL_CPP_ENHANCEMENTS && !defined(NDEBUG)
    #include "tests/all-needful-tests.hpp"
#endif

#endif  /* !defined(NEEDFUL_H_INCLUDED) */
