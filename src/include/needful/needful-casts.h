//
//  file: %needful-casts.h
//  summary: "Cast macros with added features when built as C++11 or higher"
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
// The goal of this file is to define a set of macros for casting which have
// trivial definitions when built as C, but offer enhanced features when built
// as C++11 or higher.  It is an evolution of code from this blog article:
//
//  http://blog.hostilefork.com/c-casts-for-the-masses/
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
//        PRO-TIP: #define cast() as v_cast() in your codebase!!! [A]
//
// SAFETY LEVEL
//    * Validated cast:            v_cast()    // safe default, runs hooks
//    * Unchecked completely:      u_cast()    // use with fresh malloc()s
//                                               // ...or critical debug paths
//                                               // ...!!! or va_lists !!! [B]
//
// POINTER CONSTNESS
//    * Adding mutability:         m_cast()    // const T* => T*
//    * Type AND mutability:       x_cast()    // const T1* => T2*
//    * Preserving constness:      c_cast()    // T1* => T2* ...or...
//                                               // const T1* => const T2*
//    * Unchecked c_cast():      u_c_cast()    // c_cast() w/no v_cast() hooks
//
// TYPE CONVERSIONS
//    * Non-pointer to pointer:    p_cast()    // intptr_t => T*
//    * Non-integral to integral:  i_cast()    // T* => intptr_t
//    * Function to function:      f_cast()    // ret1(*)(...) => ret2(*)(...)
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// A. Because `cast` has a fair likelihood of being defined as the name of a
//    function or variable in C codebases, Needful does not force a definition
//    of `cast`.  But in an ideal situation, you could adapt your codebase
//    such that cast() can be defined, and defined as v_cast().
//
//    It's also potentially the case that you might want to start it out as
//    meaning u_cast()...especially if gradually adding Needful to existing
//    code.  You could start by turning all the old (T)(V) casts into cast()
//    defined as u_cast()...and redefine it as v_cast() after a process over
//    the span of time, having figured out which needed to be other casts.
//
// B. The va_list type is compiler magic, and the standard doesn't even
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
// C. The C preprocessor doesn't know about templates, so it parses things
//    like FOO(something<a,b>) as taking "something<a" and "b>".  This is a
//    headache for implementing the macros, but also if a macro produces a
//    comma and gets passed to another macro.  To work around it, we wrap
//    the product of the macro containing commas in parentheses.
//
// D. The casts are implemented with a static method of a templated struct vs.
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
// E. The CastHelper classes do not use references in their specialization.
//    So don't write:
//
//        struct CastHelper<Foo<X>&, Y*>
//          { static Y* convert(Foo<X>& foo) { ... } }
//
//    It won't ever match since the reference was removed by the macro.  But
//    do note you can leave the reference on the convert method if needed:
//
//        struct CastHelper<Foo<X>, Y*>  // note no reference on Foo<X>
//          { static Y* convert(Foo<X>& foo) { ... } }
//

#ifndef NEEDFUL_CASTS_H  // "include guard" allows multiple #includes
#define NEEDFUL_CASTS_H


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


