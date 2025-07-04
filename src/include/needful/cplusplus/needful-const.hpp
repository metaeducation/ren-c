//
//  file: %needful-const.h
//  summary: "Mutability transference from input arguments to return results"
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


//=//// CONSTIFY: ADD CONST TO POINTED-TO TYPE ////////////////////////////=//
//
// There is no standardized way to request constness be added to a wrapped
// pointer in C++.  All implemenations of this are equally ad-hoc.
//

// Forward declarations
template<typename T, bool = std::is_fundamental<T>::value || std::is_enum<T>::value>
struct ConstifyHelper;

template<typename T, bool = std::is_fundamental<T>::value || std::is_enum<T>::value>
struct UnconstifyHelper;

// ConstifyHelper - fundamental/enum case (no const added)
template<typename T>
struct ConstifyHelper<T, true> {
    using type = T;
};

// ConstifyHelper - general case
template<typename T>
struct ConstifyHelper<T, false> {
private:
    template<typename U, bool has_wrapped>
    struct ConstifyImpl {
        using type = const U;
    };

    template<typename U>
    struct ConstifyImpl<U, true> {
        using type = typename TemplateExtractor<U>::template type<
            typename ConstifyHelper<typename U::wrapped_type>::type
        >::result;
    };

public:
    using type = typename ConstifyImpl<T, HasWrappedType<T>::value>::type;
};

// ConstifyHelper - pointer specializations
template<typename T>
struct ConstifyHelper<T*, false> {
    using type = const T*;
};

template<typename T>
struct ConstifyHelper<T* const, false> {
    using type = const T* const;
};

// UnconstifyHelper - fundamental/enum case (remove const if present)
template<typename T>
struct UnconstifyHelper<T, true> {
    using type = typename std::remove_const<T>::type;
};

// UnconstifyHelper - general case
template<typename T>
struct UnconstifyHelper<T, false> {
private:
    template<typename U, bool has_wrapped>
    struct UnconstifyImpl {
        using type = typename std::remove_const<U>::type;
    };

    template<typename U>
    struct UnconstifyImpl<U, true> {
        using type = typename TemplateExtractor<U>::template type<
            typename UnconstifyHelper<typename U::wrapped_type>::type
        >::result;
    };

public:
    using type = typename UnconstifyImpl<T, HasWrappedType<T>::value>::type;
};

// UnconstifyHelper - pointer specializations
template<typename T>
struct UnconstifyHelper<T*, false> {
    using type = typename std::remove_const<T>::type*;
};

template<typename T>
struct UnconstifyHelper<T* const, false> {
    using type = typename std::remove_const<T>::type* const;
};

// Convenience aliases (C++11 compatible)
template<typename T>
struct Constify {
    using type = typename ConstifyHelper<typename std::remove_reference<T>::type>::type;
};

template<typename T>
struct Unconstify {
    using type = typename UnconstifyHelper<typename std::remove_reference<T>::type>::type;
};


// Macros for convenience
#define needful_constify_type(T) \
    typename needful::Constify<T>::type

#define needful_unconstify_type(T) \
    typename needful::Unconstify<T>::type


//=/// IsConstlikeType: SMART-POINTER EXTENSIBLE CONSTNESS CHECK //////////=//
//
// This helper for testing if something is "constlike" is able to return
// true for things like Option(const char*), and false for Option(char*).
//
// It does so by building on top of the hook that such types already have
// to write for ConstifyHelper<T>... all it does is check to see if the
// type is the same as its constification!

template<typename T>
struct IsConstlikeType {
    static constexpr bool value = std::is_same<
        needful_remove_reference(T),
        needful_constify_type(needful_remove_reference(T))
    >::value;
};


//=//// IsConstIrrelevantForType: DODGE GCC WARNINGS //////////////////////=//
//
// It would be nice if we could just mirror const onto things without
// special-casing things.  But GCC will warn if you try to make const enum
// values or things of the sort e.g. as a return type from a function
// (it shouldn't matter, but the warning is probably there just to help make
// sure you aren't misunderstanding and thinking the const does something).
//
// So we have special handling for types where constness is irrelevant.
//

template<typename T>
struct IsConstIrrelevantForType : std::integral_constant<
    bool,
    std::is_fundamental<T>::value  // note: nullptr_t is fundamental
        or std::is_enum<T>::value
> {};


//=//// CONST MIRRORING: TRANSFER CONSTNESS FROM ONE TYPE TO ANOTHER //////=//
//
// This const mirroring builds on top of the ConstifyHelper, rather than
// needing to be a separate construct that things like smart pointers need
// to overload.  The concept is that it checks to see if a type is the same
// as its constification, and if so then it's const.
//

template<typename From, typename To>
struct MirrorConstHelper {
    using type = typename std::conditional<
        IsConstIrrelevantForType<needful_remove_reference(From)>::value,
        To,  // leave as-is for const-irrelevant types
        typename std::conditional<
            IsConstlikeType<From>::value,  // mirror constness otherwise
            needful_constify_type(To),
            needful_unconstify_type(To)
        >::type
    >::type;
};

#define needful_mirror_const(From, To) \
    typename needful::MirrorConstHelper<From, To>::type


//=//// PROPAGATE CONSTNESS FROM ARGUMENTS TO RETURN TYPES ///////////////=//


#undef MUTABLE_IF_C
#define MUTABLE_IF_C(ReturnType, ...) \
    template<typename T> \
    __VA_ARGS__ needful_mirror_const(T, ReturnType)

#undef CONST_IF_C
#define CONST_IF_C(ParamType) /* !!! static_assert ParamType somehow? */ \
    T&&  // universal reference to arg

#undef CONSTABLE
#define CONSTABLE(ParamType) \
    needful_mirror_const(T, ParamType)
