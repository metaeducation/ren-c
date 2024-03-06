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
// BLANK! takes on the placeholder responsibilities of Rebol2's #[none]
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
// The antiform of BLANK! is called "trash", and it is used for the state of
// an unset variable.  The quasiform of BLANK! is a tilde (instead of ~_~)
//
//    >> x: anti _
//
//    >> unset? 'x
//    == ~true~  ; anti
//
//    >> meta get/any 'x
//    == ~
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

INLINE Element* Init_Blank_Untracked(Cell* out, Byte quote_byte) {
    Freshen_Cell_Untracked(out);
    out->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL
            | FLAG_HEART_BYTE(REB_BLANK) | FLAG_QUOTE_BYTE(quote_byte)
    );

  #ifdef ZERO_UNUSED_CELL_FIELDS
    EXTRA(Any, out).corrupt = CORRUPTZERO;  // not Cell_Extra_Needs_Mark()
    PAYLOAD(Any, out).first.corrupt = CORRUPTZERO;
    PAYLOAD(Any, out).second.corrupt = CORRUPTZERO;
  #endif

    return cast(Element*, out);
}

#define Init_Blank(out) \
    TRACK(Init_Blank_Untracked((out), NOQUOTE_1))

#define Init_Quasi_Blank(out) \
    TRACK(Init_Blank_Untracked((out), QUASIFORM_2))



//=//// '~' ISOTOPE (a.k.a. TRASH) ////////////////////////////////////////=//
//
// Picking antiform as the contents of unset variables has many benefits
// over choosing something like an `~unset~` or `~trash~` antiforms:
//
//  * Reduces noise when looking at a list of variables to see which are unset
//
//  * We consider variables to be unset and not values, e.g. (unset? 'var).
//    This has less chance for confusion as if it were named ~unset~ people
//    would likely expect `(unset? ~unset~)` to work.
//
//  * Quick way to unset variables, simply `(var: ~)`
//
// While "trash" is a slightly jarring name for ~ antiforms, one doesn't need
// to call it by name to use it.  e.g. return specs can say `return: [~]`
// instead of `return: [trash?]`, and `return ~` instead of `return trash`
//
// The choice of this name (vs. "unset") was meditated on for quite some time,
// and resolved as superior to trying to claim there's such a thing as an
// "unset value".
//

#define Init_Trash(out) \
    u_cast(Value*, TRACK( \
        Init_Blank_Untracked(ensure(Sink(Value*), (out)), ANTIFORM_0)))

#define Init_Meta_Of_Trash(out)     Init_Quasi_Blank(out)

#define TRASH_CELL \
    cast(const Value*, &PG_Trash_Cell)  // Lib(TRASH) would be a function
