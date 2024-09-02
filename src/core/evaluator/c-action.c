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
// This file contains the Action_Executor(), which does the work of calling
// functions in the evaluator.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Action_Executor() is LONG.  That is for the most part a purposeful choice.
//   Breaking it into functions would add overhead (in the debug build if not
//   also release builds) and prevent interesting tricks and optimizations.
//   It is separated into sections, and the invariants in each section are
//   made clear with comments and asserts.
//

#include "sys-core.h"


// Prefer these to XXX_Executor_Flag(ACTION) in this file (much faster!)

#define Get_Action_Executor_Flag(L,name) \
    (((L)->flags.bits & ACTION_EXECUTOR_FLAG_##name) != 0)

#define Not_Action_Executor_Flag(L,name) \
    (((L)->flags.bits & ACTION_EXECUTOR_FLAG_##name) == 0)

#define Set_Action_Executor_Flag(L,name) \
    ((L)->flags.bits |= ACTION_EXECUTOR_FLAG_##name)

#define Clear_Action_Executor_Flag(L,name) \
    ((L)->flags.bits &= ~ACTION_EXECUTOR_FLAG_##name)


// By the same token, prefer direct testing of IN_DISPATCH to the macros

#undef Is_Level_Dispatching
#undef Is_Level_Fulfilling


#define L_next              cast(const Cell*, L->feed->p)
#define L_next_gotten       L->feed->gotten
#define L_specifier         Level_Specifier(L)

#undef ARG                       // undefine the ARG(x) macro that natives use
#define ARG L->u.action.arg      // ...aredefine as currently fulfilling arg
#define stable_ARG Stable_Unchecked(ARG)

#undef PARAM
#define PARAM L->u.action.param

#define KEY L->u.action.key
#define KEY_TAIL L->u.action.key_tail

#define ORIGINAL L->u.action.original

#define level_ L  // for OUT, SPARE, STATE macros


// In debug builds, the KIND_BYTE() calls enforce cell validity...but slow
// things down a little.  So we only use the checked version in the main
// switch statement.  This abbreviation is also shorter and more legible.
//
#define kind_current KIND_BYTE_UNCHECKED(v)


// When arguments are hard quoted or soft-quoted, they don't call into the
// evaluator to do it.  But they need to use the logic of the evaluator for
// noticing when to defer enfix:
//
//     foo: func [...] [
//          return the 1 then ["this needs to be returned"]
//     ]
//
// If the first time the THEN was seen was not after the 1, but when the
// THE ran, it would get deferred until after the RETURN.  This is not
// consistent with the pattern people expect.
//
// Returns TRUE if it set the flag.
//
bool Lookahead_To_Sync_Enfix_Defer_Flag(Feed* feed) {
    assert(Not_Feed_Flag(feed, DEFERRING_ENFIX));
    assert(not feed->gotten);

    Clear_Feed_Flag(feed, NO_LOOKAHEAD);

    if (Is_Feed_At_End(feed))
        return false;

    if (VAL_TYPE_UNCHECKED(At_Feed(feed)) != REB_WORD)
        return false;

    feed->gotten = Lookup_Word(At_Feed(feed), FEED_SPECIFIER(feed));

    if (
        not feed->gotten
        or not Is_Action(unwrap feed->gotten)
    ){
        return false;
    }

    if (Not_Enfixed(unwrap feed->gotten))
        return false;

    if (Get_Action_Flag(VAL_ACTION(unwrap feed->gotten), DEFERS_LOOKBACK))
        Set_Feed_Flag(feed, DEFERRING_ENFIX);
    return true;
}


//
//  Proxy_Multi_Returns_Core: C
//
// This code has to be factored out because RETURN uses it before it does an
// UNWIND.  We already force type checking through the returns, so this (along
// with any typechecking) should also be done.
//
// 1. At one time this code would not do proxying if the value to return was
//    null or void.  This was to have parity with a usermode RETURN which
//    implemented the same policy.  However that policy proved to be a burden
//    on usermode RETURN, so it was changed to require explicit suppression
//    of multi-return proxying via RETURN/ONLY.  Hence natives now need to
//    take that responsibility of choosing whether or not to proxy.
//
Bounce Proxy_Multi_Returns_Core(Level* L, Atom* v)
{
    assert(not Is_Raised(v));

    StackIndex base = TOP_INDEX;

    Copy_Meta_Cell(PUSH(), v);  // packs contain meta values

    KEY = ACT_KEYS(&KEY_TAIL, L->u.action.original);
    PARAM = ACT_PARAMS_HEAD(L->u.action.original);
    ARG = Level_Args_Head(L);

    for (; KEY != KEY_TAIL; ++KEY, ++PARAM, ++ARG) {
        if (Is_Specialized(PARAM))
            continue;
        if (Cell_ParamClass(PARAM) != PARAMCLASS_OUTPUT)
            continue;

        if (not Typecheck_Coerce_Argument(PARAM, ARG))
            fail (Error_Phase_Arg_Type(L, KEY, PARAM, stable_ARG));

        Copy_Meta_Cell(PUSH(), stable_ARG);  // packs contain meta values
    }

    if (TOP_INDEX == base + 1)  // no multi return values
        DROP();  // drop the initial push for the main result
    else
        Init_Pack(v, Pop_Stack_Values(base));

    return v;
}


//
//  Action_Executor: C
//
Bounce Action_Executor(Level* L)
{
    if (THROWING) {
        if (Get_Action_Executor_Flag(L, DISPATCHER_CATCHES))
            goto dispatch_phase;  // wants to see the throw

        if (Get_Level_Flag(L, ABRUPT_FAILURE)) {
            assert(Get_Level_Flag(L, NOTIFY_ON_ABRUPT_FAILURE));
            goto dispatch_phase;
        }

        goto handle_thrown_maybe_redo;
    }

    if (Not_Action_Executor_Flag(L, IN_DISPATCH)) {
        assert(Not_Action_Executor_Flag(L, DISPATCHER_CATCHES));
        assert(Not_Level_Flag(L, NOTIFY_ON_ABRUPT_FAILURE));

        switch (STATE) {
          case ST_ACTION_INITIAL_ENTRY:
            STATE = ST_ACTION_FULFILLING_ARGS;
            goto fulfill;

          case ST_ACTION_INITIAL_ENTRY_ENFIX:
            STATE = ST_ACTION_FULFILLING_ENFIX_FROM_OUT;
            goto fulfill;

          case ST_ACTION_FULFILLING_ARGS:
            if (Cell_ParamClass(PARAM) != PARAMCLASS_META) {
                if (Is_Barrier(ARG)) {
                    STATE = ST_ACTION_BARRIER_HIT;
                    Init_Anti_Word(ARG, Canon(END));
                }
                else
                    Decay_If_Unstable(ARG);
            }
            goto continue_fulfilling;

          case ST_ACTION_TYPECHECKING:
            goto typecheck_then_dispatch;

          case ST_ACTION_FULFILLING_ENFIX_FROM_OUT:  // no evals during this
          default:
            assert(false);
        }
    }

    if (Get_Action_Executor_Flag(L, DELEGATE_CONTROL)) {  // delegation done
        Clear_Action_Executor_Flag(L, DELEGATE_CONTROL);
        goto check_output;  // since it's done, return type should be checked
    }

    goto dispatch_phase;  // STATE byte belongs to dispatcher after fulfill

  fulfill: {  ////////////////////////////////////////////////////////////////

    assert(not Is_Pointer_Corrupt_Debug(ORIGINAL));  // set by Begin_Action()

    assert(TOP_INDEX >= L->baseline.stack_base);  // paths push refinements

    assert(Not_Action_Executor_Flag(L, DOING_PICKUPS));

    for (; KEY != KEY_TAIL; ++KEY, ++ARG, ++PARAM) {

  //=//// CONTINUES (AT TOP SO GOTOS DO NOT CROSS INITIALIZATIONS /////////=//

        goto fulfill_loop_body;  // optimized out

      continue_fulfilling:
        assert(Is_Stable(ARG));  // also checks ARG is readable

        if (Get_Action_Executor_Flag(L, DOING_PICKUPS)) {
            if (TOP_INDEX != L->baseline.stack_base)
                goto next_pickup;

            goto fulfill_and_any_pickups_done;
        }
        continue;

      skip_fulfilling_arg_for_now:
        assert(Not_Action_Executor_Flag(L, DOING_PICKUPS));
        assert(Is_Unspecialized(ARG));
        continue;

  //=//// ACTUAL LOOP BODY ////////////////////////////////////////////////=//

      fulfill_loop_body:

      #if DEBUG
        assert(Is_Cell_Poisoned(ARG));
      #endif

        Erase_Cell(ARG);  // poison in debug, uninitialized memory in release

  //=//// SKIP ALREADY SPECIALIZED ARGUMENTS //////////////////////////////=//

        // In the fulfillment walk, the PARAM is coming from the exemplar.
        // Slots that are specialized hold values in lieu of the parameter
        // information (whether it's quoted or a refinement or what types
        // it accepts).
        //
        // The typechecking walk uses a PARAM coming from the phase, so this
        // means it can type check the specialized slots on behalf of the
        // underlying phase that will be running.
        //
        if (Is_Specialized(PARAM)) {
            Copy_Cell(ARG, PARAM);
            goto continue_fulfilling;
        }

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

        if (TOP_INDEX != BASELINE->stack_base) {  // reorderings/refinements
            StackValue(*) ordered = TOP;
            StackValue(*) lowest_ordered = Data_Stack_At(BASELINE->stack_base);
            const Symbol* param_symbol = KEY_SYMBOL(KEY);

            for (; ordered != lowest_ordered; --ordered) {
                assert(Is_Pushed_Refinement(ordered));

                if (Cell_Word_Symbol(ordered) != param_symbol)
                    continue;

                REBLEN offset = ARG - cast(Atom*, Level_Args_Head(L));
                INIT_VAL_WORD_INDEX(ordered, offset + 1);
                BINDING(ordered) = L->u.action.original;

                if (Is_Parameter_Unconstrained(PARAM)) {
                    //
                    // There's no argument, so we won't need to come back
                    // for this one.  But we did need to set its index
                    // so we knew it was valid (errors later if not set).
                    //
                    Init_Blackhole(ARG);  // # means refinement used
                    goto continue_fulfilling;
                }

                Copy_Cell(ARG, PARAM);
                goto skip_fulfilling_arg_for_now;
            }
        }

  //=//// A /REFINEMENT ARG ///////////////////////////////////////////////=//

        if (Get_Parameter_Flag(PARAM, REFINEMENT)) {
            assert(Not_Action_Executor_Flag(L, DOING_PICKUPS));  // jump lower
            assert(Is_Cell_Erased(ARG));
            Copy_Cell(ARG, PARAM);  // fills with pickup, or null by typecheck
            goto continue_fulfilling;
        }

  //=//// ARGUMENT FULFILLMENT ////////////////////////////////////////////=//

      fulfill_arg: ;  // semicolon needed--next statement is declaration

        ParamClass pclass = Cell_ParamClass(PARAM);

  //=//// SKIP OVER RETURN /////////////////////////////////////////////////=//

        // The return function is filled in by the dispatchers that provide it.

        if (pclass == PARAMCLASS_RETURN or pclass == PARAMCLASS_OUTPUT) {
            assert(Not_Action_Executor_Flag(L, DOING_PICKUPS));
            assert(Is_Cell_Erased(ARG));
            Copy_Cell(ARG, PARAM);
            goto continue_fulfilling;
        }

  //=//// HANDLE IF NEXT ARG IS IN OUT SLOT (e.g. ENFIX, CHAIN) ///////////=//

    // 1. Seeing a fresh  output slot could mean that there was really
    //    "nothing" to the left:
    //
    //        (else [...])
    //
    //    -or- it could be a consequence of being in a cell where arguments
    //    are gathering; e.g. the `+` here will perceive "nothing":
    //
    //        if + 2 [...]
    //
    // 2. Something like `lib/help left-lit` is allowed to work, but if it was
    //    just `obj/int-value left-lit` then the path evaluation won...but
    //    LEFT-LIT still gets run.  It appears it has nothing to its left, but
    //    since we remembered what happened we can give an informative error
    //    instead of a perplexing one.
    //
    // 3. If an enfixed function finds it has a variadic in its first slot,
    //    then nothing available on the left is o.k.  It means we have to put
    //    a VARARGS! in that argument slot which will react with TRUE to TAIL?,
    //    so feed it from the global empty array.
    //
    // 4. Enfix functions with variadics on the left can also deal with a
    //    single value.  An unevaluated is stored into an array-form variadic,
    //    so the user can do 0 or 1 TAKEs of it.
    //
    //    !!! It be evaluated when they TAKE (it if it's an evaluative arg),
    //    but not if they don't.  Should failing to TAKE be seen as an error?
    //    Failing to take first gives out-of-order evaluation.
    //
    // 5. This can happen e.g. with `x: 10 | x >- the`.  We raise an error in
    //    this case, while still allowing `10 >- the` to work, so people don't
    //    have to go out of their way rethinking operators if it could just
    //    work out for inert types.
    //
    // 6. SOFT permits L->out to not carry the UNEVALUATED flag--enfixed
    //    operations which have evaluations on their left are treated as if
    //    they were in a GROUP!.  This is important to `1 + 2 ->- lib/* 3`
    //    being 9, while also allowing `1 + x: ->- lib/default [...]` to work.
    //
    // 7. MEDIUM escapability means that it only allows the escape of one unit.
    //    Thus when reaching this point, it must carry the UENEVALUATED FLAG.

        if (STATE == ST_ACTION_BARRIER_HIT) {
            Init_Anti_Word(ARG, Canon(END));
            goto continue_fulfilling;
        }

        if (STATE == ST_ACTION_FULFILLING_ENFIX_FROM_OUT) {
            STATE = ST_ACTION_FULFILLING_ARGS;

            if (Is_Fresh(OUT)) {  // "nothing" to left, but [1]
                if (Get_Action_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH))
                    fail (Error_Literal_Left_Path_Raw());  // [2]

                if (Get_Parameter_Flag(PARAM, VARIADIC)) {  // empty is ok [3]
                    Init_Varargs_Untyped_Enfix(ARG, nullptr);
                    goto continue_fulfilling;
                }

                if (Not_Parameter_Flag(PARAM, ENDABLE))
                    fail (Error_No_Arg(L->label, KEY_SYMBOL(KEY)));

                Init_Nulled(ARG);
                goto continue_fulfilling;
            }

            if (Get_Parameter_Flag(PARAM, VARIADIC)) {  // non-empty is ok [4]
                assert(not Is_Nothing(OUT));
                Decay_If_Unstable(OUT);  // !!! ^META variadics?
                Init_Varargs_Untyped_Enfix(ARG, stable_OUT);
                Freshen_Cell(OUT);
            }
            else switch (pclass) {
              case PARAMCLASS_NORMAL:
                Decay_If_Unstable(OUT);
                Move_Cell(ARG, OUT);
                break;

              case PARAMCLASS_META: {
                Move_Cell(ARG, OUT);
                Meta_Quotify(ARG);
                break; }

              case PARAMCLASS_HARD:  // PARAMETER_FLAG_SKIPPABLE in pre-lookback
                Move_Cell(ARG, OUT);
                break;

              case PARAMCLASS_SOFT:  // can carry UNEVALUATED [6]
                goto escapable;

              case PARAMCLASS_MEDIUM:  // must carry UNEVALUATED [7]
                goto escapable;

              escapable:
                if (ANY_ESCAPABLE_GET(OUT)) {
                    if (Eval_Value_Throws(ARG, cast(Element*, OUT), SPECIFIED))
                        goto handle_thrown_maybe_redo;
                    Freshen_Cell(OUT);
                }
                else
                    Move_Cell(ARG, OUT);
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
            if (Get_Action_Executor_Flag(L, RUNNING_ENFIX)) {
                assert(Not_Feed_Flag(L->feed, NO_LOOKAHEAD));
                if (
                    Not_Action_Flag(Level_Phase(L), POSTPONES_ENTIRELY)
                    and
                    Not_Action_Flag(Level_Phase(L), DEFERS_LOOKBACK)
                ){
                    Set_Feed_Flag(L->feed, NO_LOOKAHEAD);
                }
            }

            assert(Is_Fresh(OUT));  // output should have been "used up"
            goto continue_fulfilling;
        }

  //=//// NON-ENFIX VARIADIC ARG (doesn't consume anything *yet*) /////////=//

        // Evaluation argument "hook" parameters (marked in FUNC by
        // `<variadic>`).  They point back to this call through a reified
        // FRAME!, and are able to consume additional arguments during the
        // function run.
        //
        if (Get_Parameter_Flag(PARAM, VARIADIC)) {
            Force_Level_Varlist_Managed(L);
            Init_Varargs_Untyped_Normal(ARG, L);
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
        if (Not_Action_Executor_Flag(L, RUNNING_ENFIX))
            Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);

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
        if (Get_Feed_Flag(L->feed, DEFERRING_ENFIX))
            fail (Error_Ambiguous_Infix_Raw());

  //=//// ERROR ON END MARKER, BAR! IF APPLICABLE /////////////////////////=//

        if (Is_Level_At_End(L)) {
            Init_Anti_Word(ARG, Canon(END));
            goto continue_fulfilling;
        }

        switch (pclass) {

  //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes 1 EVALUATE's worth) /////=//

          case PARAMCLASS_OUTPUT:  // e.g. evaluate/next [1 + 2]
            goto output_from_feed;

        output_from_feed:
          case PARAMCLASS_NORMAL:
          case PARAMCLASS_META: {
            if (Is_Level_At_End(L)) {
                Init_Anti_Word(ARG, Canon(END));
                goto continue_fulfilling;
            }

            Flags flags = EVAL_EXECUTOR_FLAG_FULFILLING_ARG;
            if (pclass == PARAMCLASS_META) {
                flags |= LEVEL_FLAG_META_RESULT;
            }

            Level* sub = Make_Level(&Stepper_Executor, L->feed, flags);
            Push_Level(ARG, sub);

            return CATCH_CONTINUE_SUBLEVEL(sub); }

  //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG ///////////////////////////////=//

          case PARAMCLASS_HARD:
            //
            // !!! Need to think about how cases like `source ||` or `help ||`
            // are supposed to act.  They set the "barrier hit" and then we
            // get here...if we don't clear the flag, then the presence of
            // a non-void causes a later assert.  Review.
            //
            if (Not_Parameter_Flag(PARAM, SKIPPABLE))
                The_Next_In_Feed(ARG, L->feed);
            else {
                Derelativize(SPARE, L_next, L_specifier);
                if (not Typecheck_Atom(PARAM, SPARE)) {
                    assert(Get_Parameter_Flag(PARAM, ENDABLE));
                    Init_Nulled(ARG);  // not actually an ~end~ (?)
                    goto continue_fulfilling;
                }
                The_Next_In_Feed(ARG, L->feed);
            }

            // Have to account for enfix deferrals in cases like:
            //
            //     return the 1 then (x => [x + 1])
            //
            Lookahead_To_Sync_Enfix_Defer_Flag(L->feed);

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

          case PARAMCLASS_SOFT:
          case PARAMCLASS_MEDIUM:
            The_Next_In_Feed(ARG, L->feed);

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
                Lookahead_To_Sync_Enfix_Defer_Flag(L->feed) and  // ensure got
                (pclass == PARAMCLASS_SOFT and Get_Subclass_Flag(
                    VARLIST,
                    ACT_PARAMLIST(VAL_ACTION(unwrap L->feed->gotten)),
                    PARAMLIST_QUOTES_FIRST
                ))
            ){
                // We need to defer and let the right hand quote that is
                // quoting leftward win.  We use ST_STEPPER_LOOKING_AHEAD
                // to jump into a sublevel where sub->out is the ARG,
                // and it knows to get the arg from there.

                Flags flags =
                    FLAG_STATE_BYTE(ST_STEPPER_LOOKING_AHEAD)  // no Freshen_Cell()
                    | EVAL_EXECUTOR_FLAG_FULFILLING_ARG
                    | EVAL_EXECUTOR_FLAG_INERT_OPTIMIZATION;

                Level* sub = Make_Level(&Stepper_Executor, L->feed, flags);
                Push_Level(ARG, sub);
                return CATCH_CONTINUE_SUBLEVEL(sub);
            }
            else if (ANY_ESCAPABLE_GET(ARG)) {
                //
                // We did not defer the quoted argument.  If the argument
                // is something like a GET-GROUP!, GET-WORD!, or GET-TUPLE!...
                // it has to be evaluated.
                //
                Move_Cell(SPARE, ARG);
                if (Get_Var_Core_Throws(
                    ARG,
                    GROUPS_OK,
                    Stable_Unchecked(SPARE),
                    SPECIFIED
                )){
                    goto handle_thrown_maybe_redo;
                }
            }
            break;

          case PARAMCLASS_RETURN:  // should not happen!
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
        //     30 = (10 + 20 eval [comment "hi"])
        //
        // Is breaking this.  Review when there is time, and put the assert
        // back if it makes sense.
        //
        /* assert(Not_Feed_Flag(L->feed, NO_LOOKAHEAD)); */
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);

        goto continue_fulfilling;
    }

  #if DEBUG_POISON_FLEX_TAILS
    assert(Is_Cell_Poisoned(ARG));  // arg can otherwise point to any arg cell
  #endif

    // There may have been refinements that were skipped because the
    // order of definition did not match the order of usage.  They were
    // left on the stack with a pointer to the `param` and `arg` after
    // them for later fulfillment.
    //
    // Note that there may be functions on the stack if this is the
    // second time through, and we were just jumping up to check the
    // parameters in response to a BOUNCE_REDO_CHECKED; if so, skip this.
    //
    if (TOP_INDEX != BASELINE->stack_base) {

      next_pickup:

        assert(Is_Pushed_Refinement(TOP));

        if (not BINDING(TOP)) {  // the loop didn't index it
            Refinify_Pushed_Refinement(TOP);
            fail (Error_Bad_Parameter_Raw(TOP));  // so duplicate or junk
        }

        // Level_Args_Head offsets are 0-based, while index is 1-based.
        // But +1 is okay, because we want the slots after the refinement.
        //
        REBINT offset =
            VAL_WORD_INDEX(TOP) - (ARG - cast(Atom*, Level_Args_Head(L))) - 1;
        KEY += offset;
        ARG += offset;
        PARAM += offset;

        assert(Cell_Word_Symbol(TOP) == KEY_SYMBOL(KEY));
        DROP();

        if (Is_Parameter_Unconstrained(PARAM)) {  // no callsite arg, just drop
            if (TOP_INDEX != BASELINE->stack_base)
                goto next_pickup;

            goto fulfill_and_any_pickups_done;
        }

        assert(Is_Unspecialized(ARG));

        Set_Action_Executor_Flag(L, DOING_PICKUPS);
        goto fulfill_arg;
    }

} fulfill_and_any_pickups_done: {  ///////////////////////////////////////////

    if (Get_Action_Executor_Flag(L, FULFILL_ONLY)) {  // no typecheck
        assert(Is_Fresh(OUT));  // didn't touch out, should be fresh
        Init_Nothing(OUT);
        goto skip_output_check;
    }

    STATE = ST_ACTION_TYPECHECKING;

    // Action arguments now gathered, do typecheck pass

} typecheck_then_dispatch: {  ////////////////////////////////////////////////

  // It might seem convenient to type check arguments while they are being
  // fulfilled vs. performing another loop.  But the semantics of the system
  // allows manipulation of arguments between fulfillment and execution, and
  // that could turn invalid arguments good or valid arguments bad.  Plus if
  // all the arguments are evaluated before any type checking, that puts
  // custom type checks in the body of a function on equal footing with
  // any system-optimized type checking.
  //
  // So a second loop is required by the system's semantics.
  //
  // 1. We assume typecheck was done when the parameter was specialized.  It
  //    cannot be manipulated from the outside (e.g. by REFRAMER) so there is
  //    no benefit to deferring the check, only extra cost on each invocation.
  //
  //    BUT note that if you have a redo situation as with an ENCLOSE, a
  //    specialized out parameter becomes visible in the frame and can be
  //    modified.  Even though it's hidden, it may need to be typechecked
  //    again (unless it was *fully* hidden).
  //
  // 2. Parameter antiforms are the default values from MAKE FRAME!.
  //
  // 3. We can't a-priori typecheck the variadic argument, since the values
  //    aren't calculated until the function starts running.  Instead we stamp
  //    this instance of the varargs with a way to reach back and see the
  //    parameter type signature.
  //
  //    The data feed is unchanged (can come from this frame, or another, or
  //    just an array from MAKE VARARGS! of a BLOCK!)
  //
  // 4. Store the offset so that both the arg and param locations can quickly
  //    be recovered, while using only a single slot in the cell.  Sign denotes
  //    whether the parameter was enfixed or not.

    assert(STATE == ST_ACTION_TYPECHECKING);

    Freshen_Cell(OUT);

    KEY = ACT_KEYS(&KEY_TAIL, Level_Phase(L));
    ARG = Level_Args_Head(L);
    PARAM = ACT_PARAMS_HEAD(Level_Phase(L));

    for (; KEY != KEY_TAIL; ++KEY, ++PARAM, ++ARG) {
        assert(Is_Stable(ARG));  // implicitly asserts Ensure_Readable(ARG)

        if (Is_Specialized(PARAM))  // checked when specialized [1]
            continue;

        if (
            Cell_ParamClass(PARAM) == PARAMCLASS_RETURN
            or Cell_ParamClass(PARAM) == PARAMCLASS_OUTPUT
        ){
            assert(Not_Specialized(stable_ARG));
            continue;  // typeset is its legal return types, wants to be unset
        }

        if (Not_Specialized(stable_ARG)) {
            if (Get_Parameter_Flag(PARAM, REFINEMENT)) {
                Init_Nulled(ARG);
                continue;
            }
            if (Get_Parameter_Flag(PARAM, SKIPPABLE)) {
                Init_Nulled(ARG);
                continue;
            }
        }
        else if (Is_Void(ARG)) {
            if (Get_Parameter_Flag(PARAM, NOOP_IF_VOID)) {  // e.g. <maybe> param
                Set_Action_Executor_Flag(L, TYPECHECK_ONLY);
                Init_Nulled(OUT);
                continue;
            }
            if (Get_Parameter_Flag(PARAM, REFINEMENT)) {
                Init_Nulled(ARG);
                continue;
            }
        }
        else if (Is_Anti_Word_With_Id(ARG, SYM_END)) {
            if (Not_Parameter_Flag(PARAM, ENDABLE))
                fail (Error_No_Arg(L->label, KEY_SYMBOL(KEY)));
            Init_Nulled(ARG);  // more convenient, use ^META for nuance
            continue;
        }

        if (Get_Parameter_Flag(PARAM, VARIADIC)) {  // can't check now [3]
            if (not Is_Varargs(ARG))  // argument itself is always VARARGS!
                fail (Error_Not_Varargs(L, KEY, PARAM, stable_ARG));

            INIT_VAL_VARARGS_PHASE(ARG, Level_Phase(L));

            bool enfix = false;  // !!! how does enfix matter?
            VAL_VARARGS_SIGNED_PARAM_INDEX(ARG) =  // store offset [4]
                enfix
                    ? -(ARG - cast(Atom*, Level_Args_Head(L)) + 1)
                    : ARG - cast(Atom*, Level_Args_Head(L)) + 1;

            assert(VAL_VARARGS_SIGNED_PARAM_INDEX(ARG) != 0);
            continue;
        }

        if (not Typecheck_Coerce_Argument(PARAM, ARG))
            fail (Error_Phase_Arg_Type(L, KEY, PARAM, stable_ARG));
    }

  // Action arguments now gathered, begin dispatching

} dispatch: {  ///////////////////////////////////////////////////////////////

  // 1. Here we free the union for use by the dispatcher...though currently
  //    one slot is stolen for the base stack address the dispatcher should
  //    consider (variables can be stored to write back to for multi-return).
  //    It's also needed to keep L->original.  Think about how to improve.
  //
  // 2. This happens if you have something intending to act as enfix but
  //    that does not consume arguments, e.g. `x: enfix func [] []`.  An
  //    enfixed function with no arguments might sound dumb, but it allows
  //    a 0-arity function to run in the same evaluation step as the left
  //    hand side.  This is how expression work (see `|:`)
  //
  //    !!! This is dealt with in `skip_output_check`, is it needed here too?
  //
  // 3. Resetting the spare cell here has a slight cost, but stops leaks of
  //    internal processing to actions.  It means that any attempts to read
  //    the spare cell will give an assert.

    assert(Not_Action_Executor_Flag(L, IN_DISPATCH));
    Set_Action_Executor_Flag(L, IN_DISPATCH);

    Action* save_original = L->u.action.original;
    Corrupt_If_Debug(L->u);  // freed for dispatcher use...
    L->u.action.original = save_original;  // ...er, mostly.  [1]
    L->u.action.dispatcher_base = TOP_INDEX;

    if (STATE == ST_ACTION_FULFILLING_ENFIX_FROM_OUT) {  // can happen [2]
        if (Get_Action_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH))  // see notes
            fail (Error_Literal_Left_Path_Raw());

        assert(Get_Action_Executor_Flag(L, RUNNING_ENFIX));
        Freshen_Cell(OUT);
    }

    assert(Get_Action_Executor_Flag(L, IN_DISPATCH));

    if (Get_Action_Executor_Flag(L, TYPECHECK_ONLY)) {  // <maybe>
        assert(Is_Nulled(OUT));
        goto skip_output_check;
    }

    Freshen_Cell(SPARE);  // tiny cost (one bit clear) but worth it [3]
    STATE = STATE_0;  // reset to zero for each phase

    L_next_gotten = nullptr;  // arbitrary code changes fetched variables

} dispatch_phase: {  /////////////////////////////////////////////////////////

  // 1. Each time a continuation happens, the dispatcher gets a new chance to
  //    decide if it wants to catch throws.
  //
  //    !!! Should this be done in the continuations themselves, so that an
  //    action that doesn't use any continuations won't pay for this clearing?
  //
  // 2. The stale bit is set on the output before we run the dispatcher.  We
  //    check to make sure it's not stale at the end--because that could often
  //    mean the function forgot to write the output cell on some code path.
  //    (To intentionally not write anything and "vaporize", use `return VOID`
  //    which gives back a distinct `Bounce` signal to know it's purposeful.)

    assert(Not_Action_Executor_Flag(LEVEL, DELEGATE_CONTROL));  // delegated!
    Clear_Action_Executor_Flag(LEVEL, DISPATCHER_CATCHES);  // [1]

    Action* phase = Level_Phase(L);

    Dispatcher* dispatcher = ACT_DISPATCHER(phase);

    Bounce b = (*dispatcher)(L);

    if (b == OUT) {  // common case, made fastest
        assert(not Is_Fresh(OUT));  // must write output, even if just void
    }
    else if (b == nullptr) {  // API and internal code can both return `nullptr`
        Init_Nulled(OUT);
    }
    else if (Is_Bounce_An_Atom(b)) {
        Atom* r = Atom_From_Bounce(b);
        assert(Is_Api_Value(r));
        Copy_Cell(OUT, r);
        Release_Api_Value_If_Unmanaged(r);
    }
    else switch (VAL_RETURN_SIGNAL(b)) {  // it's a "pseudotype" instruction

      case C_CONTINUATION:
        return BOUNCE_CONTINUE;  // Note: may not have pushed a new level...

      case C_DELEGATION:
        Set_Action_Executor_Flag(LEVEL, DELEGATE_CONTROL);
        STATE = DELEGATE_255;  // the trampoline does this when delegating
        return BOUNCE_CONTINUE;

      case C_SUSPEND:
        return BOUNCE_SUSPEND;

      case C_THROWN:
        goto handle_thrown_maybe_redo;

      case C_REDO_UNCHECKED:
        Clear_Action_Executor_Flag(L, IN_DISPATCH);
        goto dispatch;  // Note: dispatcher may have changed level's PHASE

      case C_REDO_CHECKED:
        Clear_Action_Executor_Flag(L, IN_DISPATCH);
        STATE = ST_ACTION_TYPECHECKING;
        goto typecheck_then_dispatch;

      default:
        assert(!"Invalid pseudotype returned from action dispatcher");
    }

    goto check_output;

} check_output: {  ///////////////////////////////////////////////////////////

  // Here we know the function finished and nothing threw past it or had an
  // abrupt fail().  (It may have done a `return RAISE(...)`, however.)

  #if !defined(NDEBUG)
    Do_After_Action_Checks_Debug(L);
  #endif

    if (not Is_Raised(OUT))  // !!! Should there be an R_FAIL ?
        assert(L->u.action.dispatcher_base == TOP_INDEX);

} skip_output_check: {  //////////////////////////////////////////////////////

  // This is where things get jumped to if you pass a <maybe> argument a
  // VOID and it wants to jump past all the processing and return, or if
  // a level just wants argument fulfillment and no execution.
  //
  // NOTE: Anything that calls fail() must do so before Drop_Action()!
  //
  // 1. !!! This used to assert rather than fail, but it turns out this can
  //    actually happen:
  //
  //      >> left-soft: enfix func ['x [word!]] [return x]
  //      >> (|| left-soft)
  //
  //    The LEFT-SOFT looked back, and would have been able to take the ||
  //    except it noticed that it took no arguments.  So it allowed the ||
  //    to win the context (this is how HELP can quote things that quote
  //    left and would usually win, but don't when they have no args).
  //
  // 2. Want to keep this flag between an operation and an ensuing enfix in
  //    the same level, so can't clear in Drop_Action(), e.g. due to:
  //
  //      left-the: enfix :the
  //      o: make object! [f: does [1]]
  //      o.f left-the  ; want error suggesting -> here, need flag for that

    if (STATE == ST_ACTION_FULFILLING_ENFIX_FROM_OUT)  // [1]
        fail ("Left lookback toward thing that took no args, look at later");

    Clear_Action_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH);  // [2]

    Drop_Action(L);  // must fail before Drop_Action()

    return OUT;  // not thrown

} handle_thrown_maybe_redo: {  ///////////////////////////////////////////////

  // Until stackless is universal, an action dispatcher may make a stackful
  // call to something that issues a REDO.  So we can't handle REDO at the
  // top of this executor where we test for THROWING, the way we might if
  // we could always expect continuations as the sources of throws.

    const Value* label = VAL_THROWN_LABEL(level_);
    if (Is_Frame(label)) {
        if (
            VAL_ACTION(label) == VAL_ACTION(Lib(REDO))  // REDO [1]
            and VAL_FRAME_COUPLING(label) == cast(Context*, L->varlist)
        ){
            CATCH_THROWN(OUT, level_);
            assert(Is_Logic(OUT));  // signal if we want to gather args or not

            assert(Get_Action_Executor_Flag(L, IN_DISPATCH));
            Clear_Action_Executor_Flag(L, IN_DISPATCH);

            Clear_Action_Executor_Flag(L, DISPATCHER_CATCHES);
            Clear_Level_Flag(L, NOTIFY_ON_ABRUPT_FAILURE);

            if (Is_True(OUT)) {
                STATE = ST_ACTION_FULFILLING_ARGS;
                goto fulfill;
            }
            STATE = ST_ACTION_TYPECHECKING;
            goto typecheck_then_dispatch;
        }
    }

    while (TOP_LEVEL != L)  // convenient for natives pushing SUBLEVEL [4]
        Drop_Level(TOP_LEVEL);  // !!! Should all inert levels be aborted?

    Drop_Action(L);
    Drop_Data_Stack_To(L->baseline.stack_base);  // unprocessed refinements

    return BOUNCE_THROWN;
}}


//
//  Push_Action: C
//
// Allocate the Array of Values inspected by a function when executed (the
// Cells behind ARG(name), REF(name), D_ARG(3),  etc.)
//
// 1. We perform a traversal of the argument slots.  This fills any cells that
//    have been specialized with the specialized value, and erases cells
//    that need to be fulfilled from the callsite.
//
//    Originally the idea was to leave the argument slots uninitialized here,
//    and have the code that fulfills the arguments handle copying the
//    specialized values.  This makes the GC have to be more sensitive to
//    how far fulfillment has progressed and not try to mark uninitialized
//    memory--the invariants are messier, but it's technically possible.  But
//    the unification of frames and actions nixed this idea, since if frames
//    are mutable they could be modified by code that runs in mid-fulfillment.
//
//    Additionally, only a subset of the information needed for specialization
//    is available to the fulfillment process.  It walks the "paramlist" of
//    the underlying action, which contains typechecking information for
//    slots that are nothing (and hence unspecialized) for frame invocations.
//    But walking exemplar frames to see specializations would only see those
//    nothing cells and not know how to typecheck.
//
//    Empirically this extra walk can be costing us as much as 5% of runtime
//    vs. leaving memory uninitialized and folding the walks together.  If
//    frames could somehow be rigged to store parameters instead of nothing
//    and merely give the impression of nothing on extraction, the gains could
//    be substantial.
//
// 2. Each layer of specialization of a function can only add specializations
//    of arguments which have not been specialized already.  For efficiency,
//    the act of specialization merges all the underlying specializations
//    together.  This means only the outermost specialization is needed to
//    fill the specialized slots contributed by later phases.
//
void Push_Action(
    Level* L,
    Action* act,
    Option(Context*) coupling  // actions may only be coupled to contexts ATM
){
    assert(L->executor == &Action_Executor);

    assert(Not_Action_Executor_Flag(L, FULFILL_ONLY));
    assert(Not_Action_Executor_Flag(L, RUNNING_ENFIX));

    Length num_args = ACT_NUM_PARAMS(act);  // includes specialized + locals

    assert(L->varlist == nullptr);

    Stub* s = Prep_Stub(
        Alloc_Stub(),  // not preallocated
        FLEX_MASK_VARLIST
            | FLEX_FLAG_FIXED_SIZE  // FRAME!s don't expand ATM
            // not managed by default, see Force_Level_Varlist_Managed()
    );
    s->info.any.flags = FLEX_INFO_MASK_NONE;
    INIT_BONUS_KEYSOURCE(x_cast(Array*, s), L);  // maps varlist back to L
    MISC(VarlistAdjunct, s) = nullptr;
    LINK(Patches, s) = nullptr;

    if (not Did_Flex_Data_Alloc(
        s,
        num_args + 1 + ONE_IF_POISON_TAILS  // +1 is rootvar
    )){
        Set_Flex_Inaccessible(s);
        GC_Kill_Stub(s);  // ^-- needs non-null data unless free
        fail (Error_No_Memory(
            sizeof(Cell) * (num_args + 1 + ONE_IF_POISON_TAILS))
        );
    }

    L->varlist = x_cast(Array*, s);

    L->rootvar = cast(Element*, s->content.dynamic.data);
    USED(Erase_Cell(L->rootvar));  // want the tracking info, overwriting header
    L->rootvar->header.bits =
        NODE_FLAG_NODE
            | NODE_FLAG_CELL
            | CELL_FLAG_PROTECTED  // payload/coupling tweaked, but not by user
            | CELL_MASK_FRAME
            | FLAG_QUOTE_BYTE(NOQUOTE_1);
    INIT_VAL_CONTEXT_VARLIST(L->rootvar, L->varlist);

    INIT_VAL_FRAME_PHASE(L->rootvar, ACT_IDENTITY(act));  // Level_Phase()
    INIT_VAL_FRAME_COUPLING(L->rootvar, coupling);  // Level_Coupling()

    s->content.dynamic.used = num_args + 1;

  #if DEBUG
    Cell* tail = Array_Tail(L->varlist);
    Cell* prep = L->rootvar + 1;
    for (; prep < tail; ++prep)
        Poison_Cell(prep);

    #if DEBUG_POISON_EXCESS_CAPACITY  // poison cells past usable range
        for (; prep < L->rootvar + s->content.dynamic.rest; ++prep)
            Poison_Cell(prep);  // unreadable + unwritable
    #endif

    #if DEBUG_POISON_FLEX_TAILS  // redundant if excess capacity poisoned
        Poison_Cell(Array_Tail(L->varlist));
    #endif
  #endif

    Array* partials = maybe ACT_PARTIALS(act);
    if (partials) {
        const Element* word_tail = Array_Tail(partials);
        const Element* word = Array_Head(partials);
        for (; word != word_tail; ++word)
            Copy_Cell(PUSH(), word);
    }

    assert(Not_Node_Managed(L->varlist));

    ORIGINAL = act;
}


//
//  Begin_Action_Core: C
//
void Begin_Action_Core(
    Level* L,
    Option(const Symbol*) label,
    bool enfix
){
    // These assertions were blocking code sharing with SET-BLOCK! mechanics.
    // Review where the right place to put them is.
    //
    /*assert(Not_Action_Executor_Flag(L, RUNNING_ENFIX));
    assert(Not_Feed_Flag(L->feed, DEFERRING_ENFIX));*/

    assert(Not_Subclass_Flag(VARLIST, L->varlist, FRAME_HAS_BEEN_INVOKED));
    Set_Subclass_Flag(VARLIST, L->varlist, FRAME_HAS_BEEN_INVOKED);

    KEY = ACT_KEYS(&KEY_TAIL, ACT_IDENTITY(ORIGINAL));
    PARAM = cast(Param*, CTX_VARS_HEAD(ACT_EXEMPLAR(ORIGINAL)));
    ARG = L->rootvar + 1;

    assert(Is_Pointer_Corrupt_Debug(L->label));  // ACTION! makes valid
    assert(not label or Is_String_Symbol(unwrap label));
    L->label = label;
  #if DEBUG_LEVEL_LABELS  // helpful for looking in the debugger
    L->label_utf8 = Level_Label_Or_Anonymous_UTF8(L);
  #endif

    if (enfix) {
        //
        // While ST_ACTION_FULFILLING_ARG_FROM_OUT is set only during the first
        // argument of an enfix call, ACTION_EXECUTOR_FLAG_RUNNING_ENFIX is
        // set for the whole duration.
        //
        Set_Action_Executor_Flag(L, RUNNING_ENFIX);

        // All the enfix call sites cleared this flag on the feed, so it was
        // moved into the Begin_Enfix_Action() case.  Note this has to be done
        // *after* the existing flag state has been captured for invisibles.
        //
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);

        Level_State_Byte(L) = ST_ACTION_INITIAL_ENTRY_ENFIX;
    }
}


//
//  Drop_Action: C
//
void Drop_Action(Level* L) {
    assert(not L->label or Is_String_Symbol(unwrap L->label));

    Clear_Action_Executor_Flag(L, RUNNING_ENFIX);
    Clear_Action_Executor_Flag(L, FULFILL_ONLY);

    assert(BONUS(KeySource, L->varlist) == L);

    if (Is_Node_Managed(L->varlist)) {
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
        L->varlist = CTX_VARLIST(
            Steal_Context_Vars(
                cast(Context*, L->varlist),
                ORIGINAL  // degrade keysource from f
            )
        );
        assert(Not_Node_Managed(L->varlist));
        INIT_BONUS_KEYSOURCE(L->varlist, L);
      #endif

        INIT_BONUS_KEYSOURCE(L->varlist, ACT_KEYLIST(ORIGINAL));
        L->varlist = nullptr;
    }
    else {
        // We can reuse the varlist and its data allocation, which may be
        // big enough for ensuing calls.
        //
        // But no Array bits we didn't set should be set...and right now,
        // only DETAILS_FLAG_IS_NATIVE sets HOLD.  Clear that.
        //
        Clear_Flex_Info(L->varlist, HOLD);
        Clear_Subclass_Flag(VARLIST, L->varlist, FRAME_HAS_BEEN_INVOKED);

        assert(
            0 == (FLEX_INFO(L->varlist) & ~(  // <- note bitwise not
                FLEX_INFO_0_IS_FALSE
                    | FLAG_USED_BYTE(255)  // mask out non-dynamic-len
        )));
    }

  #if !defined(NDEBUG)
    if (L->varlist) {
        assert(Not_Node_Managed(L->varlist));

        Cell* rootvar = Array_Head(L->varlist);
        assert(CTX_VARLIST(VAL_CONTEXT(rootvar)) == L->varlist);
        INIT_VAL_FRAME_PHASE_OR_LABEL(rootvar, nullptr);  // can't corrupt ptr
        Corrupt_Pointer_If_Debug(BINDING(rootvar));
    }
  #endif

    Corrupt_Pointer_If_Debug(ORIGINAL); // action is no longer running
    L->executor = nullptr;  // so GC won't think level needs Action marking

    Corrupt_Pointer_If_Debug(L->label);
  #if DEBUG_LEVEL_LABELS
    Corrupt_Pointer_If_Debug(L->label_utf8);
  #endif
}
