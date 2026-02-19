//
//  file: %needful-wrapping.hpp
//  summary: "Helpers for wrapped type detection and rewrapping"
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
// Needful's goal is to bring C++ as a service to codebases whose semantic
// behaviors are all accomplished with C.
//
// This means that any wrapper classes used in a C++ build are only for
// type checking and assertions.  Hence they are always "thin" proxies for
// some single wrapped implementation type.  That narrowness makes it
// possible for us to provide efficient and automatic metaprogramming
// abilities for these wrapped types (e.g. mutability casts).
//
// What we do is have all such classes expose a static member called
// `::wrapped_type`, which we then leverage to make several metaprogramming
// operations automatic--without the wrapper class author having to get
// involved and write specializations.  All they have to do is permit
// explicit casting to the wrapped type, and Needful can do the rest.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. In order for the metaprogramming assistance to work, the wrapped_type
//    must be *the* type you are templated against.  e.g. don't do this:
//
//        template<typename T>
//        struct MyWrapper {
//           using wrapped_type = T*;
//           T* pointer;
//           ...
//        };
//
//     When you didn't synchronize `wrapped_type` with the actual type then
//     the intelligence to extract the type via an explicit cast and then
//     use `needful_rewrap_type()` to build a new wrapper with a different
//     inner type won't work.
//
//     If you want to do something like this, you can hide the pointer with
//     a macro.  You'll be using a macro anyway, because Needful is for C
//     codebases, so you wouldn't have <> in source anyway!
//
//         #if defined(__cplusplus)
//             #define MyWrap(T) MyWrapper<T*>
//         #else
//             #define MyWrap(T) T*
//         #endif
//


//=//// WRAPPER CLASS DEFINITION ///////////////////////////////////////////=//
//
// This is better than just `using wrapped_type = T;` because it helps people
// reading the code navigate to this definition and read the rationale for
// why this convention is used.  It also means you can't cheat--the bits
// are the same as the C definition.
//
// 1. If you have a base class which has the actual storage of the wrapped
//    type, and a derived class which just adjusts the access for it, then
//    this is a special case where the derived class may need to override
//    the wrapped_type to be in sync with its template parameter.
//

#define NEEDFUL_DECLARE_WRAPPED_FIELD(T,name) \
    static_assert(std::is_standard_layout<T>::value, \
        "Needful wrapped types must be standard-layout"); \
    using wrapped_type = T; \
    using wrapped_deep_type = needful_deeply_unwrapped_type(T); \
    T name

#define NEEDFUL_OVERRIDE_WRAPPED_FIELD_TYPE(T) /* derived class may use */ \
    static_assert(std::is_standard_layout<T>::value, \
        "Needful wrapped types must be standard-layout"); \
      using wrapped_type = T


//=//// WRAPPER CLASS DETECTION ///////////////////////////////////////////=//
//
// Detects whether a type `T` declares a nested type named `wrapped_type`.
// Types that do so are treated as "wrapper" types by Needful's generic code.
//
// This uses the standard C++11 "detection idiom" based on `void_t`.  The
// primary template defaults to `false_type`, while a partial specialization
// is selected only if substitution of `typename T::wrapped_type` succeeds.
// If the nested type does not exist, substitution fails silently (SFINAE),
// and the primary template is used instead.
//
// (NOTE: Using a common empty base class for all wrapper classes was tried
// to see if it would be more efficient.  But it didn't help, and just made
// a rule you might not want to follow in your class, so we stick with this.)
//
// 1. Any type exposing `wrapped_type` must be standard-layout, because
//    NEEDFUL_EXTRACT_INNER relies on reinterpret_cast to the first member
//    (guaranteed only for standard-layout structs).  Enforcing this here
//    catches violations at the earliest possible point--when the wrapper
//    type is first examined--rather than at each individual cast site.
//

template<typename T, typename = void>
struct HasWrappedType : std::false_type {};

template<typename T>
struct HasWrappedType<T, void_t<typename T::wrapped_type>>
    : std::true_type
{
    static_assert(
        std::is_standard_layout<T>::value,
        "Wrapper types (with wrapped_type) must be standard-layout"
    );
};


//=//// UNWRAPPING: GET THE INNER TYPE OF A WRAPPER /////////////////////=//

template<typename T, typename = void>
struct UnwrappedType {
    using type = T;
};

template<typename T>
struct UnwrappedType<T, void_t<typename T::wrapped_type>> {
    using type = typename T::wrapped_type;
};

template<typename T, typename = void>
struct DeeplyUnwrappedType {
    using type = T;
};

template<typename T>
struct DeeplyUnwrappedType<T, void_t<typename T::wrapped_deep_type>> {
    using type = typename T::wrapped_deep_type;
};

#define needful_unwrapped_type(T) /* unwrapped IF wrapped, else T */ \
    typename needful::UnwrappedType<T>::type

#define needful_deeply_unwrapped_type(T) /* unwrapped IF wrapped, else T */ \
    typename needful::DeeplyUnwrappedType<T>::type


//=//// REWRAP AN INNER TYPE WITH THE SAME TEMPLATE ///////////////////////=//
//
// This allows you to generically "re-wrap" a type with the same template as
// the original wrapper, but with a different inner type. This is a common
// metaprogramming pattern sometimes called "rebind" in C++ libraries.
//
// 1. Wrapper here is a "template template parameter":
//
//      https://en.cppreference.com/w/cpp/language/template_parameters.html
//

