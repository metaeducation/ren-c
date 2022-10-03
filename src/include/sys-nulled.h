//
//  File: %sys-nulled.h
//  Summary: "NULL definitions (transient evaluative cell--not a DATATYPE!)"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
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
// NULL is a transient evaluation product.  It is used as a signal for
// "soft failure", e.g. `find [a b] 'c` is NULL, hence they are conditionally
// false.  But null isn't an "ANY-VALUE!", and can't be stored in BLOCK!s that
// are seen by the user.
//
// The libRebol API takes advantage of this by actually using C's concept of
// a null pointer to directly represent the optional state.  By promising this
// is the case, clients of the API can write `if (value)` or `if (!value)`
// and be sure that there's not some nonzero address of a "null-valued cell".
// So there is no `isRebolNull()` API.
//
// But that's the API.  Internally, cells are the currency used, and if they
// are to represent an "optional" value, there must be a special bit pattern
// used to mark them as not containing any value at all.  These are called
// "nulled cells" and marked by means of their HEART_BYTE being REB_NULL.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * NULL has an isotopic form called VOID, which closely relates the two.
//   see %sys-void.h.
//
// * BLANK! are a kind of "reified" form of nothingness, that evaluate
//   to NULL, and use the same payload structure
//
//     >> _
//     ; null
//
// * NULL has no semantic "payload" for its cell contents.  So it uses the
//   space to record the file and line number the frame was at when it was
//   generated.  So error messages can be supplemented with some information
//   about where the null came from.  BLANK!s and VOID get this too.
//

inline static const Raw_String* VAL_NULLED_FILE(noquote(Cell(const*)) v) {
    assert(HEART_BYTE(v) == REB_VOID or HEART_BYTE(v) == REB_BLANK);
    return SYM(VAL_NODE1(v));
}
inline static void INIT_VAL_NULLED_FILE(Cell(*) v, String(const*) file)
  { INIT_VAL_NODE1(v, file); }

inline static LineNumber VAL_NULLED_LINE(noquote(Cell(const*)) v) {
    assert(HEART_BYTE(v) == REB_VOID or HEART_BYTE(v) == REB_BLANK);
    return PAYLOAD(Any, v).second.i;
}
inline static void INIT_VAL_NULLED_LINE(Cell(*) v, LineNumber line)
  { PAYLOAD(Any, v).second.i = line; }


#define Is_Nulled(v) \
    (VAL_TYPE(v) == REB_NULL)

inline static String(const*) FRM_FILE(Frame(*) f);
inline static LineNumber FRM_LINE(Frame(*) f);

inline static REBVAL *Init_Nothing_Untracked(
    Cell(*) out,
    Byte heart_byte,
    Byte quote_byte
){
    FRESHEN_CELL_EVIL_MACRO(out);
    out->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL
            | FLAG_HEART_BYTE(heart_byte) | FLAG_QUOTE_BYTE(quote_byte)
            | CELL_FLAG_FIRST_IS_NODE
    );

    // Extra can't be used for an integer, because although NULL isn't bindable
    // the BLANK! has to be evaluative, pushing its number up into the range of
    // bindable types.  Set binding to nullptr and use payload slot for line.

    mutable_BINDING(out) = nullptr;

    if (TOP_FRAME) {
        INIT_VAL_NULLED_LINE(out, FRM_LINE(TOP_FRAME));
        INIT_VAL_NULLED_FILE(out, FRM_FILE(TOP_FRAME));
    }
    else {
        INIT_VAL_NULLED_LINE(out, 0);
        INIT_VAL_NULLED_FILE(out, nullptr);
    }

    return cast(REBVAL*, out);
}

#define Init_Blank_Untracked(out,quote_byte) \
    Init_Nothing_Untracked((out), REB_BLANK, (quote_byte))

// We ensure that non-quoted, non-quasi NULL isn't written into a Cell(*) e.g.
// for a BLOCK!... must be a Value(*), e.g. a context variable or frame output.
//
#define Init_Nulled(out) \
    TRACK(Init_Blank_Untracked(ensure(Value(*), (out)), ISOTOPE_0))

#define Init_Meta_Of_Null(out) \
    Init_Blank(out)

#define Is_Meta_Of_Null(v) \
    IS_BLANK(v)

// We test for null on an arbitrary result that may be an isotope.  Since we
// don't have generic isotope handling we usually just pass them through, so
// the unchecked type test for null is done first.  But plain Is_Nulled() will
// assert on isotopes.  Make the code look friendlier with this simple macro.
//
#define Is_Breaking_Null(out) \
    (VAL_TYPE_UNCHECKED(out) == REB_NULL)

#define Init_Blank(out) \
    TRACK(Init_Blank_Untracked((out), UNQUOTED_1))



//=//// BLANK! ISOTOPE (THEN-triggering NULL) /////////////////////////////=//
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
// The "decay" of BLANK! isotopes occurs on variable assignment, and is seen
// on future fetches.  Hence:
//
//     >> x: if true [null]
//     == ~_~  ; isotope (decays to null)
//
//     >> x
//     ; null
//
// As with the natural concept of radiation, working with BLANK! isotopes can
// be tricky, and should be avoided by code that doesn't need to do it.
//

#define Init_Quasi_Blank(out) \
    TRACK(Init_Blank_Untracked((out), QUASI_2))

inline static bool Is_Quasi_Blank(Cell(const*) v)
  { return IS_QUASI(v) and HEART_BYTE(v) == REB_BLANK; }



//=//// "HEAVY NULLS" (BLOCK! Isotope Pack with `_` in it) ////////////////=//
//
// An "pack" of a ~[_]~ isotope is used for the concept of a "heavy null".
// This is something that will act like "pure" NULL in almost all contexts,
// except that things like THEN will consider it to have been the product of
// a "taken branch".
//
//     >> x: ~[_]~
//     ; null
//
//     >> if true [null]
//     == ~[_]~  ; isotope
//
//     >> if true [null] else [print "This won't run"]
//     == ~[_]~  ; isotope
//
// ("Heavy Voids" are an analogous concept for VOID.)

#define Init_Heavy_Null(out) \
    Init_Pack((out), PG_1_Blank_Array)

#define Init_Meta_Of_Heavy_Null(out) \
    TRACK(Init_Pack_Untracked((out), QUASI_2, (a)))

inline static bool Is_Heavy_Null(Cell(const*) v) {
    if (not Is_Pack(v))
        return false;
    Cell(const*) tail;
    Cell(const*) at = VAL_ARRAY_AT(&tail, v);
    return (tail == at + 1) and Is_Meta_Of_Null(at);
}

inline static bool Is_Meta_Of_Heavy_Null(Cell(const*) v) {
    if (not Is_Meta_Of_Pack(v))
        return false;
    Cell(const*) tail;
    Cell(const*) at = VAL_ARRAY_AT(&tail, v);
    return (tail == at + 1) and Is_Meta_Of_Null(at);
}

inline static Value(*) Isotopify_If_Nulled(Value(*) v) {
    if (VAL_TYPE_UNCHECKED(v) == REB_NULL)
        Init_Heavy_Null(v);
    return v;
}
