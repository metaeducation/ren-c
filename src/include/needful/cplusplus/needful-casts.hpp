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
// This methodology is an evolution of code from this 2015 blog article:
//
//  http://blog.hostilefork.com/c-casts-for-the-masses/
//
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
// E. The CastHook classes do not use references in their specialization.
//    So don't write:
//
//        struct CastHook<Foo<X>&, Y*>
//          { static void Validate_Bits(Foo<X>& foo) { ... } }
//
//    It won't ever match since the reference was removed by the macro.  But
//    do note you can leave the reference on the Validate_Bits arg if needed:
//
//        struct CastHook<Foo<X>, Y*>  // note no reference on Foo<X>
//          { static void Validate_Bits(Foo<X>& foo) { ... } }
//


//=//// x_cast(): XTREME CAST (AKA WHAT PARENTHESES WOULD DO) /////////////=//
//
// Unhookable cast which does not offer any validation hooks.  Use e.g. when
// casting a fresh allocation to avoid triggering validation of uninitialized
// structures in debug builds.
//

/* no need to override the C definition from %needful.h here */


//=//// VA_LIST POINTER CAST //////////////////////////////////////////////=//
//
// 1. `const va_list*` MAY be illegal, and va_list COULD possibly be any type
//    (including fundamentals like `char`), the generic machinery behind
//    cast() could be screwed up if you ever use va_list* with it.  We warn
//    you to use v_cast() if possible--but it's not always possible to warn,
//    since it might look like a completely mundane type.  :-(
//
//    (Good argument for building your code with more than one compiler...)
//

template<typename From, typename To>
struct ValistCastBlocker {  // non-valist-casts use to stop valist casts [1]
  #if (! NEEDFUL_DONT_INCLUDE_STDARG_H)  // included by default for check
    static_assert(
        (  // v-- we can't warn you about va_list* cast() if this is true
            std::is_pointer<va_list>::value
            and std::is_fundamental<remove_pointer_t<va_list>>::value
        )
        or (  // v-- but if it's a struct or something we can warn you
            not std::is_same<From, va_list*>::value
            and not std::is_same<To, va_list*>::value
        ),
        "only legal va_list casts are v_cast() mutable va_list* <-> void*"
    );
  #endif
};

template<typename From, typename To>
struct ValistPointerCastHelper {
    using FromDecayed = decay_t<From>;
    using ToDecayed = decay_t<To>;

  #if (! NEEDFUL_DONT_INCLUDE_STDARG_H)  // included by default for check
    static_assert(
        (std::is_same<FromDecayed, va_list*>::value
            and std::is_same<ToDecayed, void*>::value)
        or (std::is_same<FromDecayed, void*>::value
            and std::is_same<ToDecayed, va_list*>::value),
        "v_cast() can only convert va_list* <-> void*"
    );
  #endif

    using type = ToDecayed;
};

#undef needful_valist_cast
#define needful_valist_cast(T,expr) \
    (reinterpret_cast< \
        needful::ValistPointerCastHelper<decltype(expr),T>::type \
    >(expr))


//=//// u_cast(): UNHOOKABLE CONST-PRESERVING CAST ////////////////////////=//
//
// This cast is useful for defining macros that want to mirror the constness
// of the input pointer, when you don't know if the caller is passing a
// const or mutable pointer in.  The C build will always give you a mutable
// pointer back, so you have to rely on the C++ build for const enforcement.
//
// It can also be nice as a shorthand, even if you know the input is const:
//
//     const Float* Const_Number_To_Float(const Number* n) {
//         return cast(Float*, n);  // briefer than `cast(const Float*, n)`
//     }
//

#undef needful_lenient_unhookable_cast
#define needful_lenient_unhookable_cast(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::ValistCastBlocker<decltype(expr), T>), \
        needful_xtreme_cast(needful_merge_const_t(decltype(expr), T), (expr)))



