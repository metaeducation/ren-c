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
//  reeval: native [
//
//  {Process received value *inline* as the evaluator loop would.}
//
//      return: [<opt> any-value!]
//      value [<opt> any-value!]
//          {BLOCK! passes-thru, ACTION! runs, SET-WORD! assigns...}
//      expressions [<opt> any-value! <...>]
//          {Depending on VALUE, more expressions may be consumed}
//      /only
//          {Suppress evaluation on any ensuing arguments value consumes}
//  ]
//
REBNATIVE(reeval)
{
    INCLUDE_PARAMS_OF_REEVAL;

    // EVAL only *acts* variadic, but uses DO_FLAG_REEVALUATE_CELL
    //
    UNUSED(ARG(expressions));

    DECLARE_SUBFRAME (child, frame_);

    // We need a way to slip the value through to the evaluator.  Can't run
    // it from the frame's cell.
    //
    child->u.reval.value = ARG(value);

    REBFLGS flags = DO_FLAG_REEVALUATE_CELL;
    if (REF(only)) {
        flags |= DO_FLAG_EXPLICIT_EVALUATE;
        ARG(value)->header.bits ^= VALUE_FLAG_EVAL_FLIP;
    }

    Init_Void(D_OUT);  // !!! R3C patch, better than error on `reeval :elide`

    if (Eval_Step_In_Subframe_Throws(D_OUT, frame_, flags, child))
        return R_THROWN;

    return D_OUT;
}


//
//  shove: enfix native [
//
//  {Shove a left hand parameter into an ACTION!, effectively making it enfix}
//
//      return: [<opt> any-value!]
//          "REVIEW: How might this handle shoving enfix invisibles?"
//      :left [<...> <end> any-value!]
//          "Requests parameter convention based on enfixee's first argument"
//      :enfixee [<end> word! path! group! action!]
//          "Needs ACTION!...but WORD!s fetched, PATH!s/GROUP!s evaluated"
//      :args [<...> <end> any-value!]
//          "Will handle args the way the enfixee expects"
//  ]
//
REBNATIVE(shove)
{
    INCLUDE_PARAMS_OF_SHOVE;

    UNUSED(ARG(left));
    UNUSED(ARG(enfixee));
    UNUSED(ARG(args));

    // !!! It's nice to imagine the system evolving to where actions this odd
    // could be written generically vs. being hardcoded in the evaluator.
    // But for now it is too "meta", and Eval_Core_Throws() detects
    // NAT_ACTION(shove) when used as enfix...and implements it there.
    //
    // Only way this native would be called would be if it were not enfixed.

    fail ("SHOVE may only be run as an ENFIX operation");
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
    if (not Is_Frame_Style_Varargs_May_Fail(&f, ARG(rest))) {
        //
        // It wouldn't be *that* hard to support block-style varargs, but as
        // this routine is a hack to implement ME, don't make it any longer
        // than it needs to be.
        //
        fail ("EVAL-ENFIX is not made to support MAKE VARARGS! [...] rest");
    }

    if (IS_END(f->value)) // no PATH! yet...
        fail ("ME and MY hit end of input");

    DECLARE_SUBFRAME (child, f); // saves DSP before refinement push

    const bool push_refinements = true;
    REBSTR *opt_label;
    DECLARE_LOCAL (temp);
    if (Get_If_Word_Or_Path_Throws(
        temp,
        &opt_label,
        f->value,
        f->specifier,
        push_refinements
    )){
        RETURN (temp);
    }

    if (not IS_ACTION(temp))
        fail ("ME and MY only work if right hand WORD! is an ACTION!");

    // Here we do something devious.  We subvert the system by setting
    // f->gotten to an enfixed version of the function even if it is
    // not enfixed.  This lets us slip in a first argument to a function
    // *as if* it were enfixed, e.g. `series: my next`.
    //
    SET_VAL_FLAG(temp, VALUE_FLAG_ENFIXED);
    PUSH_GC_GUARD(temp);
    f->gotten = temp;

    // !!! If we were to give an error on using ME with non-enfix or MY with
    // non-prefix, we'd need to know the fetched enfix state.  At the moment,
    // Get_If_Word_Or_Path_Throws() does not pass back that information.  But
    // if PATH! is going to do enfix dispatch, it should be addressed then.
    //
    UNUSED(REF(prefix));

    // Since enfix dispatch only works for words (for the moment), we lie
    // and use the label found in path processing as a word.
    //
    DECLARE_LOCAL (word);
    Init_Word(word, opt_label);
    f->value = word;

    // Simulate as if the passed-in value was calculated into the output slot,
    // which is where enfix functions usually find their left hand values.
    //
    Move_Value(D_OUT, ARG(left));

    // We're kind-of-abusing an internal mechanism, where it is checked that
    // we are actually doing a deferment.  Try not to make that abuse break
    // the assertions in Eval_Core.
    //
    // Note that while f may have a "prior" already, its prior will become
    // this frame...so when it asserts about "f->prior->deferred" it means
    // the frame of EVAL-ENFIX that is invoking it.
    //
    assert(IS_POINTER_TRASH_DEBUG(FS_TOP->u.defer.arg));
    FS_TOP->u.defer.arg = m_cast(REBVAL*, BLANK_VALUE); // !!! signal our hack

    REBFLGS flags = DO_FLAG_FULFILLING_ARG | DO_FLAG_POST_SWITCH;
    if (Eval_Step_In_Subframe_Throws(D_OUT, f, flags, child)) {
        DROP_GC_GUARD(temp);
        return R_THROWN;
    }

    TRASH_POINTER_IF_DEBUG(FS_TOP->u.defer.arg);

    DROP_GC_GUARD(temp);
    return D_OUT;
}


