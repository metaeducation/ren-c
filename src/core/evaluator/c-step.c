//
//  file: %c-step.c
//  summary: "Code for Evaluation of one Step in the Main Interpreter"
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
// This file contains code for the `Stepper_Executor()`.  It's responsible
// for the typical interpretation of BLOCK! or GROUP!, in terms of giving
// sequences like `x: 1 + 2` a meaning for how SET-WORD! or INTEGER! behaves.
//
// It returns its results "Meta", e.g. stepping across [1 + 2] returns '3.
// The reason for this choice is that there needs to be a way to return
// a signal indicating that there was no evaluative product.  That's because
// it's necessary for functions like EVAL:STEP to be able to know when there
// is no result to return.
//
// Consider:
//
//    eval:step [1 + 2, void]
//       => new position [, void]
//       => synthesized result is 3
//
//    eval:step [, void]
//       => new position []
//       => synthesized result is unstable ~[]~ antiform
//
//    eval:step []
//       => null (no position, no synthesized result)
//
// By the same token:
//
//    eval:step [, , ,]
//       => null (no position, no synthesized result)
//
// To provide the fundamental information that EVAL:STEP needs, there has to
// be a way to return something to indicate "no synthesized result" that is
// completely out of band of all possible expression evaluation results.
// This can be done by having expression results be ^META (quasiform and
// quoted) and reserving e.g. the "trash" antiform state to signal no result.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Stepper_Executor() is LONG.  That's largely on purpose.  Breaking it
//   into functions would add overhead (in the RUNTIME_CHECKS build, if not
//   also NO_RUNTIME_CHECKS builds) and prevent interesting optimizations.
//   Factoring better is desired--but more in order to reduce redundant code
//   paths, as opposed to making this file shorter as a goal in and of itself.
//
//   It is separated into sections, and the invariants in each section are
//   made clear with comments and asserts.
//
// * See %d-eval.c for more detailed assertions of the preconditions,
//   postconditions, and state...which are broken out to help keep this file
//   a (slightly) more manageable length.
//
// * The evaluator only moves forward, and operates on a strict window of
//   visibility of two elements at a time (current position and "lookback").
//   See `Feed` for the code that provides this abstraction over Ren-C
//   arrays as well as C va_list.
//
// * By design the evaluator isn't recursive at the C level--it's "stackless".
//   At points where a sub-expression must be evaluated in a new level, it will
//   heap-allocate that level and then do a C `return` of BOUNCE_CONTINUE.
//   Processing then goes through the "Trampoline" (see %c-trampoline.c), which
//   later re-enters the suspended level's executor with the result.  Setting
//   the level's STATE byte prior to suspension is a common way of letting a
//   level know where to pick up from when it left off.
//
// * When it encounters something that needs to be handled as a function
//   application, it defers to %c-action.c for the Action_Executor().  The
//   action gets its own level.
//

#include "sys-core.h"


// Prefer these to XXX_Executor_Flag(EVAL) in this file (much faster!)

