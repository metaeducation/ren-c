//
//  file: %needful-sinks.hpp
//  summary: "Contravariant type checking and corruption of output parameters"
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
// If CHECK_CELL_SUBCLASSES is enabled, the inheritance heirarchy has
// Value at the base, with Element at the top.  Since what Elements can contain
// is more constrained than what Atoms can contain, this means you can pass
// Element* to a parameter taking an Value*, but not vice-versa.
//
// However, when you have a Sink(Element) parameter instead of an Element*,
// the checking needs to be reversed.  You are -writing- an Element, so
// the receiving caller can pass an Value* and it will be okay.  But if you
// were using Sink(Value), then passing an Element* would not be okay, as
// after the initialization the Element could hold invalid states.
//
// We use "SFINAE" to selectively enable the upside-down hierarchy, based
// on a reversed usage of the `std::is_convertible<>` type trait.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. The copy-and-swap idiom doesn't seem to be very helpful here, as we
//    aren't dealing with exceptions and self-assignment has to be handled
//    manually due to the handoff of the corruption_pending flag.
//
//      https://stackoverflow.com/questions/3279543/
//
// B. While it might seem natural to use a base class to share functions
//    between SinkWrapper, InitWrapper, and ExactWrapper, this is avoided for
//    debug build performance.  Debug builds don't inline function calls, and
//    inheritance/virtual functions would add overhead to what is already
//    debug instrumentation.  The wrappers are kept separate to maintain
//    maximum performance in debug builds, which are run almost always by
//    developers, and we'd like to keep them as fast as they can be.
//
//    Though macros could be used to factor the code without runtime overhead,
//    they'd make debugging confusing, and make it harder to add specific
//    instrumentation if it were needed.
//
// C. This file names the macros [NeedfulInit NeedfulSink NeedfulExact]
//    instead of [Init Sink Exact].  This is because those short names are
//    particularly likely to be defined in existing codebases...so you can
//    #define these to whatever name is appropriate for your code.
//
// D. When doing CastHook specializations, you should not use reference
//    types.  See the documentation for CastHook for why decltype() has
//    references removed when matching the specialization, even if the convert
//    function wants to take a reference.
//
// E. In the initial design, default constructing things like SinkWrapper<>
//    was not supported.  But in MSVC it seems that some cases (for instance
//    Option(Sink(bool))) will utilize default construction in mechanics for
//    things like passing nullptr, even when a (SinkWrapper<bool> nullptr_t)
//    constructor exists.
//
//    AI seems to think MSVC is on the right side of the standard and is
//    allowed to require a default constructor.  It's not useless to be
//    able to default construct these types, but they can't give semantic
//    meanings to default construction... C builds couldn't have parity.
//
// F. C++ doesn't really let you template cast operators, so if we're going
//    to force contravariant conversions for wrapper types the "loophole"
//    you can use is to do the contravariance testing via construction and
//    then ask the type to cast to void*, and then cast to the type.  It's
//    a workaround that seems to work for wrapper types.


//=//// FORWARD DEFINITIONS ///////////////////////////////////////////////=//
//
// This makes forward declarations of the wrappers, and defines the traits
// used for contravariance checking.  Sink() and Init() have an extra ability
// to convert things that are specifically convertible to outputs...some
// special types might be willing to write data only when fresh (e.g. there
// might be some additional meaning when non-fresh, like the location could
// hold a "setter" function that would need to run vs. accepting raw bits.)
//
// 1. You can't safely extract ::type outside of a SFINAE context, so this
//    workaround exposes a static member called `enable` that can be used in
//    a `typename` context.
//

template<typename T> struct ExactWrapper;
template<typename T> struct SinkWrapper;
template<typename T> struct InitWrapper;

template<typename U, typename T>
struct AllowSinkConversion : std::false_type {};

