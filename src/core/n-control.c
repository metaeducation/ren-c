//
//  File: %n-control.c
//  Summary: "native functions for control flow"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// Control constructs follow these rules:
//
// * If they do not run any branches, the construct will "return void".  This
//   will signal functions like ELSE and DIDN'T, much like the NULL state
//   conveying soft failure does.
//
//   (Note: return_void() doesn't actually overwrite the contents of the
//    output cell.  This makes it possible for functions like ALL to skip
//    over void results and let the previous evaluation "drop out".
//    See %sys-void.h for more information about this mechanic.)
//
// * If a branch *does* run--and its evaluation happens to produce NULL--then
//   conditionals designed to be used with branching (like IF or CASE) will
//   return a special "isotope" form of the BAD-WORD! of ~null~.  It's called
//   an "isotope" because it will "decay" into a regular NULL when assigned
//   to a variable.  But so long as it doesn't decay, then constructs like
//   ELSE and THEN consider it distinct from NULL, and hence a signal that a
//   branch actually ran.
//
//   (See %sys-bad-word.h for more details about isotopes.)
//
// * Zero-arity function values used as branches will be executed, and
//   single-arity functions used as branches will also be executed--but passed
//   the value of the triggering condition.  Isotopes of NULL, FALSE, and
//   BLANK are decayed before being passed to the function, unless the argument
//   is taken as a ^META parameter.
//
//   (See Do_Branch_Throws() for supported ANY-BRANCH! types and behaviors.)
//
// * There is added checking that a literal block is not used as a condition,
//   to catch common mistakes like `if [x = 10] [...]`.
//

#include "sys-core.h"

#define Value REBVAL


//
//  if: native [
//
//  {When TO LOGIC! CONDITION is true, execute branch}
//
//      return: "null if branch not run, otherwise branch result"
//          [<opt> <void> any-value!]
//      condition [<opt> any-value!]
//      :branch "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch!]
//  ]
//
REBNATIVE(if)
{
    INCLUDE_PARAMS_OF_IF;

    Value *condition = ARG(condition);
    Value *branch = ARG(branch);

    if (Is_Conditional_False(condition))
        return VOID;  // ^-- test errors on literal block

    if (Do_Branch_Throws(OUT, branch, condition))
        return THROWN;  // ^-- condition is passed to function branches

    return_branched (OUT);  // asserts not null or ~void~
}


//
//  either: native [
//
//  {Choose a branch to execute, based on TO-LOGIC of the CONDITION value}
//
//      return: [<opt> any-value!]
//          "Returns null if either branch returns null (unlike IF...ELSE)"
//      condition [<opt> any-value!]
//      :true-branch "If arity-1 ACTION!, receives the evaluated condition"
//          [any-branch!]
//      :false-branch
//          [any-branch!]
//  ]
//
REBNATIVE(either)
{
    INCLUDE_PARAMS_OF_EITHER;

    Value *condition = ARG(condition);

    Value *branch = Is_Conditional_True(condition)
        ? ARG(true_branch)  // ^-- test errors on literal block
        : ARG(false_branch);

    if (Do_Branch_Throws(OUT, branch, condition))
        return THROWN;  // ^-- condition is passed to function branches

    return_branched (OUT);  // asserts not null or ~void~
}


//
//  did: native [
//
//  {Synonym for NOT NULL? that is isotope tolerant (IF DID is prefix THEN)}
//
//      return: [logic!]
//      ^optional "Argument to test"
//          [<opt> <void> any-value!]
//      /decay "Pre-decay ~null~ isotope input to NULL"
//  ]
//
REBNATIVE(did_1)  // see TO-C-NAME
//
// DID exists as a complement to isotopes to help solve conflation of falsey
// values with conditional tests.  One example:
//
//     >> match [logic! integer!] false
//     == ~false~  ; isotope
//
//     >> if (match [logic! integer!] false) [print "Want this to run"]
//     ** Error: We save you by not letting isotopes be conditionally tested
//
//     >> did match [logic! integer!] false
//     == #[true]  ; DID tolerates isotopes, returns #[false] only on true NULL
//
//     >> if (did match [logic! integer!] false) [print "Praise isotopes!"]
//     Praise isotopes!
//
// By making routines that intend to return ANY-VALUE! (even falsey ones) on
// success return the falsey ones as isotopes, incorrect uses can be caught
// and guided to use DID or DIDN'T (or whatever they actually meant).
{
    INCLUDE_PARAMS_OF_DID_1;

    Value *in = ARG(optional);

    if (IS_NULLED(in) or Is_Meta_Of_Void(in))
        return Init_False(OUT);

    if (REF(decay) and Is_Meta_Of_Null_Isotope(in))
        return Init_False(OUT);

    return Init_True(OUT);
}


