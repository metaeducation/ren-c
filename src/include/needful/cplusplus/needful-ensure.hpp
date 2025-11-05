//
//  file: %needful-ensure.h
//  summary: "Type ensuring helpers that use C++ type_traits"
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

//=//// ENABLE IF FOR SAME TYPE ///////////////////////////////////////////=//
//
// This is useful for SFINAE (Substitution Failure Is Not An Error), as a
// very common pattern.  It's variadic, so you can use it like:
//
//   ENABLE_IF_EXACT_ARG_TYPE(TypeOne, TypeTwo)
//   INLINE bool operator==(const TypeThree& three, T&& t) = delete;
//
// Written out long form that would look like:
//
//    template <
//        typename T,
//        typename std::enable_if<
//            std::is_same<T, TypeOne>::value
//            or std::is_same<T, TypeTwo>::value
//        >::type* = nullptr
//     >
//     INLINE bool operator==(const TypeThree& three, T&& t) = delete;
//
// You can use this with code that runs in C or C++.

template <typename T, typename... Allowed>
struct IsSameAny;

template <typename T, typename First, typename... Rest>
struct IsSameAny<T, First, Rest...> {
    static constexpr bool value =
        std::is_same<T, First>::value
        or IsSameAny<T, Rest...>::value;
};

template <typename T>
struct IsSameAny<T>
  { static constexpr bool value = false; };


#undef ENABLE_IF_EXACT_ARG_TYPE
#define ENABLE_IF_EXACT_ARG_TYPE(...) \
    template <typename T, needful::enable_if_t< \
        needful::IsSameAny<T, __VA_ARGS__>::value>* = nullptr>

#undef DISABLE_IF_EXACT_ARG_TYPE
#define DISABLE_IF_EXACT_ARG_TYPE(...) \
    template <typename T, typename needful::enable_if_t< \
        not needful::IsSameAny<T, __VA_ARGS__>::value>* = nullptr>

#undef ENABLEABLE
#define ENABLEABLE(T, name)  T name


//=//// CONVERTIBLE ARGUMENT TYPES ////////////////////////////////////////=//
//

template <typename T, typename... Allowed>
struct IsConvertibleAny;

template <typename T, typename First, typename... Rest>
struct IsConvertibleAny<T, First, Rest...> {
    static constexpr bool value =
        needful_is_convertible_v(T, First)
        or IsConvertibleAny<T, Rest...>::value;
};

template <typename T>
struct IsConvertibleAny<T>
  { static constexpr bool value = false; };


#undef ENABLE_IF_ARG_CONVERTIBLE_TO
#define ENABLE_IF_ARG_CONVERTIBLE_TO(...) \
    template <typename T, needful::enable_if_t< \
        needful::IsConvertibleAny<T, __VA_ARGS__>::value>* = nullptr>

#undef DISABLE_IF_ARG_CONVERTIBLE_TO
#define DISABLE_IF_ARG_CONVERTIBLE_TO(...) \
    template <typename T, needful::enable_if_t< \
        not needful::IsConvertibleAny<T, __VA_ARGS__>::value>* = nullptr>

// Uses same ENABLEABLE()


//=//// TYPE ENSURING HELPER //////////////////////////////////////////////=//


template<typename From, typename First, typename... Rest>
struct IsConvertibleAsserter {
    static_assert(
        needful::IsConvertibleAny<From, First, Rest...>::value,
        "ensure() failed"
    );
};

#undef needful_rigid_ensure
#define needful_rigid_ensure(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::IsConvertibleAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        T \
    >), \
    needful_xtreme_cast(T, (expr)))

#undef needful_lenient_ensure
#define needful_lenient_ensure(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::IsConvertibleAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        needful_constify_t(T) /* loosen to matching constified T too */ \
    >), \
    needful_xtreme_cast(needful_merge_const_t(decltype(expr), T), (expr)))


#undef needful_ensure_any
#define needful_ensure_any(TLIST,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::IsConvertibleAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        NEEDFUL_UNPARENTHESIZE TLIST \
    >), \
    (expr))


//=//// TYPE LIST HELPER //////////////////////////////////////////////////=//
//
// This is a C++ template metaprogramming utility, really only useful if
// you are writing CastHook<>.
//
// Type lists allow checking if a type is in a list of types at compile time:
//
//     template<typename T>
//     void process(T value) {
//         using NumericTypes = CTypeList<int, float, double>;
//         static_assert(NumericTypes::contains<T>{}, "T must be numeric");
//         // ...
//     }
//
// 1. For C++11 compatibility, it must be `List::contains<T>{}` with braces.
//    C++14 or higher has variable templates:
//
//        struct contains_impl {  /* instead of calling this `contains` */
//            enum { value = false };
//        };
//        template<typename T>
//        static constexpr bool contains = contains_impl<T>::value;
//
//    Without that capability, best we can do is to construct an instance via
//    a default constructor, and then have a constexpr implicit boolean
//    coercion for that instance.
//
// 2. To expose the type list functionality in a way that will look less
//    intimidating to C programmers, `DECLARE_C_TYPE_LIST` is provided, along
//    with `In_C_Type_List`.  This lets you make it look like a regular
//    function call:
//
//     template<typename T>
//     void process(T value) {
//         DECLARE_C_TYPE_LIST(numeric_types,
//             int, float, double
//         );
//         STATIC_ASSERT(In_C_Type_List(numeric_types, T));
//         // ...
//     }
//
//    While obfuscating the C++ is questionable in terms of people's education
//    about the language--and making compile-time code look like runtime
//    functions is a bit of a lie--this does make it easier.  It also means
//    the In_C_Type_List() function can be used in STATIC_ASSERT() macros
//    without making you write STATIC_ASSERT(()) to wrap < and > characters
//    that would appear with ::contains<T> at the callsite.
//

template<typename... Ts>
struct CTypeList {
    template<typename T>
    struct contains {
        enum { value = false };

        // Allow usage without ::value in most contexts [1]
        constexpr operator bool() const { return value; }
    };
};

template<typename T1, typename... Ts>
struct CTypeList<T1, Ts...> {  // Specialization for non-empty lists
    template<typename T>
    struct contains {
        enum { value = std::is_same<T, T1>::value or
                    typename CTypeList<Ts...>::template contains<T>() };

        // Allow usage without ::value in most contexts [1]
        constexpr operator bool() const { return value; }
    };
};

#define DECLARE_C_TYPE_LIST(name, ...)  /* friendly for C [2] */ \
    using name = CTypeList<__VA_ARGS__>

#define In_C_Type_List(list,T)  /* friendly for C [2] */ \
    list::contains<T>()
