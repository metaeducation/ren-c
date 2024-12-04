//
//  File: %cell-blank.h
//  Summary: "BLANK! inert placeholder type"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// BLANK! cells are inert in the evaluator, and represented by an underscore.
// They are used as agnostic placeholders.
//
//    >> append [a b c] _
//    == [a b c _]
//
// BLANK! takes on some placeholder responsibilities of Rebol2's #[none]
// value, while the "soft failure" aspects are covered by NULL (which unlike
// blanks, can't be stored in blocks).  Consequently blanks are not "falsey"
// which means all "reified" values that can be stored in blocks are
// conditionally true.
//
//     >> if fourth [a b c _] [print "Blanks are truthy"]
//     Blanks are truthy
//
// Aiding in blank's usefulness as a placeholder, SPREAD of BLANK! gives
// back the same behavior as if you were to SPREAD an empty block:
//
//    >> append [d e] spread fourth [a b c []]
//    == [d e]
//
//    >> append [d e] spread fourth [a b c _]
//    == [d e]
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * A speculative feature for blanks is to consider them as spaces when
//   dealing with string operations:
//
//       >> append "ab" _
//       == "ab "
//
//       >> parse "a b" ["a" _ "b"]
//       == "b"
//
//   There are benefits and drawbacks to being casual about tihs conversion,
//   so at time of writing, it's not certain if this will be kept.
//

INLINE Element* Init_Blank_Untracked(Init(Element) out) {
    Reset_Cell_Header_Noquote(out, CELL_MASK_BLANK);
    Corrupt_Unused_Field(EXTRA(out).corrupt);  // doesn't get marked
    Corrupt_Unused_Field(PAYLOAD(Any, out).first.corrupt);
    Corrupt_Unused_Field(PAYLOAD(Any, out).second.corrupt);

    return out;
}

#define Init_Blank(out) \
    TRACK(Init_Blank_Untracked(out))


//=//// '~' QUASIFORM (a.k.a. TRASH) //////////////////////////////////////=//
//
// The quasiform of BLANK! is a tilde (instead of ~_~), and called TRASH
//
//    >> meta print "Quasiform of BLANK is TRASH"
//    Quasiform of BLANK is TRASH
//    == ~
//
// Trash cannot be SPREAD or passed to routines like EMPTY?, so it is a more
// ornery placeholder than blank.  Depending on one's desires, it can be
// a better substitution for Rebol's historical NONE! type than BLANK!.
// (Although both TRASH and BLANK! are truthy in Ren-C.)
//

INLINE Element* Init_Trash_Untracked(Init(Element) out) {
    Reset_Cell_Header(out, QUASIFORM_2_COERCE_ONLY, CELL_MASK_BLANK);
    Corrupt_Unused_Field(EXTRA(out).corrupt);  // doesn't get marked
    Corrupt_Unused_Field(PAYLOAD(Any, out).first.corrupt);
    Corrupt_Unused_Field(PAYLOAD(Any, out).second.corrupt);

    return out;
}

#define Init_Trash(out) \
    TRACK(Init_Trash_Untracked(out))

INLINE bool Is_Trash(Need(const Element*) v)
  { return HEART_BYTE(v) == REB_BLANK and QUOTE_BYTE(v) == QUASIFORM_2; }


//=//// '~' ANTIFORM (a.k.a. NOTHING) /////////////////////////////////////=//
//
// The antiform of BLANK! is called NOTHING, and it is used for the state of
// an unset variable.  It is also the result when a function has no meaningful
// value of return, so it has no display in the console.
//
//    >> x: anti _
//
//    >> unset? 'x
//    == ~true~  ; anti
//
//    >> print "Hello"
//
//    >> nothing? print "Hello"
//    == ~true~
//
// Picking antiform blank as the contents of unset variables has many benefits
// over choosing something like an `~unset~` or `~nothing~` antiforms:
//
//  * Reduces noise when looking at a list of variables to see which are unset
//
//  * We consider variables to be unset and not values, e.g. (unset? 'var).
//    This has less chance for confusion as if it were named ~unset~ people
//    would likely expect `(unset? ~unset~)` to work.
//
//  * Quick way to unset variables, simply `(var: ~)`
//
// The choice of this name (vs. "unset") was meditated on for quite some time,
// and resolved as superior to trying to claim there's such a thing as an
// "unset value".
//

INLINE Value* Init_Nothing_Untracked(Init(Value) out) {
    Reset_Cell_Header(out, ANTIFORM_0_COERCE_ONLY, CELL_MASK_BLANK);
    Corrupt_Unused_Field(EXTRA(out).corrupt);  // doesn't get marked
    Corrupt_Unused_Field(PAYLOAD(Any, out).first.corrupt);
    Corrupt_Unused_Field(PAYLOAD(Any, out).second.corrupt);

    return out;
}

#define Init_Nothing(out) \
    TRACK(Init_Nothing_Untracked(out))

#define Init_Meta_Of_Nothing(out)  Init_Trash(out)

#define NOTHING_VALUE \
    cast(const Value*, &PG_Nothing_Value)  // LIB(NOTHING) would be an action