#define Get_Eval_Executor_Flag(L,name) \
    (((L)->flags.bits & EVAL_EXECUTOR_FLAG_##name) != 0)

#define Not_Eval_Executor_Flag(L,name) \
    (((L)->flags.bits & EVAL_EXECUTOR_FLAG_##name) == 0)

#define Set_Eval_Executor_Flag(L,name) \
    ((L)->flags.bits |= EVAL_EXECUTOR_FLAG_##name)

#define Clear_Eval_Executor_Flag(L,name) \
    ((L)->flags.bits &= ~EVAL_EXECUTOR_FLAG_##name)


// The level contains a "feed" whose ->value typically represents a "current"
// step in the feed.  But the evaluator is organized in a way that the
// notion of what is "current" can get out of sync with the feed.  An example
// would be when a SET-WORD! evaluates its right hand side, causing the feed
// to advance an arbitrary amount.
//
// So the Stepper_Executor() has its own state (in `u.eval`) to track the
// "current" position, and maintains the optional cache of what the fetched
// value of that is.  These macros help make the code less ambiguous.
//
#undef At_Level
#define L_next              cast(const Element*, L->feed->p)
#define L_next_gotten       L->feed->gotten
#define L_current_gotten    L->u.eval.current_gotten

#undef SCRATCH  // rename for its specific use in the evaluator
#define CURRENT  u_cast(Element*, &L->scratch)  // no executor check

#define level_ L  // for OUT, SPARE, STATE macros

// We make the macro for getting binding a bit more complex here, to account
// for reevaluation.
//
// https://forum.rebol.info/t/should-reevaluate-apply-let-bindings/1521
//
#undef L_binding
#define L_binding \
    (STATE == ST_STEPPER_REEVALUATING ? SPECIFIED : Level_Binding(L))


// !!! In earlier development, the Level* for evaluating across a block was
// reused for each action invocation.  Since no more than one action was
// running at a time, this seemed to work.  However, because "Levels" and
// "Frames" were conflated, there was concern that this would not give enough
// reified FRAME! objects to the user.  Now that Levels and Frames are
// distinct, this should be revisited.


#define Make_Action_Sublevel(parent) \
    Make_Level(&Action_Executor, (parent)->feed, LEVEL_FLAG_ERROR_RESULT_OK)


// When a SET-BLOCK! is being processed for multi-returns, it may encounter
// leading-SPACE chains as in ([foo :bar]: 10).  Once the work of extracting
// the real variable from the path is done and pushed to the stack, this bit
// is used to record that the variable was optional.  This makes it easier
// for the phase after the right hand side is evaluated--vs. making it pick
// apart the path again.
//
#define CELL_FLAG_STACK_NOTE_OPTIONAL CELL_FLAG_NOTE


//
// SET-WORD! and SET-TUPLE! want to do roughly the same thing as the first step
// of their evaluation.  They evaluate the right hand side into L->out.
//
// 1. Note that any infix quoting operators that would quote backwards to see
//    the `x:` would have intercepted it during a lookahead...pre-empting any
//    of this code.
//
// 2. Using a SET-XXX! means you always have at least two elements; it's like
//    an arity-1 function.  `1 + x: whatever ...`.  This overrides the no
//    lookahead behavior flag right up front.
//
INLINE Level* Maybe_Rightward_Continuation_Needed(Level* L)
{
    if (Is_Feed_At_End(L->feed))  // `eval [x:]`, `eval [o.x:]`, etc. illegal
        panic (Error_Need_Non_End(CURRENT));

    Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);  // always >= 2 elements [2]

    Flags flags =  // v-- if L was fulfilling, we are
        (L->flags.bits & EVAL_EXECUTOR_FLAG_FULFILLING_ARG)
        | LEVEL_FLAG_ERROR_RESULT_OK;  // trap [e: transcode "1&aa"] works

    Level* sub = Make_Level(
        &Stepper_Executor,
        L->feed,
        flags  // inert optimize adjusted the flags to jump in mid-eval
    );
    Push_Level_Erase_Out_If_State_0(OUT, sub);

    return sub;
}


//
//  Inert_Stepper_Executor: C
//
// This simplifies implementation of operators that can run in an "inert" mode:
//
//     >> any [1 + 2]
//     == 3
//
//     >> any @[1 + 2]
//     == 1
//
// Inert operations wind up costing a bit more because they push a Level when
// it seems "they don't need to".  But it means the code can be written in a
// regularized form that applies whether evaluations are done or not, and it
// handles all the things like locking the array from modification during the
// iteration, etc.
//
Bounce Inert_Stepper_Executor(Level* L)
{
    enum {
        ST_INERT_STEPPER_INITIAL_ENTRY = STATE_0,
        ST_INERT_STEPPER_FINISHED
    };

    assert(STATE == ST_INERT_STEPPER_INITIAL_ENTRY);
    STATE = ST_INERT_STEPPER_FINISHED;  // can't leave as STATE_0

    if (Is_Feed_At_End(L->feed))
        return Init_Endlike_Trash(OUT);

    Derelativize(OUT, At_Feed(L->feed), Feed_Binding(L->feed));
    Fetch_Next_In_Feed(L->feed);
    return Liftify(OUT);
}


//
//  Stepper_Executor: C
//
// Expression execution can be thought of as having four distinct states:
//
//    * new_expression
//    * evaluate
//    * lookahead
//    * finished -or- threw
//
// It is possible to preload states and start an evaluator at any of these.
//
Bounce Stepper_Executor(Level* L)
{
    if (THROWING)
        return THROWN;  // no state to clean up

    assert(TOP_INDEX >= STACK_BASE);  // e.g. REDUCE accrues
    assert(OUT != SPARE);  // overwritten by temporary calculations

    // Given how the evaluator is written, it's inevitable that there will
    // have to be a test for points to `goto` before running normal eval.
    // This cost is paid on every entry to Eval_Core().
    //
    switch (STATE) {
      case ST_STEPPER_INITIAL_ENTRY:
        goto start_new_expression;

      case ST_STEPPER_LOOKING_AHEAD:
        goto lookahead;

      case ST_STEPPER_REEVALUATING: {  // v-- IMPORTANT: Keep STATE
        //
        // It's important to leave STATE as ST_STEPPER_REEVALUATING
        // during the switch state, because that's how the evaluator knows
        // not to redundantly apply LET bindings.  See `L_binding` above.

        // Note: What if the re-evaluate functionality doesn't want to heed
        // the infix state in the action itself?
        //
        Erase_Cell(OUT);
        L_current_gotten = nullptr;  // !!! allow/require to be passed in?
        goto look_ahead_for_left_literal_infix; }

    #if (! DEBUG_DISABLE_INTRINSICS)
      intrinsic_lifted_arg_in_spare:
      case ST_STEPPER_CALCULATING_INTRINSIC_ARG: {
        Details* details = Ensure_Cell_Frame_Details(CURRENT);
        Dispatcher* dispatcher = Details_Dispatcher(details);

        assert(Is_Quoted(SPARE) or Is_Quasiform(SPARE));
        assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
        Set_Level_Flag(L, DISPATCHING_INTRINSIC);  // level_ is not its Level
        Bounce bounce = Apply_Cfunc(dispatcher, L);
        if (bounce == nullptr)
            Init_Nulled(OUT);
        else if (bounce == BOUNCE_OKAY)
            Init_Okay(OUT);
        else if (bounce == L->out) {
            if (Is_Error(OUT))
                return PANIC(Cell_Error(OUT));
        }
        else if (bounce == BOUNCE_BAD_INTRINSIC_ARG)
            return Native_Panic_Result(L, Error_Bad_Intrinsic_Arg_1(L));
        else {
            assert(bounce == BOUNCE_PANIC);  // no BOUNCE_CONTINUE, API handles
            return bounce;
        }

        Clear_Level_Flag(L, DISPATCHING_INTRINSIC);
        goto lookahead; }
      #endif

      case TYPE_GROUP:
        goto group_or_meta_group_result_in_out;

      case ST_STEPPER_SET_GROUP:
        goto set_group_result_in_spare;

      case ST_STEPPER_GENERIC_SET:
        goto generic_set_rightside_dual_in_out;

      case ST_STEPPER_SET_BLOCK:
        goto set_block_rightside_dual_in_out;

      case TYPE_FRAME:
        goto lookahead;

      case ST_STEPPER_TIE_EVALUATING_RIGHT_SIDE:
        goto tie_rightside_dual_in_out;

      case ST_STEPPER_APPROVE_EVALUATING_RIGHT_SIDE:
        goto approval_rightside_dual_in_out;

    #if RUNTIME_CHECKS
      case ST_STEPPER_FINISHED_DEBUG:
        assert(!"Stepper STATE not re-initialized after completion");
        break;
    #endif

      default:
        assert(false);
    }

  #if RUNTIME_CHECKS
    Evaluator_Expression_Checks_Debug(L);
  #endif


  start_new_expression: {  ///////////////////////////////////////////////////

    Sync_Feed_At_Cell_Or_End_May_Panic(L->feed);

    Update_Expression_Start(L);  // !!! See Level_Array_Index() for caveats

    if (Is_Feed_At_End(L->feed)) {
        assert(Is_Cell_Erased(OUT));
        Init_Endlike_Trash(OUT);
        STATE = ST_STEPPER_NONZERO_STATE;
        goto finished_dont_lift_out;
    }

    L_current_gotten = L_next_gotten;  // Lookback clears it
    Copy_Cell_Core(CURRENT, L_next, CELL_MASK_THROW);
    Fetch_Next_In_Feed(L->feed);


} look_ahead_for_left_literal_infix: { ///////////////////////////////////////

    // The first thing we do in an evaluation step has to be to look ahead for
    // any function that takes its left hand side literally.  Arrow functions
    // are a good example:
    //
    //     >> x: does [print "Running X the function"]
    //
    //     >> all [1 2 3] then x -> [print "Result of ALL was" x]
    //     Result of ALL was 3
    //
    // When we moved on from THEN to evaluate X, it had to notice that -> is
    // an infix function that takes its first argument literally.  That meant
    // running the X function is suppressed, and instead the X word! gets
    // passed as the first argument to ->
    //
    // 1. REEVALUATE jumps here.  Note that jumping to this label doesn't
    //    advance the expression index, so as far as error messages and such
    //    are concerned, `reeval x` will still start with `reeval`.
    //
    // 2. !!! Using L_binding here instead of Feed_Binding(L->feed) seems to
    //    break `let x: me + 1`, due to something about the conditionality on
    //    reevaluation.  L_binding's conditionality should be reviewed for
    //    relevance in the modern binding model.

    if (Is_Feed_At_End(L->feed) or Is_Comma(L_next))
        goto give_up_backward_quote_priority;

    assert(not L_next_gotten);  // Fetch_Next_In_Feed() cleared it

    if (QUOTE_BYTE(L_next) != NOQUOTE_1)  // quoted right can't look back
        goto give_up_backward_quote_priority;

    Option(InfixMode) infix_mode;
    Phase* infixed;

    switch (Heart_Of(L_next)) {  // words and chains on right may look back
      case TYPE_WORD: {
        L_next_gotten = Lookup_Word(
            L_next,
            Feed_Binding(L->feed)  // L_binding breaks here [2]
        );
        if (
            not L_next_gotten
            or not Is_Action(unwrap L_next_gotten)
            or not (infix_mode = Cell_Frame_Infix_Mode(unwrap L_next_gotten))
        ){
            goto give_up_backward_quote_priority;
        }
        infixed = Cell_Frame_Phase(unwrap L_next_gotten);
        break; }

      case TYPE_CHAIN:
        goto give_up_backward_quote_priority;  // should be enfixable!

      default:
        goto give_up_backward_quote_priority;
    }

    goto check_first_infix_parameter_class;


  check_first_infix_parameter_class: { ///////////////////////////////////////

    // 1. Lookback args are fetched from OUT, then copied into an arg slot.
    //    Put the backwards quoted value into OUT.  (Do this before next
    //    step because we need value for type check)
    //
    // 2. We make a special exemption for left-stealing arguments, when they
    //    have nothing to their right.  They lose their priority and we run
    //    the left hand side with them as a priority instead.  This lets us
    //    do (the ->) or (help of)

    Option(ParamClass) pclass = Get_First_Param_Literal_Class(infixed);
    if (not pclass)
        goto give_up_backward_quote_priority;

    if (pclass == PARAMCLASS_JUST)  // infix func ['x ...] [...]
        Copy_Cell(OUT, CURRENT);  // put left side in OUT [1]
    else {
        assert(
            pclass == PARAMCLASS_THE  // infix func [@x ...] [...]
            or pclass == PARAMCLASS_SOFT
        );
        Derelativize(OUT, CURRENT, L_binding);  // put left side in OUT [1]
    }

    L_current_gotten = L_next_gotten;
    Copy_Cell(CURRENT, L_next);  // CURRENT now invoking word (->-, OF, =>)
    Fetch_Next_In_Feed(L->feed);  // ...now skip that invoking word

    if (
        Is_Feed_At_End(L->feed)  // v-- OUT is what used to be on left
        and (
            Type_Of_Unchecked(OUT) == TYPE_WORD
            or Type_Of_Unchecked(OUT) == TYPE_PATH
        )
    ){  // exemption: put OUT back in CURRENT and CURRENT back in feed [2]
        Move_Atom(&L->feed->fetched, CURRENT);
        L->feed->p = &L->feed->fetched;
        L->feed->gotten = L_current_gotten;

        Move_Atom(CURRENT, cast(Element*, OUT));
        L_current_gotten = nullptr;

        Set_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH);

        if (Is_Word(CURRENT)) {
            STATE = cast(StepperState, TYPE_WORD);
            goto word_common;
        }

        assert(Is_Path(CURRENT));
        STATE = cast(StepperState, TYPE_PATH);
        goto path_common;
    }

    goto right_hand_literal_infix_wins;


} right_hand_literal_infix_wins: { ///////////////////////////////////////////

    Level* sub = Make_Action_Sublevel(L);
    Push_Action(sub, unwrap L_current_gotten);

    Option(const Symbol*) label = Is_Word(CURRENT)
        ? Cell_Word_Symbol(CURRENT)
        : Cell_Frame_Label_Deep(CURRENT);

    Begin_Action(sub, label, infix_mode);
    Push_Level_Erase_Out_If_State_0(OUT, sub);  // infix_mode sets state
    goto process_action;


}} give_up_backward_quote_priority: {

    assert(Is_Cell_Erased(OUT));

    if (QUOTE_BYTE(CURRENT) == NOQUOTE_1) {
        Option(Sigil) sigil = Sigil_Of(CURRENT);
        switch (maybe sigil) {
          case SIGIL_0:
            goto handle_plain;

          case SIGIL_META:  // ^ lifts the value
            goto handle_any_metaform;

          case SIGIL_PIN:  // @ pins the value
            goto handle_any_pinned;

          case SIGIL_TIE:  // $ ties the value
            goto handle_any_tied;
        }
    }

    if (QUOTE_BYTE(CURRENT) == QUASIFORM_2)
        goto handle_quasiform;

    assert(QUOTE_BYTE(CURRENT) != ANTIFORM_0);
    goto handle_quoted;


} handle_quoted: { //// QUOTED! [ 'XXX  '''@XXX  '~XXX~ ] ////////////////////

    // Quoted values drop one quote level.  Binding is left as-is.

    Copy_Cell(OUT, CURRENT);

    QUOTE_BYTE(OUT) -= Quote_Shift(1);
    STATE = cast(StepperState, TYPE_QUOTED);  // can't leave STATE_0

    goto lookahead;


} handle_quasiform: { //// QUASIFORM! ~XXX~ //////////////////////////////////

    // Quasiforms produce antiforms when they evaluate.  Binding is erased.
    //
    // 1. Not all quasiforms have legal antiforms.  For instance: while all
    //    WORD!s have quasiforms, only a few are allowed to become antiform
    //    keywords (e.g. ~null~ and ~okay~)

    Copy_Cell_Core(OUT, CURRENT, CELL_MASK_THROW);

    Option(Error*) e = Trap_Coerce_To_Antiform(OUT);  // may be illegal [1]
    if (e)
        return PANIC(unwrap e);

    STATE = cast(StepperState, TYPE_QUASIFORM);  // can't leave STATE_0
    goto lookahead;


} handle_any_pinned: { //// PINNED! (@XXX) ///////////////////////////////////

    // Type that just leaves the sigil:
    //
    //    >> @word
    //    == @word
    //
    // This offers some parity with the @ operator, which gives its next
    // argument back literally (used heavily in the API):
    //
    //    >> @ var:
    //    == var:
    //
    // Most of the datatypes use is in dialects, but the evaluator behavior
    // comes in handy for cases like passing a signal that reducing constructs
    // should not perform further reduction:
    //
    //    >> pack [1 + 2 10 + 20]
    //    == ~['3 '30]~  ; anti
    //
    //    >> pack @[1 + 2 10 + 20]
    //    == ~['1 '+ '2 '10 '+ '20]~  ; anti
    //
    // It also helps in cases like:
    //
    //    import @xml
    //    import @json/1.1.2
    //
    // Leaving the sigil means IMPORT can typecheck against a @WORD! + @PATH!
    // and not have a degree of freedom that it can't distinguish from being
    // called as (import 'xml) or (import 'json/1.1.2)

    if (Is_Sigil(CURRENT, SIGIL_PIN))
        goto handle_pin_sigil;  // special handling for lone @

    Inertly_Derelativize_Inheriting_Const(OUT, CURRENT, L->feed);
    goto lookahead;

} handle_pin_sigil: {  //// "PIN" Pinned Space Sigil (@) /////////////////////

    // @ acts like THE (literal, but bound):
    //
    //     >> abc: 10
    //
    //     >> word: @ abc
    //     == abc
    //
    //     >> get word
    //     == 10
    //
    // 2. There's a twist, that @ can actually handle antiforms if they are
    //    coming in via an API feed.  This is a convenience so you can write:
    //
    //        rebElide("append block opt @", value_might_be_null);
    //
    //     ...instead of:
    //
    //        rebElide("append block opt", rebQ(value_might_be_null));
    //
    //    If you consider the API to be equivalent to TRANSCODE-ing the
    //    given material into a BLOCK! and then EVAL-ing it, then this is
    //    creating an impossible situation of having an antiform in the
    //    block.  But the narrow exception limited to seeing such a sequence
    //    in the evaluator is considered worth it:
    //
    //      https://forum.rebol.info/t/why-isnt-a-precise-synonym-for-the/2215
    //
    // 3. We know all feed items with FEED_NOTE_META were synthesized in the
    //    feed and so it should be safe to tweak the flag.  Doing so lets us
    //    use The_Next_In_Feed() and Just_Next_In_Feed() which use At_Feed()
    //    that will error on FEED_NOTE_META to prevent the suspended-animation
    //    antiforms from being seen by any other part of the code.

    if (Is_Feed_At_End(L->feed))  // no literal to take as argument
        return PANIC(Error_Need_Non_End(CURRENT));

    assert(Not_Feed_Flag(L->feed, NEEDS_SYNC));
    const Element* elem = c_cast(Element*, L->feed->p);

    bool antiform = Get_Cell_Flag(elem, FEED_NOTE_META);  // [2]
    Clear_Cell_Flag(m_cast(Element*, elem), FEED_NOTE_META);  // [3]

    The_Next_In_Feed(L->out, L->feed);  // !!! review infix interop

    if (antiform)  // exception [2]
        Unliftify_Known_Stable(L->out);

    goto lookahead;


} handle_any_tied: { //// TIED! ($XXX) ///////////////////////////////////////

    // The $XXX types evaluate to remove the decoration, but be bound:
    //
    //     >> var: 1020
    //
    //     >> $var
    //     == var
    //
    //     >> get $var
    //     == 1020
    //
    // This is distinct from quoting the item, which would give you the item
    // undecorated but not changing the binding (usually resulting in unbound).
    //
    //     >> var: 1020
    //
    //     >> get 'var
    //     ** Error: var is unbound

    if (Is_Sigil(CURRENT, SIGIL_TIE))
        goto handle_tie_sigil;  // special handling for lone $

    Inertly_Derelativize_Inheriting_Const(OUT, CURRENT, L->feed);
    Plainify(u_cast(Element*, OUT));  // remove the $ Sigil
    goto lookahead;

} handle_tie_sigil: {  //// "TIE" Tied Space Sigil ($) ///////////////////////

    // The $ sigil will evaluate the right hand side, and then bind the
    // product into the current evaluator environment.

    Level* right = Maybe_Rightward_Continuation_Needed(L);
    if (not right)
        goto tie_rightside_dual_in_out;

    STATE = ST_STEPPER_TIE_EVALUATING_RIGHT_SIDE;
    return CONTINUE_SUBLEVEL(right);

} tie_rightside_dual_in_out: {

    Unliftify_Undecayed(OUT);
    if (Is_Antiform(OUT))
        return PANIC("$ operator cannot bind antiforms");

    Derelativize(SPARE, cast(Element*, OUT), Level_Binding(L));
    Copy_Cell(OUT, SPARE);  // !!! inefficient
    goto lookahead;


} handle_any_metaform: { //// META (^) ///////////////////////////////////////

    // METAFORM! types will LIFT variables on storage, and UNLIFT them on
    // fetching.  This is complex logic.

  switch (STATE = cast(Byte, Heart_Of(CURRENT))) {

  case TYPE_WORD: { //// META WORD! ^XXX /////////////////////////////////////

    goto handle_get_word;


} case TYPE_CHAIN: { //// META CHAIN! (^XXX: ^:XXX ...) //////////////////////

    goto handle_chain_or_meta_chain;


} case TYPE_GROUP: { //// META GROUP! ^(...) /////////////////////////////////

    goto handle_group_or_meta_group;


} case TYPE_BLOCK: { //// META BLOCK! ^[...] /////////////////////////////////

    // Produces a PACK! of what it is given:
    //
    //    >> ^[1 + 2 null]
    //    == ~['3 ~null~]~  ; anti
    //
    // This is the most useful meaning, and it round trips the values:
    //
    //    >> ^[a b]: ^[1 + 2 null]
    //    == ~['3 ~null~]~
    //
    //    >> a
    //    == 3
    //
    //    >> b
    //    == ~null~  ; anti

    Element* out = Inertly_Derelativize_Inheriting_Const(OUT, CURRENT, L->feed);
    HEART_BYTE(out) = TYPE_BLOCK;
    Quotify(out);  // !!! was quoting, to avoid binding?

    Element* spare = Init_Word(SPARE, CANON(PACK));
    dont(Quotify(Known_Element(SPARE)));  // want to run word

    Value* temp = rebLift_helper(
        Level_Binding(L),
        spare, out, rebEND
    );
    Copy_Cell(OUT, temp);
    rebRelease(temp);

    Unliftify_Undecayed(OUT);
    goto lookahead;


} case TYPE_FENCE: { //// META FENCE! ^{...} /////////////////////////////////

    return PANIC("Don't know what ^FENCE! is going to do yet");


} case TYPE_RUNE: { //// META RUNE! /////////////////////////////////////////

    if (Is_Sigil(CURRENT, SIGIL_META))
        goto handle_action_approval_sigil;  // special handling for lone ^

    return PANIC("Don't know what ^RUNE! is going to do yet (besides ^)");

} handle_action_approval_sigil: {  //// "APPROVE" Meta Space Sigil (^) ///////

    // [^] is used to turn "surprising actions" into "unsurprising actions".
    //
    // See CELL_FLAG_OUT_HINT_UNSURPRISING for more details.

    Level* right = Maybe_Rightward_Continuation_Needed(L);
    if (not right)
        goto approval_rightside_dual_in_out;

    STATE = ST_STEPPER_APPROVE_EVALUATING_RIGHT_SIDE;
    return CONTINUE_SUBLEVEL(right);

} approval_rightside_dual_in_out: {

    // 1. It does not also turn "surprising ghosts" into "unsurprising ghosts",
    //    because that would conflate the functionality to where if you used ^
    //    you could either be saying "I approve this as an action" or "I want
    //    to allow the entire structure of this code to be disrupted".  Ghosts
    //    are the lower-priority feature, so they are not affected by the ^.
    //
    //    (Possibly ^ should turn surprising ghosts into voids, but it should
    //    definitely not turn surprising ghosts into unsurprising ghosts.)

    Unliftify_Undecayed(OUT);
    if (Is_Atom_Action(OUT))  // don't do ghosts, just actions [1]
        Set_Cell_Flag(OUT, OUT_HINT_UNSURPRISING);  // see flag notes
    goto lookahead;


} default: { /////////////////////////////////////////////////////////////////

    return PANIC(
        "Only ^WORD!, ^GROUP, ^BLOCK! eval at this time for METAFORM!"
    );

  }} // end switch()


} handle_plain: { //// *** THIS IS THE "MAIN" SWITCH STATEMENT *** ///////////

    // This switch is done with a case for all TYPE_XXX values, in order to
    // facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
    //
    // Subverting the jump table optimization with specialized branches for
    // fast tests like Any_Inert() and IS_NULLED_OR_VOID_OR_END() has shown
    // to reduce performance in practice.  The compiler does the right thing.
    //
    // 1. The Stepper_Executor()'s state bytes are a superset of the
    //    Heart_Of() of processed values.  See the ST_STEPPER_XXX enumeration.

  switch ((STATE = cast(Byte, Heart_Of(CURRENT)))) {  // superset [1]

  case TYPE_COMMA: { //// COMMA! , ///////////////////////////////////////////

    // A comma is a lightweight looking expression barrier.  Though it acts
    // something like COMMENT or ELIDE, it does not evaluate to a "ghost"
    // state.  It just errors on evaluations that aren't interstitial, or
    // gets skipped over otherwise.
    //
    //   https://forum.rebol.info/t/1387/6

    if (Get_Eval_Executor_Flag(L, FULFILLING_ARG))
        return PANIC(Error_Expression_Barrier_Raw());

    goto start_new_expression;


} case TYPE_FRAME: { //// FRAME! /////////////////////////////////////////////

    // If a FRAME! makes it to the SWITCH statement, that means it is either
    // literally a frame in the array (eval compose [(unrun :add) 1 2]) or
    // is being retriggered via REEVAL.
    //
    // Most FRAME! evaluations come from the antiform ("actions") triggered
    // from a WORD! or PATH! case.)
    //
    // 1. If an infix function is run at this moment, it will not have a left
    //    hand side argument.

    if (Cell_Frame_Lens(CURRENT))  // running frame if lensed
        return PANIC("Use REDO to restart a running FRAME! (can't EVAL)");

    Level* sub = Make_Action_Sublevel(L);
    Push_Action(sub, CURRENT);
    Option(InfixMode) infix_mode = Cell_Frame_Infix_Mode(CURRENT);
    assert(Is_Cell_Erased(OUT));  // so nothing on left [1]
    Begin_Action(sub, Cell_Frame_Label_Deep(CURRENT), infix_mode);
    Push_Level_Erase_Out_If_State_0(OUT, sub);  // infix_mode sets state

    goto process_action;

} process_action: {

    // Gather args and execute function (the arg gathering makes nested
    // eval calls that lookahead, but no lookahead after the action runs)

    STATE = cast(StepperState, TYPE_FRAME);
    return CONTINUE_SUBLEVEL(TOP_LEVEL);


} case TYPE_WORD: { //// WORD! ///////////////////////////////////////////////

    // A plain word tries to fetch its value through its binding.  It panics
    // if the word is unbound (or if the binding is to a variable which is
    // set, but to the antiform of space e.g. TRASH).  Should the word
    // look up to an antiform FRAME!, then that "Action" will be invoked.
    //
    // NOTE: The usual dispatch of infix functions is *not* via a TYPE_WORD in
    // this switch, it's by some code at the `lookahead:` label.  You only see
    // infix here when there was nothing to the left, so cases like `(+ 1 2)`
    // or in "stale" left hand situations like `10 comment "hi" + 20`.
    //
    // 1. When dispatching infix and you have something on the left, you
    //    want to push the level *after* the flag for infixness has been
    //    set...to avoid overwriting the output cell that's the left hand
    //    side input.  But in this case we don't have a left input, even
    //    though we're doing infix.  So pushing *before* we set the flags
    //    means the FLAG_STATE_BYTE() will be 0, and we get clearing.

} word_common: {

    Sink(Value) out = OUT;
    Option(Error*) error = Trap_Get_Any_Word(out, CURRENT, L_binding);
    if (error)
        return PANIC(unwrap error);  // don't conflate with function result

    if (Is_Action(out))
        goto run_action_in_out;

    if (Get_Cell_Flag(CURRENT, CURRENT_NOTE_RUN_WORD)) {
        if (Is_Frame(out))
            goto run_action_in_out;
        return PANIC("Leading slash means execute FRAME! or ACTION! only");
    }

    if (Is_Trash(out))  // checked second
        return PANIC(Error_Bad_Word_Get(CURRENT, out));

    goto lookahead;

} run_action_in_out: {

    Value* out = cast(Value*, OUT);
    Option(InfixMode) infix_mode = Cell_Frame_Infix_Mode(out);
    const Symbol* label = Cell_Word_Symbol(CURRENT);  // use WORD!

    if (infix_mode) {
        if (infix_mode != INFIX_TIGHT) {  // defer or postpone
            if (Get_Eval_Executor_Flag(L, FULFILLING_ARG)) {
                Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
                Set_Feed_Flag(L->feed, DEFERRING_INFIX);
                goto finished;
            }
        }
    }

    if (Get_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH)) {
        if (infix_mode)
            assert(false);  // !!! this won't work, can it happen?

        Clear_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH);
    }

  #if (! DEBUG_DISABLE_INTRINSICS)
    Details* details = maybe Try_Cell_Frame_Details(out);
    if (
        not infix_mode  // too rare a case for intrinsic optimization
        and details
        and Get_Details_Flag(details, CAN_DISPATCH_AS_INTRINSIC)
        and Not_Level_At_End(L)  // can't do <end>, fallthru to error
        and not SPORADICALLY(10)  // checked builds sometimes bypass
    ){
        Option(VarList*) coupling = Cell_Frame_Coupling(out);
        Init_Frame(
            CURRENT,
            details,
            label,
            coupling
        );
        Param* param = Phase_Param(details, 1);
        Flags flags = EVAL_EXECUTOR_FLAG_FULFILLING_ARG;

        switch (Cell_Parameter_Class(param)) {
          case PARAMCLASS_NORMAL:
            break;

          case PARAMCLASS_META:
            flags |= LEVEL_FLAG_ERROR_RESULT_OK;
            break;

          case PARAMCLASS_JUST:
            Just_Next_In_Feed(SPARE, L->feed);
            Liftify(SPARE);
            goto intrinsic_lifted_arg_in_spare;

          case PARAMCLASS_THE:
            The_Next_In_Feed(SPARE, L->feed);
            Liftify(SPARE);
            goto intrinsic_lifted_arg_in_spare;

          default:
            return PANIC("Unsupported Intrinsic parameter convention");
        }

        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);  // when non-infix call

        Level* sub = Make_Level(&Stepper_Executor, L->feed, flags);
        Push_Level_Erase_Out_If_State_0(SPARE, sub);
        STATE = ST_STEPPER_CALCULATING_INTRINSIC_ARG;
        return CONTINUE_SUBLEVEL(sub);
    }
  #endif

    Level* sub = Make_Action_Sublevel(L);
    Push_Action(sub, out);
    Push_Level_Erase_Out_If_State_0(OUT, sub);  // *always* clear out
    Begin_Action(sub, label, infix_mode);
    unnecessary(Push_Level_Erase_Out_If_State_0(OUT, sub)); // see [1]

    goto process_action;


} handle_chain_or_meta_chain:  //// CHAIN! [ a:  ^a:  b:c:d  ^:e ] /////////
  case TYPE_CHAIN: {

    // Due to the consolidation of all the SET-XXX! and GET-XXX! types as
    // CHAIN! with leading or trailing blanks, CHAIN! has to break that down
    // and dispatch to the appropriate behavior.
    //
    // 1. There's a weird feature of binding being sensitive to SET-WORD!s
    //    only that is currently broken.  When we convert a SET-WORD to a
    //    WORD! to do the assign, that feature is lost.

    switch (Try_Get_Sequence_Singleheart(CURRENT)) {
      case NOT_SINGLEHEART_0:
        break;  // wasn't xxx: or :xxx where xxx is BLOCK!/CHAIN!/WORD!/etc

      case TRAILING_SPACE_AND(WORD): {  // FOO: or ^FOO:
        Copy_Cell(CURRENT, Derelativize(SPARE, CURRENT, L_binding));
        if (Any_Metaform(CURRENT)) {  // ^foo: -> ^foo
            Plainify(CURRENT);
            Unchain(CURRENT);
            Metafy(CURRENT);
        }
        else
            Unchain(CURRENT);  // foo: -> foo
        goto handle_generic_set; }

      case TRAILING_SPACE_AND(TUPLE):  // a.b.c: is a set tuple
        Unchain(CURRENT);
        assert(Is_Tuple(CURRENT));
        goto handle_generic_set;

      case TRAILING_SPACE_AND(BLOCK):  // [a b]: multi-return assign
        Unchain(CURRENT);
        STATE = ST_STEPPER_SET_BLOCK;
        goto handle_set_block;

      case TRAILING_SPACE_AND(GROUP): {  // (xxx): -- generic retrigger set
        Unchain(CURRENT);
        L_next_gotten = nullptr;  // arbitrary code changes fetched vars
        Level* sub = Make_Level_At_Inherit_Const(
            &Evaluator_Executor,
            CURRENT,
            L_binding,
            LEVEL_MASK_NONE
        );
        Init_Void(Evaluator_Primed_Cell(sub));
        Push_Level_Erase_Out_If_State_0(SPARE, sub);
        STATE = ST_STEPPER_SET_GROUP;
        return CONTINUE_SUBLEVEL(sub); }

      case LEADING_SPACE_AND(WORD):  // :FOO, refinement, error on eval?
        Unchain(CURRENT);
        STATE = ST_STEPPER_GET_WORD;
        assert(!":WORD! meaning is likely to become TRY WORD!");
        goto handle_get_word;

      case LEADING_SPACE_AND(TUPLE):  // :a.b.c -- what will this do?
        Unchain(CURRENT);
        STATE = ST_STEPPER_GET_TUPLE;
        assert(!":TUPLE! meaning is likely to become TRY TUPLE!");
        goto handle_get_tuple;

      case LEADING_SPACE_AND(BLOCK):  // !!! :[a b] reduces, not great...
        Unchain(CURRENT);
        Derelativize(SPARE, CURRENT, L_binding);
        if (rebRunThrows(
            cast(Value*, OUT),  // <-- output, API won't make atoms
            CANON(REDUCE), SPARE
        )){
            goto return_thrown;
        }
        goto lookahead;

      case LEADING_SPACE_AND(GROUP):
        Unchain(CURRENT);
        return PANIC("GET-GROUP! has no evaluator meaning at this time");

      default:  // it's just something like :1 or <tag>:
        return PANIC("No current evaluation for things like :1 or <tag>:");
    }

    Sink(Value) out = OUT;
    Option(Error*) error = Trap_Get_Chain_Push_Refinements(
        out,  // where to write action
        SPARE,  // temporary GC-safe scratch space
        CURRENT,
        L_binding
    );
    if (error)  // lookup failed, a GROUP! in path threw, etc.
        return PANIC(unwrap error);  // don't definitional error for now

    assert(Is_Action(out));

    if (Is_Cell_Frame_Infix(out)) {  // too late, left already evaluated
        Drop_Data_Stack_To(STACK_BASE);
        return PANIC("Use `->-` to shove left infix operands into CHAIN!s");
    }

} handle_action_in_out_with_refinements_pushed: {

    Value* out = cast(Value*, OUT);

    Level* sub = Make_Action_Sublevel(L);
    sub->baseline.stack_base = STACK_BASE;  // refinements

    Option(const Symbol*) label = Cell_Frame_Label_Deep(out);

    Push_Action(sub, out);
    Begin_Action(sub, label, PREFIX_0);  // not infix so, sub state is 0
    Push_Level_Erase_Out_If_State_0(OUT, sub);
    goto process_action;


} handle_get_word: {  // jumps here for CHAIN! that's like a GET-WORD!

    // A GET-WORD! gives you the contents of a variable as-is, with no
    // dispatch on functions.  This includes antiforms.
    //
    // https://forum.rebol.info/t/1301

    assert(
        (STATE == ST_STEPPER_GET_WORD and Is_Word(CURRENT))
        or (
            STATE == cast(StepperState, TYPE_WORD)
            and Is_Metaform(WORD, CURRENT)
        )
    );
    Option(Error*) error = Trap_Get_Any_Word_Maybe_Trash(
        OUT,
        CURRENT,
        L_binding
    );
    if (error)
        return PANIC(unwrap error);

    if (Is_Metaform(WORD, CURRENT)) {
        if (not Any_Lifted(OUT))
            return PANIC("^WORD! can only UNMETA quoted/quasiform");
        Unliftify_Undecayed(OUT);
    }

    goto lookahead;


} case TYPE_GROUP: //// GROUP! (...) /////////////////////////////////////////
  handle_group_or_meta_group: {

    // Groups simply evaluate their contents, and can evaluate to GHOST! if
    // the contents completely disappear.
    //
    // 1. If you say just `()` then that creates an "unsurprising ghost":
    //
    //        >> eval [1 + 2 ()])
    //        == 3
    //
    //    If more unsurprising ghosts vaporize in the Evaluator_Executor(),
    //    the result that will fall out is that original unsurprising ghost:
    //
    //        >> 1 + 2 (elide "we all vanish" comment "unsurprisingly")
    //        we all vanish
    //        == 3

    L_next_gotten = nullptr;  // arbitrary code changes fetched variables

    Flags flags = LEVEL_FLAG_ERROR_RESULT_OK;

    Level* sub = Make_Level_At_Inherit_Const(
        &Evaluator_Executor,
        CURRENT,
        L_binding,
        flags
    );

    Atom* primed = Evaluator_Primed_Cell(sub);
    Init_Unsurprising_Ghost(primed);  // want to vaporize if all ghosts [1]

    Push_Level_Erase_Out_If_State_0(OUT, sub);
    return CONTINUE_SUBLEVEL(sub);

} group_or_meta_group_result_in_out: {

    // 1. As a mitigation of making people write (x: ^ ^arg), we allow for
    //    you to instead write (x: ^(arg)) and it will assume the ^.  This
    //    is maybe a bit random but it makes the code look better.

    if (Is_Group(CURRENT))
        goto lookahead;  // not decayed, result is good

    assert(Is_Metaform(GROUP, CURRENT));

    if (not Any_Lifted(OUT))
        return PANIC("^GROUP! can only UNLIFT quoted/quasiforms");

    Unliftify_Undecayed(OUT);  // GHOST! legal, ACTION! legal...
    Set_Cell_Flag(OUT, OUT_HINT_UNSURPRISING);  // just lifted approve [1]
    goto lookahead;


} case TYPE_TUPLE: { //// TUPLE! [ a.  b.c.d  .e ] ///////////////////////////

    // TUPLE! runs through an extensible mechanism based on PICK and POKE.
    // Hence `a.b.c` is kind of like a shorthand for `pick (pick a 'b) 'c`.
    //
    // In actuality, the mechanism is more sophisticated than that...because
    // some picking does "sub-value" addressing.  For more details, see the
    // explanation in %sys-pick.h
    //
    // For now, we defer to what GET does.
    //
    // Tuples looking up to nothing (~ antiform) are handled consistently with
    // WORD! and GET-WORD!, and will error...directing you use GET:ANY if
    // fetching nothing is what you actually intended.

    Element* spare = Copy_Sequence_At(SPARE, CURRENT, 0);
    bool blank_at_head = Is_Space(spare);
    if (
        not blank_at_head  // `.a` means pick member from "self"
        and Any_Inert(spare)  // `1.2.3` is inert
    ){
        Derelativize(OUT, CURRENT, L_binding);
        goto lookahead;
    }

    Sink(Value) out = OUT;
    Option(Error*) error = Trap_Get_Tuple(  // vacant will cause error
        out,
        GROUPS_OK,
        CURRENT,
        L_binding
    );
    if (error) {  // tuples never run actions, erroring won't conflate
        Init_Warning(OUT, unwrap error);
        Failify(OUT);
        goto lookahead;  // e.g. EXCEPT might want error
    }

    if (Is_Action(out))
        assert(Not_Cell_Flag(out, OUT_HINT_UNSURPRISING));

    goto lookahead;


} case TYPE_PATH: { //// PATH! [ a/  b/c/d  e/ ] /////////////////////////////

    // Ren-C moved to member access with "dots instead of slashes" (TUPLE!)
    // and refinements are done with "colons instead of slashes" (CHAIN!).
    // So PATH!s role has come to be specificially dealing with functions:
    //
    // * abc/     - means ABC is a function, return it as-is
    // * /abc     - means ensure ABC is a function and run it
    // * abc/def  - means ABC is a context, DEF is a function, run it
    // * abc/def/ - means ABC and DEF are functions, compose them
    //
    // 1. It's likely that paths like 1/2 or otherwise inert-headed will be
    //    inert and evaluate to themselves.
    //
    // 2. Slash at head will signal running actions soon enough.  But for the
    //    moment it is still refinement.  Let's try not binding it by default
    //    just to see what headaches that causes...if any.

} path_common: {

    bool slash_at_head;
    bool slash_at_tail;
    Option(SingleHeart) single = Try_Get_Sequence_Singleheart(CURRENT);

    if (not single) {
        Element* spare = Copy_Sequence_At(SPARE, CURRENT, 0);
        if (Any_Inert(spare)) {
            if (Is_Space(spare))
                slash_at_head = true;
            else {
                Derelativize(OUT, CURRENT, L_binding);  // inert [2]
                goto lookahead;
            }
        }
        else
            slash_at_head = false;

        Length len = Cell_Sequence_Len(CURRENT);
        spare = Copy_Sequence_At(SPARE, CURRENT, len - 1);
        slash_at_tail = Is_Space(spare);
    }
    else switch (unwrap single) {
      case LEADING_SPACE_AND(WORD):
        Unpath(CURRENT);
        Set_Cell_Flag(CURRENT, CURRENT_NOTE_RUN_WORD);
        goto word_common;

      case LEADING_SPACE_AND(CHAIN): {  // /abc: or /?:?:?
        Unpath(CURRENT);

        switch (Try_Get_Sequence_Singleheart(CURRENT)) {
          case TRAILING_SPACE_AND(WORD):  // /abc: is set actions only
            Unchain(CURRENT);
            Set_Cell_Flag(CURRENT, CURRENT_NOTE_SET_ACTION);
            goto handle_generic_set;

          case TRAILING_SPACE_AND(TUPLE):  // /a.b.c: is set actions only
            Unchain(CURRENT);
            Set_Cell_Flag(CURRENT, CURRENT_NOTE_SET_ACTION);
            goto handle_generic_set;

          default:
            return PANIC("/a:b:c will guarantee a function call, in time");
        }
        break; }

      default:
        slash_at_tail = Singleheart_Has_Trailing_Space(unwrap single);
        slash_at_head = Singleheart_Has_Leading_Space(unwrap single);
        assert(slash_at_head == not slash_at_tail);
        break;
    }

  delegate_to_get_path: {

    // 1. It would not make sense to return a definitional error when a path
    //    lookup does not exist.  Imagine making null back for `try lib/append`
    //    if you wrote `try lib/append [a b c] [d e]` when lib/append did not
    //    exist--that's completely broken.
    //
    // 2. Since paths with trailing slashes just return the action as-is, it's
    //    an arity-0 operation.  So returning a definitional error isn't
    //    complete nonsense, but still might not be great.  Review the choice.
    //
    // 3. Trailing slash notation is a particularly appealing way of denoting
    //    that something is an action, and that you'd like to fetch it in a
    //    way that does not take arguments:
    //
    //         /for-next: specialize for-skip/ [skip: 1]
    //         ;                         ---^
    //         ; slash helps show block is not argument
    //
    // 4. The left hand side does not look ahead at paths to find infix
    //    functions.  This is because PATH! dispatch is costly and can error
    //    in more ways than sniffing a simple WORD! for infix can.  So the
    //    prescribed way of running infix with paths is `left ->- right/side`,
    //    which uses an infix WORD! to mediate the interaction.

    Sink(Value) out = OUT;
    Option(Error*) error = Trap_Get_Path_Push_Refinements(
        out,  // where to write action
        SPARE,  // temporary GC-safe scratch space
        CURRENT,
        L_binding
    );
    if (error) {  // lookup failed, a GROUP! in path threw, etc.
        if (not slash_at_tail)
            return PANIC(unwrap error);  // RAISE error would conflate [1]
        return PANIC(unwrap error);  // don't RAISE error for now [2]
    }

    assert(Is_Action(out));
    if (slash_at_tail) {  // do not run action, just return it [3]
        if (STACK_BASE != TOP_INDEX) {
            if (Specialize_Action_Throws(
                SPARE, out, nullptr, STACK_BASE
            )){
                goto return_thrown;
            }
            Move_Atom(OUT, SPARE);
        }
        Set_Cell_Flag(OUT, OUT_HINT_UNSURPRISING);  // foo/ is always ACTION!
        goto lookahead;
    }

    if (Is_Cell_Frame_Infix(out)) {  // too late, left already evaluated [4]
        Drop_Data_Stack_To(STACK_BASE);
        return PANIC("Use `->-` to shove left infix operands into PATH!s");
    }

    UNUSED(slash_at_head);  // !!! should e.g. enforce /1.2.3 as warning?
    goto handle_action_in_out_with_refinements_pushed;

}} handle_generic_set: {

    // Right side is evaluated into `out`, and then copied to the variable.
    //
    // !!! The evaluation ordering is dictated by the fact that there isn't a
    // separate "evaluate path to target location" and "set target' step.
    // This is because some targets of assignments (e.g. gob.size.x:) do not
    // correspond to a cell that can be returned; the path operation "encodes
    // as it goes" and requires the value to set as a parameter.  Yet it is
    // counterintuitive given the "left-to-right" nature of the language:
    //
    //     >> foo: make object! [[bar][bar: 10]]
    //
    //     >> foo.(print "left" 'bar): (print "right" 20)
    //     right
    //     left
    //     == 20
    //
    // 1. Running functions flushes the L_next_gotten cache.  But a plain
    //    assignment can cause trouble too:
    //
    //        >> x: <before> x: 1 x
    //                            ^-- x value was cached in infix lookahead
    //
    //    It used to not be a problem, when variables didn't just pop into
    //    existence.  Reconsidered in light of "emergence".  Review.
    //
    // * Antiform assignments are allowed: https://forum.rebol.info/t/895/4

    assert(
        Is_Word(CURRENT) or Is_Metaform(WORD, CURRENT)
        or Is_Tuple(CURRENT)
        or Is_Lifted_Void(CURRENT)
    );
    STATE = ST_STEPPER_GENERIC_SET;

    Level* right = Maybe_Rightward_Continuation_Needed(L);
    if (not right)
        goto generic_set_rightside_dual_in_out;

    return CONTINUE_SUBLEVEL(right);

} generic_set_rightside_dual_in_out: {

    if (Is_Lifted_Void(CURRENT)) {  // e.g. `(void): ...`
        Unliftify_Undecayed(OUT);  // !!! do this with space VAR instead
        goto lookahead;  // pass through everything
    }

    Derelativize(SPARE, CURRENT, L_binding);  // !!! workaround !!! FIX !!!
    Move_Atom(CURRENT, SPARE);

    if (Set_Var_In_Scratch_To_Unlift_Out_Uses_Spare_Throws(
        LEVEL, GROUPS_OK, LIB(POKE_P)
    )){
        goto return_thrown;
    }

    // assignments of /foo: or /obj.field: require action
    //
    // !!! This is too late, needs to be folded in with Set_Var...()
    // But we don't pre-decay, so have to do it here for now.
    //
    if (Get_Cell_Flag(CURRENT, CURRENT_NOTE_SET_ACTION)) {
        if (not Is_Atom_Action(OUT))
            return PANIC(
                "/word: and /obj.field: assignments require Action"
            );
    }

    L_next_gotten = nullptr;  // cache can tamper with lookahead [1]

    goto lookahead;

} set_group_result_in_spare: {

    assert(L_current_gotten == nullptr);

    if (Is_Void(SPARE)) {
        Init_Lifted_Void(CURRENT);  // can't put voids in feed position
        goto handle_generic_set;
    }
    else switch (Type_Of(SPARE)) {
      case TYPE_BLOCK :
        Copy_Cell(CURRENT, cast(Element*, SPARE));
        STATE = ST_STEPPER_SET_BLOCK;
        goto handle_set_block;

      case TYPE_WORD :
        Copy_Cell(CURRENT, cast(Element*, SPARE));
        goto handle_generic_set;

      case TYPE_TUPLE :
        Copy_Cell(CURRENT, cast(Element*, SPARE));
        goto handle_generic_set;

      default:
        return PANIC("Unknown type for use in SET-GROUP!");
    }
    goto lookahead;

} handle_get_tuple: {

    // Note that the GET native on a TUPLE! won't allow GROUP! execution:
    //
    //    foo: [X]
    //    path: 'foo.(print "side effect!" 1)
    //    get path  ; not allowed, due to surprising side effects
    //
    // However a source-level GET-TUPLE! allows them, since they are at the
    // callsite and you are assumed to know what you are doing:
    //
    //    :foo.(print "side effect" 1)  ; this is allowed
    //
    // Consistent with GET-WORD!, a GET-TUPLE! won't allow nothing access on
    // the plain (unfriendly) forms.

    assert(STATE == ST_STEPPER_GET_TUPLE and Is_Tuple(CURRENT));

    Option(Error*) error = Trap_Get_Tuple_Maybe_Trash(
        OUT,
        GROUPS_OK,
        CURRENT,
        L_binding
    );
    if (error) {
        Init_Warning(OUT, unwrap error);
        Failify(OUT);
        goto lookahead;  // e.g. EXCEPT might want to see error
    }

    goto lookahead;


} handle_set_block: {

    // The evaluator treats SET-BLOCK! specially as a means for implementing
    // multiple return values.  It unpacks antiform blocks into components.
    //
    //     >> pack [1 2]
    //     == ~['1 '2]~  ; anti
    //
    //     >> [a b]: pack [1 2]
    //     == 1
    //
    //     >> a
    //     == 1
    //
    //     >> b
    //     == 2
    //
    // If a component is optional (e.g. the pack is too short to provide it),
    // it can be marked as a refinement.
    //
    //     >> [a b]: pack [1]
    //     ** Error: pack doesn't have enough values to set b
    //
    //     >> [a :b]: pack [1]
    //     == 1
    //
    //     >> b
    //     == ~null~  ; anti
    //
    // It supports `_` in slots whose results you don't want to ask for, `#`
    // in slots you want to ask for (but don't want to name), will evaluate
    // GROUP!s, and also allows FENCE! to {circle} which result you want to be
    // the overall result of the expression (defaults to passing through the
    // entire pack).
    //
    // 1. Empty SET-BLOCK! are not supported, although it could be argued
    //    that an empty set-block could receive a VOID (~[]~) pack.

    assert(STATE == ST_STEPPER_SET_BLOCK and Is_Block(CURRENT));

    if (Cell_Series_Len_At(CURRENT) == 0)  // not supported [1]
        return PANIC("SET-BLOCK! must not be empty for now.");

    const Element* tail;
    const Element* check = Cell_List_At(&tail, CURRENT);
    Context* check_binding = Derive_Binding(L_binding, CURRENT);

    // we've extracted the array at and tail, can reuse current now

    Option(StackIndex) circled = 0;

for (; check != tail; ++check) {  // push variables

    // We pre-process the SET-BLOCK! first and collect the variables to write
    // on the stack.  (It makes more sense for any GROUP!s in the set-block to
    // be evaluated on the left before the right.)
    //
    // !!! Should the block be locked while the advancement happens?  It
    // wouldn't need to be since everything is on the stack before code is run
    // on the right...but it might reduce confusion.

    if (Is_Quoted(check))
        return PANIC("QUOTED? not currently permitted in SET-BLOCK!s");

    bool circle_this;

    if (not Is_Fence(check)) {  // not "circled"
        circle_this = false;
        Derelativize(CURRENT, check, check_binding);  // same heart
        goto circle_detection_finished;
    }

  handle_fence_in_set_block: {  // "circled"

    // By default, the evaluation product of a SET-BLOCK expression is what
    // the right hand side was (e.g. an entire pack).  But {xxx} indicates a
    // desire to pick a specific unpacked result as the return.
    //
    //     >> [a b]: pack [1 2]
    //     == ~['1 '2]~  ; anti
    //
    //     >> [a {b}]: pack [1 2]
    //     == 2

    if (circled)
        return PANIC("Can only {Circle} one multi-return result");

    Length len_at = Cell_Series_Len_At(check);
    if (len_at == 1) {
        Derelativize(
            CURRENT,
            Cell_List_Item_At(check),
            check_binding
        );
    }
    else  // !!! should {} be a synonym for {_}?
        return PANIC("{Circle} only one element in multi-return");

    circle_this = true;

} circle_detection_finished: {

    bool is_optional;

    if (not Is_Chain(CURRENT)) {
        is_optional = false;
        goto optional_detection_finished;
    }

  handle_chain_in_set_block: {

    Option(SingleHeart) single;
    if (
        not (single = Try_Get_Sequence_Singleheart(CURRENT))
        or not Singleheart_Has_Leading_Space(unwrap single)
    ){
        return PANIC(
            "Only leading SPACE CHAIN! in SET BLOCK! dialect"
        );
    }
    Unchain(CURRENT);
    is_optional = true;

} optional_detection_finished: {

    if (
        Is_Group(CURRENT)
        or Is_Pinned(GROUP, CURRENT)
        or Is_Metaform(GROUP, CURRENT)
    ){
        if (Eval_Any_List_At_Throws(SPARE, CURRENT, SPECIFIED)) {
            Drop_Data_Stack_To(STACK_BASE);
            goto return_thrown;
        }
        if (Is_Void(SPARE) and Is_Group(CURRENT)) {
            Init_Quasar(PUSH());  // [(void)]: ... pass thru
        }
        else {
            Value* spare = Decay_If_Unstable(SPARE);
            if (Is_Antiform(spare))
                return PANIC(Error_Bad_Antiform(spare));

            if (Is_Pinned(GROUP, CURRENT))
                Pinify(Known_Element(spare));  // add @ decoration
            else {
                assert(Is_Metaform(GROUP, CURRENT));
                Metafy(Known_Element(spare));  // add ^ decoration
            }
            Copy_Cell(PUSH(), spare);
        }
    }
    else
        Copy_Cell(PUSH(), CURRENT);

    UNUSED(*CURRENT);  // look at stack top now

    if (is_optional)  // so next phase won't worry about leading slash
        Set_Cell_Flag(TOP, STACK_NOTE_OPTIONAL);

    if (circle_this)
        circled = TOP_INDEX;

    if (Is_Meta_Sigil(TOP) or Is_Metaform(WORD, TOP))  // meta-assign result
        continue;

    if (Is_Word(TOP) or Is_Tuple(TOP))
        continue;

    if (Is_Space(TOP))
        continue;

    return PANIC(
        "SET-BLOCK! items are (@THE, ^META) WORD/TUPLE or _ or ^]"
    );

}}} set_block_eval_right_hand_side: {

    level_->u.eval.stackindex_circled = circled;  // remember it

    Level* sub = Maybe_Rightward_Continuation_Needed(L);
    if (not sub)
        goto set_block_rightside_dual_in_out;

    return CONTINUE_SUBLEVEL(sub);

}} set_block_rightside_dual_in_out: {

    // 1. On errors we don't assign variables, yet pass the error through.
    //    That permits code like this to work:
    //
    //        trap [[a b]: transcode "1&aa"]

    if (Is_Lifted_Error(OUT)) {  // don't assign variables [1]
        Unliftify_Undecayed(OUT);
        goto set_block_drop_stack_and_continue;
    }

} set_block_result_not_error: {

    // 1. The OUT cell is used by the Set_Var() mechanics as the place to
    //    write from.  Free it up so there's more space to work.  (This
    //    means we have to stop our variable enumeration right before the
    //    top of the stack.)
    //
    // 2. We enumerate from left to right in the SET-BLOCK!, with the "main"
    //    being the first assigned to any variables.  This has the benefit
    //    that if any of the multi-returns were marked as "circled" then the
    //    overwrite of the returned OUT for the whole evaluation will happen
    //    *after* the original OUT was captured into any desired variable.

    Copy_Cell(PUSH(), Known_Element(OUT));  // free up OUT cell [1]

    const Source* pack_array;  // needs GC guarding when OUT overwritten
    const Element* pack_at;  // individualpack block items are lifted
    const Element* pack_tail;

    if (Is_Lifted_Pack(OUT)) {  // antiform block
        pack_at = Cell_List_At(&pack_tail, OUT);

        pack_array = Cell_Array(OUT);
        Push_Lifeguard(pack_array);
    }
    else {  // keep quoted (it aligns with pack items being metaforms)
        Move_Atom(SPARE, OUT);
        pack_at = cast(Element*, SPARE);
        pack_tail = cast(Element*, SPARE) + 1;  // not a valid cell

        pack_array = nullptr;
    }

    StackIndex stackindex_var = STACK_BASE + 1;  // [2]
    Option(StackIndex) circled = level_->u.eval.stackindex_circled;

  next_pack_item: {

    if (stackindex_var == (TOP_INDEX + 1) - 1)  // -1 accounts for pushed OUT
        goto set_block_finalize_and_drop_stack;

    bool is_optional = Get_Cell_Flag(
        Data_Stack_Cell_At(stackindex_var),
        STACK_NOTE_OPTIONAL
    );

    Element* var = CURRENT;  // stable location (scratch), safe across SET
    Copy_Cell(var, Data_Stack_At(Element, stackindex_var));

    assert(QUOTE_BYTE(var) == NOQUOTE_1);

    if (pack_at == pack_tail) {
        if (not is_optional)
            return PANIC("Not enough values for required multi-return");

        // match typical input of lift which will be Unliftify'd
        // (special handling ^WORD! below will actually use plain null to
        // distinguish)
        //
        Init_Lifted_Null(OUT);
    }
    else
        Copy_Cell(OUT, pack_at);

    if (Is_Meta_Sigil(var)) {
        panic ("META sigil should allow ghost pass thru, probably?");
        /* goto circled_check; */
    }

    if (Is_Metaform(WORD, var)) {
        Plainify(var);  // !!! temporary, remove ^ sigil (set should see it)
        if (pack_at == pack_tail) {  // special detection
            Init_Lifted_Null(OUT);
            if (Set_Var_In_Scratch_To_Unlift_Out_Uses_Spare_Throws(
                LEVEL, NO_STEPS, LIB(POKE_P)
            )){
                goto return_thrown;
            }
            goto circled_check;
        }
        assert(Any_Lifted(OUT));  // out is lifted'd
        Liftify(OUT);  // quote it again !!! TBD: set heeds lift
        if (Set_Var_In_Scratch_To_Unlift_Out_Uses_Spare_Throws(
            LEVEL, NO_STEPS, LIB(POKE_P)
        )){
            goto return_thrown;
        }
        Unliftify_Undecayed(OUT);  // set unquotified, undo it again...
        goto circled_check;  // ...because we may have circled this
    }

    if (Is_Space(var)) {
        Unliftify_Undecayed(OUT);
        goto circled_check;
    }

    if (Is_Error(OUT))  // don't pass thru errors if not @
        return PANIC(Cell_Error(OUT));

    if (Is_Word(var) or Is_Tuple(var) or Is_Pinned(WORD, var)) {
        if (Set_Var_In_Scratch_To_Unlift_Out_Uses_Spare_Throws(
            LEVEL,  // overwrites SPARE if single item, but we're done w/it
            GROUPS_OK,
            LIB(POKE_P)
        )){
            goto return_thrown;
        }
    }
    else
        assert(false);

} circled_check: { // Note: no circling passes through the original OUT

    if (circled == stackindex_var)
        Copy_Lifted_Cell(TOP_ELEMENT, OUT);  // unlift'd on finalization

    ++stackindex_var;
    ++pack_at;
    goto next_pack_item;

} set_block_finalize_and_drop_stack: {

    // 1. At the start of the process we pushed the meta-value of whatever the
    //    right hand side of the SET_BLOCK! was (as long as it wasn't an
    //    ERROR!).  OUT gets overwritten each time we write a variable, so we
    //    have to restore it to make the overall SET-BLOCK! process match
    //    the right hand side.  (This value is overwritten by a circled value,
    //    so it may not actually be the original right hand side.);

    if (pack_array)
        Drop_Lifeguard(pack_array);

    Move_Cell(OUT, TOP_ELEMENT);  // restore OUT (or circled) from stack [1]
    Unliftify_Undecayed(OUT);

}} set_block_drop_stack_and_continue: {

    // We've just changed the values of variables, and these variables
    // might be coming up next.  Consider:
    //
    //     304 = [a]: test 1020
    //     a = 304
    //
    // The `a` was fetched and found to not be infix, and in the process
    // its value was known.  But then we assigned that a with a new value
    // in the implementation of SET-BLOCK! here, so, it's incorrect.

    L_next_gotten = nullptr;

    Drop_Data_Stack_To(STACK_BASE);  // drop writeback variables
    goto lookahead;


} case TYPE_FENCE: { ///// FENCE! {...} //////////////////////////////////////

    // FENCE! is the guinea pig for a technique of calling a function defined
    // in the local environment to do the handling.

    Element* out = Inertly_Derelativize_Inheriting_Const(OUT, CURRENT, L->feed);
    Quotify(out);

    Element* spare = Init_Word(SPARE, CANON(FENCE_X_EVAL));
    dont(Quotify(Known_Element(SPARE)));  // want to run word

    Value* temp = rebValue_helper(  // passing binding explicitly, use helper
        Level_Binding(L),
        spare, out,
        rebEND  // must pass END explicitly to helper
    );
    Copy_Cell(OUT, temp);
    rebRelease(temp);
    goto lookahead;


} case HEART_ENUM(0): //// "INERT" TYPES (EXTENSIBILITY TBD) /////////////////
  case TYPE_BLOCK:
  case TYPE_BLOB:
  case TYPE_TEXT:
  case TYPE_FILE:
  case TYPE_EMAIL:
  case TYPE_URL:
  case TYPE_TAG:
  case TYPE_RUNE:
  case TYPE_BITSET:
  case TYPE_MAP:
  case TYPE_VARARGS:
  case TYPE_OBJECT:
  case TYPE_MODULE:
  case TYPE_WARNING:
  case TYPE_PORT:
  case TYPE_LET:
  case TYPE_INTEGER:
  case TYPE_DECIMAL:
  case TYPE_PERCENT:
  case TYPE_MONEY:
  case TYPE_PAIR:
  case TYPE_TIME:
  case TYPE_DATE:
  case TYPE_PARAMETER:
  case TYPE_HANDLE: {

    // Today these datatypes are all inert, but with RebindableSyntax the
    // concept is that you can define a function that says how things like
    // INTEGER! will behave.

    Inertly_Derelativize_Inheriting_Const(OUT, CURRENT, L->feed);
    goto lookahead;


} default: { //// !! CORRUPTION (pseudotypes or otherwise) !! ////////////////

    crash (CURRENT);

  }}  //// END handle_plain: SWITCH STATEMENT ////////////////////////////////


} lookahead: { ///////////////////////////////////////////////////////////////

    // We're sitting at what "looks like the end" of an evaluation step.
    // But we still have to consider infix.  e.g.
    //
    //    [pos val]: evaluate:step [1 + 2 * 3]
    //
    // We want that to give a position of [] and `val = 9`.  The evaluator
    // cannot just dispatch on TYPE_INTEGER in the switch() above, give you 1,
    // and consider its job done.  It has to notice that the word `+` looks up
    // to an ACTION! whose cell has an InfixMode set in the header.
    //
    // Next, there's a subtlety with FEED_FLAG_NO_LOOKAHEAD which explains why
    // processing of the 2 argument doesn't greedily continue to advance, but
    // waits for `1 + 2` to finish.  This is because the right hand argument
    // of math operations tend to be declared #tight.
    //
    // If that's not enough to consider :-) it can even be the case that
    // subsequent infix gets "deferred".  Then, possibly later the evaluated
    // value gets re-fed back in, and we jump right to this post-switch point
    // to give it a "second chance" to take the infix.  (See 'deferred'.)
    //
    // So this post-switch step is where all of it happens, and it's tricky!
    //
    // 1. If something was run with the expectation it should take the next
    //    arg from the output cell, and an evaluation cycle ran that wasn't
    //    an ACTION! (or that was an arity-0 action), that's not what was
    //    meant.  But it can happen, e.g. `x: 10 | x ->-`, where ->- doesn't
    //    get an opportunity to quote left because it has no argument...and
    //    instead retriggers and lets x run.
    //
    // 2. For long-pondered technical reasons, only WORD! is able to dispatch
    //    infix.  If necessary to dispatch an infix function via path, then
    //    a word is used to do it, like `>>` in `x: >> lib/default [...]`.

    if (Get_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH))
        return PANIC(Error_Literal_Left_Path_Raw());  // [1]

    if (Is_Feed_At_End(L->feed)) {
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
        goto finished;  // hitting end is common, avoid do_next's switch()
    }

    switch (Type_Of_Unchecked(L_next)) {
      case TYPE_WORD:  // only WORD! does infix now (TBD: CHAIN!) [2]
        if (not L_next_gotten)
            L_next_gotten = Lookup_Word(L_next, Feed_Binding(L->feed));
        else
            assert(L_next_gotten == Lookup_Word(L_next, Feed_Binding(L->feed)));
        break;  // need to check for lookahead

      case TYPE_FRAME:
        L_next_gotten = L_next;
        break;

      default:  // if not a WORD! or ACTION!, start new non-infix expression
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
        goto finished;
    }

} test_word_or_action_for_infix: { ///////////////////////////////////////////

    Option(InfixMode) infix_mode;

    if (
        not L_next_gotten
        or (
            not (Is_Word(L_next) and Is_Action(unwrap L_next_gotten))
            and not Is_Frame(L_next)
        )
        or not (infix_mode = Cell_Frame_Infix_Mode(unwrap L_next_gotten))
    ){
      lookback_quote_too_late: // run as if starting new expression

        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
        Clear_Eval_Executor_Flag(L, INERT_OPTIMIZATION);

        goto finished;
    }

  check_for_literal_first: { /////////////////////////////////////////////////

    // 1. Left-quoting by infix needs to be done in the lookahead before an
    //    evaluation, not this one that's after.  This happens in cases like:
    //
    //        left-the: infix func [@value] [value]
    //        the <something> left-the
    //
    //    But due to the existence of <end>-able parameters, the left quoting
    //    function might be okay with seeing nothing on the left.  Start a
    //    new expression and let it error if that's not ok.

    Phase* infixed = Cell_Frame_Phase(unwrap L_next_gotten);
    ParamList* paramlist = Phase_Paramlist(infixed);

    if (Get_Flavor_Flag(VARLIST, paramlist, PARAMLIST_LITERAL_FIRST)) {  // [1]
        assert(Not_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH));
        if (Get_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH))
            return PANIC(Error_Literal_Left_Path_Raw());

        const Param* first = First_Unspecialized_Param(nullptr, infixed);
        if (Cell_Parameter_Class(first) == PARAMCLASS_SOFT) {
            if (Get_Feed_Flag(L->feed, NO_LOOKAHEAD)) {
                Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
                Clear_Eval_Executor_Flag(L, INERT_OPTIMIZATION);
                goto finished;
            }
        }
        else if (Not_Eval_Executor_Flag(L, INERT_OPTIMIZATION))
            goto lookback_quote_too_late;
    }

    Clear_Eval_Executor_Flag(L, INERT_OPTIMIZATION);  // served purpose if set

    if (
        Get_Eval_Executor_Flag(L, FULFILLING_ARG)
        and infix_mode != INFIX_DEFER
                            // ^-- (1 + if null [2] else [3]) => 4
    ){
        if (Get_Feed_Flag(L->feed, NO_LOOKAHEAD)) {
            // Don't do infix lookahead if asked *not* to look.

            Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);

            assert(Not_Feed_Flag(L->feed, DEFERRING_INFIX));
            Set_Feed_Flag(L->feed, DEFERRING_INFIX);

            goto finished;
        }

        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
    }

    // A deferral occurs, e.g. with:
    //
    //     return if condition [...] else [...]
    //
    // The first time the ELSE is seen, IF is fulfilling its branch argument
    // and doesn't know if its done or not.  So this code senses that and
    // runs, returning the output without running ELSE, but setting a flag
    // to know not to do the deferral more than once.
    //
    if (
        Get_Eval_Executor_Flag(L, FULFILLING_ARG)
        and (
            infix_mode == INFIX_POSTPONE
            or (
                infix_mode == INFIX_DEFER
                and Not_Feed_Flag(L->feed, DEFERRING_INFIX)
            )
        )
    ){
        if (
            Is_Action_Level(L->prior)
            and Get_Executor_Flag(ACTION, L->prior, ERROR_ON_DEFERRED_INFIX)
        ){
            // Operations that inline functions by proxy (such as MATCH and
            // ENSURE) cannot directly interoperate with THEN or ELSE...they
            // are building a frame with PG_Dummy_Action as the function, so
            // running a deferred operation in the same step is not an option.
            // The expression to the left must be in a GROUP!.
            //
            return PANIC(Error_Ambiguous_Infix_Raw());
        }

        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);

        if (
            Is_Action_Level(L->prior)
            //
            // ^-- !!! Before stackless it was always the case when we got
            // here that a function level was fulfilling, because setting word
            // would reuse levels while fulfilling arguments...but stackless
            // changed this and has setting words start new Levels.  Review.
            //
            and not Is_Level_Fulfilling(L->prior)
        ){
            // This should mean it's a variadic level, e.g. when we have
            // the 2 in the output slot and are at the THEN in:
            //
            //     variadic2 1 2 then (t => [print ["t is" t] <then>])
            //
            // We used to treat this like a barrier, but there is now no such
            // thing as a "BARRIER_HIT" flag.  What should we do now?  Try
            // just jumping to `finished`.
            //
            goto finished;
        }

        Set_Feed_Flag(L->feed, DEFERRING_INFIX);

        // Leave infix operator pending in the feed.  It's up to the parent
        // level to decide whether to ST_STEPPER_LOOKING_AHEAD to jump
        // back in and finish fulfilling this arg or not.  If it does resume
        // and we get to this check again, L->prior->deferred can't be null,
        // otherwise it would be an infinite loop.
        //
        goto finished;
    }

    Clear_Feed_Flag(L->feed, DEFERRING_INFIX);

    // An evaluative lookback argument we don't want to defer, e.g. a normal
    // argument or a deferable one which is not being requested in the context
    // of parameter fulfillment.  We want to reuse the OUT value and get it
    // into the new function's frame.

    Level* sub = Make_Action_Sublevel(L);
    Push_Action(sub, unwrap L_next_gotten);

    Option(const Symbol*) label = Is_Word(L_next)
        ? Cell_Word_Symbol(L_next)
        : Cell_Frame_Label_Deep(L_next);

    Begin_Action(sub, label, infix_mode);
    Fetch_Next_In_Feed(L->feed);

    Push_Level_Erase_Out_If_State_0(OUT, sub);  // infix_mode sets state
    goto process_action;


}} finished: { ///////////////////////////////////////////////////////////////

    Liftify(OUT);  // see top of file notes about why it's Stepper()

} finished_dont_lift_out: {  // called if at end, and it's trash

    // 1. Want to keep this flag between an operation and an ensuing infix in
    //    the same level, so can't clear in Drop_Action(), e.g. due to:
    //
    //        left-the: infix the/
    //        o: make object! [f: does [1]]
    //        o.f left-the  ; want error suggesting SHOVE, need flag for it

    Clear_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH);  // [1]

  #if RUNTIME_CHECKS
    Evaluator_Exit_Checks_Debug(L);

    possibly(STATE == ST_STEPPER_INITIAL_ENTRY);  // TYPE_0 is state 0
    STATE = ST_STEPPER_FINISHED_DEBUG;  // must reset to STATE_0 if reused
  #endif

    return OUT;


} return_thrown: { ///////////////////////////////////////////////////////////

  #if RUNTIME_CHECKS
    Evaluator_Exit_Checks_Debug(L);
  #endif

    return BOUNCE_THROWN;
}}
