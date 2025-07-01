//
//  file: %needful-option.h
//  summary: "Optional Wrapper Trick for C's Boolean Coercible Types"
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
// Option() is a light wrapper class that uses a trick to provide limited
// functionality in the vein of `std::optional` and Rust's `Option`:
//
//     Option(char*) abc = "abc";
//     Option(char*) xxx = nullptr;
//
//     if (abc)
//        printf("abc is truthy, so `unwrap abc` is safe!\n")
//
//     if (xxx)
//        printf("XXX is falsey, so don't `unwrap xxx`...\n")
//
//     char* s1 = abc;                  // **compile time error
//     Option(char*) s2 = abc;          // legal
//
//     char* s3 = unwrap xxx;           // **runtime error
//     char* s4 = maybe xxx;            // gets nullptr out
//
// The trick is that in plain C builds, it doesn't use a wrapper class at all.
// It falls back on the natural boolean coercibility of the standalone type.
// Hence you can only use this with things like pointers, integers or enums
// where 0 means no value.  If used in the C++ build with smart pointer
// classes, they must be boolean coercible, e.g. `operator bool() const {...}`
//
// Comparison is lenient, allowing direct comparison to the contained value.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// A. Unlike std::optional, Needful's Option() can only store types that have
//    a natural empty/falsey "sentinel" state.
//
//    BUT this means Option(T) is the same size as T, with no separate boolean
//    to track the disengaged state!  Hence it is notably cheaper than
//    std::optional, and can interoperate cleanly with C code.
//


//=//// OPTION MACRO DEFINITIONS //////////////////////////////////////////=//
//
// The `unwrap` operator will assert if the Option() does not contain a value.
// The `maybe` operator will give the 0/null state in the case of no value.
//
// To avoid the need for parentheses and give a "keyword" look to the `unwrap`
// and `maybe` operators they are defined in C++ as putting a global variable
// on the left of an output stream operator.  The variable holds a dummy class
// which only implements the extraction.
//
//    Option(Foo*) foo = ...;
//    if (foo)
//        Some_Function(unwrap foo)
//
//    /* because we have `#define unwrap g_unwrap_helper <<`, this makes...*/
//
//    Option(Foo*) foo = ...;
//    if (foo)
//        Some_Function(g_unwrap_helper << foo)
//

#if (NEEDFUL_OPTION_USES_WRAPPER)
    #define OptionTypemacro(T)  OptionWrapper<T>

    #define unwrap_option  g_unwrap_helper <<
    #define maybe_option  g_maybe_helper <<
#else
    #define OptionTypemacro(T)  T  // no-op in plain C

    #define unwrap_option
    #define maybe_option
#endif

#if NEEDFUL_USE_DEFAULT_SHORTHANDS
    #define Option  OptionTypemacro
    #define unwrap  unwrap_option
    #define maybe   maybe_option
#endif