//
//  didn't: native [
//
//  {Synonym for NULL? that is isotope tolerant (IF DIDN'T is prefix ELSE)}
//
//      return: [logic!]
//      ^optional "Argument to test"
//          [<opt> <void> any-value!]
//      /decay "Pre-decay ~null~ isotope input to NULL"
//  ]
//
REBNATIVE(didnt)
{
    INCLUDE_PARAMS_OF_DIDNT;

    Value *in = ARG(optional);

    if (IS_NULLED(in) or Is_Meta_Of_Void(in))
        return Init_True(OUT);

    if (REF(decay) and Is_Meta_Of_Null_Isotope(in))
        return Init_True(OUT);

    return Init_False(OUT);
}


//
//  then: enfix native [
//
//  {If input is null, return null, otherwise evaluate the branch}
//
//      return: "null if input is null, or branch result"
//          [<opt> <void> any-value!]
//      ^optional "<deferred argument> Run branch if this is not null"
//          [<opt> <void> any-value!]
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [any-branch!]
//      /decay "Pre-decay ~null~ isotope input to NULL"
//  ]
//
REBNATIVE(then)  // see `tweak :then 'defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_THEN;

    Value *in = ARG(optional);

    if (
        IS_NULLED(in)  // soft failure signal
        or Is_Meta_Of_Void(in)
        or (REF(decay) and Is_Meta_Of_Null_Isotope(in))  // null isotope
    ){
        return VOID;
    }

    // We received the left hand side as ^meta, so it's quoted in order
    // to be isotope-tolerant.  Now that's tested, unquote it back to normal.

    Meta_Unquotify(in);

    if (Do_Branch_Throws(OUT, ARG(branch), in))
        return THROWN;

    return_branched (OUT);  // asserts not null or ~void~
}


//
//  also: enfix native [
//
//  {For non-null input, evaluate and discard branch (like a pass-thru THEN)}
//
//      return: "The same value as input, regardless of if branch runs"
//          [<opt> <void> any-value!]
//      ^optional "<deferred argument> Run branch if this is not null"
//          [<opt> <void> any-value!]
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [any-branch!]
//      /decay "Pre-decay ~null~ isotope input to NULL"
//  ]
//
REBNATIVE(also)  // see `tweak :also 'defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_ALSO;  // `then func [x] [(...) :x]` => `also [...]`

    Value *in = ARG(optional);

    if (IS_NULLED(in))
        return nullptr;  // telegraph pure null

    if (Is_Meta_Of_Void(in))
        return VOID;  // telegraph invisible intent

    if (REF(decay) and Is_Meta_Of_Null_Isotope(in))
        return Init_Isotope(OUT, Canon(NULL));  // telegraph null isotope

    Meta_Unquotify(in);

    if (Do_Branch_Throws(SPARE, ARG(branch), in))
        return THROWN;

    Move_Cell(OUT, in);
    return_branched (OUT);  // also wouldn't run if `in` was null or ~void~
}


//
//  else: enfix native [
//
//  {If input is not null, return that value, otherwise evaluate the branch}
//
//      return: "Input value if not null, or branch result"
//          [<opt> <void> any-value!]
//      ^optional "<deferred argument> Run branch if this is null"
//          [<opt> <void> any-value!]
//      :branch [any-branch!]
//      /decay "Pre-decay ~null~ isotope input to NULL"
//  ]
//
REBNATIVE(else)  // see `tweak :else 'defer on` in %base-defs.r
{
    INCLUDE_PARAMS_OF_ELSE;

    Value *in = ARG(optional);

    if (IS_NULLED(in)) {
        //
        // Triggers for running an ELSE are null and void
        //
        // Note ELSE is not reactive to "nothing" isotopes (~) by design:
        //
        //    >> if true [print "branch ran", if false [<a>]]
        //    == ~  ; isotope
        //
        //    >> if true [print "branch ran, if false [<a>]] else [<b>]
        //    == ~  ; isotope
        //
    }
    else if (Is_Meta_Of_Void(in)) {
        Init_Bad_Word(in, Canon(VOID));
    }
    else if (REF(decay) and Is_Meta_Of_Null_Isotope(in)) {
        //
        // Since the input is a ^META parameter, this signals a null isotope.
        // When /DECAY is specified we trigger running the branch here also.
        // Do_Branch() decays ~null~ isotopes to NULL for non-meta actions
        //
        // !!! While it may be misleading to call it /DECAY to trigger and not
        // decay what's passed to the function, the ostensible only reason an
        // ELSE would ever have a function taking a parameter would be to know
        // the distinction.
        //
        Init_Null_Isotope(in);
    }
    else {
        // We have to use a ^meta parameter in order to detect the difference
        // between NULL (which runs the branch) and a ~null~ isotope (which
        // does not run the branch, unless we are decaying).  But we don't want
        // to actually return a quoted parameter.
        //
        return_value (Meta_Unquotify(in));
    }

    if (Do_Branch_Throws(OUT, ARG(branch), in))
        return THROWN;

    return_branched (OUT);  // asserts not null or ~void~
}


