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
// This tries to simplify a bounce to get it to be just an Value content in
// the OUT cell, if possible.  Not all bounces can be simplified, but when
// they can be this can save when delegating code, on needing to call a cycle
// of trampoline.
//
// NOTE: A nullptr coming from this means *the bounce was reducible*.  e.g.
// if it was just a nulled cell, then that would be put in OUT and it will
// return `none` (which is a zero-ish state like nullptr is, but here we
// are interpreting it differently than a typical nullptr Bounce).
//
Option(Bounce) Irreducible_Bounce(Level* level_, Bounce b) {
    if (b == OUT) {  // common case, made fastest
        assert(Is_Cell_Readable(OUT));  // must write out, even if just void
        return none;
    }

  handle_null_or_failure: {

  // 1. nullptr Bounce often means "return a ~null~ antiform", though internal
  //    APIs usually use NULL_OUT to initialize the output cell directly to
  //    the null state and take the earlier/faster branch above.
  //
  //    However there is a second meaning of the 0 state when `g_failure` is
  //    set, which is the desire to return a FAILURE! antiform of whatever the
  //    Error* in `g_failure` is.
  //
  // 2. At one time this would unwind levels as a convenience on failure:
  //
  //        while (TOP_LEVEL != L) {
  //            Rollback_Level(TOP_LEVEL);
  //            Drop_Level(TOP_LEVEL);
  //            Erase_Cell(TOP_LEVEL->out);
  //        }
  //        Rollback_Level(L);
  //
  //   However this is a bit presumptuous; nothing used it so it's hard to
  //   tell how many places would be helped.  It was discovered to be broken'
  //   for intrinsics since they shouldn't roll back the sublevel they ran in
  //   as they weren't expected to...so this was removed for now.

    if (b == nullptr) {  // could be from a NEEDFUL_RESULT_0 [1]
        if (not g_failure) {
            Init_Null(OUT);
            return none;
        }

        assert(not Is_Throwing(L));

        Init_Error_Cell(L->out, g_failure);
        g_failure = nullptr;  // have to do before Force_Location_Of_Error()
        Failify_Cell_And_Force_Location(L->out);

        return none;
    }

} give_up_on_simplifying_if_bounce_wild: {

    if (Is_Bounce_Wild(b))
        return b;  // can't simplify, may be a panic, continuation, etc.

} handle_bounce_okay: {

  // BOUNCE_OKAY is just LIB(OKAY) (fixed pointer).  It's important for cases
  // like typecheckers that don't actually want to overwrite their OUT cell,
  // which is essential to the intrinsic optimization.
  //
  // (But if we're receiving it here, we're not a typechecker call, so we
  // go ahead and reify the okay into the output cell.

    if (b == BOUNCE_OKAY) {
        Init_Okay(OUT);
        return none;
    }

} copy_api_cell_to_out_and_release_it: {

    if (Is_Bounce_A_Cell(b)) {  // must be Api Value
        Api(Value*) v = Value_From_Bounce(b);
        assert(Is_Api_Value(v));
        Copy_Cell(OUT, v);
        Release_Api_Value_If_Unmanaged(v);
        return none;
    }

} turn_utf8_into_delegated_code: {

  // While it might seem more obvious for `return "some string";` to give back
  // a text string, it's actually far more useful to run UTF-8 returns as
  // delegated code:
  //
  //   https://forum.rebol.info/t/returning-a-string-from-a-native/2357
  //
  // 1. %sys-core.h natives don't want to manage their varlists, so use things
  //    like ARG(NAME) for efficiency.  They pass LIBREBOL_BINDING_NAME() in
  //    API calls as just the module they're in (core uses LIB context).
  //
  //    But here we're executing some code like `return "print -[Hi!]-"` and
  //    there was no implicit capture of LIBREBOL_BINDING_NAME().  So we don't
  //    have the binding...just a (probably) unmanaged varlist which never had
  //    a moment that the inherited binding was poked into it (because core
  //    natives are their own dispatchers).
  //
  //    To get basics like `return "halt"` to work in the stdio module, we
  //    just run the code in LIB in this case.  You need to use an API like
  //    rebContinue() if that's not good enough (but it still would not see
  //    function arguments in %sys-core.h natives).

    assert(Detect_Rebol_Pointer(b) == DETECTED_AS_UTF8);

    const char* cp = cast(const char*, b);
    if (cp[0] == '~') {
        if (cp[1] == '\0') {
            Init_Void(L->out);
            return none;  // make return "~" fast!
        }
        if (
            cp[1] == '<' and cp[2] == '?' and cp[3] == '>'
            and cp[4] == '~' and cp[5] == '\0'
        ) {
            Init_Tripwire(L->out);
            return none;  // make return "~<?>~" fast!
        }
    }

    if (Link_Inherit_Bind(L->varlist) == nullptr) {  // raw native hack [1]
        assert(Get_Details_Flag(Ensure_Level_Details(L), RAW_NATIVE));
        possibly(Not_Base_Managed(L->varlist));
        rebDelegateCore(cast(RebolContext*, g_lib_context), cp);
        return BOUNCE_DELEGATE;
    }

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
bool Lookahead_To_Sync_Infix_Defer_Flag(Level* L)
{
    assert(Not_Feed_Flag(L->feed, DEFERRING_INFIX));

    Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);

    if (Is_Feed_At_End(L->feed))
        return false;

    if (Type_Of_Unchecked(At_Feed(L->feed)) != TYPE_WORD)
        return false;

    const StackIndex base = TOP_INDEX;
    assert(base == STACK_BASE);

    if (not Try_Push_Steps_To_Stack_For_Word(At_Feed(L->feed), L_binding))
        return false;

    heeded (Init_Null_Signifying_Tweak_Is_Pick(SCRATCH));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    StateByte saved_state = STATE;
    heeded (STATE = ST_TWEAK_GETTING);

    Option(Error*) e = Tweak_Stack_Steps_With_Dual_Scratch_To_Dual_Spare();
    Drop_Data_Stack_To(base);
    STATE = saved_state;

    if (e)
        return false;

    if (not Is_Lifted_Action(As_Stable(SPARE)))  // DUAL protocol (lifted!)
        return false;

    Option(InfixMode) infix_mode = Frame_Infix_Mode(SPARE);
    if (not infix_mode)
        return false;

    if (infix_mode == INFIX_DEFER)
        Set_Feed_Flag(L->feed, DEFERRING_INFIX);
    return true;
}


// When the end of a feed is hit, we need an indicator that is distinct from
// something like NULL or VOID!, because these can be normal by-products of
// evaluation.  e.g. you should be able to tell between:
//
//     >> hole-ok-function ()
//     HOLE-OK-FUNCTION received a VOID! antiform
//
//     >> hole-ok-function null
//     HOLE-OK-FUNCTION received a ~null~ antiform
//
//     >> hole-ok-function
//     HOLE-OK-FUNCTION received a HOLE
//
// However, we want BLANK! (which manifests as comma characters in molds) to
// be symmetric (also symmetric in terms of any errors):
//
//     >> hole-ok-function,
//     HOLE-OK-FUNCTION received a HOLE
//
// This is why we put a "bedrock hole" cell in the frame.  It's out of band
// in terms of "Value" results that can be produced by an EVAL, so we can't
// call `Stepper_Executor()` to produce it...and need to check the feed for
// an end or a BLANK! before calling the stepper.  Hence if a BLANK! is seen
// it will stay as a BLANK! in the feed until the action has acquired all
// its arguments.
//
static void Handle_Barrier_Hit(Sink(Param) out, Level* L) {
    assert(Is_Cell_A_Bedrock_Hole(PARAM));
    Copy_Cell(out, PARAM);
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

      #if DEBUG_TRACK_EXTEND_CELLS  // undo exception before user can see it
        ARG->track_flags.bits &= (~ TRACK_FLAG_VALID_EVAL_TARGET);
      #endif

        if (Get_Action_Executor_Flag(L, DOING_PICKUPS)) {
            if (TOP_INDEX != L->baseline.stack_base)
                goto next_pickup;

            goto fulfill_and_any_pickups_done;
        }
        continue;

      skip_fulfilling_arg_for_now:
        assert(Not_Action_Executor_Flag(L, DOING_PICKUPS));
        assert(Is_Null_Signifying_Unspecialized(ARG));  // couldn't leave erased
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

        if (Is_Specialized(PARAM)) {
            Blit_Cell(ARG, PARAM);
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
                REBLEN offset = ARG - Level_Args_Head(L);
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

                Init_Null_Signifying_Unspecialized(Erase_Cell(ARG));
                goto skip_fulfilling_arg_for_now;  // ^-- can't leave erased
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

  // 1. Evaluation argument "hook" parameters (marked in FUNC by `<variadic>`).
  //    They point back to this call through a reified FRAME!, and are able to
  //    consume additional arguments during the function run.

  fulfill_arg: {

    ParamClass pclass = Parameter_Class(PARAM);

    if (STATE == ST_ACTION_FULFILLING_INFIX_FROM_OUT)
        goto fill_next_arg_from_out_cell;

    if (Get_Parameter_Flag(PARAM, VARIADIC)) {  // don't consume *yet* [1]
        Force_Level_Varlist_Managed(L);
        Init_Varargs_Untyped_Normal(ARG, L);
        goto continue_fulfilling;
    }

    goto fill_next_arg_from_callsite;


  fill_next_arg_from_out_cell: { /////////////////////////////////////////////

  // ST_ACTION_FULFILLING_INFIX_FROM_OUT is primarily for infix, but also
  // CASCADE, etc... as a trick for slipping in the first argument when you
  // want the rest of the arguments to come from an input feed.

    STATE = ST_ACTION_FULFILLING_ARGS;

    if (Is_Cell_Erased(OUT)) {  // "nothing" to left
        if (
            L->prior->executor == &Stepper_Executor
            and Get_Executor_Flag(EVAL, L->prior, FULFILLING_ARG)
        ){
            panic (Error_Invalid_Lookback_Raw());  // (if + 2 [...]), no good
        }

        Handle_Barrier_Hit(ARG, L);  // (else [...]), treated as <hole>
        Init_Unreadable(OUT);
    }
    else if (Get_Parameter_Flag(PARAM, VARIADIC)) {  // treat as single value
        Init_Varargs_Untyped_Infix(ARG, As_Element(OUT));
        Init_Unreadable(OUT);
    }
    else switch (pclass) {
      case PARAMCLASS_NORMAL:  // decay happens during typechecking
      case PARAMCLASS_META:  // !!! ...META "ParamClass" is going away!
        possibly(Not_Cell_Stable(OUT));  // e.g. VOID! as left hand of ELSE
        Move_Cell(ARG, OUT);
        break;

      case PARAMCLASS_LITERAL:
        assert(not Is_Antiform(OUT));
        Move_Cell(ARG, OUT);
        break;

      case PARAMCLASS_SOFT:
        assert(not Is_Antiform(OUT));
        if (Is_Soft_Escapable_Group(As_Element(OUT))) {
            if (Eval_Any_List_At_Throws(ARG, As_Element(OUT), SPECIFIED))
                goto handle_thrown;
            Init_Unreadable(OUT);
        }
        else
            Move_Cell(ARG, OUT);
        break;

      default:
        assert(false);
    }

    assert(Not_Cell_Readable(OUT));  // output should be "used up"

} update_no_lookahead_flag_and_continue_fulfilling: {

  // When we see `1 + 2 * 3`, when we're at the 2, we don't want to let the
  // `*` run yet.  So set a flag which says not to do lookahead.  It gets
  // cleared when a function takes an argument *or* a new expression starts.
  //
  // (This effectively puts the infix into a "single step defer")

    Option(InfixMode) infix_mode = Get_Level_Infix_Mode(L);
    if (infix_mode) {
        assert(Not_Feed_Flag(L->feed, NO_LOOKAHEAD));
        if (infix_mode == INFIX_TIGHT)  // not postpone or defer
            Set_Feed_Flag(L->feed, NO_LOOKAHEAD);
    }

    goto continue_fulfilling;


} fill_next_arg_from_callsite: { /////////////////////////////////////////////

  clear_no_lookahead_flag_if_not_infix: {

  // If this is a non-infix action, we're at least at *second* slot:
  //
  //     1 + non-infix-action <we-are-here> * 3
  //
  // That's enough to indicate we're not going to read this as:
  //
  //     (1 + non-infix-action <we-are-here>) * 3
  //
  // Contrast with the zero-arity case:
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
  // So it has to wait until -after- the callsite gather happens to be assured
  // it can delete the flag, to ensure that:
  //
  //      >> 1 + 2 * 3
  //      == 9

    if (not Is_Level_Infix(L))
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);

} error_if_deferring_infix_when_we_reach_here: {

  // Once a deferred flag is set, it must be cleared during the evaluation of
  // the argument it was set for... OR the function call has to end.  If we
  // need to gather an argument when that is happening, it means neither of
  // those things are true, e.g.:
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

    if (Get_Feed_Flag(L->feed, DEFERRING_INFIX))
        panic (Error_Ambiguous_Infix_Raw());

} handle_hitting_blank_or_end_of_feed: {

  // 1. Right now you can't process BLANK! as an argument, not even if the
  //    parameter is @literal.  A <blank> annotation that lets you say that
  //    you want literal blanks should (maybe?) be available?  Or maybe not.
  //
  // 2. Newlines pose a problem for <hole>-taking functions (RETURN, QUIT,
  //    CONTINUE, THROW ...)
  //
  //        foo: func [x [integer!]] [
  //            return
  //            print "We want this to be disallowed"
  //        ]
  //
  //    Note: This will likely escalate to a general rule that evaluations
  //    can't span newlines like this, which will require a special line
  //    continuation character in the scanner--probably backslash).

    if (Next_Is_End_Or_Blank(L)) {  // [1]
        Handle_Barrier_Hit(ARG, L);
        goto continue_fulfilling;
    }

    if (Get_Parameter_Flag(PARAM, HOLE_OK)) {
        if (Get_Cell_Flag(At_Feed(L->feed), NEWLINE_BEFORE))
            panic (Error_Hole_Spans_Newline(L));  // [2]
    }

} feed_has_arg_and_we_want_it: {

  // 1. We want this to work, see Lookahead_To_Sync_Infix_Defer_Flag():
  //
  //        return the 10 then (x => [x + 10])
  //
  // 2. If FEED_FLAG_NO_LOOKAHEAD was set going into this argument gathering,
  //    it should get cleared or converted into FEED_FLAG_DEFERRING_INFIX.
  //
  //     1 + 2 * 3
  //           ^-- this deferred its chance, so 1 + 2 will complete

    switch (pclass) {
      case PARAMCLASS_NORMAL:
      case PARAMCLASS_META: {
        Flags flags = EVAL_EXECUTOR_FLAG_FULFILLING_ARG;

        require (
          Level* sub = Make_Level(&Stepper_Executor, L->feed, flags)
        );
        possibly(Is_Light_Null(ARG));  // !!! review

      #if DEBUG_TRACK_EXTEND_CELLS  // special exception--not user visible yet
        ARG->track_flags.bits |= TRACK_FLAG_VALID_EVAL_TARGET;
      #endif

        Push_Level(Erase_Cell(ARG), sub);

        return CONTINUE_SUBLEVEL; }

      case PARAMCLASS_LITERAL:
        The_Next_In_Feed(ARG, L->feed);  // pick up binding (may throw away)
        Lookahead_To_Sync_Infix_Defer_Flag(L);  // [1]
        goto continue_fulfilling;

      case PARAMCLASS_SOFT: {
        The_Next_In_Feed(ARG, L->feed);  // pick up binding (may throw away)
        Lookahead_To_Sync_Infix_Defer_Flag(L);  // [1]

        if (Is_Soft_Escapable_Group(As_Element(ARG))) {
            Element* arg_in_spare = Move_Cell(SPARE, As_Element(ARG));

          #if DEBUG_TRACK_EXTEND_CELLS  // special exception
            ARG->track_flags.bits |= TRACK_FLAG_VALID_EVAL_TARGET;
          #endif

            bool threw = Eval_Any_List_At_Throws(ARG, arg_in_spare, SPECIFIED);

          #if DEBUG_TRACK_EXTEND_CELLS  // undo special exception
            ARG->track_flags.bits &= (~ TRACK_FLAG_VALID_EVAL_TARGET);
          #endif

            if (threw)
                goto handle_thrown;
        }
        break; }

      default:
        assert(false);
    }

    assert(Not_Feed_Flag(L->feed, NO_LOOKAHEAD));  // should be clear now [1]

    goto continue_fulfilling;

}}}}  // <-- this ends for() loop, rethink to not use for()!

  // There may have been refinements that were skipped because the order of
  // definition did not match the order of usage.  They were left on the stack
  // with a pointer to the `param` and `arg` after them for later fulfillment.
  //
  // Note that there may be functions on the stack if this is the second time
  // through, and we were just jumping up to check the parameters in response
  // to a BOUNCE_REDO_CHECKED; if so, skip this.
  //
  // 1. PANIC() uses the data stack, so we can't pass stack values to it.

  #if DEBUG_POISON_FLEX_TAILS
    assert(Is_Cell_Poisoned(ARG));  // arg can otherwise point to any arg cell
  #endif

    if (TOP_INDEX != STACK_BASE) {

      next_pickup:

        assert(Is_Pushed_Refinement(TOP_STABLE));

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
            VAL_WORD_INDEX(TOP) - (ARG - Level_Args_Head(L)) - 1;
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

        assert(Is_Null_Signifying_Unspecialized(ARG));  // had to init

        Set_Action_Executor_Flag(L, DOING_PICKUPS);
        goto fulfill_arg;
    }

} fulfill_and_any_pickups_done: { ////////////////////////////////////////////

    if (Get_Action_Executor_Flag(L, FULFILL_ONLY)) {  // no typecheck
        assert(Is_Cell_Erased(OUT));  // didn't touch out, should be fresh
        Init_Tripwire(OUT);  // trampoline requires some valid OUT result
        goto skip_output_check;
    }

    STATE = ST_ACTION_TYPECHECKING;

    goto typecheck_then_dispatch;  // action arguments are all now gathered


} typecheck_then_dispatch: { /////////////////////////////////////////////////

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

    Phase* phase = Level_Phase(L);

    STATIC_ASSERT(VARLIST_FLAG_PURE == CELL_FLAG_CONST);

    Flags pure_mask = phase->header.bits & STUB_FLAG_PHASE_PURE;
    L->varlist->header.bits |= pure_mask;

  check_paramlist_layer: { ///////////////////////////////////////////////////

    KEY = Phase_Keys(&KEY_TAIL, phase);
    ARG = Level_Args_Head(L);
    PARAM = Phase_Params_Head(phase);

    for (; KEY != KEY_TAIL; ++KEY, ++PARAM, ++ARG) {
        Track_Clear_Cell_Shield(ARG);  // we Mem_Copy(), shield bits copied

        ARG->header.bits |= pure_mask;  // when's the right time for this [?]

        if (Is_Specialized(PARAM)) {  // no typecheck info in this layer
            if (Not_Cell_Flag(PARAM, PARAM_MARKED_SEALED)) {
                possibly(Get_Cell_Flag(ARG, PARAM_MARKED_SEALED));
                Clear_Cell_Flag(ARG, PARAM_MARKED_SEALED);
            }
            continue;  // digging should typecheck the ARG if not sealed [4]
        }

        assert(Not_Cell_Flag(ARG, PARAM_MARKED_SEALED));

        if (Is_Typechecked(ARG))  // residual typechecks from another pass...
            continue;  // !!! possibly not valid in redo situations?  Review.

        if (Is_Cell_A_Bedrock_Hole(ARG)) {
            if (Get_Parameter_Flag(PARAM, HOLE_OK)) {
                if (Get_Parameter_Flag(PARAM, VARIADIC))  // good or bad idea?
                    Init_Varargs_Untyped_Infix(ARG, nullptr);

                Mark_Typechecked(ARG);
                continue;  // !!! standardize hole to the parameter?
            }
            if (Get_Parameter_Flag(PARAM, REFINEMENT)) {
             #if DEBUG_POISON_UNINITIALIZED_CELLS
                Poison_Cell(ARG);
              #endif
                Blit_Null_Typechecked(ARG);
                continue;
            }
            panic (Error_No_Arg(Level_Label(L), Key_Symbol(KEY)));
        }

        assert(LIFT_BYTE(ARG) != BEDROCK_0);
        Value* arg = As_Value(ARG);

        if (
            Is_Cell_A_Veto_Hot_Potato(arg)
            and Not_Parameter_Flag(PARAM, WANT_VETO)
        ){
            Set_Action_Executor_Flag(L, TYPECHECK_ONLY);
            Mark_Typechecked(ARG);
            Init_Null(OUT);
            continue;
        }

        if (Get_Parameter_Flag(PARAM, UNDO_OPT)) {
            if (Any_Void(arg)) {  // accepts empty pack -or- void
                Init_Null(ARG);
                Mark_Typechecked(ARG);  // null rejected
                continue;
            }
        }

        if (Get_Parameter_Flag(PARAM, VARIADIC)) {  // can't check now [2]
            require (
                Stable* stable = Decay_If_Unstable(arg)
            );
            if (not Is_Varargs(stable))
                panic (Error_Not_Varargs(L, KEY, PARAM, stable));

            Tweak_Cell_Varargs_Phase(ARG, Level_Phase(L));

            bool infix = false;  // !!! how does infix matter?
            CELL_VARARGS_SIGNED_PARAM_INDEX(ARG) =  // store offset [3]
                infix
                    ? -(ARG - Level_Args_Head(L) + 1)
                    : ARG - Level_Args_Head(L) + 1;

            assert(CELL_VARARGS_SIGNED_PARAM_INDEX(ARG) != 0);
            Mark_Typechecked(ARG);
            continue;
        }

        require (
          bool check = Typecheck_Coerce_Use_Toplevel(
            L, Known_Unspecialized(PARAM), arg
          )
        );
        if (not check)
            panic (Error_Phase_Arg_Type(L, KEY, PARAM, arg));
        Mark_Typechecked(ARG);
    }

    if (Stub_Flavor(phase) != FLAVOR_DETAILS) {
        Phase* archetype_phase = Frame_Phase(Phase_Archetype(phase));
        assert(archetype_phase != phase);
        phase = archetype_phase;
        goto check_paramlist_layer;  // don't skip unhiding/typechecking
    }

  require (
    Tweak_Level_Phase(L, phase)  // [4]
  );

}} dispatch: {

  // 1. This happens when infix doesn't consume args, e.g. (x: infix does []).
  //    Such functions aren't useless: they allow a 0-arity function to run
  //    in the same evaluation step as its left hand side.
  //
  // 2. Resetting OUT, SPARE, and SCRATCH for a dispatcher's STATE_0 entry
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

    if (STATE == ST_ACTION_FULFILLING_INFIX_FROM_OUT)  // arity-0 infix [1]
        assert(Is_Level_Infix(L));

    assert(Get_Action_Executor_Flag(L, IN_DISPATCH));

    if (Get_Action_Executor_Flag(L, TYPECHECK_ONLY)) {  // e.g. got VETO
        assert(Is_Light_Null(OUT));
        goto skip_output_check;
    }

    Erase_Cell(OUT);  // three 0 assignments to cell headers, worth it [2]
    Erase_Cell(SPARE);
    Erase_Cell(SCRATCH);

    STATE = STATE_0;  // reset to zero for each phase

} dispatch_phase: { //////////////////////////////////////////////////////////

    assert(Not_Action_Executor_Flag(LEVEL, DELEGATE_CONTROL));  // delegated!

    Details* details = Ensure_Level_Details(L);  // guaranteed Details
    Dispatcher* dispatcher = Details_Dispatcher(details);

  doublecheck_typecheck: {

  // Last-minute typecheck verification, when we have the dispatcher in our
  // hands to more easily see who's getting the non-typechecked arguments.
  //
  // 1. Things like SPECIALIZE don't type check their arguments a-priori.
  //    Consequently, if you do something like an ADAPT of a SPECIALIZE then
  //    there could be fields invisible to the ADAPT that are Is_Specialized()
  //    but not typechecked.  We detect this situation by seeing if the
  //    ParamList is "owned" by the Details (e.g. no deeper details laying a
  //    claim on it, so it must have been a baseline ParamList e.g. the ones
  //    created before they are paired with a native).  If not owned, then
  //    we only check the non-specialized parameters.
  //
  // 2. When dispatching, we aren't using the parameter enumeration states.
  //    These are essentially 4 free pointers (though once a BOUNCE is
  //    returned, the Action_Executor() may start using them again, so they
  //    are only scratch space for the Dispatcher while it is running).

  #if RUNTIME_CHECKS
    if (STATE == STATE_0) {
        bool inputs_only = did Phase_Details(Phase_Paramlist(details));  // [1]

        KEY = Phase_Keys(&KEY_TAIL, details);
        ARG = Level_Args_Head(L);
        PARAM = Phase_Params_Head(details);

        for (; KEY != KEY_TAIL; ++KEY, ++PARAM, ++ARG) {
            if (Get_Cell_Flag(PARAM, PARAM_MARKED_SEALED)) {
                assert(inputs_only);
                assert(Get_Cell_Flag(ARG, PARAM_MARKED_SEALED));
                continue;
            }

            if (inputs_only and Is_Specialized(PARAM))
                continue;

            if (not Is_Typechecked(ARG))
                assert(Not_Parameter_Checked_Or_Coerced(PARAM));
        }
    }

    Corrupt_If_Needful(L->u.action.key);  // free for use by dispatcher [2]
    Corrupt_If_Needful(L->u.action.key_tail);
    Corrupt_If_Needful(L->u.action.arg);
    Corrupt_If_Needful(L->u.action.param);
  #endif

    Bounce b = opt Irreducible_Bounce(L, Apply_Cfunc(dispatcher, L));
    if (not b)
        goto check_output;  // consolidated return result into OUT cell

  ensure_typecheckers_dont_overwrite_output: {

  // If a NATIVE:INTRINSIC returns [logic!] then it is supposed to return
  // that logic by means of either `nullptr` or `BOUNCE_OKAY`, as opposed
  // to disrupting the output cell.  We can check enforce that even if
  // not being called intrinsically, to catch bugs in the typecheckers.

  #if RUNTIME_CHECKS
    if (
        Get_Details_Flag(details, CAN_DISPATCH_AS_INTRINSIC)
        and b != nullptr
        and b != BOUNCE_OKAY
        and Is_Intrinsic_Typechecker(details)
    ){
        panic ("Intrinsic typechecker overwrote output cell");
    }
  #endif

} handle_bounce: {

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
        if (Get_Action_Executor_Flag(L, IN_DISPATCH))
            goto dispatch_phase;
        assert(STATE == ST_ACTION_TYPECHECKING);
        goto typecheck_then_dispatch;

      default:
        assert(!"Invalid pseudotype returned from action dispatcher");
    }

}}} check_output: {  /////////////////////////////////////////////////////////

  // Here we know the function finished and nothing threw past it or had an
  // abrupt panic().  (It may have done a `return fail (...)`, however.)

  #if RUNTIME_CHECKS
    Do_After_Action_Checks(L);
  #endif

    if (not Is_Failure(OUT))  // !!! Should there be an R_FAIL ?
        assert(STACK_BASE == TOP_INDEX);

    if (Get_Level_Flag(L, VANISHABLE_VOIDS_ONLY) and Is_Void(OUT))
        Note_Level_Out_As_Void_To_Make_Heavy(L);

} skip_output_check: {  //////////////////////////////////////////////////////

  // This is where things get jumped to if you passsed a VETO argument and it
  // wants to jump past all the processing and return, or if a level wants
  // argument fulfillment and no execution.
  //
  // NOTE: Anything that calls panic() must do so before Drop_Action()!

    if (STATE == ST_ACTION_FULFILLING_INFIX_FROM_OUT)  // !!! can this happen?
        panic ("skip_output_check + ST_ACTION_FULFILLING_INFIX_FROM_OUT");

    Drop_Action(L);  // must panic before Drop_Action()

    if (Get_Level_Flag(L, FORCE_HEAVY_BRANCH))
        Force_Cell_Heavy(OUT);

    return OUT;  // not thrown

} handle_thrown: {  //////////////////////////////////////////////////////////

    if (L->varlist)
        Drop_Action(L);

    return THROWN;
}}


