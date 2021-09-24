//
//  File: %c-action.c
//  Summary: "Central Interpreter Evaluator"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This file contains Process_Action_Throws(), which does the work of calling
// functions in the evaluator.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Process_Action_Throws() is LONG.  That is largely a purposeful choice.
//   Breaking it into functions would add overhead (in the debug build if not
//   also release builds) and prevent interesting tricks and optimizations.
//   It is separated into sections, and the invariants in each section are
//   made clear with comments and asserts.
//

#include "sys-core.h"



// The frame contains a "feed" whose ->value typically represents a "current"
// step in the feed.  But the evaluator is organized in a way that the
// notion of what is "current" can get out of sync with the feed.  An example
// would be when a SET-WORD! evaluates its right hand side, causing the feed
// to advance an arbitrary amount.
//
// So the frame has its own frame state for tracking the "current" position,
// and maintains the optional cache of what the fetched value of that is.
// These macros help make the code less ambiguous.
//
#undef f_value
#undef f_gotten
#define f_next f->feed->value
#define f_next_gotten f->feed->gotten

// In debug builds, the KIND_BYTE() calls enforce cell validity...but slow
// things down a little.  So we only use the checked version in the main
// switch statement.  This abbreviation is also shorter and more legible.
//
#define kind_current KIND_BYTE_UNCHECKED(v)


#if DEBUG_EXPIRED_LOOKBACK
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (f->feed->stress != nullptr)
#else
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (v == &f->feed->lookback)
#endif


// Anything without potential for invisible return starts f->out as reset.
// If there is potential for invisible return, the cell contents are left
// as is, but the output is marked "stale" to prevent its usage as input
// for an enfix function.
//
// Note: The incoming cell may be completely 0 bytes (unformatted space) if
// Process_Action() was called without passing through the Eval() step that
// transitions cells to have at minimum the NODE and CELL flags.  So setting
// CELL_FLAG_OUT_NOTE_STALE can't be done with SET_CELL_FLAG()
//
inline static void Expire_Out_Cell_Unless_Invisible(REBFRM *f) {
    REBACT *phase = FRM_PHASE(f);
    if (not ACT_HAS_RETURN(phase))
        f->out->header.bits |= (  // ^-- see note
            NODE_FLAG_NODE | NODE_FLAG_CELL | CELL_FLAG_OUT_NOTE_STALE
        );
    else {
        const REBKEY *key = ACT_KEYS_HEAD(phase);
        const REBPAR *param = ACT_PARAMS_HEAD(phase);
        assert(KEY_SYM(key) == SYM_RETURN);
        UNUSED(key);
        if (NOT_PARAM_FLAG(param, ENDABLE))
            RESET(f->out);
        else
            f->out->header.bits |= (  // ^-- see note
                NODE_FLAG_NODE | NODE_FLAG_CELL | CELL_FLAG_OUT_NOTE_STALE
            );
    }
}


// When arguments are hard quoted or soft-quoted, they don't call into the
// evaluator to do it.  But they need to use the logic of the evaluator for
// noticing when to defer enfix:
//
//     foo: func [...] [
//          return the 1 then ["this needs to be returned"]
//     ]
//
// If the first time the THEN was seen was not after the 1, but when the
// LIT ran, it would get deferred until after the RETURN.  This is not
// consistent with the pattern people expect.
//
// Returns TRUE if it set the flag.
//
bool Lookahead_To_Sync_Enfix_Defer_Flag(struct Reb_Feed *feed) {
    assert(NOT_FEED_FLAG(feed, DEFERRING_ENFIX));
    assert(not feed->gotten);

    CLEAR_FEED_FLAG(feed, NO_LOOKAHEAD);

    if (not IS_WORD(feed->value))
        return false;

    feed->gotten = Lookup_Word(feed->value, FEED_SPECIFIER(feed));

    if (not feed->gotten or not IS_ACTION(unwrap(feed->gotten)))
        return false;

    if (NOT_ACTION_FLAG(VAL_ACTION(unwrap(feed->gotten)), ENFIXED))
        return false;

    if (GET_ACTION_FLAG(VAL_ACTION(unwrap(feed->gotten)), DEFERS_LOOKBACK))
        SET_FEED_FLAG(feed, DEFERRING_ENFIX);
    return true;
}


