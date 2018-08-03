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
    if (not Is_Frame_Style_Varargs_May_Fail(&f, ARG(rest))) {
        //
        // It wouldn't be *that* hard to support block-style varargs, but as
        // this routine is a hack to implement ME, don't make it any longer
        // than it needs to be.
        //
        fail ("EVAL-ENFIX is not made to support MAKE VARARGS! [...] rest");
    }

    if (FRM_AT_END(f)) // no PATH! yet...
        fail ("ME and MY hit end of input");

    DECLARE_FRAME (child); // capture DSP *now*, before any refinements push

    const REBOOL push_refinements = true;
    REBSTR *opt_label;
    if (Get_If_Word_Or_Path_Throws(
        D_CELL,
        &opt_label,
        f->value,
        f->specifier,
        push_refinements
    )){
        return D_CELL;
    }

    // !!! If we were to give an error on using ME with non-enfix or MY with
    // non-prefix, we'd need to know the fetched enfix state.  At the moment,
    // Get_If_Word_Or_Path_Throws() does not pass back that information.  But
    // if PATH! is going to do enfix dispatch, it should be addressed then.
    //
    f->gotten = D_CELL;

    if (not IS_ACTION(f->gotten))
        fail ("ME and MY only work if right hand WORD! is an ACTION!");

    DECLARE_LOCAL (word);

    // Here we do something devious.  We subvert the system by setting
    // f->gotten to an enfixed version of the function even if it is
    // not enfixed.  This lets us slip in a first argument to a function
    // *as if* it were enfixed, e.g. `series: my next`.
    //
    UNUSED(REF(prefix));
    SET_VAL_FLAG(D_CELL, VALUE_FLAG_ENFIXED);

    // Since enfix dispatch only works for words (for the moment), we lie
    // and use the label found in path processing as a word.
    //
    Init_Word(word, opt_label);
    f->value = word;

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
    assert(IS_POINTER_TRASH_DEBUG(FS_TOP->deferred));
    FS_TOP->deferred = m_cast(REBVAL*, BLANK_VALUE); // !!! signal our hack

    REBFLGS flags = DO_FLAG_FULFILLING_ARG | DO_FLAG_POST_SWITCH;
    if (Do_Next_In_Subframe_Throws(D_OUT, f, flags, child))
        return D_OUT;

    FS_TOP->deferred = nullptr;

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
        Init_Void(D_OUT); // so `do []` is consistent with `while [] [...]`
        REBIXO indexor = Do_Array_At_Core(
            D_OUT,
            nullptr, // opt_head (interpreted as no head, not nulled cell)
            VAL_ARRAY(source),
            VAL_INDEX(source),
            VAL_SPECIFIER(source),
            DO_FLAG_TO_END
        );

        if (indexor == THROWN_FLAG)
            return D_OUT;

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
            Init_Void(D_OUT);
            REBIXO indexor = Do_Array_At_Core(
                D_OUT,
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
                return D_OUT;
            }

            if (indexor == END_FLAG)
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
        DECLARE_FRAME (child);
        REBFLGS flags = 0;
        Init_Void(D_OUT);
        while (not FRM_AT_END(f)) {
            if (Do_Next_In_Subframe_Throws(D_OUT, f, flags, child))
                return D_OUT;
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

        const REBOOL fully = TRUE; // error if not all arguments consumed
        if (Apply_Only_Throws(
            D_OUT,
            fully,
            sys_do_helper,
            source,
            NULLIZE(ARG(arg)), // nulled cells => nullptr for API
            REF(only) ? TRUE_VALUE : FALSE_VALUE,
            rebEND
        )){
            return D_OUT;
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
            return D_OUT;
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

        DECLARE_FRAME (f);
        f->out = D_OUT;
        Push_Frame_At_End(
            f,
            DO_FLAG_GOTO_PROCESS_ACTION | DO_FLAG_FULLY_SPECIALIZED
        );

        assert(CTX_KEYS_HEAD(c) == ACT_FACADE_HEAD(phase));
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

        (*PG_Do)(f);

        Drop_Frame_Core(f);

        if (THROWN(f->out))
            return f->out; // prohibits recovery from exits

        assert(FRM_AT_END(f)); // we started at END_FLAG, can only throw

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
//      var [any-word! blank!]
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

    const REBVAL *var = ARG(var);
    if (IS_BLANK(var))
        var = NULLED_CELL;
    UNUSED(REF(set)); // accounted for by checking var for nulled cell

    switch (VAL_TYPE(source)) {
    case REB_BLANK:
        return nullptr; // "blank in, null out" convention

    case REB_BLOCK:
    case REB_GROUP: {
        REBIXO indexor = Do_Array_At_Core(
            D_CELL,
            nullptr, // opt_head
            VAL_ARRAY(source),
            VAL_INDEX(source),
            VAL_SPECIFIER(source),
            DO_MASK_NONE
        );

        if (indexor == THROWN_FLAG)
            return D_CELL;

        if (indexor == END_FLAG)
            return nullptr; // no disruption of output result

        assert(NOT_VAL_FLAG(D_CELL, VALUE_FLAG_UNEVALUATED));

        if (not IS_NULLED(var))
            Move_Value(Sink_Var_May_Fail(ARG(var), SPECIFIED), D_CELL);

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
            REBIXO indexor = Do_Array_At_Core(
                D_CELL,
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
                return D_OUT;
            }

            if (indexor == END_FLAG) {
                SET_END(position); // convention for shared data at end point
                return nullptr;
            }

            if (not IS_NULLED(var))
                Move_Value(Sink_Var_May_Fail(var, SPECIFIED), source);

            return source; // original VARARGS! will have an updated position
        }

        REBFRM *f;
        if (not Is_Frame_Style_Varargs_May_Fail(&f, source))
            panic (source); // Frame is the only other type

        // By definition, we are in the middle of a function call in the frame
        // the varargs came from.  It's still on the stack, and we don't want
        // to disrupt its state.  Use a subframe.
        //
        DECLARE_FRAME (child);
        REBFLGS flags = 0;
        if (FRM_AT_END(f))
            return nullptr;

        if (Do_Next_In_Subframe_Throws(D_CELL, f, flags, child))
            return D_CELL;

        if (not IS_NULLED(var))
            Move_Value(Sink_Var_May_Fail(var, SPECIFIED), D_CELL);

        return source; } // original VARARGS! will have an updated position

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
    // an adaptation of Do_Core() with some kind of mode flag, and would take
    // some redesign to do efficiently.

    if (VAL_LEN_AT(ARG(source)) == 0)
        return nullptr;

    return ARG(source);
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
    return D_OUT;
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
// special exception case inside of Do_Core) it has to "re-enter" Do_Core
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

    DECLARE_FRAME (f); // captures f->dsp
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
        return D_OUT;
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
        if (GET_VAL_FLAG(key, TYPESET_FLAG_UNBINDABLE))
            continue; // shouldn't have been in the binder
        if (GET_VAL_FLAG(var, ARG_MARKED_CHECKED))
            continue; // was part of a specialization internal to the action
        Remove_Binder_Index(&binder, VAL_KEY_CANON(key));
    }
    SHUTDOWN_BINDER(&binder); // must do before running code that might BIND

    // Run the bound code, ignore evaluative result (unless thrown)
    //
    PUSH_GC_GUARD(exemplar);
    REBOOL threw = Do_Any_Array_At_Throws(D_CELL, ARG(def));
    DROP_GC_GUARD(exemplar);

    assert(CTX_KEYS_HEAD(exemplar) == ACT_FACADE_HEAD(VAL_ACTION(applicand)));
    f->param = CTX_KEYS_HEAD(exemplar);
    REBCTX *stolen = Steal_Context_Vars(
        exemplar,
        NOD(ACT_FACADE(VAL_ACTION(applicand)))
    );
    LINK(stolen).keysource = NOD(f); // changes CTX_KEYS_HEAD result

    if (threw) {
        Free_Unmanaged_Array(CTX_VARLIST(stolen)); // could TG_Reuse it
        return D_CELL;
    }

    Push_Frame_At_End(f, DO_FLAG_GOTO_PROCESS_ACTION);

    if (REF(opt))
        f->deferred = nullptr; // needed if !(DO_FLAG_FULLY_SPECIALIZED)
    else {
        //
        // If nulls are taken literally as null arguments, then no arguments
        // are gathered at the callsite, so the "ordering information"
        // on the stack isn't needed.  Do_Core() will just treat a slot
        // with an INTEGER! for a refinement as if it were "true".
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
    assert(IS_POINTER_TRASH_DEBUG(f->deferred)); // Do_Core() sanity checks

    (*PG_Do)(f);

    Drop_Frame_Core(f);

    if (THROWN(f->out))
        return f->out;

    assert(FRM_AT_END(f)); // we started at END_FLAG, can only throw
    return D_OUT;
}
