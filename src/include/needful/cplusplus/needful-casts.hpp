//
//  file: %needful-casts.hpp
//  summary: "Cast macros with added features when built as C++11 or higher"
//  homepage: <needful homepage TBD>
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2026 hostilefork.com
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
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. Most casts use a static method of a templated struct vs. a templated
//    function.  This is because partial specialization of function templates
//    is not legal in C++ due to the fact that functions can be overloaded,
//    while structs and classes can't:
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
//    The exception is h_cast() which requires && for "universal references".
//    Hence Hookable_Cast_Helper is a function, not a struct.
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
#if NEEDFUL_DONT_INCLUDE_STDARG_H
    template<typename From, typename To>
    struct ValistCastBlocker {};  // no-op

    // leave needful_valist_cast() as plain cast
#else
    template<typename From, typename To>
    struct ValistCastBlocker {  // non-valist-casts stop valist casts [1]
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
    };

    template<typename From, typename To>
    struct ValistPointerCastHelper {
        using FromDecayed = decay_t<From>;
        using ToDecayed = decay_t<To>;

        static_assert(
            (std::is_same<FromDecayed, va_list*>::value
                and std::is_same<ToDecayed, void*>::value)
            or (std::is_same<FromDecayed, void*>::value
                and std::is_same<ToDecayed, va_list*>::value),
            "v_cast() can only convert va_list* <-> void*"
        );

        using type = ToDecayed;
    };

    #undef needful_valist_cast
    #define needful_valist_cast(T,expr) \
        (reinterpret_cast< \
            needful::ValistPointerCastHelper<decltype(expr),T>::type \
        >(expr))
#endif


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
// by needful_upcast-ing the wrapper type to its extracted type, then running a
// const_cast on that type, then static casting again to the target type.
// This can be made to work for both pointers and wrapped pointers.
//
// 1. It's possible for m_cast() to be arity-1 and auto-detect the type, with
//    the C version just casting to void*.  That would drop all type checking
//    in the C build (bad), and would require pulling some pretty big C++
//    mechanics into the needful.h header for it to compile.  The compromise
//    of making it arity-2 but able to do an arbitrary upcast is a good middle
//    ground that makes it more useful.
//

#undef needful_mutable_cast
#define needful_mutable_cast(T,expr) /* not arity-1 on purpose! [1] */ \
    (static_cast<T>(const_cast< \
        needful_unconstify_t(needful_unwrapped_type(T)) \
    >(needful_upcast(needful_constify_t(needful_unwrapped_type(T)), (expr)))))


//=//// CastHook DEFINITION ///////////////////////////////////////////////=//
//
// To hook a cast, you define `CastHook` for the types you are interested
// in hooking. Example specialization:
//
//    template<>
//    struct CastHook<SourceType, TargetType> {
//        static void Validate_Bits(SourceType value) {
//            assert(some_condition(value));  // your check here
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
// 3. The CastHook classes do not use references in their specialization.
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

template<typename V, typename T>
struct CastHook {  // object template for partial specialization [2]
    static void Validate_Bits(V v) {
        UNUSED(v);
        return;
    }
};


//=//// h_cast(): HOOKABLE CAST, IDEALLY cast() = h_cast() ////////////////=//
//
// This is the form of hookable cast you should generally reach for.
//
// USAGE:
//    T result = h_cast(T, value);
//
// The five overloads of Hookable_Cast_Helper are:
//
//   1. basic -> basic: constexpr static_cast (fast path, no hooks)
//   2. non-basic non-rewrappable -> basic: hooked, pointer<->int rejected
//   3. non-rewrappable -> non-basic: general pointer/class/array casts
//   4. semantic rewrappable -> re-wrapped: preserves Option/Result
//   5. non-semantic rewrappable -> extracted: strips Sink/Need/Exact/etc.
//
// Non-template wrappers (plain structs like Heart/Type that expose
// `wrapped_type` for introspection but aren't template instantiations)
// are NOT rewrappable, so they route through overloads 1-3 like regular
// types--their own conversion operators handle the casting naturally.
//