//=//// v_cast(): VALIDATED CAST, IDEALLY cast() = v_cast() [A] ///////////=//
//
// This is the form of hookable cast you should generally reach for.  Default
// hooks are provided for pointer-to-pointer, or integral-to-integral.
//
// USAGE:
//    T result = v_cast(T, value);
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
// C++ to add any runtime validation at the moment of casting.  But v_cast()
// macro is based on a `CastHelper` template you can inject any validation for
// that pointer pairing that you want:
//
//    template<>
//    struct CastHelper<const Number*,const Float*> {  // const pointers! [1]
//        static const Float* convert(const Number* num) {
//            assert(num->is_float);
//            return reinterpret_cast<const Float*>(num);
//        }
//    };
//
// This way, whenever you cast from a Number* to a Float*, debug builds can
// check that the number actually was allocated as a float.  You can also do
// partial specialization of the cast helper, and do checks on the types
// being cast from... here's the same helper done with partial specialization
// just to give you the idea of the mechanism:
//
//    template<typename V>
//    struct CastHelper<const Number*,const V*> {  // const pointers! [1]
//        static const Float* convert(const V* v) {
//            static_assert(std::is_same<V, Float>::value, "Float expected");
//            assert(num->is_float);
//            return reinterpret_cast<const Float*>(num);
//        }
//    };
//
// 1. For pointer types, the system consolidates the dispatch mechanism based
//    on const pointers.  This is the natural choice for the general mechanic
//    because it is the most constrained (mutable pointers can be made const,
//    and have const methods called on them, not necessarily vice versa).
//    Hence mutable casts from Number* to Float* above runs the same code,
//    while returning the correct mutable output from the v_cast()...and it
//    correctly prohibits casting from a const Number* to a mutable Float*.
//
// 2. This has to be an object template and not a function template, in order
//    to allow partial specialization.  Without partial specialization you
//    can't provide a default that doesn't create ambiguities with the
//    SFINAE that come afterwards.
//
// 3. If you want things like char[10] to be decayed to char* for casting
//    purposes, it has to go through a decaying process.
//
// 4. Many of the "helpers" in this file use structs with static member
//    functions, to work around the fact that functions can't be partially
//    specialized while objects can.  However, we want "universal references"
//    which is how (T&& arg) is interpreted for functions vs. objects.
//


#if NO_CPLUSPLUS_11
  #define v_cast(T,v) \
    ((T)(v))  // in C, just another alias for parentheses cast
