//
//  file: %cell-blank.h
//  summary: "BLANK! Datatype (decorates as [~ ' $ ^^ @ ~]) and VOID! Antiform"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020-2026 Ren-C Open Source Contributors
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
// The BLANK! is a datatype whose evaluator behavior is to act as what is
// referred to as an "expression barrier".  It will stop argument fulfillment,
// but if no argument fulfillment is in place then it has no effect.
//
// Rendering and scanning wise, blanks have no representation.  But rendering
// for lists use comma characters to "show where the blanks are":
//
//     >> 1 + 2,  ; <-- a BLANK! after the 2, hence we see a comma
//     == 3
//
//     >> 1 +, 2
//     ** Error: barrier hit during evaluation
//
// It has the property that it renders "glued" to the element to the left.
//
// Blanks are recognized specially by the evaluator, and produce a VOID!:
//
//     >> eval:step [1 + 2, 10 + 20]
//     == [, 10 + 20]  ; new position, but produced 3 as product
//
//     >> [x ^y]: eval:step [, 10 + 20]
//     == \~('[] ~)~\  ; antiform (pack!)
//
// (Although internally, if the evaluator knows you're not debugging, it will
// silently skip through the blanks without yielding an evaluative product.)
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * Something like BLANK! was actually seriously considered for R3-Alpha,
//   as an "explicit evaluation terminator":
//
//     http://www.rebol.net/r3blogs/0086.html
//

INLINE Element* Init_Blank_Untracked(Init(Element) out, Flags flags) {
    Reset_Cell_Header(
        out,
        FLAG_KIND_BYTE(TYPE_BLANK) | flags
            | CELL_FLAG_DONT_MARK_PAYLOAD_1
            | CELL_FLAG_DONT_MARK_PAYLOAD_2
    );
    Tweak_Cell_Binding(out, UNBOUND);  // Is_Bindable_Heart() due to niche use
    Corrupt_Unused_Field(out->payload.split.one.corrupt);
    Corrupt_Unused_Field(out->payload.split.two.corrupt);

    return out;
}

#define Init_Blank(out) \
    TRACK(Init_Blank_Untracked((out), FLAG_LIFT_BYTE(As_Lift(TYPE_BLANK))))


//=//// DECORATED BLANK! ("," DOES NOT RENDER) ////////////////////////////=//
//
// When a BLANK! is decorated with a Sigil, Quasi, or Quote, it does not
// render the comma character:
//
//     >> blank: first [,]
//
//     >> reduce [pin blank tie blank meta blank quasi blank quote blank]
//     == [@ ^ $ ~ ']  ; not [@, ^, $, ~, ',]
//
// These standalone forms are integral parts, and since they are so integral
// then their atomicity is more important than the scanner making it easy
// to make decorated commas.  e.g. (x: '@, y: ~, z: ', ...) should not see
// those as being decorated BLANK!s, merely separating the assignments.
//
// Given that: BLANK! becomes the perfect type to underlie the standalone
// decorated forms.  Since VOID! is antiform BLANK!, this also has the *very*
// pleasing property that the ~ antiform is void.
//

INLINE Element* Init_Sigiled_Blank_Core(Init(Element) out, Sigil sigil) {
    return Init_Blank_Untracked(
        out,
        FLAG_LIFT_BYTE(sigil ? Lift_From_Sigil(sigil) : As_Lift(TYPE_BLANK))
            | FLAG_SIGIL(sigil)
    );
}

#define Init_Sigiled_Blank(out,sigil) \
    TRACK(Init_Sigiled_Blank_Core((out), (sigil)))

#define Is_Cell_Blank_With_Lift_Sigil(v, lift_byte, sigil) \
    Cell_Has_Lift_Sigil_Heart( \
        Known_Stable(v), (lift_byte), (sigil), TYPE_BLANK)

#define Is_Pinned_Blank(v) /* renders as `@` [1] */ \
    Is_Cell_Blank_With_Lift_Sigil((v), As_Lift(TYPE_PINNED), SIGIL_PIN)

#define Is_Metaform_Blank(v) /* renders as `^` [1] */ \
    Is_Cell_Blank_With_Lift_Sigil((v), As_Lift(TYPE_METAFORM), SIGIL_META)

#define Is_Tied_Blank(v) /* renders as `$` [1] */ \
    Is_Cell_Blank_With_Lift_Sigil((v), As_Lift(TYPE_TIED), SIGIL_TIE)

INLINE bool Any_Sigiled_Blank(const Element* v) {
    if (LIFT_BYTE(v) > MAX_LIFT_NOQUOTE_NOQUASI or not Sigil_Of(v))
        return false;
    return Heart_Of(v) == TYPE_BLANK;
}


//=//// '~' QUASIFORM (a.k.a. QUASAR) /////////////////////////////////////=//
//
// The quasiform of BLANK! is a tilde (instead of ~,~), and called QUASAR
//
//    >> lift ()
//    == ~
//