//=//// m_cast(): MUTABILITY CAST /////////////////////////////////////////=//
//
// The mutability cast works on regular types, but also on wrapped types...
// so long as the wrapping struct has a static field called `::wrapped_type`.
// (See %needful-wrapping.hpp for more information on this convention.)
//
// m_cast() is stylized so that it does not need to invoke any functions,
// because even constexpr functions have cost in debug builds.  If the type
// you are casting is not a wrapper, it should be able to do its work
// entirely at compile-time...even in debug builds!  This is accomplished
// by static casting the wrapper type to its extracted type, then running a
// const_cast on that type, then static casting again to the target type.
// This can be made to work for both pointers and wrapped pointers.
//
// 1. Attempts to make m_cast() arity-1 and auto-detect the target type were
//    tried, with the C version just casting to void*.  But this winds up
//    requiring C++-specific code to leak into %needful.h - because the C
//    version of m_cast() that creates void* would no longer be legal in C++.
//    This goes against the design principle of Needful to have a baseline of
//    building with a single header file of C-only definitions.
//

#undef needful_mutable_cast
#define needful_mutable_cast(T,expr) /* Note: not arity-1 on purpose! */ \
    (static_cast<T>( \
        const_cast< \
            needful_unconstify_t(needful_unwrapped_type(T)) \
        >(static_cast<needful_constify_t(needful_unwrapped_type(T))>(expr))))


//=//// h_cast(): HOOKABLE CAST, IDEALLY cast() = h_cast() [A] ////////////=//
//
// This is the form of hookable cast you should generally reach for.  Default
// hooks are provided for pointer-to-pointer, or integral-to-integral.
//
// USAGE:
//    T result = h_cast(T, value);
//
// BEHAVIOR:
// - For arithmetic/enum types: static_cast if not explicitly convertible
// - For pointer-to-pointer: reinterpret_cast if not explicitly convertible
// - For explicitly convertible types: static_cast
//
// CUSTOMIZATION:
// To hook the cast, you define `CastHook` for the types you are interested
// in hooking. Example specialization:
//
//    template<>
//    struct CastHook<SourceType, TargetType> {
//        static void Validate_Bits(SourceType value) {
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
// C++ to add any runtime validation at the moment of casting.  But h_cast()
// macro is based on a `CastHook` template you can inject any validation for
// that pointer pairing that you want:
//
//    template<>
//    struct CastHook<const Number*,const Float*> {  // const pointers! [1]
//        static void Validate_Bits(const Number* num) {
//            assert(num->is_float);
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
//    struct CastHook<const Number*,const V*> {  // const pointers! [1]
//        static void Validate_Bits(const V* v) {
//            static_assert(std::is_same<V, Float>::value, "Float expected");
//            assert(num->is_float);
//        }
//    };
//
// 1. For pointer types, the system consolidates the dispatch mechanism based
//    on const pointers.  This is the natural choice for the general mechanic
//    because it is the most constrained (mutable pointers can be made const,
//    and have const methods called on them, not necessarily vice versa).
//    Hence mutable casts from Number* to Float* above runs the same code,
//    while returning the correct mutable output from the h_cast()...and it
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

template<typename>
struct IsResultWrapper : std::false_type {};

template<typename T>
struct ResultWrapper;


template<typename V, typename T>
struct CastHook {  // object template for partial specialization [2]
    static void Validate_Bits(V v) {
        UNUSED(v);
        return;
    }
};

template<typename T>
struct IsBasicType {
    static constexpr bool value = std::is_fundamental<T>::value
        or std::is_enum<T>::value
        or (HasWrappedType<T>::value and (
            std::is_fundamental<needful_unwrapped_type(T)>::value
            or std::is_enum<needful_unwrapped_type(T)>::value
        ));
};

template<typename To, typename From>
enable_if_t<  // For ints/enums: use & as && can't bind const
    IsBasicType<From>::value and IsBasicType<To>::value,
    To
>
constexpr Hookable_Cast_Helper(const From& from) {
    return static_cast<To>(from);  // static_cast can be constexpr
}


