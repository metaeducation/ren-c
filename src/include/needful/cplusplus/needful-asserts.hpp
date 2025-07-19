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

// 1. You can't use __has_cpp_attribute(nodiscard), because that only says if
//    the compiler *recognizes* the attribute, not if it's *valid* in the
//    current language standard.  So if you're compiling with --std=c++11 it
//    would say it has the attribute, but error when [[nodiscard]] is used.
//
// 2. There may be a definition in GCC/Clang/MSVC even in C code that can be
//    used for NEEDFUL_NODISCARD.  But note these definitions only work on
//    functions, not types.  The C++17 way works on structs as well (though
//    not on typedefs).
//
#if __cplusplus >= 201703L  // C++17 or higher, can't check attribute [1]
    #undef NEEDFUL_NODISCARD
    #define NEEDFUL_NODISCARD  [[nodiscard]]  // the C++17 way (best!)
#else
    // leave NEEDFUL_NODISCARD defined however needful.h defined it [2]
#endif


// 1. We use __VA_ARGS__ here to avoid the need for double-parenthesization
//    at the callsite, e.g. STATIC_ASSERT((std::is_same<T, U>::value)), which
//    is what you'd have to do otherwise if you used templated stuff that
//    has commas in it that the preprocessor would misinterpret.

#undef STATIC_ASSERT
#define STATIC_ASSERT(...) /* variadic for [1] */ \
    static_assert((__VA_ARGS__), #__VA_ARGS__) // callsite has semicolon

#undef STATIC_ASSERT_DECLTYPE_BOOL
#define STATIC_ASSERT_DECLTYPE_BOOL(...) /* variadic for [1] */ \
    static_assert(std::is_convertible<decltype((__VA_ARGS__)), bool>::value, \
        "expression must be convertible to bool")

#undef STATIC_ASSERT_DECLTYPE_VALID
#define STATIC_ASSERT_DECLTYPE_VALID(...) /* variadic for [1] */ \
    static_assert(std::is_same<decltype((__VA_ARGS__)(expr)), void>::value, "")


#undef STATIC_FAIL
#define STATIC_FAIL(msg)  static_assert(0, #msg)

// 1. Double-parentheses needed to force reference qualifiers for decltype.

#undef STATIC_ASSERT_LVALUE
#define STATIC_ASSERT_LVALUE(x) \
    static_assert( \
        std::is_lvalue_reference<decltype((x))>::value, /* [1] */ \
        "must be lvalue reference")
