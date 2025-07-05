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


#if __has_cpp_attribute(nodiscard)
    #undef NEEDFUL_NODISCARD
    #define NEEDFUL_NODISCARD  [[nodiscard]]  // the C++17 way (best!)
#else
    // leave NEEDFUL_NODISCARD defined however needful.h defined it (which
    // may have a definition in GCC/Clang/MSVC even in C code)
    //
    // these alternative definitions only work on functions, and should be
    // noops on structs.  But the C++17 way works on structs.
#endif

#undef STATIC_ASSERT
#define STATIC_ASSERT(cond) \
    static_assert((cond), #cond) // callsite has semicolon, see C trick

#undef STATIC_ASSERT_DECLTYPE_BOOL
#define STATIC_ASSERT_DECLTYPE_BOOL(expr) \
    static_assert(std::is_convertible<decltype((expr)), bool>::value, \
        "expression must be convertible to bool")

#undef STATIC_ASSERT_DECLTYPE_VALID
#define STATIC_ASSERT_DECLTYPE_VALID(expr) \
    static_assert(std::is_same<decltype((void)(expr)), void>::value, "")


#undef STATIC_FAIL
#define STATIC_FAIL(msg)  static_assert(0, #msg)

// 1. Double-parentheses needed to force reference qualifiers for decltype.

#undef STATIC_ASSERT_LVALUE
#define STATIC_ASSERT_LVALUE(x) \
    static_assert( \
        std::is_lvalue_reference<decltype((x))>::value, /* [1] */ \
        "must be lvalue reference")


//=//// TYPE ENSURING HELPER //////////////////////////////////////////////=//
//

template<typename From, typename To>
struct IsConvertibleAsserter {
    static_assert(
        std::is_convertible<From, To>::value, "ensure() failed"
    );
};

#undef Needful_Ensure_Rigid
#define Needful_Ensure_Rigid(T,expr) \
    (NEEDFUL_USED((needful::IsConvertibleAsserter< /* USED() for clang */ \
        needful_remove_reference(decltype(expr)), \
        T \
    >{})), \
    x_cast(T, (expr)))

#undef Needful_Ensure_Lenient
#define Needful_Ensure_Lenient(T,expr) \
    (NEEDFUL_USED((needful::IsConvertibleAsserter< /* USED() for clang */ \
        needful_remove_reference(decltype(expr)), \
        needful_constify_type(T) /* loosen to matching constified T too */ \
    >{})), \
    x_cast(needful_merge_const(decltype(expr), T), (expr)))
