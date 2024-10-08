//
//  File: %cell-logic.h
//  Summary: "LOGIC! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Despite Rebol's C heritage, the INTEGER! 0 is purposefully not "falsey".
//


//=//// CANON CONDITIONAL LOGIC [~NULL~ ~OKAY~] ///////////////////////////=//
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

INLINE bool Is_Logic(Need(const Value*) v) {
    Assert_Cell_Readable(v);
    if (QUOTE_BYTE(v) != ANTIFORM_0 or HEART_BYTE(v) != REB_WORD)
        return false;
    Option(SymId) id = Cell_Word_Id(v);
    return id == SYM_NULL or id == SYM_OKAY;
}

#define Is_Okay(v) \
    Is_Anti_Word_With_Id((v), SYM_OKAY)

#define Init_Okay(out) \
    Init_Anti_Word((out), Canon(OKAY))

INLINE Value* Init_Logic(Sink(Value) out, bool flag) {
    return Init_Anti_Word(out, flag ? Canon(OKAY) : Canon(NULL));
}

INLINE bool Cell_Logic(Need(const Value*) v) {
    assert(Is_Antiform(v));
    assert(HEART_BYTE(v) == REB_WORD);
    SymId id = unwrap Cell_Word_Id(v);
    assert(id == SYM_NULL or id == SYM_OKAY);
    return id == SYM_OKAY;
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
//      == ~void~  ; anti
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

#define Init_True(out)      Init_Word((out), Canon(TRUE))
#define Init_False(out)     Init_Word((out), Canon(FALSE))

#define Is_True(out)        Is_Word_With_Id((out), SYM_TRUE)
#define Is_False(out)       Is_Word_With_Id((out), SYM_FALSE)

INLINE bool Is_Boolean(Need(const Value*) v) {
    Assert_Cell_Readable(v);

    if (QUOTE_BYTE(v) != NOQUOTE_1 or HEART_BYTE(v) != REB_WORD)
        return false;

    Option(SymId) id = Cell_Word_Id(v);
    return id == SYM_TRUE or id == SYM_FALSE;
}

#define Init_Boolean(out,flag) \
    Init_Word((out), (flag) ? Canon(TRUE) : Canon(FALSE))

INLINE bool Cell_True(Need(const Value*) v) {  // corresponds to TRUE?
    assert(Is_Word(v));
    Option(SymId) id = Cell_Word_Id(v);
    if (id == SYM_TRUE)
        return true;
    if (id != SYM_FALSE)
        assert(!"Attempt to test Cell_True() on non-[true false]");
    return false;
}

#define Cell_False(v) (not Cell_True(v))


//=//// [ON OFF] WORDS ////////////////////////////////////////////////////=//

INLINE bool Is_OnOff(Need(const Value*) v) {
    Assert_Cell_Readable(v);
    if (QUOTE_BYTE(v) != NOQUOTE_1 or HEART_BYTE(v) != REB_WORD)
        return false;
    Option(SymId) id = Cell_Word_Id(v);
    return id == SYM_ON or id == SYM_OFF;
}

#define Init_OnOff(out,flag) \
    Init_Word((out), (flag) ? Canon(ON) : Canon(OFF))

INLINE bool Cell_On(Need(const Value*) v) {  // corresponds to ON?
    assert(Is_Word(v));
    Option(SymId) id = Cell_Word_Id(v);
    if (id == SYM_ON)
        return true;
    if (id != SYM_OFF)
        assert(!"Attempt to test Cell_On() on non-[on off]");
    return false;
}

#define Cell_Off(v) (not Cell_On(v))


//=//// [YES NO] WORDS ////////////////////////////////////////////////////=//

INLINE bool Is_YesNo(Need(const Value*) v) {
    Assert_Cell_Readable(v);
    if (QUOTE_BYTE(v) != NOQUOTE_1 or HEART_BYTE(v) != REB_WORD)
        return false;
    Option(SymId) id = Cell_Word_Id(v);
    return id == SYM_YES or id == SYM_NO;
}

#define Init_YesNo(out,flag) \
    Init_Word((out), (flag) ? Canon(YES) : Canon(NO))

INLINE bool Cell_Yes(Need(const Value*) v) {  // corresponds to YES?
    assert(Is_Word(v));
    Option(SymId) id = Cell_Word_Id(v);
    if (id == SYM_YES)
        return true;
    if (id != SYM_NO)
        assert(!"Attempt to test Cell_Yes() on non-[yes no]");
    return false;
}

#define Cell_No(v) (not Cell_Yes(v))


//=//// BRANCH TRIGGERING & BRANCH INHIBITING /////////////////////////////=//
//
// At time of writing, ~null~ antiforms are the only branch inhibitors.
//
// 1. ~void~ antiforms are neither triggering nor inhibiting: since voids opt
//    out of aggregate logic operations, an isolated operation like IF cannot
//    consider void to be either true or false.
//
// 2. Because a branch evaluation can produce NULL or VOID, we would not be
//    able from the outside to discern a taken branch from a non-taken one
//    in order to implement constructs like ELSE and THEN.  For this reason,
//    branching constructs "box" NULLs and VOIDs into antiform blocks, to
//    make them a multi-return parameter "pack".  Because these decay back
//    to plain VOID and NULL in most contexts, this will give the right
//    behavior most of the time...while being distinct enough that ELSE and
//    THEN can react to them as signals the branch was taken.
//

INLINE bool Is_Trigger(const Value* v) {
    Assert_Cell_Readable(v);

    if (QUOTE_BYTE(v) == ANTIFORM_0) {
        if (HEART_BYTE(v) != REB_WORD)
            return true;  // !!! are all non-word antiforms truthy?
        Option(SymId) id = Cell_Word_Id(v);
        if (id == SYM_NULL)
            return false;
        if (id == SYM_OKAY)
            return true;
        if (id == SYM_VOID)
            fail (Error_Bad_Void());  // void is not trigger or inhibitor [1]
        fail (Error_Bad_Antiform(v));  // !!! special error?
    }

  #if DEBUG
    if (Is_Word(v)) {  // temporary for logic-WORD! transition
        Option(SymId) id = Cell_Word_Id(v);
        if (id == SYM_TRUE)
            fail ("Warning: TRUE used as branch trigger");
        if (id == SYM_FALSE)
            fail ("Warning: FALSE used as branch trigger");
        if (id == SYM_YES)
            fail ("Warning: YES used as branch trigger");
        if (id == SYM_NO)
            fail ("Warning: NO used as branch trigger");
        if (id == SYM_ON)
            fail ("Warning: ON used as branch trigger");
        if (id == SYM_OFF)
            fail ("Warning: OFF used as branch trigger");
    }
  #endif

    return true;  // all non-antiform values are truthy
}

#define Is_Inhibitor(v) \
    (not Is_Trigger(v))

INLINE Atom* Packify_If_Inhibitor(Atom* v) {
    if (Is_Nulled(v))
        Init_Heavy_Null(v);
    return v;
}

INLINE Bounce Native_Branched_Result(Level* level_, Atom* v) {
    assert(v == level_->out);  // would not be zero cost if we supported copy
    if (Is_Stable(v)) {
        if (Is_Void(v))
            Init_Heavy_Void(v);  // box up for THEN reactivity [2]
        else if (Is_Nulled(v))
            Init_Heavy_Null(v);  // box up for THEN reactivity [2]
    }
    return level_->out;
}
