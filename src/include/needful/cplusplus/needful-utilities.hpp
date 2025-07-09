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
        static_assert(sizeof(T) == 0, "See compiler errors for probed name");
    };

    #define PROBE_DECLTYPE(expr) \
        (needful::ProbeTypeHelper<decltype(expr)>())
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
//     #define MY_MACRO(list,expr) \
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
#define NEEDFUL_UNPARENTHESIZE(...)  __VA_ARGS__


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
