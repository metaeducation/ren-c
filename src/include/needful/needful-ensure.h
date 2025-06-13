//
//  file: %needful-ensure.h
//  summary: "Helpers for ensuring a type is correct without a function"
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


//=//// TYPE ENSURING HELPERS /////////////////////////////////////////////=//
//
// It's useful when building macros to make inline functions that just check
// a type, for example:
//
//      INLINE Foo* ensure_foo(Foo* f) { return f; }
//
// This has the annoying property that you have to write that function and
// put it somewhere.  But also, there's a problem with constness... if you
// want to retain it, you will need two overloads in C++ (and C does not
// have overloading).
//
// This introduces a simple `ensure` construct:
//
//      void *p = ensure(Flex*, s);
//
// 1. Because ensure is a no-op in non-checked builds, it does no casting in
//    the checked builds.  It only validates the type is convertible, and
//    then passes it through as its original self!  So if you say something
//    like `ensure(const foo*, bar)` and bar is a pointer to a mutable foo,
//    it will be valid...but pass the mutable bar as-is.
//
// 2. There was a macro for `ensureNullptr(p) = xxx` which did a runtime
//    check that a pointer was already nulled before assigning.  It turned
//    out templates specialize with nullptr, so `ensure(nullptr, p) = xxx`
//    happens to work out as an overloading of this construct, even though
//    it's rather different as a runtime value check instead of a compile-time
//    type check.  Avoids needing another name.
//
#if NO_CPLUSPLUS_11
    #define ensure(T,v) (v)
#else
    template<typename V, typename T>
    constexpr V const& ensure_impl(V const& v) {
        static_assert(
            std::is_convertible<V,T>::value, "ensure() failed"
        );
        return v;  // doesn't coerce to type T [1]
    }
    template<typename V, nullptr_t>  // runtime check of nullptr [2]
    V & ensure_impl(V & v) {
        assert(v == nullptr);
        return v;  // doesn't coerce to type T [1]
    }
    #define ensure(T,v) (ensure_impl<decltype(v),T>(v))
#endif
