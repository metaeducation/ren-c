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
// The trick is that in a plain C build, it doesn't use a wrapper class at all.
// It falls back on the natural boolean coercibility of the standalone type.
// Hence you can only use this with things like pointers, integers or enums
// where 0 means no value.  If used in the C++ build with smart pointer
// classes, they must be boolean coercible, e.g. `operator bool() const {...}`
//
// Comparison is lenient, allowing direct comparison to the contained value.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// A. Uppercase Option() is chosen vs. option(), to keep `option` available
//    as a variable name, and to better fit the new DataType NamingConvention.
//


//=//// OPTION DEFINITION /////////////////////////////////////////////////=//
//
// 1. Because we want this to work in plain C, we can't take advantage of a
//    default construction to a zeroed value.  But we also can't disable the
//    default constructor, because we want to be able to default construct
//    structures with members that are Option().  :-(
//
// 2. While the combinatorics may seem excessive with repeating the equality
//    and inequality operators, this is the way std::optional does it too.
//
#if (! CHECK_OPTIONAL_TYPEMACRO)
    #define Option(T) T
#else
    template<typename T>
    struct OptionWrapper {
        T p;  // not always pointer, but use common convention with Sink/Need

        OptionWrapper () = default;  // garbage, or 0 if global [1]

        template <typename U>
        OptionWrapper (const U& something) : p (something)
          {}

        template <typename X>
        OptionWrapper (const OptionWrapper<X>& other)
          : p (other.p)  // necessary...won't use the (U something) template
          {}

        operator uintptr_t() const  // so it works in switch() statements
          { return u_cast(uintptr_t, p); }  // remember, may not be a pointer

        explicit operator T()  // must be an *explicit* cast
          { return p; }

        explicit operator bool() {
           // explicit exception in if https://stackoverflow.com/q/39995573/
           return p ? true : false;
        }
    };

    //=//// LABORIOUS REPEATED OPERATORS [2] //////////////////////////////=//

    template<typename L, typename R>
    bool operator==(const OptionWrapper<L>& left, const OptionWrapper<R>& right)
        { return left.p == right.p; }

    template<typename L, typename R>
    bool operator==(const OptionWrapper<L>& left, R right)
        { return left.p == right; }

    template<typename L, typename R>
    bool operator==(L left, const OptionWrapper<R>& right)
        { return left == right.p; }

    template<typename L, typename R>
    bool operator!=(const OptionWrapper<L>& left, const OptionWrapper<R>& right)
        { return left.p != right.p; }

    template<typename L, typename R>
    bool operator!=(const OptionWrapper<L>& left, R right)
        { return left.p != right; }

    template<typename L, typename R>
    bool operator!=(L left, const OptionWrapper<R>& right)
        { return left != right.p; }

    template<class P>
    INLINE void Corrupt_Pointer_If_Debug(OptionWrapper<P> &option)
      { Corrupt_Pointer_If_Debug(option.p); }

    template<class P>
    INLINE bool Is_Pointer_Corrupt_Debug(OptionWrapper<P> &option)
      { return Is_Pointer_Corrupt_Debug(option.p); }

  #if (! DEBUG_STATIC_ANALYZING)
    template<typename P>
    struct Corrupter<OptionWrapper<P>> {
      static void corrupt(OptionWrapper<P>& option) {
        Corrupt_If_Debug(option.p);
      }
    };
  #endif
#endif


//=//// UNWRAP AND MAYBE "KEYWORD-LIKE" OPERATORS /////////////////////////=//
//
// The `unwrap` operator will assert if the Option() does not contain a value.
// The `maybe` operator will give the 0/null state in the case of no value.
//
// To avoid the need for parentheses and give a "keyword" look to the `unwrap`
// and `maybe` operators they are defined as putting a global variable on the
// left of an output stream operator.  The variable holds a dummy class which
// only implements the extraction.
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

#if (! CHECK_OPTIONAL_TYPEMACRO)
    #define unwrap
    #define maybe
#else
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

    #define Option(T) OptionWrapper<T>
    #define unwrap g_unwrap_helper <<
    #define maybe g_maybe_helper <<
#endif
