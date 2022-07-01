//
//  File: %sys-casts.h
//  Summary: {Instrumented operators for downcasting series subclasses}
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// This file contains some of the "scariest" magic for the checking that the
// C++ debug build provides.  But it's also some of the most vital.
//
// It is often the case that a stored pointer for a series or node is the
// base class, e.g. a REBSER* when it is actually the varlist of a Context(*).
// The process for casting something from a base class to a subclass is
// referred to as "downcasting":
//
// https://en.wikipedia.org/wiki/Downcasting
//
// Downcasting has the potential to be unsafe when the base class is not
// actually an instance of the subclass.  The C build has no way to involve
// any kind of active checking in this process during compile-time...a cast
// will blindly do whatever you ask it to.
//
// In the C++ build we can do better:
//
// * Templates can stop illegal downcasting (e.g. keep you from trying to turn
//   an `int*` into a `Array(*)`, but allow you to do it for `REBSER*`).
//
// * They can also stop unnecessary downcasting...such as casting a REBSER*
//   to a REBSER*.
//
// * Bit patterns can be checked in the node to make sure that the cast is
//   actually legal at runtime.  While this can be done in C too, the nature
//   of handling of things like `const` would mean making differently-named
//   functions for const and non-const cast variants...which is uglier.
//
// Though some of what DEBUG_CHECK_CASTS does is handled at compile-time,
// its runtime component means code will be slower.  Though unfortunate, this
// is still one of the best first lines of defense for catching problems.
//


#if (! DEBUG_CHECK_CASTS)

    // Plain definitions are very permissive...they cast away any constness
    // of the input pointer, and always return a mutable output.  This is
    // because to do otherwise in the C build would require having variants
    // like `const_SER()` and `SER()`, which would be ugly.
    //
    // So we just trust the occasional build with DEBUG_CHECK_CASTS will use
    // C++ templating magic to validate the constness, and keep the C source
    // form more readable at the callsites.

    #define NOD(p)          m_cast(REBNOD*, x_cast(const REBNOD*, (p)))

    #define SER(p)          m_cast(REBSER*, x_cast(const REBSER*, (p)))
    #define ARR(p)          m_cast(Array(*), x_cast(Array(const*), (p)))
    #define ACT(p)          m_cast(Action(*), x_cast(const Action(*), (p)))
    #define CTX(p)          m_cast(Context(*), x_cast(const Context(*), (p)))

    #define STR(p)          m_cast(String(*), x_cast(String(const*), (p)))
    #define SYM(p)          m_cast(Symbol(*), x_cast(Symbol(const*), (p)))

    #define VAL(p)          x_cast(REBVAL*, (p))

    #define FRM(p)          x_cast(Frame(*), (p))

