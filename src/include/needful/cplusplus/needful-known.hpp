//
//  file: %needful-known.hpp
//  summary: "Compile-time only type assurance helpers using C++ type_traits"
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
//
// 1. known(T, expr) does not change the type of expr, but only asserts that
//    it can be converted to T.  known_not(T, expr) and known_any(T, expr)
//    wouldn't know what type to convert it to if you wanted it to convert!
//
// 2. x_cast_known(T, expr) *does* change the type of expr; and it comes in
//    lenient form, that detects constness and applies it to T as needed
//    based on the constness of expr.
//

template<typename From, typename First, typename... Rest>
struct IsConvertibleAsserter {
    static_assert(
        needful::IsConvertibleAny<From, First, Rest...>::value,
        "known() failed"
    );
};

template<typename From, typename First, typename... Rest>
struct NotConvertibleAsserter {
    static_assert(
        not needful::IsConvertibleAny<From, First, Rest...>::value,
        "known_not() failed"
    );
};

#undef needful_rigid_known
#define needful_rigid_known(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::IsConvertibleAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        T \
    >), \
    (expr))  // [1]

#undef needful_lenient_known
#define needful_lenient_known(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::IsConvertibleAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        needful_constify_t(T) /* loosen to matching constified T too */ \
    >), \
    (expr))  // [1]

#undef needful_rigid_known_any
#define needful_rigid_known_any(TLIST,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::IsConvertibleAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        NEEDFUL_UNPARENTHESIZE TLIST \
    >), \
    (expr))  // [1]

// !!! write lenient_known_any if needed

#undef needful_rigid_known_not
#define needful_rigid_known_not(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::NotConvertibleAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        T \
    >), \
    (expr))  // [1]

#undef needful_lenient_known_not
#define needful_lenient_known_not(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::NotConvertibleAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        needful_constify_t(T) /* loosen to matching constified T too */ \
    >), \
    (expr))  // [1]

#undef needful_rigid_x_cast_known
#define needful_rigid_x_cast_known(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::IsConvertibleAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        T \
    >), \
    needful_xtreme_cast(T, (expr)))  // [2]

#undef needful_lenient_x_cast_known
#define needful_lenient_x_cast_known(T,expr) \
    (NEEDFUL_DUMMY_INSTANCE(needful::IsConvertibleAsserter< \
        needful::remove_reference_t<decltype(expr)>, \
        needful_constify_t(T) /* loosen to matching constified T too */ \
    >), \
    needful_xtreme_cast(needful_merge_const_t(decltype(expr), T), (expr))) // [2]


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
        constexpr explicit operator bool() const { return value; }
    };
};

template<typename T1, typename... Ts>
struct CTypeList<T1, Ts...> {  // Specialization for non-empty lists
    template<typename T>
    struct contains {
        enum { value = std::is_same<T, T1>::value or
                    typename CTypeList<Ts...>::template contains<T>() };

        // Allow usage without ::value in most contexts [1]
        constexpr explicit operator bool() const { return value; }
    };
};

#define DECLARE_C_TYPE_LIST(name, ...)  /* friendly for C [2] */ \
    using name = CTypeList<__VA_ARGS__>

#define In_C_Type_List(list,T)  /* friendly for C [2] */ \
    list::contains<T>()


//=//// EXACT() FOR FORBIDDING COVARIANT INPUT PARAMETERS /////////////////=//
//
// Exact() prohibits covariance, but but unlike Sink() or Init() it doesn't
// imply corruption, so contravariance doesn't make sense.  It just enforces
// that only the exact type is used.
//
// NOTE: The code below might seem overcomplicated for that stated purpose,
// and simplifications are welcome!  But it has some interoperability with
// Sink() and Init() as well as fitting into Needful's casting framework.
// So it's more complex than a minimal C++11 Exact() implementation would be.
//
// 1. While Sink(T) and Init(T) implicitly add pointers to the type, you have
//    to say Exact(T*) if it's a pointer.  This allows you to use Exact
//    with non-pointer types.
//
//    However, the template -must- be parameterized with the type it is a
//    stand-in for, so it is `SinkWrapper<T*>`, `InitWrapper<T*>`, and
//    `ExactWrapper<T*>`.
//
//    (See needful_rewrap_type() for the reasoning behind this constraint.)
//
// 2. Uses in the codebase the Needful library were written for required that
//    Exact(T*) be able to accept cells with pending corruptions.  I guess
//    the thing I would say that if you want to argue with this design point,
//    you should consider that there's nothing guaranteeing a plain `T*` is
//    not corrupt...so you're not bulletproofing much and breaking some uses
//    that turned out to be important.  It's better to have cross-cutting
//    ways at runtime to notice a given T* is corrupt regardless of Exact().
//
// 3. Non-dependent enable_if conditions work in MSVC, but GCC has trouble
//    with them.  Introducing a dependent type seems to help it along.
//

