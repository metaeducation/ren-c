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
template<typename T>
struct ConstifyHelper {  // default: non-pointer types, add const
    using type = const T;
};

template<typename T>
struct ConstifyHelper<T*> {  // raw pointer specialization: inject const
    using type = const T*;
};

#define needful_constify_type(T) \
    typename needful::ConstifyHelper< \
        typename std::remove_reference<T>::type \
    >::type


//=/// IsTypeConstlike: SMART-POINTER EXTENSIBLE CONSTNESS CHECK //////////=//
//
// This helper for testing if something is "constlike" is able to return
// true for things like Option(const char*), and false for Option(char*).
//
// It does so by building on top of the hook that such types already have
// to write for ConstifyHelper<T>... all it does is check to see if the
// type is the same as its constification!

template<typename T>
struct IsTypeConstlike {
    static constexpr bool value = std::is_same<
        T,
        needful_constify_type(T)
    >::value;
};


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
        IsTypeConstlike<From>::value,
        needful_constify_type(To),
        To
    >::type;
};


//=//// PROPAGATE CONSTNESS FROM ARGUMENTS TO RETURN TYPES ///////////////=//


#undef MUTABLE_IF_C
#define MUTABLE_IF_C(ReturnType, ...) \
    template<typename T> \
    __VA_ARGS__ typename needful::MirrorConstHelper<T, ReturnType>::type

#undef CONST_IF_C
#define CONST_IF_C(ParamType) /* !!! static_assert ParamType somehow? */ \
    T&&  // universal reference to arg

#undef CONSTABLE
#define CONSTABLE(ParamType) \
    typename needful::MirrorConstHelper<T, ParamType>::type
