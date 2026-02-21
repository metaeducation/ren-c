//
//  file: %cell-logic.h
//  summary: "LOGIC! Datatype Header"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2026 Ren-C Open Source Contributors
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
// seen as conditionally "truthy"), it was an uphill battle to figure out
// a representation for logic literals.  R3-Alpha used #[true] and #[false]
// but often molded them as looking like the words true and false anyway.
//
// Ren-C's answer is a concept known as "Flexible Logic".  The concept is
// that WORD!s are used to represent boolean states like TRUE, FALSE, YES, NO
// ON, OFF, etc.  When tested by something like an IF, those will all trigger
// the branch to be taken--so it is expected that you use an appropriate
// test... such as TRUE? (which verifies that the argument is either the TRUE
// or FALSE word, and returns the ~null~ antiform if it's not TRUE).  Then,
// ~null~ antiforms are chosen as the only "branch inhibitor".
//
// The belief is that once people have internalized that conditionals like IF
// test for NULL (and NULL only), they will habitually use the correct
// patterns and be able to leverage null as the "not set to anything, not even
// the TRUE or FALSE state".
//
//   https://forum.rebol.info/t/flexible-logic-system-terminology/2252
//
// Null has another application, as a signal for "soft failure", for instance
// (find [c d] 'e) is null.  It is treated as a "branch inhibitor" by control
// constructs like IF.
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
// If a no-op is desired in this situation, OPT can "opt in with nothing" by
// converting the null to a VOID!:
//
//    >> opt find [c d] 'e
//    == \~\  ; antiform (void!)
//
//    >> append [a b] opt find [c d] 'e
//    == [a b]
//
// COND can convert the NULL to a VETO, which "opts out" and makes null:
//
//    >> append [a b] opt find [c d] 'e
//    == \~null~\  ; antiform (logic!)
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. In the libRebol API, a nulled RebolValue* actually uses C's concept of
//    a null pointer to represent the optional state.  By promising this
//    is the case, clients of the API can write `if (value)` or `if (!value)`
//    as tests for the null state...with no need to release cell handles
//    for nulls.  Hence there is no `rebIsNull()` API.
//
//    HOWEVER: The definition which must be used is `nullptr` and not C's
//    macro for NULL, because NULL may just be defined as just the integer 0.
//    This can confuse variadics which won't treat NULL as a pointer.
//
// B. Despite Rebol's C heritage, INTEGER! 0 is purposefully not "falsey".
//


//=//// LOGIC INITIALIZATION AND EXTRACTION //////////////////////////////=//
//
// Null testing is done often enough that it's worth being able to do that
// test only via masking the header bits of a cell, without having to look
// at the underlying Symbol ID to confirm it is SYM_NULL or SYM_OKAY.  So
// we use a dedicated header flag applicable to LOGIC! cells.
//
// 1. This creates a bit of potential for mistakes when constructing LOGIC!
//    cells, as you can't just change the LIFT_BYTE() of a WORD!.  But this
//    is true of all antiforms--this is why producing quasiforms and antiforms
//    are forced through chokepoints like Coerce_To_Antiform().
//

#define CELL_FLAG_LOGIC_IS_OKAY  CELL_FLAG_TYPE_SPECIFIC_A  // [1]

INLINE Stable* Init_Logic_Untracked(Init(Stable) out, bool logic) {
    return Init_Word_Untracked(
        out,
        FLAG_LIFT_BYTE(LIFTBYTE_LOGIC)
            | (logic ? CELL_FLAG_LOGIC_IS_OKAY : 0),
        logic ? CANON(OKAY) : CANON(NULL)
    );
}

#define Init_Logic(out,flag) \
    TRACK(Init_Logic_Untracked((out), (flag)))

#define Init_Null(out) /* name helps avoid confusion [B] */ \
    TRACK(Init_Word_Untracked( \
        Possibly_Antiform(out), \
        FLAG_LIFT_BYTE(LIFTBYTE_LOGIC) | (not CELL_FLAG_LOGIC_IS_OKAY),  \
        CANON(NULL)))

