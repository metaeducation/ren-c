//
//  File: %sys-bad-word.h
//  Summary: "BAD-WORD! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// All datatypes (besides QUOTED!, VOID and NULL) have an "isotopic" form as
// well as a "quasi" form.  The quasi form will evaluate to the isotopic form,
// and the isotopic form is expressly prohibited from being put in arrays,
// while also causing errors if accessed from plain word fetches.
//
//     >> nice: first [~foo~]
//     == ~foo~
//
//     >> nice
//     == ~foo~
//
//     >> mean: ~foo~
//     == ~foo~  ; isotope
//
//     >> mean
//     ** Script Error: mean is ~foo~ isotope (see ^(...) and GET/ANY)
//
// With the use of the `^xxx` family of types and the `^` operator, it is
// possible to leverage a form of quoting to transition isotopes to normal, and
// normal bad words to quoted:
//
//     >> ^nice
//     == '~foo~
//
//     >> ^mean
//     == ~foo~
//
// Isotopes are foundational in covering edge cases in representation which
// plague Rebol2 and Red.  They enable shifting into a "non-literal" domain,
// where whatever "weird" condition the isotope was attempting to capture can
// be handled without worrying about conflating with more literal usages.
// A good example is addressing the splicing intent for blocks:
//
//     >> append [a b c] [d e]
//     == [a b c [d e]]
//
//     >> append [a b c] ~[d e]~
//     == [a b c d e]
//
//     >> append [a b c] '~[d e]~
//     == [a b c ~[d e]~]
//
// As demonstrated, the reified QUASI! form and the "ghostly" isotopic form
// work in concert to solve the problem.
//
// A special parameter convention must be used to receive isotopes.  Code that
// isn't expecting such strange circumstances can error if they ever happen,
// while more sensitive code can be adapted to cleanly handle the intents that
// they care about.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * The isotope states of several WORD!s have specific meaning to the
//   system...such as ~void~, and ~null~.  Each are described in below.
//
// * QUASI! states are truthy.  There's a reason for this, because it
//   allows operations in the ^META domain to easily use functions like ALL
//   and ANY on the meta values.  (See the FOR-BOTH example.)
//
// * Isotopes forms are neither true nor false...they must be decayed or
//   handled in some other way, for instance DID/DIDN'T or THEN/ELSE.
//

inline static REBVAL *Init_Any_Word_Untracked(
    Cell(*) out,
    enum Reb_Kind kind,
    Symbol(const*) sym,
    Flags flags
);

inline static OPT_SYMID VAL_WORD_ID(noquote(Cell(const*)) v);


inline static bool Is_Quasi_Word(Cell(const*) v)
  { return IS_QUASI(v) and HEART_BYTE_UNCHECKED(v) == REB_WORD; }

#define Init_Quasi_Word(out,sym) \
    Init_Any_Word_Untracked( \
        TRACK(out), REB_WORD, (sym), FLAG_QUOTE_BYTE(QUASI_1))


//=//// BAD-WORD! ISOTOPES (just called "isotopes" for short) //////////////=//

// A bad word isotope is produced by the evaluator when an ordinary BAD-WORD!
// is evaluated.  These cannot live in blocks, and most are "unfriendly" and
// cannot be passed as normal parameters...you have to use ^META ones.

inline static REBVAL *Init_Word_Isotope_Untracked(
    Value(*) out,
    option(Symbol(const*)) label
){
    return Init_Any_Word_Untracked(
        out, REB_WORD, label, FLAG_QUOTE_BYTE(ISOTOPE_255)
    );
}

#define Init_Word_Isotope(out,label) \
    Init_Word_Isotope_Untracked(TRACK(out), (label))

inline static bool Is_Word_Isotope(Cell(const*) v)
  { return QUOTE_BYTE(v) == ISOTOPE_255 and HEART_BYTE(v) == REB_WORD; }

inline static bool Is_Isotope(Cell(const*) v) {
    if (QUOTE_BYTE(v) != ISOTOPE_255)
        return false;

    assert(HEART_BYTE(v) != REB_0 and HEART_BYTE(v) != REB_NULL);
    return true;
}

inline static Value(*) Reify_Isotope(Value(*) v) {
    assert(Is_Isotope(v));
    mutable_QUOTE_BYTE(v) = QUASI_1;
    return v;
}

inline static bool Is_Word_Isotope_With_Id(
    Cell(const*) v,
    enum Reb_Symbol_Id id  // want to take ID instead of canon, faster check!
){
    assert(id != SYM_0);  // SYM_0 means unknown, not e.g. Is_None()

    if (not Is_Word_Isotope(v))
        return false;

    return id == cast(enum Reb_Symbol_Id, VAL_WORD_ID(v));
}


