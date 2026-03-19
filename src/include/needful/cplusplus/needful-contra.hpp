//
//  file: %needful-contra.hpp
//  summary: "Contravariant type checking and corruption of output parameters"
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
// Needful's concept of contravariance is based on a very stylized usage of
// inheritance, in which classes in a derivation hierarchy are all using the
// same underlying bit patterns.  The only reason they're using inheritance
// is to get compile-time checking of constraints on those bits, where the
// subclasses represent more constrained bit patterns than their bases.
//
// SinkWrapper and InitWrapper accept pointers to BASE classes (writing less-
// constrained bits into a more constrained location is the error to catch).
//
// The core question is: "Can U be safely used where T* is expected?" This is
// answered by IsContravariant<U, T>, which integrates all safety checks:
//
// - Inheritance/same-layout relationship (contravariance itself)
//
// - Indirect encodings (Slot has getter/setter -> can't write blindly)
//
//   * Init/Sink wrappers suppress this (they're output targets)
//   * Identity conversions ignore this (Slot* -> Slot is always safe)
//
// Wrappers with special semantics (nullable, etc.) can specialize
// IsContravariant directly to opt out of contravariance entirely.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. The copy-and-swap idiom doesn't seem to be very helpful here, as we
//    aren't dealing with exceptions and self-assignment has to be handled
//    manually due to the handoff of the corruption_pending flag.
//
//      https://stackoverflow.com/questions/3279543/
//
// B. In the initial design, default constructing things like SinkWrapper<>
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
//       { dont(Corrupt_If_Needful(p)); }  // may be zero in global scope
//
// C. C++ doesn't really let you template cast operators, so if we're going
//    to force contravariant conversions for wrapper types the "loophole"
//    you can use is to do the contravariance testing via construction and
//    then ask the type to cast to void*, and then cast to the type.  It's
//    a workaround that seems to work for wrapper types.
//


//=//// BASIC CONTRAVARIANT LAYOUT TRAIT (no safety checks) ///////////////=//
//
// Check if U can be converted to T* from an inheritance/layout perspective.
//
// The stylized contravariance needs Plain-Old-Data (POD) C structs, that are
// standard-layout where no fields are added in derivation.  This is the only
// way that the "dangerous"-looking casts performed by Sink() and Init() are
// safe.  So we check for standard-layout and size-equality on base and
// derived classes before allowing them to be used this way.
//
// Does NOT check safety concerns (indirect encodings, storage availability).
//
// Delegates to IsSameLayoutBase (in needful-utilities.hpp) for the core
// inheritance + layout validation.  This keeps the invariant that makes
// multi-level pointer casts safe (IsDeepPointerConvertible) and the
// invariant that makes contravariant Sink/Init safe grounded on the same
// single trait.
//

template<typename UP, typename T, bool = HasWrappedType<UP>::value>
struct IsContravariantLayout {
    using U = remove_pointer_t<UP>;

    static constexpr bool is_identity = std::is_same<UP, T*>::value;

    // IsSameLayoutBase<U, T> checks is_base_of<U, T>, enforces
    // standard_layout and sizeof equality via its own static_assert.
    //
    static constexpr bool is_valid =
        std::is_pointer<UP>::value
        and IsSameLayoutBase<U, T>::value;

    static constexpr bool value = is_identity or is_valid;
};

template<typename U, typename T>
struct IsContravariantLayout<U, T, true> {  // unwrap through wrappers
    static constexpr bool value =
        IsContravariantLayout<typename U::wrapped_type, T>::value;
};


//=//// INDIRECT ENCODING SAFETY //////////////////////////////////////////=//
//
// An "indirect encoding" is when a place where a value might be stored
// actually holds something like a getter/setter function pointer instead of
// the value itself.  It may be that some bit patterns indicate such encodings
// and shouldn't be blindly overwritten.
//
// For safety, these types aren't considered contravariant for the purposes
// of initialization.  However, when they are wrapped in Init(T) or Sink(T)
// wrappers, the wrapper overrides this.  These are considered to be "fresh"
// output targets that are safe to overwrite completely, and can't contain
// any indirections.
//
// The MayUseIndirectEncoding trait is defined as:
//
//   - Init/Sink wrappers: false (output targets guarantee safety)
//   - Other wrappers: delegate to inner type
//   - Unwrapped types: false (specialize to true for types like Slot)
//

template<typename T, bool = HasWrappedType<T>::value>
struct MayUseIndirectEncoding : std::false_type {};  // unwrapped types

template<typename T>
struct MayUseIndirectEncoding<T, true>  // wrapper default: delegate to wrapped
    : MayUseIndirectEncoding<typename T::wrapped_type> {};


//=//// CONTRAVARIANT TRAIT (WITH INTEGRATED SAFETY) /////////////////////=//
//
// IsContravariant<U, T> answers the single question: "Can U be safely used
// where T* is expected?"  This integrates multiple safety concerns:
//
// 1. INHERITANCE: Does U point to a derived class of T? (or same class)
//
// 2. INDIRECT ENCODINGS: Does the target type use indirect encodings that
//    make blind writes dangerous? (Slot with getters/setters)
//    - Init/Sink wrappers suppress this check (they're output targets)
//    - Plain pointers must check the pointee type
//
// 3. SPECIAL WRAPPER SEMANTICS: Some wrappers can't participate in
//    contravariance at all (e.g., Option might be disengaged -> no storage).
//    These should specialize IsContravariant directly to return false.
//

