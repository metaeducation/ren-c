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
// BAD-WORD!s carry symbols like WORD!s do, but are rendered like `~void~` or
// `~`.  They are designed to cover some edge cases in representation.
//
// But there's an additional twist on bad words, which is that when they are
// put into a variable they can be stored in either a normal state or an
// "isotope" state.  They are transitioned into the isotope state by evaluation
// which leads to "pricklier" behaviors...such as not being able to be
// retrieved through ordinary WORD! fetches.
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
// This enables shifting into a kind of "meta" domain, where whatever "weird"
// condition the isotope was attempting to capture and warn about can be
// handled literally.  Code that isn't expecting such strange circumstances
// can error if they ever happen, while more sensitive code can be adapted to
// cleanly handle the intents that they care about.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * The isotope states of several BAD-WORD!s have specific meaning to the
//   system...such as `~`, ~void~, and ~null~.  Each are described in below.
//
// * Normal BAD-WORD!s are truthy.  There's a reason for this, because it
//   allows operations in the ^META domain to easily use functions like ALL
//   and ANY on the meta values, with NULL being the only falsey meta state.
//
// * Isotopes forms are neither true nor false...they must be decayed or
//   handled in some other way, for instance DID/DIDN'T or THEN/ELSE.
//
// * See %sys-trash.h for a special case of a cell that will trigger panics
//   if it is ever read in the debug build, but is just an ordinary BAD-WORD!
//   of ~trash~ in the release build.
//

inline static REBVAL *Init_Any_Word_Untracked(
    RELVAL *out,
    enum Reb_Kind kind,
    const REBSYM *sym
);


// Note: Init_Bad_Word_Untracked() forward-declared in %sys-trash.h

#define Init_Bad_Word(out,sym) \
    Init_Bad_Word_Untracked(TRACK(out), (sym), CELL_MASK_NONE)

inline static option(const REBSYM*) VAL_BAD_WORD_LABEL_UNCHECKED(
    noquote(const Cell*) v
){
    return cast(const REBSYM*, VAL_NODE1(v));
}

inline static option(const REBSYM*) VAL_BAD_WORD_LABEL(
    noquote(const Cell*) v
){
    assert(CELL_KIND(v) == REB_BAD_WORD);
    assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
    return cast(const REBSYM*, VAL_NODE1(v));
}

inline static OPT_SYMID VAL_BAD_WORD_ID(const RELVAL *v) {
    assert(IS_BAD_WORD(v));
    assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
    if (not VAL_NODE1(v))
        return cast(OPT_SYMID, SYM_0);

    return ID_OF_SYMBOL(cast(const REBSYM*, VAL_NODE1(v)));
}


//=//// BAD-WORD! ISOTOPES (just called "isotopes" for short) //////////////=//

// A bad word isotope is produced by the evaluator when an ordinary BAD-WORD!
// is evaluated.  These cannot live in blocks, and most are "unfriendly" and
// cannot be passed as normal parameters.
//
// Note: Testing for isotopes could be faster if CELL_FLAG_ISOTOPE promised to
// only be used on BAD-WORD!s...but given how rare cell flags are it's
// probably better to reserve it to mean something else for other types.

inline static bool Is_Isotope_With_Id(
    const RELVAL *v,
    enum Reb_Symbol_Id id  // want to take ID instead of canon, faster check!
){
    if (not IS_BAD_WORD(v) or NOT_CELL_FLAG(v, ISOTOPE))
        return false;
    return cast(REBLEN, id) == cast(REBLEN, VAL_BAD_WORD_ID(v));
}

inline static bool Is_Isotope(const RELVAL *v) {
    if (not IS_BAD_WORD(v) or NOT_CELL_FLAG(v, ISOTOPE))
        return false;
    return true;
}

inline static REBVAL *Init_Isotope_Untracked(
    RELVAL *out,
    option(const REBSYM*) label
){
    return Init_Bad_Word_Untracked(out, label, CELL_FLAG_ISOTOPE);
}

#define Init_Isotope(out,label) \
    Init_Isotope_Untracked(TRACK(out), (label))


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
// But since we have to talk about what it is, we call it "none".  Unlike
// ~void~ or ~end~ isotopes, it is reified and can appear in variables.
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

#define Init_None(out)              Init_Isotope((out), nullptr)
#define Is_None(v)                  Is_Isotope_With_Id((v), SYM_0)
#define Init_Meta_Of_None(out)      Init_Bad_Word((out), nullptr)

inline static bool Is_Meta_Of_None(const RELVAL *v)
  { return IS_BAD_WORD(v) and VAL_BAD_WORD_ID(v) == SYM_0; }


//=//// VOID ISOTOPES AND VOID META STATE (@void) /////////////////////////=//
//
// Void states are actually just CELL_FLAG_OUT_NOTE_VOIDED on top of
// an empty cell, to indicate something that has "invisible intent" but did
// not actually vanish.
//
//     >> x: ~void~
//     ; void (decays to none)
//
//     >> get/any 'x
//     == ~  ; isotope (none, e.g. x is unset)
//
// The isotope state exists to be used in frames as a signal of void intent,
// but since it is reified it lays claim to the BAD-WORD! ~void~ when ^META'd.
// True void is @void to distinguish it.
//

