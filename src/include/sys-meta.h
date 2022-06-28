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

inline static bool Is_Failure(const Cell *v)
  { return HEART_BYTE_UNCHECKED(v) == REB_ERROR
    and QUOTE_BYTE_UNCHECKED(v) == FAILURE_255; }

inline static Cell *Reify_Failure(Cell *v) {
    assert(HEART_BYTE(v) == REB_ERROR and QUOTE_BYTE(v) == FAILURE_255);
    mutable_QUOTE_BYTE(v) = 0;
    return v;
}

inline static Value *Failurize(Cell *v) {
    assert(IS_ERROR(v) and QUOTE_BYTE(v) == 0);
    Force_Location_Of_Error(VAL_CONTEXT(v), FS_TOP);  // ideally already set
    mutable_QUOTE_BYTE(v) = FAILURE_255;
    return VAL(v);
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

inline static Cell *Isotopic_Quote(Cell *v) {
    if (Is_Isotope(v)) {
        Reify_Isotope(v);  // ...make it "friendly" now...
        return v;  // ...but differentiate its status by not quoting it...
    }
    if (Is_Failure(v)) {
        Reify_Failure(v);
        return v;
    }
    return Quotify(v, 1);  // a non-isotope BAD-WORD! winds up quoted
}

inline static Cell *Isotopic_Unquote(Cell *v) {
    assert(not Is_Nulled(v));  // use Meta_Unquotify() instead
    if (IS_BAD_WORD(v))  // Meta quote flipped isotope off, flip back on.
        Isotopify(v);
    else if (IS_ERROR(v))
        Failurize(v);
    else
        Unquotify_Core(v, 1);
    return v;
}

// It's easiest to write the isotopic general forms by doing a single isotopic
// step, and then N - 1 non-isotopic steps.

#if CPLUSPLUS_11
    inline static REBVAL *Isotopic_Quote(REBVAL *v)
      { return SPECIFIC(Isotopic_Quote(cast(Cell*, v))); }

    inline static REBVAL *Isotopic_Unquote(REBVAL *v)
      { return SPECIFIC(Isotopic_Unquote(cast(Cell*, v))); }
#endif


//=//// META QUOTING ///////////////////////////////////////////////////////=//


// Meta quoting is almost exactly like isotopic quoting, but it has a twist
// that NULL does not become a single tick mark (') but rather it stays as
// NULL.  It also translates emptiness (e.g. an END marker) into an isotope
// BAD-WORD! of ~void~.  It is done by ^ and the the REB_META_XXX family.

inline static Cell *Meta_Quotify(Cell *v) {
    if (VAL_TYPE_UNCHECKED(v) == REB_NULL)
        return v;  // as-is
    return Isotopic_Quote(v);
}

inline static Cell *Meta_Unquotify(Cell *v) {
    if (Is_Nulled(v))
        return v;  // do nothing
    if (IS_ERROR(v))
        fail (VAL_CONTEXT(v));  // too dangerous to create failure easily
    return Isotopic_Unquote(v);
}

#if CPLUSPLUS_11
    inline static REBVAL *Meta_Quotify(REBVAL *v)
        { return SPECIFIC(Meta_Quotify(cast(Cell*, v))); }

    inline static REBVAL *Meta_Unquotify(REBVAL *v)
        { return SPECIFIC(Meta_Unquotify(cast(Cell*, v))); }
#endif

inline static REBVAL *Reify_Eval_Out_Meta(REBVAL *out) {
    if (Is_Void(out))
        return Init_Meta_Of_Void(out);

    return Meta_Quotify(out);
}

inline static REB_R Native_Unmeta_Result(REBFRM *frame_, const REBVAL *v) {
    assert(Is_Stale_Void(&TG_Thrown_Arg));
    if (Is_Meta_Of_Void(v))
        return R_VOID;
    if (Is_Meta_Of_End(v))
        fail ("END not processed by UNMETA at this time");
    if (IS_ERROR(v))
        return Failurize(Copy_Cell(frame_->out, v));
    return Meta_Unquotify(Copy_Cell(frame_->out, v));
}