template<
    typename UP,
    typename T,
    bool IsWrapper = HasWrappedType<UP>::value
>
struct IsContravariant {
    using U = remove_pointer_t<UP>;

    static constexpr bool value =
        std::is_pointer<UP>::value
        and IsContravariantLayout<UP, T>::value
        and (std::is_same<U, T>::value  // identity: always safe
            or not MayUseIndirectEncoding<U>::value);
};

template<typename U, typename T>
struct IsContravariant<U, T, /* IsWrapper = */ true> {
    static constexpr bool value =
        not MayUseIndirectEncoding<U>::value  // wrapper may override wrapped
        and IsContravariantLayout<typename U::wrapped_type, T>::value;
};

template<typename U, typename T>  // SFINAE helper
using IfContravariant = typename std::enable_if<
    IsContravariant<U, T>::value
>::type;


//=//// CONTRA() FOR LIGHTWEIGHT CONTRAVARIANT PASS-THROUGH ///////////////=//
//
// ContraWrapper is the base contravariant wrapper--it checks contravariant
// type compatibility and stores a pointer.
//
// 1. If a plain ContraWrapper receives a SinkWrapper source, it ensures that
//    corruption is not pending.  This is distinct from what InitWrapper does,
//    which is to suppress the corruption (presuming that the initialization
//    code will overwrite the target).

#undef NeedfulContra
#define NeedfulContra(T)  needful::ContraWrapper<T*>

template<typename TP>
struct ContraWrapper {
    using T = remove_pointer_t<TP>;  // T* is clearer than "TP" in this class

    NEEDFUL_DECLARE_WRAPPED_FIELD (T*, p);

    ContraWrapper()  // compiler might need, see [B]
        {}

    ContraWrapper(std::nullptr_t) : p {nullptr}
        {}

    ContraWrapper(const ContraWrapper& other) : p {other.p}
        {}

    template<typename U, IfContravariant<U, T>* = nullptr>
    ContraWrapper(const U& u) {
        this->p = x_cast(T*, x_cast(void*, u));  // cast workaround [C]
    }

    template<typename U, IfContravariant<SinkWrapper<U>, T>* = nullptr>
    ContraWrapper(const SinkWrapper<U>& sink) {
        assert(not sink.corruption_pending);  // catch corruption transfer [1]
        this->p = static_cast<T*>(sink.p);
    }

    ContraWrapper& operator=(std::nullptr_t) {
        this->p = nullptr;
        return *this;
    }

    ContraWrapper& operator=(const ContraWrapper& other) {
        if (this != &other) {
            this->p = other.p;
        }
        return *this;
    }

    template<typename U, IfContravariant<U, T>* = nullptr>
    ContraWrapper& operator=(const U& u) {
        this->p = x_cast(T*, x_cast(void*, u));  // [C]
        return *this;
    }

    template<typename U, IfContravariant<SinkWrapper<U>, T>* = nullptr>
    ContraWrapper& operator=(const SinkWrapper<U>& sink) {
        assert(not sink.corruption_pending);  // catch corruption transfer [1]
        this->p = static_cast<T*>(sink.p);
        return *this;
    }

    explicit operator bool() const { return p != nullptr; }

    operator T*() const { return p; }

    template<typename U>
    explicit operator U*() const
        { return const_cast<U*>(reinterpret_cast<const U*>(p)); }

    T* operator->() const { return p; }
};


//=//// SINK() WRAPPER FOR OUTPUT PARAMETERS //////////////////////////////=//
//
// 1. SinkWrapper is marked as MayUseIndirectEncoding=false (it's an output
//    target), so Sink(Slot) can be passed where Sink(Element) is expected,
//    though Slot uses indirect encodings; the wrapper guarantees safe writes.
//
// 2. It might seem natural to use a base class to share functions between
//    the wrappers.  But in SinkWrapper, corruption tracking pervades every
//    method...so it doesn't inherit.  (ContraWrapper and InitWrapper DO use
//    inheritance: they differ only in how they handle SinkWrapper sources.)
//
// 3. The original implementation was simpler, by just doing the corruption
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
// 4. Retargeting the pointer of a Sink(T) could be prohibited, but that would
//    make it less flexible than a pointer.  Reassignment can be useful, e.g.
//    if you get an Option(Sink(T)) as nullptr as an argument, you might want
//    to default it for internal purposes...even though the caller isn't
//    expecting a value back.
//
//    So rather than disallow overwriting the pointer in a Sink, it just
//    triggers another corruption if you assign a non-nullptr.
//

#undef NeedfulSink
#define NeedfulSink(T)  needful::SinkWrapper<T*>

