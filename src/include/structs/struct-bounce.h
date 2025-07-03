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
// done for making two-byte patterns for things like BOUNCE_CONTINUE.
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
// 2. Ren-C is conservative about accepting arbitrary cells for two reasons.
//    One is that it's easy to slip and return a local address of a cell
//    made with something like DECLARE_ELEMENT(), and the warnings which
//    catch this (like -Wreturn-local-addr) are flaky and don't work at all
//    unless you're using higher optimization levels.  Another is that the
//    performance is best if the native itself copies the cell into the
//    OUT slot, because when the action code calls the dispatcher it checks
//    for equality to that pointer first.  Use `return COPY(cell)`.
//
#if NO_CPLUSPLUS_11 || NO_RUNTIME_CHECKS
    typedef RebolBounce Bounce;
#else
    struct Bounce {  // same size as RebolBounce...not directly compatible [1]
        RebolBounce b;  // void*

        Bounce() = default;

        explicit Bounce(const void* p) : b {p} {}

        Bounce(const nullptr_t&) : b {nullptr} {}

        Bounce(PermissiveZeroStruct&&) : b {nullptr} {}

        Bounce(const Cell* cell) : b {cell} {}  // either API cell or OUT [2]

        explicit Bounce(WildTwo* wildtwo) : b {wildtwo} {}

        Bounce(const char* utf8) : b {utf8} {}

        operator const void*() const
          { return b; }

        explicit operator const char*() const
          { return u_cast(const char*, b); }
    };
#endif


//=//// RE-SKIN APIS RETURNING RebolBounce TO RETURN Bounce ///////////////=//

#undef rebDelegate

#define rebDelegate(...) \
    cast(Bounce, rebDelegate_helper( \
        LIBREBOL_BINDING_NAME(),  /* captured from callsite! */ \
        __VA_ARGS__, rebEND \
    ))
