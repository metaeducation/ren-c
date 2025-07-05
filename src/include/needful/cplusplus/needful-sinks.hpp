//
//  file: %needful-sinks.h
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

//
// e.g. if CHECK_CELL_SUBCLASSES is enabled, the inheritance heirarchy has
// Atom at the base, with Element at the top.  Since what Elements can contain
// is more constrained than what Atoms can contain, this means you can pass
// Element* to a parameter taking an Atom*, but not vice-versa.
//
// However, when you have a Sink(Element) parameter instead of an Element*,
// the checking needs to be reversed.  You are -writing- an Element, so
// the receiving caller can pass an Atom* and it will be okay.  But if you
// were using Sink(Atom), then passing an Element* would not be okay, as
// after the initialization the Element could hold invalid states.
//
// We use "SFINAE" to selectively enable the upside-down hierarchy, based
// on the `std::is_base_of<>` type trait.
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
//    between SinkWrapper, InitWrapper, and NeedWrapper, this is avoided for
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
// C. This file names the macros [NeedfulInit NeedfulSink NeedfulNeed]
//    instead of [Init Sink Need].  This is because those short names are
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

template<typename T> struct NeedWrapper;
template<typename T> struct SinkWrapper;
template<typename T> struct InitWrapper;

template<typename U, typename T>
struct AllowSinkConversion : std::false_type {};

template<typename U, typename T>
struct IfReverseInheritable2 {  // used by Need()
    static constexpr bool value
        = std::is_same<U, T>::value or std::is_base_of<U, T>::value;
    using enable = typename std::enable_if<value>;  // not ::type [1]
};

template<typename U, typename T>
struct IfOutputConvertible2 {  // used by Init() and Sink()
    static constexpr bool value = IfReverseInheritable2<U, T>::value
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
    using wrapped_type = TP;
    using T = typename std::remove_pointer<TP>::type;

    T* p;
    mutable bool corruption_pending;  // can't corrupt on construct [1]

    using MT = typename std::remove_const<T>::type;  // mutable type

    template<typename U>
    using IfSinkConvertible
        = typename IfOutputConvertible2<U, T>::enable::type;

    SinkWrapper()  // compiler MIGHT need [E]
        : corruption_pending {false}
    {
        Corrupt_If_Needful(p);  // pointer itself, not contents!
    }

