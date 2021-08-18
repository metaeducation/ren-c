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
// generalized evaluator is needed, use the special-purpose REEVAL(UATE).
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
    )){
        return R_THROWN;
    }
    return D_OUT;  // don't clear stale flag...act invisibly
}


//
//  shove: native [
//
//  {Shove a parameter into an ACTION! as its first argument}
//
//      return: [<opt> any-value!]
//          "REVIEW: How might this handle shoving enfix invisibles?"
//      :left [<end> <opt> any-value!]
//          "Requests parameter convention based on enfixee's first argument"
//      'right [<variadic> <end> any-value!]
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

        Copy_Cell(shovee, D_OUT);
    }
    else if (IS_GROUP(f_value)) {
        if (Do_Any_Array_At_Throws(D_OUT, f_value, f_specifier))
            return R_THROWN;
        if (IS_END(D_OUT))  // !!! need SHOVE frame for type error
            fail ("GROUP! passed to SHOVE did not evaluate to content");

        Copy_Cell(shovee, D_OUT);  // Note: can't eval directly into arg slot
    }
    else
        Copy_Cell(shovee, SPECIFIC(f_value));

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
    Init_Trash(D_OUT); // make sure we reassign it
  #endif

    if (REF(set)) {
        if (IS_SET_WORD(left)) {
            Copy_Cell(D_OUT, Lookup_Word_May_Fail(left, SPECIFIED));
        }
        else if (IS_SET_PATH(left) or IS_SET_TUPLE(left)) {
            f->feed->gotten = nullptr;  // calling arbitrary code, may disrupt
            composed_set_path = rebValue("compose @", left);
            REBVAL *temp = rebValue("get/hard @", composed_set_path);
            Copy_Cell(D_OUT, temp);
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
        Copy_Cell(D_OUT, left);
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

    assert(NOT_CELL_FLAG(D_OUT, OUT_NOTE_STALE));  // !!! can this happen?

    if (REF(set)) {
        if (IS_SET_WORD(left)) {
            Decay_If_Nulled(D_OUT);
            Copy_Cell(Sink_Word_May_Fail(left, SPECIFIED), D_OUT);
        }
        else if (IS_SET_PATH(left) or IS_SET_TUPLE(left)) {
            f->feed->gotten = nullptr;  // calling arbitrary code, may disrupt
            rebElide("set/hard @", composed_set_path, "@", NULLIFY_NULLED(D_OUT));
            rebRelease(composed_set_path);
        }
        else
            assert(false); // SET-WORD!/SET-PATH! was checked above
    }

    return D_OUT;
}


//
//  Do_Frame_Ctx_Throws: C
//
bool Do_Frame_Ctx_Throws(
    REBVAL *out,
    REBCTX *c,
    REBCTX *binding,
    option(const REBSYM*) label
){
    REBFLGS flags = EVAL_MASK_DEFAULT
        | EVAL_FLAG_FULLY_SPECIALIZED
        | FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING);

    DECLARE_END_FRAME (f, flags);
    Push_Frame(out, f);

    REBARR *varlist = CTX_VARLIST(c);
    f->varlist = varlist;
    f->rootvar = CTX_ROOTVAR(c);
    INIT_LINK_KEYSOURCE(varlist, f);

    assert(FRM_PHASE(f) == CTX_FRAME_ACTION(c));
    INIT_FRM_BINDING(f, binding);

    Begin_Prefix_Action(f, label);

    bool threw = Process_Action_Maybe_Stale_Throws(f);
    assert(threw or IS_END(f->feed->value));  // we started at END_FLAG

    Drop_Frame(f);
    return threw;
}


//
//  Do_Frame_Maybe_Stale_Throws: C
//
bool Do_Frame_Maybe_Stale_Throws(REBVAL *out, REBVAL *frame) {
    if (IS_FRAME_PHASED(frame))  // see REDO for tail-call recursion
        fail ("Use REDO to restart a running FRAME! (not DO)");

    REBCTX *c = VAL_CONTEXT(frame);  // checks for INACCESSIBLE

    if (GET_SUBCLASS_FLAG(VARLIST, CTX_VARLIST(c), FRAME_HAS_BEEN_INVOKED))
        fail (Error_Stale_Frame_Raw());

    return Do_Frame_Ctx_Throws(
        out,
        c,
        VAL_FRAME_BINDING(frame),
        VAL_FRAME_LABEL(frame)
    );
}


