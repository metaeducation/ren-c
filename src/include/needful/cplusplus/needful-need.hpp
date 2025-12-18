//
//  file: %needful-need.hpp
//  summary: "Need Wrapper Trick to get Non-Boolean-Coercible Types"
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
#define NeedfulNeed(TP) \
    needful::NeedWrapper<TP>  // * not implicit [1]


template<typename U, typename T>
struct IfCovariant2 {
    static constexpr bool value = std::is_convertible<U, T>::value;
    using enable = typename std::enable_if<value>;  // not ::type [1]
};

template<typename TP>  // TP may or may not be a pointer type
struct NeedWrapper {
  private:  // even private deleted operators get considered for overload [2]
    /* operator bool() const = delete; */

  public:
    NEEDFUL_DECLARE_WRAPPED_FIELD (TP, p);

    using T = remove_pointer_t<TP>;

    template<typename U>
    using IfCovariant
        = typename IfCovariant2<U, T>::enable::type;

    NeedWrapper() = default;  // compiler MIGHT need, see [E]

    NeedWrapper(std::nullptr_t) = delete;
    NeedWrapper(NoneStruct) = delete;

    // enable x_cast() from void* to work around casting issues [F]
    //
    explicit NeedWrapper(void* p) : p {x_cast(TP, p)} {}
    explicit NeedWrapper(const void* p) : p {x_cast(TP, p)} {}

    template<
        typename U,
        typename D = TP,  // [3]
        typename = enable_if_t<
            std::is_pointer<D>::value
        >,
        IfCovariant<U>* = nullptr
    >
    NeedWrapper(U* u) : p {x_cast(TP, u)}
        {}

    template<
        typename UP,
        typename D = TP,  // [3]
        typename = enable_if_t<
            not std::is_pointer<D>::value
        >,
        IfCovariant<UP>* = nullptr
    >
    NeedWrapper(UP u) : p {u}
        {}

    template<
        typename U,
        typename D = TP,  // [3]
        typename = enable_if_t<
            std::is_pointer<D>::value
        >,
        IfCovariant<U>* = nullptr
    >
    NeedWrapper(const NeedWrapper<U*>& other)
        : p {other.p}
        {}

    template<
        typename UP,
        typename D = TP,  // [3]
        typename = enable_if_t<
            not std::is_pointer<D>::value
        >,
        IfCovariant<UP>* = nullptr
    >
    NeedWrapper(const NeedWrapper<UP>& other)
        : p {other.p}
        {}

    NeedWrapper(const NeedWrapper& other) : p {other.p}
        {}

    template<typename U, IfCovariant<U>* = nullptr>
    NeedWrapper(const SinkWrapper<U*>& sink)
        : p {static_cast<TP>(sink)}
    {
        dont(assert(not sink.corruption_pending));  // must allow corrupt [2]
    }

    template<typename U, IfCovariant<U>* = nullptr>
    NeedWrapper(const InitWrapper<U*>& init)
        : p {static_cast<TP>(init.p)}
        {}

    NeedWrapper& operator=(std::nullptr_t) = delete;
    NeedWrapper& operator=(NoneStruct) = delete;

    NeedWrapper& operator=(const NeedWrapper& other) {
        if (this != &other) {
            this->p = other.p;
        }
        return *this;
    }

    template<typename U, IfCovariant<U>* = nullptr>
    NeedWrapper& operator=(U* ptr) {
        this->p = static_cast<TP>(ptr);
        return *this;
    }

    template<typename U, IfCovariant<U>* = nullptr>
    NeedWrapper& operator=(const SinkWrapper<U*>& sink) {
        dont(assert(not sink.corruption_pending));  // must allow corrupt [2]
        this->p = static_cast<TP>(sink);  // not sink.p (flush corruption)
        return *this;
    }

    template<typename U, IfCovariant<U>* = nullptr>
    NeedWrapper& operator=(const InitWrapper<U*>& init) {
        this->p = static_cast<TP>(init.p);
        return *this;
    }

    operator TP() const { return p; }

    operator ExactWrapper<needful_constify_t(TP)>() const { return p; }

    template<typename U>
    explicit operator U*() const
        { return const_cast<U*>(reinterpret_cast<const U*>(p)); }

    TP operator->() const { return p; }
};

  //=//// LABORIOUS REPEATED OPERATORS ////////////////////////////////////=//

  // While the combinatorics may seem excessive with repeating the equality
  // and inequality operators, this is the way std::optional does it too.

template<typename L, typename R>
bool operator==(const NeedWrapper<L>& left, const NeedWrapper<R>& right)
  { return left.p == right.p; }

template<typename L, typename R>
bool operator==(const NeedWrapper<L>& left, R right)
  { return left.p == right; }

template<typename L, typename R>
bool operator==(L left, const NeedWrapper<R>& right)
  { return left == right.p; }

template<typename L, typename R>
bool operator!=(const NeedWrapper<L>& left, const NeedWrapper<R>& right)
  { return left.p != right.p; }

template<typename L, typename R>
bool operator!=(const NeedWrapper<L>& left, R right)
  { return left.p != right; }

template<typename L, typename R>
bool operator!=(L left, const NeedWrapper<R>& right)
  { return left != right.p; }


//=/// UNWRAP HELPER CLASS ////////////////////////////////////////////////=//
//
// To avoid needing parentheses and give a "keyword" look to the `unwrap`
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