template<typename T>  // write ok even if contained type can be indirect [1]
struct MayUseIndirectEncoding<SinkWrapper<T>, true> : std::false_type {};

template<typename TP>  // doesn't inherit from ContraWrapper [2]
struct SinkWrapper {
    using T = remove_pointer_t<TP>;  // T* is clearer than "TP" in this class

    static_assert(
        not std::is_const<T>::value,
        "Sink(T) cannot sink const pointee (Sink(const T*) is okay)"
    );

    NEEDFUL_DECLARE_WRAPPED_FIELD (T*, p);

    mutable bool corruption_pending;  // can't corrupt on construct [3]

    SinkWrapper()  // compiler MIGHT need, see [B]
        : corruption_pending {false}
    {
        Corrupt_If_Needful(p);  // pointer itself in this case, not contents!
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

    template<typename U, IfContravariant<U, T>* = nullptr>
    SinkWrapper(const U& u) {
        this->p = x_cast(T*, x_cast(void*, u));  // cast workaround [C]
        this->corruption_pending = (this->p != nullptr);
    }

    template<typename U, IfContravariant<SinkWrapper<U>, T>* = nullptr>
    SinkWrapper(const SinkWrapper<U>& other) {
        this->p = static_cast<T*>(other.p);  // safe: IfContravariant guarantees
        this->corruption_pending = (other.p != nullptr);  // corrupt
        other.corruption_pending = false;  // we take over corrupting
    }

    SinkWrapper(const SinkWrapper& other) {  // same-type copy constructor
        this->p = other.p;
        this->corruption_pending = (other.p != nullptr);  // corrupt
        other.corruption_pending = false;  // we take over corrupting
    }

    SinkWrapper& operator=(std::nullptr_t) {  // `=` allowed [4]
        this->p = nullptr;
        this->corruption_pending = false;
        return *this;
    }

    SinkWrapper& operator=(const SinkWrapper& other) {  // `=` allowed [4]
        if (this != &other) {
            this->p = other.p;
            this->corruption_pending = (other.p != nullptr);  // corrupt
            other.corruption_pending = false;  // we take over corrupting
        }
        return *this;
    }

    template<typename U, IfContravariant<U, T>* = nullptr>
    SinkWrapper& operator=(const U& u) {  // `=` allowed [4]
        this->p = x_cast(T*, x_cast(void*, u));  // [C]
        this->corruption_pending = (this->p != nullptr);  // corrupt
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
// any branches in it.
//
// InitWrapper derives from ContraWrapper because they are nearly identical--
// the only difference is how they handle a SinkWrapper source:
//
//   ContraWrapper: preserves pending corruption (just passing through)
//   InitWrapper: squashes pending corruption (consuming it freshly)
//
// BUT if you want to double check the initializations, it should still work
// to make Init() equivalent to Sink() and corrupt the cell.  It's not likely
// to catch many bugs...but it could, so doing it occasionally might be a
// good idea.  Just do:
//
//     #define DEBUG_CHECK_INIT_SINKS  1  // Init() => actually Sink()
//
// 1. InitWrapper is marked as MayUseIndirectEncoding=false (it's an output
//    target), so Init(Slot) can be passed where Init(Element) is expected,
//    though Slot uses indirect encodings; the wrapper guarantees safe writes.
//

#if !defined(DEBUG_CHECK_INIT_SINKS)
    #define DEBUG_CHECK_INIT_SINKS  0
#endif

#if DEBUG_CHECK_INIT_SINKS
    #undef NeedfulInit
    #define NeedfulInit(T)  needful::SinkWrapper<T*>  // see notes above
#else
    #undef NeedfulInit
    #define NeedfulInit(T)  needful::InitWrapper<T*>
#endif

template<typename T>  // write ok even if contained type can be indirect [1]
struct MayUseIndirectEncoding<InitWrapper<T>, true> : std::false_type {};

template<typename TP>
struct InitWrapper : ContraWrapper<TP> {
    using Base = ContraWrapper<TP>;
    using T = typename Base::T;

    static_assert(
        not std::is_const<T>::value,
        "Init(T) cannot init const pointee (Init(const T*) is okay)"
    );

    using Base::Base;  // inherit constructors from ContraWrapper

    InitWrapper() : Base()  // compiler might need, see [B]
        {}  // not inherited by `using` in C++11

    InitWrapper(const InitWrapper& other) = default;

    template<typename U, IfContravariant<SinkWrapper<U>, T>* = nullptr>
    InitWrapper(const SinkWrapper<U>& sink) {
        this->p = static_cast<T*>(sink.p);  // `sink.p` avoids `sink` corrupt
        sink.corruption_pending = false;  // squash corruption (see above)
    }

    using Base::operator=;

    InitWrapper& operator=(const InitWrapper& other) = default;

    template<typename U, IfContravariant<SinkWrapper<U>, T>* = nullptr>
    InitWrapper& operator=(const SinkWrapper<U>& sink) {
        this->p = static_cast<T*>(sink.p);  // `sink.p` avoids `sink` corrupt
        sink.corruption_pending = false;  // squash corruption (see above)
        return *this;
    }
};