//
//  Process_Action_Maybe_Stale_Throws: C
//
bool Process_Action_Maybe_Stale_Throws(REBFRM * const f)
{
  #if !defined(NDEBUG)
    assert(f->original);  // set by Begin_Action()
    Do_Process_Action_Checks_Debug(f);
  #endif

    if (not Is_Action_Frame_Fulfilling(f))
        goto dispatch;  // STATE_BYTE() belongs to the dispatcher if key=null

    switch (STATE_BYTE(f)) {
      case ST_ACTION_INITIAL_ENTRY:
        goto fulfill;

      case ST_ACTION_TYPECHECKING:
        goto typecheck_then_dispatch;

      default:
        assert(false);
    }

  fulfill:

    assert(DSP >= f->dsp_orig);  // path processing may push REFINEMENT!s

    assert(NOT_EVAL_FLAG(f, DOING_PICKUPS));

    for (; f->key != f->key_tail; ++f->key, ++f->arg, ++f->param) {

  //=//// CONTINUES (AT TOP SO GOTOS DO NOT CROSS INITIALIZATIONS /////////=//

        goto fulfill_loop_body;  // optimized out

      continue_fulfilling:

        if (GET_EVAL_FLAG(f, DOING_PICKUPS)) {
            if (DSP != f->dsp_orig)
                goto next_pickup;

            f->key = nullptr;  // don't need f->key
            f->key_tail = nullptr;
            goto fulfill_and_any_pickups_done;
        }
        continue;

      skip_fulfilling_arg_for_now:  // the GC marks args up through f->arg...

        continue;

  //=//// ACTUAL LOOP BODY ////////////////////////////////////////////////=//

      fulfill_loop_body:

  //=//// SPECIALIZED ARGUMENTS ////////////////////////////////////////////=//

        // Parameters that are specialized (which includes locals, that are
        // specialized to ~unset~ isotopes) are hidden from the public
        // interface.  So they will never come from argument fulfillment.
        // Their value comes from the exemplar frame in the slot where typesets
        // would be if it was unspecialized.
        //
        if (Is_Specialized(f->param)) {  // specialized includes local
            //
            // For specialized cases, we assume type checking was done
            // when the parameter is hidden.  It cannot be manipulated
            // from the outside (e.g. by REFRAMER) so there is no benefit
            // to deferring the check, only extra cost on each invocation.
            //
            Copy_Cell(f->arg, f->param);

            goto continue_fulfilling;
        }

        assert(IS_TYPESET(f->param));

  //=//// CHECK FOR ORDER OVERRIDE ////////////////////////////////////////=//

        // Parameters are fulfilled in either 1 or 2 passes, depending on
        // whether the path uses any "refinements".
        //
        // Refinements can be tricky because the "visitation order" of the
        // parameters while walking across the parameter array might not
        // match the "consumption order" of the expressions that need to
        // be fetched from the callsite.  For instance:
        //
        //     foo: func [a /b [integer!] /c [integer!]] [...]
        //
        //     foo/b/c 10 20 30
        //     foo/c/b 10 20 30
        //
        // The first PATH! pushes /B to the top of stack, with /C below.
        // The second PATH! pushes /C to the top of stack, with /B below
        //
        // While historically Rebol paths for invoking functions could only
        // use refinements for optional parameters, Ren-C leverages the same
        // two-pass mechanism to implement the reordering of non-optional
        // parameters at the callsite.

        if (DSP != f->dsp_orig) {  // reorderings or refinements pushed
            STKVAL(*) ordered = DS_TOP;
            STKVAL(*) lowest_ordered = DS_AT(f->dsp_orig);
            const REBSTR *param_symbol = KEY_SYMBOL(f->key);

            for (; ordered != lowest_ordered; --ordered) {
                if (VAL_WORD_SYMBOL(ordered) != param_symbol)
                    continue;

                REBLEN offset = f->arg - FRM_ARGS_HEAD(f);
                INIT_VAL_WORD_BINDING(ordered, f->varlist);
                INIT_VAL_WORD_INDEX(ordered, offset + 1);

                if (Is_Typeset_Empty(f->param)) {
                    //
                    // There's no argument, so we won't need to come back
                    // for this one.  But we did need to set its index
                    // so we knew it was valid (errors later if not set).
                    //
                    Init_Blackhole(f->arg);  // # means refinement used
                    goto continue_fulfilling;
                }

                goto skip_fulfilling_arg_for_now;
            }
        }

  //=//// A /REFINEMENT ARG ///////////////////////////////////////////////=//

        if (GET_PARAM_FLAG(f->param, REFINEMENT)) {
            assert(NOT_EVAL_FLAG(f, DOING_PICKUPS));  // jump lower
            Init_Nulled(f->arg);  // null means refinement not used
            goto continue_fulfilling;
        }

  //=//// ARGUMENT FULFILLMENT ////////////////////////////////////////////=//

      fulfill_arg: ;  // semicolon needed--next statement is declaration

        enum Reb_Param_Class pclass = VAL_PARAM_CLASS(f->param);

  //=//// SKIP OVER RETURN /////////////////////////////////////////////////=//

        // The return function is filled in by the dispatchers that provide it.

        if (pclass == PARAM_CLASS_RETURN) {
            assert(NOT_EVAL_FLAG(f, DOING_PICKUPS));
            Init_Nulled(f->arg);
            goto continue_fulfilling;
        }

  //=//// HANDLE IF NEXT ARG IS IN OUT SLOT (e.g. ENFIX, CHAIN) ///////////=//

        if (GET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT)) {
            CLEAR_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);

            if (GET_CELL_FLAG(f->out, OUT_NOTE_STALE)) {
                //
                // Something like `lib/help left-lit` is allowed to work,
                // but if it were just `obj/int-value left-lit` then the
                // path evaluation won...but LEFT-LIT still gets run.
                // It appears it has nothing to its left, but since we
                // remembered what happened we can give an informative
                // error message vs. a perplexing one.
                //
                if (GET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH))
                    fail (Error_Literal_Left_Path_Raw());

                // Seeing an END in the output slot could mean that there
                // was really "nothing" to the left, or it could be a
                // consequence of a frame being in an argument gathering
                // mode, e.g. the `+` here will perceive "nothing":
                //
                //     if + 2 [...]
                //
                // If an enfixed function finds it has a variadic in its
                // first slot, then nothing available on the left is o.k.
                // It means we have to put a VARARGS! in that argument
                // slot which will react with TRUE to TAIL?, so feed it
                // from the global empty array.
                //
                if (GET_PARAM_FLAG(f->param, VARIADIC)) {
                    Init_Varargs_Untyped_Enfix(f->arg, END_CELL);
                    goto continue_fulfilling;
                }

                // The OUT_NOTE_STALE flag is also used by BAR! to keep
                // a result in f->out, so that the barrier doesn't destroy
                // data in cases like `(1 + 2 | comment "hi")` => 3, but
                // left enfix should treat that just like an end.

                if (pclass == PARAM_CLASS_META)
                    Init_Void(f->arg);
                else
                    Init_Endish_Nulled(f->arg);
                goto continue_fulfilling;
            }

            if (GET_PARAM_FLAG(f->param, VARIADIC)) {
                //
                // Stow unevaluated cell into an array-form variadic, so
                // the user can do 0 or 1 TAKEs of it.
                //
                // !!! It be evaluated when they TAKE (it if it's an
                // evaluative arg), but not if they don't.  Should failing
                // to TAKE be seen as an error?  Failing to take first
                // gives out-of-order evaluation.
                //
                assert(NOT_END(f->out));
                Init_Varargs_Untyped_Enfix(f->arg, f->out);
            }
            else switch (pclass) {
              case PARAM_CLASS_NORMAL:
              case PARAM_CLASS_OUTPUT:
              case PARAM_CLASS_META:
                Copy_Cell(f->arg, f->out);
                if (GET_CELL_FLAG(f->out, UNEVALUATED))
                    SET_CELL_FLAG(f->arg, UNEVALUATED);

                if (pclass == PARAM_CLASS_META)
                    Meta_Quotify(f->arg);
                break;

              case PARAM_CLASS_HARD:
                if (NOT_CELL_FLAG(f->out, UNEVALUATED)) {
                    //
                    // This can happen e.g. with `x: 10 | x >- lit`.  We
                    // raise an error in this case, while still allowing
                    // `10 >- lit` to work, so people don't have to go
                    // out of their way rethinking operators if it could
                    // just work out for inert types.
                    //
                    fail (Error_Evaluative_Quote_Raw());
                }

                // PARAM_FLAG_SKIPPABLE accounted for in pre-lookback

                Copy_Cell(f->arg, f->out);
                SET_CELL_FLAG(f->arg, UNEVALUATED);

                if (IS_BAD_WORD(f->arg))  // source should only be isotope form
                    assert(NOT_CELL_FLAG(f->arg, ISOTOPE));
                break;

              case PARAM_CLASS_SOFT:
                //
                // SOFT permits f->out to not carry the UNEVALUATED
                // flag--enfixed operations which have evaluations on
                // their left are treated as if they were in a GROUP!.
                // This is important to `1 + 2 ->- lib/* 3` being 9, while
                // also allowing `1 + x: ->- lib/default [...]` to work.
                //
                goto escapable;

              case PARAM_CLASS_MEDIUM:
                //
                // MEDIUM escapability means that it only allows the escape
                // of one unit.  Thus when reaching this point, it must carry
                // the UENEVALUATED FLAG.
                //
                assert(GET_CELL_FLAG(f->out, UNEVALUATED));
                goto escapable;

              escapable:
                if (ANY_ESCAPABLE_GET(f->out)) {
                    if (Eval_Value_Throws(f->arg, f->out, SPECIFIED)) {
                        Copy_Cell(f->out, f->arg);
                        goto abort_action;
                    }
                }
                else {
                    Copy_Cell(f->arg, f->out);
                    SET_CELL_FLAG(f->arg, UNEVALUATED);

                    if (IS_BAD_WORD(f->arg))  // !!! source should only be isotope
                        assert(NOT_CELL_FLAG(f->arg, ISOTOPE));
                }
                break;

              default:
                assert(false);
            }

            // When we see `1 + 2 * 3`, when we're at the 2, we don't
            // want to let the * run yet.  So set a flag which says we
            // won't do lookahead that will be cleared when function
            // takes an argument *or* when a new expression starts.
            //
            // This effectively puts the enfix into a *single step defer*.
            //
            if (GET_EVAL_FLAG(f, RUNNING_ENFIX)) {
                assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD));
                if (
                    NOT_ACTION_FLAG(FRM_PHASE(f), POSTPONES_ENTIRELY)
                    and
                    NOT_ACTION_FLAG(FRM_PHASE(f), DEFERS_LOOKBACK)
                ){
                    SET_FEED_FLAG(f->feed, NO_LOOKAHEAD);
                }
            }

            // We are expiring the output cell here because we have "used up"
            // the output result.  We don't know at this moment if the
            // function going to behave invisibly.  If it does, then we have
            // to *un-expire* the enfix invisible flag (!)
            //
            Expire_Out_Cell_Unless_Invisible(f);

            goto continue_fulfilling;
        }

  //=//// NON-ENFIX VARIADIC ARG (doesn't consume anything *yet*) /////////=//

        // Evaluation argument "hook" parameters (marked in MAKE ACTION!
        // by a `[[]]` in the spec, and in FUNC by `<variadic>`).  They point
        // back to this call through a reified FRAME!, and are able to
        // consume additional arguments during the function run.
        //
        if (GET_PARAM_FLAG(f->param, VARIADIC)) {
            Init_Varargs_Untyped_Normal(f->arg, f);
            goto continue_fulfilling;
        }

  //=//// AFTER THIS, PARAMS CONSUME FROM CALLSITE IF NOT APPLY ///////////=//

        // If this is a non-enfix action, we're at least at *second* slot:
        //
        //     1 + non-enfix-action <we-are-here> * 3
        //
        // That's enough to indicate we're not going to read this as
        // `(1 + non-enfix-action <we-are-here>) * 3`.  Contrast with the
        // zero-arity case:
        //
        //     >> two: does [2]
        //     >> 1 + two * 3
        //     == 9
        //
        // We don't get here to clear the flag, so it's `(1 + two) * 3`
        //
        // But if it's enfix, arg gathering could still be like:
        //
        //      1 + <we-are-here> * 3
        //
        // So it has to wait until -after- the callsite gather happens to
        // be assured it can delete the flag, to ensure that:
        //
        //      >> 1 + 2 * 3
        //      == 9
        //
        if (NOT_EVAL_FLAG(f, RUNNING_ENFIX))
            CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

        // Once a deferred flag is set, it must be cleared during the
        // evaluation of the argument it was set for... OR the function
        // call has to end.  If we need to gather an argument when that
        // is happening, it means neither of those things are true, e.g.:
        //
        //     if 1 then [<bad>] [print "this is illegal"]
        //     if (1 then [<good>]) [print "but you can do this"]
        //
        // The situation also arises in multiple arity infix:
        //
        //     arity-3-op: func [a b c] [...]
        //
        //     1 arity-3-op 2 + 3 <ambiguous>
        //     1 arity-3-op (2 + 3) <unambiguous>
        //
        if (GET_FEED_FLAG(f->feed, DEFERRING_ENFIX))
            fail (Error_Ambiguous_Infix_Raw());

  //=//// ERROR ON END MARKER, BAR! IF APPLICABLE /////////////////////////=//

        if (IS_END(f_next)) {
            if (pclass == PARAM_CLASS_META) {
                Init_Void(f->arg);
                SET_CELL_FLAG(f->arg, UNEVALUATED);
            }
            else
                Init_Endish_Nulled(f->arg);
            goto continue_fulfilling;
        }

        switch (pclass) {

  //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes 1 EVALUATE's worth) /////=//

          case PARAM_CLASS_NORMAL:
          case PARAM_CLASS_OUTPUT:
          case PARAM_CLASS_META: {
            if (GET_FEED_FLAG(f->feed, BARRIER_HIT)) {
                if (pclass == PARAM_CLASS_META)
                    Init_Void(f->arg);
                else
                    Init_Endish_Nulled(f->arg);
                goto continue_fulfilling;
            }

            REBFLGS flags = EVAL_MASK_DEFAULT
                | EVAL_FLAG_FULFILLING_ARG;

            if (Eval_Step_In_Subframe_Throws(f->arg, f, flags)) {
                Move_Cell(f->out, f->arg);
                goto abort_action;
            }

            if (IS_END(f->arg)) {
                if (pclass == PARAM_CLASS_META)
                    Init_Void(f->arg);
                else
                    Init_Endish_Nulled(f->arg);
            }
            else if (pclass == PARAM_CLASS_META)
                Meta_Quotify(f->arg);
            break; }

  //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG ///////////////////////////////=//

          case PARAM_CLASS_HARD:
            if (NOT_PARAM_FLAG(f->param, SKIPPABLE))
                Literal_Next_In_Frame(f->arg, f);  // CELL_FLAG_UNEVALUATED
            else {
                if (not Typecheck_Including_Constraints(f->param, f_next)) {
                    assert(GET_PARAM_FLAG(f->param, ENDABLE));
                    Init_Endish_Nulled(f->arg);  // not EVAL_FLAG_BARRIER_HIT
                    goto continue_fulfilling;
                }
                Literal_Next_In_Frame(f->arg, f);
                SET_CELL_FLAG(f->arg, UNEVALUATED);
            }

            if (IS_BAD_WORD(f->arg))  // !!! Source should only be isotope form
                assert(NOT_CELL_FLAG(f->arg, ISOTOPE));

            // Have to account for enfix deferrals in cases like:
            //
            //     return the 1 then (x => [x + 1])
            //
            Lookahead_To_Sync_Enfix_Defer_Flag(f->feed);

            goto continue_fulfilling;

  //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  //////////////////////////////=//

    // Quotes from the right already "win" over quotes from the left, in
    // a case like `help left-quoter` where they point at teach other.
    // But there's also an issue where something sits between quoting
    // constructs like the `x` in between the `else` and `->`:
    //
    //     if condition [...] else x -> [...]
    //
    // Here the neutral `x` is meant to be a left argument to the lambda,
    // producing the effect of:
    //
    //     if condition [...] else (`x` -> [...])
    //
    // To get this effect, we need a different kind of deferment that
    // hops over a unit of material.  Soft quoting is unique in that it
    // means we can do that hop over exactly one unit without breaking
    // the evaluator mechanics of feeding one element at a time with
    // "no takebacks".
    //
    // First, we cache the quoted argument into the frame slot.  This is
    // the common case of what is desired.  But if we advance the feed and
    // notice a quoting enfix construct afterward looking left, we call
    // into a nested evaluator before finishing the operation.

          case PARAM_CLASS_SOFT:
          case PARAM_CLASS_MEDIUM:
            Literal_Next_In_Frame(f->arg, f);  // CELL_FLAG_UNEVALUATED

            // See remarks on Lookahead_To_Sync_Enfix_Defer_Flag().  We
            // have to account for enfix deferrals in cases like:
            //
            //     return if false '[foo] else '[bar]
            //
            // Note that this quoting lookahead ("lookback?") is exempt
            // from the usual "no lookahead" rule while gathering enfix
            // arguments.  This supports `null then x -> [1] else [2]`,
            // being 2.  See details at:
            //
            // https://forum.rebol.info/t/1361
            //
            if (
                Lookahead_To_Sync_Enfix_Defer_Flag(f->feed) and  // ensure got
                (pclass == PARAM_CLASS_SOFT and GET_ACTION_FLAG(
                    VAL_ACTION(unwrap(f->feed->gotten)),  // ensured
                    QUOTES_FIRST
                ))
            ){
                // We need to defer and let the right hand quote that is
                // quoting leftward win.  We use ST_EVALUATOR_LOOKING_AHEAD
                // to jump into a subframe where subframe->out is the f->arg,
                // and it knows to get the arg from there.

                REBFLGS flags = EVAL_MASK_DEFAULT
                    | EVAL_FLAG_FULFILLING_ARG
                    | FLAG_STATE_BYTE(ST_EVALUATOR_LOOKING_AHEAD)
                    | EVAL_FLAG_INERT_OPTIMIZATION;

                DECLARE_FRAME (subframe, f->feed, flags);

                Push_Frame(f->arg, subframe);
                bool threw = Eval_Throws(subframe);
                Drop_Frame(subframe);

                if (threw) {
                    Copy_Cell(f->out, f->arg);
                    goto abort_action;
                }
            }
            else if (ANY_ESCAPABLE_GET(f->arg)) {
                //
                // We did not defer the quoted argument.  If the argument
                // is something like a GROUP!, GET-WORD!, or GET-PATH!...
                // it has to be evaluated.
                //
                Move_Cell(f_spare, f->arg);
                if (Eval_Value_Throws(f->arg, f_spare, f_specifier)) {
                    Move_Cell(f->out, f->arg);
                    goto abort_action;
                }
            }
            else {
                if (IS_BAD_WORD(f->arg))  // !!! Source should only be isotope form
                    assert(NOT_CELL_FLAG(f->arg, ISOTOPE));
            }
            break;

          case PARAM_CLASS_RETURN:  // should not happen!
            assert(false);
            break;

          default:
            assert(false);
        }

        // If FEED_FLAG_NO_LOOKAHEAD was set going into the argument
        // gathering above, it should have been cleared or converted into
        // FEED_FLAG_DEFER_ENFIX.
        //
        //     1 + 2 * 3
        //           ^-- this deferred its chance, so 1 + 2 will complete
        //
        // !!! The case of:
        //
        //     30 = (10 + 20 devoid do [])
        //
        // Is breaking this.  Review when there is time, and put the assert
        // back if it makes sense.
        //
        /* assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD)); */
        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

        assert(NOT_EVAL_FLAG(f, FULLY_SPECIALIZED));

        goto continue_fulfilling;
    }

  #if DEBUG_TERM_ARRAYS
    assert(IS_CELL_FREE(f->arg));  // arg can otherwise point to any arg cell
  #endif

    // There may have been refinements that were skipped because the
    // order of definition did not match the order of usage.  They were
    // left on the stack with a pointer to the `param` and `arg` after
    // them for later fulfillment.
    //
    // Note that there may be functions on the stack if this is the
    // second time through, and we were just jumping up to check the
    // parameters in response to a R_REDO_CHECKED; if so, skip this.
    //
    if (DSP != f->dsp_orig and IS_WORD(DS_TOP)) {

      next_pickup:

        assert(IS_WORD(DS_TOP));

        if (not IS_WORD_BOUND(DS_TOP)) {  // the loop didn't index it
            Refinify(DS_TOP);  // used as refinement, so report that way
            fail (Error_Bad_Parameter_Raw(DS_TOP));  // so duplicate or junk
        }

        // FRM_ARGS_HEAD offsets are 0-based, while index is 1-based.
        // But +1 is okay, because we want the slots after the refinement.
        //
        REBINT offset =
            VAL_WORD_INDEX(DS_TOP) - (f->arg - FRM_ARGS_HEAD(f)) - 1;
        f->key += offset;
        f->arg += offset;
        f->param += offset;

        assert(VAL_WORD_SYMBOL(DS_TOP) == KEY_SYMBOL(f->key));
        DS_DROP();

        if (Is_Typeset_Empty(f->param)) {  // no callsite arg, just drop
            if (DSP != f->dsp_orig)
                goto next_pickup;

            f->key = nullptr;  // don't need f->key
            f->key_tail = nullptr;
            goto fulfill_and_any_pickups_done;
        }

        if (not Is_Fresh(f->arg)) {
            assert(IS_NULLED(f->arg));
            RESET(f->arg);
        }

        SET_EVAL_FLAG(f, DOING_PICKUPS);
        goto fulfill_arg;
    }

  fulfill_and_any_pickups_done:

    CLEAR_EVAL_FLAG(f, DOING_PICKUPS);  // reevaluate may set flag again
    f->key = nullptr;  // signals !Is_Action_Frame_Fulfilling()
    f->key_tail = nullptr;

    if (GET_EVAL_FLAG(f, FULFILL_ONLY)) {  // only fulfillment, no typecheck
        assert(Is_Fresh(f->out));  // didn't touch out
        goto skip_output_check;
    }

  //=//// ACTION! ARGUMENTS NOW GATHERED, DO TYPECHECK PASS ///////////////=//

    // It might seem convenient to type check arguments while they are being
    // fulfilled vs. performing another loop.  But the semantics of the system
    // allows manipulation of arguments between fulfillment and execution, and
    // that could turn invalid arguments good or valid arguments bad.  Plus
    // if all the arguments are evaluated before any type checking, that puts
    // custom type checks inside the body of a function on equal footing with
    // any system-optimized type checking.
    //
    // So a second loop is required by the system's semantics.

  typecheck_then_dispatch:

    f->key = ACT_KEYS(&f->key_tail, FRM_PHASE(f));
    f->arg = FRM_ARGS_HEAD(f);
    f->param = ACT_PARAMS_HEAD(FRM_PHASE(f));

    for (; f->key != f->key_tail; ++f->key, ++f->arg, ++f->param) {
        assert(NOT_END(f->arg));  // all END fulfilled as Init_Endish_Nulled()

        // Note that if you have a redo situation as with an ENCLOSE, a
        // specialized out parameter becomes visible in the frame and can be
        // modified.  Even though it's hidden, it may need to be typechecked
        // again, unless it was fully hidden.
        //
        if (Is_Specialized(f->param))
            continue;

        // We can't a-priori typecheck the variadic argument, since the values
        // aren't calculated until the function starts running.  Instead we
        // stamp this instance of the varargs with a way to reach back and
        // see the parameter type signature.
        //
        // The data feed is unchanged (can come from this frame, or another,
        // or just an array from MAKE VARARGS! of a BLOCK!)
        //
        if (GET_PARAM_FLAG(f->param, VARIADIC)) {
            //
            // The types on the parameter are for the values fetched later.
            // Actual argument must be a VARARGS!
            //
            if (not IS_VARARGS(f->arg))
                fail (Error_Not_Varargs(f, f->key, f->param, VAL_TYPE(f->arg)));

            INIT_VAL_VARARGS_PHASE(f->arg, FRM_PHASE(f));

            // Store the offset so that both the arg and param locations can
            // quickly be recovered, while using only a single slot in the
            // REBVAL.  Sign denotes whether the parameter was enfixed or not.
            //
            bool enfix = false;  // !!! how does enfix matter?
            VAL_VARARGS_SIGNED_PARAM_INDEX(f->arg) =
                enfix
                    ? -(f->arg - FRM_ARGS_HEAD(f) + 1)
                    : f->arg - FRM_ARGS_HEAD(f) + 1;

            assert(VAL_VARARGS_SIGNED_PARAM_INDEX(f->arg) != 0);
            continue;
        }

        if (VAL_PARAM_CLASS(f->param) == PARAM_CLASS_RETURN)
            continue;  // !!! hack

        // Refinements have a special rule beyond plain type checking, in that
        // they don't just want an ISSUE! or NULL, they want # or NULL.
        //
        if (GET_PARAM_FLAG(f->param, REFINEMENT)) {
            if (
                GET_EVAL_FLAG(f, FULLY_SPECIALIZED)
                and Is_Unset(f->arg)
            ){
                Init_Nulled(f->arg);
            }
            else
                Typecheck_Refinement(f->key, f->param, f->arg);
            continue;
        }

        // !!! In GCC 9.3.0-10 at -O2 optimization level in the C++ build
        // this seemed to trigger:
        //
        //   error: array subscript 2 is outside array bounds
        //      of 'const char [9]'
        //
        // It points to the problem being at VAL_STRING_AT()'s line:
        //
        //     const REBSTR *s = VAL_STRING(v);
        //
        // Attempts to further isolate it down were made by deleting and
        // inlining bits of code until one low-level line would trigger it.
        // This led to seemingly unrelated declaration of an unused byte
        // variable being able to cause it or not.  It may be a compiler
        // optimization bug...in any cae, that warning is disabled for
        // now on this file.  Review.

        if (IS_ENDISH_NULLED(f->arg)) {
            //
            // Note: `1 + comment "foo"` => `1 +`, arg is END
            //
            if (NOT_PARAM_FLAG(f->param, ENDABLE))
                fail (Error_No_Arg(f->label, KEY_SYMBOL(f->key)));

            continue;
        }

        REBYTE kind_byte = KIND3Q_BYTE(f->arg);

        if (
            kind_byte == REB_BLANK  // v-- e.g. <blank> param
            and GET_PARAM_FLAG(f->param, NOOP_IF_BLANK)
        ){
            SET_EVAL_FLAG(f, TYPECHECK_ONLY);
            continue;
        }

        if (PARAM_CLASS_META == VAL_PARAM_CLASS(f->param)) {
            if (
                kind_byte != REB_BAD_WORD
                and kind_byte != REB_NULL
                and not IS_QUOTED_KIND(kind_byte)
            ){
                fail ("^META arguments must be quoted!, bad-word!, or null");
            }
        }
        else if (kind_byte == REB_BAD_WORD and GET_CELL_FLAG(f->arg, ISOTOPE))
            fail (Error_Isotope_Arg(f, f->param));

        // Apply constness if requested.
        //
        // !!! Should explicit mutability override, so people can say things
        // like `foo: func [...] mutable [...]` ?  This seems bad, because the
        // contract of the function hasn't been "tweaked" with reskinning.
        //
        if (GET_PARAM_FLAG(f->param, CONST))
            SET_CELL_FLAG(f->arg, CONST);

        // !!! Review when # is used here
        if (GET_PARAM_FLAG(f->param, REFINEMENT)) {
            Typecheck_Refinement(f->key, f->param, f->arg);
            continue;
        }

        if (KEY_SYM(f->key) == SYM_RETURN)
            continue;  // !!! let whatever go for now

        if (not Typecheck_Including_Constraints(f->param, f->arg)) {
            if (
                IS_NULLED(f->arg)
                and GET_PARAM_FLAG(f->param, ENDABLE)
                and GET_EVAL_FLAG(f, FULLY_SPECIALIZED)
            ){
                // !!! We don't really want people to be calling endable
                // non-<opt> functions with NULL arguments.  But if someone
                // uses a DO on a FRAME! we can't tell the difference...
                // the "endish nulled" mechanic is not available.  We could
                // try making an exception for ~void~ isotopes being allowed
                // in frames (the exception exists at the moment for meta
                // parameters) but this would ruin some of the convenience
                // of END.  Perhaps there should be an ~end~ isotope that will
                // decay to NULL like ~null~ isotopes do?  This could give
                // the best of both worlds.  For now, just suppress erroring
                // if an <end> parameter gets NULL from a frame.
            }
            else
                fail (Error_Arg_Type(f, f->key, VAL_TYPE(f->arg)));
        }
    }


  //=//// ACTION! ARGUMENTS NOW GATHERED, DISPATCH PHASE //////////////////=//

  dispatch:

    if (GET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT)) {
        if (GET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH))  // see notes on flag
            fail (Error_Literal_Left_Path_Raw());
    }

    // This happens if you have something intending to act as enfix but
    // that does not consume arguments, e.g. `x: enfixed func [] []`.
    // An enfixed function with no arguments might sound dumb, but it allows
    // a 0-arity function to run in the same evaluation step as the left
    // hand side.  This is how expression work (see `|:`)
    //
    assert(NOT_EVAL_FLAG(f, UNDO_NOTE_STALE));
    if (GET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT)) {
        assert(GET_EVAL_FLAG(f, RUNNING_ENFIX));
        CLEAR_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);
        f->out->header.bits |= CELL_FLAG_OUT_NOTE_STALE;  // won't undo this
    }
    else if (GET_EVAL_FLAG(f, RUNNING_ENFIX) and NOT_END(f->out))
        SET_EVAL_FLAG(f, UNDO_NOTE_STALE);

    assert(not Is_Action_Frame_Fulfilling(f));
    assert(
        IS_END(f_next)
        or FRM_IS_VARIADIC(f)
        or IS_VALUE_IN_ARRAY_DEBUG(FEED_ARRAY(f->feed), f_next)
    );

    if (GET_EVAL_FLAG(f, TYPECHECK_ONLY)) {  // <blank> uses this
        Init_Nulled(f->out);  // by convention: BLANK! in, NULL out
        goto skip_output_check;
    }

    f_next_gotten = nullptr;  // arbitrary code changes fetched variables

    // Note that the dispatcher may push ACTION! values to the data stack
    // which are used to process the return result after the switch.
    //
  blockscope {
    REBACT *phase = FRM_PHASE(f);

    // Native code trusts that type checking has ensured it won't get bits
    // in its argument slots that the C won't recognize.  Usermode code that
    // gets its hands on a native's FRAME! (e.g. for debug viewing) can't be
    // allowed to change the frame values to other bit patterns out from
    // under the C or it could result in a crash.
    //
    // !!! Once the IS_NATIVE flag was the same as the HOLD info bit, but that
    // trick got shaken up with flag reordering.  Review.
    //
    /*STATIC_ASSERT(DETAILS_FLAG_IS_NATIVE == SERIES_INFO_HOLD);*/
    if (GET_ACTION_FLAG(phase, IS_NATIVE))
        SER_INFO(f->varlist) |= SERIES_INFO_HOLD;

    REBNAT dispatcher = ACT_DISPATCHER(phase);

    assert(not Is_Evaluator_Throwing_Debug());

    Expire_Out_Cell_Unless_Invisible(f);

    // Resetting the spare cell here has a slight cost, but keeps from leaking
    // internal processing to actions.
    //
    RESET(f_spare);

    const REBVAL *r = (*dispatcher)(f);

    if (r == f->out) {
        //
        // common case; we'll want to clear the UNEVALUATED flag if it's
        // not an invisible return result (other cases Copy_Cell())
        //
    }
    else if (not r) {  // API and internal code can both return `nullptr`
        Init_Nulled(f->out);
        goto dispatch_completed;  // skips invisible check
    }
    else if (not IS_RETURN_SIGNAL(r)) {
        assert(GET_CELL_FLAG(r, ROOT));  // API, from Alloc_Value()
        Handle_Api_Dispatcher_Result(f, r);
        goto dispatch_completed;  // skips invisible check
    }
    else switch (VAL_RETURN_SIGNAL(r)) {  // it's a "pseudotype" instruction
        //
        // !!! Thrown values used to be indicated with a bit on the value
        // itself, but now it's conveyed through a return value.  This
        // means typical return values don't have to run through a test
        // for if they're thrown or not, but it means Eval_Core has to
        // return a boolean to pass up the state.  It may not be much of
        // a performance win either way, but recovering the bit in the
        // values is a definite advantage--as header bits are scarce!
        //
      case C_THROWN: {
        const REBVAL *label = VAL_THROWN_LABEL(f->out);
        if (IS_ACTION(label)) {
            if (
                VAL_ACTION(label) == VAL_ACTION(Lib(UNWIND))
                and VAL_ACTION_BINDING(label) == CTX(f->varlist)
                    // !!! Note f->varlist may be INACCESSIBLE here
                    // e.g. this happens with RETURN during ENCLOSE
            ){
                // Eval_Core catches unwinds to the current frame, so throws
                // where the "/name" is the JUMP native with a binding to
                // this frame, and the thrown value is the return code.
                //
                // !!! This might be a little more natural if the name of
                // the throw was a FRAME! value.  But that also would mean
                // throws named by frames couldn't be taken advantage by
                // the user for other features, while this only takes one
                // function away.
                //
                CATCH_THROWN(f->out, f->out);
                goto dispatch_completed;
            }
            else if (
                VAL_ACTION(label) == VAL_ACTION(Lib(REDO))
                and VAL_ACTION_BINDING(label) == CTX(f->varlist)
            ){
                // This was issued by REDO, and should be a FRAME! with
                // the phase and binding we are to resume with.
                //
                CATCH_THROWN(f->out, f->out);
                assert(IS_FRAME(f->out));

                // We are reusing the frame and may be jumping to an
                // "earlier phase" of a composite function, or even to
                // a "not-even-earlier-just-compatible" phase of another
                // function.  Type checking is necessary, as is zeroing
                // out any locals...but if we're jumping to any higher
                // or different phase we need to reset the specialization
                // values as well.
                //
                // Since dispatchers run arbitrary code to pick how (and
                // if) they want to change the phase on each redo, we
                // have no easy way to tell if a phase is "earlier" or
                // "later".
                //
                // !!! Consider folding this pass into an option for the
                // typechecking loop itself.
                //
                REBACT *redo_phase = VAL_FRAME_PHASE(f->out);
                f->key = ACT_KEYS(&f->key_tail, redo_phase);
                f->param = ACT_PARAMS_HEAD(redo_phase);
                f->arg = FRM_ARGS_HEAD(f);
                for (; f->key != f->key_tail; ++f->key, ++f->arg, ++f->param) {
                    if (Is_Specialized(f->param)) {
                        Copy_Cell(f->arg, f->param);
                    }
                }

                INIT_FRM_PHASE(f, redo_phase);
                INIT_FRM_BINDING(f, VAL_FRAME_BINDING(f->out));
                CLEAR_EVAL_FLAG(f, UNDO_NOTE_STALE);
                goto typecheck_then_dispatch;
            }
        }

        // Stay THROWN and let stack levels above try and catch
        //
        goto abort_action; }

    //
    // REDO instructions represent the idea that it is desired to run the
    // f->phase again.  The dispatcher may have changed the value of what
    // f->phase is, for instance.
    //

      case C_REDO_UNCHECKED:
        CLEAR_EVAL_FLAG(f, UNDO_NOTE_STALE);
        goto dispatch;

      case C_REDO_CHECKED:
        CLEAR_EVAL_FLAG(f, UNDO_NOTE_STALE);
        goto typecheck_then_dispatch;

        // !!! There were generic dispatchers that were returning this, and
        // it wasn't noticed when unhandled was the same as END.  For instance
        // this was arising in the ISSUE! dispatcher when you said `#a + 1`.
        // Path dispatchers make use of this, but should regular actions?  Is
        // there a meaningful enough error to fabricate?
        //
      case C_UNHANDLED:
        fail ("Not handled (review instances of this error!)");

      default:
        assert(!"Invalid pseudotype returned from action dispatcher");
    }
  }

  //=//// CHECK FOR INVISIBILITY (STALE OUTPUT) ///////////////////////////=//

    if (not (f->out->header.bits & CELL_FLAG_OUT_NOTE_STALE))
        CLEAR_CELL_FLAG(f->out, UNEVALUATED);
    else {
        // We didn't know before we ran the enfix function if it was going
        // to be invisible, so the output was expired.  Un-expire it if we
        // are supposed to do so.
        //
        STATIC_ASSERT(
            EVAL_FLAG_UNDO_NOTE_STALE == CELL_FLAG_OUT_NOTE_STALE
        );
        f->out->header.bits ^= (f->flags.bits & EVAL_FLAG_UNDO_NOTE_STALE);

        // If a "good" output is in `f->out`, the invisible should have
        // had no effect on it.  So jump to the position after output
        // would be checked by a normal function.
        //
        if (NOT_CELL_FLAG(f->out, OUT_NOTE_STALE) or IS_END(f_next)) {
            //
            // Note: could be an END that is not "stale", example:
            //
            //     is-barrier?: func [x [<end> integer!]] [null? x]
            //     is-barrier? (<| 10)
            //
            goto dispatch_completed;
        }

        // If the evaluation is being called by something like EVALUATE,
        // they may want to see the next value literally.  Refer to this
        // explanation:
        //
        // https://forum.rebol.info/t/1173/4
        //
        // But argument evaluation isn't customizable at that level, and
        // wants all the invisibles processed.  So only do one-at-a-time
        // invisibles if we're not fulfilling arguments.
        //
        if (GET_EVAL_FLAG(f, FULFILLING_ARG))
            goto dispatch_completed;

        // Note that we do not do START_NEW_EXPRESSION() here when an
        // invisible is being processed as part of an argument.  They
        // all get lumped into one step.
        //
        // !!! How does this interact with the idea of a debugger that could
        // single step across invisibles (?)  Is that only a "step in", as
        // one would have to do when dealing with a function argument?
        //
        assert(NOT_EVAL_FLAG(f, FULFILL_ONLY));
        Drop_Action(f);
        return false;
    }

  dispatch_completed:

  //=//// ACTION! CALL COMPLETION /////////////////////////////////////////=//

    // Here we know the function finished and nothing threw past it or
    // FAIL / fail()'d.  It should still be in REB_ACTION evaluation
    // type, and overwritten the f->out with a non-thrown value.  If the
    // function composition is a CHAIN, the chained functions are still
    // pending on the stack to be run.

  #if !defined(NDEBUG)
    Do_After_Action_Checks_Debug(f);
  #endif

  skip_output_check:

    CLEAR_EVAL_FLAG(f, UNDO_NOTE_STALE);

    Drop_Action(f);

    // Want to keep this flag between an operation and an ensuing enfix in
    // the same frame, so can't clear in Drop_Action(), e.g. due to:
    //
    //     left-the: enfix :the
    //     o: make object! [f: does [1]]
    //     o/f left-the  ; want error suggesting -> here, need flag for that
    //
    CLEAR_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH);
    assert(NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT));  // must be consumed

    return false;  // false => not thrown

  abort_action:

    Drop_Action(f);
    DS_DROP_TO(f->dsp_orig);  // drop unprocessed refinements/chains on stack

    return true;  // true => thrown
}