#define Init_Lifted_Null(out) \
    Init_Quasi_Word((out), CANON(NULL))

#define Init_Quasi_Null(out) \
    Init_Quasi_Word(Known_Element(out), CANON(NULL))


#define Init_Okay(out) \
    TRACK(Init_Word_Untracked( \
        Possibly_Antiform(out), \
        FLAG_LIFT_BYTE(LIFTBYTE_LOGIC) \
            | CELL_FLAG_LOGIC_IS_OKAY,  \
        CANON(OKAY)))


//=//// LOGIC EXTRACTION //////////////////////////////////////////////////=//
//
// If you know you have a LOGIC!, all you need to do is test the flag...
//

INLINE bool Cell_Logic_Core(const Stable* v) {
    assert(Is_Logic(v));
    return Get_Cell_Flag(v, LOGIC_IS_OKAY);
}

#define Cell_Logic(v) \
    Cell_Logic_Core(Known_Stable(v))


//=//// LIGHT NULLS (WORD! antiform of NULL) //////////////////////////////=//
//
// 1. Beyond having a special header-only test for nulls, we can also do it
//    without even an inline function call.  (Debug builds tend not to inline
//    functions aggressively.)  However, the Readable_Cell() adds overhead
//    in many checked builds...all the more reasons to not add another call!
//
// 2. If you are uninitiated to the codebase, you might think `Is_Null(v)`
//    was defined as `(v == nullptr)`.  `Is_Nulled()` was used as the name for
//    this macro for many years.  But as antiform WORD! ~null~ cells never
//    make it into API handles and nullptr is how they are experienced...this
//    is a purely internal distinction.  As Gemini points out:
//
//    "The 'tax' of clunky naming is paid every time you read or write code,
//    whereas the 'confusion' of a C developer is a one-time cost of entry to
//    the codebase.  If you're already disciplined about using nullptr for C's
//    null concept, the ambiguity is significantly reduced."
//

#define Is_Light_Null(v) /* test allowed on potentially unstable values */ \
    ((Readable_Cell(Possibly_Unstable(v))->header.bits & ( \
        CELL_MASK_HEART_AND_SIGIL_AND_LIFT | CELL_FLAG_LOGIC_IS_OKAY \
    )) == ( \
        FLAG_LIFT_BYTE(LIFTBYTE_LOGIC) | FLAG_HEART(TYPE_WORD) \
            | (not CELL_FLAG_LOGIC_IS_OKAY)))

#define Is_Null(v) /* test for stable values, don't confuse w/nullptr [2] */ \
    ((Readable_Cell(Possibly_Antiform(v))->header.bits & ( \
        CELL_MASK_HEART_AND_SIGIL_AND_LIFT | CELL_FLAG_LOGIC_IS_OKAY \
    )) == ( \
        FLAG_LIFT_BYTE(LIFTBYTE_LOGIC) | FLAG_HEART(TYPE_WORD) \
            | (not CELL_FLAG_LOGIC_IS_OKAY)))


INLINE bool Is_Lifted_Null(const Value* v) {
    if (LIFT_BYTE(v) != QUASIFORM_64)
        return false;
    if (Heart_Of(v) != TYPE_WORD)
        return false;
    return Word_Id(v) == SYM_NULL;
}

#define Is_Quasi_Null(v) \
    Is_Lifted_Null(Known_Stable(v))  // subtle distinction of question...


//=//// UNREFINED SIGNALING MACROS ////////////////////////////////////////=//
//
// It was wondered for a time if unused refinements should be VOID! instead
// of NULL, as a more "pure" signal of "no value provided".  This was made
// more feasible by the :VAR notation to mean "give NULL if VAR holds VOID!"
//
// That idea was panned for its poor ergonomics.  But trying it out meant
// finding sites where nulls had been initialized for unrefined slots, and
// these macros help find those places if it's of interest later.
//