#else

    template<typename T>
    struct is_function_pointer : std::false_type {};

    template<typename Ret, typename... Args>
    struct is_function_pointer<Ret (*)(Args...)> : std::true_type {};

    template<typename From, typename To>
    struct is_const_removing_pointer_cast {  // catching const incorrectness
        static constexpr bool value =
            std::is_pointer<From>::value and std::is_pointer<To>::value
            and (
                std::is_const<typename std::remove_pointer<From>::type>::value
            ) and not (
                std::is_const<typename std::remove_pointer<To>::type>::value
            );
    };

    template<typename T>
    struct make_const_ptr {
        typedef T type;
    };

    template<typename T>
    struct make_const_ptr<T*> {
        typedef const T* type;
    };

    #define DEFINE_MAKE_CONST_PTR_FOR_CONVENTION(...) \
        template<typename Ret, typename... Args> \
        struct make_const_ptr<Ret(__VA_ARGS__ *)(Args...)> { \
            using type = Ret(__VA_ARGS__ *)(Args...); \
        }

    DEFINE_MAKE_CONST_PTR_FOR_CONVENTION();
    /* DEFINE_MAKE_CONST_PTR_FOR_CONVENTION(__cdecl); */
    /* DEFINE_MAKE_CONST_PTR_FOR_CONVENTION(__stdcall); */
    /* DEFINE_MAKE_CONST_PTR_FOR_CONVENTION(__fastcall); */
    #undef DEFINE_MAKE_CONST_PTR_FOR_CONVENTION

    // Helper to check if a cast would remove constness
    template<typename From, typename To>
    struct removes_constness {
        static const bool value = false;
    };

    template<typename T1, typename T2>
    struct removes_constness<const T1*, T2*> {
        static const bool value = true;
    };

    template<typename V, typename T>
    struct CastHelper {  // object template for partial specialization [2]
      static T convert(V v) {
        return u_cast(T, v);  // plain C cast is most versatile here
      }
    };

    template<typename From, typename To>
    struct ConstAwareCastDispatcher {  // implements const canonization [1]
        // Case 1: Both are pointers - dispatch to const version
        template<typename F = From, typename T = To>
        static typename std::enable_if<
            std::is_pointer<F>::value and std::is_pointer<T>::value,
            T
        >::type convert(const F& from) {
            typedef typename make_const_ptr<F>::type ConstFrom;
            typedef typename make_const_ptr<T>::type ConstTo;

            ConstTo const_result = CastHelper<ConstFrom, ConstTo>::convert(
                const_cast<ConstFrom>(from)
            );

            return const_cast<T>(const_result);
        }

        // Case 2: Not both pointers - direct cast
        template<typename F = From, typename T = To>
        static typename std::enable_if<
            not (std::is_pointer<F>::value and std::is_pointer<T>::value),
            T
        >::type convert(const F& from) {
            return CastHelper<F, T>::convert(from);
        }
    };

    template<  // Main cast with constness validation
        typename From,
        typename To,
        bool RemovesConst = is_const_removing_pointer_cast<From, To>::value
    >
    struct ValidatedCastHelper {
        static_assert(
            not is_function_pointer<From>::value
            and not is_function_pointer<To>::value,
            "Use f_cast() for function pointer casts"
        );

        static_assert(
            not is_const_removing_pointer_cast<From, To>::value,
            "cast removing const: use m_cast() or x_cast() if you mean it"
        );

      #if (! NEEDFUL_DONT_INCLUDE_STDARG_H)  // included by default for check
        static_assert(
            (   // v-- we can't warn you about va_list* cast() if this is true
                std::is_pointer<va_list>::value
                and std::is_fundamental<
                    typename std::remove_pointer<va_list>::type
                >::value
            )
            or (  // v-- but if it's a struct or something we can warn you
                not std::is_same<From, va_list*>::value
                and not std::is_same<To, va_list*>::value
            ),
            "can't cast va_list*!  u_cast() mutable va_list* <-> void* only"
        );  // read [B] at top of file for more information
      #endif

      static To convert(const From& v) {
        return ConstAwareCastDispatcher<From, To>::convert(v);
      }
    };

    template<typename From, typename To>  // const removal specialization
    struct ValidatedCastHelper<From, To, true> {
      static To convert(From /*v*/) {
        static_assert(
            false,
            "cast removing const: use m_cast() or x_cast() if you mean it"
        );
        return nullptr;
      }
    };


    template<typename To, typename From>
    typename std::enable_if<  // For ints/enums: use & as && can't bind const
        not std::is_array<typename std::remove_reference<From>::type>::value
            and std::is_fundamental<To>::value
            or std::is_enum<To>::value,
        To
    >::type
    v_cast_decaying_helper(const From& v) {
        return ValidatedCastHelper<
            typename std::decay<From>::type, To
        >::convert(v);
    }

    template<typename To, typename From>
    typename std::enable_if<  // For arrays: decay to pointer
        std::is_array<typename std::remove_reference<From>::type>::value
            and not std::is_fundamental<To>::value
            and not std::is_enum<To>::value,
        To
    >::type
    v_cast_decaying_helper(From&& v) {
        return ValidatedCastHelper<
            typename std::decay<From>::type, To
        >::convert(std::forward<From>(v));
    }

    template<typename To, typename From>
    typename std::enable_if<  // For non-arrays: forward as-is
        not std::is_array<typename std::remove_reference<From>::type>::value
            and not std::is_fundamental<To>::value
            and not std::is_enum<To>::value,
        To
    >::type
    v_cast_decaying_helper(From&& v) {
        return ValidatedCastHelper<From, To>::convert(
            std::forward<From>(v)  // preserves reference-ness
        );
    }
    #define v_cast(T, v) \
        v_cast_decaying_helper<T>(v)  // function for universal references [3]
#endif


//=//// c_cast(): CONST-PRESERVING CAST WITH u_c_cast() UNCHECKED /////////=//
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
// 1. The default `c_cast()` is It's built on top of the `CastHelper` used
//    by `v_cast()`, so debug checks applicable to a validated cast will also
//    be run by `c_cast()`.
//
// 2. If you don't want the validation checks and just want the const
//    preserving behavior, you can use `u_c_cast()` instead, for "unchecked
//    version of c_cast()"
//
#if NO_CPLUSPLUS_11
    #define c_cast(T,v) \
        ((T)(v))  // in C, just another alias for parentheses cast

    #define u_c_cast(T,v) \
        ((T)(v))  // in C, just another alias for parentheses cast
