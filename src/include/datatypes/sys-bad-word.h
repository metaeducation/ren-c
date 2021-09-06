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
// BAD-WORD!s carry symbols like WORD!s do, but are rendered like ~void~ or
// ~unset~.  They are designed to cover some edge cases in representation, and
// are ordinarily considered neither true nor false:
//
//     >> if (first [~foo~]) [print "This won't work."]
//     ** Script Error: BAD-WORD! values aren't conditionally true or false
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
//   system...such as ~unset~, ~void~, ~stale~, and ~null~.  Each are described
//   in sections below.
//
// * While normal BAD-WORD!s are neither true nor false, this may vary for the
//   isotope forms.  (For instance the ~null~ isotope is falsey!)
//
// * See %sys-trash.h for a special case of a cell that will trigger panics
//   if it is ever read in the debug build, but is just an ordinary BAD-WORD!
//   of ~trash~ in the release build.
//


// Note: definition of Init_Bad_Word_Untracked() is in %sys-trash.h

#define Init_Bad_Word(out,sym) \
    Init_Bad_Word_Untracked(TRACK_CELL_IF_DEBUG(out), (sym), CELL_MASK_NONE)

inline static const REBSYM* VAL_BAD_WORD_LABEL(
    REBCEL(const*) v
){
    assert(CELL_KIND(v) == REB_BAD_WORD);
    assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
    return cast(const REBSYM*, VAL_NODE1(v));
}

#define VAL_BAD_WORD_ID(v) \
    ID_OF_SYMBOL(VAL_BAD_WORD_LABEL(v))


//=//// BAD-WORD! ISOTOPES (or just "isotopes") ////////////////////////////=//

// A bad word isotope is produced by the evaluator when an ordinary BAD-WORD!
// is evaluated.  These cannot live in blocks, and most are "unfriendly" and
// cannot be passed as normal parameters.

inline static bool Is_Isotope(
    const RELVAL *v,
    enum Reb_Symbol_Id id  // want to take ID instead of canon, faster check!
){
    assert(id != SYM_0);
    if (not IS_BAD_WORD(v))
        return false;
    if (NOT_CELL_FLAG(v, ISOTOPE))
        return false;  // friendly form of BAD-WORD!
    if (cast(REBLEN, id) == cast(REBLEN, VAL_BAD_WORD_ID(v)))
        return true;
    return false;
}

#define Init_Isotope(out,sym) \
    Init_Bad_Word_Untracked(TRACK_CELL_IF_DEBUG(out), (sym), CELL_FLAG_ISOTOPE)


// ~unset~ is chosen in particular by the system to represent variables that
// have not been assigned.

#define UNSET_VALUE         c_cast(const REBVAL*, &PG_Unset_Value)
#define Init_Unset(out)     Init_Isotope((out), Canon(UNSET))
#define Is_Unset(v)         Is_Isotope(v, SYM_UNSET)


// `~void~` is treated specially by the system, to convey "invisible intent".
// It is what `do []` evaluates to, as well as `do [comment "hi"])`.  Some
// constructs will actually implicitly assume the invisible intent means
// invisibility, such as `all [do [comment "hi"], 1 + 2]` being 3...despite
// the fact that a ~void~ isotope is not generally "true or fales".

#define VOID_VALUE          c_cast(const REBVAL*, &PG_Void_Value)
#define Init_Void(out)      Init_Isotope((out), Canon(VOID))
#define Is_Void(v)          Is_Isotope((v), SYM_VOID)


// See EVAL_FLAG_INPUT_WAS_INVISIBLE for the rationale behind ~stale~, that
// has a special relationship with ~void~.
//
#define Init_Stale(out)     Init_Isotope((out), Canon(STALE))
#define Is_Stale(v)         Is_Isotope((v), SYM_STALE)


// `~none~` is the default RETURN for when you just write something like
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
#define Init_None(out)      Init_Isotope((out), Canon(NONE))
#define Is_None(v)          Is_Isotope((v), SYM_NONE)


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

#define Init_Nulled_Isotope(out) \
    Init_Isotope((out), Canon(NULL))

inline static bool Is_Nulled_Isotope(const RELVAL *v)
  { return Is_Isotope(v, SYM_NULL); }

inline static RELVAL *Decay_If_Isotope(RELVAL *v) {
    if (Is_Nulled_Isotope(v))
        Init_Nulled(RESET(v));
    else if (Is_Isotope(v, SYM_BLANK))
        Init_Blank(RESET(v));
    else if (Is_Isotope(v, SYM_FALSE))
        Init_False(RESET(v));
    return v;
}

inline static RELVAL *Isotopify_If_Falsey(RELVAL *v) {
    if (IS_NULLED(v))
        Init_Isotope(RESET(v), Canon(NULL));
    else if (IS_BLANK(v))
        Init_Isotope(RESET(v), Canon(BLANK));
    else if (IS_LOGIC(v) and VAL_LOGIC(v) == false)
        Init_Isotope(RESET(v), Canon(FALSE));
    return v;
}

inline static RELVAL *Isotopify_If_Nulled(RELVAL *v) {
    if (IS_NULLED(v))
        Init_Nulled_Isotope(RESET(v));
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

  #if defined(DEBUG_TRACK_EXTEND_CELLS)  // `out` has tracking info we can use
    v->file = out->file;
    v->line = out->line;
    v->tick = TG_Tick;
  #endif

    return cast(REBVAL*, out);
}

#define Move_Cell(out,v) \
    Move_Cell_Untracked(TRACK_CELL_IF_DEBUG(out), (v), CELL_MASK_COPY)

#define Move_Cell_Core(out,v,cell_mask) \
    Move_Cell_Untracked(TRACK_CELL_IF_DEBUG(out), (v), (cell_mask))
