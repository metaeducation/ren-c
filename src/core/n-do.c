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
// Copyright 2012-2017 Ren-C Open Source Contributors
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
//  {Process an evaluated argument *inline* as the evaluator loop would}
//
//      return: [<opt> <invisible> any-value!]
//      value [any-value!]
//          {BLOCK! passes-thru, ACTION! runs, SET-WORD! assigns...}
//      expressions [<opt> any-value! <variadic>]
//          {Depending on VALUE, more expressions may be consumed}
//  ]
//
REBNATIVE(reeval)
{
    INCLUDE_PARAMS_OF_REEVAL;

    // REEVAL only *acts* variadic, but uses ST_EVALUATOR_REEVALUATING
    //
    UNUSED(ARG(expressions));

    REBVAL *v = ARG(value);

    bool enfix = IS_ACTION(v) and GET_ACTION_FLAG(VAL_ACTION(v), ENFIXED);

    REBFLGS flags = EVAL_MASK_DEFAULT;
    if (Reevaluate_In_Subframe_Maybe_Stale_Throws(
        D_OUT,  // reeval :comment "this should leave old input"
        frame_,
        ARG(value),
        flags,
        enfix
    ))
        return R_THROWN;

    return D_OUT;  // don't clear stale flag...act invisibly
}


//
//  shove: native [
//
//  {Shove a parameter into an ACTION! as its first argument}
//
//      return: [<opt> any-value!]
//          "REVIEW: How might this handle shoving enfix invisibles?"
//      'left [<end> <opt> any-value!]
//          "Requests parameter convention based on enfixee's first argument"
//      :right [<variadic> <end> any-value!]
//          "(uses magic -- SHOVE can't be written easily in usermode yet)"
//      /prefix "Force either prefix or enfix behavior (vs. acting as is)"
//          [logic!]
//      /set "If left hand side is a SET-WORD! or SET-PATH!, shove and assign"
//  ]
//
REBNATIVE(shove)
//
// PATH!s do not do infix lookup in Rebol, and there are good reasons for this
// in terms of both performance and semantics.  However, it is sometimes
// needed to dispatch via a path--for instance to call an enfix function that
// lives in a context, or even to call one that has refinements.
//
// The SHOVE operation is used to push values from the left to act as the
// first argument of an operation, e.g.:
//
//      >> 10 >- lib/(print "Hi!" first [multiply]) 20
//      Hi!
//      200
//
// It's becoming more possible to write something like this in usermode, but
// it would be inefficient.  This version of shove is a light variation on
// the EVAL native, which retriggers the actual enfix machinery.
{
    INCLUDE_PARAMS_OF_SHOVE;

    REBFRM *f;
    if (not Is_Frame_Style_Varargs_May_Fail(&f, ARG(right)))
        fail ("SHOVE (>-) not implemented for MAKE VARARGS! [...] yet");

    REBVAL *left = ARG(left);

    if (IS_END(f_value))  // ...shouldn't happen for WORD!/PATH! unless APPLY
        RETURN (ARG(left));  // ...because evaluator wants `help <-` to work

    // It's best for SHOVE to do type checking here, as opposed to setting
    // some kind of EVAL_FLAG_SHOVING and passing that into the evaluator, then
    // expecting it to notice if you shoved into an INTEGER! or something.
    //
    // !!! Pure invisibility should work; see SYNC-INVISIBLES for ideas,
    // something like this should be in the tests and be able to work:
    //
    //    >> 10 >- comment "ignore me" lib/+ 20
    //    == 30
    //
    // !!! To get the feature working as a first cut, this doesn't try get too
    // fancy with apply-like mechanics and slipstream refinements on the
    // stack to enfix functions with refinements.  It specializes the ACTION!.
    // We can do better, but seeing as how you couldn't call enfix actions
    // with refinements *at all* before, this is a step up.

    REBVAL *shovee = ARG(right); // reuse arg cell for the shoved-into

    if (IS_WORD(f_value) or IS_PATH(f_value)) {
        if (Get_If_Word_Or_Path_Throws(
            D_OUT, // can't eval directly into arg slot
            f_value,
            f_specifier,
            false // !!! see above; false = don't push refinements
        )){
            return R_THROWN;
        }

        Move_Value(shovee, D_OUT);
    }
    else if (IS_GROUP(f_value)) {
        if (Do_Any_Array_At_Throws(D_OUT, f_value, f_specifier))
            return R_THROWN;
        if (IS_END(D_OUT))  // !!! need SHOVE frame for type error
            fail ("GROUP! passed to SHOVE did not evaluate to content");

        Move_Value(shovee, D_OUT);  // Note: can't eval directly into arg slot
    }
    else
        Move_Value(shovee, SPECIFIC(f_value));

    if (not IS_ACTION(shovee) and not ANY_SET_KIND(VAL_TYPE(shovee)))
        fail ("SHOVE's immediate right must be ACTION! or SET-XXX! type");

    // Basic operator `>-` will use the enfix status of the shovee.
    // `->-` will force enfix evaluator behavior even if shovee is prefix.
    // `>--` will force prefix evaluator behavior even if shovee is enfix.
    //
    bool enfix;
    if (REF(prefix))
        enfix = not VAL_LOGIC(ARG(prefix));
    else if (IS_ACTION(shovee))
        enfix = GET_ACTION_FLAG(VAL_ACTION(shovee), ENFIXED);
    else
        enfix = false;

    Fetch_Next_Forget_Lookback(f);

    // Trying to EVAL a SET-WORD! or SET-PATH! with no args would be an error.
    // So interpret it specially...GET the value and SET it back.  Note this
    // is tricky stuff to do when a SET-PATH! has groups in it to avoid a
    // double evaluation--the API is used here for simplicity.
    //
    REBVAL *composed_set_path = nullptr;

    // Since we're simulating enfix dispatch, we need to move the first arg
    // where enfix gets it from...the frame output slot.
    //
    // We quoted the argument on the left, but the ACTION! we are feeding
    // into may want it evaluative.  (Enfix handling itself does soft quoting)
    //
  #if !defined(NDEBUG)
    Init_Unreadable_Void(D_OUT); // make sure we reassign it
  #endif

    if (REF(set)) {
        if (IS_SET_WORD(left)) {
            Move_Value(D_OUT, Lookup_Word_May_Fail(left, SPECIFIED));
        }
        else if (IS_SET_PATH(left)) {
            f->feed->gotten = nullptr;  // calling arbitrary code, may disrupt
            composed_set_path = rebValueQ("compose", left, rebEND);
            REBVAL *temp = rebValueQ("get/hard", composed_set_path, rebEND);
            Move_Value(D_OUT, temp);
            rebRelease(temp);
        }
        else
            fail ("Left hand side must be SET-WORD! or SET-PATH!");
    }
    else if (
        GET_CELL_FLAG(left, UNEVALUATED)
        and not (
            IS_ACTION(shovee)
            and GET_ACTION_FLAG(VAL_ACTION(shovee), QUOTES_FIRST)
        )
    ){
        if (Eval_Value_Throws(D_OUT, left, SPECIFIED))
            return R_THROWN;
    }
    else {
        Move_Value(D_OUT, left);
        if (GET_CELL_FLAG(left, UNEVALUATED))
            SET_CELL_FLAG(D_OUT, UNEVALUATED);
    }

    REBFLGS flags = EVAL_MASK_DEFAULT;
    SET_FEED_FLAG(frame_->feed, NEXT_ARG_FROM_OUT);

    if (Reevaluate_In_Subframe_Maybe_Stale_Throws(
        D_OUT,
        frame_,
        shovee,
        flags,
        enfix
    )){
        rebRelease(composed_set_path);  // ok if nullptr
        return R_THROWN;
    }

    assert(NOT_CELL_FLAG(D_OUT, OUT_MARKED_STALE));  // !!! can this happen?

    if (REF(set)) {
        if (IS_SET_WORD(left)) {
            Move_Value(Sink_Word_May_Fail(left, SPECIFIED), D_OUT);
        }
        else if (IS_SET_PATH(left)) {
            f->feed->gotten = nullptr;  // calling arbitrary code, may disrupt
            rebElideQ(
                "set/hard", composed_set_path, NULLIFY_NULLED(D_OUT),
            rebEND);
            rebRelease(composed_set_path);
        }
        else
            assert(false); // SET-WORD!/SET-PATH! was checked above
    }

    return D_OUT;
}