//
//  do: native [
//
//  {Evaluates a block of source code (directly or fetched according to type)}
//
//      return: [<opt> <invisible> any-value!]
//      source [
//          <blank>  ; opts out of the DO, returns null
//          block!  ; source code in block form
//          get-block!  ; same
//          meta-block!  ; same
//          group!  ; same
//          get-group!  ; same
//          meta-group!  ; same
//          text!  ; source code in text form
//          binary!  ; treated as UTF-8
//          url!  ; load code from URL via protocol
//          file!  ; load code from file on local disk
//          tag!  ; module name (URL! looked up from table)
//          error!  ; should use FAIL instead
//          action!  ; will only run arity 0 actions (avoids DO variadic)
//          frame!  ; acts like APPLY (voids are optionals, not unspecialized)
//          varargs!  ; simulates as if frame! or block! is being executed
//          quoted!  ; removes quote level
//      ]
//      /args "Sets system/script/args if doing a script (usually a TEXT!)"
//          [any-value!]
//      /only "Don't catch QUIT (default behavior for BLOCK!)"
//  ]
//
REBNATIVE(do)
{
    INCLUDE_PARAMS_OF_DO;
    assert(ACT_HAS_RETURN(FRM_PHASE(frame_)));

    REBVAL *source = ARG(source);

    // Due to the mechanics of true invisibility, `1 + 2 do [comment "hi"]`
    // cannot rely on OUT_NOTE_STALE...because it clears the stale flag.  This
    // may be a bug or bad mechanic overall, but getting the right information
    // in this case (stale vs. invisible) requires making a note and starting
    // from end.
    //
    SET_END(D_OUT);  // !!! need to defeat enfix invisibles, review

    // If `source` is not const, tweak it to be explicitly mutable--because
    // otherwise, it would wind up inheriting the FEED_MASK_CONST of our
    // currently executing frame.  That's no good for `repeat 2 [do block]`,
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
      case REB_META_BLOCK:
      case REB_GET_BLOCK:
      case REB_GROUP:
      case REB_META_GROUP:
      case REB_GET_GROUP: {
        DECLARE_FEED_AT_CORE (feed, source, SPECIFIED);
        if (Do_Feed_To_End_Maybe_Stale_Throws(
            D_OUT,
            feed,
            EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED
        )){
            return R_THROWN;
        }
        goto handle_void; }  // may be stale, which would mean invisible

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
                Init_Trash(position);
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

        Init_Void(D_OUT);
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

        CLEAR_CELL_FLAG(D_OUT, OUT_NOTE_STALE);
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
        if (First_Unspecialized_Param(nullptr, VAL_ACTION(source)))
            fail (Error_Do_Arity_Non_Zero_Raw());

        if (Eval_Value_Maybe_Stale_Throws(D_OUT, source, SPECIFIED))
            return R_THROWN;
        break; }

      case REB_FRAME: {
        if (Do_Frame_Maybe_Stale_Throws(D_OUT, source))
            return R_THROWN; // prohibits recovery from exits

        goto handle_void; }

      case REB_QUOTED:
        Copy_Cell(D_OUT, ARG(source));
        return Unquotify(D_OUT, 1);

      default:
        fail (Error_Do_Arity_Non_Zero_Raw());  // https://trello.com/c/YMAb89dv
    }

  handle_void:

    if (IS_END(D_OUT))  // didn't make new result (non-/VOID forced END)
        Init_Void(D_OUT);  // may be overridden by Init_Stale()

    return D_OUT;
}


