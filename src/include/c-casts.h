//
//  file: %c-casts.h
//  summary: "Cast macros with added features when built as C++11 or higher"
//  homepage: http://blog.hostilefork.com/c-casts-for-the-masses/
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
// The goal of this file is to define a set of macros for casting which have
// trivial definitions when built as C, but offer enhanced features when built
// as C++11 or higher.
//
// It provides *easier-to-spot* variants of the parentheses cast, and also
// helps document at the callsite what the purpose of the cast is.  When
// built under C++11 with access to <type_traits>, the variants are able to
// enforce their narrower policies.
//
// Also, the casts are designed to be "hookable" so that checks can be done
// at runtime in C++ checked builds to ensure that the cast is good.  This
// lets the callsites remain simple and clean while still getting the
// advantage of debug checks when desired--though semantics should not be
// changed (e.g. validate the data being cast, but don't change the cast).
// This means that downcasting even raw pointers can do runtime validation
// on the pointed-to-data to ensure the cast is correct.
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
// SAFETY LEVEL
//    * Normal usage:             cast()      // safe default choice
//    * Not checked at all:       u_cast()    // use with fresh malloc()s
//                                            //   ...or critical debug paths
//
// POINTER CONSTNESS
//    * Preserving constness:     c_cast()    // T1* => T2* ...or...
//                                              // const T1* => const T2*
//    * Adding mutability:        m_cast()    // const T* => T*
//    * Type and mutability:      x_cast()    // const T1* => T2*
//
// TYPE CONVERSIONS
//    * Non-pointer to pointer:    p_cast()   // int => T*
//    * Non-integral to integral:  i_cast()   // ptr => intptr_t
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// A. The C preprocessor doesn't know about templates, so it parses things
//    like FOO(something<a,b>) as taking "something<a" and "b>".  This is a
//    headache for implementing the macros, but also if a macro produces a
//    comma and gets passed to another macro.  To work around it, we wrap
//    the product of the macro containing commas in parentheses.
//
// B. The casts are implemented with a static method of a templated struct vs.
//    just as a templated function.  This is because partial specialization of
//    function templates is not legal in C++ due to the fact that functions can
//    be overloaded, while structs and classes can't:
//
//      http://www.gotw.ca/publications/mill17.htm
//
//    "If you're writing a function base template, prefer to write it as a
//     single function template that should never be specialized or overloaded,
//     and then implement the function template entirely as a simple handoff
//     to a class template containing a static function with the same
//     signature. Everyone can specialize that -- both fully and partially,
//     and without affecting the results of overload resolution."
//

#ifndef C_CASTS_H  // "include guard" allows multiple #includes
#define C_CASTS_H


//=//// TYPE_TRAITS IN C++11 AND ABOVE ///////////////////////////////////=//
//
// One of the most powerful tools you can get from allowing a C codebase to
// compile as C++ comes from type_traits:
//
// http://en.cppreference.com/w/cpp/header/type_traits
//
// This is essentially an embedded query language for types, allowing one to
// create compile-time errors for any C construction that isn't being used
// in the way one might want.
//
// 1. The type trait is_explicitly_convertible() is useful, but it was taken
//    out of GCC.  This uses a simple implementation that was considered to
//    be buggy for esoteric reasons, but is good enough for our purposes.
//
//    https://stackoverflow.com/a/16944130
//
//    Note this is not defined in the `std::` namespace since it is a shim.
//
#if CPLUSPLUS_11
    #include <type_traits>

  namespace shim {  // [1]
    template<typename _From, typename _To>
    struct is_explicitly_convertible : public std::is_constructible<_To, _From>
      { };
  }
#endif


//=//// UNCHECKED CAST ////////////////////////////////////////////////////=//
//
// Unchecked cast which does not offer any validation hooks.  Use e.g. when
// casting a fresh allocation to avoid triggering validation of uninitialized
// structures in debug builds.
//
// Also: while the other casts should not cost anything in release builds, the
// practical concerns of debug builds do mean that even constexpr templates
// have overhead.  So judicious use of this unchecked u_cast() operation can
// be worth it for speeding up debug builds in critical trusted functions,
// while still being easier to spot than a parentheses-based cast.
//
#define u_cast(T,v) \
    ((T)(v))  // in both C and C++, just an alias for parentheses cast


