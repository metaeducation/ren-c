//
//  file: %needful-utilities.h
//  summary: "Utility macros for C++11 and higher"
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
        static_assert(sizeof(T) == 0, "See sizeof() error for probed type");
    };

    #define PROBE_DECLTYPE(expr) \
        (needful::ProbeTypeHelper<decltype(expr)>{})

    #define PROBE_CTYPE(...) /* workaround for Templated<T1,T2> arguments */ \
        (needful::ProbeTypeHelper<__VA_ARGS__>{})
#endif


//=//// VARIADIC MACRO PARENTHESES REMOVAL ////////////////////////////////=//
//
// NEEDFUL_UNPARENTHESIZE is used to remove a single layer of parentheses
// from a macro argument.  This is useful if you want to capture variadic
// arguments at a macro callsite as a single argument in parentheses.
//
// Needful uses it to transfer variadic arguments to C++ templates in a way
// that C can just ignore:
//
//     #define MY_MACRO(list,expr)  /* [1] */
//         my_template<NEEDFUL_UNPARENTHESIZE list>(expr)
//
// Note the macro isn't invoked with parentheses in the expansion--it uses
// the parentheses in the argument.  So if you say:
//
//    MY_MACRO((int, float, double), value)
//
// It expands to:
//
//    my_template<NEEDFUL_UNPARENTHESIZE (int, float, double)>(expr)
//
// Which further expands to:
//
//    my_template<int, float, double>(expr)
//
// 1. You can't put backslashes in comments, but there'd be one here.
//
#define NEEDFUL_UNPARENTHESIZE(...)  __VA_ARGS__


//=//// TYPE TRAIT ALIAS SHIMS (FOR C++11 COMPATIBILITY) //////////////////=//
//
// Needful is supposed to work in C++11, so we don't use the C++14/17/20
// type trait aliases.  But the language is fully capable of supporting them
// as a feature--they're just weren't in the standard library.
//
// Shimming them into the std:: namespace is fraught.  So instead, we just
// define their equivalents in the needful:: namespace, which means that
// much of the code in Needful can use it without namespacing (unless it's
// a macro intended to be used outside the needful:: namespace, at which
// point it has to carry the namespace.)
//
// 1. is_convertible_v<From,To> is not something you can define in C++11.
//
//      template<typename From, typename To>  // needs C++14 :-(
//      constexpr bool is_convertible_v = std::is_convertible<F, T>::value;
//
//      template<typename From, typename To>  // needs C++17 :-(
//      using is_convertible_v = std::is_convertible<From, To>::value;
//
//    Defining a macro seems worth it for the readability advantage.
//

template<typename T>  // C++14
using decay_t = typename std::decay<T>::type;

template<typename T>  // C++14
using remove_reference_t = typename std::remove_reference<T>::type;

template<typename T>  // C++14
using remove_const_t = typename std::remove_const<T>::type;

template<typename T>  // C++14
using remove_pointer_t = typename std::remove_pointer<T>::type;

template<typename T>  // C++14
using add_pointer_t = typename std::add_pointer<T>::type;

template<bool B, typename T = void>  // C++14
using enable_if_t = typename std::enable_if<B, T>::type;

template<bool B, typename T, typename F>  // C++14
using conditional_t = typename std::conditional<B, T, F>::type;

#define needful_is_convertible_v(From,To) /* macro HACK [1] */ \
    std::is_convertible<From, To>::value

#define needful_is_constructible_v(From,To) /* macro HACK [1] */ \
    std::is_constructible<From, To>::value

template<typename From, typename To>
class is_explicitly_convertible {  // C++20
    template<typename F, typename T>
    static auto test(int) ->
        decltype(static_cast<T>(std::declval<F>()), std::true_type{});
    template<typename, typename>
    static std::false_type test(...);
  public:
    static constexpr bool value = decltype(test<From, To>(0))::value;
};

#define needful_is_explicitly_convertible_v(From,To) /* macro HACK [1] */ \
    needful::is_explicitly_convertible<From, To>::value


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