//
//  evaluate: native [
//
//  {Perform a single evaluator step, returning the next source position}
//
//      return: "Next position (quoted if result requested and invisible)"
//          [<opt> quoted! block! group! varargs!]
//      result: "<output> Value from the step (invisibles quote return pos)"
//          [<opt> any-value!]
//
//      source [
//          <blank>  ; useful for `evaluate try ...` scenarios when no match
//          quoted!  ; accepts quoted source (may carry bit from prior eval)
//          block!  ; source code in block form
//          group!  ; same as block (or should it have some other nuance?)
//          varargs!  ; simulates as if frame! or block! is being executed
//      ]
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
        if (VAL_LEN_AT(source) == 0) {  // `evaluate []` should return null
            Init_Nulled(D_OUT);
            Init_Void(D_SPARE);
        }
        else {
            DECLARE_FEED_AT_CORE (feed, source, SPECIFIED);
            assert(NOT_END(feed->value));  // checked for VAL_LEN_AT() == 0

            DECLARE_FRAME (
                f,
                feed,
                EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED
            );

            SET_END(D_SPARE);
            Push_Frame(D_SPARE, f);
            bool threw = Eval_Throws(f);  // only one step of evaluation

            if (not threw) {
                Copy_Cell(D_OUT, source);

                VAL_INDEX_UNBOUNDED(D_OUT) = FRM_INDEX(f);  // new index

                // There may have been a LET statement in the code.  If there
                // was, then we have to incorporate the binding it added into
                // the reported state *somehow*.  Right now we add it to the
                // block we give back...though this gives rise to questionable
                // properties, such as if the user goes backward in the block
                // and were to evaluate it again:
                //
                // https://forum.rebol.info/t/1496
                //
                // Right now we can politely ask "don't do that", but better
                // would probably be to make EVALUATE return something with
                // more limited privileges... more like a FRAME!/VARARGS!.
                //
                INIT_BINDING_MAY_MANAGE(D_OUT, f_specifier);
            }

            Drop_Frame(f);

            if (threw) {
                Move_Cell(D_OUT, D_SPARE);
                return R_THROWN;
            }

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
                Init_Void(D_SPARE);
                Quotify(D_OUT, 1);  // void-is-invisible signal on array
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
                Init_Trash(position);
                Move_Cell(D_OUT, D_SPARE);
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
                Move_Cell(D_OUT, D_SPARE);
                return R_THROWN;
            }

            if (IS_END(D_SPARE))  // remainder just comments and invisibles
                return nullptr;
        }

        Copy_Cell(D_OUT, source);  // VARARGS! will have updated position
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
//  redo: native [
//
//  {Restart a frame's action from the top with its current state}
//
//      return: []  ; !!! notation for divergent function?
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

        Copy_Cell(restartee, D_OUT);
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
        if (ACT_KEYLIST(f->original) != ACT_KEYLIST(VAL_ACTION(sibling)))
            fail ("/OTHER function passed to REDO has incompatible FRAME!");

        INIT_VAL_FRAME_PHASE(restartee, VAL_ACTION(sibling));
        INIT_VAL_FRAME_BINDING(restartee, VAL_ACTION_BINDING(sibling));
    }

    // We need to cooperatively throw a restart instruction up to the level
    // of the frame.  Use REDO as the throw label that Eval_Core() will
    // identify for that behavior.
    //
    Copy_Cell(D_OUT, NATIVE_VAL(redo));
    INIT_VAL_ACTION_BINDING(D_OUT, c);

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
//      action [action!]
//      def "Frame definition block (will be bound and evaluated)"
//          [block!]
//      /partial "Treat nulls as unspecialized <<experimental!>>"
//  ]
//
REBNATIVE(applique)
{
    INCLUDE_PARAMS_OF_APPLIQUE;

    REBVAL *action = ARG(action);

    // Need to do this up front, because it captures f->dsp.
    //
    DECLARE_END_FRAME (
        f,
        EVAL_MASK_DEFAULT
            | FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING)  // skips fulfillment
    );

    REBDSP lowest_ordered_dsp = DSP;  // could push refinements here

    // Make a FRAME! for the ACTION!, weaving in the ordered refinements
    // collected on the stack (if any).  Any refinements that are used in
    // any specialization level will be pushed as well, which makes them
    // out-prioritize (e.g. higher-ordered) than any used in a PATH! that
    // were pushed during the Get of the ACTION!.
    //
    struct Reb_Binder binder;
    INIT_BINDER(&binder);
    REBCTX *exemplar = Make_Context_For_Action_Push_Partials(
        action,
        f->dsp_orig, // lowest_ordered_dsp of refinements to weave in
        &binder,
        UNSET_VALUE
    );
    REBARR *varlist = CTX_VARLIST(exemplar);
    Manage_Series(varlist); // binding code into it

    Virtual_Bind_Deep_To_Existing_Context(
        ARG(def),
        exemplar,
        &binder,
        REB_SET_WORD
    );

    // Reset all the binder indices to zero, balancing out what was added.
    //
  blockscope {
    Init_Frame(D_SPARE, exemplar, ANONYMOUS);

    EVARS e;
    Init_Evars(&e, D_SPARE);  // CTX_ARCHETYPE(exemplar) is phased, sees locals

    while (Did_Advance_Evars(&e)) {
        //
        // !!! This is another case where if you want to literaly apply
        // with ~unset~ you have to manually hide the frame key.
        //
        if (Is_Unset(e.var))
            Init_Nulled(e.var);

        Remove_Binder_Index(&binder, KEY_SYMBOL(e.key));
    }
    SHUTDOWN_BINDER(&binder); // must do before running code that might BIND

    Shutdown_Evars(&e);
  }

    // Run the bound code, ignore evaluative result (unless thrown)
    //
    PUSH_GC_GUARD(exemplar);
    DECLARE_LOCAL (temp);
    bool def_threw = Do_Any_Array_At_Throws(temp, ARG(def), SPECIFIED);
    DROP_GC_GUARD(exemplar);

    if (def_threw)
        RETURN (temp);

    if (not REF(partial)) {
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

    f->varlist = varlist;
    f->rootvar = CTX_ROOTVAR(exemplar);
    INIT_LINK_KEYSOURCE(varlist, f);

    INIT_FRM_PHASE(f, VAL_ACTION(action));
    INIT_FRM_BINDING(f, VAL_ACTION_BINDING(action));

    Begin_Prefix_Action(f, VAL_ACTION_LABEL(action));

    bool action_threw = Process_Action_Throws(f);
    assert(action_threw or IS_END(f->feed->value));  // we started at END_FLAG

    Drop_Frame(f);

    if (action_threw)
        return R_THROWN;

    return D_OUT;
}