//
//  match: native [
//
//  {Check value using tests (match types, TRUE or FALSE, or filter action)}
//
//      return: "Input if it matched, NULL if it did not (isotope if falsey)"
//          [<opt> any-value!]
//      test "Typeset or arity-1 filter function"
//          [<opt> logic! action! block! datatype! typeset!]
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(match)
//
// Note: Ambitious ideas for the "MATCH dialect" are on hold, and this function
// just does some fairly simple matching:
//
//   https://forum.rebol.info/t/time-to-meet-your-match-dialect/1009/5
{
    INCLUDE_PARAMS_OF_MATCH;

    REBVAL *v = ARG(value);
    REBVAL *test = ARG(test);

    switch (VAL_TYPE(test)) {
      case REB_NULL:
        if (not IS_NULLED(v))
            return nullptr;
        break;

      case REB_LOGIC:
        if (IS_TRUTHY(v) != VAL_LOGIC(test))
            return nullptr;
        break;

      case REB_DATATYPE:
        if (VAL_TYPE(v) != VAL_TYPE_KIND(test))
            return nullptr;
        break;

      case REB_BLOCK: {
        REB_R r = MAKE_Typeset(SPARE, REB_TYPESET, nullptr, test);
        if (r == R_THROWN)
            return THROWN;
        test = SPARE;
        goto test_is_typeset; }

      case REB_TYPESET:
      test_is_typeset:
        if (not TYPE_CHECK(test, VAL_TYPE(v)))
            return nullptr;
        break;

      case REB_ACTION: {
        if (rebRunThrows(
            SPARE,  // <-- output cell
            test, rebQ(v)
        )){
            return THROWN;
        }
        if (IS_FALSEY(SPARE))
            return nullptr;
        break; }

      default:
        fail (PAR(test));  // all test types should be accounted for in switch
    }

    //=//// IF IT GOT THIS FAR WITHOUT RETURNING, THE TEST MATCHED /////////=//

    // Falsey matched values return isotopes to show they did match, but to
    // avoid misleading falseness of the result:
    //
    //     >> value: false
    //     >> if match [integer! logic!] value [print "Won't run :-("]
    //     ; null  <-- this would be a bad result!
    //
    // So successful matching of falsey values will give back ~false~, ~blank~,
    // or ~null~ isotopes.  This can be consciously turned back into their
    // original values with DECAY, which happens automatically in assignments.
    //
    //     >> match blank! _
    //     == ~blank~  ; isotope
    //
    //     >> decay match blank! _
    //     == _
    //
    Isotopify_If_Falsey(v);

    Copy_Cell(OUT, v);  // Otherwise, input is the result

    return_branched (OUT);  // asserts no pure NULL or isotope ~void~
}


//
//  must: native [
//
//  {Ensure that the argument is not NULL}
//
//      return: "Same as input value if non-NULL"
//          [any-value!]
//      value [<opt> any-value!]
//  ]
//
REBNATIVE(must)  // `must x` is a faster synonym for `non null x`
{
    INCLUDE_PARAMS_OF_MUST;

    if (IS_NULLED(ARG(value)))
        fail ("MUST requires argument to not be NULL");

    return_value (ARG(value));
}