//=//// OVERLOAD 1: basic -> basic (int, enum, bool) //////////////////////=//
//
// Why it exists: Fundamentals and enums can bind to `const&` which allows
// constexpr, and static_cast between them is always valid.  This is the
// fast path that avoids reinterpret_cast entirely.  Rewrappable wrappers
// (e.g. Need(int)) are routed to overloads 4/5 instead.
//
template<typename To, typename From>
enable_if_t<
    IsBasicType<From>::value and IsBasicType<To>::value
        and not IsRewrappable<From>::value,  // rewrappable -> overload 4/5
    To
>
constexpr Hookable_Cast_Helper(const From& from) {
    return static_cast<To>(from);  // static_cast can be constexpr
}


//=//// OVERLOAD 2: non-basic non-wrapped -> basic ////////////////////////=//
//
// Why it exists: Catches casts where the source is non-basic (pointer, class)
// but the target is basic (int, enum, bool).  Can't use universal references
// (overload 3) because the target is basic, and can't merge with overload 1
// because the source isn't basic.  A key role is rejecting pointer<->integer
// casts with a clear static_assert directing you to i_cast()/p_cast().
//
template<typename To, typename From>
enable_if_t<
    (not std::is_array<remove_reference_t<From>>::value
        and not IsBasicType<From>::value
        and not IsRewrappable<From>::value  // rewrappable -> overload 4/5
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


//=//// OVERLOAD 3: non-wrapped -> non-basic (pointers, classes, arrays) //=//
//
// Why it exists: The general-purpose overload for unwrapped types where the
// target is non-basic (typically pointer-to-pointer casts).  Uses universal
// references (FromRef&&) so arrays like `unsigned char[64]` deduce correctly
// as references.  This is where CastHook validation fires for ordinary
// pointer casts, enabling debug-build checks (e.g. verifying a Number*
// really is a Float* before downcasting).
//
// 1. Arrays (e.g. `unsigned char[64]`) were once handled by a separate
//    overload taking `const FromRef&`.  They are now handled uniformly by
//    the universal reference overload (`FromRef&&`), which deduces arrays
//    as references (e.g. `FromRef = int(&)[10]`).  The only difference in
//    the body is that CastHook dispatch uses `decay_t` for arrays (to get
//    the pointer type) vs. `needful_unwrapped_if_wrapped_type` otherwise.
//
template<
    typename To,
    typename FromRef,
    typename ResultType = needful_merge_const_t(  // lenient cast
        remove_reference_t<FromRef>, To
    )
>
enable_if_t<
    not IsRewrappable<remove_reference_t<FromRef>>::value
        and not std::is_fundamental<To>::value
        and not std::is_enum<To>::value,
    ResultType
>
Hookable_Cast_Helper(FromRef&& from)  // && is why helper is a function! [A]
{
    using From = remove_reference_t<FromRef>;
    using ConstTo = needful_constify_t(To);

    static_assert(
        not is_function_pointer<From>::value
        and not is_function_pointer<To>::value,
        "Use f_cast() for function pointer casts"
    );

  #if NEEDFUL_CAST_CALLS_HOOKS
    using HookFrom = conditional_t<  // arrays must decay [1]
        std::is_array<From>::value,
        decay_t<From>,
        From
    >;
    using ConstFrom = needful_constify_t(HookFrom);
    CastHook<ConstFrom, ConstTo>::Validate_Bits(static_cast<ConstFrom>(from));
  #endif

    return needful_mutable_cast(
        ResultType,  // passthru const on const mismatch (lenient)
        needful_xtreme_cast(ConstTo, std::forward<FromRef>(from))
    );
}


//=//// OVERLOAD 4: semantic wrapper -> re-wrapped result /////////////////=//
//
// Why it exists: Semantic wrappers (Option, Result) carry meaning beyond the
// raw type—their wrapping is part of the data's contract.  Cast must not
// silently strip them.  Instead, the inner value is cast and re-wrapped in
// the same template: `cast(Stump*, Result(Stub*))` -> `Result(Stump*)`.
// This is essential for patterns like `require(cast(T, result_expr))` where
// the Result wrapper must survive for `%` extraction, and for Option where
// engaged/disengaged state must be preserved.
//
template<
    typename To,
    typename FromWrapperRef,
    typename From = remove_reference_t<FromWrapperRef>,
    typename InnerFrom = needful_unwrapped_type(From),
    typename CastResult = needful_merge_const_t(  // lenient cast
        InnerFrom, To
    ),
    typename ResultType = needful_rewrap_type(From, CastResult)
>
enable_if_t<
    IsRewrappable<From>::value
        and IsWrapperSemantic<From>::value,
    ResultType
>
Hookable_Cast_Helper(const FromWrapperRef& from_wrapper)
{
    using ConstTo = needful_constify_t(To);

    static_assert(
        not is_function_pointer<InnerFrom>::value
        and not is_function_pointer<To>::value,
        "Use f_cast() for function pointer casts"
    );

    const InnerFrom& inner = NEEDFUL_EXTRACT_INNER(InnerFrom, from_wrapper);

  #if NEEDFUL_CAST_CALLS_HOOKS
    using ConstFrom = needful_constify_t(InnerFrom);  // hooks see raw type
    CastHook<ConstFrom, ConstTo>::Validate_Bits(inner);
  #endif

    CastResult cast_inner = needful_mutable_cast(  // rewrap, keep semantics
        CastResult,
        needful_xtreme_cast(ConstTo, inner)
    );
    return ResultType{cast_inner};
}


//=//// OVERLOAD 5: non-semantic wrapper -> extracted result ///////////////=//
//
// Why it exists: Non-semantic wrappers (Sink, Need, Exact, Contra, etc.)
// describe parameter-passing conventions, not data state.  Once you cast,
// you're done with the convention, so cast strips the wrapper and returns
// the raw inner value.  The inner value is accessed via reinterpret_cast on
// the standard-layout first member, bypassing conversion operators that may
// have side effects (e.g. Sink's corruption semantics).
//
template<
    typename To,
    typename FromWrapperRef,
    typename From = remove_reference_t<FromWrapperRef>,
    typename InnerFrom = needful_unwrapped_type(From),
    typename ResultType = needful_merge_const_t(  // lenient cast
        InnerFrom, To
    )
>
enable_if_t<
    IsRewrappable<From>::value
        and not IsWrapperSemantic<From>::value,
    ResultType
>
Hookable_Cast_Helper(const FromWrapperRef& from_wrapper)
{
    using ConstTo = needful_constify_t(To);

    static_assert(
        not is_function_pointer<InnerFrom>::value
        and not is_function_pointer<To>::value,
        "Use f_cast() for function pointer casts"
    );

    const InnerFrom& inner = NEEDFUL_EXTRACT_INNER(InnerFrom, from_wrapper);

  #if NEEDFUL_CAST_CALLS_HOOKS
    using ConstFrom = needful_constify_t(InnerFrom);  // hooks see raw type
    CastHook<ConstFrom, ConstTo>::Validate_Bits(inner);
  #endif

    return needful_mutable_cast(
        ResultType,  // passthru const on const mismatch (lenient)
        needful_xtreme_cast(ConstTo, inner)
    );
}

#undef needful_lenient_hookable_cast
#define needful_lenient_hookable_cast(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::ValistCastBlocker<decltype(expr), T>), \
        needful::Hookable_Cast_Helper<T>(expr))  // func: universal refs [A]


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
        operator+(const T& value) const { /* + lower than % [4] */ \
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


//=//// INTEGER-LIKE CAST /////////////////////////////////////////////////=//
//
// NOTE: Usually i_cast() acts like just a C-style cast, and ii_cast() is
// mapped to this cast behavior.  It's because using this cast for all simple
// integer casts in every build slows it down *dramatically*.  See %needful.h
// for more information, and only use ii_cast() when you know you are
// dealing with a wrapper...most useful on hot paths.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// i_cast(T, expr) signals "both sides of this conversion are conceptually
// integer-like data".  Its goal is zero runtime cost in debug builds, and
// because it sidesteps Needful wrappers it can actually be faster than a
// C-style cast (though Needful wrappers are debug-only, so this is only a
// concern in debug builds... C casts won't run conversions when the wrapper
// is not there)
//
//     int n = i_cast(int, some_enum);    // enum -> int
//     Byte b = i_cast(Byte, big_int);    // narrowing int -> int
//     int n = i_cast(int, opt_enum);     // Option(Enum) -> int (unwraps)
//     Heart h = i_cast(Heart, byte);     // int -> wrapper class
//
// WHY NOT JUST USE cast()?
//
// cast() is a function (Hookable_Cast_Helper), not a macro expression.
// Even in simple cases like `cast(int, some_enum)`, the compiler emits a
// function call in unoptimized debug builds.  This matters in hot paths
// where integer constants are converted thousands of times per second.
//
// cast() *must* be a function because it relies on universal references
// (&&) for array deduction, needs multiple overloads for wrapper semantics
// (rewrap-vs-extract), and calls CastHook::Validate_Bits for runtime
// validation.  None of that machinery can collapse to a bare cast
// expression.  i_cast can, because integer-like conversions don't need
// any of it--the C-style cast does the right thing directly.
//
// WHAT GUARANTEES DOES i_cast ACTUALLY MAKE?
//
// For raw int/enum sources, the guarantee is real: the static_asserts reject
// pointers (use p_cast), function pointers (use f_cast), and non-convertible
// class types.  For Needful wrappers, it additionally verifies the deeply
// unwrapped leaf is integral/enum (needful wrappers are not supposed to have
// any more "runtime truth" than their raw types).
//
// For non-Needful class types, i_cast relaxes to "convertible": if either
// side is a class constructible/convertible from the other, the cast is
// allowed.  This means class-to-class casts slip through on convertibility
// alone.  That's a deliberate trade-off: i_cast won't break when a plain
// integer gets wrapped in a non-Needful class later.  At that point i_cast
// becomes commentary--"this is integer-like"--rather than a hard enforcement.
// That's acceptable; the alternative (refusing class targets and forcing
// everything to x_cast) gives i_cast() poor invariants.
//
// IMPLEMENTATION NOTES:
//
// - IntegerCastHelper<T> is a single-type struct (not paired <To,From>), so
//   the compiler instantiates O(N+M) of them instead of O(N*M).  This avoids
//   the combinatoric explosion that a paired IntegerCastHelper<To,From> would
//   cause across pervasive use sites.
//
// - NEEDFUL_MATERIALIZE_PRVALUE(expr) binds expr to a same-type const
//   ref and takes its address.  This forces temporary materialization
//   so even prvalues get a stack location we can pointer-reinterpret.
//   The outer *x_cast(const ExtractType*, ...) dereferences through
//   the reinterpreted pointer—zero cost, no conversion operator call.
//

template<typename T>
struct IntegerCastHelper {
    using type = needful_deeply_unwrapped_type(T);

    static_assert(
        std::is_integral<type>::value or std::is_enum<type>::value
            or std::is_class<type>::value,  // class fair game, if they convert
        "invalid i_cast() - source must be integer, enum, or class type"
    );
};

#define Needful_Integer_Cast_Validate(T) \
    NEEDFUL_DUMMY_INSTANCE(needful::IntegerCastHelper<T>::type)

#undef needful_integer_cast
#define needful_integer_cast(T,expr) \
    (Needful_Integer_Cast_Validate(T), \
    needful_xtreme_cast(T, \
        *needful_xtreme_cast( \
            const typename needful::IntegerCastHelper< \
                needful::remove_reference_t<decltype(expr)> \
            >::type *, \
            NEEDFUL_MATERIALIZE_PRVALUE(expr))))


//=//// POINTER CAST //////////////////////////////////////////////////////=//
//
// p_cast(TP, expr) is for conversions where exactly one side is a pointer
// and the other is not (int -> pointer or pointer -> int).  Zero runtime
// cost: struct validates at compile time, macro body is a bare C-style cast.
//
//     int i = p_cast(int, some_ptr);       // pointer -> int
//     Foo* p = p_cast(Foo*, some_intptr);   // int -> pointer
//

template<typename To, typename From>
struct PointerCastHelper {
    static constexpr bool to_is_pointer =
        std::is_pointer<To>::value or std::is_array<To>::value
            or std::is_same<std::nullptr_t, To>::value;
    static constexpr bool from_is_pointer =
        std::is_pointer<From>::value or std::is_array<From>::value
            or std::is_same<std::nullptr_t, From>::value;

    static_assert(
        to_is_pointer != from_is_pointer,
        "invalid p_cast() - exactly one side must be a pointer (or array)"
    );
};

#undef needful_pointer_cast
#define needful_pointer_cast(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::PointerCastHelper< \
        T, needful::remove_reference_t<decltype(expr)>>), \
    needful_xtreme_cast(T, (expr)))


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

template<typename To, typename From>
struct FunctionPointerCastHelper {
    using ToDecayed = decay_t<To>;
    using FromDecayed = decay_t<From>;

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
        needful::FunctionPointerCastHelper<T,decltype(expr)>::type \
    >(expr))
