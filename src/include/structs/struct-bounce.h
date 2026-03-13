//
//  file: %struct-bounce.h
//  summary: "Return value from native functions"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2019-2025 Ren-C Open Source Contributors
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
// R3-Alpha natives returned an enum value, with indicators like R_RET or
// R_TRUE or R_ARG1 to say where to look for the return result.  Ren-C opens
// up the return result to basically any `const void*`, and then uses the
// properties of Detect_Rebol_Pointer() and other flags in the cell to decide
// what to do with the result.


//=//// WILDTWO TYPE USED FOR ARBITRARY BOUNCE SIGNALS ////////////////////=//
//
// The logic of Detect_Rebol_Pointer() is used to figure out what a bounce
// represents.  The DETECTED_AS_WILD pattern uses an illegal UTF-8 byte to
// let you build something that's not a Cell and not a Stub, and this is
// done for making two-byte patterns for BOUNCE_XXX values.
//
typedef Byte WildTwo[2];


//=//// BOUNCE ////////////////////////////////////////////////////////////=//
//
// 1. I thought that if it was `struct Bounce { RebolBounce b; }` that it
//    would be able to do checks on the types it received while being
//    compatible with a void* in the dispatchers using %rebol.h.  So these
//    would be compatible:
//
//      typedef Bounce (Dispatcher)(...);  // %sys-core.h clients
//      typedef void* (RebolActionCFunction)(...);  // %rebol.h clients
//
//    As it turns out the compiler doesn't generate compatible output, even
//    with Bounce being a standard_layout struct.  :-/
//
//    The importance of compile-time checking Bounce in the core is high,
//    because it's very easy to say `return Error_Xxx()` instead of writing
//    `return fail (Error_Xxx())` or `panic (Error_Xxx())`.
//
// 2. While the API clients use RebolBounce as void* and accept Value* as
//    a parameter, internal code doesn't let you pass arbitrary cells.  So you
//    must fill the output cell and return BOUNCE_TOPLEVEL_OUT if it's not an
//    API value (or use a helper that does that, like BOUNCE_OUT).
//

INLINE bool Is_Api_Value(const Value* v);

#if (! CPLUSPLUS_11)
    typedef RebolBounce Bounce;
#else
    struct NEEDFUL_NODISCARD Bounce {
        RebolBounce b;  // Bounce not directly compatible w/RebolBounce!!! [1]

        Bounce() = default;

        Bounce(std::nullptr_t) : b {nullptr} {}

        Bounce(Value* v) : b {v} {
            assert(
                v == nullptr  // rebValue() and friends may return nullptr
                or Is_Api_Value(v)  // use BOUNCE_TOPLEVEL_OUT if not API [2]
            );
        }

        Bounce(const char* cstr) : b {cstr} {}  // not-yet-validated UTF-8

        explicit Bounce(const void* p) : b {p} {}  // a.k.a. RebolBounce

        explicit Bounce(Level* L) : b {L} {}  // Level* is a continuation

      #if NEEDFUL_RESULT_USES_WRAPPER
        Bounce(needful::Result0Struct) : b {nullptr} {}  // to accept `fail`
      #endif

      #if NEEDFUL_CPP_ENHANCEMENTS
        Bounce(needful::Nocast0Struct) : b {nullptr} {}  // Result(T) uses this
      #else
        explicit Bounce(int z) : b {nullptr} {
          assert(z == 0);  // only 0 allowed, for Result(Bounce) from 0
          UNUSED(z);
        }
      #endif

        explicit Bounce(WildTwo* wildtwo) : b {wildtwo} {}

        operator const void*() const
          { return b; }

        explicit operator const char*() const
          { return u_cast(const char*, b); }
    };
#endif

#if CPLUSPLUS_11  // avoid e.g. legacy comparisons to OUT
    void operator==(const Bounce& b, const Cell* cell) = delete;
    void operator==(const Cell* cell, const Bounce& b) = delete;
    void operator!=(const Bounce& b, const Cell* cell) = delete;
    void operator!=(const Cell* cell, const Bounce& b) = delete;

    inline bool operator==(const Bounce& b, std::nullptr_t)
      { return b.b == nullptr; }

    inline bool operator==(std::nullptr_t, const Bounce& b)
      { return b.b == nullptr; }

    inline bool operator!=(const Bounce& b, std::nullptr_t)
      { return b.b != nullptr; }

    inline bool operator!=(std::nullptr_t, const Bounce& b)
      { return b.b != nullptr; }
#endif


// C function implementing a native ACTION!
//
typedef Bounce (Executor)(Level* level_);
typedef Executor Dispatcher;  // sub-dispatched in Action_Executor()


//=//// RE-SKIN APIS RETURNING RebolBounce TO RETURN Bounce ///////////////=//

#undef rebDelegate

#define rebDelegate(...) \
    x_cast(Bounce, rebDelegate_helper( \
        LIBREBOL_BINDING_NAME(),  /* captured from callsite! */ \
        __VA_ARGS__, rebEND \
    ))
