//
//  file: %needful-asserts.h
//  summary: "Assertions and commentary macros for C and C++""
//  homepage: <needful homepage TBD>
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License
//
//=/////////////////////////////////////////////////////////////////////////=//
//


//=//// UNREACHABLE CODE ANNOTATIONS //////////////////////////////////////=//
//
// Because Rebol uses `longjmp` and `exit` there are cases where a function
// might look like not all paths return a value, when those paths actually
// aren't supposed to return at all.  For instance:
//
//     int foo(int x) {
//         if (x < 1020)
//             return x + 304;
//         panic ("x is too big"); // compiler may warn about no return value
//     }
//
// One way of annotating to say this is okay is on the caller, with DEAD_END:
//
//     int foo(int x) {
//         if (x < 1020)
//             return x + 304;
//         panic ("x is too big");
//         DEAD_END; // our warning-suppression macro for applicable compilers
//     }
//
// DEAD_END is just a no-op in compilers that don't have the feature of
// suppressing the warning--which can often mean they don't have the warning
// in the first place.
//
// Another macro we define is ATTRIBUTE_NO_RETURN.  This can be put on the
// declaration site of a function like `panic()` itself, so the callsites don't
// need to be changed.  As with DEAD_END it degrades into a no-op in compilers
// that don't support it.
//

/* THESE HAVE BEEN RELOCATED TO %rebol.h, SEE DEFINITIONS THERE */


//=//// STATIC IGNORE /////////////////////////////////////////////////////=//
//
// This is a trick for commenting things out in global scope.
//
//   https://stackoverflow.com/q/53923706
//
#define STATIC_IGNORE(expr)  struct GlobalScopeNoopTrick


//=//// STATIC ASSERT /////////////////////////////////////////////////////=//
//
// Some conditions can be checked at compile-time, instead of deferred to a
// runtime assert.  This macro triggers an error message at compile time.
// `static_assert` is an arity-2 keyword in C++11 and can act as arity-1 in
// C++17, for expedience we mock up an arity-1 form.
//
// Note: STATIC_ASSERT((std::is_same<T, U>::value)) is a common pattern,
// and needs double parentheses for the < and > to work with the macro.

