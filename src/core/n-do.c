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
//      return: [<opt> <void> any-value!]
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
        OUT,  // reeval :comment "this should leave old input"
        frame_,
        ARG(value),
        flags,
        enfix
    )){
        return_thrown (OUT);
    }

    if (Is_Stale(OUT))
        return_void (OUT);

    Clear_Stale_Flag(OUT);
    return OUT;  // don't clear stale flag...act invisibly
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
//      >> 10 >- lib.(print "Hi!" first [multiply]) 20
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
        return ARG(left);  // ...because evaluator wants `help <-` to work

    // It's best for SHOVE to do type checking here, as opposed to setting
    // some kind of EVAL_FLAG_SHOVING and passing that into the evaluator, then
    // expecting it to notice if you shoved into an INTEGER! or something.
    //
    // !!! To get the feature working as a first cut, this doesn't try get too
    // fancy with apply-like mechanics and slipstream refinements on the
    // stack to enfix functions with refinements.  It specializes the ACTION!.
    // We can do better, but seeing as how you couldn't call enfix actions
    // with refinements *at all* before, this is a step up.

    REBVAL *shovee = ARG(right); // reuse arg cell for the shoved-into

    if (IS_WORD(f_value) or IS_PATH(f_value) or IS_TUPLE(f_value)) {
        Get_Var_May_Fail(
            OUT, // can't eval directly into arg slot
            f_value,
            f_specifier,
            false
        );
        Move_Cell(shovee, OUT);
    }
    else if (IS_GROUP(f_value)) {
        if (Do_Any_Array_At_Throws(RESET(OUT), f_value, f_specifier))
            return_thrown (OUT);

        Move_Cell(shovee, OUT);  // can't eval directly into arg slot
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
    if (REF(set)) {
        if (IS_SET_WORD(left)) {
            Copy_Cell(OUT, Lookup_Word_May_Fail(left, SPECIFIED));
        }
        else if (IS_SET_PATH(left) or IS_SET_TUPLE(left)) {
            f->feed->gotten = nullptr;  // calling arbitrary code, may disrupt
            composed_set_path = rebValue("compose @", left);
            REBVAL *temp = rebValue("get @", composed_set_path);
            Copy_Cell(OUT, temp);
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
        if (Eval_Value_Throws(OUT, left, SPECIFIED))
            return_thrown (OUT);
    }
    else {
        Copy_Cell(OUT, left);
        if (GET_CELL_FLAG(left, UNEVALUATED))
            SET_CELL_FLAG(OUT, UNEVALUATED);
    }

    REBFLGS flags = EVAL_MASK_DEFAULT;
    SET_FEED_FLAG(frame_->feed, NEXT_ARG_FROM_OUT);

    if (Reevaluate_In_Subframe_Maybe_Stale_Throws(
        OUT,
        frame_,
        shovee,
        flags,
        enfix
    )){
        rebRelease(composed_set_path);  // ok if nullptr
        return_thrown (OUT);
    }

    assert(not Is_Stale(OUT));  // !!! can this happen?

    if (REF(set)) {
        if (IS_SET_WORD(left)) {
            Decay_If_Isotope(OUT);
            Copy_Cell(Sink_Word_May_Fail(left, SPECIFIED), OUT);
        }
        else if (IS_SET_PATH(left) or IS_SET_TUPLE(left)) {
            f->feed->gotten = nullptr;  // calling arbitrary code, may disrupt
            rebElide("set @", composed_set_path, "@", NULLIFY_NULLED(OUT));
            rebRelease(composed_set_path);
        }
        else
            assert(false); // SET-WORD!/SET-PATH! was checked above
    }

    return OUT;
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
    INIT_BONUS_KEYSOURCE(varlist, f);

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
//  {Evaluates source code (see also EVAL for stepwise or invisible evaluation)}
//
//      return: [<opt> any-value!]
//      source "Block of code, or indirect specification to find/make it" [
//          <blank>  ; opts out of the DO, returns null
//          block!  ; source code in block form (see EVALUATE for other kinds)
//          text!  ; source code in text form
//          binary!  ; treated as UTF-8
//          url!  ; load code from URL via protocol
//          file!  ; load code from file on local disk
//          tag!  ; load relative to system.script.name
//          the-word!  ; module name (URL! looked up from table)
//          error!  ; should use FAIL instead
//          action!  ; will only run arity 0 actions (avoids DO variadic)
//          frame!  ; acts like APPLY (voids are optionals, not unspecialized)
//          varargs!  ; simulates as if frame! or block! is being executed
//          quoted!  ; removes quote level
//      ]
//      /args "Sets system.script.args if doing a script (usually a TEXT!)"
//          [any-value!]
//      /only "Don't catch QUIT (default behavior for BLOCK!)"
//  ]
//
REBNATIVE(do)
{
    INCLUDE_PARAMS_OF_DO;
    assert(ACT_HAS_RETURN(FRM_PHASE(frame_)));

    REBVAL *source = ARG(source);

    // Note: See also, EVALUATE
    //
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
    SET_CELL_FLAG(source, PROTECTED);  // maybe only GC reference, keep!
  #endif

    switch (VAL_TYPE(source)) {
      //
      // DO only takes BLOCK! as input.  The reason is that it might be
      // considered deceptive if DO of a block like ^[1 + 2] didn't return
      // a QUOTED! 3.  It also doesn't provide accommodation for invisible
      // products; a DO of a script that's empty is void.  This makes the
      // code here simpler.
      //
      case REB_BLOCK : {
        if (Do_Any_Array_At_Throws(SET_END(OUT), source, SPECIFIED))
            return_thrown (OUT);
        break; }

      case REB_VARARGS : {
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
            if (Do_Any_Array_At_Throws(SET_END(OUT), position, SPECIFIED)) {
                //
                // !!! A BLOCK! varargs doesn't technically need to "go bad"
                // on a throw, since the block is still around.  But a FRAME!
                // varargs does.  This will cause an assert if reused, and
                // having BLANK! mean "thrown" may evolve into a convention.
                //
                Init_Trash(position);
                return_thrown (OUT);
            }

            SET_END(position); // convention for shared data at end point
            return OUT;
        }

        REBFRM *f;
        if (not Is_Frame_Style_Varargs_May_Fail(&f, source))
            panic (source); // Frame is the only other type

        // By definition, we are in the middle of a function call in the frame
        // the varargs came from.  It's still on the stack, and we don't want
        // to disrupt its state.  Use a subframe.

        if (IS_END(f->feed->value)) {
            Init_None(OUT);
            return OUT;
        }

        REBFLGS flags = EVAL_MASK_DEFAULT | EVAL_FLAG_OVERLAP_OUTPUT;
        DECLARE_FRAME (subframe, f->feed, flags);

        bool threw;
        Push_Frame(SET_END(OUT), subframe);
        do {
            threw = Eval_Step_Maybe_Stale_Throws(OUT, subframe);
        } while (not threw and NOT_END(f->feed->value));
        Drop_Frame(subframe);

        if (threw)
            return_thrown (OUT);

        Clear_Stale_Flag(OUT);
        Reify_Eval_Out_Plain(OUT);
        break; }

      case REB_THE_WORD : goto do_string;
      case REB_BINARY : goto do_string;
      case REB_TEXT : goto do_string;
      case REB_URL : goto do_string;
      case REB_FILE : goto do_string;
      case REB_TAG : goto do_string;

      do_string : {
        UNUSED(REF(args)); // detected via `value? :arg`

        if (rebRunThrows(
            SET_END(OUT),
            true,  // fully = true, error if not all arguments consumed
            Sys(SYM_DO_P),
            source,
            rebQ(REF(args)),
            REF(only) ? Lib(TRUE) : Lib(FALSE)
        )){
            return_thrown (OUT);
        }
        return OUT;
      }

      case REB_ERROR :
        //
        // FAIL is the preferred operation for triggering errors, as it has
        // a natural behavior for blocks passed to construct readable messages
        // and "FAIL X" more clearly communicates a failure than "DO X"
        // does.  However DO of an ERROR! would have to raise an error
        // anyway, so it might as well raise the one it is given...and this
        // allows the more complex logic of FAIL to be written in Rebol code.
        //
        fail (VAL_CONTEXT(source));

      case REB_ACTION :
        //
        // Ren-C will only run arity 0 functions from DO, otherwise REEVAL
        // must be used.  Look for the first non-local parameter to tell.
        //
        if (First_Unspecialized_Param(nullptr, VAL_ACTION(source)))
            fail (Error_Do_Arity_Non_Zero_Raw());

        if (Eval_Value_Throws(SET_END(OUT), source, SPECIFIED))
            return_thrown (OUT);
        break;

      case REB_FRAME :
        if (Do_Frame_Maybe_Stale_Throws(SET_END(OUT), source))
            return_thrown (OUT); // prohibits recovery from exits
        Clear_Stale_Flag(OUT);
        Reify_Eval_Out_Plain(OUT);
        break;

      case REB_QUOTED :
        Copy_Cell(OUT, ARG(source));
        return Unquotify(OUT, 1);

      default :
        fail (Error_Do_Arity_Non_Zero_Raw());  // https://trello.com/c/YMAb89dv
    }

    return OUT;
}


//
//  evaluate: native [
//
//  {Perform a single evaluator step, returning the next source position}
//
//      return: "Value from the step"
//          [<opt> <void> any-value!]
//      next: "<output> Next expression position in block"
//          [any-array! varargs!]
//
//      source [
//          <blank>  ; useful for `evaluate try ...` scenarios when no match
//          any-array!  ; source code in block form
//          action!
//          frame!
//          varargs!  ; simulates as if frame! or block! is being executed
//      ]
//  ]
//
REBNATIVE(evaluate)
{
    INCLUDE_PARAMS_OF_EVALUATE;

    REBVAL *source = ARG(source);  // may be only GC reference, don't lose it!

    // Note: See also, DO
    //
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
    SET_CELL_FLAG(ARG(source), PROTECTED);
  #endif

    REBVAL *next = ARG(next);

    if (ANY_ARRAY(source)) {
        if (VAL_LEN_AT(source) == 0) {  // `evaluate []` is invisible intent
            // leave OUT as is
            Init_Nulled(source);
        }
        else {
            DECLARE_FEED_AT_CORE (feed, source, SPECIFIED);
            assert(NOT_END(feed->value));  // checked for VAL_LEN_AT() == 0

            bool threw;
            if (REF(next)) {  // only one step, want the output position
                DECLARE_FRAME (
                    f,
                    feed,
                    EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED
                );

                Push_Frame(SPARE, f);
                threw = Eval_Maybe_Stale_Throws(f);
                if (threw) {
                    Drop_Frame(f);
                    return_thrown (SPARE);
                }

                // !!! Since we're passing in a clear cell, we don't really
                // care about the stale (it's stale if the cell is still END).
                //
                Clear_Stale_Flag(SPARE);

                if (not threw) {
                    VAL_INDEX_UNBOUNDED(source) = FRM_INDEX(f);  // new index

                    // <ay have been a LET statement in the code.  If there
                    // was, we have to incorporate the binding it added into
                    // the reported state *somehow*.  Right now we add it to the
                    // block we give back...this gives rise to questionable
                    // properties, such as if the user goes backward in the
                    // block and were to evaluate it again:
                    //
                    // https://forum.rebol.info/t/1496
                    //
                    // Right now we can politely ask "don't do that", but better
                    // would probably be to make EVALUATE return something with
                    // more limited privileges... more like a FRAME!/VARARGS!.
                    //
                    INIT_BINDING_MAY_MANAGE(source, f_specifier);
                }

                Drop_Frame(f);

                if (REF(next))
                    rebElide(Lib(SET), rebQ(next), source);
            }
            else {  // assume next position not requested means run-to-end
                if (Do_Feed_To_End_Maybe_Stale_Throws(
                    SPARE,
                    feed,
                    EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED
                )){
                    return_thrown (SPARE);
                }

                Clear_Stale_Flag(SPARE);

                if (REF(next))
                    rebElide(Lib(SET), rebQ(next), rebQ(Lib(NULL)));
            }
        }
        // update variable
    }
    else switch (VAL_TYPE(source)) {

      case REB_FRAME :
        //
        // !!! It is likely that the return result for the NEXT: will actually
        // be a FRAME! when the input to EVALUATE is a BLOCK!, so that the
        // LET bindings can be preserved.  Binding is still a mess when it
        // comes to questions like backtracking in blocks, so review.
        //
        if (REF(next))
            fail ("/NEXT Behavior not implemented for FRAME! in EVALUATE");

        if (Do_Frame_Maybe_Stale_Throws(SPARE, source))
            return_thrown (SPARE);  // prohibits recovery from exits
        Clear_Stale_Flag(SPARE);
        break;

      case REB_ACTION: {
        //
        // Ren-C will only run arity 0 functions from DO, otherwise REEVAL
        // must be used.  Look for the first non-local parameter to tell.
        //
        if (First_Unspecialized_Param(nullptr, VAL_ACTION(source)))
            fail (Error_Do_Arity_Non_Zero_Raw());

        if (Eval_Value_Maybe_Stale_Throws(SPARE, source, SPECIFIED))
            return_thrown (SPARE);

        Clear_Stale_Flag(SPARE);
        break; }

      case REB_VARARGS : {
        assert(IS_VARARGS(source));

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
                SPARE,
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
                return_thrown (SPARE);
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
            if (Eval_Step_In_Subframe_Maybe_Stale_Throws(SPARE, f, flags))
                return_thrown (SPARE);

            Clear_Stale_Flag(SPARE);
        }
        break; }

      default:
        fail (PAR(source));
    }

    if (IS_TRUTHY(next))
        Set_Var_May_Fail(next, SPECIFIED, source);

    if (IS_VOID(SPARE))
        return_void (OUT);

    return SPARE;
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
        if (not Did_Get_Binding_Of(OUT, restartee))
            fail ("No context found from restartee in REDO");

        if (not IS_FRAME(OUT))
            fail ("Context of restartee in REDO is not a FRAME!");

        Move_Cell(restartee, OUT);
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
    Copy_Cell(SPARE, Lib(REDO));
    INIT_VAL_ACTION_BINDING(SPARE, c);

    // The FRAME! contains its ->phase and ->binding, which should be enough
    // to restart the phase at the point of parameter checking.  Make that
    // the actual value that Eval_Core() catches.
    //
    return Init_Thrown_With_Label(OUT, restartee, SPARE);
}