//
//  do: native [
//
//  {Evaluates a block of source code (directly or fetched according to type)}
//
//      return: [<opt> any-value!]
//      source [
//          blank! ;-- useful for `do try ...` scenarios when no match
//          block! ;-- source code in block form
//          group! ;-- same as block (or should it have some other nuance?)
//          text! ;-- source code in text form
//          binary! ;-- treated as UTF-8
//          url! ;-- load code from URL via protocol
//          file! ;-- load code from file on local disk
//          tag! ;-- module name (URL! looked up from table)
//          error! ;-- should use FAIL instead
//          action! ;-- will only run arity 0 actions (avoids DO variadic)
//          frame! ;-- acts like APPLY (voids are optionals, not unspecialized)
//          varargs! ;-- simulates as if frame! or block! is being executed
//      ]
//      /args
//          {If value is a script, this will set its system/script/args}
//      arg
//          "Args passed to a script (normally a string)"
//      /only
//          "Don't catch QUIT (default behavior for BLOCK!)"
//  ]
//
REBNATIVE(do)
{
    INCLUDE_PARAMS_OF_DO;

    REBVAL *source = ARG(source); // may be only GC reference, don't lose it!
  #if !defined(NDEBUG)
    SET_VAL_FLAG(ARG(source), CELL_FLAG_PROTECTED);
  #endif

    switch (VAL_TYPE(source)) {
    case REB_BLANK:
        return nullptr; // "blank in, null out" convention

    case REB_BLOCK:
    case REB_GROUP: {
        REBIXO indexor = Eval_Array_At_Core(
            Init_Void(D_OUT), // so `do []` matches up with `while [] [...]`
            nullptr, // opt_head (interpreted as no head, not nulled cell)
            VAL_ARRAY(source),
            VAL_INDEX(source),
            VAL_SPECIFIER(source),
            DO_FLAG_TO_END
        );

        if (indexor == THROWN_FLAG)
            return R_THROWN;

        assert(NOT_VAL_FLAG(D_OUT, VALUE_FLAG_UNEVALUATED));
        return D_OUT; }

    case REB_VARARGS: {
        REBVAL *position;
        if (Is_Block_Style_Varargs(&position, source)) {
            //
            // We can execute the array, but we must "consume" elements out
            // of it (e.g. advance the index shared across all instances)
            //
            // !!! If any VARARGS! op does not honor the "locked" flag on the
            // array during execution, there will be problems if it is TAKE'n
            // or DO'd while this operation is in progress.
            //
            REBIXO indexor = Eval_Array_At_Core(
                Init_Void(D_OUT),
                nullptr, // opt_head (no head, not intepreted as nulled cell)
                VAL_ARRAY(position),
                VAL_INDEX(position),
                VAL_SPECIFIER(source),
                DO_FLAG_TO_END
            );

            if (indexor == THROWN_FLAG) {
                //
                // !!! A BLOCK! varargs doesn't technically need to "go bad"
                // on a throw, since the block is still around.  But a FRAME!
                // varargs does.  This will cause an assert if reused, and
                // having BLANK! mean "thrown" may evolve into a convention.
                //
                Init_Unreadable_Blank(position);
                return R_THROWN;
            }

            SET_END(position); // convention for shared data at end point
            return D_OUT;
        }

        REBFRM *f;
        if (not Is_Frame_Style_Varargs_May_Fail(&f, source))
            panic (source); // Frame is the only other type

        // By definition, we are in the middle of a function call in the frame
        // the varargs came from.  It's still on the stack, and we don't want
        // to disrupt its state.  Use a subframe.
        //
        DECLARE_SUBFRAME (child, f);
        REBFLGS flags = 0;
        Init_Void(D_OUT);
        while (NOT_END(f->value)) {
            if (Eval_Step_In_Subframe_Throws(D_OUT, f, flags, child))
                return R_THROWN;
        }

        return D_OUT; }

    case REB_BINARY:
    case REB_TEXT:
    case REB_URL:
    case REB_FILE:
    case REB_TAG: {
        //
        // See code called in system/intrinsic/do*
        //
        REBVAL *sys_do_helper = CTX_VAR(Sys_Context, SYS_CTX_DO_P);
        assert(IS_ACTION(sys_do_helper));

        UNUSED(REF(args)); // detected via `value? :arg`

        const bool fully = true; // error if not all arguments consumed
        if (Apply_Only_Throws(
            D_OUT,
            fully,
            sys_do_helper,
            source,
            NULLIZE(ARG(arg)), // nulled cells => nullptr for API
            REF(only) ? TRUE_VALUE : FALSE_VALUE,
            rebEND
        )){
            return R_THROWN;
        }
        return D_OUT; }

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

    case REB_ACTION: {
        //
        // Ren-C will only run arity 0 functions from DO, otherwise EVAL
        // must be used.  Look for the first non-local parameter to tell.
        //
        REBVAL *param = ACT_PARAMS_HEAD(VAL_ACTION(source));
        while (
            NOT_END(param)
            and (VAL_PARAM_CLASS(param) == PARAM_CLASS_LOCAL)
        ){
            ++param;
        }
        if (NOT_END(param))
            fail (Error_Use_Eval_For_Eval_Raw());

        if (Eval_Value_Throws(D_OUT, source))
            return R_THROWN;
        return D_OUT; }

    case REB_FRAME: {
        REBCTX *c = VAL_CONTEXT(source); // checks for INACCESSIBLE
        REBACT *phase = VAL_PHASE(source);

        if (CTX_FRAME_IF_ON_STACK(c)) // see REDO for tail-call recursion
            fail ("Use REDO to restart a running FRAME! (not DO)");

        // To DO a FRAME! will "steal" its data.  If a user wishes to use a
        // frame multiple times, they must say DO COPY FRAME, so that the
        // data is stolen from the copy.  This allows for efficient reuse of
        // the context's memory in the cases where a copy isn't needed.

        DECLARE_END_FRAME (f);
        f->out = D_OUT;
        Push_Frame_At_End(
            f,
            DO_FLAG_FULLY_SPECIALIZED | DO_FLAG_PROCESS_ACTION
        );

        assert(CTX_KEYS_HEAD(c) == ACT_PARAMS_HEAD(phase));
        f->param = CTX_KEYS_HEAD(c);
        REBCTX *stolen = Steal_Context_Vars(c, NOD(phase));
        LINK(stolen).keysource = NOD(f); // changes CTX_KEYS_HEAD() result

        // Its data stolen, the context's node should now be GC'd when
        // references in other FRAME! value cells have all gone away.
        //
        assert(GET_SER_FLAG(c, NODE_FLAG_MANAGED));
        assert(GET_SER_INFO(c, SERIES_INFO_INACCESSIBLE));

        f->varlist = CTX_VARLIST(stolen);
        f->rootvar = CTX_ARCHETYPE(stolen);
        f->arg = f->rootvar + 1;
        //f->param set above
        f->special = f->arg;

        assert(FRM_PHASE(f) == phase);
        FRM_BINDING(f) = VAL_BINDING(source); // !!! should archetype match?

        REBSTR *opt_label = nullptr;
        Begin_Action(f, opt_label, ORDINARY_ARG);

        bool threw = (*PG_Eval_Throws)(f);

        Drop_Frame(f);

        if (threw)
            return R_THROWN; // prohibits recovery from exits

        assert(IS_END(f->value)); // we started at END_FLAG, can only throw

        return f->out; }

    default:
        break;
    }

    fail (Error_Use_Eval_For_Eval_Raw()); // https://trello.com/c/YMAb89dv
}