//=//// BASIC CAST /////////////////////////////////////////////////////////=//
//
// This is the form of hookable cast you should generally reach for.  Default
// hooks are provided for pointer-to-pointer, or integral-to-integral.
//
// USAGE:
//    T result = cast(T, value);
//
// BEHAVIOR:
// - For arithmetic/enum types: static_cast if not explicitly convertible
// - For pointer-to-pointer: reinterpret_cast if not explicitly convertible
// - For explicitly convertible types: static_cast
//
// CUSTOMIZATION:
// To hook the cast, you define `CastHelper` for the types you are interested
// in hooking. Example specialization:
//
//    template<>
//    struct CastHelper<SourceType, TargetType> {
//        static TargetType convert(SourceType value) {
//            // Add validation logic here
//            return reinterpret_cast<TargetType>(value);
//        }
//    };
//
// A key usage of this is to give smart-pointer-like validation opportunities
// at the moment of casting, even though you are using raw pointers.
//
// For instance, let's imagine you have a variant Number class that can hold
// either integers or floats.  Due to strict aliasing, the C build can only
// define one struct for both--limiting the ability of the type system to
// catch passing the wrong type at compile time.  But C++ can use inheritance
// and still be on the right side of strict aliasing:
//
//    union IntegerOrFloat { int i; float f; }
//    struct Number { bool is_float; union IntegerOrFloat iof; };
//
//    #if CPLUSPLUS_11
//        // Compile-time checks prevent Integer* passed to Float* arguments
//        // (or Float* passed to Integer*).  Still legal to pass either an
//        // Integer* or Float* to where Number* is expected.
//        //
//        struct Integer : public Number;
//        struct Float : public Number;
//    #else
//        // Just typedefs, so no checks when codebase is built as plain C.
//        // Float* can be passed where Integer* is expected (and vice-versa).
//        //
//        typedef Number Integer;
//        typedef Number Float;
//    #endif
//
// This means you can define an allocator for a Float* which gives back
// a pointer that can be used as a Number* without violating strict aliasing
// in either C or C++:
//
//    Float* Allocate_Float(float f) {
//        Float* number = u_cast(Float*, malloc(sizeof(Float)));
//        number->is_float = true;
//        number->foi.f = f;
//        return number;
//    }
//
// So now, what if you have a Number* you want to cast?
//
//    void Process_Float(Float* f) { ... }
//
//    void Do_Something(Number* num) {
//       if (num->is_float) {
//           Float* f = cast(Float*, num);
//           Process_Float(f);
//       }
//    }
//
// Because Float* is not a smart pointer class, there's not a place in typical
// C++ to add any runtime validation at the moment of casting.  But the cast()
// macro is based on a `CastHelper` template you can inject any validation for
// that pointer pairing that you want:
//
//    template<>
//    struct CastHelper<Number*,Float*> {
//        static Float* convert(Number* num) {
//            assert(num->is_float);
//            return reinterpret_cast<Float*>(num);
//        }
//    };
//
// This way, whenever you cast from a Number* to a Float*, debug builds can
// check that the number actually was allocated as a float.
//
#if NO_CPLUSPLUS_11
    #define cast(T,v) \
        ((T)(v))  // in C, just another alias for parentheses cast
#else
    template<typename V, typename T>
    struct CastHelper {
        // arithmetic/enum types that aren't explicitly convertible
        template<typename V_ = V, typename T_ = T>
        static constexpr typename std::enable_if<
            not shim::is_explicitly_convertible<V_,T_>::value and (
                (std::is_arithmetic<V_>::value or std::is_enum<V_>::value)
                and (std::is_arithmetic<T_>::value or std::is_enum<T_>::value)
            ),
        T>::type convert(V_ v) { return static_cast<T>(v); }

        // pointer-to-pointer conversions that aren't explicitly convertible
        template<typename V_ = V, typename T_ = T>
        static constexpr typename std::enable_if<
            not shim::is_explicitly_convertible<V_,T_>::value and (
                std::is_pointer<V_>::value and std::is_pointer<T_>::value
            ),
        T>::type convert(V_ v) { return reinterpret_cast<T>(v); }

        // for types that are explicitly convertible
        template<typename V_ = V, typename T_ = T>
        static constexpr typename std::enable_if<
            shim::is_explicitly_convertible<V_,T_>::value,
        T>::type convert(V_ v) { return static_cast<T>(v); }
    };

    template<typename V>
    struct CastHelper<V,void>
      { static void convert(V v) { (void)(v); } };  // void can't be constexpr

    #define cast(T,v)  /* needs outer parens, see [A] */ \
        (CastHelper<typename std::remove_reference< \
            decltype(v)>::type, T>::convert(v))
