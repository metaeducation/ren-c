//
//  file: %cell-comma.h
//  summary: "COMMA! Datatype and Vanishing GHOST! Antiform of ~,~"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020-2025 Ren-C Open Source Contributors
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
// Commas are recognized specially by the evaluator, and are skipped over
// entirely without producing any evaluative product.
//
//     >> eval:step [1 + 2, 10 + 20]
//     == [, 10 + 20]  ; new position, but produced 3 as product
//
//     >> eval:step [, 10 + 20]
//     == []  ; leading commas are ignored in an eval step
//
//     >> eval:step []
//     == ~null~  ; anti (signals end reached, and no evaluative product)
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * Something like COMMA! was actually seriously considered for R3-Alpha,
//   as an "explicit evaluation terminator":
//
//     http://www.rebol.net/r3blogs/0086.html
//

INLINE Element* Init_Comma_Untracked(Init(Element) out) {
    Reset_Cell_Header_Noquote(out, CELL_MASK_COMMA);
    Tweak_Cell_Binding(out, UNBOUND);  // Is_Bindable() due to niche feed use
    Corrupt_Unused_Field(out->payload.split.one.corrupt);
    Corrupt_Unused_Field(out->payload.split.two.corrupt);

    return out;
}

#define Init_Comma(out) \
    TRACK(Init_Comma_Untracked(out))


//=//// GHOST! (COMMA! ANTIFORM) //////////////////////////////////////////=//
//
// The unstable ~,~ antiform is used to signal vanishing intent, e.g. it is
// the return result of things like COMMENT and ELIDE.
//
// See Evaluator_Executor() for how stepping over a block retains the last
// value at each step, so that if a step produces a GHOST! the previous
// evaluation can be preserved.
//

INLINE Atom* Init_Ghost_Untracked(Init(Atom) out, bool surprising) {
    Init_Comma_Untracked(out);
    LIFT_BYTE_RAW(out) = ANTIFORM_0;  // commas are isotopic
    if (not surprising)
        Set_Cell_Flag(out, OUT_HINT_UNSURPRISING);
    return out;
}

#define Init_Surprising_Ghost(out) \
    TRACK(Init_Ghost_Untracked((out), true))

#define Init_Unsurprising_Ghost(out) \
    TRACK(Init_Ghost_Untracked((out), false))

INLINE Atom* UNSURPRISING(Atom* atom) {
    assert(Is_Ghost(atom) or Is_Atom_Action(atom));
    Set_Cell_Flag(atom, OUT_HINT_UNSURPRISING);
    return atom;
}