template<typename To, typename From>
enable_if_t<  // For ints/enums: use & as && can't bind const
    (not std::is_array<remove_reference_t<From>>::value
        and not IsBasicType<From>::value
    ) and (
        IsBasicType<To>::value
    ),
    To
>
Hookable_Cast_Helper(const From& from) {
    using ConstTo = needful_constify_t(To);

    static_assert(
        not (std::is_pointer<From>::value and not std::is_pointer<To>::value)
        and
        not (not std::is_pointer<From>::value and std::is_pointer<To>::value),
        "use needful_pointer_cast() [p_cast()] for pointer <-> integer casts"
    );

  #if NEEDFUL_CAST_CALLS_HOOKS
    using ConstFrom = needful_constify_t(From);
    CastHook<ConstFrom, ConstTo>::Validate_Bits(from);
  #endif

    return needful_xtreme_cast(ConstTo, from);
}

template<
    typename To,
    typename FromRef,
    typename ResultType = needful_merge_const_t(  // lenient cast
        remove_reference_t<FromRef>, To
    )
>
enable_if_t<  // For arrays: decay to pointer
    std::is_array<remove_reference_t<FromRef>>::value
        and not std::is_fundamental<To>::value
        and not std::is_enum<To>::value,
    ResultType
>
Hookable_Cast_Helper(const FromRef & from) {
    using ConstTo = needful_constify_t(To);

  #if NEEDFUL_CAST_CALLS_HOOKS
    using From = decay_t<FromRef>;
    using ConstFrom = needful_constify_t(From);
    CastHook<ConstFrom, ConstTo>::Validate_Bits(std::forward<FromRef>(from));
  #endif

    return needful_mutable_cast(
        ResultType,  // passthru const on const mismatch (lenient)
        needful_xtreme_cast(ConstTo, from)
    );
}

template<
    typename To,
    typename FromRef,
    typename ResultType = needful_merge_const_t(  // lenient cast
        remove_reference_t<FromRef>, To
    )
>
enable_if_t<  // For non-arrays: forward as-is
    not std::is_array<remove_reference_t<FromRef>>::value
    and not IsResultWrapper<remove_reference_t<FromRef>>::value
        and not std::is_fundamental<To>::value
        and not std::is_enum<To>::value,
    ResultType
>
Hookable_Cast_Helper(FromRef&& from)
{
    using From = remove_reference_t<FromRef>;
    using ConstTo = needful_constify_t(To);

    static_assert(
        not is_function_pointer<From>::value
        and not is_function_pointer<To>::value,
        "Use f_cast() for function pointer casts"
    );

  #if NEEDFUL_CAST_CALLS_HOOKS
    using ConstFrom = needful_constify_t(From);
    CastHook<ConstFrom, ConstTo>::Validate_Bits(std::forward<FromRef>(from));
  #endif

    return needful_mutable_cast(
        ResultType,  // passthru const on const mismatch (lenient)
        needful_xtreme_cast(ConstTo, std::forward<FromRef>(from))
    );
}


template<
    typename To,
    typename FromWrapperRef,
    typename ResultType = ResultWrapper<needful_merge_const_t(  // lenient cast
        remove_reference_t<FromWrapperRef>, To
    )>
>
enable_if_t<
    IsResultWrapper<remove_reference_t<FromWrapperRef>>::value,
    ResultWrapper<To>
>
Hookable_Cast_Helper(const FromWrapperRef& from_wrapper)
{
    using From = typename remove_reference_t<FromWrapperRef>::wrapped_type;
    using ConstTo = needful_constify_t(To);

    static_assert(
        not is_function_pointer<From>::value
        and not is_function_pointer<To>::value,
        "Use f_cast() for function pointer casts"
    );

  #if NEEDFUL_CAST_CALLS_HOOKS
    using ConstFrom = needful_constify_t(From);
    CastHook<ConstFrom, ConstTo>::Validate_Bits(from_wrapper.r);
  #endif

    return needful_mutable_cast(
        ResultType,  // passthru const on const mismatch (lenient)
        needful_xtreme_cast(ConstTo, from_wrapper.r)
    );
}