//=//// OPTION WRAPPER C++ TEMPLATED CLASS ////////////////////////////////=//
//
// 1. Because we want this to work in plain C, we can't take advantage of a
//    default construction to a zeroed value.  But we also can't disable the
//    default constructor, because we want to be able to default construct
//    structures with members that are Option().  :-(  Also, global variables
//    need to be compatible with the 0-initialization property they'd have
//    if they weren't marked Option().
//
// 2. If doing something like `u_cast(Option(SomeEnum), 17)` there has to be
//    a universal reference to grab onto these constants.  Make the explicit
//    converting constructor willing to do such conversions use &&.
//
// 3. For convenience, an Option(SomeEnum) is allowed to work in switch()
//    statements without unwrapping it.  Also, wrapper classes which may
//    be able to convert to uintptr_t are allowed.  Raw pointers are not.
//
#if NEEDFUL_OPTION_USES_WRAPPER
  template<typename T>
  struct OptionWrapper {
    T p;  // not always pointer, but use common convention with Sink/Need

    /* bool engaged; */  // unlike with std::optional, not needed! [A]

    static_assert(
        not std::is_same<T, uintptr_t>::value,
        "Option(uintptr_t) is implemented via specialization (see below)"
    );

    OptionWrapper () = default;  // garbage, or 0 if global [1]

    OptionWrapper(const PermissiveZero&)  // see definition of PERMISSIVE_ZERO
        : p (u_cast(T, PERMISSIVE_ZERO))
      {}

    template <typename U>
    OptionWrapper (const U& something)
        : p (something)
      {}

    template <typename U>
    explicit OptionWrapper(U&& something)  // needed for Byte->enum [2]
        : p (u_cast(T, std::forward<U>(something)))
      {}

    template <typename X>
    OptionWrapper (const OptionWrapper<X>& other)
        : p (other.p)  // necessary...won't use the (U something) template
      {}

    operator uintptr_t() const {  // to work in switch() cases [3]
        static_assert(
            std::is_enum<T>::value or std::is_class<T>::value,
            "non-explicit Option() -> uintptr_t() only for enum/class/struct"
        );
        return u_cast(uintptr_t, p);  // enum/class/struct, not a pointer!
    }

    template<typename U>
    explicit operator U() const  // *explicit* cast if not using `unwrap`
      { return u_cast(U, p); }  // remember: p not always a pointer

    explicit operator bool() const {
        // explicit exception in `if` https://stackoverflow.com/q/39995573/
        return p ? true : false;
    }
  };

  //=//// SPECIALIZATION FOR uintptr_t (YES, IT'S NECESSARY) //////////////=//

  // We want enum and class types to have an implicit `operator uintptr_t()`
  // defined, so Option(SomeEnum) can be used in switch() statements without
  // having to be unwrapped.  But in order to allow Option(uintptr_t) to be
  // used, there can't be an implicit `operator uintptr_t()` defined in it.
  //
  // There's sadly no way to exclude a conversion operator to a built-in type
  // using SFINAE.  Specialization is the only way to do it.  :-(
  //
  // NOTE: For comments, see the unspecialized definition above.

  template<>
  struct OptionWrapper<uintptr_t> {
    uintptr_t p;

    OptionWrapper () = default;

    OptionWrapper(const PermissiveZero&)
        : p (u_cast(uintptr_t, 0))
      {}

    template <typename U>
    OptionWrapper (const U& something)
        : p (something)
      {}

    template <typename U>
    explicit OptionWrapper(U&& something)
        : p (u_cast(uintptr_t, std::forward<U>(something)))
      {}

    template <typename X>
    OptionWrapper (const OptionWrapper<X>& other)
        : p (other.p)
      {}

    explicit operator uintptr_t() const
      { return p; }

    explicit operator bool() const
      { return p ? true : false; }
  };

  //=//// LABORIOUS REPEATED OPERATORS ////////////////////////////////////=//

  // While the combinatorics may seem excessive with repeating the equality
  // and inequality operators, this is the way std::optional does it too.

    template<typename L, typename R>
    bool operator==(
        const OptionWrapper<L>& left, const OptionWrapper<R>& right
    ){
        return left.p == right.p;
    }

    template<typename L, typename R>
    bool operator==(const OptionWrapper<L>& left, R right)
      { return left.p == right; }

    template<typename L, typename R>
    bool operator==(L left, const OptionWrapper<R>& right)
      { return left == right.p; }

    template<typename L, typename R>
    bool operator!=(
        const OptionWrapper<L>& left, const OptionWrapper<R>& right
    ){
        return left.p != right.p;
    }

    template<typename L, typename R>
    bool operator!=(const OptionWrapper<L>& left, R right)
      { return left.p != right; }

    template<typename L, typename R>
    bool operator!=(L left, const OptionWrapper<R>& right)
      { return left != right.p; }

  //=//// CORRUPTION HELPER ///////////////////////////////////////////////=//

  // See %needful-corruption.h for motivation and explanation.

  #if NEEDFUL_USES_CORRUPT_HELPER
    template<typename P>
    struct CorruptHelper<OptionWrapper<P>> {
      static void corrupt(OptionWrapper<P>& option) {
        Corrupt_If_Needful(option.p);
      }
    };
  #endif
#endif


//=/// UNWRAP AND MAYBE HELPER CLASSES ///////////////////////////////////=//
//
// Global const classes used by `unwrap` and `maybe` in the C++ Option(),
// designed to appear on the left-hand side of an << operator.
//
#if NEEDFUL_OPTION_USES_WRAPPER
    struct UnwrapHelper {};
    struct MaybeHelper {};

    template<typename T>
    T operator<<(
        const UnwrapHelper& left,
        const OptionWrapper<T>& option
    ){
        UNUSED(left);
        assert(option.p);  // non-null pointers or int/enum checks != 0
        return option.p;
    }

    template<typename T>
    T operator<<(
        const MaybeHelper& left,
        const OptionWrapper<T>& option
    ){
        UNUSED(left);
        return option.p;
    }

    constexpr UnwrapHelper g_unwrap_helper = {};
    constexpr MaybeHelper g_maybe_helper = {};
#endif
