//
//  File: %n-do.c
//  Summary: "native functions for DO, EVAL, APPLY"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// Ren-C's philosophy of DO is that the argument to it represents a place to
// find source code.  Hence `DO 3` does not evaluate to the number 3, any
// more than `DO "print hello"` would evaluate to `"print hello"`.  If a
// generalized evaluator is needed, use the special-purpose function EVAL.
//
// Note that although the code for running blocks and frames is implemented
// here as C, the handler for processing STRING!, FILE!, TAG!, URL!, etc. is
// dispatched out to some Rebol code.  See `system/intrinsic/do*`.
//

#include "sys-core.h"


//
//  eval: native [
//
//  {(Special) Process received value *inline* as the evaluator loop would.}
//
//      value [<opt> any-value!]
//          {BLOCK! passes-thru, FUNCTION! runs, SET-WORD! assigns...}
//      expressions [<opt> any-value! <...>]
//          {Depending on VALUE, more expressions may be consumed}
//      /only
//          {Suppress evaluation on any ensuing arguments value consumes}
//  ]
//
REBNATIVE(eval)
{
    INCLUDE_PARAMS_OF_EVAL;

    UNUSED(ARG(expressions)); // EVAL only *acts* variadic, uses R_REEVALUATE

    // The REEVALUATE instructions explicitly understand that the value to
    // do reevaluation of is held in the frame's f->cell.  (It would be unsafe
    // to evaluate something held in f->out.)
    //
    Move_Value(D_CELL, ARG(value));

    if (REF(only)) {
        //
        // We're going to tell the evaluator to switch into a "non-evaluating"
        // mode.  But we still want the eval cell itself to be treated
        // evaluatively despite that.  So flip its special evaluator bit.
        //
        SET_VAL_FLAG(D_CELL, VALUE_FLAG_EVAL_FLIP);
        return R_REEVALUATE_CELL_ONLY;
    }

    return R_REEVALUATE_CELL;
}


//
//  eval-enfix: native [
//
//  {Service routine for implementing ME (needs review/generalization)}
//
//      return: [<opt> any-value!]
//      left [<opt> any-value!]
//          {Value to preload as the left hand-argument (won't reevaluate)}
//      rest [varargs!]
//          {The code stream to execute (head element must be enfixed)}
//      /prefix
//          {Variant used when rest is prefix (e.g. for MY operator vs. ME)}
//  ]
//
REBNATIVE(eval_enfix)
//
// !!! Being able to write `some-var: me + 10` isn't as "simple" <ahem> as:
//
// * making ME a backwards quoting operator that fetches the value of some-var
// * quoting its next argument (e.g. +) to get a word looking up to a function
// * making the next argument variadic, and normal-enfix TAKE-ing it
// * APPLYing the quoted function on those two values
// * setting the left set-word (e.g. some-var:) to the result
//
// The problem with that strategy is that the parameter conventions of +
// matter.  Removing it from the evaluator and taking matters into one's own
// hands means one must reproduce the evaluator's logic--and that means it
// will probably be done poorly.  It's clearly not as sensible as having some
// way of slipping the value of some-var into the flow of normal evaluation.
//
// But generalizing this mechanic is...non-obvious.  It needs to be done, but
// this hacks up the specific case of "enfix with left hand side and variadic
// feed" by loading the given value into D_OUT and then re-entering the
// evaluator via the DO_FLAG_POST_SWITCH mechanic (which was actuallly
// designed for backtracking on enfix normal deferment.)
{
    INCLUDE_PARAMS_OF_EVAL_ENFIX;

    REBFRM *f;
    if (NOT(Is_Frame_Style_Varargs_May_Fail(&f, ARG(rest)))) {
        //
        // It wouldn't be *that* hard to support block-style varargs, but as
        // this routine is a hack to implement ME, don't make it any longer
        // than it needs to be.
        //
        fail ("EVAL-ENFIX is not made to support MAKE VARARGS! [...] rest");
    }

    if (FRM_AT_END(f) || VAL_TYPE(f->value) != REB_WORD) // no PATH! yet...
        fail ("ME and MY only work if right hand side starts with WORD!");

    if (f->gotten == END)
        f->gotten = Get_Opt_Var_Else_End(f->value, f->specifier);
    else 
        assert(f->gotten == Get_Opt_Var_Else_End(f->value, f->specifier));

    if (f->gotten == END || NOT(IS_FUNCTION(f->gotten)))
        fail ("ME and MY only work if right hand WORD! is a FUNCTION!");

    if (GET_VAL_FLAG(f->gotten, VALUE_FLAG_ENFIXED)) {
        if (REF(prefix))
            fail ("Use ME instead of MY with infix functions");

        // Already set up to work using our tricky technique.
    }
    else {
        if (NOT(REF(prefix)))
            fail ("Use MY instead of ME with prefix functions");

        // Here we do something devious.  We subvert the system by setting
        // f->gotten to an enfixed version of the function even if it is
        // not enfixed.  This lets us slip in a first argument to a function
        // *as if* it were enfixed, e.g. `series: my next`.
        //
        Move_Value(D_CELL, f->gotten);
        SET_VAL_FLAG(D_CELL, VALUE_FLAG_ENFIXED);
        f->gotten = D_CELL;
    }

    // Simulate as if the passed-in value was calculated into the output slot,
    // which is where enfix functions usually find their left hand values.
    //
    Move_Value(D_OUT, ARG(left));

    // We're kind-of-abusing an internal mechanism, where it is checked that
    // we are actually doing a deferment.  Try not to make that abuse break
    // the assertions in Do_Core.
    //
    // Note that while f may have a "prior" already, its prior will become
    // this frame...so when it asserts about "f->prior->deferred" it means
    // the frame of EVAL-ENFIX that is invoking it.
    //
    assert(FS_TOP->deferred == NULL);
    FS_TOP->deferred = m_cast(REBVAL*, BLANK_VALUE); // !!! signal our hack

    REBFLGS flags = DO_FLAG_FULFILLING_ARG | DO_FLAG_POST_SWITCH;
    if (Do_Next_In_Subframe_Throws(D_OUT, f, flags))
        return R_OUT_IS_THROWN;

    FS_TOP->deferred = NULL;

    return R_OUT;
}


