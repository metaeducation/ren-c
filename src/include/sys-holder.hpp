//
//  File: %sys-holder.hpp
//  Summary: {C++-only wrapper class for raw series pointers}
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// There are low-level variants of raw series, like StringT.  There is no
// way of knowing how many of these might be referred to in the active call
// stacks.  So when a garbage collect happens, you can only hope that any
// outstanding raw pointers either have been marked with a GC_GUARD, or are
// already guarded by their use in some reachable cell.
//
// The goal of the SeriesHolder<> is to use C++ magic to be able to track how
// many references are outstanding in local stacks, while not causing an
// undue burden on the C code.  The instantations look like:
//
//     String(*) str = Make_String(100);
//
// In the C++ build that becomes a "smart pointer":
//
//     SeriesHolder<StringT*> str = Make_String(100);
//
// While the C build produces:
//
//     StringT* str = Make_String(100);
//
// At time of writing, this is a debug tool.  It is used to do reference
// counting from stack variables, where each String(*) in use adds a count to
// a value stored in the node.  When GC_Kill_Series() happens, it asesrts that
// the counter has reached 0, hence there are no outstanding references.
//
// In the future, this might be used to give some added capabilities when
// building as C++.  Like being able to safely run the garbage collector
// at arbitrary moments (instead of just between evaluations)--this might make
// it possible to reclaim memory at the moment an out of memory occurs.
//


// 1. The C build can't auto-initialize variables.  But if we don't let the
//    DEBUG_COUNT_LOCALS build do so, then when it comes to the point of
//    acquiring a new value, SeriesHolder won't know if it's holding garbage
//    that needs to be ignored, or a pointer it no longer needs to refer to.
//    We're forced to set to nullptr (C code can't depend on this!)
//
// 2. This looks ugly, but it's simple as template metaprogramming goes.  It's
//    just saying we are willing to make a SeriesHolder<T*> for any type that
//    would have converted implicitly to a T* without a holder.  (e.g. it's ok
//    to assign a StringT* from a SymbolT*, as all symbols are strings.)
//
//    In order to make the SeriesHolder follow similar rules, we use the
//    `is_convertible` type-trait as the test for if we should fabricate a
//    constructor for that type at compile-time.  That's all!
//
// 3. You may notice that only the constructor does an increment of the count,
//    and only the destructor does a decrement of the count.  There's not a
//    lot of mess when you make a copy pertaining to releasing a reference to
//    the old thing and adding a reference to the new thing, spread between
//    move construction, the default copy constructor, and the template-made
//    copy constructors for convertible types.
//
//    It would be fairly logical for someone to factor out a function to do
//    the common work rather than rewrite the addref/release in all those
//    cases.  But they might have trouble designing that function...picking
//    the parameters to that function or coming up with a name for it.
//
//    Happily SeriesHolder doesn't need to do that; the right function to
//    write is called `swap()` and have it interplay with the copying that
//    function arguments can already do by parameter convention.  If you
//    stylize things this way, it's called the Copy-Swap-Idiom:
//
//      https://stackoverflow.com/q/3279543/
//
//    And as a bonus, swap() is a useful method in its own right, in most C++.
//
// 4. We don't want to generate overloaded comparison operators that compete
//    with the built-in comparison operators.  So remove those from the set
//    of considerations of compatible equality comparisons.
//

template<typename TP>
struct SeriesHolder {
    typedef typename std::remove_pointer<TP>::type T;
    typedef typename std::add_const<T>::type CT;
    typedef typename std::add_pointer<CT>::type CTP;
    typedef typename std::remove_const<T>::type MT;
    typedef typename std::add_pointer<MT>::type MTP;

    typedef MTP MutablePointerType;
    typedef TP PointerType;

    TP p;
    bool trashed;

    static_assert(
        std::is_base_of<SeriesT, typename std::remove_const<T>::type>::value,
        "SeriesHolder<SeriesT*> only works on SeriesT derived types"
    );

    SeriesHolder () : p (nullptr), trashed (false) { }  // must init, see [1]

    explicit SeriesHolder (const void* other, bool other_trashed = false) {
        p = reinterpret_cast<T*>(const_cast<void*>(other));
        trashed = other_trashed;
        if (not trashed and p) {
            auto* s = x_cast(SeriesT*, p);
            assert(s->num_locals < INT32_MAX);
            s->num_locals += 1;
        }
    }