//
//  Push_Action: C
//
// Allocate the Array of Values inspected by a function when executed (the
// Cells behind ARG(NAME), ARG(NAME), ARG_N(3),  etc.)
//
// The argument slots are left uninitialized at the outset, and are fulfilled
// as the Action_Executor() walks through the parameter list.  This makes the
// GC have to be sensitive to how far fulfillment has progressed, to avoid
// marking uninitialized memory.
//
Result(None) Push_Action(
    Level* L,
    const Value* frame,  // can be ACTION! or FRAME!
    Option(InfixMode) infix_mode
){
  #if RUNTIME_CHECKS
    assert(L->prior != nullptr);  // must be pushed
  #endif

    assert(L->executor == &Action_Executor);

    assert(Not_Action_Executor_Flag(L, FULFILL_ONLY));
    assert(not Is_Level_Infix(L));  // Begin_Action() sets mode

    Phase* phase = Frame_Phase(frame);

    Length num_args = Phase_Num_Params(phase);  // includes specialized, locals

    assert(L->varlist == nullptr);

    Set_Action_Level_Label(L, Frame_Label_Deep(frame));

    STATIC_ASSERT(DETAILS_FLAG_METHODIZED == VARLIST_FLAG_METHODIZED);

  create_varlist: {

    require (
      Flex* s = u_downcast Prep_Stub(
        STUB_MASK_LEVEL_VARLIST
            | FLEX_FLAG_FIXED_SIZE  // FRAME!s don't expand ATM
            // not managed by default, see Force_Level_Varlist_Managed()
            | (phase->header.bits & DETAILS_FLAG_METHODIZED),
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

    possibly(LIFT_BYTE(frame) != NOQUOTE_3);  // can be ACTION!, quasi, etc.

    FORCE_TRACK_0(L->rootvar)->header.bits
        = (frame->header.bits & (~ CELL_MASK_LIFT))
            | FLAG_LIFT_BYTE(NOQUOTE_3);  // canonize as FRAME!
    L->rootvar->extra = frame->extra;
    L->rootvar->payload = frame->payload;

    Shield_Rootvar_If_Debug(L->rootvar);

    s->content.dynamic.used = num_args + 1;

  poison_uninitialized_cells: {

  #if DEBUG_POISON_UNINITIALIZED_CELLS
    Cell* tail = Array_Tail(Varlist_Array(L->varlist));
    Cell* uninitialized = L->rootvar + 1;
    for (; uninitialized < tail; ++uninitialized)
        Force_Poison_Cell(uninitialized);
  #endif

} poison_excess_capacity: {

  #if DEBUG_POISON_EXCESS_CAPACITY
    Cell* tail = L->rootvar + s->content.dynamic.rest;
    Cell* excess = L->rootvar + 1 + num_args;
    for (; excess < tail ; ++excess)
        Force_Poison_Cell(excess);
  #elif DEBUG_POISON_FLEX_TAILS  // redundant if excess capacity poisoned
    Force_Poison_Cell(Array_Tail(L->varlist));
  #endif

}} begin_action: {

    assert(Not_Base_Managed(L->varlist));

    ORIGINAL = phase;

    KEY = Phase_Keys(&KEY_TAIL, ORIGINAL);
    PARAM = Phase_Params_Head(ORIGINAL);
    ARG = L->rootvar + 1;

    Begin_Action(L, infix_mode);
    return none;
}}


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
