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
// E. The CastHook classes do not use references in their specialization.
//    So don't write:
//
//        struct CastHook<Foo<X>&, Y*>
//          { static Y* convert(Foo<X>& foo) { ... } }
//
//    It won't ever match since the reference was removed by the macro.  But
//    do note you can leave the reference on the convert method if needed:
//
//        struct CastHook<Foo<X>, Y*>  // note no reference on Foo<X>
//          { static Y* convert(Foo<X>& foo) { ... } }
//
// F. By default, most of the cast() are defined to use the runtime validation
//    hooks.  However, it's possible to easily turn them off...so that a
//    macro like cast() would not run validation.  Simply do an #include:
//
//       #include "needful/cast-hooks-off.h"
//
//    This can be used in performance-critical code, where you don't want to
//    have the overhead of the runtime cast validation...and don't want to
//    decorate all your casts with "u_" to say they are unchecked.  You can
//    turn the cast runtime back on with:
//
//       #include "needful/cast-hooks-on.h"
//


//=//// x_cast(): XTREME CAST (AKA WHAT PARENTHESES WOULD DO) /////////////=//
//
// Unhookable cast which does not offer any validation hooks.  Use e.g. when
// casting a fresh allocation to avoid triggering validation of uninitialized
// structures in debug builds.
//

/* no need to override the C definition from %needful.h here */


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
#define needful_lenient_unhookable_cast(T,expr) /* outer parens [C] */ \
    needful_xtreme_cast(needful_merge_const(decltype(expr), T), (expr))



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
            needful_unconstify_type(needful_unwrapped_type(T)) \
        >(static_cast<needful_constify_type(needful_unwrapped_type(T))>(expr))))


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
// C++ to add any runtime validation at the moment of casting.  But h_cast()
// macro is based on a `CastHook` template you can inject any validation for
// that pointer pairing that you want:
//
//    template<>
//    struct CastHook<const Number*,const Float*> {  // const pointers! [1]
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
//    struct CastHook<const Number*,const V*> {  // const pointers! [1]
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
template<typename T>
struct is_function_pointer : std::false_type {};

template<typename Ret, typename... Args>
struct is_function_pointer<Ret (*)(Args...)> : std::true_type {};


template<typename V, typename T>
struct CastHook {  // object template for partial specialization [2]
    static T convert(V v) {
        return needful_xtreme_cast(T, v);  // plain C cast is most versatile
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
typename std::enable_if<  // For ints/enums: use & as && can't bind const
    IsBasicType<From>::value and IsBasicType<To>::value,
    To
>::type
constexpr Hookable_Cast_Helper(const From& from) {
    return static_cast<To>(from);  // static_cast can be constexpr
}


template<typename To, typename From>
typename std::enable_if<  // For ints/enums: use & as && can't bind const
    (not std::is_array<needful_remove_reference(From)>::value
        and not IsBasicType<From>::value
    ) and (
        IsBasicType<To>::value
    ),
    To
>::type
Hookable_Cast_Helper(const From& from) {
    using ConstFrom = needful_constify_type(From);
    using ConstTo = needful_constify_type(To);

    static_assert(
        not (std::is_pointer<From>::value and not std::is_pointer<To>::value)
        and
        not (not std::is_pointer<From>::value and std::is_pointer<To>::value),
        "use needful_pointer_cast() [p_cast()] for pointer <-> integer casts"
    );

    return CastHook<ConstFrom, ConstTo>::convert(from);
}

template<
    typename To,
    typename FromRef,
    typename ResultType = needful_merge_const(  // lenient cast
        needful_remove_reference(FromRef), To
    )
>
typename std::enable_if<  // For arrays: decay to pointer
    std::is_array<needful_remove_reference(FromRef)>::value
        and not std::is_fundamental<To>::value
        and not std::is_enum<To>::value,
    ResultType
>::type
Hookable_Cast_Helper(FromRef const && from) {
    using From = typename std::decay<FromRef>::type;
    using ConstFrom = needful_constify_type(From);
    using ConstTo = needful_constify_type(To);

    return needful_mutable_cast(
        ResultType,  // passthru const on const mismatch (lenient)
        (CastHook<ConstFrom, ConstTo>::convert(std::forward<FromRef>(from)))
    );
}

template<
    typename To,
    typename FromRef,
    typename ResultType = needful_merge_const(  // lenient cast
        needful_remove_reference(FromRef), To
    )
>
typename std::enable_if<  // For non-arrays: forward as-is
    not std::is_array<needful_remove_reference(FromRef)>::value
        and not std::is_fundamental<To>::value
        and not std::is_enum<To>::value,
    ResultType
>::type
Hookable_Cast_Helper(FromRef&& from)
{
    using From = needful_remove_reference(FromRef);
    using ConstFrom = needful_constify_type(From);
    using ConstTo = needful_constify_type(To);

    static_assert(
        not is_function_pointer<From>::value
        and not is_function_pointer<To>::value,
        "Use f_cast() for function pointer casts"
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

    return needful_mutable_cast(
        ResultType,  // passthru const on const mismatch (lenient)
        (CastHook<ConstFrom, ConstTo>::convert(std::forward<FromRef>(from)))
    );
}

#undef needful_lenient_hookable_cast
#define needful_lenient_hookable_cast(T,expr) /* outer parens [C] */ \
    (needful::Hookable_Cast_Helper<T>(expr))  // func: universal refs [3]


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
            not needful_is_constlike(To)
            and needful_is_constlike(From)
        ),
        "rigid_hookable_cast() won't pass thru constness on mutable casts"
    );
};