// The `~` isotope is chosen in particular by the system to represent variables
// that have not been assigned.  It has many benefits over choosing `~unset~`:
//
//  * Reduces noise in FRAME! to see which variables specialized
//
//  * Less chance for confusion since UNSET? takes a variable; if it were named
//    ~unset~ people would likely expect `(unset? ~unset~)` to work.
//
//  * Quick way to unset variables, simply `(var: ~)`
//
// But since we have to talk about what it is, we call it "none".
//
// It is also the default RETURN for when you just write something like
// `func [return: <none>] [...]`.  It represents the intention of not having a
// return value, but reserving the right to not be treated as invisible, so
// that if one ever did imagine an interesting value for it to return, the
// callsites wouldn't have assumed it was invisible.
//
// Even a function like PRINT has a potentially interesting return value,
// given that it channels through NULL if the print content vaporized and
// it printed nothing (not even a newline).  This lets you use it with ELSE,
// and you couldn't write `print [...] else [...]` if it would be sometimes
// invisible and sometimes not.
//
#define NONE_ISOTOPE                c_cast(const REBVAL*, &PG_None_Isotope)

#define Init_None(out) \
    Init_Blank_Untracked( \
        TRACK(ensure(Value(*), (out))), FLAG_QUOTE_BYTE(ISOTOPE_255))

#define Init_Meta_Of_None(out) \
    Init_Blank_Untracked(TRACK(out), FLAG_QUOTE_BYTE(QUASI_1))

inline static bool Is_None(Value(const*) v)
  { return Is_Isotope(v) and HEART_BYTE(v) == REB_BLANK; }

inline static bool Is_Meta_Of_None(Cell(const*) v)
  { return IS_QUASI(v) and HEART_BYTE(v) == REB_BLANK; }


//=//// VOID ISOTOPES AND VOID META STATE (NULL) //////////////////////////=//
//
// Void states are actually just CELL_FLAG_OUT_NOTE_VOIDED on top of
// an empty cell, to indicate something that has "invisible intent" but did
// not actually vanish.
//
//     >> x: if false [fail ~unreachable~]
//     ; void (decays to null)
//
//     >> x
//     ; null
//
// The isotope state exists to be used in frames as a signal of void intent,
// but since it is reified it lays claim to the QUASI-WORD! ~void~ when ^META'd.
// True void has a ^META of NULL to distinguish it.
//

#define Init_Void_Isotope(out)            Init_Word_Isotope((out), Canon(VOID))
#define Is_Void_Isotope(v)                Is_Word_Isotope_With_Id(v, SYM_VOID)
#define Init_Meta_Of_Void_Isotope(out)    Init_Quasi_Word((out), Canon(VOID))

inline static bool Is_Meta_Of_Void_Isotope(Cell(const*) v)
  { return Is_Quasi_Word(v) and VAL_WORD_SYMBOL(v) == Canon(VOID); }

#define Init_Meta_Of_Void(out)       Init_Nulled(out)
#define Is_Meta_Of_Void(v)           Is_Nulled(v)
#define Init_Decayed_Void(out)       Init_Nulled(out)
#define DECAYED_VOID_CELL            Lib(NULL)

#define Init_Meta_Of_Null(out) \
    Init_Nulled_Untracked(TRACK(out), FLAG_QUOTE_BYTE(ONEQUOTE_2))

inline static bool Is_Meta_Of_Null(Cell(const*) v)
  { return HEART_BYTE(v) == REB_NULL and QUOTE_BYTE(v) == ONEQUOTE_2; }


//=//// NULL ISOTOPE (unfriendly ~null~) ///////////////////////////////////=//
//
// There was considerable deliberation about how to handle branches that
// actually want to return NULL without triggering ELSE:
//
//     >> if true [null] else [print "Don't want this to print"]
//     ; null (desired result)
//
// Making branch results NULL if-and-only-if the branch ran would mean having
// to distort the result.
//
// The ultimate solution to this was to introduce a slight variant of NULL
// which would be short-lived (e.g. "decay" to a normal NULL) but carry the
// additional information that it was an intended branch result.  This
// seemed sketchy at first, but with ^(...) acting as a "detector" for those
// who need to know the difference, it has become a holistic solution.
//
// The "decay" of NULL isotopes occurs on variable assignment, and is seen
// on future fetches.  Hence:
//
//     >> x: if true [null]
//     == ~null~  ; isotope
//
//     >> x
//     ; null
//
// As with the natural concept of radiation, working with NULL isotopes can
// be tricky, and should be avoided by code that doesn't need to do it.  (But
// it has actually gotten much easier with ^(...) behaviors.)
//

