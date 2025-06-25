//
//  file: %cell-logic.h
//  summary: "LOGIC! Datatype Header"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
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
    if (LIFT_BYTE(v) != ANTIFORM_1 or Heart_Of(v) != TYPE_WORD)
        return false;
    Option(SymId) id = Cell_Word_Id(v);
    return id == SYM_NULL or id == SYM_OKAY;
}

#define Is_Okay(v) \
    Is_Anti_Word_With_Id((v), SYM_OKAY)

INLINE bool Is_Possibly_Unstable_Atom_Okay(Atom* atom) {  // typecheck only!
    if (not Is_Possibly_Unstable_Atom_Keyword(atom))
        return false;
    return Cell_Word_Id(atom) == SYM_OKAY;
}

#define Init_Okay(out) \
    TRACK(Init_Word_Untracked( \
        (out), \
        ANTIFORM_1,  /* OKAY is valid keyword symbol */ \
        CANON(OKAY)))

INLINE Value* Init_Logic_Untracked(Init(Value) out, bool flag) {
    return Init_Word_Untracked(
        out,
        ANTIFORM_1,  // OKAY and NULL are valid keyword symbols
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

    if (LIFT_BYTE(v) != NOQUOTE_2 or Heart_Of(v) != TYPE_WORD)
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
    if (LIFT_BYTE(v) != NOQUOTE_2 or Heart_Of(v) != TYPE_WORD)
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
    if (LIFT_BYTE(v) != NOQUOTE_2 or Heart_Of(v) != TYPE_WORD)
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


//=//// CONDITIONAL "TRUTHINESS" and "FALSEYNESS" /////////////////////////=//
//
// The default behavior of the system is to consider there being only one
// conditionally false value: the ~null~ antiform.
//
// This is slated to be extensible, so that contexts can provide a different
// definition of "truthiness" and "falseyness" via the COND(ITIONAL) function.
// That hasn't happened yet, so at time of writing, ~null~ antiforms are the
// only conditionally false state.
//
// 1. VOID antiforms are neither "truthy" nor "falsey": since voids opt
//    out of aggregate logic operations, an isolated operation like IF cannot
//    consider void to be either true or false.  Type checking helps enforce
//    this rule, since unstable values cannot be passed as a condition to
//    the test functions.
//
//    It would be possible to say that VOIDs were truthy, and that would
//    produce some potentially interesting use cases like (any [expr, void])
//    being able to evaluate to void if expr1 was falsey or opted out.  Yet
//    semantically, we want to think of the truthiness of a PACK! as being
//    directly tied to its first element...and voids have no element there
//    to be tested, and should not decay to assign a normal variable.  So it's
//    not particularly coherent to try and argue voids are true or false,
//    and creates ambiguity to gain a relatively unimportant feature.
//
//    !!! Should this enforce Value* passed, and disallow Element*, since
//    the builtin conditional never considers elements to be falsey?
//
// 2. There used to be a ~void~ antiform as "stable void", but the role has
//    been overtaken by the ~()~ empty splice antiform ("BLANK").  So now
//    ~okay~ and ~null~ are the only two KEYWORD!s (antiform WORD!s).  There
//    is some question on what behavior is wanted from ~NaN~... would it be
//    falsey?  Not known since it's not in use yet.  But generally right now
//    it looks like ~null~ and ~okay~ the only things to consider, and if
//    anything else is tested it errors.

INLINE Option(Error*) Trap_Test_Conditional(
    Sink(bool) cond,
    const Value* v  // Not Atom*, has to be stable... no VOID [1]
){
    Assert_Cell_Readable(v);

    if (LIFT_BYTE(v) != ANTIFORM_1) {
        *cond = true;  // all non-antiforms (including quasi/quoted) are truthy
        return SUCCESS;
    }

    if (Heart_Of(v) == TYPE_RUNE) {  // trash--not legal to test conditionally
      #if APPEASE_WEAK_STATIC_ANALYSIS
        *cond = false;
      #endif
        return Error_Trash_Condition_Raw(v);
    }

    if (Heart_Of(v) != TYPE_WORD) {
        *cond = true;  // !!! all stable non-word antiforms are truthy
        return SUCCESS;
    }

    Option(SymId) id = Cell_Word_Id(v);
    if (id == SYM_NULL) {
        *cond = false;  // ~null~ antiform is the only falsey value
        return SUCCESS;
    }
    if (id == SYM_OKAY) {
        *cond = true;  // ~okay~ antiform is the only truthy keyword
        return SUCCESS;
    }

  #if APPEASE_WEAK_STATIC_ANALYSIS
    *cond = false;
  #endif
    return Error_Keyword_Condition_Raw(v);  // none exist yet, review [2]
}
