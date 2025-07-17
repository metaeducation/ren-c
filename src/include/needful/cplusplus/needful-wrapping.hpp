//
//  file: %needful-wrapping.h
//  summary: "Helpers for wrapped type detection and rewrapping"
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
// reading the code naviate to this definition and read the rationale for
// why this convention is used.  It also means you can't cheat--the bits
// are the same as the C definition.
//
// 1. If you have a base class which has the actual storage of the wrapped
//    type, and a derived class which just adjusts the access for it, then
//    this is a special case where the derived class may need to override
//    the wrapped_type to be in sync with its template parameter.
//

#define NEEDFUL_DECLARE_WRAPPED_FIELD(T,name) \
    using wrapped_type = T; \
    T name

#define NEEDFUL_OVERRIDE_WRAPPED_FIELD_TYPE(T) /* derived class may use */ \
      using wrapped_type = T


//=//// WRAPPER CLASS DETECTION ///////////////////////////////////////////=//
//
// Uses SFINAE (Substitution Failure Is Not An Error) to detect if a type T
// has a nested type called `wrapped_type`. This enables generic code to
// distinguish between "wrapper" types (which define `wrapped_type`) and
// regular types.
//
// The trick is to declare two overloads of `test`: one that is valid only if
// `T::wrapped_type` exists, and a fallback. The result is a compile-time
// boolean constant, `HasWrappedType<T>::value`, that is true if T has a
// `wrapped_type`, and false otherwise.
//
// This pattern is robust and avoids hard errors for types that do not have
// the member, making it ideal for metaprogramming and generic code.
//

template<typename T>
struct HasWrappedType {
  private:
    template<typename U>
    static auto test(int) -> decltype(
        typename U::wrapped_type{},
        std::true_type{}
    );
    template<typename>
    static std::false_type test(...);

  public:
    static constexpr bool value = decltype(test<T>(0))::value;
};


//=//// UNWRAPPING: GET THE INNER TYPE OF A WRAPPER /////////////////////=//

template<typename T, bool = HasWrappedType<T>::value>
struct UnwrappedType
  { using type = T; };

template<typename T>
struct UnwrappedType<T, true>
  { using type = typename T::wrapped_type; };

#define needful_unwrapped_type(T) \
    typename needful::UnwrappedType<T>::type


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