//
//  all: native [
//
//  {Short-circuiting variant of AND, using a block of expressions as input}
//
//      return: "Product of last passing evaluation if all truthy, else null"
//          [<opt> <void> any-value!]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! the-block!]
//      /predicate "Test for whether an evaluation passes (default is DID)"
//          [action!]
//  ]
//
REBNATIVE(all)
//
// 1. ALL takes advantage of the tricky mechanics of "void" in the system, so
//    that void evaluations can be vanished without requiring saving a copy of
//    the previous output to drop out.  For instance:
//
//        >> check1: true, check2: false
//
//        >> all [if check1 [<kept>], if check2 [<dropped>]]
//        == <kept>
//
//    When the second IF begins running, the <kept> value is in the OUT cell.
//    Condition fails, so it uses `return_void` to set CELL_FLAG_NOTE_VOIDED on
//    the OUT cell--but without overwriting it.  The void step is skipped,
//    and then Clear_Void_Flag() is called at the end of the loop to clear
//    CELL_FLAG_VOIDED and un-hide the <kept> result.
//
// 2. Historicall there has been controversy over what should be done about
//    (all []) and (any []).  Languages that have variadic short-circuiting
//    AND + OR operations typically empty AND-ing is truthy while empty OR-ing
//    is falsey.
//
//    There are some reasonable intuitive arguments for that--*if* those are
//    your only two choices.  Because Ren-C has the option of isotopes, it's
//    better to signal to the caller that nothing happened.  For an example
//    of how useful it is, see the loop wrapper FOR-BOTH.  Other behaviors
//    can be forced with (all [... null]) or (any [true ...])
//
// 3. When the ALL starts, the OUT cell is stale and may contain any value
//    produced by a previous evaluation.  But we use EVAL_FLAG_MAYBE_STALE
//    and may be skipping stale evaluations from inside the ALL:
//
//        >> <foo> all [<bar> comment "<bar> will be stale, due to comment"]
//        == <bar>  ; e.g. we don't want <foo>
//
//    So the stale flag alone is not enough information to know if the output
//    cell should be marked non-stale.  So we use `any_matches`.
//
// 4. The only way a falsey evaluation should make it to the end is if a
//    predicate let it pass.  Don't want that to trip up `if all` so make it
//    an isotope...but this way `(all/predicate [null] :not?) then [<runs>]`
{
    INCLUDE_PARAMS_OF_ALL;

    REBVAL *predicate = ARG(predicate);

    REBFLGS flags = EVAL_MASK_DEFAULT | EVAL_FLAG_MAYBE_STALE;  // see [1]

    if (IS_THE_BLOCK(ARG(block)))
        flags |= EVAL_FLAG_NO_EVALUATIONS;

    DECLARE_FRAME_AT (f, ARG(block), flags);
    Push_Frame(nullptr, f);

    bool any_matches = false;

    while (NOT_END(f_value)) {
        if (Eval_Step_Throws(OUT, f)) {  // overlap output, see [1]
            Abort_Frame(f);
            return THROWN;
        }

        if (Is_Stale(OUT))  // void steps, e.g. (comment "hi") (if false [<a>])
            continue;

        if (Is_Isotope(OUT))
            fail (Error_Bad_Isotope(OUT));

        if (IS_NULLED(predicate)) {  // default predicate effectively .DID
            if (IS_FALSEY(OUT)) {  // false/blank/null triggers failure
                Abort_Frame(f);
                return nullptr;
            }
        }
        else {
            if (rebRunThrows(
                SPARE,  // <-- output cell
                predicate, rebQ(NULLIFY_NULLED(OUT))
            )){
                return THROWN;
            }

            if (IS_FALSEY(SPARE)) {
                Abort_Frame(f);
                return nullptr;
            }
        }

        any_matches = true;
    }

    Drop_Frame(f);

    if (not any_matches)  // e.g. `all []`, can't use Is_Stale(OUT), see [3]
        return VOID;

    Clear_Stale_Flag(OUT);  // un-hide values "underneath" void, again see [1]

    Isotopify_If_Falsey(OUT);  // predicates can approve falsey values, see [4]
    return_branched (OUT);
}


//
//  any: native [
//
//  {Short-circuiting version of OR, using a block of expressions as input}
//
//      return: "First passing evaluative result, or null if none pass"
//          [<opt> <void> any-value!]
//      block "Block of expressions, @[block] will be treated inertly"
//          [block! the-block!]
//      /predicate "Test for whether an evaluation passes (default is DID)"
//          [action!]
//  ]
//
REBNATIVE(any)
//
// Note: See ALL for more comments (ANY is very similar)
{
    INCLUDE_PARAMS_OF_ANY;

    REBVAL *predicate = ARG(predicate);
    REBVAL *block = ARG(block);

    REBFLGS flags = EVAL_MASK_DEFAULT | EVAL_FLAG_MAYBE_STALE;
    if (IS_THE_BLOCK(block))
        flags |= EVAL_FLAG_NO_EVALUATIONS;

    DECLARE_FRAME_AT (f, block, flags);
    Push_Frame(nullptr, f);

    while (NOT_END(f_value)) {
        if (Eval_Step_Throws(OUT, f)) {
            Abort_Frame(f);
            return THROWN;
        }

        if (Is_Stale(OUT))  // void steps, e.g. (comment "hi") (if false [<a>])
            continue;

        if (Is_Isotope(OUT))
            fail (Error_Bad_Isotope(OUT));

        if (IS_NULLED(predicate)) {  // default predicate effectively .DID
            if (IS_TRUTHY(OUT)) {
                Abort_Frame(f);
                return OUT;  // successful ANY returns the value
            }
        }
        else {
            if (rebRunThrows(
                SPARE,  // <-- output cell
                predicate, rebQ(NULLIFY_NULLED(OUT))
            )){
                return THROWN;
            }

            if (IS_TRUTHY(SPARE)) {
                //
                // Don't let ANY return something falsey, but using an isotope
                // makes `any .not [null] then [<run>]` work
                //
                Isotopify_If_Falsey(OUT);

                Abort_Frame(f);
                return OUT;  // return input to the test, not result
            }
        }

    }

    Drop_Frame(f);

    if (Is_Stale(OUT))  // original staleness, or all the ANY steps were void
        return VOID;  // `any []` is ~void~ isotope

    return nullptr;
}