#define Init_Null_Signifying_Unspecialized(out)  Init_Null(out)
#define Init_Lifted_Null_Signifying_Unspecialized(out)  Init_Lifted_Null(out)
#define Is_Null_Signifying_Unspecialized(v)  Is_Light_Null(v)

#define Init_Null_Signifying_Vetoed(out)  Init_Null(out)


//=//// CANON LOGIC TRUTHY: ~OKAY~ ANTIFORM ///////////////////////////////=//
//
// The ~null~ antiform is the only "branch inhibitor", e.g. the only thing
// that prevents functions like IF from running their branches.  But most
// everything else is considered to be a "branch trigger".
//
// This means it might seem arbitrary to pick what something like (10 < 20)
// would return...since anything (the word! TRUE, the word! FALSE, the tag!
// <banana>) would run a branch.  But there's special value in choosing
// an antiform as NULL's complement.  So the ~okay~ antiform is used:
//
//    >> 10 > 20
//    == ~null~  ; anti
//
//    >> 10 < 20
//    == ~okay~  ; anti
//
// This has the advantage of not having one result of conditionals be unable
// to be put in blocks, while the other could be--as well as potentially
// conflate with dialected meanings.
//

#define Is_Possibly_Unstable_Value_Okay(v) \
    ((Readable_Cell(Possibly_Unstable(v))->header.bits & ( \
        CELL_MASK_HEART_AND_SIGIL_AND_LIFT | CELL_FLAG_LOGIC_IS_OKAY \
    )) == ( \
        FLAG_LIFT_BYTE(LIFTBYTE_LOGIC) \
            | FLAG_HEART(HEART_WORD_SIGNIFYING_LOGIC) \
            | CELL_FLAG_LOGIC_IS_OKAY))

#define Is_Okay(v) \
    ((Readable_Cell(Possibly_Antiform(v))->header.bits & ( \
        CELL_MASK_HEART_AND_SIGIL_AND_LIFT | CELL_FLAG_LOGIC_IS_OKAY \
    )) == ( \
        FLAG_LIFT_BYTE(LIFTBYTE_LOGIC) \
            | FLAG_HEART(HEART_WORD_SIGNIFYING_LOGIC) \
            | CELL_FLAG_LOGIC_IS_OKAY))


//=//// CONDITIONAL "TRUTHINESS" and "FALSEYNESS" /////////////////////////=//
//
// The default behavior of the system is to consider there being only one
// conditionally false value: the ~null~ antiform.
//
// (Some question has arisen about the extensibility of this, if there could
// be a conditional function put in scopes that influences the test, similar
// to how RebindableSyntax works.)
//
// Since things like TRASH!, VOID!, and empty PACK! are all unstable, we
// don't have to worry about testing a stable value causing an error.  It's
// legal on all stable states.
//
#define Logical_Test(v) \
    (not Is_Null(Known_Stable(v)))


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
    Init_Pack_Untracked((out), Cell_Array(LIB(HEAVY_NULL)))

#define Init_Heavy_Null(out) \
    Init_Pack((out), Cell_Array(LIB(HEAVY_NULL)))

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

INLINE Value* Force_Cell_Heavy(Value* v) {
    if (Is_Light_Null(v))
        Init_Heavy_Null(v);
    else if (Is_Void(v))
        Init_Heavy_Void(v);
    return v;
}


