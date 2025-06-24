//
//  file: %cast-cells.hpp
//  summary: "Instrumented operators for casting Cell subclasses"
//  project: "Ren-C Interpreter and Run-time"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// See src/include/casts/README.md for general information about CastHook.
//
// This file is specifically for checking casts to Cells.
//
// One benefit is we can check cells for valid readability at the moment of
// the cast.  While this doesn't seem too profound since attempts to read
// the Cell would trigger failures anyway even without the casts...it does
// help with locality.  Also makes sure that locations are accurately labeled
// as to whether they have a valid Element/Value/Atom or are Sink()/Init().
//
// Another big benefit is that casts to Element can enusre that no antiforms
// are in the cell, and casts to Value don't hold unstable antiforms.  This
// could be accomplished without casts with helper functions such as
// Known_Element() or Known_Stable(), but using a cast makes the checks
// work generically in macros parameterized by type.  Also, using a cast
// helps point out "ugliness" that encourages caution at these points, and
// looking to find another way to do it.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. CastHook<> has two parameters (From and To types), but we pin down the
//    "To" type, then match a pattern for any "From" type (F).
//
// B. See the definition of CastHook for why the generalized casting
//    mechanic runs through const pointers only.
//
// C. See the definitions of UpcastTag and DowncastTag for an explanation of
//    why we trust upcasts by default (you can override it if needed).
//

// We don't bother with the "trusted" upcast here (yet?) but just check all
// conversions from these types.
//
DECLARE_C_TYPE_LIST(g_convertible_to_cell,
    Cell, Atom, Element, Value,
    Pairing,  // same size as Stub, holds two Cells
    char,  // some memory blobs use char* for debuggers to read UTF-8 easier
    Base, Byte, void
);


//=//// cast(Atom*, ...) //////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Atom*> {  // both must be const [B]
    static const Atom* convert(const F* p) {
        STATIC_ASSERT(In_C_Type_List(g_convertible_to_cell, F));

        const Cell* c = u_cast(const Cell*, p);
        Assert_Cell_Readable(c);
        unnecessary(assert(LIFT_BYTE_RAW(c) >= ANTIFORM_0));  // always true
        return u_cast(const Atom*, c);
    }
};


//=//// cast(Value*, ...) //////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Value*> {  // both must be const [B]
    static const Value* convert(const F* p) {
        STATIC_ASSERT(In_C_Type_List(g_convertible_to_cell, F));

        const Cell* c = u_cast(const Cell*, p);
        Assert_Cell_Readable(c);
        if (LIFT_BYTE_RAW(c) == ANTIFORM_0)
            assert(Is_Stable_Antiform_Kind_Byte(KIND_BYTE_RAW(c)));
        return u_cast(const Value*, c);
    }
};


//=//// cast(Element*, ...) ///////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHook<const F*, const Element*> {  // both must be const [B]
    static const Element* convert(const F* p) {
        STATIC_ASSERT(In_C_Type_List(g_convertible_to_cell, F));

        const Cell* c = u_cast(const Cell*, p);
        Assert_Cell_Readable(c);
        assert(LIFT_BYTE_RAW(c) != ANTIFORM_0);
        return u_cast(const Element*, c);
    }
};