//
//  evaluate: native [
//
//  {Perform a single evaluator step, returning the next source position}
//
//      return: [<opt> block! group! varargs!]
//      source [
//          blank! ;-- useful for `do try ...` scenarios when no match
//          block! ;-- source code in block form
//          group! ;-- same as block (or should it have some other nuance?)
//          varargs! ;-- simulates as if frame! or block! is being executed
//      ]
//      /set "Store result in a variable (assuming something was evaluated)"
//      var [any-word!]
//          "If not blank, then a variable updated with new position"
//  ]
//
REBNATIVE(evaluate)
{
    INCLUDE_PARAMS_OF_EVALUATE;

    REBVAL *source = ARG(source); // may be only GC reference, don't lose it!
  #if !defined(NDEBUG)
    SET_VAL_FLAG(ARG(source), CELL_FLAG_PROTECTED);
  #endif

    switch (VAL_TYPE(source)) {
    case REB_BLANK:
        return nullptr; // "blank in, null out" convention

    case REB_BLOCK:
    case REB_GROUP: {
        DECLARE_LOCAL (temp);
        REBIXO indexor = Eval_Array_At_Core(
            SET_END(temp), // use END to distinguish residual non-values
            nullptr, // opt_head
            VAL_ARRAY(source),
            VAL_INDEX(source),
            VAL_SPECIFIER(source),
            DO_MASK_NONE
        );

        if (indexor == THROWN_FLAG) {
            Move_Value(D_OUT, temp);
            return R_THROWN;
        }

        if (indexor == END_FLAG or IS_END(temp))
            return nullptr; // no disruption of output result

        assert(NOT_VAL_FLAG(temp, VALUE_FLAG_UNEVALUATED));

        if (REF(set))
            Move_Value(Sink_Var_May_Fail(ARG(var), SPECIFIED), temp);

        Move_Value(D_OUT, source);
        VAL_INDEX(D_OUT) = cast(REBCNT, indexor) - 1; // was one past
        assert(VAL_INDEX(D_OUT) <= VAL_LEN_HEAD(source));
        return D_OUT; }

    case REB_VARARGS: {
        REBVAL *position;
        if (Is_Block_Style_Varargs(&position, source)) {
            //
            // We can execute the array, but we must "consume" elements out
            // of it (e.g. advance the index shared across all instances)
            //
            // !!! If any VARARGS! op does not honor the "locked" flag on the
            // array during execution, there will be problems if it is TAKE'n
            // or DO'd while this operation is in progress.
            //
            DECLARE_LOCAL (temp);
            REBIXO indexor = Eval_Array_At_Core(
                SET_END(temp),
                nullptr, // opt_head (interpreted as nothing, not nulled cell)
                VAL_ARRAY(position),
                VAL_INDEX(position),
                VAL_SPECIFIER(source),
                DO_MASK_NONE
            );

            if (indexor == THROWN_FLAG) {
                //
                // !!! A BLOCK! varargs doesn't technically need to "go bad"
                // on a throw, since the block is still around.  But a FRAME!
                // varargs does.  This will cause an assert if reused, and
                // having BLANK! mean "thrown" may evolve into a convention.
                //
                Init_Unreadable_Blank(position);
                return R_THROWN;
            }

            if (indexor == END_FLAG or IS_END(temp)) {
                SET_END(position); // convention for shared data at end point
                return nullptr;
            }

            if (REF(set))
                Move_Value(Sink_Var_May_Fail(ARG(var), SPECIFIED), source);

            RETURN (source); // original VARARGS! will have updated position
        }

        REBFRM *f;
        if (not Is_Frame_Style_Varargs_May_Fail(&f, source))
            panic (source); // Frame is the only other type

        // By definition, we are in the middle of a function call in the frame
        // the varargs came from.  It's still on the stack, and we don't want
        // to disrupt its state.  Use a subframe.
        //
        DECLARE_SUBFRAME (child, f);
        REBFLGS flags = 0;
        if (IS_END(f->value))
            return nullptr;

        DECLARE_LOCAL (temp);
        if (Eval_Step_In_Subframe_Throws(SET_END(temp), f, flags, child))
            RETURN (temp);

        if (IS_END(temp))
            return nullptr;

        if (REF(set))
            Move_Value(Sink_Var_May_Fail(ARG(var), SPECIFIED), temp);

        RETURN (source); } // original VARARGS! will have an updated position

    default:
        panic (source);
    }
}