    SinkWrapper(std::nullptr_t)
        : p {nullptr},
        corruption_pending {false}
    {
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper(U* u) {
        this->p = static_cast<T*>(u);
        this->corruption_pending = (u != nullptr);
    }

    SinkWrapper(const SinkWrapper& other) {
        this->p = other.p;
        this->corruption_pending = (other.p != nullptr);  // corrupt
        other.corruption_pending = false;  // we take over corrupting
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper(const NeedWrapper<U*>& need) {
        this->p = static_cast<T*>(need.p);
        this->corruption_pending = (need.p != nullptr);  // corrupt
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper(const SinkWrapper<U*>& other) {
        this->p = reinterpret_cast<MT*>(other.p);
        this->corruption_pending = (other.p != nullptr);  // corrupt
        other.corruption_pending = false;  // we take over corrupting
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper(const InitWrapper<U*>& init) {
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
    SinkWrapper& operator=(U* ptr) {
        this->p = u_cast(T*, ptr);
        this->corruption_pending = (ptr != nullptr);  // corrupt
        return *this;
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper& operator=(const NeedWrapper<U*>& need) {
        this->p = u_cast(T*, need.p);
        this->corruption_pending = (need.p != nullptr);  // corrupt
        return *this;
    }

    template<typename U, IfSinkConvertible<U>* = nullptr>
    SinkWrapper& operator=(const InitWrapper<U*>& init) {
        this->p = u_cast(T*, init.p);
        this->corruption_pending = (init.p != nullptr);  // corrupt
        return *this;
    }

    operator bool() const { return p != nullptr; }

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


//=//// HOOK TO CAST SINK(T) AVOIDING TEMPORARIES /////////////////////////=//
//
// The cast() macros allow for instrumentation of arbitrary casts, and for
// simplicity most of them are based on value semantics instead of reference
// semantics.  Using reference semantics introduces issues with function
// pointers and other types that have difficulty being taken by reference.
//
// However, value semantics means introducing temporaries...and SinkWrapper
// is a type that has a "unusual" meaning when taken by value, that makes
// a new corruption intent on each call.  e.g.:
//
//     void Foo_Maker_One(Sink(Foo) out) { ... }
//     void Foo_Maker_Two(Sink(Foo) out) { ... }
//
//     void Foo_Maker_Three(Sink(Foo) out) {  // out enters corrupt
//         Foo_Maker_One(out);  // initializes out so no longer corrupt
//         Do_Something_With_Foo(out);  // legal to use out now
//         Foo_Maker_Two(out);  // needs out to enter as corrupt again
//     }
//
// So it is intrinsic to the mechanism that by-value passing of Sinks will
// start a new corruption.  But if a temporary SinkWrapper is ever created
// it can create an unwanted corruption out of thin air.
//
// Hence we hook the cast() macros to use a reference to SinkWrapper instead
// of falling back on the default CastHook which uses value semantics
// for simplicity.
//
// 1. If you happen to cast a SinkWrapper<T*> to a T*, and there are validating
//    hooks in the CastHook<> specialization for that type, then it better
//    not be corrupt!  So if you think it might be corrupt, then you need to
//    cast to another Sink() or Init() and not the raw type.
//
#if 1
    template<typename V, typename T>
    struct CastHook<SinkWrapper<V*>,T*> {  // don't use SinkWrapper<V>& [D]
      static T* convert(const SinkWrapper<V*>& sink) {  // must be ref here
        if (sink.corruption_pending) {
            Corrupt_If_Needful(*sink.p);  // flush corruption
            sink.corruption_pending = false;
        }
        return h_cast(T*, sink.p);  // run validating cast if applicable [1]
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
    using wrapped_type = TP;
    using T = typename std::remove_pointer<TP>::type;

    T* p;

    template<typename U>
    using IfInitConvertible
        = typename IfOutputConvertible2<U, T>::enable::type;

    InitWrapper() {  // compiler might need [E]
        dont(Corrupt_If_Needful(p));  // lightweight behavior vs. Sink()
    }

    InitWrapper(std::nullptr_t) : p {nullptr}
        {}

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper(U* u) : p {reinterpret_cast<T*>(u)}
        {}

    InitWrapper(const InitWrapper& other) : p {other.p}
        {}

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper(const InitWrapper<U*>& init)
        : p {reinterpret_cast<T*>(init.p)}
        {}

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper(const NeedWrapper<U*>& need)
        : p {static_cast<T*>(need.p)}
        {}

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper(const SinkWrapper<U*>& sink) {
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
    InitWrapper& operator=(U* ptr) {
        this->p = u_cast(T*, ptr);
        return *this;
    }

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper& operator=(const NeedWrapper<U*>& need) {
        this->p = static_cast<T*>(need.p);
        return *this;
    }

    template<typename U, IfInitConvertible<U>* = nullptr>
    InitWrapper& operator=(const SinkWrapper<U*>& sink) {
        this->p = static_cast<T*>(sink.p);
        sink.corruption_pending = false;  // squash corruption
        return *this;
    }

    operator bool() const { return p != nullptr; }

    operator T*() const { return p; }

    template<typename U>
    explicit operator U*() const
        { return const_cast<U*>(reinterpret_cast<const U*>(p)); }

    T* operator->() const { return p; }
};


//=//// INIT() CAST HELPER TO RUN VALIDATING CASTS ON TYPE ////////////////=//
//
// When you cast a variable that is InitWrapper<T> to a T*, that should run
// whatever the CastHook<> specializations for T* are.
//
#if 1
    template<typename V, typename T>
    struct CastHook<InitWrapper<V*>,T*> {  // don't use InitWrapper<V>& [D]
      static constexpr T* convert(const InitWrapper<V*>& init) {  // ref faster
        return h_cast(T*, init.p);
      }
    };
#endif


//=//// NEED() FOR CONTRAVARIANT INPUT PARAMETERS /////////////////////////=//
//
// Need() enforces the contravariance of types (e.g. you can't pass a derived
// class into a pointer that takes base classes), but unlike Sink() or Init()
// it doesn't imply corruption, so it's an input parameter.
//
// 1. It was decided that while Sink(T) and Init(T) implicitly add pointers
//    to the type, you have to say Need(T*) if it's a pointer.  This is kind
//    of an aesthetic choice.  However, the template -must- be parameterized
//    with the type it is a stand-in for, so it is `SinkWrapper<T*>`,
//    `InitWrapper<T*>`, and `NeedWrapper<T*>`.
//
//    (See needful_rewrap_type() for the reasoning behind this constraint.)
//
// 2. Uses in the codebase the Needful library were written for required that
//    Need(T*) be able to accept cells with pending corruptions.  I guess
//    the thing I would say that if you want to argue with this design point,
//    you should consider that there's nothing guaranteeing a plain `T*` is
//    not corrupt...so you're not bulletproofing much and breaking some uses
//    that turned out to be important.  It's better to have cross-cutting
//    ways at runtime of noticing a given T* is corrupt regardless of Need().
//

#undef NeedfulNeed
#define NeedfulNeed(TP) \
    NeedWrapper<TP>  // * not implicit [1]

template<typename TP>
struct NeedWrapper {
    using wrapped_type = TP;

    using T = typename std::remove_pointer<TP>::type;
    T* p;

    using MT = typename std::remove_const<T>::type;  // mutable type

    template<typename U>
    using IfReverseInheritable
        = typename IfReverseInheritable2<U, T>::enable::type;

    NeedWrapper()  // compiler MIGHT need [E]
        { dont(Corrupt_If_Needful(p)); }  // may be zero in global scope

    NeedWrapper(std::nullptr_t) : p {nullptr}
        {}

    template<typename U,
            typename = typename std::enable_if<
                std::is_same<typename std::remove_const<T>::type, U>::value
                and std::is_const<T>::value
            >::type>
    NeedWrapper(const NeedWrapper<U*>& other)
        : p {static_cast<T*>(other.p)}
        {}

    template<
        typename U,
        typename = typename std::enable_if<  // don't disregard constness
            std::is_convertible<U*, T*>::value
        >::type,
        IfReverseInheritable<U>* = nullptr
    >
    NeedWrapper(U* u) : p {(MT*)(u)}
        {}

    NeedWrapper(const NeedWrapper& other) : p {other.p}
        {}

    template<typename U, IfReverseInheritable<U>* = nullptr>
    NeedWrapper(const SinkWrapper<U*>& sink) {
        dont(assert(not sink.corruption_pending));  // must allow corrupt [2]
        this->p = static_cast<T*>(sink);  // not sink.p (flush corruption)
    }

    template<typename U, IfReverseInheritable<U>* = nullptr>
    NeedWrapper(const InitWrapper<U*>& init)
        : p {static_cast<T*>(init.p)}
        {}

    NeedWrapper& operator=(std::nullptr_t) {
        this->p = nullptr;
        return *this;
    }

    NeedWrapper& operator=(const NeedWrapper& other) {
        if (this != &other) {
            this->p = other.p;
        }
        return *this;
    }

    template<typename U, IfReverseInheritable<U>* = nullptr>
    NeedWrapper& operator=(U* ptr) {
        this->p = static_cast<T*>(ptr);
        return *this;
    }

    template<typename U, IfReverseInheritable<U>* = nullptr>
    NeedWrapper& operator=(const SinkWrapper<U*>& sink) {
        dont(assert(not sink.corruption_pending));  // must allow corrupt [2]
        this->p = static_cast<T*>(sink);  // not sink.p (flush corruption)
        return *this;
    }

    template<typename U, IfReverseInheritable<U>* = nullptr>
    NeedWrapper& operator=(const InitWrapper<U*>& init) {
        this->p = static_cast<T*>(init.p);
        return *this;
    }

    operator bool() const { return p != nullptr; }

    operator T*() const { return p; }

    template<typename U>
    explicit operator U*() const
        { return const_cast<U*>(reinterpret_cast<const U*>(p)); }

    T* operator->() const { return p; }
};


//=//// NEED() CAST HELPER TO RUN VALIDATING CASTS ON TYPE ////////////////=//
//
// When you cast a variable that is NeedWrapper<T> to a T*, that should run
// whatever the CastHook<> specializations for T* are.
//
#if 1
    template<typename V, typename T>
    struct CastHook<NeedWrapper<V*>,T*> {  // don't use NeedWrapper<V>& [D]
      static constexpr T* convert(const NeedWrapper<V*>& need) {  // ref faster
        return h_cast(T*, need.p);
      }
    };
#endif