//=//// BOOLEAN WORDS [TRUE FALSE] ////////////////////////////////////////=//
//
// In the "Flexible Logic" paradigm, booleans are just WORD!s.  Hence both
// the FALSE and TRUE words will trigger branches.
//
//     >> flag: true
//     ** Error: true is unset (~ antiform)
//
//     >> flag: 'true
//
//     >> if flag [print "TRUE word runs branch"]
//     TRUE word runs branch
//
//     >> flag: false
//     ** Error: false is unset (~ antiform)
//
//     >> flag: 'false
//
//     >> if flag [print "FALSE word runs branch"]
//     FALSE word runs branch
//
// This means that to work in this model, one has to internalize the idea
// that IF is only testing for non-nullity...e.g. a variable to which neither
// TRUE nor FALSE have been assigned.  To make the tests useful, you have
// to convert the boolean to conditional logic first.
//
//      >> flag: 'false
//
//      >> true? flag
//      == ~null~  ; anti
//
//      >> if true? flag [print "IF TRUE? on FALSE skips branch"]
//      == ~null~  ; anti
//
//      >> false? flag
//      == ~okay~  ; anti
//
//      >> if false? flag [print "IF FALSE? on FALSE runs branch"]
//      IF FALSE? on FALSE runs branch
//
// Generally speaking, the core tries to remain agnostic and only deal in
// the currency of ~okay~ and ~null~, letting users pick the representations
// of logic that make the most sense for what they are writing.  (HostileFork
// finds that YES and NO are preferable for most cases over TRUE and FALSE,
// once given the freedom to choose.)
//

#define Init_True(out)      Init_Word((out), CANON(TRUE))
#define Init_False(out)     Init_Word((out), CANON(FALSE))

#define Is_True(out)        Is_Word_With_Id((out), SYM_TRUE)
#define Is_False(out)       Is_Word_With_Id((out), SYM_FALSE)

INLINE bool Is_Boolean(const Stable* v) {
    Assert_Cell_Readable(v);

    if (LIFT_BYTE(v) != As_Lift(TYPE_WORD))
        return false;

    Option(SymId) id = Word_Id(v);
    return id == SYM_TRUE or id == SYM_FALSE;
}

#define Init_Boolean(out,flag) \
    Init_Word((out), (flag) ? CANON(TRUE) : CANON(FALSE))

INLINE bool Cell_True(const Stable* v) {  // corresponds to TRUE?
    assert(Is_Word(v));
    Option(SymId) id = Word_Id(v);
    if (id == SYM_TRUE)
        return true;
    if (id != SYM_FALSE)
        assert(!"Attempt to test Cell_True() on non-[true false]");
    return false;
}

#define Cell_False(v) (not Cell_True(v))


//=//// [ON OFF] WORDS ////////////////////////////////////////////////////=//

INLINE bool Is_OnOff(const Stable* v) {
    Assert_Cell_Readable(v);
    if (LIFT_BYTE(v) != As_Lift(TYPE_WORD))
        return false;
    Option(SymId) id = Word_Id(v);
    return id == SYM_ON or id == SYM_OFF;
}

#define Init_OnOff(out,flag) \
    Init_Word((out), (flag) ? CANON(ON) : CANON(OFF))

INLINE bool Cell_On(const Stable* v) {  // corresponds to ON?
    assert(Is_Word(v));
    Option(SymId) id = Word_Id(v);
    if (id == SYM_ON)
        return true;
    if (id != SYM_OFF)
        assert(!"Attempt to test Cell_On() on non-[on off]");
    return false;
}

#define Cell_Off(v) (not Cell_On(v))


//=//// [YES NO] WORDS ////////////////////////////////////////////////////=//

INLINE bool Is_YesNo(const Stable* v) {
    Assert_Cell_Readable(v);
    if (LIFT_BYTE(v) != As_Lift(TYPE_WORD))
        return false;
    Option(SymId) id = Word_Id(v);
    return id == SYM_YES or id == SYM_NO;
}

#define Init_YesNo(out,flag) \
    Init_Word((out), (flag) ? CANON(YES) : CANON(NO))

INLINE bool Cell_Yes(const Stable* v) {  // corresponds to YES?
    assert(Is_Word(v));
    Option(SymId) id = Word_Id(v);
    if (id == SYM_YES)
        return true;
    if (id != SYM_NO)
        assert(!"Attempt to test Cell_Yes() on non-[yes no]");
    return false;
}

#define Cell_No(v) (not Cell_Yes(v))
