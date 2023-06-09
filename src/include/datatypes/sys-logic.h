//
//  File: %sys-logic.h
//  Summary: "LOGIC! Datatype Header"
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
// Since Rebol was firm on TRUE and FALSE being WORD!s (both of which were
// seen as conditionally truthy), it was an uphill battle to figure out
// a representation for logic literals.  R3-Alpha used #[true] and #[false]
// but often molded them as looking like the words true and false anyway.
//
// Ren-C's answer is to use the isotopic WORD!s for ~true~ and ~false~,
// which creates some friction as these isotopic states can't be put in
// blocks directly...but that turns out to be a benefit, and helps guide the
// user to triage their intent with LOGIC-TO-WORD or REIFY-LOGIC, etc.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * See ~null~ for another isotopic word state that is falsey, like ~false~.
//
// * There may be value to making ~on~ and ~off~ and ~yes~ and ~no~ be other
//   isotopic words that register as falsey... or more generally allowing
//   the concept of truthiness and falseyness to be overridden in a way that
//   is cross-cutting for a "language skin" (e.g. Redbol)
//
// * Despite Rebol's C heritage, the INTEGER! 0 is purposefully not "falsey".
//

#define Init_True(out)      Init_Word_Isotope((out), Canon(TRUE))
#define Init_False(out)     Init_Word_Isotope((out), Canon(FALSE))

#define Is_True(out)        Is_Word_Isotope_With_Id((out), SYM_TRUE)
#define Is_False(out)       Is_Word_Isotope_With_Id((out), SYM_FALSE)

inline static bool IS_LOGIC(Cell(const*) v) {
    return Is_True(v) or Is_False(v);
}

#define Init_Logic(out,flag) \
    Init_Word_Isotope((out), (flag) ? Canon(TRUE) : Canon(FALSE))

inline static bool VAL_LOGIC(Cell(const*) v) {
    if (Is_True(v))
        return true;
    if (Is_False(v))
        return false;
    fail ("Attempt to test VAL_LOGIC() on non-LOGIC!");
}


inline static bool Is_Truthy(Cell(const*) v) {
    if (QUOTE_BYTE(v) == ISOTOPE_0) {
        assert(Is_Isotope_Stable(v));
        if (not Is_Word_Isotope(v))
            return true;  // all non-word isotopes are truthy
        if (Is_Nulled(v))  // blank isotope is null, falsey
            return false;
        if (Is_True(v))
            return true;
        if (Is_False(v))
            return false;
        fail (Error_Bad_Isotope(v));  // !!! special error?
    }

    if (QUOTE_BYTE(v) != UNQUOTED_1)
        return true;  // all QUOTED! and QUASI! types are truthy

    if (HEART_BYTE(v) == REB_VOID)
        fail (Error_Bad_Void());  // void is neither truthy nor falsey

    return true;  // all other non-isotopic values are truthy
}

#define Is_Falsey(v) \
    (not Is_Truthy(v))

// Although a BLOCK! value is true, some constructs are safer by not allowing
// literal blocks.  e.g. `if [x] [print "this is not safe"]`.  The evaluated
// bit can let these instances be distinguished.  Note that making *all*
// evaluations safe would be limiting, e.g. `foo: any [false-thing []]`...
// So ANY and ALL use Is_Truthy() directly
//
inline static bool Is_Conditional_True(const REBVAL *v) {
    if (Is_Falsey(v))
        return false;
    if (IS_BLOCK(v))
        if (Get_Cell_Flag(v, UNEVALUATED))
            fail (Error_Block_Conditional_Raw(v));  // !!! Unintended_Literal?
    return true;
}

#define Is_Conditional_False(v) \
    (not Is_Conditional_True(v))


inline static bool Is_Meta_Of_False(Cell(const*) v) {
    return (
        QUOTE_BYTE(v) == QUASI_2
        and HEART_BYTE(v) == REB_WORD
        and VAL_WORD_ID(v) == SYM_FALSE
    );
}

#define Init_Heavy_False(out) \
    Init_Pack((out), PG_1_Meta_False_Array)

inline static bool Is_Heavy_False(Cell(const*) v) {
    if (not Is_Pack(v))
        return false;
    Cell(const*) tail;
    Cell(const*) at = VAL_ARRAY_AT(&tail, v);
    return (tail == at + 1) and Is_Meta_Of_False(at);
}
