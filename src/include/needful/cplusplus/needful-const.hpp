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

#define needful_constify(T) \
    typename needful::ConstifyHelper<typename std::remove_reference<T>::type>::type


#undef MUTABLE_IF_C
#define MUTABLE_IF_C(return_type, ...) \
    template<typename T> \
    __VA_ARGS__ typename std::conditional< \
        std::is_same<T, needful_constify(T)>::value, \
        needful_constify(return_type), \
        return_type \
    >::type

#undef CONST_IF_C
#define CONST_IF_C(param_type)  T&&  // universal reference to arg

#undef CONSTABLE
#define CONSTABLE(param_type) \
    typename std::conditional< \
        std::is_same<T, needful_constify(T)>::value, \
        needful_constify(param_type), \
        param_type \
    >::type
