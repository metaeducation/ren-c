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

//=//// none: OPTIONAL DISENGAGED STATE ///////////////////////////////////=//
//
// NoneStruct instances are a specific narrowed type used to construct an
// Option(T) for arbitrary T in the disengaged state.
//
//     Option(SomeEnum) foo = nullptr;  /* compile-time error */
//     Option(SomeEnum) bar = 0;  /* compile-time error */
//     Option(SomeEnum) baz = none;  /* OK */
//

struct NoneStruct {};

#undef needful_none
#define needful_none  needful::NoneStruct{}  // instantiate {} none instance


//=//// OPTION WRAPPER ////////////////////////////////////////////////////=//
//
// 1. Unlike std::optional, Needful's Option() can only store types that have
//    a natural empty/falsey "sentinel" state.
//
//    BUT this means Option(T) is the same size as T, with no separate boolean
//    to track the disengaged state!  Hence it is notably cheaper than
//    std::optional, and can interoperate cleanly with C code.
//
// 2. Because we want this to work in plain C, we can't take advantage of a
//    default construction to a zeroed value.  But we also can't disable the
//    default constructor, because we want to be able to default construct
//    structures with members that are Option().  :-(  Also, global variables
//    need to be compatible with the 0-initialization property they'd have
//    if they weren't marked Option().
//
// If used in the C++ build with smart pointer classes, they must be boolean
// coercible, e.g. `operator bool() const {...}`
//

template<typename T>
struct OptionNoneInitHelper {
    static T init() { return needful_xtreme_cast(T, 0); }
};

template<typename T>
struct OptionWrapper {
    NEEDFUL_DECLARE_WRAPPED_FIELD (T, o);

    /* bool engaged; */  // unlike with std::optional, not needed! [1]

    OptionWrapper () = default;  // garbage, or 0 if global [2]

    OptionWrapper(Result0Struct&&) = delete;  // only for Result(T)

    OptionWrapper(NoneStruct&&)
        : o {OptionNoneInitHelper<T>::init()}
      {}

    template <
        typename U,
        typename = enable_if_t<needful_is_convertible_v(U, T)>
    >
    OptionWrapper (U&& something)
        : o (something)  // not {something}, so narrowing conversions ok
      {}

    template <
        typename U,
        typename = enable_if_t<not needful_is_convertible_v(U, T)>
    >
    explicit OptionWrapper(const U& something)
        : o {needful_xtreme_cast(T, something)}
      {}

    template <typename X>
    OptionWrapper (const OptionWrapper<X>& other)
        : o {other.o}  // necessary...won't use the (U something) template
      {}

    template<typename U>
    explicit operator U() const  // *explicit* cast if not using `unwrap`
      { return u_cast(U, o); }

    explicit operator bool() const {
        // explicit exception in `if` https://stackoverflow.com/q/39995573/
        return o ? true : false;
    }
};

  //=//// LABORIOUS REPEATED OPERATORS ////////////////////////////////////=//

  // While the combinatorics may seem excessive with repeating the equality
  // and inequality operators, this is the way std::optional does it too.

template<typename L, typename R>
bool operator==(const OptionWrapper<L>& left, const OptionWrapper<R>& right)
  { return left.o == right.o; }

template<typename L, typename R>
bool operator==(const OptionWrapper<L>& left, R right)
  { return left.o == right; }

template<typename L, typename R>
bool operator==(L left, const OptionWrapper<R>& right)
  { return left == right.o; }

template<typename L, typename R>
bool operator!=(const OptionWrapper<L>& left, const OptionWrapper<R>& right)
  { return left.o != right.o; }

template<typename L, typename R>
bool operator!=(const OptionWrapper<L>& left, R right)
  { return left.o != right; }

template<typename L, typename R>
bool operator!=(L left, const OptionWrapper<R>& right)
  { return left != right.o; }

  //=//// CORRUPTION HELPER ///////////////////////////////////////////////=//

  // See %needful-corruption.h for motivation and explanation.

#if NEEDFUL_USES_CORRUPT_HELPER
    template<typename T>
    struct CorruptHelper<OptionWrapper<T>> {
      static void corrupt(OptionWrapper<T>& option) {
        Corrupt_If_Needful(option.o);
      }
    };
#endif

template<typename X>
struct IsOptionWrapper<OptionWrapper<X>> : std::true_type {};

#undef NeedfulOption
#define NeedfulOption(T)  needful::OptionWrapper<T>



//=//// RESULT0 INIT HELPER //////////////////////////////////////////////=//
//
// We don't want to force Option(T) to be constructible from 0, so when a
// Result(Option(T)) is constructed from NEEDFUL_RESULT_0, have that be
// done via a NoneStruct{}.
//

template<typename U>
struct Result0InitHelper<OptionWrapper<U>> {
    static OptionWrapper<U> init() { return NoneStruct{}; }
};


//=/// UNWRAP AND OPT HELPER CLASSES //////////////////////////////////////=//
//
// To avoid needing parentheses and give a "keyword" look to the `unwrap`
// and `opt` operators the C++ definition makes them put a global variable
// on the left of an output stream operator.  The variable holds a dummy
// class which only implements the extraction.
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
// 1. It might seem tempting to make the unwrap operator precedence something
//    prefix that's very high, like `~`.  This way you could write things
//    like (unwrap foo / 10) and it would be clear that the unwrap should
//    happen before the division (as you can't divide a wrapped Option(T)).
//
//    But interoperability with Result(T) means that postfix extraction of
//    results should ideally be higher precedence than opt or unwrap:
//
//       trap(Foo* foo = opt Some_Thing())
//
//    We have this expand out into:
//
//       Foo* foo = opt_helper << Some_Thing() % result_extractor;
//       /* more expansion of trap macro */
//
//    If the result extractor wasn't higher precedence, maybe_helper would
//    get a Result(Option(T)) and have to re-wrap that as a Result(T), which
//    makes wasteful extra objects.  It's also semantically questionable: the
//    result is conceptually on the "outside", and should extract first.
//
//    By choosing `<<` as the operator, at least it's higher precedence than
//    equality, so you can write (unwrap foo == 10) or (10 == unwrap foo) and
//    it should work as expected.
//
// 2. The operator for giving you back the raw (possibly null or 0) value
//    from a wrapped option is called `opt`.  It's a name with some flaws,
//    because it sort of sounds like something that would create an Option
//    from a raw pointer, vs creating a raw pointer from an Option.  However,
//    on balance it seems to be the best name (it was once called `maybe`,
//    but in the context of the system Needful was designed for, that means
//    something completely different now.)

struct UnwrapHelper {};
struct OptHelper {};

template<typename T>
T operator<<(  // lower precedence than % [1]
    const UnwrapHelper& left,
    const OptionWrapper<T>& option
){
    UNUSED(left);
    assert(option.o);  // non-null pointers or int/enum checks != 0
    return option.o;
}

template<typename T>
T operator<<(  // lower precedence than % [1]
    const OptHelper& left,
    const OptionWrapper<T>& option
){
    UNUSED(left);
    return option.o;
}

constexpr UnwrapHelper g_unwrap_helper = {};
constexpr OptHelper g_opt_helper = {};


#undef needful_unwrap
#define needful_unwrap \
    needful::g_unwrap_helper <<  // lower precedence than % [1]

#undef needful_opt  // imperfect name for raw extract, but oh well [2]
#define needful_opt \
    needful::g_opt_helper <<  // lower precedence than % [1]
