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

#define Get_Action_Executor_Flag(f,name) \
    (((f)->flags.bits & ACTION_EXECUTOR_FLAG_##name) != 0)

#define Not_Action_Executor_Flag(f,name) \
    (((f)->flags.bits & ACTION_EXECUTOR_FLAG_##name) == 0)

#define Set_Action_Executor_Flag(f,name) \
    ((f)->flags.bits |= ACTION_EXECUTOR_FLAG_##name)

#define Clear_Action_Executor_Flag(f,name) \
    ((f)->flags.bits &= ~ACTION_EXECUTOR_FLAG_##name)


// By the same token, prefer direct testing of IN_DISPATCH to the macros

#undef Is_Action_Frame_Dispatching
#undef Is_Action_Frame_Fulfilling


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
#undef f_gotten
#define f_next              cast(const Reb_Cell*, f->feed->p)
#define f_next_gotten       f->feed->gotten

#undef ARG                       // undefine the ARG(x) macro that natives use
#define ARG f->u.action.arg      // ...aredefine as currently fulfilling arg
#define stable_ARG Stable_Unchecked(ARG)

#undef PARAM
#define PARAM f->u.action.param

#define KEY f->u.action.key
#define KEY_TAIL f->u.action.key_tail

#define ORIGINAL f->u.action.original

#define frame_ f  // for OUT, SPARE, STATE macros


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
bool Lookahead_To_Sync_Enfix_Defer_Flag(Feed(*) feed) {
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
        or not Is_Activation(unwrap(feed->gotten))
    ){
        return false;
    }

    if (Not_Action_Flag(VAL_ACTION(unwrap(feed->gotten)), ENFIXED))
        return false;

    if (Get_Action_Flag(VAL_ACTION(unwrap(feed->gotten)), DEFERS_LOOKBACK))
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
Bounce Proxy_Multi_Returns_Core(Frame(*) f, Atom(*) v)
{
    assert(not Is_Raised(v));

    StackIndex base = TOP_INDEX;

    Meta_Quotify(v);  // unquotified at end if not overwritten
    Copy_Cell(PUSH(), v);  // can't push unstable isotopes to data stack

    KEY = ACT_KEYS(&KEY_TAIL, f->u.action.original);
    PARAM = ACT_PARAMS_HEAD(f->u.action.original);
    ARG = FRM_ARGS_HEAD(f);

    for (; KEY != KEY_TAIL; ++KEY, ++PARAM, ++ARG) {
        if (Is_Specialized(PARAM))
            continue;
        if (VAL_PARAM_CLASS(PARAM) != PARAM_CLASS_OUTPUT)
            continue;

        if (not Typecheck_Coerce_Argument(PARAM, ARG))
            fail (Error_Phase_Arg_Type(f, KEY, PARAM, stable_ARG));

        Meta_Quotify(Copy_Cell(PUSH(), ARG));
    }

    if (TOP_INDEX == base + 1) {  // no multi return values
        DROP();
        Meta_Unquotify_Undecayed(v);
    } else
        Init_Pack(v, Pop_Stack_Values(base));

    return v;
}


//
//  Action_Executor: C
//
Bounce Action_Executor(Frame(*) f)
{
    if (THROWING) {
        if (Get_Action_Executor_Flag(f, DISPATCHER_CATCHES))
            goto dispatch_phase;  // wants to see the throw

        if (Get_Frame_Flag(f, ABRUPT_FAILURE)) {
            assert(Get_Frame_Flag(f, NOTIFY_ON_ABRUPT_FAILURE));
            goto dispatch_phase;
        }

        goto handle_thrown_maybe_redo;
    }

    if (Not_Action_Executor_Flag(f, IN_DISPATCH)) {
        assert(Not_Action_Executor_Flag(f, DISPATCHER_CATCHES));
        assert(Not_Frame_Flag(f, NOTIFY_ON_ABRUPT_FAILURE));

        switch (STATE) {
          case ST_ACTION_INITIAL_ENTRY:
            STATE = ST_ACTION_FULFILLING_ARGS;
            goto fulfill;

          case ST_ACTION_FULFILLING_ENFIX_FROM_OUT:
            goto fulfill;

          case ST_ACTION_FULFILLING_ARGS:
            if (VAL_PARAM_CLASS(PARAM) != PARAM_CLASS_META) {
                if (Is_Barrier(ARG)) {
                    STATE = ST_ACTION_BARRIER_HIT;
                    Init_Word_Isotope(ARG, Canon(END));
                }
                else
                    Decay_If_Unstable(ARG);
            }
            goto continue_fulfilling;

          case ST_ACTION_TYPECHECKING:
            goto typecheck_then_dispatch;

          default:
            assert(false);
        }
    }

    if (Get_Action_Executor_Flag(f, DELEGATE_CONTROL)) {  // delegation done
        Clear_Action_Executor_Flag(f, DELEGATE_CONTROL);
        goto check_output;  // since it's done, return type should be checked
    }

    goto dispatch_phase;  // STATE byte belongs to dispatcher after fulfill

  fulfill: {  ////////////////////////////////////////////////////////////////

    assert(not IS_POINTER_TRASH_DEBUG(ORIGINAL));  // set by Begin_Action()

    assert(TOP_INDEX >= f->baseline.stack_base);  // paths push refinements

    assert(Not_Action_Executor_Flag(f, DOING_PICKUPS));

    for (; KEY != KEY_TAIL; ++KEY, ++ARG, ++PARAM) {

  //=//// CONTINUES (AT TOP SO GOTOS DO NOT CROSS INITIALIZATIONS /////////=//

        goto fulfill_loop_body;  // optimized out

      continue_fulfilling:
        assert(Is_Stable(ARG));  // implicitly asserts READABLE(ARG)

        if (Get_Action_Executor_Flag(f, DOING_PICKUPS)) {
            if (TOP_INDEX != f->baseline.stack_base)
                goto next_pickup;

            goto fulfill_and_any_pickups_done;
        }
        continue;

      skip_fulfilling_arg_for_now:  // the GC marks args up through ARG...
        assert(Is_Cell_Erased(ARG));
        continue;

  //=//// ACTUAL LOOP BODY ////////////////////////////////////////////////=//

      fulfill_loop_body:

  //=//// SPECIALIZED ARGUMENTS ////////////////////////////////////////////=//

        // Parameters that are specialized (which includes locals, that are
        // specialized to `~` isotopes) are completely hidden from the public
        // interface.  So they will never come from argument fulfillment.
        // Their value comes from the exemplar frame in the slot where typesets
        // would be if it was unspecialized.
        //
        if (Is_Specialized(PARAM)) {  // specialized includes local
            Copy_Cell(ARG, PARAM);
            goto continue_fulfilling;
        }

        assert(IS_PARAMETER(PARAM));

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
            Symbol(const*) param_symbol = KEY_SYMBOL(KEY);

            for (; ordered != lowest_ordered; --ordered) {
                assert(Is_Pushed_Refinement(ordered));

                if (VAL_WORD_SYMBOL(ordered) != param_symbol)
                    continue;

                REBLEN offset = ARG - cast(Atom(*), FRM_ARGS_HEAD(f));
                INIT_VAL_WORD_BINDING(ordered, f->varlist);
                INIT_VAL_WORD_INDEX(ordered, offset + 1);

                if (Is_Parameter_Unconstrained(PARAM)) {
                    //
                    // There's no argument, so we won't need to come back
                    // for this one.  But we did need to set its index
                    // so we knew it was valid (errors later if not set).
                    //
                    Init_Blackhole(ARG);  // # means refinement used
                    goto continue_fulfilling;
                }

                goto skip_fulfilling_arg_for_now;
            }
        }

  //=//// A /REFINEMENT ARG ///////////////////////////////////////////////=//

        if (GET_PARAM_FLAG(PARAM, REFINEMENT)) {
            assert(Not_Action_Executor_Flag(f, DOING_PICKUPS));  // jump lower
            Finalize_None(ARG);  // may be filled by a pickup
            goto continue_fulfilling;
        }

  //=//// ARGUMENT FULFILLMENT ////////////////////////////////////////////=//

      fulfill_arg: ;  // semicolon needed--next statement is declaration

        enum Reb_Param_Class pclass = VAL_PARAM_CLASS(PARAM);

  //=//// SKIP OVER RETURN /////////////////////////////////////////////////=//

        // The return function is filled in by the dispatchers that provide it.

        if (pclass == PARAM_CLASS_RETURN or pclass == PARAM_CLASS_OUTPUT) {
            assert(Not_Action_Executor_Flag(f, DOING_PICKUPS));
            Finalize_None(ARG);
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
    // 2. Something like `lib.help left-lit` is allowed to work, but if it was
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
    // 6. SOFT permits f->out to not carry the UNEVALUATED flag--enfixed
    //    operations which have evaluations on their left are treated as if
    //    they were in a GROUP!.  This is important to `1 + 2 ->- lib.* 3`
    //    being 9, while also allowing `1 + x: ->- lib.default [...]` to work.
    //
    // 7. MEDIUM escapability means that it only allows the escape of one unit.
    //    Thus when reaching this point, it must carry the UENEVALUATED FLAG.

        if (STATE == ST_ACTION_BARRIER_HIT) {
            Init_Word_Isotope(ARG, Canon(END));
            goto continue_fulfilling;
        }

        if (STATE == ST_ACTION_FULFILLING_ENFIX_FROM_OUT) {
            STATE = ST_ACTION_FULFILLING_ARGS;

            if (Is_Fresh(OUT)) {  // "nothing" to left, but see [1]
                if (Get_Action_Executor_Flag(f, DIDNT_LEFT_QUOTE_TUPLE))
                    fail (Error_Literal_Left_Tuple_Raw());  // see [2]

                if (GET_PARAM_FLAG(PARAM, VARIADIC)) {  // empty is ok, see [3]
                    Init_Varargs_Untyped_Enfix(ARG, nullptr);
                    goto continue_fulfilling;
                }

                if (NOT_PARAM_FLAG(PARAM, ENDABLE))
                    fail (Error_No_Arg(f->label, KEY_SYMBOL(KEY)));

                Init_Nulled(ARG);
                goto continue_fulfilling;
            }

            if (GET_PARAM_FLAG(PARAM, VARIADIC)) {  // non-empty is ok, see [4]
                assert(not Is_None(OUT));
                Decay_If_Unstable(OUT);  // !!! ^META variadics?
                Init_Varargs_Untyped_Enfix(ARG, stable_OUT);
                FRESHEN(OUT);
            }
            else switch (pclass) {
              case PARAM_CLASS_NORMAL:
                Decay_If_Unstable(OUT);
                Move_Cell(ARG, OUT);
                break;

              case PARAM_CLASS_META: {
                Move_Cell(ARG, OUT);
                Meta_Quotify(ARG);
                break; }

              case PARAM_CLASS_HARD:  // PARAM_FLAG_SKIPPABLE in pre-lookback
                if (Not_Cell_Flag(OUT, UNEVALUATED))  // `x: 10 | x >- the`
                    fail (Error_Evaluative_Quote_Raw());  // see [5]

                Move_Cell(ARG, OUT);
                assert(Get_Cell_Flag(ARG, UNEVALUATED));  // move preserves
                break;

              case PARAM_CLASS_SOFT:  // can carry UNEVALUATED, see [6]
                goto escapable;

              case PARAM_CLASS_MEDIUM:  // must carry UNEVALUATED, see [7]
                assert(Get_Cell_Flag(OUT, UNEVALUATED));
                goto escapable;

              escapable:
                if (ANY_ESCAPABLE_GET(OUT)) {
                    if (Eval_Value_Throws(ARG, OUT, SPECIFIED))
                        goto handle_thrown_maybe_redo;
                    FRESHEN(OUT);
                }
                else {
                    Move_Cell(ARG, OUT);
                    Set_Cell_Flag(ARG, UNEVALUATED);
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
            if (Get_Action_Executor_Flag(f, RUNNING_ENFIX)) {
                assert(Not_Feed_Flag(f->feed, NO_LOOKAHEAD));
                if (
                    Not_Action_Flag(FRM_PHASE(f), POSTPONES_ENTIRELY)
                    and
                    Not_Action_Flag(FRM_PHASE(f), DEFERS_LOOKBACK)
                ){
                    Set_Feed_Flag(f->feed, NO_LOOKAHEAD);
                }
            }

            assert(Is_Fresh(OUT));  // output should have been "used up"
            goto continue_fulfilling;
        }

  //=//// NON-ENFIX VARIADIC ARG (doesn't consume anything *yet*) /////////=//

        // Evaluation argument "hook" parameters (marked in MAKE ACTION!
        // by a `[[]]` in the spec, and in FUNC by `<variadic>`).  They point
        // back to this call through a reified FRAME!, and are able to
        // consume additional arguments during the function run.
        //
        if (GET_PARAM_FLAG(PARAM, VARIADIC)) {
            Init_Varargs_Untyped_Normal(ARG, f);
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
        if (Not_Action_Executor_Flag(f, RUNNING_ENFIX))
            Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);

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
        if (Get_Feed_Flag(f->feed, DEFERRING_ENFIX))
            fail (Error_Ambiguous_Infix_Raw());

  //=//// ERROR ON END MARKER, BAR! IF APPLICABLE /////////////////////////=//

        if (Is_Frame_At_End(f)) {
            Init_Word_Isotope(ARG, Canon(END));
            goto continue_fulfilling;
        }

        switch (pclass) {

  //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes 1 EVALUATE's worth) /////=//

          case PARAM_CLASS_OUTPUT:  // e.g. evaluate/next [1 + 2] 'var
            goto output_from_feed;

        output_from_feed:
          case PARAM_CLASS_NORMAL:
          case PARAM_CLASS_META: {
            if (Is_Frame_At_End(f)) {
                Init_Word_Isotope(ARG, Canon(END));
                goto continue_fulfilling;
            }

            Flags flags = EVAL_EXECUTOR_FLAG_FULFILLING_ARG;
            if (pclass == PARAM_CLASS_META) {
                flags |= FRAME_FLAG_META_RESULT;
            }

            if (Did_Init_Inert_Optimize_Complete(ARG, f->feed, &flags))
                break;  // no frame needed

            Frame(*) subframe = Make_Frame(f->feed, flags);
            Push_Frame(ARG, subframe);

            return CATCH_CONTINUE_SUBFRAME(subframe); }

  //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG ///////////////////////////////=//

          case PARAM_CLASS_HARD:
            //
            // !!! Need to think about how cases like `source ||` or `help ||`
            // are supposed to act.  They set the "barrier hit" and then we
            // get here...if we don't clear the flag, then the presence of
            // a non-void causes a later assert.  Review.
            //
            if (NOT_PARAM_FLAG(PARAM, SKIPPABLE))
                Literal_Next_In_Frame(ARG, f);  // CELL_FLAG_UNEVALUATED
            else {
                Derelativize(SPARE, f_next, f_specifier);
                if (not TYPE_CHECK(PARAM, SPARE)) {
                    assert(GET_PARAM_FLAG(PARAM, ENDABLE));
                    Init_Nulled(ARG);  // not actually an ~end~ (?)
                    goto continue_fulfilling;
                }
                Literal_Next_In_Frame(ARG, f);
                Set_Cell_Flag(ARG, UNEVALUATED);
            }

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
            Literal_Next_In_Frame(ARG, f);  // CELL_FLAG_UNEVALUATED

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
                (pclass == PARAM_CLASS_SOFT and Get_Action_Flag(
                    VAL_ACTION(unwrap(f->feed->gotten)),  // ensured
                    QUOTES_FIRST
                ))
            ){
                // We need to defer and let the right hand quote that is
                // quoting leftward win.  We use ST_EVALUATOR_LOOKING_AHEAD
                // to jump into a subframe where subframe->out is the ARG,
                // and it knows to get the arg from there.

                Flags flags =
                    FLAG_STATE_BYTE(ST_EVALUATOR_LOOKING_AHEAD)  // no FRESHEN()
                    | EVAL_EXECUTOR_FLAG_FULFILLING_ARG
                    | EVAL_EXECUTOR_FLAG_INERT_OPTIMIZATION;

                Frame(*) subframe = Make_Frame(f->feed, flags);
                Push_Frame(ARG, subframe);
                return CATCH_CONTINUE_SUBFRAME(subframe);
            }
            else if (ANY_ESCAPABLE_GET(ARG)) {
                //
                // We did not defer the quoted argument.  If the argument
                // is something like a GET-GROUP!, GET-WORD!, or GET-PATH!...
                // it has to be evaluated.
                //
                Move_Cell(SPARE, ARG);
                if (Get_Var_Core_Throws(ARG, GROUPS_OK, SPARE, SPECIFIED))
                    goto handle_thrown_maybe_redo;
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
        //     30 = (10 + 20 maybe do [])
        //
        // Is breaking this.  Review when there is time, and put the assert
        // back if it makes sense.
        //
        /* assert(Not_Feed_Flag(f->feed, NO_LOOKAHEAD)); */
        Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);

        goto continue_fulfilling;
    }

  #if DEBUG_POISON_SERIES_TAILS
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

        if (not IS_WORD_BOUND(TOP)) {  // the loop didn't index it
            Refinify_Pushed_Refinement(TOP);
            fail (Error_Bad_Parameter_Raw(TOP));  // so duplicate or junk
        }

        // FRM_ARGS_HEAD offsets are 0-based, while index is 1-based.
        // But +1 is okay, because we want the slots after the refinement.
        //
        REBINT offset =
            VAL_WORD_INDEX(TOP) - (ARG - cast(Atom(*), FRM_ARGS_HEAD(f))) - 1;
        KEY += offset;
        ARG += offset;
        PARAM += offset;

        assert(VAL_WORD_SYMBOL(TOP) == KEY_SYMBOL(KEY));
        DROP();

        if (Is_Parameter_Unconstrained(PARAM)) {  // no callsite arg, just drop
            if (TOP_INDEX != BASELINE->stack_base)
                goto next_pickup;

            goto fulfill_and_any_pickups_done;
        }

        if (not Is_Cell_Erased(ARG)) {
            assert(Is_Nulled(ARG));
            FRESHEN(ARG);
        }

        Set_Action_Executor_Flag(f, DOING_PICKUPS);
        goto fulfill_arg;
    }

} fulfill_and_any_pickups_done: {  ///////////////////////////////////////////

    if (Get_Action_Executor_Flag(f, FULFILL_ONLY)) {  // no typecheck
        Finalize_None(OUT);  // didn't touch out, should be fresh
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
  // custom type checks inside the body of a function on equal footing with
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
  // 2. None (isotopic voids) are the default values from MAKE FRAME!.
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

    FRESHEN(OUT);

    KEY = ACT_KEYS(&KEY_TAIL, FRM_PHASE(f));
    ARG = FRM_ARGS_HEAD(f);
    PARAM = ACT_PARAMS_HEAD(FRM_PHASE(f));

    for (; KEY != KEY_TAIL; ++KEY, ++PARAM, ++ARG) {
        assert(Is_Stable(ARG));  // implicitly asserts READABLE(ARG)

        if (Is_Specialized(PARAM))  // checked when specialized, see [1]
            continue;

        if (
            VAL_PARAM_CLASS(PARAM) == PARAM_CLASS_RETURN
            or VAL_PARAM_CLASS(PARAM) == PARAM_CLASS_OUTPUT
        ){
            assert(Is_None(ARG));
            continue;  // typeset is its legal return types, wants to be unset
        }

        if (Is_None(ARG)) {  // e.g. (~) isotope, unspecialized, see [2]
            if (GET_PARAM_FLAG(PARAM, REFINEMENT)) {
                Init_Nulled(ARG);
                continue;
            }
            if (GET_PARAM_FLAG(PARAM, SKIPPABLE)) {
                Init_Nulled(ARG);
                continue;
            }
        }
        else if (Is_Void(ARG)) {
            if (GET_PARAM_FLAG(PARAM, NOOP_IF_VOID)) {  // e.g. <maybe> param
                Set_Action_Executor_Flag(f, TYPECHECK_ONLY);
                Init_Nulled(OUT);
                continue;
            }
            if (GET_PARAM_FLAG(PARAM, REFINEMENT)) {
                Init_Nulled(ARG);
                continue;
            }
        }
        else if (Is_Word_Isotope_With_Id(ARG, SYM_END)) {
            if (NOT_PARAM_FLAG(PARAM, ENDABLE))
                fail (Error_No_Arg(f->label, KEY_SYMBOL(KEY)));
            Init_Nulled(ARG);  // more convenient, use ^META for nuance
            continue;
        }

        if (GET_PARAM_FLAG(PARAM, VARIADIC)) {  // can't check now, see [3]
            if (not IS_VARARGS(ARG))  // argument itself is always VARARGS!
                fail (Error_Not_Varargs(f, KEY, PARAM, stable_ARG));

            INIT_VAL_VARARGS_PHASE(ARG, FRM_PHASE(f));

            bool enfix = false;  // !!! how does enfix matter?
            VAL_VARARGS_SIGNED_PARAM_INDEX(ARG) =  // store offset, see [4]
                enfix
                    ? -(ARG - cast(Atom(*), FRM_ARGS_HEAD(f)) + 1)
                    : ARG - cast(Atom(*), FRM_ARGS_HEAD(f)) + 1;

            assert(VAL_VARARGS_SIGNED_PARAM_INDEX(ARG) != 0);
            continue;
        }

        if (not Typecheck_Coerce_Argument(PARAM, ARG))
            fail (Error_Phase_Arg_Type(f, KEY, PARAM, stable_ARG));
    }

  // Action arguments now gathered, begin dispatching

} dispatch: {  ///////////////////////////////////////////////////////////////

  // 1. Here we free the union for use by the dispatcher...though currently
  //    one slot is stolen for the base stack address the dispatcher should
  //    consider (variables can be stored to write back to for multi-return).
  //    It's also needed to keep f->original.  Think about how to improve.
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

    assert(Not_Action_Executor_Flag(f, IN_DISPATCH));
    Set_Action_Executor_Flag(f, IN_DISPATCH);

    Action(*) save_original = f->u.action.original;
    TRASH_IF_DEBUG(f->u);  // freed for dispatcher use...
    f->u.action.original = save_original;  // ...er, mostly.  see [1]
    f->u.action.dispatcher_base = TOP_INDEX;

    if (STATE == ST_ACTION_FULFILLING_ENFIX_FROM_OUT) {  // can happen, see [2]
        if (Get_Action_Executor_Flag(f, DIDNT_LEFT_QUOTE_TUPLE))  // see notes
            fail (Error_Literal_Left_Tuple_Raw());

        assert(Get_Action_Executor_Flag(f, RUNNING_ENFIX));
        FRESHEN(OUT);
    }

    assert(Get_Action_Executor_Flag(f, IN_DISPATCH));
    assert(
        Is_Frame_At_End(f)
        or FRM_IS_VARIADIC(f)
        or IS_VALUE_IN_ARRAY_DEBUG(FEED_ARRAY(f->feed), f_next)
    );

    if (Get_Action_Executor_Flag(f, TYPECHECK_ONLY)) {  // <maybe>
        assert(Is_Nulled(OUT));
        goto skip_output_check;
    }

    FRESHEN(SPARE);  // tiny cost (one bit clear) but worth it, see [3]
    STATE = STATE_0;  // reset to zero for each phase

    f_next_gotten = nullptr;  // arbitrary code changes fetched variables

} dispatch_phase: {  /////////////////////////////////////////////////////////

  // 1. Each time a continuation happens, the dispatcher gets a new chance to
  //    decide if it wants to catch throws.
  //
  //    !!! Should this be done in the continuations themselves, so that an
  //    action that doesn't use any continuations won't pay for this clearing?
  //
  // 2. Native code trusts that type checking has ensured it won't get bits
  //    in its argument slots that the C won't recognize.  Usermode code that
  //    gets its hands on a native's FRAME! (e.g. for debug viewing) can't be
  //    allowed to change the frame values to other bit patterns out from
  //    under the C or it could result in a crash.
  //
  //    !!! Once the IS_NATIVE flag was the same as the HOLD info bit, but
  //    that trick got shaken up with flag reordering.  Review.
  //
  // 3. The stale bit is set on the output before we run the dispatcher.  We
  //    check to make sure it's not stale at the end--because that could often
  //    mean the function forgot to write the output cell on some code path.
  //    (To intentionally not write anything and "vaporize", use `return VOID`
  //    which gives back a distinct `Bounce` signal to know it's purposeful.)

    assert(Not_Action_Executor_Flag(FRAME, DELEGATE_CONTROL));  // delegated!
    Clear_Action_Executor_Flag(FRAME, DISPATCHER_CATCHES);  // see [1]

    Action(*) phase = FRM_PHASE(f);

    /*STATIC_ASSERT(DETAILS_FLAG_IS_NATIVE == SERIES_INFO_HOLD);*/
    if (Is_Action_Native(phase))
        SER_INFO(f->varlist) |= SERIES_INFO_HOLD;  // prevents crashes, see [2]

    Dispatcher* dispatcher = ACT_DISPATCHER(phase);

    Bounce b = (*dispatcher)(f);

    if (b == OUT) {  // common case, made fastest
        assert(not Is_Fresh(OUT));  // must write output, even if just void
        Clear_Cell_Flag(OUT, UNEVALUATED);
    }
    else if (b == nullptr) {  // API and internal code can both return `nullptr`
        Init_Nulled(OUT);
    }
    else if (Is_Bounce_An_Atom(b)) {
        Atom(*) r = Atom_From_Bounce(b);
        assert(Is_Api_Value(r));
        Copy_Cell(OUT, r);
        Release_Api_Value_If_Unmanaged(r);
    }
    else switch (VAL_RETURN_SIGNAL(b)) {  // it's a "pseudotype" instruction

      case C_CONTINUATION:
        return BOUNCE_CONTINUE;  // Note: may not have pushed a new frame...

      case C_DELEGATION:
        Set_Action_Executor_Flag(FRAME, DELEGATE_CONTROL);
        STATE = DELEGATE_255;  // the trampoline does this when delegating
        return BOUNCE_CONTINUE;

      case C_SUSPEND:
        return BOUNCE_SUSPEND;

      case C_THROWN:
        goto handle_thrown_maybe_redo;

      case C_REDO_UNCHECKED:
        Clear_Action_Executor_Flag(f, IN_DISPATCH);
        goto dispatch;  // Note: dispatcher may have changed frame's PHASE

      case C_REDO_CHECKED:
        Clear_Action_Executor_Flag(f, IN_DISPATCH);
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
    Do_After_Action_Checks_Debug(f);
  #endif

    if (not Is_Raised(OUT))  // !!! Should there be an R_FAIL ?
        assert(f->u.action.dispatcher_base == TOP_INDEX);

} skip_output_check: {  //////////////////////////////////////////////////////

  // This is where things get jumped to if you pass a <maybe> argument a
  // VOID and it wants to jump past all the processing and return, or if
  // a frame just wants argument fulfillment and no execution.
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
  //    the same frame, so can't clear in Drop_Action(), e.g. due to:
  //
  //      left-the: enfix :the
  //      o: make object! [f: does [1]]
  //      o.f left-the  ; want error suggesting -> here, need flag for that

    if (STATE == ST_ACTION_FULFILLING_ENFIX_FROM_OUT)  // see [1]
        fail ("Left lookback toward thing that took no args, look at later");

    Clear_Action_Executor_Flag(f, DIDNT_LEFT_QUOTE_TUPLE);  // see [2]

    Drop_Action(f);  // must fail before Drop_Action()

    return OUT;  // not thrown

} handle_thrown_maybe_redo: {  ///////////////////////////////////////////////

  // Until stackless is universal, an action dispatcher may make a stackful
  // call to something that issues a REDO.  So we can't handle REDO at the
  // top of this executor where we test for THROWING, the way we might if
  // we could always expect continuations as the sources of throws.
  //
  // 1. REDO is the mechanism for doing "tail calls", and it is a generic
  //    feature offered on ACTION! frames regardless of what executor
  //    they use.  It starts the function phase again from its top, and
  //    reuses the frame already allocated.
  //
  // 2. Since dispatchers run arbitrary code to pick how (and if) they want
  //    to change the phase on each redo, we have no easy way to tell if a
  //    phase is "earlier" or "later".
  //
  // 3. We are reusing the frame and may be jumping to an "earlier phase" of
  //    a composite function, or even to a "not-even-earlier-just-compatible"
  //    phase of another function (sibling tail call).  Type checking is
  //    necessary, as is zeroing out any locals...but if we're jumping to any
  //    higher or different phase we need to reset the specialization
  //    values as well.
  //
  //    !!! Consider folding this pass into the typechecking loop itself.
  //
  // 4. As a convenience, we automatically drop evaluator frames above on the
  //    stack.  This doesn't necessarily generalize well, but if we didn't do
  //    it then anything that pushed a subframe to do an evaluator walk (like
  //    a CASE or ANY) would need to explicitly catch evaluator throws...which
  //    seems like make-work.

    const REBVAL *label = VAL_THROWN_LABEL(frame_);
    if (IS_ACTION(label)) {
        if (
            VAL_ACTION(label) == VAL_ACTION(Lib(REDO))  // REDO, see [1]
            and VAL_ACTION_BINDING(label) == CTX(f->varlist)
        ){
            CATCH_THROWN(OUT, frame_);
            assert(IS_FRAME(OUT));

            Action(*) redo_phase = VAL_FRAME_PHASE(OUT);  // earlier?  see [2]
            KEY = ACT_KEYS(&KEY_TAIL, redo_phase);
            PARAM = ACT_PARAMS_HEAD(redo_phase);
            ARG = FRM_ARGS_HEAD(f);
            for (; KEY != KEY_TAIL; ++KEY, ++ARG, ++PARAM) {
                if (Is_Specialized(PARAM))
                    Copy_Cell(ARG, PARAM);  // must reset, see [3]
                else if (VAL_PARAM_CLASS(PARAM) == PARAM_CLASS_RETURN)
                    Init_None(ARG);  // dispatcher expects unset
            }

            INIT_FRM_PHASE(f, redo_phase);
            INIT_FRM_BINDING(f, VAL_FRAME_BINDING(OUT));
            STATE = ST_ACTION_TYPECHECKING;
            Clear_Action_Executor_Flag(f, DISPATCHER_CATCHES);  // else asserts
            Clear_Frame_Flag(f, NOTIFY_ON_ABRUPT_FAILURE);
            Clear_Action_Executor_Flag(f, IN_DISPATCH);
            goto typecheck_then_dispatch;
        }
    }

    while (TOP_FRAME != f)  // convenient for natives pushing SUBFRAME, see [4]
        Drop_Frame(TOP_FRAME);  // !!! Should all inert frames be aborted?

    Drop_Action(f);
    Drop_Data_Stack_To(f->baseline.stack_base);  // unprocessed refinements

    return BOUNCE_THROWN;
}}


//
//  Push_Action: C
//
// Allocate the series of REBVALs inspected by a function when executed (the
// values behind ARG(name), REF(name), D_ARG(3),  etc.)
//
// This only allocates space for the arguments, it does not initialize.
// Eval_Core initializes as it goes, and updates KEY so the GC knows how
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
    Frame(*) f,
    Action(*) act,
    Context(*) binding  // actions may only be bound to contexts ATM
){
    f->executor = &Action_Executor;

    assert(Not_Action_Executor_Flag(f, FULFILL_ONLY));
    assert(Not_Action_Executor_Flag(f, RUNNING_ENFIX));

    REBLEN num_args = ACT_NUM_PARAMS(act);  // includes specialized + locals

    assert(f->varlist == nullptr);

    Stub* s = Prep_Stub(
        Alloc_Stub(),  // not preallocated
        SERIES_MASK_VARLIST
            | SERIES_FLAG_FIXED_SIZE // FRAME!s don't expand ATM
    );
    SER_INFO(s) = SERIES_INFO_MASK_NONE;
    INIT_BONUS_KEYSOURCE(ARR(s), f);  // maps varlist back to f
    mutable_MISC(VarlistAdjunct, s) = nullptr;
    mutable_LINK(Patches, s) = nullptr;
    f->varlist = ARR(s);

    if (not Did_Series_Data_Alloc(s, num_args + 1 + 1)) {  // +rootvar, +end
        SET_SERIES_FLAG(s, INACCESSIBLE);
        GC_Kill_Series(s);  // ^-- needs non-null data unless INACCESSIBLE
        f->varlist = nullptr;
        fail (Error_No_Memory(sizeof(REBVAL) * (num_args + 1 + 1)));
    }

    f->rootvar = cast(REBVAL*, s->content.dynamic.data);
    USED(Erase_Cell(f->rootvar));  // want the tracking info, overwriting header
    f->rootvar->header.bits =
        NODE_FLAG_NODE
            | NODE_FLAG_CELL
            | CELL_FLAG_PROTECTED  // payload/binding tweaked, but not by user
            | CELL_MASK_FRAME
            | FLAG_QUOTE_BYTE(UNQUOTED_1);
    INIT_VAL_CONTEXT_VARLIST(f->rootvar, f->varlist);

    INIT_VAL_FRAME_PHASE(f->rootvar, act);  // FRM_PHASE()
    INIT_VAL_FRAME_BINDING(f->rootvar, binding);  // FRM_BINDING()

    s->content.dynamic.used = num_args + 1;

    // !!! Historically the idea was to prep during the walk of the frame,
    // to avoid doing two walks.  The current thinking is to move toward a
    // notion of being able to just memset() to 0 or calloc().  The debug
    // build still wants to initialize the cells with file/line info though.
    //
    Cell(*) tail = ARR_TAIL(f->varlist);
    Cell(*) prep = f->rootvar + 1;
    for (; prep < tail; ++prep)
        USED(Erase_Cell(prep));

  #if DEBUG_POISON_EXCESS_CAPACITY  // poison cells past usable range
    for (; prep < f->rootvar + s->content.dynamic.rest; ++prep)
        Poison_Cell(prep);  // unreadable + unwritable
  #endif

  #if DEBUG_POISON_SERIES_TAILS  // redundant if excess capacity poisoned
    Poison_Cell(ARR_TAIL(f->varlist));
  #endif

    // Each layer of specialization of a function can only add specializations
    // of arguments which have not been specialized already.  For efficiency,
    // the act of specialization merges all the underlying layers of
    // specialization together.  This means only the outermost specialization
    // is needed to fill the specialized slots contributed by later phases.
    //
    Array(*) partials = try_unwrap(ACT_PARTIALS(act));
    if (partials) {
        Cell(const*) word_tail = ARR_TAIL(partials);
        const REBVAL *word = SPECIFIC(ARR_HEAD(partials));
        for (; word != word_tail; ++word)
            Copy_Cell(PUSH(), word);
    }

    assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
    assert(NOT_SERIES_FLAG(f->varlist, INACCESSIBLE));
}


//
//  Begin_Action_Core: C
//
void Begin_Action_Core(
    Frame(*) f,
    option(Symbol(const*)) label,
    bool enfix
){
    // These assertions were blocking code sharing with SET-BLOCK! mechanics.
    // Review where the right place to put them is.
    //
    /*assert(Not_Action_Executor_Flag(f, RUNNING_ENFIX));
    assert(Not_Feed_Flag(f->feed, DEFERRING_ENFIX));*/

    assert(Not_Subclass_Flag(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED));
    Set_Subclass_Flag(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED);

    ORIGINAL = FRM_PHASE(f);

    KEY = ACT_KEYS(&KEY_TAIL, ORIGINAL);
    PARAM = ACT_PARAMS_HEAD(ORIGINAL);
    ARG = f->rootvar + 1;

    assert(IS_POINTER_TRASH_DEBUG(f->label));  // ACTION! makes valid
    assert(not label or IS_SYMBOL(unwrap(label)));
    f->label = label;
  #if DEBUG_FRAME_LABELS  // helpful for looking in the debugger
    f->label_utf8 = cast(const char*, Frame_Label_Or_Anonymous_UTF8(f));
  #endif

    if (enfix) {
        //
        // While ST_ACTION_FULFILLING_ARG_FROM_OUT is set only during the first
        // argument of an enfix call, ACTION_EXECUTOR_FLAG_RUNNING_ENFIX is
        // set for the whole duration.
        //
        Set_Action_Executor_Flag(f, RUNNING_ENFIX);

        // All the enfix call sites cleared this flag on the feed, so it was
        // moved into the Begin_Enfix_Action() case.  Note this has to be done
        // *after* the existing flag state has been captured for invisibles.
        //
        Clear_Feed_Flag(f->feed, NO_LOOKAHEAD);

        FRM_STATE_BYTE(f) = ST_ACTION_FULFILLING_ENFIX_FROM_OUT;
    }
}


//
//  Drop_Action: C
//
void Drop_Action(Frame(*) f) {
    assert(not f->label or IS_SYMBOL(unwrap(f->label)));

    Clear_Action_Executor_Flag(f, RUNNING_ENFIX);
    Clear_Action_Executor_Flag(f, FULFILL_ONLY);

    assert(
        GET_SERIES_FLAG(f->varlist, INACCESSIBLE)
        or BONUS(KeySource, f->varlist) == f
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
            // This node could be reused vs. calling Alloc_Pooled() on the next
            // action invocation...but easier for the moment to let it go.
            //
            Free_Pooled(STUB_POOL, f->varlist);
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
                ORIGINAL  // degrade keysource from f
            )
        );
        assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
        INIT_BONUS_KEYSOURCE(f->varlist, f);
      #endif

        INIT_BONUS_KEYSOURCE(f->varlist, ACT_KEYLIST(ORIGINAL));
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
        Clear_Subclass_Flag(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED);

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

        Cell(*) rootvar = ARR_HEAD(f->varlist);
        assert(CTX_VARLIST(VAL_CONTEXT(rootvar)) == f->varlist);
        INIT_VAL_FRAME_PHASE_OR_LABEL(rootvar, nullptr);  // can't trash ptr
        TRASH_POINTER_IF_DEBUG(mutable_BINDING(rootvar));
    }
  #endif

    TRASH_POINTER_IF_DEBUG(ORIGINAL); // action is no longer running
    f->executor = nullptr;  // so GC won't see this frame as Action GC

    TRASH_POINTER_IF_DEBUG(f->label);
  #if DEBUG_FRAME_LABELS
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
  #endif
}