//
//  case: native [
//
//  {Evaluates each condition, and when true, evaluates what follows it}
//
//      return: "Last matched case evaluation, or null if no cases matched"
//          [<opt> <void> any-value!]
//      cases "Conditions followed by branches"
//          [block!]
//      /all "Do not stop after finding first logically true case"
//      <local> branch  ; temp GC-safe holding location (can't eval into)
//      /predicate "Unary case-processing action (default is DID)"
//          [action!]
//  ]
//
REBNATIVE(case)
//
// 1. Expressions that are between branches are allowed to vaporize.  This is
//    powerful, but people should be conscious of what can produce voids and
//    not try to use them as conditions:
//
//        >> condition: false
//        >> case [if condition [<a>] [print "Whoops?"] [<hmm>]]
//        Whoops?
//        == <hmm>
//
//   Those who dislike this can use variations of CASE that require `=>`.
//
// 2. Maintain symmetry with IF on non-taken branches:
//
//        >> if false <some-tag>
//        ** Script Error: if does not allow tag! for its branch...
//
// 3. Last evaluation will "fall out" if there is no branch:
//
//        >> case [false [<a>] false [<b>]]
//        == ~void~  ; isotope
//
//        >> case [false [<a>] false [<b>] 10 + 20]
//        == 30
//
//    It's a little bit like a quick-and-dirty ELSE (or /DEFAULT), however
//    when you use CASE/ALL it's what is returned even if there's a match:
//
//        >> case/all [1 < 2 [<a>] 3 < 4 [<b>]]
//        == <b>
//
//        >> case/all [1 < 2 [<a>] 3 < 4 [<b>] 10 + 20]
//        == 30  ; so not the same as an ELSE, it's just "fallout"
{
    INCLUDE_PARAMS_OF_CASE;

    REBVAL *predicate = ARG(predicate);

    DECLARE_FRAME_AT (f, ARG(cases), EVAL_MASK_DEFAULT);

    Push_Frame(nullptr, f);

    // We potentially want to return the previous result to act "translucent"
    // if no cases run.  So condition evaluation must be done into the spare.

    assert(Is_Void(SPARE));  // spare starts out as void

    for (; NOT_END(f_value); RESET(SPARE)) {

        // Feed the frame forward one step for predicate argument.
        //
        // NOTE: It may seem tempting to run PREDICATE from on `f` directly,
        // allowing it to take arity > 2.  Don't do this.  We have to get a
        // true/false answer *and* know what the right hand argument was, for
        // full case coverage and for DEFAULT to work.

        if (Eval_Step_Throws(SPARE, f))
            goto threw;

        if (Is_Void(SPARE))  // skip void expressions, see [1]
            continue;

        if (IS_END(f_value))
            goto reached_end;  // we tolerate "fallout" from a condition

        bool matched;
        if (IS_NULLED(predicate)) {
            if (Is_Isotope(SPARE))
                fail (Error_Bad_Isotope(SPARE));

            matched = IS_TRUTHY(SPARE);
        }
        else {
            DECLARE_LOCAL (temp);
            if (rebRunThrows(
                temp,  // target of rebRun() is kept GC-safe by evaluator
                predicate, rebQ(SPARE)
            )){
                Move_Cell(OUT, temp);
                goto threw;
            }
            matched = IS_TRUTHY(temp);
        }

        if (IS_GET_GROUP(f_value)) {
            //
            // IF evaluates branches that are GET-GROUP! even if it does
            // not run them.  This implies CASE should too.
            //
            // Note: Can't evaluate directly into ARG(branch)...frame cell.
            //
            DECLARE_LOCAL (temp);  // target of Eval_Value() kept save by eval
            if (Eval_Value_Throws(temp, f_value, f_specifier))
                goto threw;
            Move_Cell(ARG(branch), temp);
        }
        else
            Derelativize(ARG(branch), f_value, f_specifier);

        Fetch_Next_Forget_Lookback(f);  // branch now in ARG(branch), so skip

        if (not matched) {
            if (not (FLAGIT_KIND(VAL_TYPE(ARG(branch))) & TS_BRANCH))
                fail (Error_Bad_Value_Raw(ARG(branch)));  // like IF, see [2]

            continue;
        }

        // Once we run a branch, translucency is no longer an option, so go
        // ahead and write OUT.

        if (Do_Branch_Throws(OUT, ARG(branch), SPARE))
            goto threw;

        if (not REF(all)) {
            Drop_Frame(f);
            return_branched (OUT);
        }
    }

  reached_end:

    assert(REF(all) or Is_Stale(OUT));

    Drop_Frame(f);

    if (not Is_Void(SPARE)) {  // prioritize fallout result, see [3]
        Isotopify_If_Nulled(SPARE);
        Move_Cell(OUT, SPARE);
        return_branched (OUT);  // asserts no ~void~ or pure null
    }

    if (Is_Stale(OUT))  // none of the clauses of an /ALL ran a branch
        return VOID;

    return_branched (OUT);  // asserts no ~void~ or pure null

  threw:

    Abort_Frame(f);
    return THROWN;
}