#undef NeedfulExact
#define NeedfulExact(TP) \
    needful::ExactWrapper<TP>  // * not implicit [1]

template<typename TP>  // TP may or may not be a pointer type
struct ExactWrapper {
    NEEDFUL_DECLARE_WRAPPED_FIELD (TP, p);

    using T = remove_const_t<remove_pointer_t<TP>>;  // base type

    template<typename U>
    using IfExactType = enable_if_t<
        std::is_same<remove_const_t<U>, T>::value  // same base type
        and (std::is_const<remove_pointer_t<TP>>::value  // const-widening ok
            or not std::is_const<U>::value)  // but no const-stripping
    >;

    ExactWrapper() = default;  // compiler MIGHT need, don't corrupt [E]

    ExactWrapper(std::nullptr_t) : p {nullptr}
        {}

    template<
        typename U,
        typename D = TP,  // [3]
        typename = enable_if_t<
            std::is_pointer<D>::value
        >,
        IfExactType<U>* = nullptr
    >
    ExactWrapper(U* u) : p {x_cast(TP, u)}
        {}

    template<
        typename UP,
        typename D = TP,  // [3]
        typename = enable_if_t<
            not std::is_pointer<D>::value
        >,
        IfExactType<UP>* = nullptr
    >
    ExactWrapper(UP u) : p {u}
        {}

    template<
        typename U,
        typename D = TP,  // [3]
        typename = enable_if_t<
            std::is_pointer<D>::value
        >,
        IfExactType<U>* = nullptr
    >
    ExactWrapper(const ExactWrapper<U*>& other)
        : p {other.p}
        {}

    template<
        typename UP,
        typename D = TP,  // [3]
        typename = enable_if_t<
            not std::is_pointer<D>::value
        >,
        IfExactType<UP>* = nullptr
    >
    ExactWrapper(const ExactWrapper<UP>& other)
        : p {other.p}
        {}

    ExactWrapper(const ExactWrapper& other) : p {other.p}
        {}

    template<typename U, IfExactType<U>* = nullptr>
    ExactWrapper(const SinkWrapper<U*>& sink)
        : p {static_cast<TP>(sink)}
    {
        dont(assert(not sink.corruption_pending));  // must allow corrupt [2]
    }

    template<typename U, IfExactType<U>* = nullptr>
    ExactWrapper(const InitWrapper<U*>& init)
        : p {static_cast<TP>(init.p)}
        {}

    template<typename U, IfExactType<U>* = nullptr>
    ExactWrapper(const ContraWrapper<U*>& contra)
        : p {static_cast<TP>(contra.p)}
        {}

    ExactWrapper& operator=(std::nullptr_t) {
        this->p = nullptr;
        return *this;
    }

    ExactWrapper& operator=(const ExactWrapper& other) {
        if (this != &other) {
            this->p = other.p;
        }
        return *this;
    }

    template<typename U, IfExactType<U>* = nullptr>
    ExactWrapper& operator=(U* ptr) {
        this->p = static_cast<TP>(ptr);
        return *this;
    }

    template<typename U, IfExactType<U>* = nullptr>
    ExactWrapper& operator=(const SinkWrapper<U*>& sink) {
        dont(assert(not sink.corruption_pending));  // must allow corrupt [2]
        this->p = static_cast<TP>(sink);  // not sink.p (flush corruption)
        return *this;
    }

    template<typename U, IfExactType<U>* = nullptr>
    ExactWrapper& operator=(const InitWrapper<U*>& init) {
        this->p = static_cast<TP>(init.p);
        return *this;
    }

    template<typename U, IfExactType<U>* = nullptr>
    ExactWrapper& operator=(const ContraWrapper<U*>& contra) {
        this->p = static_cast<TP>(contra.p);
        return *this;
    }

    explicit operator bool() const { return p != nullptr; }

    operator TP() const { return p; }

    template<typename U>
    explicit operator U*() const
        { return const_cast<U*>(reinterpret_cast<const U*>(p)); }

    TP operator->() const { return p; }
};
