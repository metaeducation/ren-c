//
//  file: %needful-utilities.hpp
//  summary: "Utility macros for C++11 and higher"
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


//=//// COMPILE-TIME-ONLY DUMMY CLASS INSTANTIATION ///////////////////////=//
//
// This uses sizeof() to force the compiler to instantiate a template, in
// a way that can be used at compile-time.
//
// We take advantage of this to create dummy instances of types whose sole
// job is to do static assertions, without bringing in runtime cost.  This
// macro documents that we're doing that.
//
// Note that if for some reason one needs function call semantics for a check,
// this works with getting the size of that function's return type.
//
#define NEEDFUL_DUMMY_INSTANCE(...) \
    static_cast<void>(sizeof(__VA_ARGS__))


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

    #define PROBE_DECLTYPE(...) \
        NEEDFUL_DUMMY_INSTANCE(needful::ProbeTypeHelper<decltype(__VA_ARGS__)>)

    #define PROBE_CTYPE(...) \
        NEEDFUL_DUMMY_INSTANCE(needful::ProbeTypeHelper<__VA_ARGS__>)
#endif


//=//// MATERIALIZE PRVALUE ///////////////////////////////////////////////=//
//
// A prvalue is a pure temporary with no memory location--you can't take
// its address or reinterpret_cast<> it.  GCC enforces this strictly, so
// tricks that MSVC and Clang tolerate (like binding a cross-type rvalue
// reference && to a prvalue for reinterpretation) are rejected.  :-(
//
// This macro is a workaround, that forces a prvalue to get a memory location
// via same-type const& binding.  C++ standard mandates "temporary
// materialization" for this, giving the prvalue a stack slot whose address we
// can take.  It can be used e.g. to bypass conversion operators by
// pointer-reinterpreting to the standard-layout first member.
//
#define NEEDFUL_MATERIALIZE_PRVALUE(expr) \
    (&needful_xtreme_cast( \
        const needful::remove_reference_t<decltype(expr)>&, \
        (expr)))


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

template<typename... Ts>  // C++17 (polyfill for C++11/14 builds)
using void_t = void;

template<bool B, typename T, typename F>  // C++14
using conditional_t = typename std::conditional<B, T, F>::type;

#define needful_is_convertible_v(From,To) /* macro HACK [1] */ \
    std::is_convertible<From, To>::value

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


//=//// SAME-LAYOUT INHERITANCE CHECK /////////////////////////////////////=//
//
// Checks if Base is an ancestor of Derived in a zero-cost inheritance
// hierarchy where derivation adds no members.  Both must be standard-layout
// classes with the same size.
//
// This is the foundational invariant that makes the "check C with C++"
// pattern safe: because the memory representations are identical, casts
// between pointer levels (Derived** -> Base**) are safe even though C++
// doesn't allow them implicitly.
//
// IsContravariantLayout (in needful-contra.hpp) builds on this same
// invariant for Sink()/Init() parameter checking.
//

template<typename Base, typename Derived, typename Enable = void>
struct IsSameLayoutBase : std::false_type {};

template<typename Base, typename Derived>
struct IsSameLayoutBase<Base, Derived, enable_if_t<
    std::is_class<Base>::value
    and std::is_class<Derived>::value
    and std::is_base_of<Base, Derived>::value
>> {
    static_assert(
        std::is_standard_layout<Base>::value
        and std::is_standard_layout<Derived>::value
        and sizeof(Base) == sizeof(Derived),
        "Same-layout inheritance requires identical-sized standard layout types"
    );

    static constexpr bool value = true;
};


//=//// DEEP POINTER CONVERTIBILITY TRAIT /////////////////////////////////=//
//
// Standard std::is_convertible<Derived**, Base**> is false because C++
// doesn't allow covariant pointer-to-pointer conversions.  But in the
// needful type system, these conversions are safe: derivation adds no
// members, so the pointer representations are identical at every level.
//
// This trait recursively strips matching pointer layers from both types,
// then delegates to std::is_convertible at the innermost pointer level.
// The recursion only fires when BOTH sides are still pointers after
// stripping one layer, ensuring mismatched pointer depths are rejected.
//
//   IsDeepPointerConvertible<Derived*, Base*>      => true  (leaf check)
//   IsDeepPointerConvertible<Derived**, Base**>    => true  (recurse once)
//   IsDeepPointerConvertible<Derived***, Base***>  => true  (recurse twice)
//   IsDeepPointerConvertible<Derived**, Base*>     => false (depth mismatch)
//
// No separate layout assertion is needed here: std::is_convertible at the
// leaf level already requires a valid inheritance relationship, and the
// layout invariant (standard-layout, same sizeof) is enforced by
// IsSameLayoutBase wherever class types participate in contravariant casts.
//

template<typename From, typename To, typename Enable = void>
struct IsDeepPointerConvertible : std::is_convertible<From, To> {};

template<typename From, typename To>
struct IsDeepPointerConvertible<From*, To*, enable_if_t<
    std::is_pointer<From>::value and std::is_pointer<To>::value
>> : IsDeepPointerConvertible<From, To> {};

#define needful_is_deep_pointer_convertible_v(From,To) \
    needful::IsDeepPointerConvertible<From, To>::value

#define needful_is_constructible_v(From,To) /* macro HACK [1] */ \
    std::is_constructible<From, To>::value


//=//// is_function_pointer TRAIT /////////////////////////////////////////=//
//
// The C++ standard defines std::is_function<> but has no equivalent test
// for function pointers.  Define a helper in the needful namespace.
//

template<typename T>
struct is_function_pointer : std::false_type {};

template<typename Ret, typename... Args>
struct is_function_pointer<Ret (*)(Args...)> : std::true_type {};


//=//// AlwaysFalse TRAIT /////////////////////////////////////////////////=//
//
// AlwaysFalse<T> is a template that always yields false, but is dependent
// on T.  This works around the problem of static_assert()s inside of SFINAE'd
// functions, which would fail even if the SFINAE conditions were not met:
//
//    static_assert(false, "Always fails, even if not SFINAE'd");
//    static_assert(AlwaysFalse<T>::value, "Only fails if SFINAE'd");
//

template<typename T>  // T is ignored, just here to make it a template
struct AlwaysFalse : std::false_type {};  // for SFINAE static_assert [2]