//
//  switch: native [
//
//  {Selects a choice and evaluates the block that follows it.}
//
//      return: "Last case evaluation, or null if no cases matched"
//          [<opt> <void> any-value!]
//      value "Target value"
//          [<opt> any-value!]
//      cases "Block of cases (comparison lists followed by block branches)"
//          [block!]
//      /all "Evaluate all matches (not just first one)"
//      /predicate "Binary switch-processing action (default is EQUAL?)"
//          [action!]
//  ]
//
REBNATIVE(switch)
{
    INCLUDE_PARAMS_OF_SWITCH;

    REBVAL *predicate = ARG(predicate);

    DECLARE_FRAME_AT (f, ARG(cases), EVAL_MASK_DEFAULT);

    Push_Frame(nullptr, f);

    REBVAL *left = ARG(value);
    if (IS_BLOCK(left) and GET_CELL_FLAG(left, UNEVALUATED))
        fail (Error_Block_Switch_Raw(left));  // `switch [x] [...]` safeguard

    assert(Is_Void(SPARE));

    for (; NOT_END(f_value); RESET(SPARE)) {  // clears fallout each `continue`

        if (IS_BLOCK(f_value) or IS_ACTION(f_value)) {
            Fetch_Next_Forget_Lookback(f);
            continue;
        }

        // Feed the frame forward...evaluate one step to get second argument.
        //
        // NOTE: It may seem tempting to run COMPARE from the frame directly,
        // allowing it to take arity > 2.  Don't do this.  We have to get a
        // true/false answer *and* know what the right hand argument was, for
        // full switching coverage and for DEFAULT to work.
        //
        // !!! Advanced frame tricks *might* make this possible for N-ary
        // functions, the same way `match parse "aaa" [some "a"]` => "aaa"

        if (Eval_Step_Throws(SPARE, f))
            goto threw;  // ^-- spare is already reset, exploit?

        if (Is_Void(SPARE))  // skip comments or failed conditionals
            continue;  // see note [2] in comments for CASE

        if (IS_END(f_value))
            goto reached_end;  // nothing left, so drop frame and return

        if (IS_NULLED(predicate)) {
            //
            // It's okay that we are letting the comparison change `value`
            // here, because equality is supposed to be transitive.  So if it
            // changes 0.01 to 1% in order to compare it, anything 0.01 would
            // have compared equal to so will 1%.  (That's the idea, anyway,
            // required for `a = b` and `b = c` to properly imply `a = c`.)
            //
            // !!! This means fallout can be modified from its intent.  Rather
            // than copy here, this is a reminder to review the mechanism by
            // which equality is determined--and why it has to mutate.
            //
            // !!! A branch composed into the switch cases block may want to
            // see the un-mutated condition value.
            //
            const bool strict = false;
            if (0 != Compare_Modify_Values(left, SPARE, strict))
                continue;
        }
        else {
            // `switch x .greater? [10 [...]]` acts like `case [x > 10 [...]]
            // The ARG(value) passed in is the left/first argument to compare.
            //
            // !!! Using Run_Throws loses the labeling of the function we were
            // given (label).  Consider how it might be passed through
            // for better stack traces and error messages.
            //
            // !!! We'd like to run this faster, so we aim to be able to
            // reuse this frame...hence SPARE should not be expected to
            // survive across this point.
            //
            DECLARE_LOCAL (temp);
            if (rebRunThrows(
                temp,  // <-- output cell
                predicate,
                    rebQ(left),  // first arg (left hand side if infix)
                    rebQ(SPARE)  // second arg (right hand side if infix)
            )){
                Move_Cell(OUT, temp);
                goto threw;
            }
            if (IS_FALSEY(temp))
                continue;
        }

        // Skip ahead to try and find BLOCK!/ACTION! branch to take the match
        //
        while (true) {
            if (IS_END(f_value))
                goto reached_end;

            if (IS_BLOCK(f_value) or IS_META_BLOCK(f_value)) {
                //
                // f_value is Cell, can't Do_Branch
                //
                if (Do_Any_Array_At_Core_Throws(
                    RESET(OUT),
                    EVAL_MASK_DEFAULT | EVAL_FLAG_BRANCH,
                    f_value,
                    f_specifier
                )){
                    goto threw;
                }
                break;
            }

            if (IS_ACTION(f_value)) {  // must have been COMPOSE'd in cases
                DECLARE_LOCAL (temp);
                if (rebRunThrows(
                    temp,  // <-- output cell
                    SPECIFIC(f_value),  // actions don't need specifiers
                        rebQ(OUT)
                )){
                    Move_Cell(OUT, temp);
                    goto threw;
                }
                Move_Cell(OUT, temp);
                break;
            }

            Fetch_Next_Forget_Lookback(f);
        }

        if (not REF(all)) {
            Drop_Frame(f);
            return_branched (OUT);  // asserts no pure NULL or isotope ~void~
        }

        Fetch_Next_Forget_Lookback(f);  // keep matching if /ALL
    }

  reached_end:

    assert(REF(all) or Is_Stale(OUT));

    Drop_Frame(f);

    // See remarks in CASE about why fallout result is prioritized, and why
    // it cannot be a pure NULL.
    //
    if (not Is_Void(SPARE)) {
        Move_Cell(OUT, SPARE);
        return_branched (OUT);
    }

    // if no fallout, use last /ALL clause, or ~void~ isotope if END
    //
    if (Is_Stale(OUT))
        return VOID;

    return_branched (OUT);  // asserts no pure NULL or isotope ~void~

  threw:

    Abort_Frame(f);
    return THROWN;
}