//
//  Do_Or_Dont_Shared_Throws: C
//
REBOOL Do_Or_Dont_Shared_Throws(
    REBVAL *out,
    const REBVAL *source,
    REBFLGS flags,
    const REBVAL *var
){
    DECLARE_LOCAL (temp);

    REBVAL *position;
    if (IS_VARARGS(source)) {
        if (Is_Block_Style_Varargs(&position, source)) {
            //
            // We can execute the array, but we must "consume" elements out
            // of it (e.g. advance the index shared across all instances)
            // This is done by the REB_BLOCK case, which we jump to.
            //
            // !!! If any VARARGS! op does not honor the "locked" flag on the
            // array during execution, there will be problems if it is TAKE'n
            // or DO'd while this operation is in progress.
            //
            goto do_and_update_position;
        }

        REBFRM *f;
        if (NOT(Is_Frame_Style_Varargs_May_Fail(&f, source)))
            panic(source); // Frame is the only other type

        // Pretty much by definition, we are in the middle of a function call
        // in the frame the varargs came from.  It's still on the stack, and
        // we don't want to disrupt its state.  Use a subframe.
        //
        if (FRM_AT_END(f))
            Init_Void(out);
        else if (Do_Next_In_Subframe_Throws(out, f, flags))
            return TRUE;

        // The variable passed in /NEXT is just set to the vararg itself,
        // which has its positioning updated automatically by virtue of the
        // evaluation performing a "consumption" of VARARGS! content.
        //
        if (NOT(IS_BLANK(var)))
            Move_Value(Sink_Var_May_Fail(var, SPECIFIED), source);

        return FALSE;
    }
    else {
        Move_Value(temp, source);
        position = temp;
    }

do_and_update_position:
    assert(IS_BLOCK(position) || IS_GROUP(position));

    REBIXO indexor = Do_Array_At_Core(
        out,
        NULL, // no opt_head, start with value at array index
        VAL_ARRAY(position),
        VAL_INDEX(position),
        VAL_SPECIFIER(position),
        flags // may have FRAME_FLAG_NEUTRAL or FRAME_FLAG_TO_END set
    );

    if (indexor == THROWN_FLAG) {
        //
        // !!! The relationship between throwing and erroring and VARARGS!
        // is not totally clear when they originate from a BLOCK!, because
        // the block isn't tied to any frame lifetime.  But a FRAME!-based
        // varargs can't be used after a throw or error, so they probably
        // shouldn't be usable either.
        //
        Init_Unreadable_Blank(position);
        return TRUE;
    }

    // "continuation" of block...turn END_FLAG into the end so it can test
    // TAIL? as true to know the evaluation finished.
    //
    // !!! Is there merit to setting to BLANK! instead?  Easier to test
    // and similar to FIND.  On the downside, "lossy" in that after the
    // DOs are finished the var can't be used to recover the series again,
    // you'd have to save it.
    //
    // Note: While `source` is a local by default, if we jump here via
    // `goto do_block_source`, it will be a singular array inside a varargs,
    // whose position being updated after evaluation is important.
    //
    if (indexor == END_FLAG)
        VAL_INDEX(position) = VAL_LEN_HEAD(position);
    else
        VAL_INDEX(position) = cast(REBCNT, indexor) - 1; // one past

    if (NOT(IS_BLANK(var))) {
        if (IS_VARARGS(source))
            Move_Value(Sink_Var_May_Fail(var, SPECIFIED), source); // VARARGS!
        else
            Move_Value(Sink_Var_May_Fail(var, SPECIFIED), position); // BLOCK!
    }

    return FALSE;
}