//=//// IfOutputConvertible TEST //////////////////////////////////////////=//
//
// The premise of Needful's contravariance is that more derived classes
// represent constraints on the bits, and the base class represents a less
// constrained rule on that bits.  Hence this is illegal:
//
//     void Initialize_Derived(Sink(Base) base) { ... }
//
//     Derived* derived = ...;
//     Initialize_Derived(derived);  // ** error to avoid breaking constraint
//
// We avoid writing bits that are legal in base but not the more-constrained
// derived class.  And no writing of bits on different branches of derivation.
// This is the usual rule.
//
// But you might have cases that are exceptions
//
//     void Initialize_Derived(Sink(Base) base) { ... }
//
//     Other* has_data = ...;
//     Initialize_Derived(has_data);  // ** error to avoid breaking constraint
//
//     Init(Other*) fresh = ...;
//     Initialize_Derived(fresh);  // maybe you want exceptions for Init/Sink
//
// If your particular problem has this character, the `AllowSinkConversion`
// trait can be specialized to allow this exception.
//
// (FYI: This came up in a case of having a low-level "Slot" which could
// represent either a value or be flagged as actually describing *where* to
// write the value.  Hence you don't want to blindly overwrite the bits of
// the Slot itself--as it might be the description of where to write.  You
// have to check and consciously establish it being an indirection or not.
// BUT if the Slot is "fresh" then you know it's not the kind that holds
// an indirection...so you don't have to check and transform it to a non-Slot
// before writing it.)
//

template<typename U, typename T>
struct IfOutputConvertible2 {  // used by Init() and Sink()
    static constexpr bool value = IfContravariant<U, T>::value
        or AllowSinkConversion<U, T>::value;

    using enable = typename std::enable_if<value>;  // not ::type [1]
};


//=//// SINK() WRAPPER FOR OUTPUT PARAMETERS //////////////////////////////=//
//
// 1. The original implementation was simpler, by just doing the corruption
//    at the moment of construction.  But this faced a problem:
//
//        bool some_function(Sink(char*) out, char* in) { ... }
//
//        if (some_function(&ptr, ptr)) { ...}
//
//    If you corrupt the data at the address the sink points to, you can
//    actually be corrupting the value of a stack variable being passed as
//    another argument before it's calculated as an argument.  So deferring
//    the corruption after construction is necessary.  It's a bit tricky
//    in terms of the handoffs and such.
//

#undef NeedfulSink
#define NeedfulSink(T)  SinkWrapper<T*>

template<typename TP>
struct SinkWrapper {
    using T = remove_pointer_t<TP>;  // T* is clearer than "TP" in this class

    NEEDFUL_DECLARE_WRAPPED_FIELD (T*, p);

    mutable bool corruption_pending;  // can't corrupt on construct [1]

    template<typename U>
    using IfSinkConvertible
        = typename IfOutputConvertible2<U, T>::enable::type;

    SinkWrapper()  // compiler MIGHT need, see [E]
        : corruption_pending {false}
    {
        Corrupt_If_Needful(p);  // pointer itself, not contents!
    }

    SinkWrapper(std::nullptr_t)
        : p {nullptr},
        corruption_pending {false}
    {
    }

