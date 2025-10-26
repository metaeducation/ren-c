//
//  file: %c-action.c
//  summary: "Central Action Executor"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// * Action_Executor() is LONG.  It's actually a somewhat purposeful choice.
//   Breaking it into functions would add overhead (in the RUNTIME_CHECKS
//   build, if not also in the NO_RUNTIME_CHECKS build), and also prevent
//   interesting tricks and optimizations.
//
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
#undef Is_Level_Fulfilling_Or_Typechecking


#define L_next              cast(const Cell*, L->feed->p)

#define L_next_gotten_raw  (&L->feed->gotten)
#define L_next_gotten      (not Is_Gotten_Invalid(L_next_gotten_raw))

#define L_binding           Level_Binding(L)

#undef ARG                       // undefine the ARG(X) macro that natives use
#define ARG L->u.action.arg      // ...aredefine as currently fulfilling arg

#undef PARAM
#define PARAM L->u.action.param

#define KEY L->u.action.key
#define KEY_TAIL L->u.action.key_tail

#define ORIGINAL L->u.action.original

#define level_ L  // for OUT, SPARE, STATE macros


//
//  Irreducible_Bounce: C
//
// This tries to simplify a bounce to get it to be just an Atom content in
// the OUT cell, if possible.  Not all bounces can be simplified, but when
// they can be this can save when delegating code, on needing to call a cycle
// of trampoline.
//
Option(Bounce) Irreducible_Bounce(Level* level_, Bounce b) {
    if (b == OUT) {  // common case, made fastest
        assert(Is_Cell_Readable(OUT));  // must write output, even if just void
        return nullptr;
    }

    if (b == nullptr) {  // API and internal code can both return `nullptr`
        if (not g_failure) {
            Init_Nulled(OUT);
            return nullptr;
        }

        // if g_failure is set, nullptr came from `return fail()` not a
        // `return nullptr` indicating null.  See NEEDFUL_RESULT_0.

        assert(not Is_Throwing(L));

        while (TOP_LEVEL != L) {  // convenience
            Rollback_Level(TOP_LEVEL);
            Drop_Level(TOP_LEVEL);
            Erase_Cell(TOP_LEVEL->out);
        }
        Rollback_Level(L);  // not throwing, no trampoline rollback TOP_LEVEL

        Init_Warning(L->out, g_failure);
        g_failure = nullptr;  // have to do before Force_Location_Of_Error()
        Failify(L->out);  // forces location of error to level

      #if DEBUG_EXTANT_STACK_POINTERS  // want to use stack in location setting
        Count save_extant = g_ds.num_refs_extant;
        g_ds.num_refs_extant = 0;
      #endif

      #if DEBUG_EXTANT_STACK_POINTERS
        assert(g_ds.num_refs_extant == 0);
        g_ds.num_refs_extant = save_extant;
      #endif

        return nullptr;
    }

    if (Is_Bounce_Wild(b))
        return b;  // can't simplify, may be a panic, continuation, etc.

    if (b == BOUNCE_OKAY) {  // BOUNCE_OKAY is just LIB(OKAY) (fixed pointer)
        Init_Okay(OUT);  // ...optimization doesn't write OUT, but we do here
        return nullptr;  // essential to typechecker intrinsic optimization...
    }

  copy_api_cell_to_out_and_release_it: {

    // 1. As of yet, no API functions have been exported which return an
    //    unstable Atom directly.  If it did, it would have to return it as
    //    a RebolBounce* not a Value*.  There's no particular reason why
    //    we couldn't offer a `rebPack()` function that did give back a
    //    pack, solely intended to use in the form `return rebPack(...)`,
    //    but it hasn't yet happened...because even if it returned a Bounce
    //    it would backed by an API cell form holding an unstable value,
    //    which is currently not legal.  Some rules and tightening would
    //    be needed, so for now we do `rebContinue("pack [...]")`
    //
    // 2. If a native does `return rebValue("lambda [x] [x]")` that should
    //    count as an "unsurprising" function result.  Preserve the flag.

    if (Is_Bounce_An_Atom(b)) {  // Cell pointer (must be Api cell)
        Atom* atom = Atom_From_Bounce(b);
        assert(Is_Atom_Api_Value(atom));
        Assert_Cell_Stable(atom);  // API cells always stable, for now [1]
        Copy_Cell_Core(OUT, atom, CELL_MASK_THROW);  // keep unsurprising [2]
        Release_Api_Value_If_Unmanaged(Known_Stable(atom));
        return nullptr;
    }

} turn_utf8_into_delegated_code: {

    // While it might seem more obvious for `return "some string";` to give
    // back a text string, it's actually far more useful to run UTF-8 returns
    // as delegated code:
    //
    // https://forum.rebol.info/t/returning-a-string-from-a-native/2357

    assert(Detect_Rebol_Pointer(b) == DETECTED_AS_UTF8);

    const char* cp = cast(const char*, b);
    if (cp[0] == '~' and cp[1] == '\0') {
        Init_Tripwire(L->out);
        return nullptr;  // make return "~" fast!
    }

    assert(Link_Inherit_Bind(L->varlist) != nullptr);
    assert(Is_Base_Managed(L->varlist));
    rebDelegateCore(cast(RebolContext*, L->varlist), cp);
    return BOUNCE_DELEGATE;
}}