//
//  apply: native [
//
//  {Invoke an ACTION! with all required arguments specified}
//
//      return: [<opt> any-value!]
//      action [action!]
//      args "Arguments and Refinements, e.g. [arg1 arg2 /ref refine1]"
//          [block!]
//  ]
//
REBNATIVE(apply)
{
    INCLUDE_PARAMS_OF_APPLY;

    REBVAL *action = ARG(action);
    REBVAL *args = ARG(args);

    REBDSP lowest_ordered_dsp = DSP;  // could push refinements here

    // Make a FRAME! for the ACTION!, weaving in the ordered refinements
    // collected on the stack (if any).  Any refinements that are used in
    // any specialization level will be pushed as well, which makes them
    // out-prioritize (e.g. higher-ordered) than any used in a PATH! that
    // were pushed during the Get of the ACTION!.
    //
    // !!! Binders cannot be held across evaluations at this time.  Do slow
    // lookups for refinements, but this is something that needs rethinking.
    //
    /*struct Reb_Binder binder;
    INIT_BINDER(&binder);*/
    REBCTX *exemplar = Make_Context_For_Action_Push_Partials(
        action,
        lowest_ordered_dsp, // lowest_ordered_dsp of refinements to weave in
        nullptr /* &binder */,
        Root_Unspecialized_Tag  // is checked for by *identity*, not value!
    );
    REBARR *varlist = CTX_VARLIST(exemplar);
    Manage_Series(varlist); // Putting into a frame

    DS_DROP_TO(lowest_ordered_dsp);  // !!! don't care about partials?

    Init_Frame(D_SPARE, exemplar, ANONYMOUS);  // Note: GC guards the exemplar

  blockscope {
    DECLARE_FRAME_AT (f, args, EVAL_MASK_DEFAULT);
    Push_Frame(nullptr, f);

    EVARS e;
    Init_Evars(&e, D_SPARE);  // CTX_ARCHETYPE(exemplar) is phased, sees locals

    REBCTX *error = nullptr;
    bool arg_threw = false;

    while (NOT_END(f_value)) {
        //
        // We do special handling if we see a /REFINEMENT ... that is taken
        // to mean we are naming the next argument.

        const REBSYM *name = nullptr;
        if (IS_PATH(f_value) and IS_REFINEMENT(f_value)) {
            name = VAL_REFINEMENT_SYMBOL(f_value);
            Fetch_Next_Forget_Lookback(f);

            // Two refinement labels in a row, not legal...treat it like the
            // next refinement is reaching a comma or end of block.
            //
            if (IS_PATH(f_value) and IS_REFINEMENT(f_value)) {
                Refinify(Init_Word(DS_PUSH(), name));
                error = Error_Need_Non_End_Raw(DS_TOP);
                DS_DROP();
                goto end_loop;
            }
        }

        if (Eval_Step_Throws(D_OUT, f)) {
            arg_threw = true;
            goto end_loop;
        }

        if (IS_END(D_OUT)) {  // no output
            // We let the frame logic inside the evaluator decide if we've
            // built a valid frame or not.  But the error we do check for is
            // if we were trying to fulfill a labeled refinement and didn't
            // get any value for it.
            //
            if (name) {
                Refinify(Init_Word(DS_PUSH(), name));
                error = Error_Need_Non_End_Raw(DS_TOP);
                DS_DROP();
                goto end_loop;
            }

            if (not IS_END(f_value))  // more input in feed so it was invisible
                continue;  // ...was a COMMA! or COMMENT, just keep going

            goto end_loop;
        }

        REBVAL *var;
        REBPAR *param;
        if (name) {
            /* REBLEN index = Get_Binder_Index_Else_0(&binder, name); */

            REBLEN index = Find_Symbol_In_Context(D_SPARE, name, false);
            if (index == 0) {
                Refinify(Init_Word(DS_PUSH(), name));
                error = Error_Bad_Parameter_Raw(DS_TOP);
                DS_DROP();
                goto end_loop;
            }
            var = CTX_VAR(exemplar, index);
            param = ACT_PARAM(VAL_ACTION(action), index);
            if (not (
                IS_TAG(var)
                and VAL_SERIES(var) == VAL_SERIES(Root_Unspecialized_Tag)
            )) {
                Refinify(Init_Word(DS_PUSH(), name));
                error = Error_Bad_Parameter_Raw(DS_TOP);
                DS_DROP();
                goto end_loop;
            }

            // Helpful service: convert LOGIC! to # or null for refinements
            // that take no argument.
            //
            if (
                IS_LOGIC(D_OUT)
                and GET_PARAM_FLAG(param, REFINEMENT)
                and Is_Typeset_Empty(param)
            ){
                if (VAL_LOGIC(D_OUT))
                    Init_Blackhole(D_OUT);
                else
                    Init_Nulled(D_OUT);
            }
        }
        else {
            while (true) {
                if (not Did_Advance_Evars(&e)) {
                    error = Error_Apply_Too_Many_Raw();
                    goto end_loop;
                }
                if (
                    VAL_PARAM_CLASS(e.param) == PARAM_CLASS_RETURN
                    or VAL_PARAM_CLASS(e.param) == PARAM_CLASS_OUTPUT
                    or GET_PARAM_FLAG(e.param, REFINEMENT)
                ){
                    continue;
                }
                if (
                    IS_TAG(e.var)
                    and VAL_SERIES(e.var) == VAL_SERIES(Root_Unspecialized_Tag)
                ){
                    break;
                }
            }
            var = e.var;
            param = e.param;
        }

        Move_Cell(var, D_OUT);
        if (VAL_PARAM_CLASS(param) == PARAM_CLASS_META)
            Meta_Quotify(var);
    }

  end_loop:

    Drop_Frame(f);

    // We need to remove the binder indices, whether we are raising an error
    // or not.  But we also want any fields not assigned to be set to ~unset~.
    // (We wanted to avoid the situation where someone purposefully set a
    // meta-parameter to ~unset~ being interpreted as never setting a field).
    //
    Shutdown_Evars(&e);

    Init_Evars(&e, D_SPARE);
    while (Did_Advance_Evars(&e)) {
        if (not arg_threw and not error and IS_TAG(e.var))
            if (VAL_SERIES(e.var) == VAL_SERIES(Root_Unspecialized_Tag))
                Init_Unset(e.var);

        /* Remove_Binder_Index(&binder, KEY_SYMBOL(e.key)); */
    }
    /* SHUTDOWN_BINDER(&binder); */
    Shutdown_Evars(&e);

    if (error)
        fail (error);  // only safe to fail *AFTER* we have cleared binder

    if (arg_threw)
        return R_THROWN;
  }

    // Need to do this up front, because it captures f->dsp.
    //
    DECLARE_END_FRAME (
        f,
        EVAL_MASK_DEFAULT
            | EVAL_FLAG_FULLY_SPECIALIZED
            | FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING)  // skips fulfillment
    );

    Push_Frame(D_OUT, f);

    f->varlist = varlist;
    f->rootvar = CTX_ROOTVAR(exemplar);
    INIT_LINK_KEYSOURCE(varlist, f);

    INIT_FRM_PHASE(f, VAL_ACTION(action));
    INIT_FRM_BINDING(f, VAL_ACTION_BINDING(action));

    Begin_Prefix_Action(f, VAL_ACTION_LABEL(action));

    bool action_threw = Process_Action_Throws(f);
    assert(action_threw or IS_END(f->feed->value));

    Drop_Frame(f);

    if (action_threw)
        return R_THROWN;

    return D_OUT;
}
