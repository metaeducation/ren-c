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


//=//// CONSTIFY/UNCONSTIFY: ADD/REMOVE CONST ON POSSIBLY-WRAPPED TYPE ////=//
//
// There is no standardized way to request constness be added or removed
// from wrapped pointers in C++.
//
// 1. Some compilers (like GCC) do not like it if you have something like
//    a return type of a function being `const int` or `const MyEnum`.
//    While it should not matter, the warning is probably there to help
//    make sure you aren't misunderstanding and thinking the const does
//    something.  So we have to special-case fundamental and enum types
//    so that they don't get consted and trigger warnings.
//

template<typename T, bool TopLevel = true, typename Enable = void>
struct ConstifyHelper {
    using consted = T;  // don't add const for top level [1]
    using unconsted = remove_const_t<T>;
};

template<typename T>
struct ConstifyHelper<T, false, enable_if_t<  // TopLevel = false
    not HasWrappedType<T>::value
    and not std::is_pointer<T>::value
>> {
    using consted = const T;
    using unconsted = remove_const_t<T>;
};

template<typename T, bool TopLevel>
struct ConstifyHelper<T*, TopLevel, void> {  // pointed-to things, any level
    using consted = const T*;  // even top level adds const
    using unconsted = remove_const_t<T>*;
};

template<typename T, bool TopLevel>
struct ConstifyHelper<T* const, TopLevel, void> {  // pointer *itself* is const
    using consted = const T* const;
    using unconsted = remove_const_t<T>* const;
};

template<typename Ret, typename... Args>
struct ConstifyHelper<Ret(*)(Args...), true, void> {  // function pointers
    using consted = Ret(*)(Args...);  // function pointers can't be const
    using unconsted = Ret(*)(Args...);
};

// For recursion inside wrappers, use TopLevel = false
template<typename T, bool TopLevel>
struct ConstifyHelper<T, TopLevel, enable_if_t<HasWrappedType<T>::value>>
{
  private:
    using wrapped_consted = typename ConstifyHelper<
        typename T::wrapped_type, false
    >::consted;

    using wrapped_unconsted = typename ConstifyHelper<
        typename T::wrapped_type, false
    >::unconsted;

  public:
    using consted = needful_rewrap_type(T, wrapped_consted);
    using unconsted = needful_rewrap_type(T, wrapped_unconsted);
};

#define needful_constify_t(T) \
    typename needful::ConstifyHelper< \
        needful::remove_reference_t<T>, true>::consted

#define needful_unconstify_t(T) \
    typename needful::ConstifyHelper< \
        needful::remove_reference_t<T>, true>::unconsted


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
struct IsConstIrrelevant : std::integral_constant<
    bool,
    not std::is_pointer<T>::value
    and not HasWrappedType<T>::value
> {};

#define needful_is_const_irrelevant(T) \
    needful::IsConstIrrelevant<needful::remove_reference_t<T>>::value


//=/// IsConstlikeType: SMART-POINTER EXTENSIBLE CONSTNESS CHECK //////////=//
//
// This helper for testing if something is "constlike" is able to return
// true for things like Option(const char*), and false for Option(char*).
//
// It does so by building on top of the hook that such types already have
// to write for ConstifyHelper<T>... all it does is check to see if the
// type is the same as its constification!

template<typename T>
struct IsConstlike {
    static constexpr bool value
        = std::is_same<T, needful_constify_t(T)>::value;
};

#define needful_is_constlike(T) \
    needful::IsConstlike<needful::remove_reference_t<T>>::value


//=//// CONST MIRRORING: MATCH CONSTNESS OF ONE TYPE ONTO ANOTHER ////////=//
//
// This const mirroring builds on top of the ConstifyHelper, rather than
// needing to be a separate construct that things like smart pointers need
// to overload.  The concept is that it checks to see if a type is the same
// as its constification, and if so then it's const.
//

template<typename From, typename To>
struct MirrorConstHelper {
    using type = conditional_t<
        needful_is_const_irrelevant(From),
        To,  // leave as-is for const-irrelevant types
        conditional_t<
            needful_is_constlike(From),  // mirror constness otherwise
            needful_constify_t(To),
            needful_unconstify_t(To)
        >
    >;
};

#define needful_mirror_const_t(From, To) \
    typename needful::MirrorConstHelper<From, To>::type


//=//// CONST MERGING: ADD ANY CONSTNESS FROM ONE TYPE ONTO ANOTHER ///////=//
//
// This is a slight variation on the MirrorConstHelper, which will not make
// the constness match (so it doesn't unconstify), but will add constness.
//

template<typename From, typename To>
struct MergeConstHelper {
    using type = conditional_t<
        not needful_is_const_irrelevant(From) and needful_is_constlike(From),
        needful_constify_t(To),
        To  // don't unconstify (see needful_mirror_const_t() for that)
    >;
};

#define needful_merge_const(From, To) \
    typename needful::MergeConstHelper<From, To>::type
