//
//  file: %cell-space.h
//  summary: "SPACE inert placeholder type"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// Space cells are inert in the evaluator, and represented by an underscore.
// They are used as agnostic placeholders.
//
//    >> append [a b c] _
//    == [a b c _]
//
// Space takes on some placeholder responsibilities of Rebol2's NONE!
// value, while the "soft failure" aspects are covered by NULL (which unlike
// blanks, can't be stored in blocks).  Consequently spaces are not "falsey"
// which means all "reified" values that can be stored in blocks are
// conditionally true.
//
//     >> if fourth [a b c _] [print "Spaces are truthy"]
//     Spaces are truthy
//



//=//// '~' QUASIFORM (a.k.a. QUASAR) /////////////////////////////////////=//
//
// The quasiform of space is a tilde (instead of ~_~), and called QUASAR
//
//    >> lift print "Quasiform of SPACE is QUASAR"
//    Quasiform of SPACE is QUASAR
//    == ~
//
// !!! At one point it was very fast to initialize a QUASAR, as it could be
// done with only the header.  Consider the idea of making character literals
// able to be initialized with just the header for space-like cases.
//

INLINE Element* Init_Quasar_Untracked(Init(Element) out) {
    Init_Char_Unchecked_Untracked(out, ' ');  // use space as the base
    LIFT_BYTE_RAW(out) = QUASIFORM_2;  // spaces are isotopic
    return out;
}

#define Init_Quasar(out) \
    TRACK(Init_Quasar_Untracked(out))

INLINE bool Is_Quasar(Need(const Element*) v) {
    if (LIFT_BYTE(v) != QUASIFORM_2)
        return false;
    return IS_CHAR_CELL(v) and Cell_Codepoint(v) == ' ';
}


//=//// '~' ANTIFORM (a.k.a. TRIPWIRE) ////////////////////////////////////=//
//
// All RUNE! values have antiforms, that are considered to be TRASH!.
//
// The antiform of SPACE is a particularly succinct trash state, called
// TRIPWIRE, and it is frequently the default state of an unset variable.  It
// is also the result when a function has no meaningful value of return, so it
// has no display in the console.
//
//    >> x: anti _
//
//    >> unset? 'x
//    == ~true~  ; anti
//
//    >> print "Hello"
//
//    >> tripwire? print "Hello"
//    == ~true~
//
// The name "tripwire" (vs. "unset") was meditated on for quite some time,
// and resolved as superior to trying to claim there's such a thing as an
// "unset value".
//
// Picking antiform SPACE as the contents of unset variables has many benefits
// over choosing a WORD! antiform like `~unset~` or `~tripwire~`:
//
//  * Reduces noise when looking at a list of variables to see which are unset
//
//  * Quick way to unset variables, simply `(var: ~)`
//
//  * Variables that hold tripwires aren't "unset", they're set to a tripwire.
//    The question of if a variable holds trash is better as TRASHED?, while
//    UNRESOLVED? can be used to talk about variables that can't be found
//    at all (UNSET? would be a weird name for that).
//

INLINE Value* Init_Tripwire_Untracked(Init(Value) out) {
    Init_Char_Unchecked_Untracked(out, ' ');  // use space as the base
    LIFT_BYTE_RAW(out) = ANTIFORM_0;  // spaces are isotopic
    return out;
}

#define Init_Tripwire(out) \
    TRACK(Init_Tripwire_Untracked(out))

#define Init_Lifted_Tripwire(out) \
    Init_Quasar(out)

INLINE bool Is_Tripwire(Need(const Value*) v) {
    if (LIFT_BYTE(v) != ANTIFORM_0)
        return false;
    return IS_CHAR_CELL(v) and Cell_Codepoint(v) == ' ';
}


//=//// <end> SIGNALING WITH TRIPWIRE (~ antiform) ////////////////////////=//
//
// Special handling is required in order to allow a kind of "light variadic"
// form, where a parameter can be missing.
//
// For a time this was distinguished with a special ~end~ antiform.  But this
// was rethought in light of the fact that trash antiforms are unique
// among stable antiforms, as needing to be a ^META parameter in order to be
// passed to a function.  That means it can signal willingness of a parameter
// to be "fully missing" no matter what position it is in an argument list.
//
// This macro helps keep track of those places in the source that are the
// implementation of the "tripwire due to end" behavior.
//

#define Init_Tripwire_Due_To_End(out) \
    Init_Tripwire(out)

#define Is_Endlike_Tripwire(cell) \
    Is_Atom_Trash(cell)


//=//// STANDALONE "SIGIL?" ELEMENTS (@ ^ $) //////////////////////////////=//
//
// These are just sigilized versions of (_) which is the literal space char.
// Space itself is not thought of as a "Sigil" because (sigil of [a b]) is
// null, not space.
//

INLINE Element* Init_Sigil(Init(Element) out, Sigil sigil) {
    return Sigilize(Init_Space(out), sigil);
}

INLINE bool Any_Sigil(const Element* e) {
    if (LIFT_BYTE(e) != NOQUOTE_1 or not Sigil_Of(e))
        return false;
    return IS_CHAR_CELL(e) and Cell_Codepoint(e) == ' ';
}

INLINE bool Is_Sigil(const Value* c, Sigil sigil) {
    if (LIFT_BYTE(c) != NOQUOTE_1 or Sigil_Of(c_cast(Element*, c)) != sigil)
        return false;
    return IS_CHAR_CELL(c) and Cell_Codepoint(c) == ' ';
}

#define Is_Pin_Sigil(cell)  Is_Sigil((cell), SIGIL_PIN)
#define Is_Meta_Sigil(cell)  Is_Sigil((cell), SIGIL_META)
#define Is_Tie_Sigil(cell)  Is_Sigil((cell), SIGIL_TIE)


//=//// SIGIL-TO-CHARACTER CONVERSION /////////////////////////////////////=//

INLINE char Char_For_Sigil(Sigil sigil) {
    switch (sigil) {
      case SIGIL_META:  return '^';
      case SIGIL_PIN:   return '@';
      case SIGIL_TIE:   return '$';
      default:
        assert(false);
        return 0;  // silence warning
    }
}
