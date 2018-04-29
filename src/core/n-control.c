//
//  File: %n-control.c
//  Summary: "native functions for control flow"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Control constructs in Ren-C differ from R3-Alpha in some ways:
//
// * If they do not run any branches, they evaluate to void ("unset!") and not
//   BLANK! ("none!").  But if a branch does run, and evaluates to void, then
//   the result is altered to be BLANK!.  Hence void can cue other functions
//   (like THEN and ELSE) to be sure no branch ran, and respond appropriately.
//
// * It is possible to ask the return result to not be "blankified", but
//   give back the possibly-void value, with the /ONLY refinement.  This is
//   specialized as functions ending in *.  (IF*, EITHER*, CASE*, SWITCH*...)
//
// * Zero-arity function values used as branches will be executed, and
//   single-arity functions used as branches will also be executed--but passed
//   the value of the triggering condition.  See Run_Branch_Throws().
//
// * There is added checking that a literal block is not used as a condition,
//   to catch common mistakes like `if [x = 10] [...]`.
//

#include "sys-core.h"


//
//  if: native [
//
//  {When TO-LOGIC CONDITION is true, execute branch}
//
//      return: "void if branch not run, else branch result"
//          [<opt> any-value!]
//      condition [any-value!]
//      branch "If arity-1 action, receives the evaluated condition"
//          [block! action!]
//      /only "If branch runs and returns void, do not convert it to BLANK!"
//  ]
//
REBNATIVE(if)
{
    INCLUDE_PARAMS_OF_IF;

    if (IS_CONDITIONAL_FALSE(ARG(condition)))
        return R_VOID;

    if (Run_Branch_Throws(D_OUT, ARG(condition), ARG(branch), REF(only)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  unless: native [
//
//  {When TO-LOGIC CONDITION is false, execute branch}
//
//      return: "void if branch not run, else branch result"
//          [<opt> any-value!]
//      condition [any-value!]
//      branch [block! action!]
//      /only "If branch runs and returns void, do not convert it to BLANK!"
//  ]
//
REBNATIVE(unless)
{
    INCLUDE_PARAMS_OF_UNLESS;

    if (IS_CONDITIONAL_TRUE(ARG(condition)))
        return R_VOID;

    if (Run_Branch_Throws(D_OUT, ARG(condition), ARG(branch), REF(only)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  either: native [
//
//  {Choose a branch to execute, based on TO-LOGIC of the condition value}
//
//      return: [<opt> any-value!]
//      condition [any-value!]
//      true-branch "If arity-1 action, receives the evaluated condition"
//          [block! action!]
//      false-branch [block! action!]
//      /only "If branch runs and returns void, do not convert it to BLANK!"
//  ]
//
REBNATIVE(either)
{
    INCLUDE_PARAMS_OF_EITHER;

    if (Run_Branch_Throws(
        D_OUT,
        ARG(condition),
        IS_CONDITIONAL_TRUE(ARG(condition))
            ? ARG(true_branch)
            : ARG(false_branch),
        REF(only)
    )){
        return R_OUT_IS_THROWN;
    }

    return R_OUT;
}


//
//  either-test: native [
//
//  {If value passes test, return that value, otherwise take the branch.}
//
//      return: "Input value if it matched, or branch result (BLANK! if void)"
//          [<opt> any-value!]
//      test "Typeset membership, LOGIC! to test TRUTHY?, filter function"
//          [action! datatype! typeset! block! logic!]
//      value [<opt> any-value!]
//      branch [block! action!]
//      /only "If branch runs and returns void, do not convert it to BLANK!"
//  ]
//
REBNATIVE(either_test)
{
    INCLUDE_PARAMS_OF_EITHER_TEST;

    REBVAL *test = ARG(test);
    REBVAL *value = ARG(value);

    if (IS_LOGIC(test)) { // test for "truthy" or "falsey"
        if (IS_VOID(value)) { // void is neither true nor false
            DECLARE_LOCAL (word);
            Init_Word(word, VAL_PARAM_SPELLING(PAR(test)));
            fail (Error_No_Value(word));
        }

        // If this is the result of composing together a test with a literal,
        // it may be the *test* that changes...so in effect, we could be
        // "testing the test" on a fixed value.  Allow literal blocks (e.g.
        // use IS_TRUTHY() instead of IS_CONDITIONAL_TRUE())
        //
        if (VAL_LOGIC(test) != IS_TRUTHY(value))
            goto test_failed;
        return R_FROM_BOOL(VAL_LOGIC(test));
    }

    // Force single items into array style access so only one version of the
    // code needs to be written.
    //
    RELVAL *item;
    REBSPC *specifier;
    if (IS_BLOCK(test)) {
        item = VAL_ARRAY_AT(test);
        specifier = VAL_SPECIFIER(test);
    }
    else {
        Move_Value(D_CELL, test);
        item = D_CELL; // implicitly terminated
        specifier = SPECIFIED;
    }

    REB_R r; // goto crosses initialization
    r = R_UNHANDLED;

    for (; NOT_END(item); ++item) {
        //
        // If we're dealing with a single item for the test, provided e.g.
        // as :even?, then it's already fetched.  But if it was a block like
        // [:even? integer!] we enumerate it in word form and have to get it.
        //
        const RELVAL *var = IS_WORD(item)
            ? Get_Opt_Var_May_Fail(item, specifier)
            : item;

        if (IS_DATATYPE(var)) {
            if (VAL_TYPE_KIND(var) == VAL_TYPE(value))
                r = R_TRUE; // any type matching counts
            else if (r == R_UNHANDLED)
                r = R_FALSE; // at least one type has to speak up now
        }
        else if (IS_TYPESET(var)) {
            if (TYPE_CHECK(var, VAL_TYPE(value)))
                r = R_TRUE; // any typeset matching counts
            else if (r == R_UNHANDLED)
                r = R_FALSE; // at least one type has to speak up now
        }
        else if (IS_ACTION(var)) {
            const REBOOL fully = TRUE;
            if (Apply_Only_Throws(D_OUT, fully, const_KNOWN(var), value, END))
                return R_OUT_IS_THROWN;

            if (IS_VOID(D_OUT))
                fail (Error_No_Return_Raw());

            if (IS_FALSEY(D_OUT))
                goto test_failed; // any function failing breaks it

            // At least one function matching tips the balance, but
            // can't alone outmatch no types matching, if any types
            // were matched at all.
            //
            if (r == R_UNHANDLED)
                r = R_TRUE;
            continue;
        }
        else
            fail (Error_Invalid_Type(VAL_TYPE(var)));
    }

    if (r == R_UNHANDLED) {
        //
        // !!! When the test is just [], what's that?  People aren't likely to
        // write it literally, but it could happen from a COMPOSE or similar.
        //
        fail ("No tests found in EITHER-TEST.");
    }

    if (r == R_FALSE) {
        //
        // This means that some types didn't match and were not later
        // redeemed by a type that did match.  Consider it failure.
        //
        goto test_failed;
    }

    // Someone spoke up for test success and was not overridden.
    //
    assert(r == R_TRUE);
    Move_Value(D_OUT, ARG(value));
    return R_OUT;

test_failed:
    if (Run_Branch_Throws(D_OUT, ARG(value), ARG(branch), REF(only)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  either-test-void: native [
//
//  {If value is void, return void, otherwise take the branch.}
//
//      return: [<opt> any-value!]
//          {Void if input is void, or branch result (BLANK! if void)}
//      value [<opt> any-value!]
//      branch [block! action!]
//      /only
//          "If branch runs and returns void, do not convert it to BLANK!"
//  ]
//
REBNATIVE(either_test_void)
//
// Native optimization of `specialize 'either-test-value [test: :void?]`
// Worth it to write because this is the functionality enfixed as ALSO.
{
    INCLUDE_PARAMS_OF_EITHER_TEST_VOID;

    if (IS_VOID(ARG(value)))
        return R_VOID;

    if (Run_Branch_Throws(D_OUT, ARG(value), ARG(branch), REF(only)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  either-test-value: native [
//
//  {If value is not void, return the value, otherwise take the branch.}
//
//      return: [<opt> any-value!]
//          {Input value if not void, or branch result (BLANK! if void)}
//      value [<opt> any-value!]
//      branch [block! action!]
//      /only
//          "If branch runs and returns void, do not convert it to BLANK!"
//  ]
//
REBNATIVE(either_test_value)
//
// Native optimization of `specialize 'either-test-value [test: :any-value?]`
// Worth it to write because this is the functionality enfixed as ELSE.
{
    INCLUDE_PARAMS_OF_EITHER_TEST_VALUE;

    if (not IS_VOID(ARG(value))) {
        Move_Value(D_OUT, ARG(value));
        return R_OUT;
    }

    if (Run_Branch_Throws(D_OUT, ARG(value), ARG(branch), REF(only)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  all: native [
//
//  {Short-circuiting variant of AND, using a block of expressions as input.}
//
//      return: [<opt> any-value!]
//          {Product of last evaluation if all TRUE?, else a BLANK! value.}
//      block [block!]
//          "Block of expressions.  Void evaluations are ignored."
//      /only
//          "Ignore void evaluations, and return void if any falsey values"
//  ]
//
REBNATIVE(all)
{
    INCLUDE_PARAMS_OF_ALL;

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    if (REF(only)) {
        //
        // The variation which tolerates voids wants `all/only [10 ()] = 10`.
        // This requires calculating into an intermediate cell and only moving
        // it to the potential result if it's not void.

        Init_Void(D_OUT);

        while (FRM_HAS_MORE(f)) {
            if (Do_Next_In_Frame_Throws(D_CELL, f)) {
                Drop_Frame(f);
                Move_Value(D_OUT, D_CELL);
                return R_OUT_IS_THROWN;
            }

            if (IS_VOID(D_CELL)) // voids do not "vote" true or false
                continue;

            if (IS_FALSEY(D_CELL)) { // failure signified with BLANK!
                Drop_Frame(f);
                return R_BLANK;
            }

            Move_Value(D_OUT, D_CELL); // preserve (later voids won't erase)
        }
    }
    else {
        // If not trying to preserve the last result in case of void, then
        // less copying can be done, so a faster loop is used.

        Init_Bar(D_OUT);

        while (FRM_HAS_MORE(f)) {
            if (Do_Next_In_Frame_Throws(D_OUT, f)) {
                Drop_Frame(f);
                return R_OUT_IS_THROWN;
            }

            if (IS_VOID(D_OUT)) // illegal in plain ALL
                fail (Error_No_Return_Raw());

            if (IS_FALSEY(D_OUT)) { // failure signified with BLANK!
                Drop_Frame(f);
                return R_BLANK;
            }
        }
    }

    Drop_Frame(f);
    return R_OUT;
}


//
//  any: native [
//
//  {Short-circuiting version of OR, using a block of expressions as input.}
//
//      return: [<opt> any-value!]
//          {First truthy evaluative result, or BLANK! value if all falsey}
//      block [block!]
//          "Block of expressions."
//      /only
//          "Ignore void evaluations, and return void if no truthy values"
//  ]
//
REBNATIVE(any)
{
    INCLUDE_PARAMS_OF_ANY;

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    REBOOL voted = FALSE;

    while (FRM_HAS_MORE(f)) {
        if (Do_Next_In_Frame_Throws(D_OUT, f)) {
            Drop_Frame(f);
            return R_OUT_IS_THROWN;
        }

        if (IS_VOID(D_OUT)) {
            if (REF(only)) // voids do not "vote" true or false
                continue;

            fail (Error_No_Return_Raw());
        }

        if (IS_TRUTHY(D_OUT)) { // successful ANY returns the value
            Drop_Frame(f);
            return R_OUT;
        }

        voted = TRUE; // signal at least one non-void result was seen
    }

    Drop_Frame(f);

    if (voted or not REF(only))
        return R_BLANK;

    return R_VOID; // all opt-outs return void if /ONLY
}


//
//  none: native [
//
//  {Short circuiting version of NOR, using a block of expressions as input.}
//
//      return: [<opt> bar! blank!]
//          {TRUE if all expressions are FALSE?, or BLANK if any are TRUE?}
//      block [block!]
//          "Block of expressions."
//      /only
//          "Ignore void evaluations, and return void if all void"
//  ]
//
REBNATIVE(none)
//
// !!! In order to reduce confusion and accidents in the near term, the
// %mezz-legacy.r renames this to NONE-OF and makes NONE report an error.
{
    INCLUDE_PARAMS_OF_NONE;

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    REBOOL voted = FALSE;

    while (FRM_HAS_MORE(f)) {
        if (Do_Next_In_Frame_Throws(D_OUT, f)) {
            Drop_Frame(f);
            return R_OUT_IS_THROWN;
        }

        if (IS_VOID(D_OUT)) {
            if (REF(only)) // voids do not "vote" true or false
                continue;

            fail (Error_No_Return_Raw());
        }

        if (IS_TRUTHY(D_OUT)) { // any true results mean failure
            Drop_Frame(f);
            return R_BLANK;
        }

        voted = TRUE; // signal that at least one non-void result was seen
    }

    Drop_Frame(f);

    if (voted or not REF(only))
        return R_BAR;

    return R_VOID; // all opt-outs
}


// Shared code for CASE (which runs BLOCK! clauses as code) and CHOOSE (which
// returns values as-is, e.g. `choose [true [print "hi"]]` => `[print "hi]`
//
static REB_R Case_Choose_Core(
    REBVAL *out,
    REBVAL *cell, // scratch "D_CELL", GC safe (and implicitly terminated)
    REBVAL *block, // "choices" or "cases", GC safe
    REBOOL all,
    REBOOL only,
    REBOOL choose // do not evaluate blocks, just "choose" them
){
    DECLARE_FRAME (f);
    Push_Frame(f, block);

    // With the block argument pushed in the enumerator, that frame slot is
    // available for scratch space in the rest of the routine.

    while (FRM_HAS_MORE(f)) {
        if (IS_BAR(f->value)) { // interstitial BAR! legal, `case [1 2 | 3 4]`
            Fetch_Next_In_Frame(f);
            continue;
        }

        // Perform a DO/NEXT's worth of evaluation on a "condition" to test

        if (Do_Next_In_Frame_Throws(cell, f)) {
            Move_Value(out, cell);
            Drop_Frame(f);
            return R_OUT_IS_THROWN;
        }

        // No void conditions allowed--as with IF.  But "invisibles" such as
        // COMMENT, ELIDE, DUMP, etc. may be used (in which case, we'd not
        // even see them here, the evaluator "elides" them during operation.)
        //
        if (IS_VOID(cell))
            fail (Error_No_Return_Raw());

        if (FRM_AT_END(f)) // require conditions and branches in pairs
            fail (Error_Past_End_Raw());

        if (IS_BAR(f->value)) // BAR! out of sync between condition and branch
            fail (Error_Bar_Hit_Mid_Case_Raw());

        if (IS_CONDITIONAL_FALSE(cell)) {
            //
            // The condition did not match.  If it's a CHOOSE operation, we
            // willingly skip any kind of value in the next slot.  For a
            // CASE be more picky--skip blocks and literal ACTION! values,
            // and soft quoted things, but error otherwise.
            //
            // !!! We want to skip evaluating GROUP!s for false clauses, but
            // should GET-PATH! and GET-WORD! be looked up to see if they are
            // BLOCK! or ACTION!?
            //
            if (
                choose
                or IS_BLOCK(f->value) or IS_ACTION(f->value)
                or IS_QUOTABLY_SOFT(f->value)
            ){
                Fetch_Next_In_Frame(f); // skip the soft-quoted slot
                continue;
            }
            fail (Error_Invalid_Core(f->value, f->specifier));
        }

        // Condition matched.  We only look at one value for the "branch" or
        // "choice".  However, this is soft-quoted, so if it's a GROUP! or a
        // GET-WORD! or a GET-PATH!, we're willing to evaluate it.
        //
        if (choose) {
            //
            // CHOOSE can evaluate directly into the output slot.
            //
            if (IS_QUOTABLY_SOFT(f->value)) {
                if (Eval_Value_Core_Throws(out, f->value, f->specifier)) {
                    Drop_Frame(f);
                    return R_OUT_IS_THROWN;
                }
            } else
                Derelativize(out, f->value, f->specifier);
        }
        else {
            // We need to hang onto the condition, in case the branch is an
            // arity-1 ACTION! and wants to be passed what that condition
            // evaluated to.  Move it into the block cell, which we no longer
            // need (the frame captured it).  Note that evaluating directly
            // into frame slots is not allowed.
            //
            Move_Value(block, cell); // only needed for CASE, not CHOOSE
            if (Eval_Value_Core_Throws(cell, f->value, f->specifier)) {
                Move_Value(out, cell);
                Drop_Frame(f);
                return R_OUT_IS_THROWN;
            }

            if (not IS_ACTION(cell) and not IS_BLOCK(cell))
                fail (Error_Invalid_Arg_Raw(cell));

            // Note that block now holds the cached evaluated condition
            //
            if (Run_Branch_Throws(out, block, cell, only)) {
                Drop_Frame(f);
                return R_OUT_IS_THROWN;
            }
        }

        if (not all) {
            Drop_Frame(f);
            return R_OUT;
        }

        // keep matching if /ALL
        //
        Fetch_Next_In_Frame(f);
    }

    // CASE/ALL can get here even if D_OUT not written

    Drop_Frame(f);
    return R_OUT_VOID_IF_UNWRITTEN;
}


//
//  case: native [
//
//  {Evaluates each condition, and when true, evaluates what follows it.}
//
//      return: [<opt> any-value!]
//          {Last matched case evaluation, or void if no cases matched}
//      cases [block!]
//          "Block of cases (conditions followed by branches)"
//      /all
//          {Evaluate all cases (do not stop at first TRUTHY? case)}
//      /only
//          "If branch runs and returns void, do not convert it to BLANK!"
//  ]
//
REBNATIVE(case)
{
    INCLUDE_PARAMS_OF_CASE;

    const REBOOL choose = FALSE;
    return Case_Choose_Core(
        D_OUT, D_CELL, ARG(cases), REF(all), REF(only), choose
    );
}


//
//  choose: native [
//
//  {Evaluates each condition, and gives back the value that follows it}
//
//      return: [<opt> any-value!]
//          {Last matched choice value, or void if no choices matched}
//      choices [block!]
//          {Evaluate all choices (do not stop at first TRUTHY? choice)}
//      /all
//          {Return the value for the last matched choice (instead of first)}
//  ]
//
REBNATIVE(choose)
{
    INCLUDE_PARAMS_OF_CHOOSE;

    // There's no need to worry about "blankification" here, though the value
    // might be void.  For now assume that means it's not a valid choice,
    // and give an error.  Review.
    //
    const REBOOL only = FALSE;

    const REBOOL choose = TRUE;
    return Case_Choose_Core(
        D_OUT, D_CELL, ARG(choices), REF(all), only, choose
    );
}


//
//  switch: native [
//
//  {Selects a choice and evaluates the block that follows it.}
//
//      return: "Last case evaluation, or void if no cases matched"
//          [<opt> any-value!]
//      value "Target value"
//          [any-value!]
//      cases "Block of cases (comparison lists followed by block branches)"
//          [block!]
//      /default "Default case if no others found"
//      default-case "Block to execute or function to run if no cases match"
//          [action! block!]
//      /all "Evaluate all matches (not just first one)"
//      /strict "Use STRICT-EQUAL? when comparing cases instead of EQUAL?"
//      /only "If branch runs and returns void, do not convert it to BLANK!"
//  ]
//
REBNATIVE(switch)
//
// !!! SWITCH historically has had a /DEFAULT refinement.  However, with the
// rise of the void-means-no-result convention and THEN and ELSE, it is
// somewhat inelegant.  Consider removing it, when a suitable way to let
// users create expanded versions of functions with their own refinements
// exists, so that creating compatibility can be easy/performant.
{
    INCLUDE_PARAMS_OF_SWITCH;

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(cases));

    assert(IS_END(D_OUT)); // evaluator guarantees this on entry

    REBVAL *value = ARG(value);

    if (IS_BLOCK(value) and GET_VAL_FLAG(value, VALUE_FLAG_UNEVALUATED))
        fail (Error_Block_Switch_Raw(value)); // `switch [x] [...]` safeguard

    // D_CELL is a temporary GC-safe location.  Initialize void, as it holds
    // the last test so that `switch 9 [1 ["a"] 2 ["b"] "c"]` is "c"

    Init_Void(D_CELL); // used for "fallout"

    while (FRM_HAS_MORE(f)) {
        //
        // If a branch is seen at this point, it doesn't correspond to any
        // condition to match.  If no more tests are run, let it suppress the
        // feature of the last value "falling out" the bottom of the switch
        //
        if (
            IS_BLOCK(f->value)
            || IS_ACTION(f->value) // literal ACTION!, likely COMPOSE'd in
        ){
            Init_Void(D_CELL);
            Fetch_Next_In_Frame(f);
            continue;
        }

        // GROUP!, GET-WORD! and GET-PATH! are evaluated in Ren-C's SWITCH
        // All other types are seen as-is (hence words act "quoted")
        //
        if (IS_QUOTABLY_SOFT(f->value)) {
            if (Eval_Value_Core_Throws(D_CELL, f->value, f->specifier)) {
                Move_Value(D_OUT, D_CELL);
                Drop_Frame(f);
                return R_OUT_IS_THROWN;
            }
        }
        else
            Derelativize(D_CELL, f->value, f->specifier);

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
        // !!! A branch composed into the switch cases block may want to see
        // the un-mutated condition value, in which case this should not
        // be changing D_CELL

        if (!Compare_Modify_Values(ARG(value), D_CELL, REF(strict) ? 1 : 0)) {
            Fetch_Next_In_Frame(f);
            continue;
        }

        // Skip ahead to try and find a block, to treat as code for the match

        do {
            Fetch_Next_In_Frame(f);
            if (FRM_AT_END(f))
                goto return_defaulted;
        } while (not IS_BLOCK(f->value) and not IS_ACTION(f->value));

        // Run the code if it was found.  Because it writes D_OUT with a value
        // (or void), it won't be END--we'll know at least one case has run.
        //
        // Derelativize the ACTION! or BLOCK! into the cases cell, which is
        // available because the frame already captured it.
        //
        // !!! We only have to derelativize because we're not using plain
        // Do_At_Throws()...which takes a specifier.  If the literal-ACTION!
        // in the cases feature turns out to be superfluous, use that instead.
        //
        Derelativize(ARG(cases), f->value, f->specifier);
        if (Run_Branch_Throws(D_OUT, D_CELL, ARG(cases), REF(only))) {
            Drop_Frame(f);
            return R_OUT_IS_THROWN;
        }

        // Only keep processing if the /ALL refinement was specified

        if (not REF(all)) {
            Drop_Frame(f);
            return R_OUT;
        }
    }

    if (NOT_END(D_OUT)) { // at least one case body ran and overwrote D_OUT
        Drop_Frame(f);
        return R_OUT;
    }

return_defaulted:
    assert(IS_END(D_OUT)); // nothing should have been written into D_OUT

    Drop_Frame(f);

    if (not REF(default)) {
        Move_Value(D_OUT, D_CELL); // last test "falls out", might be void
        return R_OUT;
    }

    // The default branch is run, but the condition triggering it is said to
    // be a void.  Hence if the default case is a single-arity function, that
    // is the argument it will be receiving.  (Loops like FOREVER pass in
    // END, so only single-arity functions can be used, but by using void
    // here it allows a common function to take the default.)
    //
    if (Run_Branch_Throws(D_OUT, VOID_CELL, ARG(default_case), REF(only)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  catch: native [
//
//  {Catches a throw from a block and returns its value.}
//
//      return: [<opt> any-value!]
//      block "Block to evaluate"
//          [block!]
//      /name "Catches a named throw" ;-- should it be called /named ?
//      names "Names to catch (single name if not block)"
//          [block! word! action! object!]
//      /quit "Special catch for QUIT native"
//      /any "Catch all throws except QUIT (can be used with /QUIT)"
//      /with "Handle thrown case with code"
//      handler "If action, the spec matches [value name]"
//          [block! action!]
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

    if (Do_Any_Array_At_Throws(D_OUT, ARG(block))) {
        if (REF(any) and not (
            IS_ACTION(D_OUT)
            and VAL_ACT_DISPATCHER(D_OUT) == &N_quit
        )){
            goto was_caught;
        }

        if (REF(quit) and (
            IS_ACTION(D_OUT)
            and VAL_ACT_DISPATCHER(D_OUT) == &N_quit
        )){
            goto was_caught;
        }

        if (REF(name)) {
            //
            // We use equal? by way of Compare_Modify_Values, and re-use the
            // refinement slots for the mutable space

            REBVAL *temp1 = ARG(quit);
            REBVAL *temp2 = ARG(any);

            // !!! The reason we're copying isn't so the VALUE_FLAG_THROWN bit
            // won't confuse the equality comparison...but would it have?

            if (IS_BLOCK(ARG(names))) {
                //
                // Test all the words in the block for a match to catch

                RELVAL *candidate = VAL_ARRAY_AT(ARG(names));
                for (; NOT_END(candidate); candidate++) {
                    //
                    // !!! Should we test a typeset for illegal name types?
                    //
                    if (IS_BLOCK(candidate))
                        fail (Error_Invalid(ARG(names)));

                    Derelativize(temp1, candidate, VAL_SPECIFIER(ARG(names)));
                    Move_Value(temp2, D_OUT);

                    // Return the THROW/NAME's arg if the names match
                    // !!! 0 means equal?, but strict-equal? might be better
                    //
                    if (Compare_Modify_Values(temp1, temp2, 0))
                        goto was_caught;
                }
            }
            else {
                Move_Value(temp1, ARG(names));
                Move_Value(temp2, D_OUT);

                // Return the THROW/NAME's arg if the names match
                // !!! 0 means equal?, but strict-equal? might be better
                //
                if (Compare_Modify_Values(temp1, temp2, 0))
                    goto was_caught;
            }
        }
        else {
            // Return THROW's arg only if it did not have a /NAME supplied
            //
            if (IS_BLANK(D_OUT))
                goto was_caught;
        }

        // Throw name is in D_OUT, thrown value is held task local
        //
        return R_OUT_IS_THROWN;
    }

    return R_OUT;

was_caught:
    if (REF(with)) {
        REBVAL *handler = ARG(handler);

        // We again re-use the refinement slots, but this time as mutable
        // space protected from GC for the handler's arguments
        //
        REBVAL *thrown_arg = ARG(any);
        REBVAL *thrown_name = ARG(quit);

        CATCH_THROWN(thrown_arg, D_OUT);
        Move_Value(thrown_name, D_OUT); // THROWN bit cleared by CATCH_THROWN

        if (IS_BLOCK(handler)) {
            //
            // There's no way to pass args to a block (so just DO it)
            //
            if (Do_Any_Array_At_Throws(D_OUT, ARG(handler)))
                return R_OUT_IS_THROWN;

            return R_OUT;
        }
        else if (IS_ACTION(handler)) {
            //
            // This calls the function but only does a DO/NEXT.  Hence the
            // function might be arity 0, arity 1, or arity 2.  If it has
            // greater arity it will process more arguments.
            //
            if (Apply_Only_Throws(
                D_OUT,
                FALSE, // do not alert if handler doesn't consume all args
                handler,
                thrown_arg,
                thrown_name,
                END
            )){
                return R_OUT_IS_THROWN;
            }

            return R_OUT;
        }
    }

    // If no handler, just return the caught thing
    //
    CATCH_THROWN(D_OUT, D_OUT);

    return R_OUT;
}


//
//  throw: native [
//
//  "Throws control back to a previous catch."
//
//      value "Value returned from catch"
//          [<opt> any-value!]
//      /name "Throws to a named catch"
//      name-value [word! action! object!]
//  ]
//
REBNATIVE(throw)
//
// Choices are currently limited for what one can use as a "name" of a THROW.
// Note blocks as names would conflict with the `name_list` feature in CATCH.
//
// !!! Should parameters be /NAMED and NAME ?
{
    INCLUDE_PARAMS_OF_THROW;

    REBVAL *value = ARG(value);

    if (IS_ERROR(value)) {
        //
        // We raise an alert from within the implementation of throw for
        // trying to use it to trigger errors, because if THROW just didn't
        // take errors in the spec it wouldn't guide what *to* use.
        //
        fail (Error_Use_Fail_For_Error_Raw(value));

        // Note: Caller can put the ERROR! in a block or use some other
        // such trick if it wants to actually throw an error.
        // (Better than complicating via THROW/ERROR-IS-INTENTIONAL!)
    }

    if (REF(name))
        Move_Value(D_OUT, ARG(name_value));
    else {
        // Blank values serve as representative of THROWN() means "no name"
        //
        Init_Blank(D_OUT);
    }

    CONVERT_NAME_TO_THROWN(D_OUT, value);
    return R_OUT_IS_THROWN;
}
