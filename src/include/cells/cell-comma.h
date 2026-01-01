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
// Commas are recognized specially by the evaluator, and produce a GHOST!:
//
//     >> eval:step [1 + 2, 10 + 20]
//     == [, 10 + 20]  ; new position, but produced 3 as product
//
//     >> [x ^y]: eval:step [, 10 + 20]
//     == \~('[] ~,~)~\  ; leading commas are ignored in an eval step
//
// (Although internally, if the evaluator knows you're not debugging, it will
// silently skip through the commas without yielding an evaluative product.)
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
    Tweak_Cell_Binding(out, UNBOUND);  // Is_Bindable_Heart() due to niche use
    Corrupt_Unused_Field(out->payload.split.one.corrupt);
    Corrupt_Unused_Field(out->payload.split.two.corrupt);

    return out;
}

#define Init_Comma(out) \
    TRACK(Init_Comma_Untracked(out))


//=//// GHOST! (COMMA! ANTIFORM) ///////////////////////////////////////////=//
//
// The unstable ~,~ antiform is used to signal vanishing intent, e.g. it is
// the return result of things like COMMENT and ELIDE.  It only *actually*
// vanishes if produced by a VANISHABLE function call, or if it is explicitly
// marked vanishable using the `^` operator.
//
// See Evaluator_Executor() for how stepping over a block retains the last
// value at each step, so that if a step produces a GHOST! the previous
// evaluation can be preserved.
//
// 1. The Stepper_Executor() needs to turn GHOST! into an empty PACK! if a
//    context is "afraid of ghosts" (e.g. a multi-step operation that hasn't
//    used an operator to indicate expectation of a ghost arising from a non
//    vanishable function).  But it can't turn the GHOST! into an empty pack
//    until it has finished processing any infix operation.  So a flag is
//    put on the GHOST! to carry the signal.
//
//    The flag is in the positive sense (i.e. if the flag is set, the GHOST!
//    is overwritten), because this way when the overwrite happens it also
//    clears the flag, so Stepper_Executor() doesn't leak a stray signal that
//    could have meaning to the next step (e.g. CELL_FLAG_NOTE is used by
//    frame processing for tracking if a FRAME! cell has been typechecked)
//

INLINE Value* Init_Ghost_Untracked(Init(Value) out) {
    Init_Comma_Untracked(out);
    Unstably_Antiformize_Unbound_Fundamental(out);
    assert(Is_Ghost(out));
    return out;
}

#define Init_Ghost(out) \
    TRACK(Init_Ghost_Untracked(out))

#define CELL_FLAG_OUT_NOTE_SCARY_GHOST  CELL_FLAG_NOTE  // turn into pack [1]

#define CELL_MASK_SCARY_GHOST \
    (FLAG_KIND_BYTE(TYPE_COMMA) \
        | FLAG_LIFT_BYTE(ANTIFORM_1) \
        | CELL_FLAG_OUT_NOTE_SCARY_GHOST)

#define Is_Scary_Ghost(out) /* check header with single mask operation */ \
    (((out)->header.bits & CELL_MASK_SCARY_GHOST) == CELL_MASK_SCARY_GHOST)


//=//// VOID is Used To Signal <end> Reached /////////////////////////////=//
//
// Unstable antiforms stored in variables is where the sidewalk ends as far
// as it comes to the idea of "truly unset".  Hence, if you want to signal
// an <end> was reached by the evaluator, VOID is pretty much the limit of
// how good it can get.
//
// If you have a ^META parameter, and the next evaluation is an actual VOID,
// it will conflate with the VOID produced by an `<end>`.  We could prohibit
// ^META parameters from being <end>-able and close that loophole, or leave
// it open and just accept the conflation.
//
// This usage of GHOST! applies in other places, such as when a PACK! has
// too few values, as this is more useful than erroring in the moment:
//
//     >> [a b c]: pack [1 2]
//     == \~('1 '2)~\  ; antiform
//
//     >> a
//     == 1
//
//     >> b
//     == 2
//
//     >> ghost? ^c
//     == \~okay~\  ; antiform
//
// Trash would be another possible choice (and able to store a message, like
// ~#PACK-TOO-SHORT~).  But the mechanics of the system are geared toward
// graceful handling of GHOST! with <opt> and null inter-convertibility, which
// aren't properties that one generally wants for TRASH!...that's designed to
// throw a deliberate informative wrench into things, to let you know why
// a variable has been "poisoned".  You shouldn't really be manipulating or
// querying TRASH!, just overwriting it (assuming it's not a protected variable
// that is intended to stay trash for a reason...)
//

#define Init_Void_For_End(out)  Init_Ghost(out)
#define Is_Endlike_Void(v)  Is_Ghost(v)

#define Init_Void_For_Unset(out)  Init_Ghost(out)
#define Is_Unsetlike_Void(v)  Is_Ghost(v)

#define Init_Unspecialized_Void(out)  Init_Ghost(out)
#define Is_Unspecialized_Void(v)  Is_Ghost(v)