//
//  do: native [
//
//  {Evaluates a block of source code (directly or fetched according to type)}
//
//      return: [<opt> any-value!]
//      source [
//          <opt> ;-- should DO accept an optional argument (chaining?)
//          blank! ;-- same question... necessary, or not?
//          block! ;-- source code in block form
//          group! ;-- same as block (or should it have some other nuance?)
//          string! ;-- source code in text form
//          binary! ;-- treated as UTF-8
//          url! ;-- load code from URL via protocol
//          file! ;-- load code from file on local disk
//          tag! ;-- proposed as module library tag name, hacked as demo
//          error! ;-- should use FAIL instead
//          function! ;-- will only run arity 0 functions (avoids DO variadic)
//          frame! ;-- acts like APPLY (voids are optionals, not unspecialized)
//          varargs! ;-- simulates as if frame! or block! is being executed
//      ]
//      /args
//          {If value is a script, this will set its system/script/args}
//      arg
//          "Args passed to a script (normally a string)"
//      /next
//          {Do next expression only, return it, update block variable}
//      var [any-word! blank!]
//          "If not blank, then a variable updated with new block position"
//      /only
//          "Don't catch QUIT (default behavior for BLOCK!)"
//  ]
//
REBNATIVE(do)
{
    INCLUDE_PARAMS_OF_DO;

    REBVAL *source = ARG(source);

    switch (VAL_TYPE(source)) {
    case REB_MAX_VOID:
        // useful for `do if ...` types of scenarios
        return R_VOID;

    case REB_BLANK:
        // useful for `do all ...` types of scenarios
        return R_BLANK;

    case REB_VARARGS:
    case REB_BLOCK:
    case REB_GROUP:
        if (Do_Or_Dont_Shared_Throws(
            D_OUT,
            source,
            REF(next) ? DO_FLAG_NORMAL : DO_FLAG_TO_END,
            REF(next) ? ARG(var) : BLANK_VALUE
        )){
            return R_OUT_IS_THROWN;
        }
        return R_OUT;

    case REB_BINARY:
    case REB_STRING:
    case REB_URL:
    case REB_FILE:
    case REB_TAG: {
        //
        // See code called in system/intrinsic/do*
        //
        const REBOOL fully = TRUE; // error if not all arguments consumed
        if (Apply_Only_Throws(
            D_OUT,
            fully,
            Sys_Func(SYS_CTX_DO_P),
            source,
            REF(args) ? TRUE_VALUE : FALSE_VALUE,
            REF(args) ? ARG(arg) : BLANK_VALUE, // can't put void in block
            REF(next) ? TRUE_VALUE : FALSE_VALUE,
            REF(next) ? ARG(var) : BLANK_VALUE, // can't put void in block
            REF(only) ? TRUE_VALUE : FALSE_VALUE,
            END
        )){
            return R_OUT_IS_THROWN;
        }
        return R_OUT; }

    case REB_ERROR:
        //
        // FAIL is the preferred operation for triggering errors, as it has
        // a natural behavior for blocks passed to construct readable messages
        // and "FAIL X" more clearly communicates a failure than "DO X"
        // does.  However DO of an ERROR! would have to raise an error
        // anyway, so it might as well raise the one it is given...and this
        // allows the more complex logic of FAIL to be written in Rebol code.
        //
        fail (VAL_CONTEXT(source));

    case REB_FUNCTION: {
        //
        // Ren-C will only run arity 0 functions from DO, otherwise EVAL
        // must be used.  Look for the first non-local parameter to tell.
        //
        REBVAL *param = FUNC_PARAMS_HEAD(VAL_FUNC(source));
        while (
            NOT_END(param)
            && (VAL_PARAM_CLASS(param) == PARAM_CLASS_LOCAL)
        ){
            ++param;
        }
        if (NOT_END(param))
            fail (Error_Use_Eval_For_Eval_Raw());

        if (Eval_Value_Throws(D_OUT, source))
            return R_OUT_IS_THROWN;
        return R_OUT; }

    case REB_FRAME: {
        REBCTX *c = VAL_CONTEXT(source);

        if (CTX_VARS_UNAVAILABLE(c)) // frame already ran, no data left
            fail (Error_Do_Expired_Frame_Raw());

        // See REBNATIVE(redo) for how tail-call recursion works.
        //
        REBFRM *f = CTX_FRAME_IF_ON_STACK(c);
        if (f != NULL)
            fail ("Use REDO to restart a running FRAME! (not DO)");

        REBSTR *opt_label = NULL; // no label available
        return Apply_Def_Or_Exemplar(
            D_OUT,
            source->payload.any_context.phase,
            VAL_BINDING(source),
            opt_label,
            NOD(VAL_CONTEXT(source))
        ); }

    default:
        break;
    }

    // Note: it is not possible to write a wrapper function in Rebol
    // which can do what EVAL can do for types that consume arguments
    // (like SET-WORD!, SET-PATH! and FUNCTION!).  DO used to do this for
    // functions only, EVAL generalizes it.
    //
    fail (Error_Use_Eval_For_Eval_Raw());
}