//
//  do: native [
//
//  {Evaluates a block of source code (directly or fetched according to type)}
//
//      return: [<opt> any-value!]
//      source [
//          <blank>  ; opts out of the DO, returns null
//          block!  ; source code in block form
//          sym-block!  ; same
//          group!  ; same as block (or should it have some other nuance?)
//          sym-group!  ; same
//          text!  ; source code in text form
//          binary!  ; treated as UTF-8
//          url!  ; load code from URL via protocol
//          file!  ; load code from file on local disk
//          tag!  ; module name (URL! looked up from table)
//          error!  ; should use FAIL instead
//          action!  ; will only run arity 0 actions (avoids DO variadic)
//          frame!  ; acts like APPLY (voids are optionals, not unspecialized)
//          varargs!  ; simulates as if frame! or block! is being executed
//      ]
//      /args "Sets system/script/args if doing a script (usually a TEXT!)"
//          [any-value!]
//      /only "Don't catch QUIT (default behavior for BLOCK!)"
//  ]
//
REBNATIVE(do)
{
    INCLUDE_PARAMS_OF_DO;

    REBVAL *source = ARG(source);

    // If `source` is not const, tweak it to be explicitly mutable--because
    // otherwise, it would wind up inheriting the FEED_MASK_CONST of our
    // currently executing frame.  That's no good for `loop 2 [do block]`,
    // because we want whatever constness is on block...
    //
    // (Note we *can't* tweak values that are RELVAL in source.  So we either
    // bias to having to do this or Do_XXX() versions explode into passing
    // mutability parameters all over the place.  This is better.)
    //
    if (NOT_CELL_FLAG(source, CONST))
        SET_CELL_FLAG(source, EXPLICITLY_MUTABLE);

  #if !defined(NDEBUG)
    SET_CELL_FLAG(ARG(source), PROTECTED);  // maybe only GC reference, keep!
  #endif

    switch (VAL_TYPE(source)) {
      case REB_BLOCK:
      case REB_SYM_BLOCK:
      case REB_GROUP:
      case REB_SYM_GROUP: {
        if (Do_Any_Array_At_Throws(D_OUT, source, SPECIFIED))
            return R_THROWN;
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
            if (Do_Any_Array_At_Throws(D_OUT, position, SPECIFIED)) {
                //
                // !!! A BLOCK! varargs doesn't technically need to "go bad"
                // on a throw, since the block is still around.  But a FRAME!
                // varargs does.  This will cause an assert if reused, and
                // having BLANK! mean "thrown" may evolve into a convention.
                //
                Init_Unreadable_Void(position);
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

        Init_Void(D_OUT, SYM_VOID);
        if (IS_END(f->feed->value))
            return D_OUT;

        DECLARE_FRAME (subframe, f->feed, EVAL_MASK_DEFAULT);

        bool threw;
        Push_Frame(D_OUT, subframe);
        do {
            threw = Eval_Step_Maybe_Stale_Throws(D_OUT, subframe);
        } while (not threw and NOT_END(f->feed->value));
        Drop_Frame(subframe);

        if (threw)
            return R_THROWN;

        CLEAR_CELL_FLAG(D_OUT, OUT_MARKED_STALE);
        return D_OUT; }

      case REB_BINARY:
      case REB_TEXT:
      case REB_URL:
      case REB_FILE:
      case REB_TAG: {
        //
        // See code called in system/intrinsic/do*
        //
        REBVAL *sys_do_helper = Get_Sys_Function(DO_P);
        assert(IS_ACTION(sys_do_helper));

        UNUSED(REF(args)); // detected via `value? :arg`

        if (RunQ_Throws(
            D_OUT,
            true,  // fully = true, error if not all arguments consumed
            rebU(sys_do_helper),
            source,
            REF(args),
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
        // Ren-C will only run arity 0 functions from DO, otherwise REEVAL
        // must be used.  Look for the first non-local parameter to tell.
        //
        if (First_Unspecialized_Param(VAL_ACTION(source)))
            fail (Error_Do_Arity_Non_Zero_Raw());

        if (Eval_Value_Throws(D_OUT, source, SPECIFIED))
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

        REBFLGS flags = EVAL_MASK_DEFAULT
            | EVAL_FLAG_FULLY_SPECIALIZED;

        DECLARE_END_FRAME (f, flags);

        assert(CTX_KEYS_HEAD(c) == ACT_PARAMS_HEAD(phase));
        f->param = CTX_KEYS_HEAD(c);
        REBCTX *stolen = Steal_Context_Vars(c, NOD(phase));
        INIT_LINK_KEYSOURCE(stolen, NOD(f));  // changes CTX_KEYS_HEAD()

        // Its data stolen, the context's node should now be GC'd when
        // references in other FRAME! value cells have all gone away.
        //
        assert(GET_SERIES_FLAG(c, MANAGED));
        assert(GET_SERIES_INFO(c, INACCESSIBLE));

        Push_Frame(D_OUT, f);
        f->varlist = CTX_VARLIST(stolen);
        f->rootvar = CTX_ROOTVAR(stolen);
        f->arg = f->rootvar + 1;
        // f->param set above
        f->special = f->arg;

        assert(FRM_PHASE(f) == phase);
        FRM_BINDING(f) = VAL_BINDING(source); // !!! should archetype match?

        // Some FRAME! values sneakily hide a label where the VAL_PHASE()
        // would be (if they can get away with it, e.g. the phase can be
        // assumed to be the main phase from the keylist).
        //
        Begin_Prefix_Action(f, VAL_FRAME_LABEL(source));

        bool threw = Process_Action_Throws(f);
        assert(threw or IS_END(f->feed->value));  // we started at END_FLAG

        Drop_Frame(f);

        if (threw)
            return R_THROWN; // prohibits recovery from exits

        return D_OUT; }

      default:
        break;
    }

    fail (Error_Do_Arity_Non_Zero_Raw());  // https://trello.com/c/YMAb89dv
}


//
//  evaluate: native [
//
//  {Perform a single evaluator step, returning the next source position}
//
//      return: "Next position (quoted if result requested and invisible)"
//          [<opt> quoted! block! group! varargs!]
//      source [
//          <blank>  ; useful for `evaluate try ...` scenarios when no match
//          quoted!  ; accepts quoted source (may carry bit from prior eval)
//          block!  ; source code in block form
//          group!  ; same as block (or should it have some other nuance?)
//          varargs!  ; simulates as if frame! or block! is being executed
//      ]
//      /result "Value from the step (VOID! + quoted return pos if invisible)"
//          [<output> <opt> any-value!]
//  ]
//
REBNATIVE(evaluate)
{
    INCLUDE_PARAMS_OF_EVALUATE;

    REBVAL *source = ARG(source);  // may be only GC reference, don't lose it!
    Dequotify(source);  // May have quotes if indicating invisible eval

  #if !defined(NDEBUG)
    SET_CELL_FLAG(ARG(source), PROTECTED);
  #endif

    REBVAL *var = ARG(result);

    switch (VAL_TYPE(source)) {
      case REB_BLOCK:
      case REB_GROUP: {
        REBLEN index;
        if (VAL_LEN_AT(source) == 0) {  // `evaluate []` should return null
            Init_Nulled(D_OUT);
            Init_Void(D_SPARE, SYM_EMPTY);
        }
        else {
            if (Eval_Step_In_Any_Array_At_Throws(
                D_SPARE,
                &index,
                source,
                SPECIFIED,
                EVAL_MASK_DEFAULT
            )){
                Move_Value(D_OUT, D_SPARE);
                return R_THROWN;
            }

            Move_Value(D_OUT, source);
            VAL_INDEX_UNBOUNDED(D_OUT) = index;

            if (IS_END(D_SPARE)) {
                //
                // This means the result was invisible:
                //
                //   evaluate [comment "hi" 1 + 2]  ; should return '[1 + 2]
                //
                // Adding a quote level on the return result helps cue the
                // caller that the void we choose to return is actually
                // invisible, if they want to do correct invisible handling.
                //
                // https://forum.rebol.info/t/1173/
                //
                Init_Void(D_SPARE, SYM_EMPTY);
                Quotify(D_OUT, 1);  // void-is-invisible signal on array
            }
            else {
                Move_Value(D_OUT, source);
                VAL_INDEX_UNBOUNDED(D_OUT) = index;
            }
        }
        break; }  // update variable

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
            REBLEN index;
            if (Eval_Step_In_Any_Array_At_Throws(
                SET_END(D_SPARE),
                &index,
                position,
                SPECIFIED,
                EVAL_MASK_DEFAULT
            )){
                // !!! A BLOCK! varargs doesn't technically need to "go bad"
                // on a throw, since the block is still around.  But a FRAME!
                // varargs does.  This will cause an assert if reused, and
                // having BLANK! mean "thrown" may evolve into a convention.
                //
                Init_Unreadable_Void(position);
                Move_Value(D_OUT, D_SPARE);
                return R_THROWN;
            }

            if (IS_END(D_SPARE)) {
                SET_END(position);  // convention for shared data at end point
                return nullptr;
            }

            VAL_INDEX_UNBOUNDED(position) = index;
        }
        else {
            REBFRM *f;
            if (not Is_Frame_Style_Varargs_May_Fail(&f, source))
                panic (source); // Frame is the only other type

            // By definition, we're in the middle of a function call in frame
            // the varargs came from.  It's still on the stack--we don't want
            // to disrupt its state (beyond feed advancing).  Use a subframe.

            if (IS_END(f->feed->value))
                return nullptr;

            REBFLGS flags = EVAL_MASK_DEFAULT;
            if (Eval_Step_In_Subframe_Throws(D_SPARE, f, flags)) {
                Move_Value(D_OUT, D_SPARE);
                return R_THROWN;
            }

            if (IS_END(D_SPARE))  // remainder just comments and invisibles
                return nullptr;
        }

        Move_Value(D_OUT, source);  // VARARGS! will have updated position
        break; }  // update variable

      default:
        panic (source);
    }

    if (IS_TRUTHY(var))
        Set_Var_May_Fail(
            var, SPECIFIED,
            D_SPARE, SPECIFIED,
            false  // not hard (e.g. GROUP!s don't run, and not literal)
        );

    return D_OUT;
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
    // an adaptation of Eval_Core() with some kind of mode flag, and
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
        REBVAL *sibling = ARG(other);
        if (FRM_UNDERLYING(f) != ACT_UNDERLYING(VAL_ACTION(sibling)))
            fail ("/OTHER function passed to REDO has incompatible FRAME!");

        INIT_VAL_CONTEXT_PHASE(restartee, VAL_ACTION(sibling));
        INIT_BINDING(restartee, VAL_BINDING(sibling));
    }

    // We need to cooperatively throw a restart instruction up to the level
    // of the frame.  Use REDO as the throw label that Eval_Core() will
    // identify for that behavior.
    //
    Move_Value(D_OUT, NATIVE_VAL(redo));
    INIT_BINDING(D_OUT, c);

    // The FRAME! contains its ->phase and ->binding, which should be enough
    // to restart the phase at the point of parameter checking.  Make that
    // the actual value that Eval_Core() catches.
    //
    return Init_Thrown_With_Label(D_OUT, restartee, D_OUT);
}