#else
    // Fixed const-preserving cast
    template<typename TP, typename VQP>
    struct ConstPreservingCastHelper {
        static_assert(std::is_pointer<TP>::value, "c_cast() non pointer!");
        static_assert(not std::is_reference<VQP>::value, "use rr_decltype()");
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

    #define c_cast(TP,v)  /* checked variation, runs v_cast() hooks [1] */ \
        (ConstAwareCastDispatcher< \
            rr_decltype(v), \
            typename ConstPreservingCastHelper<TP,rr_decltype(v)>::type \
        >::convert(v))

    #define u_c_cast(TP,v)  /* unchecked variation [2] */ \
        ((typename ConstPreservingCastHelper<TP,rr_decltype(v)>::type)(v))
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

    #define x_cast(T,v)  /* needs outer parens, see [B] */ \
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


//=//// FUNCTION POINTER CAST /////////////////////////////////////////////=//
//
// Function pointer casting is a nightmare, and there's nothing all that
// productive you could really do with it if cast() allowed you to hook it
// in terms of validating the "bits".  You can really only make it legal to
// cast from certain function pointer types to others.  Rather than making
// cast() bend itself into a pretzel to accommodate all the quirks of
// funciton pointers, this defines a separate `f_cast()`.
//
#if NO_CPLUSPLUS_11
    #define f_cast(T,v) \
        ((T)(v))  // in C, just another alias for parentheses cast
#else
    template<typename From, typename To>
    struct FunctionPointerCastHelper {
        static_assert(
            is_function_pointer<From>::value && is_function_pointer<To>::value,
            "f_cast() requires both source and target to be function pointers."
        );
        static To convert(From from) {
            return u_cast(To, from);
        }
    };

    template<typename To, typename From>
    To FunctionPointerCastDecayer(From&& from) {
        typedef typename std::decay<From>::type FromDecay;
        return FunctionPointerCastHelper<FromDecay, To>::convert(from);
    }

    #define f_cast(T, v)  FunctionPointerCastDecayer<T>(v)
#endif


//=//// BYTE STRINGS VS UNENCODED CHARACTER STRINGS ///////////////////////=//
//
// With UTF-8 Everywhere, the term "length" of a string refers to its number
// of codepoints, while "size" returns to the number of bytes (in the size_t
// sense of the word).  This makes the byte-based C function `strlen()`
// something of a misnomer.
//
// To address this issue, we define `strsize()`.  Besides having a name that
// helps emphasize it returns a byte count, it is also made polymorphic to
// accept unsigned character pointers as well as signed ones.  To do this in
// C it has to use a cast that foregoes type checking.  But the C++ build
// checks that only `const char*` and `const unsigned char*` are passed.
// (Strict aliasing rules permit casting between pointers to char types.)
//
// We also include some convenience functions for switching between char*
// and unsigned char*, from:
//
// http://blog.hostilefork.com/c-casts-for-the-masses/
//
// !!! Are these really necessary?  Should they be in %needful-casts.h?
//

#include <string.h>  // for strlen() etc, but also defines `size_t`

#if CPLUSPLUS_11
    INLINE size_t strsize(const char *cp)
      { return strlen(cp); }

    INLINE size_t strsize(const unsigned char *bp)
      { return strlen((const char*)bp); }
#else
    #define strsize(bp) \
        strlen((const char*)bp)
#endif

#if NO_RUNTIME_CHECKS
    /* These [S]tring and [B]inary casts are for "flips" between a 'char *'
     * and 'unsigned char *' (or 'const char *' and 'const unsigned char *').
     * Being single-arity with no type passed in, they are succinct to use:
     */
    #define s_cast(b)       ((char *)(b))
    #define cs_cast(b)      ((const char *)(b))
    #define b_cast(s)       ((unsigned char *)(s))
    #define cb_cast(s)      ((const unsigned char *)(s))
#else
    /* We want to ensure the input type is what we thought we were flipping,
     * particularly not the already-flipped type.  Instead of type_traits, 4
     * functions check in both C and C++ (here only during Debug builds):
     */
    INLINE unsigned char *b_cast(char *s)
      { return (unsigned char*)s; }

    INLINE const unsigned char *cb_cast(const char *s)
      { return (const unsigned char*)s; }

    INLINE char *s_cast(unsigned char *s)
      { return (char*)s; }

    INLINE const char *cs_cast(const unsigned char *s)
      { return (const char*)s; }
#endif


//=//// UPCAST AND DOWNCAST TAG DISPATCH //////////////////////////////////=//
//
// By default, if you upcast (e.g. casting from a derived class like Array to
// a base class like Flex), we do this with a very-low-cost constexpr that
// does the cast for free.  This is because every Array is-a Flex, and if you
// have an Array* in your hand we can assume you got it through a means that
// you knew it was valid.
//
// But if you downcast (e.g. from a Node* to a VarList*), then it's a riskier
// operation, so validation code is run:
//
//   https://en.wikipedia.org/wiki/Downcasting
//
// However, this rule can be bent when you need to.  If debugging a scenario
// and you suspect corruption is happening in placees an upcast could help
// locate, just comment out the optimization and run the checks for all casts.
//
// Pursuant to [D], we generally want to trust the type system when it comes
// to upcasting, and be more skeptical of downcasts...verifying the bits.
//
// To make this easier to do, this factors out the logic for determining if
// something is an upcast or downcast into a tag type.  You can then write
// two functions taking a pointer and either an UpcastTag or DowncastTag,
// and use the `WhichCastDirection<...>` to select which one to call.
//
#if CPLUSPLUS_11
    template<typename V, typename Base>
    struct IsUpcastTo : std::integral_constant<
        bool,
        std::is_base_of<Base, V>::value
    > {};

    struct UpcastTag {};
    struct DowncastTag {};

    template<typename V, typename Base>
    using WhichCastDirection = typename std::conditional<  // tag selector
        IsUpcastTo<V, Base>::value,
        UpcastTag,
        DowncastTag
    >::type;  // WhichCastDirection<...>{} will instantiate the appropriate tag
#endif


//=//// strict_..._cast(): STANDARDS-COMPLIANCE CAST //////////////////////=//
//
// Microsoft Visual C++ is stricter than GCC/Clang about which conversions
// are considered valid in template deduction and overload resolution,
// especially when user-defined conversion operators are involved.
//
// In particular, MSVC will not consider user-defined conversions (e.g. class
// with `operator T*()`) when deducing template arguments for a constructor or
// function template that takes its argument by `const U&`. GCC and Clang are
// more permissive and will allow such conversions in more contexts.
//
// This difference most often appears when using wrapper types (such as smart
// pointers or pointer-like classes) that can convert to raw pointers. In GCC,
// you can often pass such a wrapper directly to a function or constructor
// expecting a raw pointer, and the conversion will be found. In MSVC, you may
// need to explicitly cast at the callsite, or provide overloads that take the
// wrapper type directly.
//
// According to the C++ standard, MSVC's behavior is right: template argument
// deduction does not consider user-defined conversions. GCC's behavior is
// considered bad because it increases compile time and complexity by having
// to consider user-defined conversions in deduction, and creates unnecessary
// ambiguities in overload resolution.
//
// To avoid littering the codebase with explanations at every callsite, and
// to ensure that the casts are still readable, we define `strict_*_cast()`
// macros. These are no-ops on GCC/Clang, but expand to the appropriate cast
// macros (e.g. `u_cast`, `v_cast`, etc.) in standards-compilant compilers.
// This centralizes the workaround and documents the reason for its existence.
//
// (Using plain casts would run the risk of someone using GCC or Clang just
// deleting the cast when they think it isn't needed.)
//
#if defined(__GNUC__) || defined(__clang__)
    // GCC and Clang are non-conforming: user-defined conversions in deduction
    #define strict_u_cast(T,v)    (v)
    #define strict_v_cast(T,v)    (v)
    #define strict_c_cast(T,v)    (v)
    #define strict_u_c_cast(T,v)  (v)

    #define strict_cast(T,v)      (v)
#else
    // Standard-conforming (MSVC, Intel, etc.): cast needed
    #define strict_u_cast(T,v)    u_cast(T,v)
    #define strict_v_cast(T,v)    v_cast(T,v)
    #define strict_c_cast(T,v)    c_cast(T,v)
    #define strict_u_c_cast(T,v)  u_c_cast(T,v)

    #define strict_cast(T,v)      cast(T,v)  // however you defined cast()
#endif

#define strict_m_cast(T,v)  STATIC_FAIL(just_use_m_cast)
#define strict_f_cast(T,v)  STATIC_FAIL(just_use_f_cast)
#define strict_i_cast(T,v)  STATIC_FAIL(just_use_i_cast)
#define strict_p_cast(T,v)  STATIC_FAIL(just_use_p_cast)

#endif  // NEEDFUL_CASTS_H