//
//  sync-invisibles: native [
//
//  {If an evaluatable source has pending invisibles, execute and advance}
//
//      return: [<opt> block! group! varargs!]
//      source [block! group!]
//  ]
//
REBNATIVE(sync_invisibles)
{
    INCLUDE_PARAMS_OF_SYNC_INVISIBLES;

    // !!! This hasn't been implemented yet.  It is probably best done as
    // an adaptation of Eval_Core_Throws() with some kind of mode flag, and
    // would take some redesign to do efficiently.

    if (VAL_LEN_AT(ARG(source)) == 0)
        return nullptr;

    RETURN (ARG(source));
}


//
//  redo: native [
//
//  {Restart a frame's action from the top with its current state}
//
//      return: "Does not return at all (either errors or restarts)"
//          [<opt>]
//      restartee "Frame to restart, or bound word (e.g. REDO 'RETURN)"
//          [frame! any-word!]
//      /other "Restart in a frame-compatible function (sibling tail-call)"
//      sibling "Action derived from the same underlying frame as restartee"
//          [action!]
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
    if (not IS_FRAME(restartee)) {
        if (not Did_Get_Binding_Of(D_OUT, restartee))
            fail ("No context found from restartee in REDO");

        if (not IS_FRAME(D_OUT))
            fail ("Context of restartee in REDO is not a FRAME!");

        Move_Value(restartee, D_OUT);
    }

    REBCTX *c = VAL_CONTEXT(restartee);

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
        if (FRM_UNDERLYING(f) != ACT_UNDERLYING(VAL_ACTION(sibling)))
            fail ("/OTHER function passed to REDO has incompatible FRAME!");

        restartee->payload.any_context.phase = VAL_ACTION(sibling);
        INIT_BINDING(restartee, VAL_BINDING(sibling));
    }

    // Phase needs to always be initialized in FRAME! values.
    //
    assert(
        SER(ACT_PARAMLIST(restartee->payload.any_context.phase))->header.bits
        & ARRAY_FLAG_PARAMLIST
    );

    // We need to cooperatively throw a restart instruction up to the level
    // of the frame.  Use REDO as the throw label that Eval_Core_Throws() will
    // identify for that behavior.
    //
    Move_Value(D_OUT, NAT_VALUE(redo));
    INIT_BINDING(D_OUT, c);

    // The FRAME! contains its ->phase and ->binding, which should be enough
    // to restart the phase at the point of parameter checking.  Make that
    // the actual value that Eval_Core_Throws() catches.
    //
    CONVERT_NAME_TO_THROWN(D_OUT, restartee);
    return R_THROWN;
}


