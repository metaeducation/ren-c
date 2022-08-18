//
//  File: %sys-meta.h
//  Summary: "ERROR! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
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
// The concept of META is related to quoting, where in the ^META domain a
// "normal" value will be quoted one level higher than what it represents.
// Non-quoted values represent special cases.
//
//   * A non-quoted BAD-WORD! is the meta-representation of an *isotope*.
//     Typical isotopes are "unfriendly" and cannot be handled by normal
//     function arguments or variable fetches.  But once in the ^META
//     domain as a BAD-WORD!, the isotope can be passed around normally
//     until it is UNMETA'd back into an isotope again.
//
//   * A non-quoted ERROR! is the meta-representation of a *failure*.  A
//     failure state can't be stored in variables and will raise an alarm
//     if something in a processing pipeline doesn't ask to ^META it.  While
//     it's in the ^META state it can also be passed around normally until
//     it is UNMETA'd back into a failure again.
//
//   * If you ^META a NULL it remains NULL; the one falsey meta-state.
//
// It's hard to summarize in one place all the various applications of this
// feature.  But it's critical to accomplishing composability by which a
// usermode function can accomplish what the system is able to do internally
// with C.  See FOR-BOTH for at least one good example.
//
//  https://forum.rebol.info/t/1833


//=//// FAILURE STATES ////////////////////////////////////////////////////=//

inline static bool Is_Failure(Cell(const*) v)
  { return HEART_BYTE_UNCHECKED(v) == REB_ERROR
    and QUOTE_BYTE_UNCHECKED(v) == ISOTOPE_255; }

inline static bool Is_Meta_Of_Failure(Cell(const*) v)
  { return HEART_BYTE_UNCHECKED(v) == REB_ERROR
    and QUOTE_BYTE_UNCHECKED(v) == QUASI_1; }


inline static Value(*) Failurize(Cell(*) v) {
    assert(IS_ERROR(v) and QUOTE_BYTE(v) == UNQUOTED_0);
    Force_Location_Of_Error(VAL_CONTEXT(v), TOP_FRAME);  // ideally already set
    mutable_QUOTE_BYTE(v) = ISOTOPE_255;
    return VAL(v);
}

inline static bool Is_Splice(Cell(const*) v)
  { return HEART_BYTE_UNCHECKED(v) == REB_BLOCK
    and QUOTE_BYTE_UNCHECKED(v) == ISOTOPE_255; }


inline static Value(*) Splicify(Cell(*) v) {
    assert(IS_BLOCK(v) and QUOTE_BYTE(v) == UNQUOTED_0);
    mutable_QUOTE_BYTE(v) = ISOTOPE_255;
    return VAL(v);
}

inline static bool Is_Meta_Of_Splice(Cell(const*) v)
  { return HEART_BYTE_UNCHECKED(v) == REB_BLOCK
    and QUOTE_BYTE_UNCHECKED(v) == QUASI_1; }



inline static REBVAL *Unquasify(REBVAL *v) {
    assert(QUOTE_BYTE(v) == QUASI_1);
    mutable_QUOTE_BYTE(v) = UNQUOTED_0;
    return v;
}

inline static REBVAL *Quasify(REBVAL *v) {
    assert(QUOTE_BYTE(v) == UNQUOTED_0);
    assert(not Is_Nulled(v) and not Is_Void(v));
    mutable_QUOTE_BYTE(v) = QUASI_1;
    return v;
}



//=//// ISOTOPIC QUOTING ///////////////////////////////////////////////////=//

// When a plain BAD-WORD! evaluates, it stays as the same BAD-WORD! but with
// the isotope bit set.  If you want that to be generically reversible, then
// quoting an isotopic BAD-WORD! has to give a plain one...then quoting a
// plain one gives a QUOTED!, etc.
//
// Because QUOTE doesn't take isotope BAD-WORD!s as parameters, it doesn't have
// to deal with this problem.  But rebQ() in the API does, as does the idea
// of "literalization".


//=//// META QUOTING ///////////////////////////////////////////////////////=//


// Meta quoting is a superset of plain quoting.  It has the twist that it can
// quote isotopes to produce QUASI! values.  This is done by META (alias ^)
// and the REB_META_XXX family of values (like ^WORD, ^TU.P.LE...)

inline static Cell(*) Meta_Quotify(Cell(*) v) {
    if (Is_Isotope(v)) {
        Reify_Isotope(v);  // ...make it "friendly" now...
        return v;
    }
    return Quotify(v, 1);  // a non-isotope winds up quoted
}

inline static Cell(*) Meta_Unquotify(Cell(*) v) {
    if (Is_Meta_Of_Failure(v))
        fail (VAL_CONTEXT(v));  // too dangerous to create failure easily
    if (QUOTE_BYTE(v) == QUASI_1)
        mutable_QUOTE_BYTE(v) = ISOTOPE_255;
    else
        Unquotify_Core(v, 1);
    return v;
}

#if CPLUSPLUS_11
    inline static REBVAL *Meta_Quotify(REBVAL *v)
        { return SPECIFIC(Meta_Quotify(cast(Cell(*), v))); }

    inline static REBVAL *Meta_Unquotify(REBVAL *v)
        { return SPECIFIC(Meta_Unquotify(cast(Cell(*), v))); }
#endif


inline static Bounce Native_Unmeta_Result(Frame(*) frame_, const REBVAL *v) {
    assert(Is_Stale_Void(&TG_Thrown_Arg));
    if (Is_Meta_Of_Void(v))
        return BOUNCE_VOID;
    if (Is_Meta_Of_Failure(v))
        return Failurize(Unquasify(Copy_Cell(frame_->out, v)));
    return Meta_Unquotify(Copy_Cell(frame_->out, v));
}
