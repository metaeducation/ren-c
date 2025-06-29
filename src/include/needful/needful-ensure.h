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


//=//// TYPE ENSURING HELPER //////////////////////////////////////////////=//
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
// It does not cost anything at runtime--even in debug builds--because it
// doesn't rely on a function template.  So it's a cheap way to inject type
// checking into macros.
//
#if NO_CPLUSPLUS_11
    #define ensure(T,v)  (v)
#else
    template<typename V, typename T>
    struct EnsureHelper {
        static_assert(
            std::is_convertible<V,T>::value, "ensure() failed"
        );
    };

    #define ensure(T,v) \
        (UNUSED(EnsureHelper<decltype(v),T>{}), (v))  // clang needs UNUSED
#endif


//=//// ensure_nullptr() //////////////////////////////////////////////////=//
//
// At one time, ensure_nullptr(p) was implemented as ensure(nullptr, p).
// However, this forced ensure() to use a function template in order to be
// able to have runtime code ensuring the nullness of a pointer.  That meant
// some overhead in debug builds, and ensure() wanted to be a compile-time
// only check... even in debug builds.  So ensure_nullptr() became its own
// separate construct.
//
#if NO_CPLUSPLUS_11
    #define ensure_nullptr(v)  (v)
#else
    template<typename T>
    INLINE T*& ensure_nullptr(T*& v) {
        assert(v == nullptr);
        return v;
    }
#endif