#define Is_Quasar(v) \
    Cell_Has_Lift_Sigil_Heart( \
        Known_Stable(v), QUASIFORM_64, SIGIL_0, TYPE_BLANK)

INLINE Element* Init_Quasar_Untracked(Init(Element) out) {
    Init_Blank(out);
    Quasify_Isotopic_Fundamental(out);
    assert(Is_Quasar(out));
    return out;
}

#define Init_Quasar(out) \
    TRACK(Init_Quasar_Untracked(out))


//=//// VOID! (BLANK! ANTIFORM) ///////////////////////////////////////////=//
//
// The unstable ~ antiform is used to signal vanishing intent, e.g. it is
// the return result of things like COMMENT and ELIDE.  It only *actually*
// vanishes if produced by a VANISHABLE function call, or if it is explicitly
// approved as vanishable using the IDENTITY operator (`^`).
//
// See Evaluator_Executor() for how stepping over a block retains the last
// value at each step, so that if a step produces a VOID! the previous
// evaluation can be preserved.
//

#define Init_Void_Untracked(out) \
    Init_Blank_Untracked((out), FLAG_LIFT_BYTE(TYPE_VOID))

#define Init_Void(out) \
    TRACK(Init_Void_Untracked(out))

#define Init_Lifted_Void(out) \
    Init_Blank_Untracked(Possibly_Unstable(out), FLAG_LIFT_BYTE(QUASIFORM_64))


//=//// "VOID TO MAKE HEAVY" FLAG /////////////////////////////////////////=//
//
// The Stepper_Executor() needs to turn VOID! into an empty PACK! if the
// evaluation is "afraid of ghosts" (e.g. a multi-step operation that hasn't
// done something to indicate expectation of a void arising from a non
// vanishable function).  But it can't turn the VOID! into an empty pack
// until it has finished processing any infix operation.  So a flag is put on
// the VOID! to carry the signal.
//
// 1. The flag is in the positive sense (i.e. if the flag is set, the VOID!
//    gets overwritten), because this way when the overwrite happens it also
//    clears the flag, so Stepper_Executor() doesn't leak a stray signal that
//    could have meaning to the next step (e.g. CELL_FLAG_NOTE is used by
//    frame processing for tracking if a FRAME! cell has been typechecked)
//
// 2. Since "void to make heavy" voids only exist in OUT cells, we might could
//    check the header against a fixed bit pattern without masking...since it
//    is known that OUT cells don't (currently) have format bits on the cell.
//    But this could run afoul of fancier uses of extra header bits that
//    would apply to all cells, even OUT cells.  Keep it in mind, but we do
//    the masking for now.
//

#define CELL_FLAG_OUT_NOTE_VOID_TO_MAKE_HEAVY /* set if needs change [1] */ \
    CELL_FLAG_NOTE

#define Note_Level_Out_As_Void_To_Make_Heavy(L) \
    ((L)->out->header.bits |= CELL_FLAG_OUT_NOTE_VOID_TO_MAKE_HEAVY)

#define CELL_MASK_VOID_TO_MAKE_HEAVY \
    (FLAG_KIND_BYTE(HEART_BLANK_SIGNIFYING_VOID) \
        | FLAG_LIFT_BYTE(TYPE_VOID) \
        | CELL_FLAG_OUT_NOTE_VOID_TO_MAKE_HEAVY)

#define Is_Level_Out_Noted_Void_To_Make_Heavy(L) /* one mask operation [2] */ \
    (((L)->out->header.bits & CELL_MASK_VOID_TO_MAKE_HEAVY) \
        == CELL_MASK_VOID_TO_MAKE_HEAVY)


//=//// VOID! UNSET INTENT DOCUMENTATION //////////////////////////////////=//
//
// VOID! applies in some places, such as when a PACK! has too few values, as
// this is more useful than erroring in the moment:
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
//     >> void? ^c
//     == \~okay~\  ; antiform
//
// Trash would be another possible choice (and able to store a message, like
// ~<PACK-TOO-SHORT>~).  But the mechanics of the system are geared toward
// graceful handling of VOID! with <opt> and null inter-convertibility, which
// aren't properties that one generally wants for TRASH!...that's designed to
// throw a deliberate informative wrench into things, to let you know why
// a variable has been "poisoned".  You shouldn't really be manipulating or
// querying TRASH!, just overwriting it (assuming it's not a protected variable
// that is intended to stay trash for a reason...)
//

#define Init_Void_Signifying_Unset(out)  Init_Void(out)


//=//// VOID! UNBRANCHED INTENT DOCUMENTATION /////////////////////////////=//
//
// System-wide, a branching construct that doesn't take a branch will give
// VOID!...and if they take a branch that produces VOID!, they will turn it
// into an empty pack ("HEAVY VOID")
//

#define Init_Void_Signifying_Unbranched(out)  Init_Void(out)