    explicit SeriesHolder (uintptr_t other) :  // allow casting
        SeriesHolder (reinterpret_cast<void*>(other))
        {}

    SeriesHolder (SeriesHolder& other)  // copy constructor
        : SeriesHolder (other.p, other.trashed)  // construct w/other's pointer
        {}

    template<
        typename U,
        typename std::enable_if<
            std::is_convertible<U, T*>::value  // easier than it looks, see [2]
        >::type* = nullptr
    >
    SeriesHolder (const U& u)  // construct via any convertible type, see [2]
        : SeriesHolder (static_cast<const void*>(u))  // construct w/pointer
        {}

    friend void swap(SeriesHolder& first, SeriesHolder& second)
    {
        using std::swap;
        swap(first.trashed, second.trashed);
        swap(first.p, second.p);
    }

    SeriesHolder& operator= (SeriesHolder other) {
        swap(*this, other);  // copy-and-swap idiom, see [3]
        return *this;
    }

    SeriesHolder (SeriesHolder&& other) : SeriesHolder ()
     { swap(*this, other); }  // copy-and-swap idiom, see [3]

    template<
        typename U,
        typename std::enable_if<
            std::is_convertible<U, T*>::value
        >::type* = nullptr
    >
    SeriesHolder& operator= (U u) {
        SeriesHolder other = u;
        swap(*this, other);  // copy-and-swap idiom, see [3]
        return *this;
    }

    ~SeriesHolder () {
        if (not trashed and p) {
            auto* s = x_cast(SeriesT*, p);
            assert(s->num_locals > 0);
            s->num_locals = s->num_locals - 1;
        }
    }

    T* operator->() const { return p; }
    T& operator*() const { return *p; }

    template<
        typename U,
        typename std::enable_if<
            std::is_convertible<U, T*>::value
            && !std::is_same<U, SeriesHolder<T*>>::value  // builtin, see [4]
        >::type* = nullptr
    >
    bool operator== (U right) const
        { return not trashed and p == right; }

    template<
        typename U,
        typename std::enable_if<
            std::is_convertible<U, T*>::value
            && !std::is_same<U, SeriesHolder<T*>>::value  // builtin, see [4]
        >::type* = nullptr
    >
    bool operator!= (U right) const {
        assert(not trashed);
        return p != right;
    }

    explicit operator bool () const {  // "explicit" has exceptions...
        return trashed or p != nullptr;  // https://stackoverflow.com/q/39995573/
    }

    operator T* () const { return p; }

    template<
        typename U,
        typename std::enable_if<
            std::is_convertible<U*, T*>::value
        >::type* = nullptr
    >
    explicit operator U* () const { return static_cast<U*>(p); }

    explicit operator SeriesHolder<MTP> () const {
        return const_cast<MTP>(p);
    }
};


// Global equality overloads needed when left hand side is raw pointer (the
// member functions can only control equality behavior when the SeriesHolder
// is on the left side.)

template<typename U, typename T>
bool operator== (U left, SeriesHolder<T*>& right)
    { return left == right.p; }

template<typename U, typename T>
bool operator!= (U left, SeriesHolder<T*>& right)
    { return left != right.p; }


// const_cast<> and reinterpret_cast<> don't work with user-defined
// conversion operators.  But since this codebase uses m_cast, we can
// cheat when the class is being used with the helpers.
//
template <
    typename HTP,
    typename MTP = typename HTP::MutablePointerType,
    typename CT = typename std::add_const<MTP>::type,
    typename CTP = typename std::add_pointer<CT>::type,
    typename HCTP = typename SeriesHolder<CTP>::PointerType
>
inline HTP m_cast_helper(SeriesHolder<CTP> const& s)
  { return const_cast<MTP>(s.p); }


/*template <typename TP>
inline TP m_cast_helper(SeriesHolder<TP>& s)
    { return s.p; }  // m_cast() allows being a no-op*/


#if !defined(NDEBUG)
    template <typename TP>
    inline static void Trash_Pointer_If_Debug(SeriesHolder<TP>& s) {
        s.trashed = true;
        s.p = reinterpret_cast<TP>(static_cast<uintptr_t>(0xDECAFBAD));
    }

    template <typename TP>
    inline static bool Is_Pointer_Trash_Debug(SeriesHolder<TP> const& s) {
        return s.trashed;
    }
#endif