//
//  Push_Action: C
//
// Allocate the series of REBVALs inspected by a function when executed (the
// values behind ARG(name), REF(name), D_ARG(3),  etc.)
//
// This only allocates space for the arguments, it does not initialize.
// Eval_Core initializes as it goes, and updates f->key so the GC knows how
// far it has gotten so as not to see garbage.  APPLY has different handling
// when it has to build the frame for the user to write to before running;
// so Eval_Core only checks the arguments, and does not fulfill them.
//
// If the function is a specialization, then the parameter list of that
// specialization will have *fewer* parameters than the full function would.
// For this reason we push the arguments for the "underlying" function.
// Yet if there are specialized values, they must be filled in from the
// exemplar frame.
//
// Rather than "dig" through layers of functions to find the underlying
// function or the specialization's exemplar frame, those properties are
// cached during the creation process.
//
void Push_Action(
    REBFRM *f,
    REBACT *act,
    REBCTX *binding  // actions may only be bound to contexts ATM
){
    assert(NOT_EVAL_FLAG(f, FULFILL_ONLY));
    assert(NOT_EVAL_FLAG(f, RUNNING_ENFIX));

    STATIC_ASSERT(EVAL_FLAG_FULFILLING_ARG == DETAILS_FLAG_IS_BARRIER);
    REBARR *identity = ACT_IDENTITY(act);
    if (f->flags.bits & identity->leader.bits & DETAILS_FLAG_IS_BARRIER)
        fail (Error_Expression_Barrier_Raw());

    REBLEN num_args = ACT_NUM_PARAMS(act);  // includes specialized + locals

    assert(f->varlist == nullptr);
    REBSER *s = Alloc_Series_Node(
        nullptr,  // not preallocated
        SERIES_MASK_VARLIST
            | SERIES_FLAG_FIXED_SIZE // FRAME!s don't expand ATM
    );
    SER_INFO(s) = SERIES_INFO_MASK_NONE;
    INIT_LINK_KEYSOURCE(ARR(s), f);  // maps varlist back to f
    mutable_MISC(VarlistMeta, s) = nullptr;
    mutable_BONUS(Patches, s) = nullptr;
    f->varlist = ARR(s);

    if (not Did_Series_Data_Alloc(s, num_args + 1 + 1)) {  // +rootvar, +end
        SET_SERIES_FLAG(s, INACCESSIBLE);
        GC_Kill_Series(s);  // ^-- needs non-null data unless INACCESSIBLE
        f->varlist = nullptr;
        fail (Error_No_Memory(sizeof(REBVAL) * (num_args + 1 + 1)));
    }

    f->rootvar = cast(REBVAL*, s->content.dynamic.data);
    USED(Prep_Cell(f->rootvar));  // want the tracking info, overwriting header
    f->rootvar->header.bits =
        NODE_FLAG_NODE
            | NODE_FLAG_CELL
            | CELL_FLAG_PROTECTED  // payload/binding tweaked, but not by user
            | CELL_MASK_CONTEXT
            | FLAG_KIND3Q_BYTE(REB_FRAME)
            | FLAG_HEART_BYTE(REB_FRAME);
    INIT_VAL_CONTEXT_VARLIST(f->rootvar, f->varlist);

    INIT_VAL_FRAME_PHASE(f->rootvar, act);  // FRM_PHASE()
    INIT_VAL_FRAME_BINDING(f->rootvar, binding);  // FRM_BINDING()

    s->content.dynamic.used = num_args + 1;

    // !!! Historically the idea was to prep during the walk of the frame,
    // to avoid doing two walks.  The current thinking is to move toward a
    // notion of being able to just memset() to 0 or calloc().  The debug
    // build still wants to initialize the cells with file/line info though.
    //
    RELVAL *tail = ARR_TAIL(f->varlist);
    RELVAL *prep = f->rootvar + 1;
    for (; prep < tail; ++prep)
        USED(Prep_Cell(prep));

  #if DEBUG_POISON_CELLS  // poison cells past usable range
  blockscope {
    prep = ARR_AT(f->varlist, s->content.dynamic.rest - 1);
    for (; prep >= tail; --prep) {
        USED(Prep_Cell(prep));  // gets tracking info
        prep->header.bits = CELL_MASK_POISON;
    }
  }
  #endif

  #if DEBUG_TERM_ARRAYS  // expects cell is trash (e.g. a cell) not poison
    SET_CELL_FREE(Prep_Cell(ARR_TAIL(f->varlist)));
  #endif

    // Each layer of specialization of a function can only add specializations
    // of arguments which have not been specialized already.  For efficiency,
    // the act of specialization merges all the underlying layers of
    // specialization together.  This means only the outermost specialization
    // is needed to fill the specialized slots contributed by later phases.
    //
    // f->param here will either equal f->key (to indicate normal argument
    // fulfillment) or the head of the "exemplar".
    //
    // !!! It is planned that exemplars will be unified with paramlist, making
    // the context keys something different entirely.
    //
    REBARR *partials = try_unwrap(ACT_PARTIALS(act));
    if (partials) {
        const RELVAL *word_tail = ARR_TAIL(partials);
        const REBVAL *word = SPECIFIC(ARR_HEAD(partials));
        for (; word != word_tail; ++word)
            Copy_Cell(DS_PUSH(), word);
    }

    assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
    assert(NOT_SERIES_FLAG(f->varlist, INACCESSIBLE));
}