//
//  don't: native [
//
//  {Experimental function for skipping over a DO unit of code w/o evaluation}
//
//      return: [logic!]
//          {If true, it was possible to determine the arity and skip it}
//      source [block! varargs!]
//          {The value to attempt to skip content out of}
//      /next
//          {Don't do next expression only, update block variable}
//      var [any-word! blank!]
//          "If not blank, then a variable updated with new block position"
//  ]
//
REBNATIVE(dont)
//
// !!! This experimental code exploits DO_FLAG_NEUTRAL, which attempts to run
// through the same code path as DO (e.g. Do_Core()) but disable any actual
// side effects.
//
// Anytime an actual side-effect is required in order to figure out where an
// expression would end (variadic functions, GROUP! evaluation in a PATH!) the
// evaluator will throw to abort the scan.
{
    INCLUDE_PARAMS_OF_DONT;

    REBVAL *source = ARG(source);

    if (Do_Or_Dont_Shared_Throws(
        D_CELL,
        source,
        DO_FLAG_NEUTRAL | (REF(next) ? DO_FLAG_NORMAL : DO_FLAG_TO_END),
        REF(next) ? ARG(var) : BLANK_VALUE
    )){
        CATCH_THROWN(D_OUT, D_CELL);
        assert(IS_BLANK(D_CELL)); // "throw name" (current invariant)
        assert(IS_BAR(D_OUT)); // "thrown value" (current invariant)
        return R_FALSE;
    }

    return R_TRUE;
}


