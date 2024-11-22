//
//  File: %cell-comma.h
//  Summary: "COMMA! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The COMMA! is a datatype whose evaluator behavior is to act as what is
// referred to as an "expression barrier".  It will stop argument fulfillment,
// but if no argument fulfillment is in place then it has no effect.
//
//     >> 1 + 2,
//     == 3
//
//     >> 1 +, 2
//     ** Error: barrier hit during evaluation
//
// It has the property that it renders "glued" to the element to the left.
//
// Commas are effectively invisible, but they accomplish this not by
// producing Nihil (an empty PACK! antiform) but rather by making an antiform
// COMMA! which is called a "barrier".  It's treated like nihil in interstitial
// positions and vaporizes, but has the special property of appearing like
// an <end>...as well as stopping lookahead.  For code that doesn't care
// about the subtlety, nihil and barrier are both considered "elisions".
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * Something like COMMA! was actually seriously considered for R3-Alpha,
//   as an "explicit evaluation terminator":
//
//     http://www.rebol.net/r3blogs/0086.html
//
// * An original implementation of expression barriers used the heavier `|`
//   character.  However that was considered non-negotiable as "alternate" in
//   PARSE, where expression barriers would also be needed.  Also, it was a
//   fairly big interruption visually...so comma was preferred.  It is still
//   possible to get the same effect of an expression barrier with any user
//   function, so `|` could be used for this in normal evaluation if it
//   evaluated to a COMMA! antiform (for instance).
//

INLINE Element* Init_Comma(Init(Element) out) {
    Reset_Cell_Header_Untracked(out, CELL_MASK_COMMA);
    BINDING(out) = nullptr;  // Is_Bindable() due to niche variadic feed use
    Corrupt_Unused_Field(PAYLOAD(Any, out).first.corrupt);
    Corrupt_Unused_Field(PAYLOAD(Any, out).second.corrupt);

    return out;
}

INLINE Atom* Init_Barrier(Init(Atom) out) {
    Init_Comma(out);
    return Coerce_To_Unstable_Antiform(out);
}

INLINE bool Is_Elision(Need(Atom*) v) {
    return Is_Barrier(v) or Is_Nihil(v);
}

INLINE bool Is_Meta_Of_Elision(Cell* v) {
    return Is_Meta_Of_Barrier(v) or Is_Meta_Of_Nihil(v);
}
