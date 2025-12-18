//
//  file: %needful-option.hpp
//  summary: "Optional Wrapper Trick for C's Boolean Coercible Types"
//  homepage: <needful homepage TBD>
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2025 hostilefork.com
//
// Licensed under the MIT License
//
// https://en.wikipedia.org/wiki/MIT_License
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See %needful.h for an overview of Option(T).
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. Since in C you can't stop raw pointers from being null, NonZero(T) may
//    seem better than Option(T).  That would mark when pointers were *not*
//    optional...and assume unwrapped pointers were nullable.  But in practice
//    the relative rarity of optional states would make this much more of a
//    headache, look a lot worse, and provide minimal extra benefit over the
//    more familiar Option(T) approach.
//
//    The tradeoff is made to live with ambiguity that some raw pointers are
//    nullable.  Hopefully these are at the edges of the code only, for
//    interfacing with libraries that don't use Needful.  Another place it
//    can be useful is a convenience pattern which *immediately* checks the
//    null case of an optional extraction, like:
//
//        Foo* foo = opt Some_Optional_Foo(...);
//        if (! foo)
//           return Some_Missing_Foo_Error(...);
//
//        Use_Foo(foo);  // don't have to unwrap an Option(Foo)
//        Use_Foo_Again(foo);  // used again, no unwrap needed
//


//=//// none: DISENGAGED SENTINEL /////////////////////////////////////////=//
//
// `none` constructs an Option(T) in the disengaged state.
//
// If you use this with an Option(T*), then nullptr is equivalent to `none`.
//
//     Option(char*) foo = none;         /* OK */
//     Option(char*) bar = nullptr;      /* also OK */
//     Option(char*) baz = 0;            /* compile-time error */
//
// If you use it with an enum, be sure the enum was declared with a 0 value
// that is not otherwise valid for the enum:
//
//     Option(SomeEnum) foo = none;       /* OK */
//     Option(SomeEnum) bar = nullptr;    /* compile-time error */
//     Option(SomeEnum) baz = 0;          /* compile-time error */
//

struct NoneStruct {};

#undef NeedfulNone
#define NeedfulNone  needful::NoneStruct

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
struct OptionWrapper {
    NEEDFUL_DECLARE_WRAPPED_FIELD (T, o);

    /* bool engaged; */  // unlike with std::optional, not needed! [1]

    OptionWrapper () = default;  // garbage, or 0 if global [2]

    OptionWrapper(NoneStruct)
        : o {needful_nocast_0}
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

#undef NeedfulOption
#define NeedfulOption(T)  needful::OptionWrapper<T>


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
//    like (unwrap num / 10) and it would be clear that the unwrap should
//    happen before the division (as you can't divide a wrapped Option(T)).
//
//    But interoperability with Result(T) means that postfix extraction of
//    results should ideally be higher precedence than opt or unwrap:
//
//       trap(Foo* foo = opt Some_Thing())
//
//    We have this expand out into:
//
//       Foo* foo = opt_helper + Some_Thing() % result_extractor;
//       /* more expansion of trap macro */
//
//    If the result extractor wasn't higher precedence, maybe_helper would
//    get a Result(Option(T)) and have to re-wrap that as a Result(T), which
//    makes wasteful extra objects.  It's also semantically questionable: the
//    result is conceptually on the "outside", and should extract first.
//
//    We use `+` (higher precedence than `==`) so `(unwrap foo == 10)` reads
//    cleanly. `<<` would trigger "overloaded shift vs comparison" warnings.
//
// 2. The operator for giving you back the raw (possibly null or 0) value
//    from a wrapped option is called `opt`.  It's a name with some flaws,
//    because it sort of sounds like something that would create an Option
//    from a raw pointer, vs creating a raw pointer from an Option.  However,
//    on balance it seems to be the best name (it was once called `maybe`,
//    but in the context of the system Needful was designed for, that means
//    something completely different now.)  See also: [A] at top of file.
//

struct UnwrapHelper {};
struct OptHelper {};

template<typename T>
T operator+(  // lower precedence than % [1]
    UnwrapHelper,
    const OptionWrapper<T>& option
){
    assert(option.o);  // non-null or non-zero
    return option.o;
}

template<typename T>
T operator+(  // lower precedence than % [1]
    OptHelper,
    const OptionWrapper<T>& option
){
    return option.o;
}

constexpr UnwrapHelper g_unwrap_helper = {};
constexpr OptHelper g_opt_helper = {};


#undef needful_unwrap
#define needful_unwrap \
    needful::g_unwrap_helper +  // lower precedence than % [1]

#undef needful_opt  // imperfect name for raw extract, but oh well [2]
#define needful_opt \
    needful::g_opt_helper +  // lower precedence than % [1]


//=/// BLOCK OptionWrapper() CONTRAVARIANCE ///////////////////////////////=//
//
// You want compiler errors if you write Sink(Option(T)) or Init(Option(T)).
//
// Nullability is really the *only* contravariance property that a Needful
// wrapper would have that would mess up Sink() or Init().  Hence it's an
// "opt-out" property of the wrapper.
//

template<typename T>
struct IsContravariantWrapper<OptionWrapper<T>> : std::false_type {};