//
//  Begin_Action_Core: C
//
void Begin_Action_Core(
    REBFRM *f,
    option(const REBSYM*) label,
    bool enfix
){
    assert(NOT_EVAL_FLAG(f, RUNNING_ENFIX));
    assert(NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX));

    assert(NOT_SUBCLASS_FLAG(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED));
    SET_SUBCLASS_FLAG(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED);

    assert(not f->original);
    f->original = FRM_PHASE(f);

    // f->key_tail = v-- set here
    f->key = ACT_KEYS(&f->key_tail, f->original);
    f->param = ACT_PARAMS_HEAD(f->original);
    f->arg = f->rootvar + 1;

    assert(IS_OPTION_TRASH_DEBUG(f->label));  // ACTION! makes valid
    assert(not label or IS_INTERN(unwrap(label)));
    f->label = label;
  #if DEBUG_FRAME_LABELS  // helpful for looking in the debugger
    f->label_utf8 = cast(const char*, Frame_Label_Or_Anonymous_UTF8(f));
  #endif

    // Cache the feed lookahead state so it can be restored in the event that
    // the evaluation turns out to be invisible.
    //
    STATIC_ASSERT(FEED_FLAG_NO_LOOKAHEAD == EVAL_FLAG_CACHE_NO_LOOKAHEAD);
    assert(NOT_EVAL_FLAG(f, CACHE_NO_LOOKAHEAD));
    f->flags.bits |= f->feed->flags.bits & FEED_FLAG_NO_LOOKAHEAD;

    if (enfix) {
        //
        // While FEED_FLAG_NEXT_ARG_FROM_OUT is set only during the first
        // argument of an enfix function call, EVAL_FLAG_RUNNING_ENFIX is
        // set for the whole duration.
        //
        // Note: We do not set NEXT_ARG_FROM_OUT here, because that flag is
        // checked to be clear by Fetch_Next_In_Feed(), which changes the
        // value in the feed *and* checks to make sure NEXT_ARG_FROM_OUT is
        // not set.  This winds up being a problem if the caller is using the
        // current value in feed for something like the label passed in here,
        // and intends to call Fetch_Next_In_Feed() as the next step.  So
        // the caller must set it.
        //
        SET_EVAL_FLAG(f, RUNNING_ENFIX);

        // All the enfix call sites cleared this flag on the feed, so it was
        // moved into the Begin_Enfix_Action() case.  Note this has to be done
        // *after* the existing flag state has been captured for invisibles.
        //
        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
    }
}