//
//  applique: native [
//
//  {Invoke an ACTION! with all required arguments specified}
//
//      return: [<opt> <void> any-value!]
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
        NONE_ISOTOPE
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
    Init_Frame(SPARE, exemplar, ANONYMOUS);

    EVARS e;
    Init_Evars(&e, SPARE);  // CTX_ARCHETYPE(exemplar) is phased, sees locals

    while (Did_Advance_Evars(&e)) {
        //
        // !!! Is it necessary to do the `~` to null conversion here, or will
        // the frame invocation do it automatically?
        //
        if (Is_None(e.var))
            Init_Nulled(e.var);

        Remove_Binder_Index(&binder, KEY_SYMBOL(e.key));
    }
    SHUTDOWN_BINDER(&binder); // must do before running code that might BIND

    Shutdown_Evars(&e);
  }

    // !!! We have to push the frame here, because it won't be cleaned up if
    // there are failures in the code otherwise.  Review.
    //
    Push_Frame(OUT, f);

    // Run the bound code, ignore evaluative result (unless thrown)
    //
    PUSH_GC_GUARD(exemplar);
    DECLARE_LOCAL (temp);
    bool def_threw = Do_Any_Array_At_Throws(temp, ARG(def), SPECIFIED);
    DROP_GC_GUARD(exemplar);

    if (def_threw) {
        Drop_Frame(f);
        return Copy_Cell(f->out, temp);
    }

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

    f->varlist = varlist;
    f->rootvar = CTX_ROOTVAR(exemplar);
    INIT_BONUS_KEYSOURCE(varlist, f);

    INIT_FRM_PHASE(f, VAL_ACTION(action));
    INIT_FRM_BINDING(f, VAL_ACTION_BINDING(action));

    Begin_Prefix_Action(f, VAL_ACTION_LABEL(action));

    bool action_threw = Process_Action_Maybe_Stale_Throws(f);
    assert(action_threw or IS_END(f->feed->value));  // we started at END_FLAG

    Drop_Frame(f);

    if (action_threw)
        return_thrown (OUT);

    // The input may have been stale coming in, or the application of the frame
    // may have been stale.  We don't make any decisions either way when it
    // comes to the evaluation stream.  If a SET-WORD! or ^META operation
    // needs to know, it does its evaluation into an empty cell and then
    // checks for emptiness.  So be agnostic here about where the stale bit
    // actually came from.
    //
    if (Is_Stale(OUT))
        return_void (OUT);

    return OUT;
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
//      <local> frame
//  ]
//
REBNATIVE(apply)
{
    INCLUDE_PARAMS_OF_APPLY;

    REBVAL *action = ARG(action);
    REBVAL *args = ARG(args);

    REBVAL *frame = ARG(frame);

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

    Init_Frame(frame, exemplar, ANONYMOUS);  // Note: GC guards the exemplar

  blockscope {
    DECLARE_FRAME_AT (f, args, EVAL_MASK_DEFAULT);
    Push_Frame(nullptr, f);

    EVARS e;
    Init_Evars(&e, frame);  // CTX_ARCHETYPE(exemplar) is phased, sees locals

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

        if (Eval_Step_Maybe_Stale_Throws(RESET(SPARE), f)) {
            Move_Cell(OUT, SPARE);
            arg_threw = true;
            goto end_loop;
        }

        if (Is_Stale(SPARE)) {  // no output
            //
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

            REBLEN index = Find_Symbol_In_Context(frame, name, false);
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
                IS_LOGIC(SPARE)
                and GET_PARAM_FLAG(param, REFINEMENT)
                and Is_Typeset_Empty(param)
            ){
                if (VAL_LOGIC(SPARE))
                    Init_Blackhole(SPARE);
                else
                    Init_Nulled(SPARE);
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
                    or GET_PARAM_FLAG(e.param, SKIPPABLE)
                ){
                    // We treat <skip> parameters as if they can only be
                    // requested by name, like a refinement.  This is because
                    // the evaluative nature of APPLY is not compatible with
                    // the quoting requirement of skippability.
                    //
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

        Move_Cell(var, SPARE);
        if (VAL_PARAM_CLASS(param) == PARAM_CLASS_META)
            Meta_Quotify(var);
    }

  end_loop:

    // We need to remove the binder indices, whether we are raising an error
    // or not.  But we also want any fields not assigned to be set to `~`.
    // (We wanted to avoid the situation where someone purposefully set a
    // meta-parameter to `~` being interpreted as never setting a field).
    //
    Shutdown_Evars(&e);

    Drop_Frame(f);

    Init_Evars(&e, frame);
    while (Did_Advance_Evars(&e)) {
        if (not arg_threw and not error and IS_TAG(e.var))
            if (VAL_SERIES(e.var) == VAL_SERIES(Root_Unspecialized_Tag))
                Init_None(e.var);

        /* Remove_Binder_Index(&binder, KEY_SYMBOL(e.key)); */
    }
    /* SHUTDOWN_BINDER(&binder); */
    Shutdown_Evars(&e);

    if (error)
        fail (error);  // only safe to fail *AFTER* we have cleared binder

    if (arg_threw)
        return_thrown (OUT);
  }

    // Need to do this up front, because it captures f->dsp.
    //
    DECLARE_END_FRAME (
        f,
        EVAL_MASK_DEFAULT
            | EVAL_FLAG_FULLY_SPECIALIZED
            | FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING)  // skips fulfillment
    );

    Push_Frame(OUT, f);

    f->varlist = varlist;
    f->rootvar = CTX_ROOTVAR(exemplar);
    INIT_BONUS_KEYSOURCE(varlist, f);

    INIT_FRM_PHASE(f, VAL_ACTION(action));
    INIT_FRM_BINDING(f, VAL_ACTION_BINDING(action));

    Begin_Prefix_Action(f, VAL_ACTION_LABEL(action));

    bool action_threw = Process_Action_Maybe_Stale_Throws(f);
    assert(action_threw or IS_END(f->feed->value));

    Drop_Frame(f);

    if (action_threw)
        return_thrown (OUT);

    if (Is_Stale(OUT))
        return_void (OUT);

    return OUT;
}
