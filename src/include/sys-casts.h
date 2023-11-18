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
// base class, e.g. a Series* when it is actually the varlist of a Context*.
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
//   an `int*` into a `Array*`, but allow you to do it for `Series*`).
//
// * They can also stop unnecessary downcasting...such as casting a Series*
//   to a Series*.
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

    // Plain definitions are very permissive...they x_cast() away any constness
    // of the input pointer, and always return a mutable output.  This is
    // because to do otherwise in the C build would require having variants
    // like `const_SER()` and `SER()`, which would be unmanageable.
    //
    // So we just trust the occasional build with DEBUG_CHECK_CASTS will use
    // C++ templating magic to validate the constness, and keep the C source
    // form more readable at the callsites.
    //
    // Note: x_cast's fast method vaporizing at compile time in C++ will not
    // work on smart pointer types, so don't try writing:
    //
    //    #define VAL(p)    x_cast(ValueT*, (p))
    //
    // It may work when none of the macros actually resolve to smart pointer
    // classes, but will break when they are.

    #define NOD(p)          x_cast(Node*, (p))

    #define SER(p)          x_cast(Series*, (p))
    #define ARR(p)          x_cast(Array*, (p))
    #define ACT(p)          x_cast(Action*, (p))
    #define CTX(p)          x_cast(Context*, (p))

    #define STR(p)          x_cast(String*, (p))

    #define SYM(p)          x_cast(Symbol*, (p))

    #define VAL(p)          x_cast(ValueT*, (p))

    #define LVL(p)          x_cast(LevelT*, (p))

    #define BIN(p)          x_cast(Binary*, (p))

    #define MAP(p)          x_cast(Map*, (p))
    #define KEYS(p)         x_cast(KeyList*, (p))
#else

  #if (! CPLUSPLUS_11)
    #error "DEBUG_CHECK_CASTS requires C++11 (or later)"
    #include <stophere>  // https://stackoverflow.com/a/45661130
  #endif

    // The C++ variants are more heavyweight, and beyond the scope of writing
    // a long explanation here.  Suffice to say that these are templates
    // which enforce that a const base-class pointer input will result in
    // a const-derived-class pointer output.  The function will make sure
    // the bit patterns are a match for the cast.

    inline static Node* NOD(nullptr_t p) {
        UNUSED(p);
        return nullptr;
    }

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename N = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const Node,  // true branch
            Node  // false branch
        >::type
    >
    inline static N *NOD(T *p) {
        static_assert(
            std::is_same<T0, void>::value
                or std::is_same<T0, Byte>::value,
            "NOD() works on void* or Byte*"
        );

        if (not p)
            return nullptr;

        if ((FIRST_BYTE(p) & (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x40_FREE
        )) != (
            NODE_BYTEMASK_0x80_NODE
        )){
            panic (p);
        }

        return cast(N*, p);
    }

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename S = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const Series,  // true branch
            Series  // false branch
        >::type
    >
    inline S *SER(T *p) {
        static_assert(
            std::is_same<T0, void>::value
                or std::is_same<T0, Node>::value,
            "SER() works on [void* Node*]"
        );
        if (not p)
            return nullptr;

        if ((cast(const Stub*, p)->header.bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
        )) != (
            NODE_FLAG_NODE
        )){
            panic (p);
        }

        return cast(S*, p);
    }

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename A = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const Array,  // true branch
            Array  // false branch
        >::type
    >
    inline A *ARR(T *p) {
        static_assert(
            std::is_same<T0, void>::value
                or std::is_same<T0, Node>::value
                or std::is_same<T0, Series>::value,
            "ARR() works on [void* Node* Series*]"
        );
        if (not p)
            return nullptr;

        if ((cast(const Stub*, p)->header.bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
        )) != (
            NODE_FLAG_NODE
        )){
            panic (p);
        }

        return cast(A*, p);
    }

    template <
        typename T,
        typename T0 = typename std::remove_const<T>::type,
        typename C = typename std::conditional<
            std::is_const<T>::value,  // boolean
            const Context,  // true branch
            Context  // false branch
        >::type
    >
    inline static C* CTX(T *p) {
        static_assert(
            std::is_same<T0, void>::value
                or std::is_same<T0, Node>::value
                or std::is_same<T0, Series>::value
                or std::is_same<T0, Array>::value,
            "CTX() works on [void* Node* Series* Array*]"
        );
        if (not p)
            return nullptr;

        if (((cast(const Stub*, p)->header.bits & (
            SERIES_MASK_VARLIST
                | NODE_FLAG_FREE
                | NODE_FLAG_CELL
                | FLAG_FLAVOR_BYTE(255)
        ))
            | SERIES_FLAG_DYNAMIC  // permit non-dynamic (e.g. inaccessible
        ) !=
            SERIES_MASK_VARLIST
        ){
            panic (p);
        }

        return cast(C*, p);
    }

    template <typename P>
    inline Action* ACT(P p) {
        static_assert(
            std::is_same<P, void*>::value
                or std::is_same<P, Node*>::value
                or std::is_same<P, Series*>::value
                or std::is_same<P, Array*>::value,
            "ACT() works on [void* Node* Series* Array*]"
        );

        if (not p)
            return nullptr;

        if ((cast(const Stub*, p)->header.bits & (
            SERIES_MASK_DETAILS
                | NODE_FLAG_FREE
                | NODE_FLAG_CELL
                | FLAG_FLAVOR_BYTE(255)
        )) !=
            SERIES_MASK_DETAILS
        ){
            panic (p);
        }

        return cast(Action*, p);
    }

    // !!! STR() and SYM() casts should be updated to do more than const

    inline static String* STR(void *p)
      { return cast(String*, p); }

    inline static const String* STR(const void *p)
      { return cast(const String*, p); }

    // The only time a SYM should be mutable is at its creation time, or
    // when bits are being tweaked in binding slots.  Stored or external
    // pointers should always be const if downcasting.

    inline static Symbol* SYM(void *p)
      { return cast(Symbol*, p); }

    inline static const Symbol* SYM(const void *p)
      { return cast(const Symbol*, p); }

    // !!! There aren't currently that many VAL() casts in the system.  When
    // some arise, consider beefing up this cast.

    inline static REBVAL* VAL(void *p)
      { return cast(REBVAL*, p); }

    inline static const REBVAL* VAL(const void *p)
      { return cast(const REBVAL*, p); }

    template <class T>
    inline Level(*) LVL(T *p) {
        static_assert(
            std::is_same<T, void>::value
                or std::is_same<T, Node>::value,
                "LVL() works on [void* Node*]"
        );

        if (not p)
            return nullptr;

        if ((FIRST_BYTE(p) & (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x40_FREE
                | NODE_BYTEMASK_0x01_CELL
        )) != (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x01_CELL
        )){
            panic (p);
        }

        return cast(Level(*), p);
    }

    inline static Map* MAP(void *p) {  // not a fancy cast ATM.
        Array* a = ARR(p);
        assert(IS_PAIRLIST(a));
        return cast(Map*, a);
    }

    inline static Binary* BIN(void *p)
        { return cast(Binary*, p); }
    inline static const Binary* BIN(const void *p)
        { return cast(const Binary*, p); }

    #define KEYS(p)         x_cast(KeyList*, (p))
#endif