//
//  default: enfix native [
//
//  {Set word or path to a default value if it is not set yet}
//
//      return: "Former value or branch result, can only be null if no target"
//          [<opt> any-value!]
//      :target "Word or path which might be set appropriately (or not)"
//          [set-group! set-word! set-tuple!]  ; to left of DEFAULT
//      :branch "If target needs default, this is evaluated and stored there"
//          [any-branch!]
//      /predicate "Test beyond null/void for defaulting, else NOT BLANK?"
//          [action!]
//  ]
//
REBNATIVE(default)
{
    INCLUDE_PARAMS_OF_DEFAULT;

    REBVAL *target = ARG(target);

    REBVAL *predicate = ARG(predicate);

    REBVAL *steps = SPARE;

    // The TARGET may be something like a TUPLE! that contains GROUP!s, or a
    // SET-GROUP!.  When we ask for `steps` back from Get, it will give a
    // block back that has the evaluations processed out and can be used for
    // the SET without doing a double evaluation.
    //
    if (Get_Var_Core_Throws(OUT, steps, target, SPECIFIED))
        return THROWN;

    // We only consider those bad words which are in the "unfriendly" state
    // that the system also knows to mean "emptiness" as candidates for
    // overriding.  That's ~ isotopes at the moment.  Note that friendly ones
    // do not count:
    //
    //     >> x: first [~]
    //     == ~
    //
    //     >> x: default [10]
    //     == ~
    //
    bool overwrite;
    if (Is_Isotope(OUT))
        overwrite = Is_None(OUT);
    else
        overwrite = IS_NULLED(OUT) or IS_BLANK(OUT);

    if (not overwrite) {
        if (not REF(predicate)) {  // no custom additional constraint
            return OUT;  // count it as "already set"
        }
        else {
            if (rebUnboxLogic(predicate, rebQ(OUT)))  // rebTruthy() ?
                return OUT;
        }
    }

    if (Do_Branch_Throws(OUT, ARG(branch), END))
        return THROWN;

    if (Set_Var_Core_Throws(SPARE, nullptr, steps, SPECIFIED, OUT)) {
        assert(false);  // shouldn't be able to happen.
        fail (Error_No_Catch_For_Throw(FRAME));
    }

    return OUT;
}


