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
//
// 2. If we use std::is_convertible<> instead of is_explicitly_convertible<>
//    things like Optional(T) would not count, since the boolean conversion
//    is explicit (but automatic in things like conditions of if() statements)

#undef NEEDFUL_STATIC_ASSERT
#define NEEDFUL_STATIC_ASSERT(...) /* variadic for [1] */ \
    static_assert((__VA_ARGS__), #__VA_ARGS__) // callsite has semicolon

#undef NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL
#define NEEDFUL_STATIC_ASSERT_DECLTYPE_BOOL(...) /* variadic for [1] */ \
    static_assert( \
        needful_is_explicitly_convertible_v(decltype(__VA_ARGS__), bool), \
        "expression must be explicitly convertible to bool")  // [2]

#undef NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID
#define NEEDFUL_STATIC_ASSERT_DECLTYPE_VALID(...) /* variadic for [1] */ \
    static_assert( \
        std::is_same<decltype((void)(__VA_ARGS__)), void>::value, \
        "declaration type must be valid")


#undef NEEDFUL_STATIC_FAIL
#define NEEDFUL_STATIC_FAIL(msg)  static_assert(0, #msg)

// 1. Double-parentheses needed to force reference qualifiers for decltype.

#undef NEEDFUL_STATIC_ASSERT_LVALUE
#define NEEDFUL_STATIC_ASSERT_LVALUE(x) \
    static_assert( \
        std::is_lvalue_reference<decltype((x))>::value, /* [1] */ \
        "must be lvalue reference")
