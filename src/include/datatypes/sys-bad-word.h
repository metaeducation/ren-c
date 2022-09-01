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
    Byte quote_byte
);

inline static option(SymId) VAL_WORD_ID(noquote(Cell(const*)) v);


inline static bool Is_Quasi_Word(Cell(const*) v)
  { return IS_QUASI(v) and HEART_BYTE_UNCHECKED(v) == REB_WORD; }

#define Init_Quasi_Word(out,sym) \
    TRACK(Init_Any_Word_Untracked((out), REB_WORD, (sym), QUASI_2))


//=//// BAD-WORD! ISOTOPES (just called "isotopes" for short) //////////////=//

// A bad word isotope is produced by the evaluator when an ordinary BAD-WORD!
// is evaluated.  These cannot live in blocks, and most are "unfriendly" and
// cannot be passed as normal parameters...you have to use ^META ones.

#define Init_Word_Isotope(out,label) \
    TRACK(Init_Any_Word_Untracked((out), REB_WORD, (label), ISOTOPE_0))

inline static bool Is_Word_Isotope(Cell(const*) v)
  { return QUOTE_BYTE(v) == ISOTOPE_0 and HEART_BYTE(v) == REB_WORD; }

inline static bool Is_Isotope(Cell(const*) v)
  { return QUOTE_BYTE(v) == ISOTOPE_0; }

inline static Value(*) Quasify_Isotope(Value(*) v) {
    assert(Is_Isotope(v));
    mutable_QUOTE_BYTE(v) = QUASI_2;
    return v;
}

inline static Value(*) Reify(Value(*) v) {
    if (Is_Nulled(v))
        Init_Blank(v);
    else if (Is_Isotope(v))  // currently includes void
        mutable_QUOTE_BYTE(v) = QUASI_2;
    return v;
}

inline static bool Is_Word_Isotope_With_Id(Cell(const*) v, SymId id) {
    assert(id != 0);

    if (not Is_Word_Isotope(v))
        return false;

    return id == VAL_WORD_ID(v);
}


//=//// "NONE" ISOTOPE (Empty BLOCK! Isotope) /////////////////////////////=//
//
// This is the default RETURN for when you just write something like
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

inline static Value(*) Init_Empty_Pack_Untracked(
    Cell(*) out,
    Byte quote_byte
){
    Init_Block(out, EMPTY_ARRAY);
    mutable_QUOTE_BYTE(out) = quote_byte;
    return cast(Value(*), out);
}

#define Init_None_Untracked(out) \
    Init_Empty_Pack_Untracked((ensure(Value(*), (out))), ISOTOPE_0)

#define Init_None(out) \
    TRACK(Init_None_Untracked(out))

#define Init_Meta_Of_None(out) \
    TRACK(Init_Empty_Pack_Untracked((out), QUASI_2))

inline static bool Is_None(Value(const*) v) {
    if (QUOTE_BYTE(v) != ISOTOPE_0 or HEART_BYTE(v) != REB_BLOCK)
        return false;
    return VAL_LEN_AT(v) == 0;
}

inline static bool Is_Meta_Of_None(Cell(const*) v) {
    if (QUOTE_BYTE(v) != QUASI_2 or HEART_BYTE(v) != REB_BLOCK)
        return false;
    return VAL_LEN_AT(v) == 0;
}


//=//// EMPTY SPLICE (Empty GROUP! Isotope) ///////////////////////////////=//
//
// The empty splice is exploited for its property of having void-like behavior
// while not being void...hence it can propagate "void intent" out of a branch
// even though the branch runs.
//
//     >> if false [<a>]
//     ; void (will trigger ELSE)
//
//     >> if true []
//     == ~()~  ; isotope (will trigger THEN, not ELSE)
//
//     >> append [a b c] if false [<a>]
//     == [a b c]
//
//     >> append [a b c] if true []
//     == [a b c]
//

inline static Value(*) Init_Empty_Splice_Untracked(Value(*) out) {
    Init_Group(out, EMPTY_ARRAY);
    mutable_QUOTE_BYTE(out) = ISOTOPE_0;
    return out;
}

#define Init_Empty_Splice(out) \
    TRACK(Init_Empty_Splice_Untracked(out))

#define Is_Empty_Splice(v) \
    ((READABLE(v)->header.bits & FLAG_QUOTE_BYTE(255) & FLAG_HEART_BYTE(255)) \
        == FLAG_QUOTE_BYTE(ISOTOPE_0) | FLAG_HEART_BYTE(REB_GROUP))

#define Is_Meta_Of_Empty_Splice(v) \
    ((READABLE(v)->header.bits & FLAG_QUOTE_BYTE(255) & FLAG_HEART_BYTE(255)) \
        == FLAG_QUOTE_BYTE(QUASI_1) | FLAG_HEART_BYTE(REB_GROUP))


//=//// ISOTOPIC DECAY /////////////////////////////////////////////////////=//

inline static REBVAL *Init_Blackhole(Cell(*) out);  // defined in %sys-token.h

inline static Value(*) Decay_If_Isotope(Value(*) v) {
    if (Is_Blank_Isotope(v))
        return Init_Nulled(v);
    return v;
}

inline static const REBVAL *Pointer_To_Decayed(const REBVAL *v) {
    if (Is_Blank_Isotope(v))
        return Lib(NULL);
    return v;
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
        Init_Blank_Isotope(v);
    else if (IS_LOGIC(v) and VAL_LOGIC(v) == false)
        Init_Word_Isotope(v, Canon(FALSE));
    return v;
}

inline static Value(*) Isotopify_If_Nulled(Value(*) v) {
    if (VAL_TYPE_UNCHECKED(v) == REB_NULL)
        Init_Blank_Isotope(v);
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
    FRESHEN_CELL_EVIL_MACRO(v);  // track to here not useful

  #if DEBUG_TRACK_EXTEND_CELLS  // `out` has tracking info we can use
    v->file = out->file;
    v->line = out->line;
    v->tick = TG_tick;
  #endif

    return cast(REBVAL*, out);
}

#define Move_Cell(out,v) \
    TRACK(Move_Cell_Untracked((out), (v), CELL_MASK_COPY))

#define Move_Cell_Core(out,v,cell_mask) \
    TRACK(Move_Cell_Untracked((out), (v), (cell_mask)))
