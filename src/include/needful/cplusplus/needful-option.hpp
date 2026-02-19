//
//  file: %needful-option.hpp
//  summary: "Optional Wrapper Trick for C's Boolean Coercible Types"
//  homepage: <needful homepage TBD>
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2026 hostilefork.com
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
// A. Since in C you can't stop raw pointers from being null, Need(T) may
//    seem better than Option(T).  That would mark when pointers were *not*
//    optional...and assume unwrapped pointers were nullable.  But in practice
//    the relative rarity of optional states would make this more heavyweight
//    and look worse.  So Need(T) is saved for special cases only--where there
//    is an emergent risk to someone thinking they can test for null.
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

#undef NeedfulNone
#define NeedfulNone  needful::NoneStruct

#undef needful_none
#define needful_none  needful::NoneStruct{}  // instantiate {} none instance


//=//// OPTION WRAPPER ////////////////////////////////////////////////////=//
//
// 1. `T` must be explicitly bool-coercible.  `std::is_convertible<T, bool>`
//    doesn't check for that, so use our C++11-compatible shim for C++20.
//
//    This means that obviously, things like Option(Need(T)) cannot work.
//    But if not obvious, a clear error helps people who are confused.
//
// 2. Unlike std::optional, Needful's Option() can only store types that have
//    a natural empty/falsey "sentinel" state.
//
//    BUT this means Option(T) is the same size as T, with no separate boolean
//    to track the disengaged state!  Hence it is notably cheaper than
//    std::optional, and can interoperate cleanly with C code.
//
// 3. Since we want Option to work in plain C, we can't do defaulting to a
//    zeroed value.  But we also can't disable the default constructor,
//    because we want to default construct structures with Option members.
//
//    Also: global variables need to be compatible with the 0-initialization
//    property they'd have if they weren't marked as Option().
//
// 4. We want to avoid situations where Option(T) variables are assigned the
//    results of Need(T) functions, because this creates a misleading
//    situation where the variable may be boolean tested.  However C++ does
//    not distinguish between creating a variable or passing to a function
//    argument that is merely permissive.  The compromise chosen is to block
//    prvalue Need(T) from implicitly constructing Option(T), with `needed`
//    as an extractor for the raw value (or you could store in a variable).
//
// 5. Hookable_Cast_Helper blocks cast() from stripping Option().  But
//    x_cast() and other explicit conversions are allowed here, since they
//    indicate the caller is deliberately extracting.
//

#undef NeedfulOption
#define NeedfulOption(T)  needful::OptionWrapper<T>

template<typename>
struct IsOptionWrapper : std::false_type {};

template<typename T>
struct OptionWrapper {
    static_assert(
        needful_is_explicitly_convertible_v(T, bool),
        "T used with Option(T) must be explicitly convertible to bool"  // [1]
    );

    NEEDFUL_DECLARE_WRAPPED_FIELD (T, o);

    /* bool engaged; */  // unlike with std::optional, not needed! [2]

    OptionWrapper () = default;  // garbage, or 0 if global [3]

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

    template<typename U>  // block *prvalue* Need(T) specifically [4]
    OptionWrapper(NeedWrapper<U>&&) = delete;

    template<typename U>
    explicit operator U() const {
        return u_cast(U, o);  // cast() blocks removal, for x_cast() only [5]
    }

    explicit operator bool() const {  // explicit exception in `if`
        return o ? true : false;  // https://stackoverflow.com/q/39995573/
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

template<typename X>  // Option carries engaged/disengaged semantics [8]
struct IsWrapperSemantic<OptionWrapper<X>> : std::true_type {};


//=/// UNWRAP HOOK FOR Optional(T) ////////////////////////////////////////=//
//
// Use `unwrap` when you're sure that an optional contains a value (typically
// known by doing a conditional check):
//
//    Option(Foo*) foo = ...;
//    if (foo)
//        Some_Function(unwrap foo)
//
//    /* we have `#define unwrap needful::g_unwrap_helper +` so we get... */
//
//    Option(Foo*) foo = ...;
//    if (foo)
//        Some_Function(g_unwrap_helper + foo)
//
// 1. See the definition of UnwrapHelper for mechanics of how this "keyword"
//    is accomplished (and why the `+` operator was chosen specifically).
//

template<typename T>
T operator+(  // lower precedence than % [1]
    UnwrapHelper,
    const OptionWrapper<T>& option
){
    assert(option.o);  // non-null or non-zero
    return option.o;
}


//=/// OPT HELPER CLASS ///////////////////////////////////////////////////=//
//
// The operator for giving you back the raw (possibly null or 0) value from a
// wrapped Option(T) is called `opt`.
//
// 1. `opt` is a name with some flaws, as it sort of sounds like something
//    that creates an Option from a raw pointer, vs. creating a raw pointer
//    from an Option.  However, on balance it seems to be the best name.
//
//    (It was once called `maybe`, but in the context of the system Needful
//    was designed for, that means something completely different now.)
//
//    See also: [A] at top of file.
//
// 2. See the definition of UnwrapHelper for mechanics of how this "keyword"
//    is accomplished (and why the `+` operator was chosen specifically).

struct OptHelper {};
constexpr OptHelper g_opt_helper = {};

#undef needful_opt  // imperfect name for raw extract, but oh well [1]
#define needful_opt \
    needful::g_opt_helper +  // lower precedence than % [2]


template<typename T>
T operator+(  // lower precedence than % [2]
    OptHelper,
    const OptionWrapper<T>& option
){
    return option.o;
}


//=/// BLOCK `needed` ON OptionWrapper ////////////////////////////////////=//
//
// The `needed` operator is only valid on Need(T), not on Option(T).  If you
// try to use `needed` on an Option(T), it's a compile-time error.  This makes
// `needed` a useful building block for macros that reject optional types.
//
template<typename T>
T operator+(
    NeededHelper,
    const OptionWrapper<T>&
){
    static_assert(
        sizeof(T) != sizeof(T),  // dependent false
        "cannot use `need` on an Option(T) -- use `opt` or `unwrap` instead"
    );
    return *static_cast<T*>(nullptr);  // unreachable
}


//=/// BLOCK OptionWrapper() CONTRAVARIANCE ///////////////////////////////=//
//
// While OptionWrapper() is a "wrapped type", you don't want to be able to
// pass an Option(T*) to a Sink(T).  This is because Option(T*) might be
// disengaged (nullptr)... there may be no storage to write through to.  So
// it can't behave like its "wrapped type" in that situation.
//
// We specialize IsContravariant directly to always return false.
//
template<typename T, typename Target>
struct IsContravariant<OptionWrapper<T>, Target, true> : std::false_type {};