#define Init_Void_Isotope(out)              Init_Isotope((out), Canon(VOID))
#define Is_Void_Isotope(v)                  Is_Isotope_With_Id(v, SYM_VOID)
#define Init_Meta_Of_Void_Isotope(out)      Init_Bad_Word((out), Canon(VOID))

inline static bool Is_Meta_Of_Void_Isotope(const RELVAL *v)
  { return IS_BAD_WORD(v) and VAL_BAD_WORD_LABEL(v) == Canon(VOID); }

#define Init_Meta_Of_Void(out) \
    Init_Any_Word_Untracked(TRACK(out), REB_THE_WORD, Canon(VOID))

inline static bool Is_Meta_Of_Void(const RELVAL *v)
  { return IS_THE_WORD(v) and VAL_WORD_SYMBOL(v) == Canon(VOID); }


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
// The "decay" of NULL isotopes occurs on variable retrieval.  Hence:
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

#define Init_Null_Isotope(out)              Init_Isotope((out), Canon(NULL))
#define Is_Null_Isotope(v)                  Is_Isotope_With_Id(v, SYM_NULL)
#define Init_Meta_Of_Null_Isotope(out)      Init_Bad_Word((out), Canon(NULL))

inline static bool Is_Meta_Of_Null_Isotope(const RELVAL *v)
  { return IS_BAD_WORD(v) and VAL_BAD_WORD_LABEL(v) == Canon(NULL); }


//=//// END OF INPUT META STATE (@END THE-WORD!) //////////////////////////=//
//
// Reaching an end of input is something some functions want to be able to
// handle.  The @end meta state is reserved for this purpose.
//
// The ~end~ isotope is used in function frames as a signal that the frame
// slot has hit an end.  But this creates a situation where ~end~ isotopes
// are reified and thus exist...which means their ^META state has to take
// the ~end~ BAD-WORD!.

#define Init_End_Isotope(out)              Init_Isotope((out), Canon(END))
#define Is_End_Isotope(v)                  Is_Isotope_With_Id(v, SYM_END)
#define Init_Meta_Of_End_Isotope(out)      Init_Bad_Word((out), Canon(END))

inline static bool Is_Meta_Of_End_Isotope(const RELVAL *v)
  { return IS_BAD_WORD(v) and VAL_BAD_WORD_LABEL(v) == Canon(END); }


#define Init_Meta_Of_End(out) \
    Init_Any_Word_Untracked(TRACK(out), REB_THE_WORD, Canon(END))

inline static bool Is_Meta_Of_End(const RELVAL *v)
  { return IS_THE_WORD(v) and VAL_WORD_SYMBOL(v) == Canon(END); }


//=//// ISOTOPIC DECAY /////////////////////////////////////////////////////=//

inline static REBVAL *Init_Blackhole(RELVAL *out);  // defined in %sys-token.h

inline static RELVAL *Decay_If_Isotope(RELVAL *v) {
    if (not IS_BAD_WORD(v) or NOT_CELL_FLAG(v, ISOTOPE))
        return v;

    switch (VAL_BAD_WORD_ID(v)) {
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
    if (not IS_BAD_WORD(v) or NOT_CELL_FLAG(v, ISOTOPE))
        return v;

    switch (VAL_BAD_WORD_ID(v)) {
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

inline static const REBVAL *rebPointerToDecayed(const REBVAL *v) {
    if (v == nullptr)
        return v;  // API tolerance
    if (not IS_BAD_WORD(v) or NOT_CELL_FLAG(v, ISOTOPE)) {
        assert(not IS_NULLED(v));  // API speaks nullptr, not nulled cells
        return v;
    }
    if (VAL_BAD_WORD_ID(v) == SYM_NULL)
      return nullptr;

    return Pointer_To_Decayed(v);
}

inline static RELVAL *Isotopify_If_Falsey(RELVAL *v) {
    if (IS_NULLED(v))
        Init_Isotope(v, Canon(NULL));
    else if (IS_BLANK(v))
        Init_Isotope(v, Canon(BLANK));
    else if (IS_LOGIC(v) and VAL_LOGIC(v) == false)
        Init_Isotope(v, Canon(FALSE));
    return v;
}

inline static RELVAL *Isotopify_If_Nulled(RELVAL *v) {
    if (IS_NULLED(v))
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
    RELVAL *out,
    REBVAL *v,
    REBFLGS copy_mask
){
    Copy_Cell_Untracked(out, v, copy_mask);  // Move_Cell() adds track to `out`
    RESET_Untracked(v);  // not useful to track and just implicate Move_Cell()

  #if DEBUG_TRACK_EXTEND_CELLS  // `out` has tracking info we can use
    v->file = out->file;
    v->line = out->line;
    v->tick = TG_Tick;
  #endif

    return cast(REBVAL*, out);
}

#define Move_Cell(out,v) \
    Move_Cell_Untracked(TRACK(out), (v), CELL_MASK_COPY)

#define Move_Cell_Core(out,v,cell_mask) \
    Move_Cell_Untracked(TRACK(out), (v), (cell_mask))
