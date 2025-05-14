//
//  file: %cell-logic.h
//  summary: "LOGIC! Datatype Header"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    if (QUOTE_BYTE(v) != ANTIFORM_0 or Heart_Of(v) != TYPE_WORD)
        return false;
    Option(SymId) id = Cell_Word_Id(v);
    return id == SYM_NULL or id == SYM_OKAY;
}

#define Is_Okay(v) \
    Is_Anti_Word_With_Id((v), SYM_OKAY)

#define Init_Okay(out) \
    TRACK(Init_Word_Untracked( \
        (out), \
        ANTIFORM_0_COERCE_ONLY,  /* OKAY is valid keyword symbol */ \
        CANON(OKAY)))

INLINE Value* Init_Logic_Untracked(Init(Value) out, bool flag) {
    return Init_Word_Untracked(
        out,
        ANTIFORM_0_COERCE_ONLY,  // OKAY and NULL are valid keyword symbols
        flag ? CANON(OKAY) : CANON(NULL)
    );
}

#define Init_Logic(out,flag) \
    TRACK(Init_Logic_Untracked((out), (flag)))

INLINE bool Cell_Logic(Need(const Value*) v) {
    assert(Is_Antiform(v));
    assert(Heart_Of(v) == TYPE_WORD);
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

INLINE bool Is_Boolean(const Value* v) {
    Assert_Cell_Readable(v);

    if (QUOTE_BYTE(v) != NOQUOTE_1 or Heart_Of(v) != TYPE_WORD)
        return false;

    Option(SymId) id = Cell_Word_Id(v);
    return id == SYM_TRUE or id == SYM_FALSE;
}

#define Init_Boolean(out,flag) \
    Init_Word((out), (flag) ? CANON(TRUE) : CANON(FALSE))

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

INLINE bool Is_OnOff(const Value* v) {
    Assert_Cell_Readable(v);
    if (QUOTE_BYTE(v) != NOQUOTE_1 or Heart_Of(v) != TYPE_WORD)
        return false;
    Option(SymId) id = Cell_Word_Id(v);
    return id == SYM_ON or id == SYM_OFF;
}

#define Init_OnOff(out,flag) \
    Init_Word((out), (flag) ? CANON(ON) : CANON(OFF))

INLINE bool Cell_On(const Value* v) {  // corresponds to ON?
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

INLINE bool Is_YesNo(const Value* v) {
    Assert_Cell_Readable(v);
    if (QUOTE_BYTE(v) != NOQUOTE_1 or Heart_Of(v) != TYPE_WORD)
        return false;
    Option(SymId) id = Cell_Word_Id(v);
    return id == SYM_YES or id == SYM_NO;
}

#define Init_YesNo(out,flag) \
    Init_Word((out), (flag) ? CANON(YES) : CANON(NO))

INLINE bool Cell_Yes(const Value* v) {  // corresponds to YES?
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
// 1. VOID antiforms are neither triggering nor inhibiting: since voids opt
//    out of aggregate logic operations, an isolated operation like IF cannot
//    consider void to be either true or false.  Type checking helps enforce
//    this rule, since unstable values cannot be passed as a condition to
//    the test functions.
//
//    It would be possible to say that VOIDs were true, and that would
//    produce some potentially interesting use cases like (any [expr, void])
//    being able to evaluate to void if expr1 was falsey or opted out.  Yet
//    semantically, we want to think of the truthiness of a PACK! as being
//    directly tied to its first element...and voids have no element there
//    to be tested, and should not decay to assign a normal variable.  So it's
//    not particularly coherent to try and argue voids are true or false,
//    and creates ambiguity to gain a relatively unimportant feature.
//
// 2. Because a branch evaluation can produce NULL, we would not be able from
//    the outside to discern a taken branch from a non-taken one in order to
//    implement constructs like ELSE and THEN:
//
//        >> if ok [null] else [print "If passthru null, we get this :-("]
//        If passthru null, we get this :-(  ; <-- BAD!
//
//    For this reason, branching constructs "box" NULLs to antiform blocks,
//    as a parameter "pack".  Since these decay back to plain NULL in *most*
//    contexts, this gives the right behavior *most* of the time...while being
//    distinct enough that ELSE and THEN can react to them as signals the
//    branch was taken.
//
INLINE bool Is_Trigger(const Value* v) {  // stable only, can't test void [1]
    Assert_Cell_Readable(v);

    if (QUOTE_BYTE(v) != ANTIFORM_0)
        return true;  // all non-antiforms (including quasi/quoted) are truthy

    if (Heart_Of(v) != TYPE_WORD)
        return true;  // !!! all stable non-word antiforms are truthy

    Option(SymId) id = Cell_Word_Id(v);
    if (id == SYM_NULL)
        return false;
    if (id == SYM_OKAY)
        return true;
    panic (Error_Bad_Antiform(v));  // !!! special warning?
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
    if (Is_Nulled(v))
        Init_Heavy_Null(v);  // box up for THEN reactivity [2]
    return level_->out;
}

// It's necessary to be able to tell from the outside of a loop whether it
// had a BREAK or not, e.g.
//
//   flag: 'true
//   while [true? flag] [flag: 'false, null]
//
// We don't want that to evaluate to NULL--because NULL is reserved for a
// break signal.  So we make a ~[~null~]~ "heavy null" antiform PACK!.
//
// Also, returning VOID is reserved for if-and-only-if the loop never ran.
// That's crucial for implementing loop compositions that give correct result
// values.  For instance, we want these loops to have parity:
//
//     >> for-both 'x [1 2 3 4] [] [x * 10]
//     == 40
//
//     >> for-each 'x [1 2 3 4] [x * 10]
//     == 40
//
// If FOR-BOTH is implemented in terms of two FOR-EACH loops, then we want to
// know the second FOR-EACH loop never produced a result (without having to
// look at the input and hinge on the semantics of the loop).  But if VOID
// is this signal, we have to worry about:
//
//     >> for-both 'x [1 2] [3 4] [if x = 4 [void]]
//     == 20  ; if second FOR-EACH gave VOID, and we assumed "never ran"
//
// So instead, TRASH is produced for VOID if the body ever ran.  This can be
// worked around with meta-result protocols if it's truly needed.
//
INLINE Bounce Native_Looped_Result(Level* level_, Atom* atom) {
    assert(atom == level_->out);  // wouldn't be zero cost if we supported copy
    if (Is_Nulled(atom))
        Init_Heavy_Null_Untracked(atom);  // distinguish from null for BREAK
    else if (Is_Void(atom))
        Init_Trash_Untracked(atom);  // distinguish from loop that never ran
    return level_->out;
}