#endif


//=//// CONST-PRESERVING CAST //////////////////////////////////////////////=//
//
// This cast is useful for defining macros that want to mirror the constness
// of the input pointer, when you don't know if the caller is passing a
// const or mutable pointer in.  The C build will always give you a mutable
// pointer back, so you have to rely on the C++ build for const enforcement.
//
// It can also be nice as a shorthand, even if you know the input is const:
//
//     const Float* Const_Number_To_Float(const Number* n) {
//         return c_cast(Float*, n);  // briefer than `cast(const Float*, n)`
//     }
//
// It's built on top of the `CastHelper` used by plain `cast()`, so debug
// checks applicable to a plain cast will also be run by `c_cast()`.
//
#if NO_CPLUSPLUS_11
    #define c_cast(T,v) \
        ((T)(v))  // in C, just another alias for parentheses cast
#else
    template<typename TP, typename VQPR>
    struct ConstPreservingCastHelper {
        static_assert(std::is_pointer<TP>::value, "c_cast() non pointer!");
        typedef typename std::remove_reference<VQPR>::type VQP;
        typedef typename std::remove_pointer<VQP>::type VQ;
        typedef typename std::remove_pointer<TP>::type T;
        typedef typename std::add_const<T>::type TC;
        typedef typename std::add_pointer<TC>::type TCP;
        typedef typename std::conditional<
            std::is_const<VQ>::value,
            TCP,
            TP
        >::type type;
    };

    #define c_cast(TP,v)  /* needs outer parens, see [A] */ \
        (CastHelper< \
            decltype(v), typename ConstPreservingCastHelper<TP,decltype(v)>::type \
        >::convert(v))
#endif


//=//// MUTABLE CAST //////////////////////////////////////////////////////=//
//
// This is a cast whose sole purpose is to get mutable access to a pointer,
// without changing other aspects of the type.  It's allowed for the input
// pointer to already be mutable.
//
// It is hookable via `MutableCastHelper()` for the case of when C++ builds are
// using a smart pointer class instead of a raw pointer.
//
#if NO_CPLUSPLUS_11
    #define m_cast(T,v) \
        ((T)(v))  // in C, just another alias for parentheses cast
#else
    template<typename T, typename V>
    constexpr T MutableCastHelper(V v) {
        static_assert(not std::is_const<T>::value,
            "invalid m_cast() - requested a const type for output result");
        static_assert(std::is_volatile<T>::value == std::is_volatile<V>::value,
            "invalid m_cast() - input and output have mismatched volatility");
        return const_cast<T>(v);
    }

    #define m_cast(T,v) \
        MutableCastHelper<T>(v)
#endif


//=//// ARBITRARY POINTER CAST ////////////////////////////////////////////=//
//
// This is a cast for making arbitrary changes to a pointer, including
// casting away constness.  It's still slightly more restrictive than the
// unchecked `u_cast()`, because it enforces the input and output types as
// being pointers.
//
// It is not built on `CastHelper`, so an `x_cast()` won't run the debug
// checks that `cast()` and `c_cast()` would.  Use sparingly!
//
#if NO_CPLUSPLUS_11
    #define x_cast(T,v) \
        ((T)(v))  // in C, just another alias for parentheses cast
#else
    template<typename TQP>
    struct ArbitraryPointerCastHelper {
        static_assert(std::is_pointer<TQP>::value, "x_cast() non pointer!");
        typedef typename std::remove_pointer<TQP>::type TQ;
        typedef typename std::add_const<TQ>::type TC;
        typedef typename std::add_pointer<TC>::type type;
    };

    template<typename T>
    struct XCastHelper {
        typedef typename std::conditional<
            std::is_pointer<T>::value,
            typename ArbitraryPointerCastHelper<T>::type,
            T
        >::type type;
    };

    #define x_cast(T,v)  /* needs outer parens, see [A] */ \
        (const_cast<T>((typename XCastHelper<T>::type)(v)))