//
//  redo: native [
//
//  {Restart the function of a FRAME! from the top with its current state}
//
//      return: [<opt>]
//          {Does not return at all (either errors or restarts).}
//      restartee [frame! any-word!]
//          {FRAME! to restart, or WORD! bound to FRAME! (e.g. REDO 'RETURN)}
//      /other
//          {Restart in a frame-compatible function ("Sibling Tail-Call")}
//      sibling [function!]
//          {A FUNCTION! derived from the same underlying FRAME! as restartee}
//  ]
//
REBNATIVE(redo)
//
// This can be used to implement tail-call recursion:
//
// https://en.wikipedia.org/wiki/Tail_call
//
{
    INCLUDE_PARAMS_OF_REDO;

    REBVAL *restartee = ARG(restartee);
    if (NOT(IS_FRAME(restartee))) {
        if (NOT(Get_Context_Of(D_OUT, restartee)))
            fail ("No context found from restartee in REDO");

        if (NOT(IS_FRAME(D_OUT)))
            fail ("Context of restartee in REDO is not a FRAME!");

        Move_Value(restartee, D_OUT);
    }

    REBCTX *c = VAL_CONTEXT(restartee);

    if (CTX_VARS_UNAVAILABLE(c)) // frame already ran, no data left
        fail (Error_Do_Expired_Frame_Raw());

    REBFRM *f = CTX_FRAME_IF_ON_STACK(c);
    if (f == NULL)
        fail ("Use DO to start a not-currently running FRAME! (not REDO)");

    // If we were given a sibling to restart, make sure it is frame compatible
    // (e.g. the product of ADAPT-ing, CHAIN-ing, ENCLOSE-ing, HIJACK-ing a
    // common underlying function).
    //
    // !!! It is possible for functions to be frame-compatible even if they
    // don't come from the same heritage (e.g. two functions that take an
    // INTEGER! and have 2 locals).  Such compatibility may seem random to
    // users--e.g. not understanding why a function with 3 locals is not
    // compatible with one that has 2, and the test would be more expensive
    // than the established check for a common "ancestor".
    //
    if (REF(other)) {
        REBVAL *sibling = ARG(sibling);
        if (FRM_UNDERLYING(f) != FUNC_UNDERLYING(VAL_FUNC(sibling)))
            fail ("/OTHER function passed to REDO has incompatible FRAME!");

        restartee->payload.any_context.phase = VAL_FUNC(sibling);
        INIT_BINDING(restartee, VAL_BINDING(sibling));
    }

    // Phase needs to always be initialized in FRAME! values.
    //
    assert(
        SER(FUNC_PARAMLIST(restartee->payload.any_context.phase))->header.bits
        & ARRAY_FLAG_PARAMLIST
    );

    // We need to cooperatively throw a restart instruction up to the level
    // of the frame.  Use REDO as the label of the throw that Do_Core() will
    // identify for that behavior.
    //
    Move_Value(D_OUT, NAT_VALUE(redo));
    INIT_BINDING(D_OUT, c);

    // The FRAME! contains its ->phase and ->binding, which should be enough
    // to restart the phase at the point of parameter checking.  Make that
    // the actual value that Do_Core() catches.
    //
    CONVERT_NAME_TO_THROWN(D_OUT, restartee);
    return R_OUT_IS_THROWN;
}