//
//  Drop_Action: C
//
void Drop_Action(REBFRM *f) {
    assert(not f->label or IS_INTERN(unwrap(f->label)));

    if (NOT_EVAL_FLAG(f, FULFILLING_ARG))
        CLEAR_FEED_FLAG(f->feed, BARRIER_HIT);

    if (f->out->header.bits & CELL_FLAG_OUT_NOTE_STALE) {
        //
        // If the whole evaluation of the action turned out to be invisible,
        // then refresh the feed's NO_LOOKAHEAD state to whatever it was
        // before that invisible evaluation ran.
        //
        STATIC_ASSERT(FEED_FLAG_NO_LOOKAHEAD == EVAL_FLAG_CACHE_NO_LOOKAHEAD);
        f->feed->flags.bits &= ~FEED_FLAG_NO_LOOKAHEAD;
        f->feed->flags.bits |= f->flags.bits & EVAL_FLAG_CACHE_NO_LOOKAHEAD;
    }
    CLEAR_EVAL_FLAG(f, CACHE_NO_LOOKAHEAD);

    CLEAR_EVAL_FLAG(f, RUNNING_ENFIX);
    CLEAR_EVAL_FLAG(f, FULFILL_ONLY);

    assert(
        GET_SERIES_FLAG(f->varlist, INACCESSIBLE)
        or LINK(KeySource, f->varlist) == f
    );

    if (GET_SERIES_FLAG(f->varlist, INACCESSIBLE)) {
        //
        // If something like Encloser_Dispatcher() runs, it might steal the
        // variables from a context to give them to the user, leaving behind
        // a non-dynamic node.  Pretty much all the bits in the node are
        // therefore useless.  It served a purpose by being non-null during
        // the call, however, up to this moment.
        //
        if (GET_SERIES_FLAG(f->varlist, MANAGED))
            f->varlist = nullptr; // references exist, let a new one alloc
        else {
            // This node could be reused vs. calling Alloc_Node() on the next
            // action invocation...but easier for the moment to let it go.
            //
            Free_Node(SER_POOL, f->varlist);
            f->varlist = nullptr;
        }
    }
    else if (GET_SERIES_FLAG(f->varlist, MANAGED)) {
        //
        // Varlist wound up getting referenced in a cell that will outlive
        // this Drop_Action().
        //
        // !!! The new concept is to let frames survive indefinitely in this
        // case.  This is in order to not let JavaScript have the upper hand
        // in "closure"-like scenarios.  See:
        //
        // "What Happens To Function Args/Locals When The Call Ends"
        // https://forum.rebol.info/t/234
        //
        // Previously this said:
        //
        // "The pointer needed to stay working up until now, but the args
        // memory won't be available.  But since we know there are outstanding
        // references to the varlist, we need to convert it into a "stub"
        // that's enough to avoid crashes.
        //
        // ...but we don't free the memory for the args, we just hide it from
        // the stub and get it ready for potential reuse by the next action
        // call.  That's done by making an adjusted copy of the stub, which
        // steals its dynamic memory (by setting the stub not HAS_DYNAMIC)."
        //
      #if 0
        f->varlist = CTX_VARLIST(
            Steal_Context_Vars(
                CTX(f->varlist),
                f->original  // degrade keysource from f
            )
        );
        assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
        INIT_LINK_KEYSOURCE(f->varlist, f);
      #endif

        INIT_LINK_KEYSOURCE(f->varlist, ACT_KEYLIST(f->original));
        f->varlist = nullptr;
    }
    else {
        // We can reuse the varlist and its data allocation, which may be
        // big enough for ensuing calls.
        //
        // But no series bits we didn't set should be set...and right now,
        // only DETAILS_FLAG_IS_NATIVE sets HOLD.  Clear that.
        //
        CLEAR_SERIES_INFO(f->varlist, HOLD);
        CLEAR_SUBCLASS_FLAG(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED);

        assert(
            0 == (SER_INFO(f->varlist) & ~(  // <- note bitwise not
                SERIES_INFO_0_IS_FALSE
                    | FLAG_USED_BYTE(255)  // mask out non-dynamic-len
        )));
    }

  #if !defined(NDEBUG)
    if (f->varlist) {
        assert(NOT_SERIES_FLAG(f->varlist, INACCESSIBLE));
        assert(NOT_SERIES_FLAG(f->varlist, MANAGED));

        RELVAL *rootvar = ARR_HEAD(f->varlist);
        assert(CTX_VARLIST(VAL_CONTEXT(rootvar)) == f->varlist);
        INIT_VAL_FRAME_PHASE_OR_LABEL(rootvar, nullptr);  // can't trash ptr
        TRASH_POINTER_IF_DEBUG(mutable_BINDING(rootvar));
    }
  #endif

    f->original = nullptr; // signal an action is no longer running

    TRASH_OPTION_IF_DEBUG(f->label);
  #if DEBUG_FRAME_LABELS
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
  #endif
}