#undef needful_lenient_hookable_cast
#define needful_lenient_hookable_cast(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::ValistCastBlocker<decltype(expr), T>), \
        needful::Hookable_Cast_Helper<T>(expr))  // func: universal refs [3]


//=//// RIGID CAST FORMS /////////////////////////////////////////////////=//
//
// By default, cast() and u_cast() are lenient, meaning if you try to cast
// a const expression to a mutable type, rather than giving a compile-time
// error, the expression will be cast but with constness applied to the
// output type.  This is useful for many cases (such as making a polymorphic
// macro that can make either a const or mutable pointer based on the
// input expression.)  But if you really want to ensure that when you target
// a mutable type you get a mutable expression, the "Rigid" forms of the
// cast macros add a static assert to the non-rigid forms that stops them
// from doing their passthru behavior.
//

template<typename From, typename To>
struct RigidCastAsserter {
    static_assert(
        not (
            not needful_is_constlike_v(To)
            and needful_is_constlike_v(From)
        ),
        "rigid_hookable_cast() won't pass thru constness on mutable casts"
    );
};

#undef needful_rigid_hookable_cast
#define needful_rigid_hookable_cast(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::RigidCastAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        T \
    >), \
    needful_lenient_hookable_cast(T, (expr)))

#undef needful_rigid_unhookable_cast
#define needful_rigid_unhookable_cast(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::RigidCastAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        T \
    >), \
    needful_lenient_unhookable_cast(T, (expr)))


//=//// upcast(): CAST THAT WOULD BE SAFE FOR PLAIN ASSIGNMENT ///////////=//
//
// Upcast behaves like what would also be called an "implicit cast", which
// is anything that would be safe if done through a normal assignment.  It's
// just a nicer, shorter name than implicit_cast())!
//
// No checking is needed (none would have been done for an assignment).
//
// It preserves the constness of the input type.
//

template<typename From, typename To>
struct UpcastHelper {
    using type = needful_mirror_const_t(From, To);

    static_assert(
        needful_is_convertible_v(From, type),
        "upcast() cannot implicitly convert expression to type T"
    );
};

#undef needful_upcast
#define needful_upcast(T,expr) \
    ((typename needful::UpcastHelper< \
        needful::remove_reference_t<decltype(expr)>, \
        T \
    >::type)(expr))


//=//// downcast(): SINGLE-ARITY CAST THAT ONLY ALLOWS DOWNCASTING ////////=//
//
//   https://en.wikipedia.org/wiki/Downcasting
//
// A technique that Needful codebases can use is to use inheritance of types
// when built as C++, but not when built as C.  The C build can define the
// "base class" just by void*, and the C++ build can define base classes
// using the empty base class optimization.
//
// We can introduce some safety in these casting hierarchies while still
// getting brevity.
//
// 1. Sadly, every method for trying to parameterize the DowncastHolder with
//    either hookable casting or unhookable casting leads to either uglier
//    incomprehensible code, or added runtime cost in debug builds (or both!)
//    Using a macro to do the work seems the best way.
//
// 2. Although it's called "downcast" it can actually be used with things
//    that aren't classes, e.g. you can downcast a void* to an int*.  If
//    the reverse conversion is legal, that's what's allowed.
//
// 3. By making DowncastHolder [[nodiscard]], it's safe to use with things
//    like trap() and except():
//
//       trap (Derived* derived = downcast(base_ptr));
//
// 4. See remarks in Option(T) about why + is used here to combine better
//    with the % used by Result(T) extraction, and why << is avoided due to
//    some compilers issuing warnings.  The same issues apply to downcast.
//

