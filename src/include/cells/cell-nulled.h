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
//    == \~null~\  ; antiform
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
//    == \~,~\  ; antiform (ghost!) "void"
//
//    >> append [a b] opt find [c d] 'e
//    == \~null~\  ; antiform
//
//    >> append [a b] ? find [c d] 'e
//    == \~null~\  ; antiform
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. In the libRebol API, a nulled RebolValue* actually uses C's concept of
//    a null pointer to represent the optional state.  By promising this
//    is the case, clients of the API can write `if (value)` or `if (!value)`
//    as tests for the null state...with no need to release cell handles
//    for nulls.  Hence there is no `isRebolNull()` API.
//
//    HOWEVER: The definition which must be used is `nullptr` and not C's
//    macro for NULL, because NULL may just be defined as just the integer 0.
//    This can confuse variadics which won't treat NULL as a pointer.
//


//=//// CELL_FLAG_KEYWORD_IS_NULL /////////////////////////////////////////=//
//
// Null testing is done often enough that it's worth being able to do that
// test only via masking the header bits of a cell, without having to look
// at the underlying Symbol ID to confirm its SYM_NULL.
//
// This creates a bit of potential for mistakes when constructing a NULL cell,
// as you can't just change the LIFT_BYTE() of a WORD!.  But this is true of
// all antiforms--this is why producing quasiforms and antiforms are
// deliberately forced through chokepoints like Coerce_To_Antiform().
//

#define CELL_FLAG_KEYWORD_IS_NULL  CELL_FLAG_TYPE_SPECIFIC_A


//=//// LIGHT NULLS (WORD! antiform of NULL) //////////////////////////////=//
//
// 1. Beyond having a special header-only test for nulls, we can also do it
//    without even an inline function call.  (Debug builds tend not to inline
//    functions aggressively.)  However, the Ensure_Readable() adds overhead
//    in many checked builds...all the more reasons to not add another call!
//
// 2. To avoid confusing the test for whether cell contents are the null
//    representation with the test for if a pointer itself is C's NULL, it is
//    called "Is_Nulled()" instead of "Is_Null()".
//

#define Is_Light_Null(v) /* test allowed on potentially unstable values */ \
    ((Ensure_Readable(known(const Value*, (v)))->header.bits & ( \
        CELL_MASK_HEART_AND_SIGIL_AND_LIFT | CELL_FLAG_KEYWORD_IS_NULL \
    )) == ( \
        FLAG_LIFT_BYTE(ANTIFORM_1) | FLAG_HEART(TYPE_WORD) \
            | CELL_FLAG_KEYWORD_IS_NULL))

#define Is_Nulled(v) /* test for stable values, name avoids confusion [2] */ \
    ((Ensure_Readable(known(Exact(const Stable*), (v)))->header.bits & ( \
        CELL_MASK_HEART_AND_SIGIL_AND_LIFT | CELL_FLAG_KEYWORD_IS_NULL \
    )) == ( \
        FLAG_LIFT_BYTE(ANTIFORM_1) | FLAG_HEART(TYPE_WORD) \
            | CELL_FLAG_KEYWORD_IS_NULL))

#define Init_Nulled(out) /* name helps avoid confusion [B] */ \
    TRACK(Init_Word_Untracked( \
        (out), \
        FLAG_LIFT_BYTE(ANTIFORM_1) | CELL_FLAG_KEYWORD_IS_NULL,  \
        CANON(NULL)))


INLINE bool Is_Lifted_Null(const Value* v) {
    if (LIFT_BYTE(v) != QUASIFORM_3)
        return false;
    if (Heart_Of(v) != TYPE_WORD)
        return false;
    return Word_Id(v) == SYM_NULL;
}

#define Init_Lifted_Null(out) \
    Init_Quasi_Word((out), CANON(NULL))


#define Init_Quasi_Null(out) \
    Init_Lifted_Null(out)

#define Is_Quasi_Null(v) \
    Is_Lifted_Null(known(Stable*, (v)))  // subtle distinction of question...


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
//     >> x: ~(~null~)~
//     == \~(~null~)~\  ; antiform (pack!) "heavy null"
//
//     >> x
//     == \~null~\  ; antiform
//
//     >> if ok [null]
//     == \~(~null~)~\  ; antiform (pack!) "heavy null"
//
//     >> if ok [null] else [print "This won't run"]
//     == \~(~null~)~\  ; antiform (pack!) "heavy null"
//

#define Init_Heavy_Null_Untracked(out) \
    Init_Pack_Untracked((out), g_1_quasi_null_array)

#define Init_Heavy_Null(out) \
    Init_Pack((out), g_1_quasi_null_array)

INLINE bool Is_Heavy_Null(const Value* v) {
    if (not Is_Pack(v))
        return false;
    const Element* tail;
    const Element* at = List_At(&tail, v);
    return (tail == at + 1) and Is_Lifted_Null(at);
}

INLINE bool Is_Lifted_Heavy_Null(const Stable* v) {
    if (not Is_Lifted_Pack(v))
        return false;
    const Element* tail;
    const Element* at = List_At(&tail, v);
    return (tail == at + 1) and Is_Lifted_Null(at);
}

INLINE void Force_Cell_Heavy(Value* v) {
    if (Is_Light_Null(v))
        Init_Heavy_Null(v);
    else if (Is_Ghost(v))
        Init_Heavy_Void(v);
}