//
//  catch: native [
//
//  {Catches a throw from a block and returns its value.}
//
//      return: "Thrown value, or BLOCK! with value and name (if /NAME, /ANY)"
//          [<opt> any-value!]
//      result: "<output> Evaluation result (only set if not thrown)"
//          [<opt> any-value!]
//
//      block "Block to evaluate"
//          [block!]
//      /name "Catches a named throw (single name if not block)"
//          [block! word! action! object!]
//      /quit "Special catch for QUIT native"
//      /any "Catch all throws except QUIT (can be used with /QUIT)"
//  ]
//
REBNATIVE(catch)
//
// There's a refinement for catching quits, and CATCH/ANY will not alone catch
// it (you have to CATCH/ANY/QUIT).  Currently the label for quitting is the
// NATIVE! function value for QUIT.
{
    INCLUDE_PARAMS_OF_CATCH;

    // /ANY would override /NAME, so point out the potential confusion
    //
    if (REF(any) and REF(name))
        fail (Error_Bad_Refines_Raw());

    if (not Do_Any_Array_At_Throws(OUT, ARG(block), SPECIFIED)) {
        if (REF(result))
            rebElide(Lib(SET), rebQ(REF(result)), rebQ(OUT));

        return nullptr;  // no throw means just return null
    }

    const REBVAL *label = VAL_THROWN_LABEL(frame_);

    if (REF(any) and not (
        IS_ACTION(label)
        and ACT_DISPATCHER(VAL_ACTION(label)) == &N_quit
    )){
        goto was_caught;
    }

    if (REF(quit) and (
        IS_ACTION(label)
        and ACT_DISPATCHER(VAL_ACTION(label)) == &N_quit
    )){
        goto was_caught;
    }

    if (REF(name)) {
        //
        // We use equal? by way of Compare_Modify_Values, and re-use the
        // refinement slots for the mutable space

        REBVAL *temp1 = ARG(quit);
        REBVAL *temp2 = ARG(any);

        if (IS_BLOCK(ARG(name))) {
            //
            // Test all the words in the block for a match to catch

            const Cell *tail;
            const Cell *candidate = VAL_ARRAY_AT(&tail, ARG(name));
            for (; candidate != tail; candidate++) {
                //
                // !!! Should we test a typeset for illegal name types?
                //
                if (IS_BLOCK(candidate))
                    fail (PAR(name));

                Derelativize(temp1, candidate, VAL_SPECIFIER(ARG(name)));
                Copy_Cell(temp2, label);

                // Return the THROW/NAME's arg if the names match
                //
                bool strict = false;  // e.g. EQUAL?, better if STRICT-EQUAL?
                if (0 == Compare_Modify_Values(temp1, temp2, strict))
                    goto was_caught;
            }
        }
        else {
            Copy_Cell(temp1, ARG(name));
            Copy_Cell(temp2, label);

            // Return the THROW/NAME's arg if the names match
            //
            bool strict = false;  // e.g. EQUAL?, better if STRICT-EQUAL?
            if (0 == Compare_Modify_Values(temp1, temp2, strict))
                goto was_caught;
        }
    }
    else {
        // Return THROW's arg only if it did not have a /NAME supplied
        //
        if (IS_NULLED(label) and (REF(any) or not REF(quit)))
            goto was_caught;
    }

    return THROWN; // throw label and value are held task local

  was_caught:

    if (REF(name) or REF(any)) {
        REBARR *a = Make_Array(2);

        Copy_Cell(ARR_AT(a, 0), label); // throw name
        CATCH_THROWN(ARR_AT(a, 1), frame_); // thrown value--may be null!
        if (IS_NULLED(ARR_AT(a, 1)))
            SET_SERIES_LEN(a, 1); // trim out null value (illegal in block)
        else
            SET_SERIES_LEN(a, 2);
        return Init_Block(OUT, a);
    }

    CATCH_THROWN(OUT, frame_); // thrown value

    if (Is_Void(OUT))
        return Init_None(OUT);  // void would trigger ELSE

    Isotopify_If_Nulled(OUT);  // a caught NULL triggers THEN, not ELSE
    return_branched (OUT);
}


//
//  throw: native [
//
//  "Throws control back to a previous catch."
//
//      return: []  ; !!! notation for divergent function?
//      ^value "Value returned from catch"
//          [<opt> any-value!]
//      /name "Throws to a named catch"
//          [word! action! object!]
//  ]
//
REBNATIVE(throw)
//
// Choices are currently limited for what one can use as a "name" of a THROW.
// Note blocks as names would conflict with the `name_list` feature in CATCH.
//
// !!! Should it be /NAMED instead of /NAME?
{
    INCLUDE_PARAMS_OF_THROW;

    REBVAL *v = ARG(value);
    Meta_Unquotify(v);

    return Init_Thrown_With_Label(FRAME, v, ARG(name));  // name can be nulled
}