//
//  do-all: native [
//
//  {Execute a series of BAR!-separated statements with error/quit recovery.}
//
//      return: [<opt> any-value!]
//      block [block!]
//  ]
//
REBNATIVE(do_all)
//
// !!! The name of this construct is under review, as well as whether it
// should be a block-of-blocks or use BAR!.  It was added to try and solve
// a problem, but then not used--however some variant of this feature is
// useful.
{
    INCLUDE_PARAMS_OF_DO_ALL;

    // Holds either an error value that is raised, or the thrown value.
    //
    DECLARE_LOCAL (arg_or_error);
    SET_END(arg_or_error);
    PUSH_GUARD_VALUE(arg_or_error);

    // If arg_or_error is not end, but thrown_name is an end, a throw tried
    // to propagate, but was caught...but if thrown_name is an end and the
    // arg_or_error is also not, it is an error which tried to propagate.
    //
    DECLARE_LOCAL (thrown_name);
    SET_END(thrown_name);
    PUSH_GUARD_VALUE(thrown_name);

    DECLARE_FRAME (f);
    Push_Frame(f, ARG(block));

    // The trap must be pushed *after* the frame has been pushed, so that
    // when a fail() happens it won't pop the running frame.
    //
    struct Reb_State state;
    REBCTX *error;

repush:
    PUSH_TRAP(&error, &state);

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        if (NOT_END(arg_or_error)) { // already a throw or fail pending!
            DECLARE_LOCAL (arg1);
            if (IS_END(thrown_name)) {
                assert(IS_ERROR(arg_or_error));
                Move_Value(arg1, arg_or_error);
            }
            else {
                CONVERT_NAME_TO_THROWN(thrown_name, arg_or_error);
                Init_Error(arg1, Error_No_Catch_For_Throw(thrown_name));
            }

            DECLARE_LOCAL (arg2);
            Init_Error(arg2, error);

            fail (Error_Multiple_Do_Errors_Raw(arg1, arg2));
        }

        Recover_Frame(f); // Frames otherwise not ready to use after a FAIL

        assert(IS_END(thrown_name));
        Init_Error(arg_or_error, error);

        while (FRM_HAS_MORE(f) && NOT(IS_BAR(f->value)))
            Fetch_Next_In_Frame(f);

        goto repush;
    }

    Init_Void(D_OUT); // default return result of DO-ALL []

    while (FRM_HAS_MORE(f)) {
        if (IS_BAR(f->value)) {
            //
            // BAR! is handled explicitly, because you might have f->value as
            // the BAR! in `| asdf`, call into the evaluator and get an error,
            // yet then come back and still have f->value positioned at the
            // BAR!.  This comes from how child frames and optimizations work.
            // Hence it's not easy to know where to skip forward to in case
            // of an error.
            //
            // !!! Review if the invariant of Do_Next_In_Frame_Throws()
            // should be changed.  So far, this is the only routine affected,
            // because no other functions try and "resume" a throwing/failing
            // frame--as that's not generically possible unless you skip to
            // the next BAR!, as this routine does.
            //
            Init_Void(D_OUT);
            Fetch_Next_In_Frame(f);
            continue;
        }

        if (Do_Next_In_Frame_Throws(D_OUT, f)) {
            if (NOT_END(arg_or_error)) { // already a throw or fail pending!
                DECLARE_LOCAL (arg1);
                if (IS_END(thrown_name)) {
                    assert(IS_ERROR(arg_or_error));
                    Move_Value(arg1, arg_or_error);
                }
                else {
                    CONVERT_NAME_TO_THROWN(thrown_name, arg_or_error);
                    Init_Error(arg1, Error_No_Catch_For_Throw(thrown_name));
                }

                DECLARE_LOCAL (arg2);
                Init_Error(arg2, Error_No_Catch_For_Throw(D_OUT));

                // We're still inside the pushed trap for this throw.  Have
                // to drop the trap to avoid transmitting the error to the
                // `if (error)` longjmp branch above!
                //
                DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

                fail (Error_Multiple_Do_Errors_Raw(arg1, arg2));
            }

            CATCH_THROWN(arg_or_error, D_OUT);
            Move_Value(thrown_name, D_OUT); // THROWN cleared by CATCH_THROWN

            while (FRM_HAS_MORE(f) && NOT(IS_BAR(f->value)))
                Fetch_Next_In_Frame(f);
        }
    }

    Drop_Frame(f);

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    DROP_GUARD_VALUE(thrown_name); // no GC (via Do_Core()) after this point
    DROP_GUARD_VALUE(arg_or_error);

    if (IS_END(arg_or_error)) { // no throws or errors tried to propagate
        assert(IS_END(thrown_name));
        return R_OUT;
    }

    if (NOT_END(thrown_name)) { // throw tried propagating, re-throw it
        Move_Value(D_OUT, thrown_name);
        CONVERT_NAME_TO_THROWN(D_OUT, arg_or_error);
        return R_OUT_IS_THROWN;
    }

    assert(IS_ERROR(arg_or_error));
    fail (VAL_CONTEXT(arg_or_error)); // error tried propagating, re-raise it
}


//
//  apply: native [
//
//  {Invoke a function with all required arguments specified.}
//
//      return: [<opt> any-value!]
//      applicand [function! any-word! any-path!]
//          {Function or specifying word (preserves word name for debug info)}
//      def [block!]
//          {Frame definition block (will be bound and evaluated)}
//  ]
//
REBNATIVE(apply)
{
    INCLUDE_PARAMS_OF_APPLY;

    REBVAL *applicand = ARG(applicand);

    REBSTR *opt_label;
    Get_If_Word_Or_Path_Arg(D_OUT, &opt_label, applicand);
    if (!IS_FUNCTION(D_OUT))
        fail (applicand);
    Move_Value(applicand, D_OUT);

    return Apply_Def_Or_Exemplar(
        D_OUT,
        VAL_FUNC(applicand),
        VAL_BINDING(applicand),
        opt_label,
        NOD(ARG(def))
    );
}