// When arguments are hard quoted or soft-quoted, they don't call into the
// evaluator to do it.  But they need to use the logic of the evaluator for
// noticing when to defer infix:
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
bool Lookahead_To_Sync_Infix_Defer_Flag(Feed* feed) {
    assert(Not_Feed_Flag(feed, DEFERRING_INFIX));
    assert(Is_Gotten_Invalid(&feed->gotten));

    Clear_Feed_Flag(feed, NO_LOOKAHEAD);

    if (Is_Feed_At_End(feed))
        return false;

    if (Type_Of_Unchecked(At_Feed(feed)) != TYPE_WORD)
        return false;

    Get_Word(
        &feed->gotten, At_Feed(feed), Feed_Binding(feed)
    ) except (Error* e) {
        Erase_Cell(&feed->gotten);  // could this be Trap_Get_Word() invariant?
        UNUSED(e);  // don't care (if we care, we'll hit it on next step)
        return false;
    }

    if (not Is_Action(&feed->gotten))
        return false;

    Option(InfixMode) infix_mode = Frame_Infix_Mode(&feed->gotten);
    if (not infix_mode)
        return false;

    if (infix_mode == INFIX_DEFER)
        Set_Feed_Flag(feed, DEFERRING_INFIX);
    return true;
}


//
//  Action_Executor: C
//
Bounce Action_Executor(Level* L)
{
    if (THROWING) {
        if (Get_Action_Executor_Flag(L, DISPATCHER_CATCHES)) {
            assert(LEVEL_STATE_BYTE(L) != STATE_0);  // need to update
            goto dispatch_phase;  // wants to see the throw
        }
        goto handle_thrown;
    }

    if (Not_Action_Executor_Flag(L, IN_DISPATCH)) {
        assert(Not_Action_Executor_Flag(L, DISPATCHER_CATCHES));

        switch (STATE) {
          case ST_ACTION_INITIAL_ENTRY:
            STATE = ST_ACTION_FULFILLING_ARGS;
            goto fulfill;

          case ST_ACTION_INITIAL_ENTRY_INFIX:
            STATE = ST_ACTION_FULFILLING_INFIX_FROM_OUT;
            goto fulfill;

          case ST_ACTION_FULFILLING_ARGS:
            goto continue_fulfilling;

          case ST_ACTION_TYPECHECKING:
            goto typecheck_then_dispatch;

          case ST_ACTION_FULFILLING_INFIX_FROM_OUT:  // no evals during this
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

  #if NEEDFUL_DOES_CORRUPTIONS
    assert(Stub_Flavor(ORIGINAL));  // set by Begin_Action(), shouldn't crash
  #endif

    assert(TOP_INDEX >= L->baseline.stack_base);  // paths push refinements

    assert(Not_Action_Executor_Flag(L, DOING_PICKUPS));

    for (; KEY != KEY_TAIL; ++KEY, ++ARG, ++PARAM) {

  //=//// CONTINUES (AT TOP SO GOTOS DO NOT CROSS INITIALIZATIONS /////////=//

        goto fulfill_loop_body;  // optimized out

      continue_fulfilling:

        if (Get_Action_Executor_Flag(L, DOING_PICKUPS)) {
            if (TOP_INDEX != L->baseline.stack_base)
                goto next_pickup;

            goto fulfill_and_any_pickups_done;
        }
        continue;

      skip_fulfilling_arg_for_now:
        assert(Not_Action_Executor_Flag(L, DOING_PICKUPS));
        assert(Is_Light_Null(ARG));  // couldn't leave erased, so make nulled
        continue;

  //=//// ACTUAL LOOP BODY ////////////////////////////////////////////////=//

      fulfill_loop_body:

      #if DEBUG_POISON_UNINITIALIZED_CELLS
        assert(Is_Cell_Poisoned(ARG));
      #endif

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
            Blit_Param_Drop_Mark(ARG, PARAM);
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
        //     foo: func [a :b [integer!] :c [integer!]] [...]
        //
        //     foo:b:c 10 20 30
        //     foo:c:b 10 20 30
        //
        // The first CHAIN! pushes :B to the top of stack, with :C below.
        // The second CHAIN! pushes :C to the top of stack, with :B below
        //
        // While historically Rebol paths for invoking functions could only
        // use refinements for optional parameters, Ren-C leverages the same
        // two-pass mechanism to implement the reordering of non-optional
        // parameters at the callsite.

        if (TOP_INDEX != STACK_BASE) {  // reorderings/refinements
            OnStack(Element*) ordered = TOP_ELEMENT;
            OnStack(Element*) lowest = Data_Stack_At(Element, STACK_BASE);
            const Symbol* param_symbol = Key_Symbol(KEY);

            for (; ordered != lowest; --ordered) {
                assert(Is_Pushed_Refinement(ordered));

                if (Word_Symbol(ordered) != param_symbol)
                    continue;

                possibly(  // need to use u_cast() due to this possibility
                    ARG == Level_Args_Head(L) and Is_Cell_Poisoned(ARG)
                );
                REBLEN offset = ARG - u_cast(Atom*, Level_Args_Head(L));
                Tweak_Word_Index(ordered, offset + 1);
                if (Is_Stub_Details(L->u.action.original))  // !!!
                    Tweak_Cell_Relative_Binding(
                        ordered,
                        cast(Details*, L->u.action.original)
                    );
                else
                    Tweak_Cell_Binding(
                        ordered,
                        cast(ParamList*, L->u.action.original)
                    );

                if (Is_Parameter_Unconstrained(PARAM)) {
                    //
                    // There's no argument, so we won't need to come back
                    // for this one.  But we did need to set its index
                    // so we knew it was valid (errors later if not set).
                    //
                    Blit_Okay_Typechecked(ARG);  // refinement used
                    goto continue_fulfilling;
                }

                Init_Nulled(Erase_Cell(ARG));  // can't bypass and leave erased
                goto skip_fulfilling_arg_for_now;
            }
        }

  //=//// A /REFINEMENT ARG ///////////////////////////////////////////////=//

        if (Get_Parameter_Flag(PARAM, REFINEMENT)) {
            assert(Not_Action_Executor_Flag(L, DOING_PICKUPS));  // jump lower
            Blit_Null_Typechecked(ARG);  // pickup can change
            goto continue_fulfilling;
        }

        Erase_Cell(ARG);

  //=//// ARGUMENT FULFILLMENT ////////////////////////////////////////////=//

      fulfill_arg: ;  // semicolon needed--next statement is declaration

        ParamClass pclass = Parameter_Class(PARAM);

  //=//// HANDLE IF NEXT ARG IS IN OUT SLOT (e.g. INFIX, CHAIN) ///////////=//

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
    // 3. If an infix function finds it has a variadic in its first slot,
    //    then nothing available on the left is o.k.  It means we have to put
    //    a VARARGS! in that argument slot which will react with TRUE to TAIL?,
    //    so feed it from the global empty array.
    //
    // 4. Infix functions with variadics on the left can also deal with a
    //    single value.  An unevaluated is stored into an array-form variadic,
    //    so the user can do 0 or 1 TAKEs of it.
    //
    //    !!! It be evaluated when they TAKE (it if it's an evaluative arg),
    //    but not if they don't.  Should failing to TAKE be seen as an warning?
    //    Failing to take first gives out-of-order evaluation.
    //
    // 5. The idea behind quoting not getting binding isn't that it *removes*
    //    binding, but that it doesn't add it.  But the mechanics aren't
    //    sorted out to communicate "don't add binding" here yet.  Give a
    //    first-cut approximation by unbinding.

        if (STATE == ST_ACTION_BARRIER_HIT) {
            Init_Unset_Due_To_End(ARG);
            goto continue_fulfilling;
        }

        if (STATE == ST_ACTION_FULFILLING_INFIX_FROM_OUT) {
            STATE = ST_ACTION_FULFILLING_ARGS;

            if (Is_Cell_Erased(OUT)) {  // "nothing" to left, but [1]

                if (
                    L->prior->executor == &Action_Executor
                    and Get_Executor_Flag(EVAL, L->prior, DIDNT_LEFT_QUOTE_PATH)
                ){
                    panic (Error_Literal_Left_Path_Raw());  // [2]
                }

                if (Get_Parameter_Flag(PARAM, VARIADIC)) {  // empty is ok [3]
                    Init_Varargs_Untyped_Infix(ARG, nullptr);
                    goto continue_fulfilling;
                }

                if (Not_Parameter_Flag(PARAM, ENDABLE))
                    panic (Error_No_Arg(Level_Label(L), Key_Symbol(KEY)));

                Init_Unset_Due_To_End(ARG);
                goto continue_fulfilling;
            }

            if (Get_Parameter_Flag(PARAM, VARIADIC)) {  // non-empty is ok [4]
                require (
                  Value* out = Decay_If_Unstable(OUT)  // !!! ^META?
                );
                Init_Varargs_Untyped_Infix(ARG, out);
                Erase_Cell(OUT);
            }
            else switch (pclass) {
              case PARAMCLASS_NORMAL: {
                require (
                  Decay_If_Unstable(OUT)
                );
                Move_Atom(ARG, OUT);
                break; }

              case PARAMCLASS_META: {
                Move_Atom(ARG, OUT);
                break; }

              case PARAMCLASS_JUST: {
                assert(Not_Antiform(OUT));
                Move_Atom(ARG, OUT);
                break; }

              case PARAMCLASS_THE: {
                assert(Not_Antiform(OUT));
                Move_Atom(ARG, OUT);
                break; }

              case PARAMCLASS_SOFT: {
                /*assert(Not_Antiform(OUT));*/
                if (Is_Antiform(OUT))  // !!! Fix this
                    panic ("Unexpected antiform on left of soft escape");

                if (Is_Soft_Escapable_Group(cast(Element*, OUT))) {
                    if (Eval_Any_List_At_Throws(
                        ARG,
                        cast(Element*, OUT),
                        SPECIFIED
                    )){
                        goto handle_thrown;
                    }
                    Erase_Cell(OUT);
                }
                else
                    Move_Atom(ARG, OUT);
                break; }

              default:
                assert(false);
            }

            // When we see `1 + 2 * 3`, when we're at the 2, we don't
            // want to let the * run yet.  So set a flag which says we
            // won't do lookahead that will be cleared when function
            // takes an argument *or* when a new expression starts.
            //
            // This effectively puts the infix into a *single step defer*.
            //
            Option(InfixMode) infix_mode = Get_Level_Infix_Mode(L);
            if (infix_mode) {
                assert(Not_Feed_Flag(L->feed, NO_LOOKAHEAD));
                if (infix_mode == INFIX_TIGHT)  // not postpone or defer
                    Set_Feed_Flag(L->feed, NO_LOOKAHEAD);
            }

            assert(Is_Cell_Erased(OUT));  // output should have been "used up"
            goto continue_fulfilling;
        }

  //=//// NON-INFIX VARIADIC ARG (doesn't consume anything *yet*) /////////=//

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

        // If this is a non-infix action, we're at least at *second* slot:
        //
        //     1 + non-infix-action <we-are-here> * 3
        //
        // That's enough to indicate we're not going to read this as
        // `(1 + non-infix-action <we-are-here>) * 3`.  Contrast with the
        // zero-arity case:
        //
        //     >> two: does [2]
        //     >> 1 + two * 3
        //     == 9
        //
        // We don't get here to clear the flag, so it's `(1 + two) * 3`
        //
        // But if it's infix, arg gathering could still be like:
        //
        //      1 + <we-are-here> * 3
        //
        // So it has to wait until -after- the callsite gather happens to
        // be assured it can delete the flag, to ensure that:
        //
        //      >> 1 + 2 * 3
        //      == 9
        //
        if (not Is_Level_Infix(L))
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
        if (Get_Feed_Flag(L->feed, DEFERRING_INFIX))
            panic (Error_Ambiguous_Infix_Raw());

  //=//// ERROR ON END MARKER, BAR! IF APPLICABLE /////////////////////////=//

        if (Is_Level_At_End(L)) {
            Init_Unset_Due_To_End(ARG);
            goto continue_fulfilling;
        }

        switch (pclass) {

  //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes 1 EVALUATE's worth) /////=//

          case PARAMCLASS_NORMAL:
          case PARAMCLASS_META: {
            Flags flags = EVAL_EXECUTOR_FLAG_FULFILLING_ARG;

            require (
              Level* sub = Make_Level(&Stepper_Executor, L->feed, flags)
            );
            Push_Level_Erase_Out_If_State_0(ARG, sub);  // duplicate erase!

            return CONTINUE_SUBLEVEL(sub); }

  //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG ///////////////////////////////=//

    // 1. Have to account for infix deferrals in cases like:
    //
    //        return the 10 then (x => [x + 10])

          case PARAMCLASS_JUST:
            Just_Next_In_Feed(ARG, L->feed);  // don't pick up binding
            Lookahead_To_Sync_Infix_Defer_Flag(L->feed);  // [1]
            goto continue_fulfilling;

          case PARAMCLASS_THE:
            The_Next_In_Feed(ARG, L->feed);  // pick up binding
            Lookahead_To_Sync_Infix_Defer_Flag(L->feed);  // [1]
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
    // notice a quoting infix construct afterward looking left, we call
    // into a nested evaluator before finishing the operation.

          case PARAMCLASS_SOFT:
            The_Next_In_Feed(ARG, L->feed);

            // See remarks on Lookahead_To_Sync_Infix_Defer_Flag().  We
            // have to account for infix deferrals in cases like:
            //
            //     return if null '[foo] else '[bar]
            //
            // Note that this quoting lookahead ("lookback?") is exempt
            // from the usual "no lookahead" rule while gathering infix
            // arguments.  This supports `null then x -> [1] else [2]`,
            // being 2.  See details at:
            //
            // https://forum.rebol.info/t/1361
            //
            if (
                Lookahead_To_Sync_Infix_Defer_Flag(L->feed) and  // ensure got
                (Get_Flavor_Flag(
                    VARLIST,
                    Phase_Paramlist(Frame_Phase(&L->feed->gotten)),
                    PARAMLIST_LITERAL_FIRST
                ))
            ){
                // We need to defer and let the right hand quote that is
                // quoting leftward win.  We use ST_STEPPER_LOOKING_AHEAD
                // to jump into a sublevel where sub->out is the ARG,
                // and it knows to get the arg from there.

                Flags flags =
                    FLAG_STATE_BYTE(ST_STEPPER_LOOKING_AHEAD)
                    | EVAL_EXECUTOR_FLAG_FULFILLING_ARG
                    | EVAL_EXECUTOR_FLAG_INERT_OPTIMIZATION;

                require (
                  Level* sub = Make_Level(&Stepper_Executor, L->feed, flags)
                );
                Push_Level_Erase_Out_If_State_0(ARG, sub);  // not state 0
                return CONTINUE_SUBLEVEL(sub);
            }
            else if (Is_Soft_Escapable_Group(cast(Element*, ARG))) {
                //
                // We did not defer the literal argument.  If the argument
                // is a GROUP!, it has to be evaluated.
                //
                Element* arg_in_spare = Move_Cell(SPARE, cast(Element*, ARG));
                if (Eval_Any_List_At_Throws(ARG, arg_in_spare, SPECIFIED))
                    goto handle_thrown;
            }
            break;

          default:
            assert(false);
        }

        // If FEED_FLAG_NO_LOOKAHEAD was set going into the argument
        // gathering above, it should have been cleared or converted into
        // FEED_FLAG_DEFERRING_INFIX.
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
    // 1. PANIC() uses the data stack, so we can't pass stack values to it.
    //
    if (TOP_INDEX != STACK_BASE) {

      next_pickup:

        assert(Is_Pushed_Refinement(TOP));

        if (not Cell_Binding(TOP)) {  // duplicate or junk, loop didn't index
            assume (
              Refinify_Pushed_Refinement(TOP_ELEMENT)
            );
            Element* spare = Copy_Cell(SPARE, TOP_ELEMENT);  // [1]
            panic (Error_Bad_Parameter_Raw(spare));
        }

        // Level_Args_Head offsets are 0-based, while index is 1-based.
        // But +1 is okay, because we want the slots after the refinement.
        //
        REBINT offset =
            VAL_WORD_INDEX(TOP) - (ARG - cast(Atom*, Level_Args_Head(L))) - 1;
        KEY += offset;
        ARG += offset;
        PARAM += offset;

        assert(Word_Symbol(TOP) == Key_Symbol(KEY));
        DROP();

        if (Is_Parameter_Unconstrained(PARAM)) {  // no callsite arg, just drop
            if (TOP_INDEX != STACK_BASE)
                goto next_pickup;

            goto fulfill_and_any_pickups_done;
        }

        assert(Is_Light_Null(ARG));  // had to null (could not leave erased)

        Set_Action_Executor_Flag(L, DOING_PICKUPS);
        goto fulfill_arg;
    }

} fulfill_and_any_pickups_done: {

    if (Get_Action_Executor_Flag(L, FULFILL_ONLY)) {  // no typecheck
        assert(Is_Cell_Erased(OUT));  // didn't touch out, should be fresh
        Init_Tripwire(OUT);  // trampoline requires some valid OUT result
        goto skip_output_check;
    }

    STATE = ST_ACTION_TYPECHECKING;

    // Action arguments now gathered, do typecheck pass

} typecheck_then_dispatch: {

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
  // 2. We can't a-priori typecheck the variadic argument, since the values
  //    aren't calculated until the function starts running.  Instead we stamp
  //    this instance of the varargs with a way to reach back and see the
  //    parameter type signature.
  //
  //    The data feed is unchanged (can come from this frame, or another, or
  //    just an array from MAKE VARARGS! of a BLOCK!)
  //
  // 3. Store the offset so that both the arg and param locations can quickly
  //    be recovered, while using only a single slot in the cell.  Sign denotes
  //    whether the parameter was infix or not.
  //
  // 4. When we get to the point of dispatching, what we dispatch has to be
  //    a "Details" Phase... e.g. not just a SPECIALIZE or AUGMENT ParamList
  //    of values, but something that actually has a Dispatcher* C function.
  //    We have to "dig" down through the phases to find it (possibly more
  //    than one, as you can SPECIALIZE a SPECIALIZE of an AUGMENT).  We
  //    do this digging *once* when type checking is over, as opposed to
  //    having to do it for each continuation, so the

    assert(STATE == ST_ACTION_TYPECHECKING);

    KEY = Phase_Keys(&KEY_TAIL, Level_Phase(L));
    ARG = Level_Args_Head(L);
    PARAM = Phase_Params_Head(Level_Phase(L));

    for (; KEY != KEY_TAIL; ++KEY, ++PARAM, ++ARG) {
        if (Is_Typechecked(ARG)) {
            if (LIFT_BYTE(ARG) == DUAL_0)
                assert(Is_Endlike_Unset(ARG));  // locals, <end>-ables
            continue;
        }

        Phase* phase = Level_Phase(L);
        const Param* param = PARAM;
        while (Is_Specialized(param)) {
            Element* archetype = Phase_Archetype(phase);
            phase = Frame_Phase(archetype);
            param = Phase_Param(phase, ARG - cast(Atom*, L->rootvar));
        }

        if (Is_Endlike_Unset(ARG)) {  // special state, DUAL_0
            if (Get_Parameter_Flag(param, ENDABLE))  // !!! "<unset>?
                continue;
            panic (Error_Unspecified_Arg(L));
        }

        assert(LIFT_BYTE(ARG) != DUAL_0);  // not a tripwire

        if (Is_Void(ARG)) {
            if (Get_Parameter_Flag(param, OPT_OUT)) {  // <opt-out> param
                Set_Action_Executor_Flag(L, TYPECHECK_ONLY);
                Mark_Typechecked(ARG);
                Init_Nulled(OUT);
                continue;
            }
        }

        if (Get_Parameter_Flag(param, UNDO_OPT) and Is_Void(ARG)) {
            Init_Nulled(ARG);
            Mark_Typechecked(ARG);  // null generally not in typeset
            continue;
        }

        if (Get_Parameter_Flag(param, VARIADIC)) {  // can't check now [2]
            if (
                Not_Cell_Stable(ARG)
                or not Is_Varargs(Known_Stable(ARG))
            ){
                require (
                  Value* arg = Decay_If_Unstable(ARG)
                );
                panic (Error_Not_Varargs(L, KEY, param, arg));
            }

            Tweak_Cell_Varargs_Phase(ARG, phase);

            bool infix = false;  // !!! how does infix matter?
            CELL_VARARGS_SIGNED_PARAM_INDEX(ARG) =  // store offset [3]
                infix
                    ? -(ARG - Level_Args_Head(L) + 1)
                    : ARG - Level_Args_Head(L) + 1;

            assert(CELL_VARARGS_SIGNED_PARAM_INDEX(ARG) != 0);
            continue;
        }

        heeded (Corrupt_Cell_If_Needful(SPARE));
        heeded (Corrupt_Cell_If_Needful(SCRATCH));

        require (
          bool check = Typecheck_Coerce(L, param, ARG, false)
        );
        if (not check) {
            require (
              Value* arg = Decay_If_Unstable(ARG)
            );
            panic (Error_Phase_Arg_Type(L, KEY, param, arg));
        }

        Mark_Typechecked(ARG);
    }

    Tweak_Level_Phase(L, Phase_Details(Level_Phase(L)));  // ensure Details [4]

    // Action arguments are gathered, begin dispatching

} dispatch: {

  // 1. When dispatching, we aren't using the parameter enumeration states.
  //    These are essentially 4 free pointers (though once a BOUNCE is
  //    returned, the Action_Executor() may start using them again, so they
  //    are only scratch space for the Dispatcher while it is running).
  //
  // 2. This happens if you have something intending to act as infix but
  //    that does not consume arguments, e.g. (/x: infix func [] []).  An
  //    infix function with no arguments might sound dumb, but it allows
  //    a 0-arity function to run in the same evaluation step as the left
  //    hand side.  This is how expression work (see `|:`)
  //
  //    !!! This is dealt with in `skip_output_check`, is it needed here too?
  //
  // 3. Resetting OUT, SPARE, and SCRATCH for a dispatcher's STATE_0 entry
  //    has a slight cost.  The output cell may have CELL_MASK_PERSIST flags
  //    so we bit mask it, but the SPARE and SCRATCH are guaranteed not to,
  //    and can just have 0 written to their header.
  //
  //    But the cost is worth it.  Not only does it stop leaks of internal
  //    processing information to Dispatchers, it triggers asserts if you try
  //    to read them before assignment.  Plus the Dispatcher can take for
  //    granted that's the initial state--and use it as a kind of state flag
  //    to know whether it has written the output or not, and be able to do
  //    things like default it.  Also, when Levels are being persisted in
  //    something like a Plug, their SPARE and SCRATCH have to be stored...
  //    and if they are erased, then that can be an indicator that no
  //    storage is needed.

    assert(Not_Action_Executor_Flag(L, IN_DISPATCH));
    Set_Action_Executor_Flag(L, IN_DISPATCH);

    Corrupt_If_Needful(L->u.action.key);  // freed param enum for dispatcher [1]
    Corrupt_If_Needful(L->u.action.key_tail);
    Corrupt_If_Needful(L->u.action.arg);
    Corrupt_If_Needful(L->u.action.param);

    if (STATE == ST_ACTION_FULFILLING_INFIX_FROM_OUT) {  // can happen [2]
        if (
            L->prior->executor == &Stepper_Executor
            and Get_Executor_Flag(EVAL, L->prior, DIDNT_LEFT_QUOTE_PATH)
        ){  // see notes
            panic (Error_Literal_Left_Path_Raw());
        }

        assert(Is_Level_Infix(L));
    }

    assert(Get_Action_Executor_Flag(L, IN_DISPATCH));

    if (Get_Action_Executor_Flag(L, TYPECHECK_ONLY)) {  // <opt-out>
        assert(Is_Light_Null(OUT));
        goto skip_output_check;
    }

    Erase_Cell(OUT);  // three 0 assignments to cell headers, worth it [3]
    Erase_Cell(SPARE);
    Erase_Cell(SCRATCH);

    STATE = STATE_0;  // reset to zero for each phase

    Invalidate_Gotten(L_next_gotten_raw);  // arbitrary code changes variables

} dispatch_phase: {

    // 1. After typechecking is complete, it "digs" through the phases until
    //    it finds a Details* and makes that the phase.

    assert(Not_Action_Executor_Flag(LEVEL, DELEGATE_CONTROL));  // delegated!

    Details* details = Ensure_Level_Details(L);  // guaranteed Details [1]
    Dispatcher* dispatcher = Details_Dispatcher(details);

    Bounce b = opt Irreducible_Bounce(L, Apply_Cfunc(dispatcher, L));
    if (not b)
        goto check_output;  // consolidated return result into OUT cell

    switch (Bounce_Type(b)) {  // need some actual Bounce behavior...
      case C_CONTINUATION:
        return BOUNCE_CONTINUE;  // Note: may not have pushed a new level...

      case C_DELEGATION:
        Set_Action_Executor_Flag(LEVEL, DELEGATE_CONTROL);
        STATE = 123;  // BOUNCE_CONTINUE does not allow STATE_0
        return BOUNCE_CONTINUE;

      case C_SUSPEND:
        return BOUNCE_SUSPEND;

      case C_THROWN:
        goto handle_thrown;

      case C_REDO_UNCHECKED:
        Clear_Action_Executor_Flag(L, IN_DISPATCH);
        goto dispatch;  // Note: dispatcher may have changed level's PHASE

      case C_REDO_CHECKED:
        Clear_Action_Executor_Flag(L, IN_DISPATCH);
        STATE = ST_ACTION_TYPECHECKING;
        goto typecheck_then_dispatch;

      case C_DOWNSHIFTED:
        L = Adjust_Level_For_Downshift(L);
        assert(Get_Action_Executor_Flag(L, IN_DISPATCH));
        goto dispatch_phase;

      default:
        assert(!"Invalid pseudotype returned from action dispatcher");
    }

} check_output: {  ///////////////////////////////////////////////////////////

  // Here we know the function finished and nothing threw past it or had an
  // abrupt panic().  (It may have done a `return fail (...)`, however.)

  #if RUNTIME_CHECKS
    Do_After_Action_Checks_Debug(L);
  #endif

    if (not Is_Error(OUT))  // !!! Should there be an R_FAIL ?
        assert(STACK_BASE == TOP_INDEX);

} skip_output_check: {  //////////////////////////////////////////////////////

  // This is where things get jumped to if you pass a <opt-out> argument a
  // VOID and it wants to jump past all the processing and return, or if
  // a level just wants argument fulfillment and no execution.
  //
  // NOTE: Anything that calls panic() must do so before Drop_Action()!
  //
  // 1. !!! This used to assert rather than panic, but it turns out this can
  //    actually happen:
  //
  //      >> /left-soft: infix func ['x [word!]] [return x]
  //      >> (|| left-soft)
  //
  //    The LEFT-SOFT looked back, and would have been able to take the ||
  //    except it noticed that it took no arguments.  So it allowed the ||
  //    to win the context (this is how HELP can quote things that quote
  //    left and would usually win, but don't when they have no args).
  //
  // 2. Want to keep this flag between an operation and an ensuing infix in
  //    the same level, so can't clear in Drop_Action(), e.g. due to:
  //
  //      /left-the: infix the/
  //      o: make object! [/f: does [1]]
  //      o.f left-the  ; want error suggesting -> here, need flag for that

    if (STATE == ST_ACTION_FULFILLING_INFIX_FROM_OUT)  // [1]
        panic ("Left lookback toward thing that took no args");

    if (L->prior->executor == &Stepper_Executor)
        Clear_Executor_Flag(EVAL, L->prior, DIDNT_LEFT_QUOTE_PATH);  // [2]

    Drop_Action(L);  // must panic before Drop_Action()

    return OUT;  // not thrown

} handle_thrown: {  //////////////////////////////////////////////////////////

    Drop_Action(L);

    return THROWN;
}}


//
//  Push_Action: C
//
// Allocate the Array of Values inspected by a function when executed (the
// Cells behind ARG(NAME), Bool_ARG(NAME), ARG_N(3),  etc.)
//
// The argument slots are left uninitialized at the outset, and are fulfilled
// as the Action_Executor() walks through the parameter list.  This makes the
// GC have to be sensitive to how far fulfillment has progressed, to avoid
// marking uninitialized memory.
//
Result(Zero) Push_Action(
    Level* L,
    const Value* frame,
    Option(InfixMode) infix_mode
){
    assert(L->executor == &Action_Executor);

    assert(Not_Action_Executor_Flag(L, FULFILL_ONLY));
    assert(not Is_Level_Infix(L));  // Begin_Action() sets mode

    Phase* phase = Frame_Phase(frame);

    Length num_args = Phase_Num_Params(phase);  // includes specialized, locals

    assert(L->varlist == nullptr);

    Set_Action_Level_Label(L, Frame_Label_Deep(frame));

    require (
      Flex* s = u_downcast Prep_Stub(
        STUB_MASK_LEVEL_VARLIST
            | FLEX_FLAG_FIXED_SIZE,  // FRAME!s don't expand ATM
            // not managed by default, see Force_Level_Varlist_Managed()
        Alloc_Stub()
    ));
    Tweak_Misc_Runlevel(s, L);  // maps varlist back to L
    Tweak_Bonus_Keylist_Shared(s, Phase_Keylist(phase));
    Tweak_Link_Inherit_Bind_Raw(s, nullptr);

    Flex_Data_Alloc(
        s,
        num_args + 1 + ONE_IF_POISON_TAILS  // +1 is rootvar
    ) except (Error* e) {
        Set_Stub_Unreadable(s);
        GC_Kill_Stub(s);  // ^-- needs non-null data unless free
        return fail (e);
    }

    L->varlist = u_cast(ParamList*, s);
    L->rootvar = Flex_Head_Dynamic(Element, s);

    possibly(LIFT_BYTE(frame) != NOQUOTE_2);  // can be ACTION!, quasi, etc.

    TRACK(L->rootvar)->header.bits
        = (frame->header.bits & (~ CELL_MASK_LIFT))
            | FLAG_LIFT_BYTE(NOQUOTE_2)  // canonize as FRAME!
            | CELL_FLAG_PROTECTED;  // rootvars protected from modification
    L->rootvar->extra = frame->extra;
    L->rootvar->payload = frame->payload;

    s->content.dynamic.used = num_args + 1;

  #if DEBUG_POISON_UNINITIALIZED_CELLS
  {
    Cell* tail = Array_Tail(Varlist_Array(L->varlist));
    Cell* uninitialized = L->rootvar + 1;
    for (; uninitialized < tail; ++uninitialized)
        Force_Poison_Cell(uninitialized);
  }
  #endif

  #if DEBUG_POISON_EXCESS_CAPACITY
  {
    Cell* tail = L->rootvar + s->content.dynamic.rest;
    Cell* excess = L->rootvar + 1 + num_args;
    for (; excess < tail ; ++excess)
        Force_Poison_Cell(excess);
  }
  #elif DEBUG_POISON_FLEX_TAILS  // redundant if excess capacity poisoned
    Force_Poison_Cell(Array_Tail(L->varlist));
  #endif

    assert(Not_Base_Managed(L->varlist));

    ORIGINAL = phase;

    KEY = Phase_Keys(&KEY_TAIL, ORIGINAL);
    PARAM = Phase_Params_Head(ORIGINAL);
    ARG = L->rootvar + 1;

    Begin_Action(L, infix_mode);
    return zero;
}


//
//  Begin_Action: C
//
// This is separated from Push_Action() because the idea was that you could
// use an already existing VarList*, in which case you'd not need allocations
// done by Push_Action().  But most clients don't need the separation, so
// Push_Action() just calls Begin_Action().
//
// 1. This can happen during Encloser_Dispatcher().  Review.
//
void Begin_Action(Level* L, Option(InfixMode) infix_mode)
{
    assert(not L->u.action.label or Is_Stub_Symbol(unwrap L->u.action.label));
    assert(not Is_Level_Infix(L));
    /* assert(Not_Feed_Flag(L->feed, DEFERRING_INFIX)); */  // !!! happens? [1]

    assert(KEY == KEY_TAIL or Is_Stub_Symbol(*KEY));
    assert(ARG == L->rootvar + 1);

    assert(Not_Flavor_Flag(VARLIST, L->varlist, FRAME_HAS_BEEN_INVOKED));
    Set_Flavor_Flag(VARLIST, L->varlist, FRAME_HAS_BEEN_INVOKED);

    if (not infix_mode) {
        assert(not Is_Level_Infix(L));
    }
    else {
        // While ST_ACTION_FULFILLING_ARG_FROM_OUT is set only during the first
        // argument of an infix call, the type of infix we launched from is
        // set for the whole duration.
        //
        Set_Level_Infix_Mode(L, infix_mode);

        // All the infix call sites cleared this flag on the feed, so it was
        // moved into the Begin_Action() for infix.  Note this has to be done
        // *after* the existing flag state has been captured for invisibles.
        //
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);

        LEVEL_STATE_BYTE(L) = ST_ACTION_INITIAL_ENTRY_INFIX;
    }
}


//
//  Drop_Action: C
//
// 1. Varlists start out unmanaged.  If they became managed, that means they
//    wound up being referenced in a cell that may outlive this Drop_Action().
//    We allow frames to exist indefinitely, due to the belief that we would
//    not want JavaScript to have the upper hand in "closure" scenarios.  See:
//
//      "What Happens To Function Args/Locals When The Call Ends"
//      https://forum.rebol.info/t/234
//
// 2. If a varlist never became managed, there are no outstanding references,
//    and we can free it.  (There was some code at one point that tried to
//    reuse varlists, but it was a premature optimization with no benefit.)
//
void Drop_Action(Level* L) {
    Corrupt_If_Needful(L->u.action.label);  // first (data breakpoint)

    assert(Misc_Runlevel(L->varlist) == L);

    if (
        Is_Base_Managed(L->varlist)  // outstanding references may exist [1]
        or Get_Action_Executor_Flag(L, FULFILL_ONLY)
    ){
        Tweak_Misc_Runlevel(L->varlist, nullptr);
    }
    else {  // no outstanding references [2]
        GC_Kill_Flex(L->varlist);  // not in manuals tracking list
    }

    L->varlist = nullptr;
    L->flags.bits &= ~ (  // reuse scenarios are speculative, but expect this
        FLAG_STATE_BYTE(255)
            | ACTION_EXECUTOR_FLAG_FULFILL_ONLY
            | ACTION_EXECUTOR_FLAG_INFIX_A
            | ACTION_EXECUTOR_FLAG_INFIX_B
    );

    Corrupt_If_Needful(ORIGINAL); // action is no longer running
    L->executor = nullptr;  // so GC won't think level needs Action marking

  #if DEBUG_LEVEL_LABELS
    L->label_utf8 = nullptr;  // do last (for debug watchlist)
  #endif
}
