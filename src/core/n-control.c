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
// * If they do not run any branches, they evaluate to null ("unset!") and not
//   a BLANK! ("none!").  If a branch *does* run, and evaluates to null, then
//   the result is altered to be BLANK!.  Hence null can cue other functions
//   (like THEN and ELSE) to be sure no branch ran, and respond appropriately.
//
// * It is possible to ask the branch return result to not be "blankified",
//   but give back nulls as-is, with the /OPT refinement.  This is specialized
//   as functions ending in *.  (IF*, EITHER*, CASE*, SWITCH*...)
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
//      return: "null if branch not run, otherwise branch result"
//          [<opt> any-value!]
//      condition [any-value!]
//      branch "If arity-1 ACTION!, receives the evaluated condition"
//          [block! action!]
//      /opt "If branch runs and produces null, don't convert it to a BLANK!"
//  ]
//
REBNATIVE(if)
{
    INCLUDE_PARAMS_OF_IF;

    if (IS_CONDITIONAL_FALSE(ARG(condition)))
        return R_VOID;

    if (Run_Branch_Throws(D_OUT, ARG(condition), ARG(branch), REF(opt)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  if-not: native [
//
//  {When TO-LOGIC CONDITION is false, execute branch}
//
//      return: "null if branch not run, otherwise branch result"
//          [<opt> any-value!]
//      condition [any-value!]
//      branch [block! action!]
//      /opt "If branch runs and produces null, don't convert it to a BLANK!"
//  ]
//
REBNATIVE(if_not)
{
    INCLUDE_PARAMS_OF_IF_NOT;

    if (IS_CONDITIONAL_TRUE(ARG(condition)))
        return R_VOID;

    if (Run_Branch_Throws(D_OUT, ARG(condition), ARG(branch), REF(opt)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  either: native [
//
//  {Choose a branch to execute, based on TO-LOGIC of the CONDITION value}
//
//      return: [<opt> any-value!]
//      condition [any-value!]
//      true-branch "If arity-1 ACTION!, receives the evaluated condition"
//          [block! action!]
//      false-branch [block! action!]
//      /opt "If branch runs and produces null, don't convert it to a BLANK!"
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
        REF(opt)
    )){
        return R_OUT_IS_THROWN;
    }

    return R_OUT;
}


//  Either_Test_Core: C
//
// Note: There was an idea of turning the `test` BLOCK! into some kind of
// dialect.  That was later supplanted by idea of MATCH...which bridges with
// a natural interface to functions like PARSE for providing such dialects.
// This routine is just for basic efficiency behind constructs like ELSE
// that want to avoid frame creation overhead.  So BLOCK! just means typeset.
//
inline static REB_R Either_Test_Core(
    REBVAL *cell, // GC-safe temp cell
    REBVAL *test, // modified
    const REBVAL *par,
    const REBVAL *arg
){
    assert(IS_TYPESET(par));

    switch (VAL_TYPE(test)) {

    case REB_LOGIC: { // test for "truthy" or "falsey"
        if (IS_VOID(arg)) { // null is neither true nor false
            DECLARE_LOCAL (word);
            Init_Word(word, VAL_PARAM_SPELLING(par));
            fail (Error_No_Value(word));
        }

        // If this is the result of composing together a test with a literal,
        // it may be the *test* that changes...so in effect, we could be
        // "testing the test" on a fixed value.  Allow literal blocks (e.g.
        // use IS_TRUTHY() instead of IS_CONDITIONAL_TRUE())
        //
        return R_FROM_BOOL(VAL_LOGIC(test) == IS_TRUTHY(arg)); }

    case REB_WORD:
    case REB_PATH: {
        //
        // !!! Because we do not push refinements here, this means that a
        // specialized action will be generated if the user says something
        // like `either-test 'foo?/bar x [...]`.  It's possible to avoid
        // this by pushing a frame before the Get_If_Word_Or_Path_Throws()
        // and gathering the refinements on the stack, but a bit more work
        // for an uncommon case...revisit later.
        //
        const REBOOL push_refinements = FALSE;

        REBSTR *opt_label = NULL;
        REBDSP lowest_ordered_dsp = DSP;
        if (Get_If_Word_Or_Path_Throws(
            cell,
            &opt_label,
            test,
            SPECIFIED,
            push_refinements
        )){
            return R_OUT_IS_THROWN;
        }

        assert(lowest_ordered_dsp == DSP); // would have made specialization
        UNUSED(lowest_ordered_dsp);

        Move_Value(test, cell);

        if (not IS_ACTION(test))
            fail ("EITHER-TEST only takes WORD! and PATH! for ACTION! vars");
        goto handle_action; }

    case REB_ACTION: {

    handle_action:;

        if (Apply_Only_Throws(
            cell,
            TRUE, // `fully` (ensure argument consumed)
            test,
            DEVOID(arg), // convert void cells to C nullptr for API
            END
        )){
            return R_OUT_IS_THROWN;
        }

        if (IS_VOID(cell))
            fail (Error_No_Return_Raw());

        return R_FROM_BOOL(IS_TRUTHY(cell)); }

    case REB_DATATYPE: {
        return R_FROM_BOOL(VAL_TYPE_KIND(test) == VAL_TYPE(arg)); }

    case REB_TYPESET: {
        return R_FROM_BOOL(TYPE_CHECK(test, VAL_TYPE(arg))); }

    case REB_BLOCK: {
        RELVAL *item = VAL_ARRAY_AT(test);
        if (IS_END(item)) {
            //
            // !!! If the test is just [], what's that?  People aren't likely
            // to write it literally, but COMPOSE/etc. might make it.
            //
            fail ("No tests found BLOCK! passed to EITHER-TEST.");
        }

        REBSPC *specifier = VAL_SPECIFIER(test);
        for (; NOT_END(item); ++item) {
            const RELVAL *var
                = IS_WORD(item)
                    ? Get_Opt_Var_May_Fail(item, specifier)
                    : item;

            if (IS_DATATYPE(var)) {
                if (VAL_TYPE_KIND(var) == VAL_TYPE(arg))
                    return R_TRUE;
            }
            else if (IS_TYPESET(var)) {
                if (TYPE_CHECK(var, VAL_TYPE(arg)))
                    return R_TRUE;
            }
            else
                fail (Error_Invalid_Type(VAL_TYPE(var)));
        }
        return R_FALSE; }

    default:
        fail (Error_Invalid_Type(VAL_TYPE(arg)));
    }
}


//
//  either-test: native [
//
//  {If argument passes test, return it as-is, otherwise take the branch}
//
//      return: "Input argument if it matched, or branch result"
//          [<opt> any-value!]
//      test "Typeset membership, LOGIC! to test for truth, filter function"
//          [
//              word! path! action! ;-- arity-1 filter function, opt named
//              datatype! typeset! block! ;-- typeset specification forms
//              logic! ;-- tests TO-LOGIC compatibility
//          ]
//      arg [<opt> any-value!]
//      branch "If arity-1 ACTION!, receives the non-matching argument"
//          [block! action!]
//      /opt "If branch runs and produces null, don't convert it to a BLANK!"
//  ]
//
REBNATIVE(either_test)
{
    INCLUDE_PARAMS_OF_EITHER_TEST;

    REB_R r = Either_Test_Core(D_OUT, ARG(test), PAR(arg), ARG(arg));
    if (r == R_OUT_IS_THROWN)
        return R_OUT_IS_THROWN;

    if (r == R_TRUE) {
        Move_Value(D_OUT, ARG(arg));
        return R_OUT;
    }

    assert(r == R_FALSE);

    if (Run_Branch_Throws(D_OUT, ARG(arg), ARG(branch), REF(opt)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  either-test-null: native [
//
//  {If argument is null, return null, otherwise take the branch}
//
//      return: {null if input is null, or branch result}
//          [<opt> any-value!]
//      arg [<opt> any-value!]
//      branch "If arity-1 ACTION!, receives value that triggered branch"
//          [block! action!]
//      /opt "If branch runs and produces null, don't convert it to a BLANK!"
//  ]
//
REBNATIVE(either_test_null)
//
// Native optimization of `specialize 'either-test-value [test: :null?]`
// Worth it to write because this is the functionality enfixed as ALSO.
{
    INCLUDE_PARAMS_OF_EITHER_TEST_NULL;

    if (IS_VOID(ARG(arg))) // Either_Test_Core() would call Apply()
        return R_VOID;

    if (Run_Branch_Throws(D_OUT, ARG(arg), ARG(branch), REF(opt)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  either-test-value: native [
//
//  {If argument is not null, return the value, otherwise take the branch}
//
//      return: {Input value if not null, or branch result}
//          [<opt> any-value!]
//      arg [<opt> any-value!]
//      branch [block! action!]
//      /opt "If branch runs and produces null, don't convert it to a BLANK!"
//  ]
//
REBNATIVE(either_test_value)
//
// Native optimization of `specialize 'either-test-value [test: :any-value?]`
// Worth it to write because this is the functionality enfixed as ELSE.
{
    INCLUDE_PARAMS_OF_EITHER_TEST_VALUE;

    if (not IS_VOID(ARG(arg))) { // Either_Test_Core() would call Apply()
        Move_Value(D_OUT, ARG(arg));
        return R_OUT;
    }

    if (Run_Branch_Throws(D_OUT, ARG(arg), ARG(branch), REF(opt)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  match: native [
//
//  {Check value using tests (match types, TRUE or FALSE, or filter action)}
//
//      return: "Input argument if it matched, otherwise blank"
//          [<opt> any-value!]
//      'test "Typeset membership, LOGIC! to test for truth, filter function"
//          [
//              word! path! ;- special "first-arg-stealing" magic
//              lit-word! lit-path! ;-- like EITHER-TEST's WORD! and PATH!
//              datatype! typeset! block! logic! action! ;-- like EITHER-TEST
//          ]
//      :args [any-value! <...>]
//  ]
//
REBNATIVE(match)
//
// This routine soft quotes its `test` argument, and has to be variadic, in
// order to get the special `MATCH PARSE "AAA" [SOME "A"]` -> "AAA" behavior.
// But despite quoting its first argument, it processes it in a way to try
// and mimic EITHER-TEST for compatibility for other cases.
{
    INCLUDE_PARAMS_OF_MATCH;

    REBVAL *test = ARG(test);

    switch (VAL_TYPE(test)) {

    case REB_LIT_WORD:
    case REB_LIT_PATH: {
        if (NOT_VAL_FLAG(test, VALUE_FLAG_UNEVALUATED)) // soft quote eval'd
            fail (Error_Invalid(test)); // disallow `MATCH (QUOTE 'NULL?) ...`

        if (IS_LIT_WORD(test))
            VAL_SET_TYPE_BITS(test, REB_WORD);
        else
            VAL_SET_TYPE_BITS(test, REB_PATH);
        goto either_test; }

    case REB_WORD:
    case REB_PATH: {
        if (NOT_VAL_FLAG(test, VALUE_FLAG_UNEVALUATED)) // soft quote eval'd
            goto either_test; // allow `MATCH ('NULL?) ...`

        REBSTR *opt_label = NULL;
        REBDSP lowest_ordered_dsp = DSP;
        if (Get_If_Word_Or_Path_Throws(
            D_OUT,
            &opt_label,
            test,
            SPECIFIED,
            TRUE // push_refinements
        )){
            return R_OUT_IS_THROWN;
        }

        Move_Value(test, D_OUT);

        if (not IS_ACTION(test)) {
            if (ANY_WORD(test) or ANY_PATH(test))
                fail (Error_Invalid(test)); // disallow `X: 'Y | MATCH X ...`
            goto either_test; // will typecheck the result
        }

        // It was a non-soft quote eval'd word, the kind we want to give the
        // "magical" functionality to.
        //
        // We run the testing function in place in a way that appears "normal"
        // but actually captures its first argument.  That will be MATCH's
        // return value if the filter function returns a truthy result.

        REBVAL *first_arg;
        REBCTX *exemplar;
        if (Make_Invocation_Frame_Throws( // !!! currently hacky/inefficient
            D_OUT,
            &exemplar,
            &first_arg,
            test,
            PAR(args),
            ARG(args),
            lowest_ordered_dsp
        )){
            return R_OUT_IS_THROWN;
        }

        if (not first_arg)
            fail ("MATCH with a function pattern must take at least 1 arg");

        Move_Value(D_OUT, first_arg); // steal first argument before call

        DECLARE_FRAME (f);

        f->out = D_CELL;

        Push_Frame_For_Apply(f);

        Push_Action(
            f,
            opt_label,
            VAL_ACTION(test),
            VAL_BINDING(test)
        );
        f->refine = ORDINARY_ARG;
        f->special = CTX_VARS_HEAD(exemplar); // override action's exemplar

        (*PG_Do)(f);

        Drop_Frame_Core(f); // !!! Drop_Frame() asserts f->eval_type as REB_0

        if (THROWN(D_CELL)) {
            Move_Value(D_OUT, D_CELL);
            return R_OUT_IS_THROWN;
        }

        assert(FRM_AT_END(f)); // we started at END_FLAG, can only throw

        if (IS_VOID(D_CELL)) // neither true nor false
            fail (Error_No_Return_Raw());

        // We still have the first argument from the filter call in D_OUT.

        // MATCH *wants* to pass through the argument on a match, but
        // won't do so if the argument was falsey, as that is misleading.
        // Instead it passes a BAR! back.

        if (IS_TRUTHY(D_CELL)) {
            if (IS_VOID_OR_FALSEY(D_OUT))
                return R_BAR;
            return R_OUT;
        }

        // Again... MATCH *wants* to return a BLANK! on a non-match.  But it
        // wants to help cue attention to the strange BAR! result and make
        // sure the caller knows to do some additional DID-ing or NOT-ing to
        // coerce the result if the value is falsey.  NULL will do that.

        if (IS_VOID_OR_FALSEY(D_OUT))
            return R_VOID;
        return R_BLANK; }

    default:
        break;
    }

either_test:;

    // For the "non-magic" cases that are handled by plain EITHER-TEST, call
    // through with the transformed test.  Just take one normal arg via
    // variadic.

    REBVAL *varpar = PAR(args);

    INIT_VAL_PARAM_CLASS(varpar, PARAM_CLASS_NORMAL); // !!! hack
    REB_R r = Do_Vararg_Op_May_Throw(D_OUT, ARG(args), VARARG_OP_TAKE);
    INIT_VAL_PARAM_CLASS(varpar, PARAM_CLASS_HARD_QUOTE);

    if (r == R_OUT_IS_THROWN)
        return R_OUT_IS_THROWN;

    if (r == R_END)
        fail ("Frame hack is written to need argument!");

    assert(r == R_OUT);

    // See notes above about why the arg is not simply passed through or
    // blanked in the void or falsey arg case.

    r = Either_Test_Core(D_CELL, test, varpar, D_OUT);
    if (r == R_OUT_IS_THROWN)
        return R_OUT_IS_THROWN;

    if (r == R_TRUE) {
        if (IS_VOID_OR_FALSEY(D_OUT))
            return R_BAR;
        return R_OUT;
    }

    assert(r == R_FALSE);

    if (IS_VOID_OR_FALSEY(D_OUT))
        return R_VOID;
    return R_BLANK;
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
                Abort_Frame(f);
                Move_Value(D_OUT, D_CELL);
                return R_OUT_IS_THROWN;
            }

            if (IS_VOID(D_CELL)) // voids do not "vote" true or false
                continue;

            if (IS_FALSEY(D_CELL)) { // failure signified with BLANK!
                Abort_Frame(f);
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
                Abort_Frame(f);
                return R_OUT_IS_THROWN;
            }

            if (IS_VOID(D_OUT)) // illegal in plain ALL
                fail (Error_No_Return_Raw());

            if (IS_FALSEY(D_OUT)) { // failure signified with BLANK!
                Abort_Frame(f);
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
            Abort_Frame(f);
            return R_OUT_IS_THROWN;
        }

        if (IS_VOID(D_OUT)) {
            if (REF(only)) // voids do not "vote" true or false
                continue;

            fail (Error_No_Return_Raw());
        }

        if (IS_TRUTHY(D_OUT)) { // successful ANY returns the value
            Abort_Frame(f);
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
            Abort_Frame(f);
            return R_OUT_IS_THROWN;
        }

        if (IS_VOID(D_OUT)) {
            if (REF(only)) // voids do not "vote" true or false
                continue;

            fail (Error_No_Return_Raw());
        }

        if (IS_TRUTHY(D_OUT)) { // any true results mean failure
            Abort_Frame(f);
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

        // Perform a DO/NEXT's worth of evaluation on a "condition" to test

        if (Do_Next_In_Frame_Throws(cell, f)) {
            Move_Value(out, cell);
            Abort_Frame(f);
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
                    Abort_Frame(f);
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
                Abort_Frame(f);
                return R_OUT_IS_THROWN;
            }

            if (not IS_ACTION(cell) and not IS_BLOCK(cell))
                fail (Error_Invalid_Arg_Raw(cell));

            // Note that block now holds the cached evaluated condition
            //
            if (Run_Branch_Throws(out, block, cell, only)) {
                Abort_Frame(f);
                return R_OUT_IS_THROWN;
            }
        }

        if (not all) {
            Abort_Frame(f);
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
//      return: "Last case evaluation, or null if no cases matched"
//          [<opt> any-value!]
//      value "Target value"
//          [<opt> any-value!]
//      cases "Block of cases (comparison lists followed by block branches)"
//          [block!]
//      /all "Evaluate all matches (not just first one)"
//      /strict "Use STRICT-EQUAL? when comparing cases instead of EQUAL?"
//      /opt "If branch runs and returns null, do not convert it to BLANK!"
//      /quote "Do not evaluate comparison values"
//
//      /default "Default case if no others found (deprecated, see ELSE)"
//      default-case "Block to execute or function to run if no cases match"
//          [action! block!]
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
            or IS_ACTION(f->value) // literal ACTION!, likely COMPOSE'd in
        ){
            Init_Void(D_CELL);
            Fetch_Next_In_Frame(f);
            continue;
        }

        if (REF(quote))
            Quote_Next_In_Frame(D_CELL, f);
        else {
            if (Eval_Value_Core_Throws(D_CELL, f->value, f->specifier)) {
                Move_Value(D_OUT, D_CELL);
                Abort_Frame(f);
                return R_OUT_IS_THROWN;
            }

            Fetch_Next_In_Frame(f);
        }

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

        if (!Compare_Modify_Values(ARG(value), D_CELL, REF(strict) ? 1 : 0))
            continue;

        // Skip ahead to try and find a block, to treat as code for the match

        while (TRUE) {
            if (FRM_AT_END(f))
                goto return_defaulted;
            if (IS_BLOCK(f->value) or IS_ACTION(f->value))
                break;
            Fetch_Next_In_Frame(f);
        }

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
        if (Run_Branch_Throws(D_OUT, D_CELL, ARG(cases), REF(opt))) {
            Abort_Frame(f);
            return R_OUT_IS_THROWN;
        }

        // Only keep processing if the /ALL refinement was specified

        if (not REF(all)) {
            Abort_Frame(f);
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
    if (Run_Branch_Throws(D_OUT, VOID_CELL, ARG(default_case), REF(opt)))
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
                DEVOID(thrown_arg), // convert void cells to NULL for API
                DEVOID(thrown_name), // convert void cells to NULL for API
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