#define Init_Null_Isotope(out)            Init_Word_Isotope((out), Canon(NULL))
#define Is_Null_Isotope(v)                Is_Word_Isotope_With_Id(v, SYM_NULL)
#define Init_Meta_Of_Null_Isotope(out)    Init_Quasi_Word((out), Canon(NULL))

inline static bool Is_Meta_Of_Null_Isotope(Cell(const*) v)
  { return Is_Quasi_Word(v) and VAL_WORD_SYMBOL(v) == Canon(NULL); }


//=//// END OF INPUT ISOTOPE //////////////////////////////////////////////=//
//
// The ~end~ isotope is used in function frames as a signal that the frame
// slot has hit an end.  It is converted during function calling to a void.
//
// If a slot takes both a <void> and <end>, there's not currently a way to
// tell the difference.  At one point there was an "endish" property whereby
// they could be distinguished via the unevaluated bit.

#define Init_End_Isotope(out)              Init_Word_Isotope((out), Canon(END))
#define Is_End_Isotope(v)                  Is_Word_Isotope_With_Id(v, SYM_END)
#define Init_Meta_Of_End_Isotope(out)      Init_Quasi_Word((out), Canon(END))

inline static bool Is_Meta_Of_End_Isotope(Cell(const*) v)
  { return Is_Quasi_Word(v) and VAL_WORD_SYMBOL(v) == Canon(END); }


//=//// ISOTOPIC DECAY /////////////////////////////////////////////////////=//

inline static REBVAL *Init_Blackhole(Cell(*) out);  // defined in %sys-token.h

inline static Value(*) Decay_If_Isotope(Value(*) v) {
    if (Is_Void(v))
        return Init_Decayed_Void(v);

    if (not Is_Word_Isotope(v))
        return v;

    switch (VAL_WORD_ID(v)) {
      case SYM_NULL :
        return Init_Nulled(v);
      case SYM_BLANK :
        return Init_Blank(v);
      case SYM_FALSE :
        return Init_False(v);
      case SYM_BLACKHOLE :
        return Init_Blackhole(v);

      default:
        return v;
    }
}

inline static const REBVAL *Pointer_To_Decayed(const REBVAL *v) {
    if (Is_Void(v))
        return DECAYED_VOID_CELL;

    if (not Is_Word_Isotope(v))
        return v;

    switch (VAL_WORD_ID(v)) {
      case SYM_NULL :
        return Lib(NULL);
      case SYM_FALSE :
        return Lib(FALSE);
      case SYM_BLANK :
        return Lib(BLANK);
      case SYM_BLACKHOLE :
        return Lib(BLACKHOLE);

      default:
        return v;
    }
}

inline static const REBVAL *rebPointerToDecayed(const REBVAL *v) {  // unused?
    if (v == nullptr)
        return v;  // API tolerance

    Value(const*) decayed = Pointer_To_Decayed(v);
    if (decayed == v)
        return v;
    return Is_Nulled(decayed) ? nullptr : decayed;
}

inline static Value(*) Isotopify_If_Falsey(Value(*) v) {
    if (Is_Isotope(v))
        return v;  // already an isotope (would trigger asserts on IS_X tests)
    if (Is_Nulled(v))
        Init_Null_Isotope(v);
    else if (IS_BLANK(v))
        Init_Word_Isotope(v, Canon(BLANK));
    else if (IS_LOGIC(v) and VAL_LOGIC(v) == false)
        Init_Word_Isotope(v, Canon(FALSE));
    return v;
}

inline static Value(*) Isotopify_If_Nulled(Value(*) v) {
    if (VAL_TYPE_UNCHECKED(v) == REB_NULL)
        Init_Null_Isotope(v);
    return v;
}


//=//// CELL MOVEMENT //////////////////////////////////////////////////////=//

// Moving a cell invalidates the old location.  This idea is a potential
// prelude to being able to do some sort of reference counting on series based
// on the cells that refer to them tracking when they are overwritten.  One
// advantage would be being able to leave the reference counting as-is.
//
// In the meantime, this just does a Copy + RESET.

inline static REBVAL *Move_Cell_Untracked(
    Cell(*) out,
    REBVAL *v,
    Flags copy_mask
){
    Copy_Cell_Untracked(out, v, copy_mask);  // Move_Cell() adds track to `out`
    RESET_Untracked(v);  // not useful to track and just implicate Move_Cell()

  #if DEBUG_TRACK_EXTEND_CELLS  // `out` has tracking info we can use
    v->file = out->file;
    v->line = out->line;
    v->tick = TG_tick;
  #endif

    return cast(REBVAL*, out);
}

#define Move_Cell(out,v) \
    Move_Cell_Untracked(TRACK(out), (v), CELL_MASK_COPY)

#define Move_Cell_Core(out,v,cell_mask) \
    Move_Cell_Untracked(TRACK(out), (v), (cell_mask))