template<typename WrapperType, typename NewInnerType>
struct RewrapHelper;

template<
    template<typename> class Wrapper,
    typename OldInner,
    typename NewInner
>
struct RewrapHelper<Wrapper<OldInner>, NewInner> {
    using type = Wrapper<NewInner>;
};

template<typename Wrapper, typename NewWrapped>
struct RewrapHelper<const Wrapper, NewWrapped> {  // const needs forwarding
    using type = const typename RewrapHelper<Wrapper, NewWrapped>::type;
};

#define needful_rewrap_type(WrapperType, NewInnerType) \
    typename needful::RewrapHelper<WrapperType, NewInnerType>::type


//=//// REWRAPPABLE DETECTION /////////////////////////////////////////////=//
//
// Not all types that expose `wrapped_type` are template instantiations that
// RewrapHelper can decompose.  For example, a plain struct like:
//
//     struct Heart {
//         NEEDFUL_DECLARE_WRAPPED_FIELD(HeartEnum, h);
//         ...
//     };
//
// ...has `wrapped_type` (so HasWrappedType is true), but it is not
// `SomeTemplate<HeartEnum>`, so RewrapHelper cannot pattern-match it.
// IsRewrappable detects this so that other metaprogramming (like
// ConstifyHelper) can fall back gracefully for non-template wrappers.
//

template<typename T, typename = void>
struct IsRewrappable : std::false_type {};

template<typename T>
struct IsRewrappable<T,
    enable_if_t<HasWrappedType<T>::value,
        void_t<typename RewrapHelper<T, typename T::wrapped_type>::type>
    >
> : std::true_type {};

#define needful_is_rewrappable_v(T) \
    needful::IsRewrappable<T>::value


//=//// LEAF TYPE IN WRAPPER LAYERS ///////////////////////////////////////=//
//
// LeafPointee: Extract the innermost pointee type through wrapper layers.
//
//     LeafPointee<Slot*>::type             => Slot
//     LeafPointee<ExactWrapper<Slot*>>     => Slot
//     LeafPointee<OnStack(Init(Slot))>     => Slot
//
template<typename T, bool = HasWrappedType<T>::value>
struct LeafPointee { using type = remove_pointer_t<T>; };

template<typename T>
struct LeafPointee<T, true> : LeafPointee<typename T::wrapped_type> {};


//=//// SEMANTIC VS. NON-SEMANTIC WRAPPERS ////////////////////////////////=//
//
// Wrappers fall into two categories that matter for casting behavior:
//
// * "Semantic" wrappers carry meaning beyond the raw type--their wrapping
//   is part of the data's contract.  Option(T) tracks engaged/disengaged
//   state; Result(T) signals that an error may have occurred.  Casting
//   should preserve these wrappers: `cast(U, Result(T))` --> `Result(U)`.
//
// * "Non-semantic" wrappers describe parameter-passing conventions rather
//   than data state: Sink(T) marks a write-through parameter, Need(T) marks
//   a non-null contract, Exact(T) blocks implicit conversions, etc.  Once
//   you cast, you're done with the convention, so cast extracts the inner
//   value: `cast(U, sink_value)` --> `U`.
//
// Specialize `IsWrapperSemantic` to `true_type` for your wrapper if
// cast() should auto-preserve it.  The default is false (extract).
//

template<typename>
struct IsWrapperSemantic : std::false_type {};


//=//// BASIC TYPE DETECTION //////////////////////////////////////////////=//
//
// C++ splits its types into "scalar" (arithmetic + enum + pointer + member
// pointer + nullptr_t) and "compound" (everything else).  But scalar lumps
// pointers together with integers and enums, which is wrong for our cast
// dispatch: pointer-to-pointer casts need reinterpret_cast and CastHook
// validation, while int-to-enum or bool-to-int are fine with static_cast.
//
// IsBasicType captures the subset of types where static_cast is always
// valid and no hook dispatch is needed: fundamentals (int, bool, float...)
// and enums.  For wrapped types, it looks through the wrapper to classify
// based on the inner type (e.g. Need(int) is basic, Need(Foo*) is not).
//
// This drives SFINAE in the h_cast() overloads:
//   - basic -> basic:  overload 1 (constexpr static_cast, no hooks)
//   - non-basic -> basic:  overload 2 (hooks, but basic target)
//   - everything else:  overloads 3/4/5 (reinterpret_cast territory)
//

template<typename T>
struct IsBasicType {
    using Adjusted = needful_unwrapped_type(T);  // !!! deep?

    static constexpr bool value =
        std::is_fundamental<Adjusted>::value or std::is_enum<Adjusted>::value;
};


//=//// INNER EXTRACTION FROM WRAPPERS ////////////////////////////////////=//
//
// Needful wrappers are standard-layout structs whose first (and only) data
// member is the wrapped value.  C++ guarantees that a pointer to a
// standard-layout struct can be reinterpret_cast to a pointer to its first
// member (and vice versa):
//
//   https://en.cppreference.com/w/cpp/language/data_members#Standard-layout
//
// This is how the casting wrapped conversion operators (which may have
// side effects—e.g. Sink's corruption semantics) can directly access the
// inner value for casting purposes.  The static_assert on is_standard_layout
// in each overload ensures the guarantee actually holds.
//

#define NEEDFUL_EXTRACT_INNER(InnerType, wrapper) \
    reinterpret_cast<const InnerType&>(wrapper)