#define NEEDFUL_DEFINE_DOWNCAST_HELPERS(BaseName, hookability) /* ugh [1] */ \
    template<typename From> \
    struct NEEDFUL_NODISCARD BaseName##Holder { /* NODISCARD needed [3] */ \
        From f; /* trivially constructible only (fastest for debug) */\
        \
        BaseName##Holder() = delete; /* to trivially construct w/Result(T) */ \
        \
        template< /* "downcast" means "reverse is_convertible" [2] */ \
            typename To, \
            typename = enable_if_t<needful_is_convertible_v(To, From)> \
        > \
        operator To() const \
            { return needful_lenient_##hookability##_cast(To, f); } \
    }; \
    \
    struct BaseName##Maker { \
        template<typename T> \
        BaseName##Holder<remove_reference_t<T>> \
        operator+(const T& value) const { /* << lower than % [4] */ \
            return BaseName##Holder<T>{value}; \
        } \
    }; \
    \
    constexpr BaseName##Maker g_##BaseName##_maker{}

NEEDFUL_DEFINE_DOWNCAST_HELPERS(HookableDowncast, hookable);
NEEDFUL_DEFINE_DOWNCAST_HELPERS(UnhookableDowncast, unhookable);

#undef NEEDFUL_DEFINE_DOWNCAST_HELPERS  // macro need not leak

#undef needful_hookable_downcast
#define needful_hookable_downcast \
    needful::g_HookableDowncast_maker +  // + lower than % [4]

#undef needful_unhookable_downcast
#define needful_unhookable_downcast \
    needful::g_UnhookableDowncast_maker +  // + lower than % [4]


//=//// NON-POINTER TO POINTER CAST ////////////////////////////////////////=//
//
// If your intent is to turn a non-pointer into a pointer, this identifies
// that as the purpose of the cast.  The C++ build can confirm that is what
// is actually being done.
//

template<typename TP, typename V>
constexpr TP PointerCastHelper(V v) {
    static_assert(std::is_pointer<TP>::value,
        "invalid p_cast() - target type must be pointer");
    static_assert(not std::is_pointer<V>::value,
        "invalid p_cast() - source type can't be pointer");
    return reinterpret_cast<TP>(v);
}

#undef needful_pointer_cast
#define needful_pointer_cast(TP,expr) \
    needful::PointerCastHelper<TP>(expr)


//=//// NON-INTEGRAL TO INTEGRAL CAST /////////////////////////////////////=//
//
// If your intent is to turn a non-integral into an integral, this identifies
// that as the purpose of the cast.  The C++ build can confirmthat is what
// is actually being done.
//

template<typename T, typename V>
constexpr T IntegralCastHelper(V v) {
    static_assert(std::is_integral<T>::value,
        "invalid i_cast() - target type must be integral");
    static_assert(not std::is_integral<V>::value,
        "invalid i_cast() - source type can't be integral");
    return reinterpret_cast<T>(v);
}

#undef needful_integral_cast
#define needful_integral_cast(T,expr) \
    needful::IntegralCastHelper<T>(expr)


//=//// FUNCTION POINTER CAST /////////////////////////////////////////////=//
//
// Function pointer casting is a nightmare, and there's nothing all that
// productive you could really do with it if cast() allowed you to hook it
// in terms of validating the "bits".  You can really only make it legal to
// cast from certain function pointer types to others.  Rather than making
// cast() bend itself into a pretzel to accommodate all the quirks of
// function pointers, this defines a separate `f_cast()`.
//
// Stylized so that it costs nothing at runtime, even in debug builds.
//

template<typename From, typename To>
struct FunctionPointerCastHelper {
    using FromDecayed = decay_t<From>;
    using ToDecayed = decay_t<To>;

    static_assert(
        is_function_pointer<FromDecayed>::value
            and is_function_pointer<ToDecayed>::value,
        "f_cast() requires both source and target to be function pointers"
    );

    using type = ToDecayed;
};

#undef needful_function_cast
#define needful_function_cast(T,expr) \
    (reinterpret_cast< \
        needful::FunctionPointerCastHelper<decltype(expr),T>::type \
    >(expr))
