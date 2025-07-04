
//=//// DUMP TYPE NAME FOR DEBUGGING //////////////////////////////////////=//
//
// Somehow, despite decades of C++ development, there is no standard way to
// print the name of a type at compile time.  This is a workaround that
// uses a static assertion to force the compiler to print the type name in
// the error message.
//

#if !defined(NDEBUG)
    template<typename T>
    struct ProbeTypeHelper {
        static_assert(sizeof(T) == 0, "See compiler errors for probed name");
    };

    #define PROBE_TYPE(T) \
        (needful::ProbeTypeHelper<T>())
#endif


//=//// ENABLE IF FOR SAME TYPE ///////////////////////////////////////////=//
//
// This is useful for SFINAE (Substitution Failure Is Not An Error), as a
// very common pattern.  It's variadic, so you can use it like:
//
//   template <typename T, EnableIfSame<T, TypeOne, TypeTwo> = nullptr>
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

template <typename T, typename... Allowed>
struct IsSameAny;

template <typename T, typename First, typename... Rest>
struct IsSameAny<T, First, Rest...> {
    static constexpr bool value =
        std::is_same<T, First>::value or IsSameAny<T, Rest...>::value;
};

template <typename T>
struct IsSameAny<T> {
    static constexpr bool value = false;
};

template <typename T, typename... Allowed>
using EnableIfSame =
    typename std::enable_if<IsSameAny<T, Allowed...>::value>::type*;

template <typename T, typename... Allowed>
using DisableIfSame =
    typename std::enable_if<not IsSameAny<T, Allowed...>::value>::type*;


//=//// REMOVE REFERENCE SHORTHAND ////////////////////////////////////////=//
//
// Removing references is a common operation in C++ template code.  While
// sometimes you want to make distinctions for the behavior of a reference
// type vs. a value type, it's often more convenient to collapse them (for
// instance if you're trying to dispatch to specialization code...and don't
// want people to have to write separate specializations for T and T&).
//
// Macros are not namespaced, so we have to use a prefix to avoid conflicts.
// As a result, this macro isn't much shorter than what it replaces... but
// it makes the callsites eaier to scan without all the symbols.
//

#define needful_remove_reference(T) \
    typename std::remove_reference<T>::type


//=//// TYPE LIST HELPER //////////////////////////////////////////////////=//
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


// 2. AlwaysFalse<T> is a template that always yields false, but is dependent
//    on T.  This works around the problem of static_assert()s inside of
//    SFINAE'd functions, which would fail even if the SFINAE conditions
//    were not met:
//
//        static_assert(false, "Always fails, even if not SFINAE'd");
//        static_assert(always_false<T>::value, "Only fails if SFINAE'd");
//

template<typename T>  // T is ignored, just here to make it a template
struct AlwaysFalse : std::false_type {};  // for SFINAE static_assert [2]


//=//// ALWAYS VOID FOR SFINAE DETECTION //////////////////////////////////=//

template<typename...>
using AlwaysVoid = void;  // std::void_t in C++17


//=//// WRAPPED TYPE DETECTION ////////////////////////////////////////////=//

// Simple wrapper types can be used to add C++ power to C++ codebase.  But
// the wrappers get in the way of metaprogramming operations.  To simplify
// this, make type wrappers have a static member called `wrapped_type`, so
// that some metaprogramming operations can be automatic without having
// to write specializations for every wrapper type.

template<typename T>
struct HasWrappedType {
private:
    template<typename U>
    static auto test(int) -> decltype(typename U::wrapped_type{}, std::true_type{});
    template<typename>
    static std::false_type test(...);
public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

// Helper to extract template and rewrap - using struct instead of alias
template<typename T>
struct TemplateExtractor;

template<template<typename> class Wrapper, typename T>
struct TemplateExtractor<Wrapper<T>> {
    template<typename U>
    struct type {
        using result = Wrapper<U>;
    };
};