    SinkWrapper(Nocast0Struct)  // for Result(Sink(Element))
        : p {nullptr},
        corruption_pending {false}
    {
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper(const U& u) {
        this->p = x_cast(T*, x_cast(void*, u));  // cast workaround [F]
        this->corruption_pending = (this->p != nullptr);
    }

    SinkWrapper(const SinkWrapper& other) {
        this->p = other.p;
        this->corruption_pending = (other.p != nullptr);  // corrupt
        other.corruption_pending = false;  // we take over corrupting
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper(const ExactWrapper<U>& exact) {
        this->p = static_cast<T*>(exact.p);
        this->corruption_pending = (exact.p != nullptr);  // corrupt
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper(const SinkWrapper<U>& other) {
        this->p = reinterpret_cast<T*>(other.p);
        this->corruption_pending = (other.p != nullptr);  // corrupt
        other.corruption_pending = false;  // we take over corrupting
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper(const InitWrapper<U>& init) {
        this->p = reinterpret_cast<T*>(init.p);
        this->corruption_pending = (init.p != nullptr);  // corrupt
    }

    SinkWrapper& operator=(std::nullptr_t) {
        this->p = nullptr;
        this->corruption_pending = false;
        return *this;
    }

    SinkWrapper& operator=(const SinkWrapper& other) {
        if (this != &other) {
            this->p = other.p;
            this->corruption_pending = (other.p != nullptr);  // corrupt
            other.corruption_pending = false;  // we take over corrupting
        }
        return *this;
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper& operator=(U ptr) {
        this->p = u_cast(T*, ptr);
        this->corruption_pending = (ptr != nullptr);  // corrupt
        return *this;
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper& operator=(const ExactWrapper<U>& exact) {
        this->p = u_cast(T*, exact.p);
        this->corruption_pending = (exact.p != nullptr);  // corrupt
        return *this;
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper& operator=(const InitWrapper<U>& init) {
        this->p = u_cast(T*, init.p);
        this->corruption_pending = (init.p != nullptr);  // corrupt
        return *this;
    }

    explicit operator bool() const { return p != nullptr; }

    operator T*() const {  // corrupt before yielding pointer
        if (corruption_pending) {
            Corrupt_If_Needful(*p);  // corrupt pointed-to item
            corruption_pending = false;
        }
        return p;
    }

    template<typename U>
    explicit operator U*() const {  // corrupt before yielding pointer
        if (corruption_pending) {
            Corrupt_If_Needful(*p);  // corrupt pointed-to item
            corruption_pending = false;
        }
        return const_cast<U*>(reinterpret_cast<const U*>(p));
    }

    T* operator->() const {  // handle corruption before dereference
        if (corruption_pending) {
            Corrupt_If_Needful(*p);  // corrupt pointed-to item
            corruption_pending = false;
        }
        return p;
    }

    ~SinkWrapper() {  // make sure we don't leave scope without corrupting
        if (corruption_pending)
            Corrupt_If_Needful(*p);  // corrupt pointed-to item
    }
};


//=//// HOOK TO CORRUPT *POINTER ITSELF* INSIDE SINK(T) ///////////////////=//
//
// Usually when we think about Sinks and corruption, it's about corrupting
// the pointed-to data.  But sometimes we want to corrupt the pointer itself.
//
//     void Perform_Assignment_Maybe(Sink(int) out, bool assign) {
//         if (not assign)
//             Corrupt_If_Needful(out);  /* corrupt the pointer itself */
//
//         *out = 42;  /* we want unguarded write to crash if not assign */
//     }
//
// (This also would happen if we said UNUSED(out))
//
// The default implementation of Corrupt_if_Debug() would corrupt all the
// bytes in a struct.  We don't want to do that for SinkWrapper<T> because
// it would corrupt the corruption_pending flag itself as well...leading
// to a situation where it might think it needs to corrupt the pointed-to
// data when the pointer itself is actually corrupt...which would crash at
// a seemingly random moment.
//
// So we do just a pointer corruption, and clear the corruption_pending flag
// so it doesn't try to corrupt the pointed-to data at the bad pointer.
//
#if NEEDFUL_USES_CORRUPT_HELPER
    template<typename T>
    struct CorruptHelper<SinkWrapper<T*>&> {  // C pointer corrupt fails
      static void corrupt(SinkWrapper<T*>& wrapper) {
        Corrupt_If_Needful(wrapper.p);  // pointer itself (not contents)
        wrapper.corruption_pending = false;
      }
    };
#endif


//=//// INIT() AS (USUALLY) FAST VARIANT OF SINK() ////////////////////////=//
//
// When we write initialization routines, the output is technically a Sink(),
// in the sense that it's intended to be overwritten.  But Sink() has a cost
// since it corrupts the target.  It's unlikely to help catch bugs with
// initialization, because Init_Xxx() routines are typically not code with
// any branches in it that might fail to overwrite the cell.
//
// This defines Init() as typically just being a class that squashes any
// pending corruptions.  So all it's doing is the work to make sure that the
// caller's pointer can legitimately store the subclass, without doing any
// corrupting of the cell.
//
// BUT if you want to double check the initializations, it should still work
// to make Init() equivalent to Sink() and corrupt the cell.  It's not likely
// to catch many bugs...but it could, so doing it occasionally might be a
// good idea.  Just do:
//
//     #define DEBUG_CHECK_INIT_SINKS  1  // Init() => actually Sink()
//

#if !defined(DEBUG_CHECK_INIT_SINKS)
    #define DEBUG_CHECK_INIT_SINKS  0
#endif

#if DEBUG_CHECK_INIT_SINKS
    #undef NeedfulInit
    #define NeedfulInit(T)  SinkWrapper<T*>
#else
    #undef NeedfulInit
    #define NeedfulInit(T)  InitWrapper<T*>
#endif

template<typename TP>
struct InitWrapper {
    using T = remove_pointer_t<TP>;  // T* is clearer than "TP" in this class

    NEEDFUL_DECLARE_WRAPPED_FIELD (T*, p);

    template<typename U>
    using IfInitConvertible
        = typename IfOutputConvertible2<U, T>::enable::type;

    InitWrapper() {  // compiler might need, see [E]
        dont(Corrupt_If_Needful(p));  // lightweight behavior vs. Sink()
    }

    InitWrapper(std::nullptr_t) : p {nullptr}
        {}

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper(const U& u) {
        this->p = x_cast(T*, x_cast(void*, u));  // cast workaround [F]
    }

    InitWrapper(const InitWrapper& other) : p {other.p}
        {}

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper(const InitWrapper<U>& init)
        : p {reinterpret_cast<T*>(init.p)}
        {}

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper(const ExactWrapper<U>& need)
        : p {static_cast<T*>(need.p)}
        {}

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper(const SinkWrapper<U>& sink) {
        this->p = static_cast<T*>(sink.p);
        sink.corruption_pending = false;  // squash corruption
    }

    InitWrapper& operator=(std::nullptr_t) {
        this->p = nullptr;
        return *this;
    }

    InitWrapper& operator=(const InitWrapper& other) {
        if (this != &other) {
            this->p = other.p;
        }
        return *this;
    }

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper& operator=(U ptr) {
        this->p = u_cast(T*, ptr);
        return *this;
    }

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper& operator=(const ExactWrapper<U>& need) {
        this->p = static_cast<T*>(need.p);
        return *this;
    }

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper& operator=(const SinkWrapper<U>& sink) {
        this->p = static_cast<T*>(sink.p);
        sink.corruption_pending = false;  // squash corruption
        return *this;
    }

    explicit operator bool() const { return p != nullptr; }

    operator T*() const { return p; }

    template<typename U>
    explicit operator U*() const
        { return const_cast<U*>(reinterpret_cast<const U*>(p)); }

    T* operator->() const { return p; }
};


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
    ExactWrapper<TP>  // * not implicit [1]

template<typename U, typename T>
struct IfExactType2 {
    static constexpr bool value = std::is_same<U, T>::value;
    using enable = typename std::enable_if<value>;  // not ::type [1]
};

template<typename TP>  // TP may or may not be a pointer type
struct ExactWrapper {
    NEEDFUL_DECLARE_WRAPPED_FIELD (TP, p);

    using MTP = needful_unconstify_t(TP);  // mutable type

    using T = remove_pointer_t<MTP>;

    template<typename U>
    using IfExactType
        = typename IfExactType2<needful_unconstify_t(U), T>::enable::type;

    ExactWrapper()  // compiler MIGHT need, see [E]
        { dont(Corrupt_If_Needful(p)); }  // may be zero in global scope

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
    ExactWrapper(const SinkWrapper<U*>& sink) {
        dont(assert(not sink.corruption_pending));  // must allow corrupt [2]
        this->p = static_cast<T*>(sink);  // not sink.p (flush corruption)
    }

    template<typename U, IfExactType<U>* = nullptr>
    ExactWrapper(const InitWrapper<U*>& init)
        : p {static_cast<TP>(init.p)}
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

    explicit operator bool() const { return p != nullptr; }

    operator TP() const { return p; }

    template<typename U>
    explicit operator U*() const
        { return const_cast<U*>(reinterpret_cast<const U*>(p)); }

    TP operator->() const { return p; }
};