#endif


//=//// NON-POINTER TO POINTER CAST ////////////////////////////////////////=//
//
// If your intent is to turn a non-pointer into a pointer, this identifies
// that as the purpose of the cast.  The C++ build can confirm that is what
// is actually being done.
//
#if NO_CPLUSPLUS_11
    #define p_cast(T,v) \
        ((T)(v))  // in C, just another alias for parentheses cast
#else
    template<typename TP, typename V>
    constexpr TP p_cast_helper(V v) {
        static_assert(std::is_pointer<TP>::value,
            "invalid p_cast() - target type must be pointer");
        static_assert(not std::is_pointer<V>::value,
            "invalid p_cast() - source type can't be pointer");
        return reinterpret_cast<TP>(static_cast<uintptr_t>(v));
    }

    #define p_cast(TP,v) \
        p_cast_helper<TP>(v)
#endif


//=//// NON-INTEGRAL TO INTEGRAL CAST /////////////////////////////////////=//
//
// If your intent is to turn a non-integral into an integral, this identifies
// that as the purpose of the cast.  The C++ build can confirmthat is what
// is actually being done.
//
#if NO_CPLUSPLUS_11
    #define i_cast(T,v) \
        ((T)(v))  // in C, just another alias for parentheses cast
#else
    template<typename T, typename V>
    constexpr T i_cast_helper(V v) {
        static_assert(std::is_integral<T>::value,
            "invalid i_cast() - target type must be integral");
        static_assert(not std::is_integral<V>::value,
            "invalid i_cast() - source type can't be integral");
        return reinterpret_cast<T>(v);
    }

    #define i_cast(T,v) \
        i_cast_helper<T>(v)
#endif


//=//// REMOVE REFERENCE CAST /////////////////////////////////////////////=//
//
// Simplifying remove-reference cast.
//
#if NO_CPLUSPLUS_11
    #define rr_cast(T,v) \
        ((T)(v))  // in C, just another alias for parentheses cast
#else
    template<typename V>
    struct RemoveReferenceCastHelper {
        typedef typename std::conditional<
            std::is_reference<V>::value,
            typename std::remove_reference<V>::type,
            V
        >::type type;
    };

    #define rr_cast(v) \
        static_cast<typename RemoveReferenceCastHelper<decltype(v)>::type>(v)
#endif


//=//// TYPE LIST HELPER //////////////////////////////////////////////////=//
//
// Type lists allow checking if a type is in a list of types at compile time:
//
//     template<typename T>
//     void process(T value) {
//         using NumericTypes = c_type_list<int, float, double>;
//         static_assert(NumericTypes::contains<T>(), "T must be numeric");
//         // ...
//     }
//
// 1. Due to wanting C++11 compatibility, it must be `List::contains<T>()` with
//    the parentheses, which is a bit of a wart.  C++14 or higher is needed
//    for variable templates, which allows `List::contains<T>` without parens:
//
//        struct contains_impl {  /* instead of calling this `contains` */
//            enum { value = false };
//        };
//        template<typename T>
//        static constexpr bool contains = contains_impl<T>::value;
//
//    Without that capability, best we can do is to construct an instance via
//    a default constructor (the parentheses), and then have a constexpr
//    implicit boolean coercion for that instance.
//
#if CPLUSPLUS_11
    template<typename... Ts>
    struct c_type_list {
        template<typename T>
        struct contains {
            enum { value = false };

            // Allow usage without ::value in most contexts [1]
            constexpr operator bool() const { return value; }
        };
    };

    template<typename T1, typename... Ts>
    struct c_type_list<T1, Ts...> {  // Specialization for non-empty lists
        template<typename T>
        struct contains {
            enum { value = std::is_same<T, T1>::value or
                        typename c_type_list<Ts...>::template contains<T>() };

            // Allow usage without ::value in most contexts [1]
            constexpr operator bool() const { return value; }
        };
    };
#endif


#endif  // C_CASTS_H