#undef needful_rigid_hookable_cast
#define needful_rigid_hookable_cast(T,expr) /* outer parens [C] */ \
    (NEEDFUL_USED((needful::RigidCastAsserter< /* USED() for clang */ \
        needful_remove_reference(decltype(expr)), \
        T \
    >{})), \
    needful_lenient_hookable_cast(T, (expr)))

#undef needful_rigid_unhookable_cast
#define needful_rigid_unhookable_cast(T,expr) /* outer parens [C] */ \
    (NEEDFUL_USED((needful::RigidCastAsserter< /* USED() for clang */ \
        needful_remove_reference(decltype(expr)), \
        T \
    >{})), \
    Needful_Lenient_Unhookable_Cast(T, (expr)))


//=//// downcast(): CAST THAT WOULD BE SAFE FOR PLAIN ASSIGNMENT //////////=//
//
// Downcast behaves like what would also be called an "implicit cast", which
// is anything that would be safe if done through a normal assignment.  It's
// just a nicer, shorter name than implicit_cast())!
//
// No checking is needed (none would have been done for an assignment).
//
// It preserves the constness of the input type.
//

template<typename From, typename To>
struct DowncastHelper {
    using type = typename MirrorConstHelper<From, To>::type;

    static_assert(
        std::is_convertible<From, type>::value,
        "downcast() cannot implicitly convert expression to type T"
    );
};

#undef needful_downcast
#define needful_downcast(T,expr) \
    ((typename needful::DowncastHelper< \
        needful_remove_reference(decltype(expr)), \
        T \
    >::type)(expr))


//=//// upcast(): SINGLE-ARITY CAST THAT ONLY ALLOWS UPCASTING ///////////=//
//
// A technique that Needful codebases can use is to use inheritance of types
// when built as C++, but not when built as C.  The C build can define the
// "base class" just by void*, and the C++ build can define base classes
// using the empty base class optimization.
//
// We can introduce some safety in these casting hierarchies while still
// getting brevity.
//

template<typename From>
struct UpcastWrapper {
    From p;
    explicit constexpr UpcastWrapper(const From& from) : p {from} {}

    template<
        typename To,
        typename = typename std::enable_if<
            std::is_convertible<To, From>::value
        >::type
    >
    operator To() const noexcept
        { return needful_lenient_hookable_cast(To, p); }
};

#undef needful_upcast
#define needful_upcast(expr) \
    (needful::UpcastWrapper< \
        needful_remove_reference(decltype(expr)) \
    >(expr))


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
// funciton pointers, this defines a separate `f_cast()`.
//

template<typename From, typename To>
struct FunctionPointerCastHelper {
    using FromDecayed = typename std::decay<From>::type;
    using ToDecayed = typename std::decay<To>::type;

    static_assert(
        is_function_pointer<FromDecayed>::value
        and is_function_pointer<ToDecayed>::value,
        "f_cast() requires both source and target to be function pointers."
    );
    static To convert(From from) {
        return x_cast(To, from);
    }
};

template<typename To, typename From>
To FunctionPointerCastDecayer(From&& from) {
    typedef typename std::decay<From>::type FromDecay;
    return FunctionPointerCastHelper<FromDecay, To>::convert(from);
}

#undef f_cast
#define f_cast(T,expr)  needful::FunctionPointerCastDecayer<T>(expr)


//=//// UPCAST AND DOWNCAST TAG DISPATCH //////////////////////////////////=//
//
// By default, if you upcast (e.g. casting from a derived class like Array to
// a base class like Flex), we do this with a very-low-cost constexpr that
// does the cast for free.  This is because every Array is-a Flex, and if you
// have an Array* in your hand we can assume you got it through a means that
// you knew it was valid.
//
// But if you downcast (e.g. from a Base* to a VarList*), then it's a riskier
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
