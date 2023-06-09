//
//  File: %sys-nulled.h
//  Summary: "NULL definitions"
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
// are to represent an "optional" value, they must have a bit pattern.  So
// NULL is the isotopic form of the WORD! null.
//

inline static const Raw_String* VAL_NOTHING_FILE(noquote(Cell(const*)) v) {
    assert(HEART_BYTE(v) == REB_VOID or HEART_BYTE(v) == REB_BLANK);
    return SYM(VAL_NODE1(v));
}
inline static void INIT_VAL_NOTHING_FILE(Cell(*) v, String(const*) file)
  { INIT_VAL_NODE1(v, file); }

inline static LineNumber VAL_NOTHING_LINE(noquote(Cell(const*)) v) {
    assert(HEART_BYTE(v) == REB_VOID or HEART_BYTE(v) == REB_BLANK);
    return PAYLOAD(Any, v).second.i;
}
inline static void INIT_VAL_NOTHING_LINE(Cell(*) v, LineNumber line)
  { PAYLOAD(Any, v).second.i = line; }


inline static bool Is_Nulled(Cell(const*) v) {
    return QUOTE_BYTE(v) == 0  // Checked version, checks for READABLE()
        and HEART_BYTE_UNCHECKED(v) == REB_WORD
        and VAL_WORD_ID(v) == SYM_NULL;
}

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
        INIT_VAL_NOTHING_LINE(out, FRM_LINE(TOP_FRAME));
        INIT_VAL_NOTHING_FILE(out, FRM_FILE(TOP_FRAME));
    }
    else {
        INIT_VAL_NOTHING_LINE(out, 0);
        INIT_VAL_NOTHING_FILE(out, nullptr);
    }

    return cast(REBVAL*, out);
}


//=//// BLANK! ////////////////////////////////////////////////////////////=//
//

#define Init_Blank_Untracked(out,quote_byte) \
    Init_Nothing_Untracked((out), REB_BLANK, (quote_byte))

#define Init_Blank(out) \
    TRACK(Init_Blank_Untracked((out), UNQUOTED_1))

#define Init_Quasi_Blank(out) \
    TRACK(Init_Blank_Untracked((out), QUASI_2))

inline static bool Is_Quasi_Blank(Cell(const*) v)
  { return IS_QUASI(v) and HEART_BYTE(v) == REB_BLANK; }


//=//// NULL //////////////////////////////////////////////////////////////=//
//
// 1. We ensure that non-quoted, non-quasi NULL isn't written into a Cell(*)
//    e.g. for a BLOCK!... must be a Value(*), e.g. a context variable or
//    frame output.

#define Init_Word_Isotope(out,label) \
    TRACK(Init_Any_Word_Untracked(ensure(Value(*), (out)), REB_WORD, \
            (label), ISOTOPE_0))

#define Init_Quasi_Word(out,label) \
    TRACK(Init_Any_Word_Untracked((out), REB_WORD, (label), QUASI_2))

inline static bool Is_Word_Isotope(Cell(const*) v);

inline static bool Is_Word_Isotope_With_Id(Cell(const*) v, SymId id);

#define Init_Nulled(out) \
    Init_Word_Isotope((out), Canon(NULL))

#define Init_Quasi_Null(out) \
    Init_Quasi_Word((out), Canon(NULL))

inline bool Is_Quasi_Null(Cell(const*) v) {
    if (not IS_QUASI(v))
        return false;
    if (HEART_BYTE(v) != REB_WORD)
        return false;
    return VAL_WORD_ID(v) == SYM_NULL;
}

#define Init_Meta_Of_Null(out) \
    Init_Quasi_Null(out)

#define Is_Meta_Of_Null(v) \
    Is_Quasi_Null(v)


//=//// "HEAVY NULLS" (BLOCK! Isotope Pack with `~null~` in it) ///////////=//
//
// An "pack" of a ~[~null~]~ isotope is used for the concept of a "heavy null".
// This is something that will act like "pure" NULL in almost all contexts,
// except that things like THEN will consider it to have been the product of
// a "taken branch".
//
//     >> x: ~[~null~]~
//     == ~null~  ; isotope
//
//     >> if true [null]
//     == ~[~null~]~  ; isotope
//
//     >> if true [null] else [print "This won't run"]
//     == ~[~null~]~  ; isotope
//
// ("Heavy Voids" are an analogous concept for VOID.)

#define Init_Heavy_Null(out) \
    Init_Pack((out), PG_1_Quasi_Null_Array)

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
    if (Is_Nulled(v))
        Init_Heavy_Null(v);
    return v;
}