#else

  #if (! CPLUSPLUS_11)
    #error "DEBUG_CHECK_CASTS requires C++11 (or later)"
  #endif

    // The C++ variants are more heavyweight, and beyond the scope of writing
    // a long explanation here.  Suffice to say that these are templates
    // which enforce that a const base-class pointer input will result in
    // a const-derived-class pointer output.  The function will make sure
    // the bit patterns are a match for the cast.

    inline static REBNOD *NOD(nullptr_t p) {
        UNUSED(p);
        return nullptr;
    }

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename N = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const REBNOD,  // true branch
            REBNOD  // false branch
        >::type
    >
    inline static N *NOD(T *p) {
        constexpr bool base =
            std::is_same<T0, void>::value
            or std::is_same<T0, Byte>::value;

        static_assert(
            base,
            "NOD() works on void* or Byte*"
        );

        if (not p)
            return nullptr;

        if ((*reinterpret_cast<const Byte*>(p) & (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x40_STALE
        )) != (
            NODE_BYTEMASK_0x80_NODE
        )){
            panic (p);
        }

        return reinterpret_cast<N*>(p);
    }

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename S = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const REBSER,  // true branch
            REBSER  // false branch
        >::type
    >
    inline S *SER(T *p) {
        static_assert(
            std::is_same<T0, void>::value
                or std::is_same<T0, REBNOD>::value,
            "SER() works on [void* REBNOD*]"
        );
        if (not p)
            return nullptr;

        if ((reinterpret_cast<const REBSER*>(p)->leader.bits & (
            NODE_FLAG_NODE | SERIES_FLAG_FREE | NODE_FLAG_CELL
        )) != (
            NODE_FLAG_NODE
        )){
            panic (p);
        }

        return reinterpret_cast<S*>(p);
    }

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename A = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const Reb_Array,  // true branch
            Reb_Array  // false branch
        >::type
    >
    inline A *ARR(T *p) {
        static_assert(
            std::is_same<T0, void>::value
                or std::is_same<T0, REBNOD>::value
                or std::is_same<T0, REBSER>::value,
            "ARR() works on [void* REBNOD* REBSER*]"
        );
        if (not p)
            return nullptr;

        if ((reinterpret_cast<const REBSER*>(p)->leader.bits & (
            NODE_FLAG_NODE | SERIES_FLAG_FREE | NODE_FLAG_CELL
        )) != (
            NODE_FLAG_NODE
        )){
            panic (p);
        }

        return reinterpret_cast<A*>(p);
    }

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename C = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const Reb_Context,  // true branch
            Reb_Context  // false branch
        >::type
    >
    inline static C *CTX(T *p) {
        static_assert(
            std::is_same<T0, void>::value
                or std::is_same<T0, REBNOD>::value
                or std::is_same<T0, REBSER>::value
                or std::is_same<T0, Reb_Array>::value,
            "CTX() works on [void* REBNOD* REBSER* Array(*)]"
        );
        if (not p)
            return nullptr;

        if (((reinterpret_cast<const REBSER*>(p)->leader.bits & (
            SERIES_MASK_VARLIST
                | SERIES_FLAG_FREE
                | NODE_FLAG_CELL
                | FLAG_FLAVOR_BYTE(255)
        ))
            | SERIES_FLAG_DYNAMIC  // permit non-dynamic (e.g. inaccessible
        ) !=
            SERIES_MASK_VARLIST
        ){
            panic (p);
        }

        return reinterpret_cast<C*>(p);
    }

    template <typename P>
    inline Action(*) ACT(P p) {
        static_assert(
            std::is_same<P, void*>::value
                or std::is_same<P, REBNOD*>::value
                or std::is_same<P, REBSER*>::value
                or std::is_same<P, Array(*)>::value,
            "ACT() works on [void* REBNOD* REBSER* Array(*)]"
        );

        if (not p)
            return nullptr;

        if ((reinterpret_cast<const REBSER*>(p)->leader.bits & (
            SERIES_MASK_DETAILS
                | SERIES_FLAG_FREE
                | NODE_FLAG_CELL
                | FLAG_FLAVOR_BYTE(255)
        )) !=
            SERIES_MASK_DETAILS
        ){
            panic (p);
        }

        return reinterpret_cast<Action(*)>(p);
    }

    // !!! STR() and SYM() casts should be updated to do more than const

    inline static String(*) STR(void *p)
      { return cast(String(*), p); }

    inline static String(const*) STR(const void *p)
      { return cast(String(const*), p); }

    // The only time a SYM should be mutable is at its creation time, or
    // when bits are being tweaked in binding slots.  Stored or external
    // pointers should always be const if downcasting.

    inline static Symbol(*) SYM(void *p)
      { return cast(Symbol(*), p); }

    inline static Symbol(const*) SYM(const void *p)
      { return cast(Symbol(const*), p); }

    // !!! There aren't currently that many VAL() casts in the system.  When
    // some arise, consider beefing up this cast.

    inline static REBVAL* VAL(void *p)
      { return cast(REBVAL*, p); }

    inline static const REBVAL* VAL(const void *p)
      { return cast(const REBVAL*, p); }

    template <class T>
    inline Frame(*) FRM(T *p) {
        constexpr bool base = std::is_same<T, void>::value
            or std::is_same<T, REBNOD>::value
            or std::is_same<T, Reb_Frame>::value;

        static_assert(base, "FRM() works on void/REBNOD/Frame(*)");

        if (not p)
            return nullptr;

        if ((*reinterpret_cast<Byte*>(p) & (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x40_STALE
                | NODE_BYTEMASK_0x01_CELL
        )) != (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x01_CELL
        )){
            panic (p);
        }

        return reinterpret_cast<Frame(*)>(p);
    }
#endif


inline static REBMAP *MAP(void *p) {  // not a fancy cast ATM.
    Array(*) a = ARR(p);
    assert(IS_PAIRLIST(a));
    return cast(REBMAP*, a);
}