//
//  applique: native [
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
REBNATIVE(applique)
{
    INCLUDE_PARAMS_OF_APPLIQUE;

    REBVAL *applicand = ARG(applicand);

    // Need to do this up front, because it captures f->dsp.  Note that the
    // EVAL_FLAG_PROCESS_ACTION causes the evaluator to jump straight to the
    // point in the switch() where a function is invoked.
    //
    DECLARE_END_FRAME (f, EVAL_MASK_DEFAULT);

    // Argument can be a literal action (APPLY :APPEND) or a WORD!/PATH!.
    // If it is a path, we push the refinements to the stack so they can
    // be taken into account, e.g. APPLY 'APPEND/ONLY/DUP pushes /ONLY, /DUP
    //
    REBDSP lowest_ordered_dsp = DSP;
    if (Get_If_Word_Or_Path_Throws(
        D_OUT,
        applicand,
        SPECIFIED,
        true // push_refinements, don't specialize ACTION! on 'APPEND/ONLY/DUP
    )){
        return R_THROWN;
    }

    if (not IS_ACTION(D_OUT))
        fail (PAR(applicand));
    Move_Value(applicand, D_OUT);

    // Make a FRAME! for the ACTION!, weaving in the ordered refinements
    // collected on the stack (if any).  Any refinements that are used in
    // any specialization level will be pushed as well, which makes them
    // out-prioritize (e.g. higher-ordered) than any used in a PATH! that
    // were pushed during the Get of the ACTION!.
    //
    struct Reb_Binder binder;
    INIT_BINDER(&binder);
    REBCTX *exemplar = Make_Context_For_Action_Push_Partials(
        applicand,
        f->dsp_orig, // lowest_ordered_dsp of refinements to weave in
        &binder
    );
    Manage_Array(CTX_VARLIST(exemplar)); // binding code into it

    // Bind any SET-WORD!s in the supplied code block into the FRAME!, so
    // e.g. APPLY 'APPEND [VALUE: 10]` will set VALUE in exemplar to 10.
    //
    // !!! Today's implementation mutates the bindings on the passed-in block,
    // like R3-Alpha's MAKE OBJECT!.  See Virtual_Bind_Deep_To_New_Context()
    // for potential future directions.
    //
    Bind_Values_Inner_Loop(
        &binder,
        VAL_ARRAY_AT_MUTABLE_HACK(ARG(def)),  // mutates bindings
        exemplar,
        FLAGIT_KIND(REB_SET_WORD),  // types to bind (just set-word!),
        0,  // types to "add midstream" to binding as we go (nothing)
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
    bool def_threw = Do_Any_Array_At_Throws(temp, ARG(def), SPECIFIED);
    DROP_GC_GUARD(exemplar);

    assert(CTX_KEYS_HEAD(exemplar) == ACT_PARAMS_HEAD(VAL_ACTION(applicand)));
    f->param = CTX_KEYS_HEAD(exemplar);
    REBCTX *stolen = Steal_Context_Vars(
        exemplar,
        NOD(VAL_ACTION(applicand))
    );
    INIT_LINK_KEYSOURCE(stolen, NOD(f));  // changes CTX_KEYS_HEAD result

    if (def_threw) {
        Free_Unmanaged_Array(CTX_VARLIST(stolen)); // could TG_Reuse it
        RETURN (temp);
    }

    if (not REF(opt)) {
        //
        // If nulls are taken literally as null arguments, then no arguments
        // are gathered at the callsite, so the "ordering information"
        // on the stack isn't needed.  Eval_Core() will just treat a
        // slot with an INTEGER! for a refinement as if it were "true".
        //
        f->flags.bits |= EVAL_FLAG_FULLY_SPECIALIZED;
        DS_DROP_TO(lowest_ordered_dsp); // zero refinements on stack, now
    }

    Push_Frame(D_OUT, f);
    f->varlist = CTX_VARLIST(stolen);
    f->rootvar = CTX_ROOTVAR(stolen);
    f->arg = f->rootvar + 1;
    // f->param assigned above
    f->special = f->arg; // signal only type-check the existing data
    INIT_FRM_PHASE(f, VAL_ACTION(applicand));
    FRM_BINDING(f) = VAL_BINDING(applicand);

    Begin_Prefix_Action(f, VAL_ACTION_LABEL(applicand));

    bool action_threw = Process_Action_Throws(f);
    assert(action_threw or IS_END(f->feed->value));  // we started at END_FLAG

    Drop_Frame(f);

    if (action_threw)
        return R_THROWN;

    return D_OUT;
}
