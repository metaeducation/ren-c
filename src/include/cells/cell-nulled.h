//
//  file: %cell-nulled.h
//  summary: "NULL definitions"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// Null is used as a signal for "soft failure", e.g. (find [c d] 'e) is null.
// It is treated as a "branch inhibitor" by control constructs like IF.
//
// The representation for nulls is the antiform of the WORD! "null":
//
//    >> find [c d] 'e
//    == ~null~  ; anti
//
// This choice conveniently fits with the rule that nulls should not be able
// to be stored in blocks (as no antiforms can be).  Greater safety comes from
// catching potential mistakes with this property:
//
//    >> append [a b] find [c d] 'e
//    ** Error: Cannot put ~null~ antiforms in blocks
//
// If a no-op is desired in this situation, OPT can be used to convert the
// null to a void, with ? as a shorthand:
//
//    >> opt find [c d] 'e
//    == ~[]~  ; anti
//
//    >> append [a b] opt find [c d] 'e
//    == [a b]
//
//    >> append [a b] ? find [c d] 'e
//    == [a b]
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * In the libRebol API, a nulled RebolValue* actually uses C's concept of
//   a null pointer to represent the optional state.  By promising this
//   is the case, clients of the API can write `if (value)` or `if (!value)`
//   as tests for the null state...with no need to release cell handles
//   for nulls.  Hence there is no `isRebolNull()` API.
//
//   HOWEVER: The definition which must be used is `nullptr` and not C's
//   macro for NULL, because NULL may just be defined as just the integer 0.
//   This can confuse variadics which won't treat NULL as a pointer.
//
// * To avoid confusing the test for whether cell contents are the null
//   representation with the test for if a pointer itself is C's NULL, it is
//   called "Is_Nulled()" instead of "Is_Null()".
//
// * We ensure that non-quoted, non-quasi NULL isn't written into an Element*
//   e.g. for a BLOCK!... must be a Value*, e.g. a context variable or
//   frame output.
//

INLINE bool Is_Light_Null(Need(const Atom*) a) {
    Assert_Cell_Readable(a);
    return LIFT_BYTE(a) == ANTIFORM_1
        and Heart_Of(a) == TYPE_WORD
        and Word_Id(a) == SYM_NULL;
}

INLINE bool Is_Nulled(const Value* v) {
    Assert_Cell_Readable(v);
    return LIFT_BYTE(v) == ANTIFORM_1
        and Heart_Of(v) == TYPE_WORD
        and Word_Id(v) == SYM_NULL;
}

#define Init_Nulled(out) \
    TRACK(Init_Word_Untracked( \
        (out), \
        ANTIFORM_1,  /* NULL is valid keyword symbol */ \
        CANON(NULL)))

#define Init_Quasi_Null(out) \
    Init_Quasi_Word((out), CANON(NULL))

INLINE bool Is_Quasi_Null(const Cell* v) {
    if (not Is_Quasiform(v))
        return false;
    if (Heart_Of(v) != TYPE_WORD)
        return false;
    return Word_Id(v) == SYM_NULL;
}

#define Init_Lifted_Null(out) \
    Init_Quasi_Null(out)

#define Is_Lifted_Null(v) \
    Is_Quasi_Null(v)

#define Is_Undone_Opt_Nulled(v)  Is_Nulled(v)


//=//// "HEAVY NULLS" (BLOCK! Antiform Pack with `~null~` in it) //////////=//
//
// Because a branch evaluation can produce NULL, we would not be able from
// the outside to discern a taken branch from a non-taken one in order to
// implement constructs like ELSE and THEN:
//
//     >> if ok [null] else [print "If passthru null, we get this :-("]
//     If passthru null, we get this :-(  ; <-- BAD!
//
// For this reason, branching constructs "box" NULLs to antiform blocks, as a
// parameter "pack".  Since these decay back to plain NULL in *most* contexts,
// this gives the right behavior *most* of the time...while being distinct
// enough that ELSE & THEN can react to them as signals the branch was taken.
//
//     >> x: ~[~null~]~
//     == ~null~  ; anti
//
//     >> if ok [null]
//     == ~[~null~]~  ; anti (heavy null)
//
//     >> if ok [null] else [print "This won't run"]
//     == ~[~null~]~  ; anti (heavy null)
//

#define Init_Heavy_Null_Untracked(out) \
    Init_Pack_Untracked((out), g_1_quasi_null_array)

#define Init_Heavy_Null(out) \
    Init_Pack((out), g_1_quasi_null_array)

INLINE bool Is_Heavy_Null(const Atom* v) {
    if (not Is_Pack(v))
        return false;
    const Element* tail;
    const Element* at = List_At(&tail, v);
    return (tail == at + 1) and Is_Lifted_Null(at);
}

INLINE bool Is_Lifted_Heavy_Null(const Value* v) {
    if (not Is_Lifted_Pack(v))
        return false;
    const Element* tail;
    const Element* at = List_At(&tail, v);
    return (tail == at + 1) and Is_Lifted_Null(at);
}