//
//  apply: native [
//
//  {Invoke an ACTION! with all required arguments specified}
//
//      return: [<opt> any-value!]
//      applicand "Literal action, or location to find one (preserves name)"
//          [action! word! path!]
//      def "Frame definition block (will be bound and evaluated)"
//          [block!]
//      /opt "Treat nulls as unspecialized <<experimental!>>"
//  ]
//
REBNATIVE(apply)
//
// !!! Because APPLY is being written as a regular native (and not a
// special exception case inside of Eval_Core) it has to "re-enter" Eval_Core
// and jump to the argument processing.
//
// This could also be accomplished if function dispatch were a subroutine
// that would be called both here and from the evaluator loop.  But if
// the subroutine were parameterized with the frame state, it would be
// basically equivalent to a re-entry.  And re-entry is interesting to
// experiment with for other reasons (e.g. continuations), so that is what
// is used here.
{
    INCLUDE_PARAMS_OF_APPLY;

    REBVAL *applicand = ARG(applicand);

    DECLARE_END_FRAME (f); // captures f->dsp
    f->out = D_OUT;

    // Argument can be a literal action (APPLY :APPEND) or a WORD!/PATH!.
    // If it is a path, we push the refinements to the stack so they can
    // be taken into account, e.g. APPLY 'APPEND/ONLY/DUP pushes /ONLY, /DUP
    //
    REBDSP lowest_ordered_dsp = DSP;
    REBSTR *opt_label;
    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        &opt_label,
        applicand,
        SPECIFIED,
        true // push_refinements, don't specialize ACTION! on 'APPEND/ONLY/DUP
    )){
        return R_THROWN;
    }

    if (not IS_ACTION(D_OUT))
        fail (Error_Invalid(applicand));
    Move_Value(applicand, D_OUT);

    // Make a FRAME! for the ACTION!, weaving in the ordered refinements
    // collected on the stack (if any).  Any refinements that are used in
    // any specialization level will be pushed as well, which makes them
    // out-prioritize (e.g. higher-ordered) than any used in a PATH! that
    // were pushed during the Get of the ACTION!.
    //
    struct Reb_Binder binder;
    INIT_BINDER(&binder);
    REBCTX *exemplar = Make_Context_For_Action_Int_Partials(
        applicand,
        f->dsp_orig, // lowest_ordered_dsp of refinements to weave in
        &binder,
        CELL_MASK_STACK
    );
    MANAGE_ARRAY(CTX_VARLIST(exemplar)); // binding code into it

    // Bind any SET-WORD!s in the supplied code block into the FRAME!, so
    // e.g. APPLY 'APPEND [VALUE: 10]` will set VALUE in exemplar to 10.
    //
    // !!! Today's implementation mutates the bindings on the passed-in block,
    // like R3-Alpha's MAKE OBJECT!.  See Virtual_Bind_Deep_To_New_Context()
    // for potential future directions.
    //
    Bind_Values_Inner_Loop(
        &binder,
        VAL_ARRAY_HEAD(ARG(def)), // !!! bindings are mutated!  :-(
        exemplar,
        FLAGIT_KIND(REB_SET_WORD), // types to bind (just set-word!),
        0, // types to "add midstream" to binding as we go (nothing)
        BIND_DEEP
    );

    // Reset all the binder indices to zero, balancing out what was added.
    //
    REBVAL *key = CTX_KEYS_HEAD(exemplar);
    REBVAL *var = CTX_VARS_HEAD(exemplar);
    for (; NOT_END(key); key++, ++var) {
        if (Is_Param_Unbindable(key))
            continue; // shouldn't have been in the binder
        if (Is_Param_Hidden(key))
            continue; // was part of a specialization internal to the action
        Remove_Binder_Index(&binder, VAL_KEY_CANON(key));
    }
    SHUTDOWN_BINDER(&binder); // must do before running code that might BIND

    // Run the bound code, ignore evaluative result (unless thrown)
    //
    PUSH_GC_GUARD(exemplar);
    DECLARE_LOCAL (temp);
    bool def_threw = Do_Any_Array_At_Throws(temp, ARG(def));
    DROP_GC_GUARD(exemplar);

    assert(CTX_KEYS_HEAD(exemplar) == ACT_PARAMS_HEAD(VAL_ACTION(applicand)));
    f->param = CTX_KEYS_HEAD(exemplar);
    REBCTX *stolen = Steal_Context_Vars(
        exemplar,
        NOD(VAL_ACTION(applicand))
    );
    LINK(stolen).keysource = NOD(f); // changes CTX_KEYS_HEAD result

    if (def_threw) {
        Free_Unmanaged_Array(CTX_VARLIST(stolen)); // could TG_Reuse it
        RETURN (temp);
    }

    Push_Frame_At_End(f, DO_FLAG_PROCESS_ACTION);

    if (REF(opt))
        f->u.defer.arg = nullptr; // needed if !(DO_FLAG_FULLY_SPECIALIZED)
    else {
        //
        // If nulls are taken literally as null arguments, then no arguments
        // are gathered at the callsite, so the "ordering information"
        // on the stack isn't needed.  Eval_Core_Throws() will just treat a
        // slot with an INTEGER! for a refinement as if it were "true".
        //
        f->flags.bits |= DO_FLAG_FULLY_SPECIALIZED;
        DS_DROP_TO(lowest_ordered_dsp); // zero refinements on stack, now
    }

    f->varlist = CTX_VARLIST(stolen);
    SET_SER_FLAG(f->varlist, SERIES_FLAG_STACK);
    f->rootvar = CTX_ARCHETYPE(stolen);
    f->arg = f->rootvar + 1;
    // f->param assigned above
    f->special = f->arg; // signal only type-check the existing data
    FRM_PHASE(f) = VAL_ACTION(applicand);
    FRM_BINDING(f) = VAL_BINDING(applicand);

    Begin_Action(f, opt_label, ORDINARY_ARG);
    assert(IS_POINTER_TRASH_DEBUG(f->u.defer.arg)); // see Eval_Core_Throws()

    bool action_threw = (*PG_Eval_Throws)(f);

    Drop_Frame(f);

    if (action_threw)
        return R_THROWN;

    assert(IS_END(f->value)); // we started at END_FLAG, can only throw
    return D_OUT;
}
