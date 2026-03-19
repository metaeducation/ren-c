//
//  file: %needful-need.hpp
//  summary: "Need Wrapper Trick to get Non-Boolean-Coercible Types"
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
// See %needful.h for an overview of Need(T).
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
//


//=//// NEED() FOR COVARIANT NON-BOOLEAN-COERCIBLE INPUT PARAMETERS ///////=//
//
// Need() implements covariance...for types that are not supposed to be null
// or zero, and hence disable boolean coercion.
//
// Because it doesn't define every operation you might want to use on the
// contained type, it works with unwrap() to extract the value.  But it does
// have basic equality and inequality.
//
// 1. Works with pointers or non-pointers (so the pointer is not implicit
//    as it is with Sink() and Init()).  If it is a pointer, then TP is
//    destructured into T for the pointed-to type.
//
// 2. The primary purpose of the existence of Need() is to stop implicit
//    conversions to bool.  But if we explicitly say we delete it, then that
//    winds up being considered for overload resolution--even if private.
//    This is apparently by design--leaving a comment is the only workaround.
//
// 3. Non-dependent enable_if conditions work in MSVC, but GCC has trouble
//    with them.  Introducing a dependent type seems to help it along.


#undef NeedfulNeed
#define NeedfulNeed(T) \
    needful::NeedWrapper<T>  // * not implicit [1]

template<typename T>
struct NeedWrapper {
  private:  // even private deleted operators get considered for overload [2]
    /* operator bool() const = delete; */

  public:
    NEEDFUL_DECLARE_WRAPPED_FIELD (T, n);

    NeedWrapper() = default;  // need compatibility with C build behavior

    NeedWrapper(std::nullptr_t) = delete;
    NeedWrapper(NoneStruct) = delete;

    template <
        typename U,
        typename = enable_if_t<not needful_is_convertible_v(U, T)>
    >
    explicit NeedWrapper(const U& something)
        : n {needful_xtreme_cast(T, something)}
      { assert(n); }

    template <
        typename U,
        typename = enable_if_t<needful_is_convertible_v(U, T)>
    >
    NeedWrapper(U&& u) : n {static_cast<T>(u)} { assert(n); }

    template <
        typename U,
        typename = enable_if_t<needful_is_convertible_v(U, T)>
    >
    NeedWrapper(const NeedWrapper<U>& other) : n {other.n}
        {}  // shouldn't need to assert(n)

    NeedWrapper(const NeedWrapper& other) : n {other.n}
        {}  // shouldn't need to assert(n)

    NeedWrapper& operator=(std::nullptr_t) = delete;
    NeedWrapper& operator=(NoneStruct) = delete;

    NeedWrapper& operator=(const NeedWrapper& other) {
        if (this != &other) {
            this->n = other.n;
            assert(this->n);
        }
        return *this;
    }

    template<
        typename U,
        typename = enable_if_t<needful_is_convertible_v(U, T)>
    >
    NeedWrapper& operator=(U&& u) {
        this->n = static_cast<T>(u);
        assert(this->n);
        return *this;
    }

    operator T() const { return n; }

    operator ExactWrapper<needful_constify_t(T)>() const { return n; }

    template<typename U>
    explicit operator U() const
        { return static_cast<U>(n); }

    T operator->() const { return n; }
};

  //=//// LABORIOUS REPEATED OPERATORS ////////////////////////////////////=//

  // While the combinatorics may seem excessive with repeating the equality
  // and inequality operators, this is the way std::optional does it too.

template<typename L, typename R>
bool operator==(const NeedWrapper<L>& left, const NeedWrapper<R>& right)
  { return left.n == right.n; }

template<typename L, typename R>
bool operator==(const NeedWrapper<L>& left, R right)
  { return left.n == right; }

template<typename L, typename R>
bool operator==(L left, const NeedWrapper<R>& right)
  { return left == right.n; }

template<typename L, typename R>
bool operator!=(const NeedWrapper<L>& left, const NeedWrapper<R>& right)
  { return left.n != right.n; }

template<typename L, typename R>
bool operator!=(const NeedWrapper<L>& left, R right)
  { return left.n != right; }

template<typename L, typename R>
bool operator!=(L left, const NeedWrapper<R>& right)
  { return left != right.n; }


//=/// UNWRAP HELPER CLASS (LEGAL ON Need(), NOT JUST Option()) ///////////=//
//
// To avoid requiring parentheses and give a "keyword" look to the `unwrap`
// operator, the C++ definition makes them put a global variable on the left
// of an output stream operator.  The variable holds a dummy class which only
// implements the extraction.
//
//    Option(Foo*) foo = ...;
//    if (foo)
//        Some_Function(unwrap foo)
//
//    /* we have `#define unwrap needful::g_unwrap_helper +` so we get... */
//
//    Option(Foo*) foo = ...;
//    if (foo)
//        Some_Function(needful::g_unwrap_helper + foo)
//
// 1. It might seem tempting to make the unwrap operator precedence something
//    prefix that's very high, like `~`.  This way you could write things
//    like (unwrap num / 10) and it would be clear that the unwrap should
//    happen before the division (as you can't divide a wrapped Option(T)).
//
//    But interoperability with Result(T) means that postfix extraction of
//    results should ideally be higher precedence than opt or unwrap:
//
//       trap(Foo* foo = unwrap Some_Api())
//
//    We have this expand out into:
//
//       Foo* foo = needful::g_unwrap_helper + Some_Api() % result_extractor;
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

struct UnwrapHelper {};
constexpr UnwrapHelper g_unwrap_helper = {};

#undef needful_unwrap
#define needful_unwrap \
    needful::g_unwrap_helper +  // lower precedence than % [1]


template<typename T>
T operator+(  // lower precedence than % [1]
    UnwrapHelper,
    const NeedWrapper<T>& need
){
    return need.p;  // never allowed to be zero or null
}


//=/// "NEEDED" HELPER CLASS //////////////////////////////////////////////=//
//
// The `needed` operator extracts the raw value from Need(T), analogous to how
// `opt` extracts from Option(T).  The key difference is that `needed` gives a
// compile-time error on Option(T), making it useful as a static filter:
//
//    | Operator | On Option(T)  | On Need(T) |
//    |----------|---------------|------------|
//    | opt      | Extract raw   | Error      |
//    | needed   | Error         | Extract raw|
//    | unwrap   | Assert+Extract| Extract    |
//
// This is useful for building macros that predictably reject optional types.
//

struct NeededHelper {};
constexpr NeededHelper g_needed_helper = {};

#undef needful_needed
#define needful_needed \
    needful::g_needed_helper +  // lower precedence than %


template<typename T>
T operator+(  // lower precedence than %
    NeededHelper,
    const NeedWrapper<T>& need
){
    return need.p;  // never allowed to be zero or null
}