// 1. It's possible to hack up a static assert in C:
//
//      http://stackoverflow.com/questions/3385515/static-assert-in-c
//
//    But it's too limited.  Since the code can (and should) be built as C++11
//    to test anyway, just make it a no-op in the C build.
//
#if !defined(STATIC_ASSERT)  // used in %reb-config.h so also defined there
    #if CPLUSPLUS_11
        #define STATIC_ASSERT(cond) \
            static_assert((cond), #cond) // callsite has semicolon, see C trick
    #else
        #define STATIC_ASSERT(cond) \
            STATIC_IGNORE(cond)  // C static assert would be too limited [1]
    #endif
#endif


//=//// STATIC FAILURE FOR C AND C++ //////////////////////////////////////=//
//
// If you want to trigger a compile-time failure with a message, this macro
// can do the job.  For example:
//
//    #if (NEEDFUL_DONT_INCLUDE_STDARG_H)
//      #define h_cast(T,v)  STATIC_FAIL(v_cast_disabled)
//    #else
//      #define h_cast(T,v)  ...
//    #endif
//
// Your message has to be able to be embedded in a C identifier, since the C
// case uses it to declare a negative sized array.
//
#if CPLUSPLUS_11
    #define STATIC_FAIL(msg)  static_assert(0, #msg)
#else
    #define STATIC_FAIL(msg)  typedef int static_fail_##msg[-1]
#endif


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
// 1. Double-parentheses needed to force reference qualifiers for decltype.
//
#if CPLUSPLUS_11
    #define STATIC_ASSERT_LVALUE(x) \
        static_assert( \
            std::is_lvalue_reference<decltype((x))>::value, /* [1] */ \
            "must be lvalue reference")
#else
    #define STATIC_ASSERT_LVALUE(x) NOOP
#endif


//=//// NO-OP STATIC_ASSERTS THAT VALIDATE EXPRESSIONS ////////////////////=//
//
// These are utilized by the commentary macros, which don't do anything but
// do help keep the comments current by ensuring the expressions they take
// will compile (hence variables named by them are valid, etc.)
//
#if NO_CPLUSPLUS_11
    #define STATIC_ASSERT_DECLTYPE_BOOL(expr)     NOOP
    #define STATIC_ASSERT_DECLTYPE_VALID(expr)    NOOP
#else
    #define STATIC_ASSERT_DECLTYPE_BOOL(expr) \
        static_assert(std::is_convertible<decltype((expr)), bool>::value, \
            "expression must be convertible to bool")

    #define STATIC_ASSERT_DECLTYPE_VALID(expr) \
        static_assert(std::is_same<decltype((void)(expr)), void>::value, "")
#endif


//=//// "POSSIBLY" NON-ASSERT /////////////////////////////////////////////=//
//
// Comments often carry information about when something may be true:
//
//     int i = Get_Integer(...);  // i may be < 0
//
// `possibly` is a no-op construct which makes sure the expression you pass
// it compiles, but doesn't do anything with it.
//
//     int i = Get_Integer(...);
//     possibly(i < 0);
//
// Separating it out like that may provide a better visual flow (e.g. the
// comment might have made a line overlong), but also it's less likely to
// get out of date because it checks that the expression is well-formed.
//
#define possibly(expr)       STATIC_ASSERT_DECLTYPE_BOOL(expr)
#define POSSIBLY(expr)       STATIC_IGNORE(expr)


//=//// "UNNECESSARY" CODE SUPPRESSOR /////////////////////////////////////=//
//
// `unnecessary` is another commentary construct, where you can put some code
// that people might think you have to write--but don't.  This helps cue them
// into realizing that the omission was intentional, with the advantage of
// showing the precise code they might think they need.
//
#define unnecessary(expr)    STATIC_ASSERT_DECLTYPE_VALID(expr)
#define UNNECESSARY(expr)    STATIC_IGNORE(expr)


//=//// "DON'T" CODE SUPPRESSOR ///////////////////////////////////////////=//
//
// `dont` is a more strongly-worded version of `unnecessary`, that points out
// something you really *shouldn't* do...not because it's redundant, but
// because it would break things.
//
#define dont(expr)           STATIC_ASSERT_DECLTYPE_VALID(expr)
#define DONT(expr)           STATIC_IGNORE(expr)


//=//// "HEEDED" REMARK ///////////////////////////////////////////////////=//
//
// `heeded` runs the code you pass it, but is there to remark that even though
// it might seem disconnected or like a no-op, that it is paid attention to
// by code somewhere else.
//
// (This is useful e.g. when corrupting a variable in debug builds for the
// sole purpose of showing a routine you call that you weren't expecting it to
// have valid data at the end of their call.)
//
#define heeded(expr)         (expr)
#define HEEDED(expr)         expr


//=//// "IMPOSSIBLE" SO DON'T EVEN ASSERT IT //////////////////////////////=//
//
// `impossible` is a way of documenting something that could be an assert, but
// it would waste time because you know it should never happen.  (Uses of this
// are a bit of a red flag that the design may benefit from rethinking such
// that the impossible case isn't expressable at all.)
//
// Outside of wasting time there shouldn't be any harm in asserting it, so
// comprehensive debug builds can request to treat these as asserts.
//

#if ASSERT_IMPOSSIBLE_THINGS
    #define impossible(expr)   assert(expr)
#else
    #define impossible(expr)   STATIC_ASSERT_DECLTYPE_BOOL(expr)
#endif

#define IMPOSSIBLE(expr)     STATIC_ASSERT(!(expr))  // no runtime cost...
