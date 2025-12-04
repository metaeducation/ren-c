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
// It may return a DUAL_0 signal for unset.  That's because it's necessary for
// functions like EVAL:STEP to know when there is no result to return.
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
// Rather than awkwardly make all states ^META with an unlifted value to
// represent nothing, this uses DUAL_0 to return a *unset* result.
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

#define L_next_gotten_raw   (&L->feed->gotten)
#define L_next_gotten  (not Is_Gotten_Invalid(L_next_gotten_raw))

#define L_current_gotten_raw  (&L->u.eval.current_gotten)
#define L_current_gotten  (not Is_Gotten_Invalid(L_current_gotten_raw))

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


#define Make_Action_Sublevel(action) \
    Make_Level(&Action_Executor, L->feed, \
      Get_Cell_Flag(action, WEIRD_GHOSTABLE) ? LEVEL_MASK_NONE \
        : (L->flags.bits & LEVEL_FLAG_AFRAID_OF_GHOSTS))


// When a SET-BLOCK! is being processed for multi-returns, it may encounter
// leading-SPACE chains as in ([foo :bar]: 10).  Once the work of extracting
// the real variable from the path is done and pushed to the stack, this bit
// is used to record that the variable was optional.  This makes it easier
// for the phase after the right hand side is evaluated--vs. making it pick
// apart the path again.
//
#define CELL_FLAG_STACK_HINT_OPTIONAL  CELL_FLAG_HINT


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
        (L->flags.bits & EVAL_EXECUTOR_FLAG_FULFILLING_ARG);

    require (
      Level* sub = Make_Level(
        &Stepper_Executor,
        L->feed,
        flags  // inert optimize adjusted the flags to jump in mid-eval
    ));
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
        return Init_Unset_Due_To_End(OUT);

    Derelativize(OUT, At_Feed(L->feed), Feed_Binding(L->feed));
    Fetch_Next_In_Feed(L->feed);
    return OUT;
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

    assert(L == TOP_LEVEL);

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
        Force_Invalidate_Gotten(L_current_gotten_raw);  // !!! require pass in?
        goto look_ahead_for_left_literal_infix; }

    #if (! DEBUG_DISABLE_INTRINSICS)
      intrinsic_dual_arg_in_spare:
      case ST_STEPPER_CALCULATING_INTRINSIC_ARG: {
        Details* details = Ensure_Frame_Details(CURRENT);
        Dispatcher* dispatcher = Details_Dispatcher(details);

        assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
        Set_Level_Flag(L, DISPATCHING_INTRINSIC);  // level_ is not its Level
        dont(Set_Level_Flag(L, RUNNING_TYPECHECK));  // want panic if bad args

        Option(Bounce) b = Irreducible_Bounce(L, Apply_Cfunc(dispatcher, L));
        if (b)  // can't BOUNCE_CONTINUE etc. from an intrinsic dispatch
            panic ("Intrinsic dispatcher returned Irreducible Bounce");

        if (
            Get_Level_Flag(L, AFRAID_OF_GHOSTS)
            and Not_Cell_Flag(CURRENT, WEIRD_GHOSTABLE)
            and Is_Ghost(OUT)
        ){
            Init_Void(OUT);  // usually done in action dispatcher
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
        Init_Unset_Due_To_End(OUT);
        STATE = ST_STEPPER_NONZERO_STATE;
        goto finished;
    }

    Force_Blit_Cell(  // Lookback clears it
        L_current_gotten_raw, L_next_gotten_raw
    );
    Copy_Cell(CURRENT, L_next);
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

    if (LIFT_BYTE(L_next) != NOQUOTE_2)  // quoted right can't look back
        goto give_up_backward_quote_priority;

    Option(InfixMode) infix_mode;
    Phase* infixed;

    if (LIFT_BYTE(L_next) != NOQUOTE_2)
       goto give_up_backward_quote_priority;

    switch (KIND_BYTE_RAW(L_next)) {  // ignore Sigil
      case TYPE_WORD: {
        Get_Word(
            L_next_gotten_raw,
            L_next,
            Feed_Binding(L->feed)  // L_binding breaks here [2]
        ) except (Error* e) {
            Erase_Cell(L_next_gotten_raw);
            UNUSED(e);  // don't care (will hit on next step if we care)
            goto give_up_backward_quote_priority;
        }
        if (
            not Is_Action(L_next_gotten_raw)
            or not (infix_mode = Frame_Infix_Mode(L_next_gotten_raw))
        ){
            goto give_up_backward_quote_priority;
        }
        infixed = Frame_Phase(L_next_gotten_raw);
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

    Force_Blit_Cell(L_current_gotten_raw, L_next_gotten_raw);
    Copy_Cell(CURRENT, L_next);  // CURRENT now invoking word (->-, OF, =>)
    Fetch_Next_In_Feed(L->feed);  // ...now skip that invoking word

    if (
        Is_Feed_At_End(L->feed)  // v-- OUT is what used to be on left
        and (
            Type_Of_Unchecked(OUT) == TYPE_WORD
            or Type_Of_Unchecked(OUT) == TYPE_PATH
        )
    ){  // exemption: put OUT back in CURRENT and CURRENT back in feed [2]
        Move_Value(&L->feed->fetched, CURRENT);
        L->feed->p = &L->feed->fetched;
        Force_Blit_Cell(&L->feed->gotten, L_current_gotten_raw);

        Move_Value(CURRENT, cast(Element*, OUT));
        Invalidate_Gotten(L_current_gotten_raw);

        Set_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH);

        if (Is_Word(CURRENT)) {
            STATE = cast(StepperState, TYPE_WORD);
            goto handle_word_where_action_lookups_are_active;
        }

        assert(Is_Path(CURRENT));
        STATE = cast(StepperState, TYPE_PATH);
        goto handle_path_where_action_lookups_are_active;
    }

    goto right_hand_literal_infix_wins;


} right_hand_literal_infix_wins: { ///////////////////////////////////////////

    require (
      Level* sub = Make_Action_Sublevel(L_current_gotten_raw)
    );
    require (
      Push_Action(sub, L_current_gotten_raw, infix_mode)
    );
    Push_Level_Erase_Out_If_State_0(OUT, sub);  // infix_mode sets state
    goto process_action;


}} give_up_backward_quote_priority: {

    assert(Is_Cell_Erased(OUT));

    if (LIFT_BYTE(CURRENT) == NOQUOTE_2) {
        Option(Sigil) sigil = Sigil_Of(CURRENT);
        switch (opt sigil) {
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

    if (LIFT_BYTE(CURRENT) == QUASIFORM_3)
        goto handle_quasiform;

    assert(LIFT_BYTE(CURRENT) != ANTIFORM_1);
    goto handle_quoted;


} handle_quoted: { //// QUOTED! [ 'XXX  '''@XXX  '~XXX~ ] ////////////////////

    // Quoted values drop one quote level.  Binding is left as-is.

    Copy_Cell(OUT, CURRENT);

    LIFT_BYTE(OUT) -= Quote_Shift(1);
    STATE = cast(StepperState, TYPE_QUOTED);  // can't leave STATE_0

    goto lookahead;


} handle_quasiform: { //// QUASIFORM! ~XXX~ //////////////////////////////////

    // Quasiforms produce antiforms when they evaluate.  Binding is erased.
    //
    // 1. Not all quasiforms have legal antiforms.  For instance: while all
    //    WORD!s have quasiforms, only a few are allowed to become antiform
    //    keywords (e.g. ~null~ and ~okay~)
    //
    // 2. If we are in a step of a sequential series of evaluations, then
    //    it is risky to allow GHOST! to vanish, e.g.:
    //
    //        eval compose [some stuff (lift ^var)]  ; var incidentally GHOST!
    //
    //    It's a fine line. If you had composed in code like `comment "hi"`
    //    that would be one thing, but synthesizing a lifted value from an
    //    arbitrary expression feels less specific.  Use ^ operator if you
    //    really want vaporization: `[some stuff ^ (lift ^var)]`

    Copy_Cell(OUT, CURRENT);

    require (  // may be illegal [1]
      Coerce_To_Antiform(OUT)
    );

    if (Get_Level_Flag(L, AFRAID_OF_GHOSTS) and Is_Ghost(OUT))
        Init_Void(OUT);  // help avoid accidental vanishing [2]

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

    if (Is_Pinned_Space(CURRENT))
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
        panic (Error_Need_Non_End(CURRENT));

    assert(Not_Feed_Flag(L->feed, NEEDS_SYNC));
    const Element* elem = cast(Element*, L->feed->p);

    bool antiform = Get_Cell_Flag(elem, FEED_NOTE_META);  // [2]
    Clear_Cell_Flag(m_cast(Element*, elem), FEED_NOTE_META);  // [3]

    The_Next_In_Feed(L->out, L->feed);  // !!! review infix interop

    if (antiform)  // exception [2]
        Unliftify_Known_Stable(Known_Stable(L->out));

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

    if (Is_Tied_Space(CURRENT))
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

    if (Is_Antiform(OUT))
        panic ("$ operator cannot bind antiforms");

    Bind_If_Unbound(Known_Element(OUT), Level_Binding(L));
    goto lookahead;


} handle_any_metaform: { //// META (^) ///////////////////////////////////////

    // METAFORM! types will LIFT variables on storage, and UNLIFT them on
    // fetching.  This is complex logic.

  switch (STATE = cast(Byte, Heart_Of(CURRENT))) {

  case TYPE_WORD: { //// META WORD! ^XXX /////////////////////////////////////

    // A META-WORD! gives you the undecayed representation of the variable
    //
    // 1. We don't want situations like `^x: (<expr> ^y)` to assign <expr>
    //    to x just because y incidentally held a GHOST!.  You need to be
    //    explicit with `^x: (<expr> ^ ^y)` to get that behavior, which
    //    would bypass the LEVEL_FLAG_AFRAID_OF_GHOSTS which sequential
    //    evaluations in Evaluator_Executor() use by default.

    require (
      Get_Any_Word_Maybe_Trash(OUT, CURRENT, L_binding)
    );

    possibly(Not_Cell_Stable(OUT) or Is_Trash(Known_Stable(OUT)));

    if (Get_Level_Flag(L, AFRAID_OF_GHOSTS) and Is_Ghost(OUT))
        Init_Void(OUT);  // help avoid accidental vanishing [1]

    goto lookahead;


} case TYPE_TUPLE: { //// META TUPLE! ^XXX.YYY ///////////////////////////////

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
    //
    // 1. It's possible for (^obj.^lifted-error) to give back an ERROR! due
    //    to the field being a lifted error, or (^obj.missing-field) to give
    //    an ERROR! due to the field being absent.

    Plainify(CURRENT);  // remove the ^ sigil

    heeded (Bind_If_Unbound(CURRENT, L_binding));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    require (
      Get_Var_In_Scratch_To_Out(L, GROUPS_OK)
    );
    possibly(Is_Error(OUT));  // last step may be missing, or meta-error [1]

    goto lookahead;  // even ERROR! wants lookahead (e.g. for EXCEPT)


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
    KIND_BYTE(out) = TYPE_BLOCK;
    Quotify(out);  // !!! was quoting, to avoid binding?

    Element* spare = Init_Word(SPARE, CANON(PACK));
    dont(Quotify(Known_Element(SPARE)));  // want to run word

    Api(Stable*) temp = Known_Stable(rebLift_helper(
        cast(RebolContext*, Level_Binding(L)),
        spare, out, rebEND
    ));
    Copy_Cell(OUT, temp);
    rebRelease(temp);

    assume (
      Unliftify_Undecayed(OUT)
    );
    goto lookahead;


} case TYPE_FENCE: { //// META FENCE! ^{...} /////////////////////////////////

    panic ("Don't know what ^FENCE! is going to do yet");


} case TYPE_RUNE: { //// META RUNE! /////////////////////////////////////////

    if (Is_Metaform_Space(CURRENT))
        goto handle_caret_sigil;  // special handling for lone ^

    panic ("Don't know what ^RUNE! is going to do yet (besides ^)");

} handle_caret_sigil: {  //// Meta Space Sigil (^) ///////////////////////////

    Level* right = Maybe_Rightward_Continuation_Needed(L);
    if (not right)
        goto approval_rightside_dual_in_out;

    STATE = ST_STEPPER_APPROVE_EVALUATING_RIGHT_SIDE;
    return CONTINUE_SUBLEVEL(right);

} approval_rightside_dual_in_out: {

    // !!! Did all the work just by making a not-afraid of ghosts step?

    goto lookahead;


} default: { /////////////////////////////////////////////////////////////////

    panic (
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
        panic (Error_Expression_Barrier_Raw());

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

    if (Frame_Lens(CURRENT))  // running frame if lensed
        panic ("Use REDO to restart a running FRAME! (can't EVAL)");

    Option(InfixMode) infix_mode = Frame_Infix_Mode(CURRENT);

    require (
      Level* sub = Make_Action_Sublevel(CURRENT)
    );
    assert(Is_Cell_Erased(OUT));  // so nothing on left [1]
    require (
      Push_Action(sub, CURRENT, infix_mode)
    );
    Push_Level_Erase_Out_If_State_0(OUT, sub);  // infix_mode sets state

    goto process_action;

} process_action: {

    // Gather args and execute function (the arg gathering makes nested
    // eval calls that lookahead, but no lookahead after the action runs)

    STATE = cast(StepperState, TYPE_FRAME);
    return CONTINUE_SUBLEVEL(TOP_LEVEL);


} case TYPE_WORD: { //// WORD! ///////////////////////////////////////////////

  // A plain word tries to fetch its value through its binding.  It panics if
  // the word is unbound (or if bound to a variable which holds TRASH!).  If
  // it looks up to an antiform FRAME!, then that "Action" will be invoked.
  //
  // NOTE: The usual dispatch of infix functions is *not* via a TYPE_WORD in
  // this switch, it's by some code at the `lookahead:` label.  You only see
  // infix here when there was nothing to the left, so cases like `(+ 1 2)`

} handle_word_where_action_lookups_are_active: {

    // 1. If we're just going to panic on a TRASH! access, there's no reason
    //    to check for it first.  See if it's an ACTION!, in which case run
    //    it and we don't pay for the trash check.

    assert(not Sigil_Of(CURRENT));

    heeded (Bind_If_Unbound(CURRENT, L_binding));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    require (
      Get_Var_In_Scratch_To_Out(LEVEL, NO_STEPS)
    );

    if (Is_Error(OUT))  // e.g. couldn't pick word as field from binding
        panic (Cell_Error(OUT));  // don't conflate with action result

    assert(Is_Cell_Stable(OUT));  // plain WORD! pick, ERROR! is only unstable
    Stable* out = cast(Stable*, OUT);

    if (Is_Action(out))  // check first [1]
        goto run_action_in_out;

    if (Get_Cell_Flag(CURRENT, CURRENT_NOTE_RUN_WORD)) {
        if (Is_Frame(out))
            goto run_action_in_out;
        panic ("Leading slash means execute FRAME! or ACTION! only");
    }

    if (Is_Trash(out))  // checked second [1]
        panic (Error_Bad_Word_Get(CURRENT, out));

    goto lookahead;

} run_action_in_out: {

  // 1. For C-DEBUG-BREAK, it's not that helpful to *actually* dispatch to an
  //    action called C-DEBUG-BREAK, because we want to debug the callsite,
  //    not the guts of that function.  So if a word looks up to C-DEBUG-BREAK
  //    we notice that here.  Then we just jump direct to the next expression,
  //    which allows you to break in the middle of things, like:
  //
  //        1 + c-debug-break 2
  //
  // 2. When dispatching infix and you have something on the left, you want to
  //    push the level *after* the flag for infixness has been set...to avoid
  //    overwriting the output cell that's the left hand side input.  But in
  //    this case we don't have a left input, even though we're doing infix.
  //    So pushing *before* we set the flags means the FLAG_STATE_BYTE() will
  //    be 0, and we get clearing.

   Stable* out = cast(Stable*, OUT);

#if INCLUDE_C_DEBUG_BREAK_NATIVE && RUNTIME_CHECKS
  if (
      not Is_Trash(Mutable_Lib_Var(SYM_C_DEBUG_BREAK))  // unset in boot
      and Frame_Phase(LIB(C_DEBUG_BREAK)) == Frame_Phase(out)
  ){
      debug_break();  // <---------------------------------- C-DEBUG-BREAK [1]
      Erase_Cell(OUT);  // start_new_expression requires OUT erased
      goto start_new_expression;
  }
#endif

    Option(InfixMode) infix_mode = Frame_Infix_Mode(out);

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
    Details* details = opt Try_Frame_Details(out);
    if (
        not infix_mode  // too rare a case for intrinsic optimization
        and details
        and Get_Details_Flag(details, CAN_DISPATCH_AS_INTRINSIC)
        and Not_Level_At_End(L)  // can't do <end>, fallthru to error
        and not SPORADICALLY(10)  // checked builds sometimes bypass
    ){
        Copy_Plain_Cell(CURRENT, out);

        Param* param = Phase_Param(details, 1);
        Flags flags = EVAL_EXECUTOR_FLAG_FULFILLING_ARG;

        switch (Parameter_Class(param)) {
          case PARAMCLASS_NORMAL:
            break;

          case PARAMCLASS_META:
            break;

          case PARAMCLASS_JUST:
            Just_Next_In_Feed(SPARE, L->feed);
            goto intrinsic_dual_arg_in_spare;

          case PARAMCLASS_THE:
            The_Next_In_Feed(SPARE, L->feed);
            goto intrinsic_dual_arg_in_spare;

          default:
            panic ("Unsupported Intrinsic parameter convention");
        }

        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);  // when non-infix call

        require (
          Level* sub = Make_Level(&Stepper_Executor, L->feed, flags)
        );
        Push_Level_Erase_Out_If_State_0(SPARE, sub);
        STATE = ST_STEPPER_CALCULATING_INTRINSIC_ARG;
        return CONTINUE_SUBLEVEL(sub);
    }
  #endif

    require (
      Level* sub = Make_Action_Sublevel(out)
    );
    require (
      Push_Action(sub, out, infix_mode)  // before OUT erased [2]
    );
    Erase_Cell(OUT);  // want OUT clear, even if infix_mode sets state nonzero
    Push_Level_Erase_Out_If_State_0(OUT, sub);

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

    switch (opt Try_Get_Sequence_Singleheart(CURRENT)) {
      case NOT_SINGLEHEART_0:
        break;  // wasn't xxx: or :xxx where xxx is BLOCK!/CHAIN!/WORD!/etc

      case TRAILING_SPACE_AND(WORD): {  // FOO: or ^FOO:
        Bind_If_Unbound(CURRENT, L_binding);
        if (Is_Metaform(CURRENT)) {  // ^foo: -> ^foo
            Plainify(CURRENT);
            assume (
              Unsingleheart_Sequence(CURRENT)
            );
            Metafy(CURRENT);
        }
        else {
            assume (
              Unsingleheart_Sequence(CURRENT)  // foo: -> foo
            );
        }
        goto handle_generic_set; }

      case TRAILING_SPACE_AND(TUPLE): {  // a.b.c: is a set tuple
        assume (
          Unsingleheart_Sequence(CURRENT)
        );
        assert(Is_Tuple(CURRENT));
        goto handle_generic_set; }

      case TRAILING_SPACE_AND(BLOCK): {  // [a b]: multi-return assign
        assume (
          Unsingleheart_Sequence(CURRENT)
        );
        STATE = ST_STEPPER_SET_BLOCK;
        goto handle_set_block; }

      case TRAILING_SPACE_AND(GROUP): {  // (xxx): -- generic retrigger set
        assume (
          Unsingleheart_Sequence(CURRENT)
        );
        Invalidate_Gotten(L_next_gotten_raw);  // arbitrary code changes
        require (
          Level* sub = Make_Level_At_Inherit_Const(
            &Evaluator_Executor,
            CURRENT,
            L_binding,
            LEVEL_MASK_NONE | (not LEVEL_FLAG_AFRAID_OF_GHOSTS)
        ));
        Init_Ghost(Evaluator_Primed_Cell(sub));
        Push_Level_Erase_Out_If_State_0(SPARE, sub);
        STATE = ST_STEPPER_SET_GROUP;
        return CONTINUE_SUBLEVEL(sub); }

      case LEADING_SPACE_AND(WORD): {  // :FOO, refinement, error on eval?
        assume (
          Unsingleheart_Sequence(CURRENT)
        );
        STATE = ST_STEPPER_GET_WORD;
        panic (":WORD! meaning is likely to become TRY WORD!"); }

      case LEADING_SPACE_AND(TUPLE): {  // :a.b.c -- what will this do?
        assume (
          Unsingleheart_Sequence(CURRENT)
        );
        STATE = ST_STEPPER_GET_TUPLE;
        panic (":TUPLE! meaning is likely to become TRY TUPLE!"); }

      case LEADING_SPACE_AND(BLOCK): {  // !!! :[a b] reduces, not great...
        assume (
          Unsingleheart_Sequence(CURRENT)
        );
        Bind_If_Unbound(CURRENT, L_binding);
        if (rebRunThrows(
            u_cast(Sink(Stable), OUT),  // <-- output, API won't make atoms
            CANON(REDUCE), CURRENT
        )){
            goto return_thrown;
        }
        goto lookahead; }

      case LEADING_SPACE_AND(GROUP): {
        assume (
          Unsingleheart_Sequence(CURRENT)
        );
        panic ("GET-GROUP! has no evaluator meaning at this time"); }

      default:  // it's just something like :1 or <tag>:
        panic ("No current evaluation for things like :1 or <tag>:");
    }

    require (
      Stable* out = Get_Chain_Push_Refinements(
        OUT,  // where to write action
        SPARE,  // temporary GC-safe scratch space
        CURRENT,
        L_binding
    ));

    assert(Is_Action(out));

    if (Is_Frame_Infix(out)) {  // too late, left already evaluated
        Drop_Data_Stack_To(STACK_BASE);
        panic ("Use `->-` to shove left infix operands into CHAIN!s");
    }

} handle_action_in_out_with_refinements_pushed: {

    Stable* out = cast(Stable*, OUT);

    require (
      Level* sub = Make_Action_Sublevel(out)
    );
    sub->baseline.stack_base = STACK_BASE;  // refinements

    require (
      Push_Action(sub, out, PREFIX_0)
    );
    Push_Level_Erase_Out_If_State_0(OUT, sub);  // not infix, sub state is 0
    goto process_action;


} case TYPE_GROUP: //// GROUP! (...) /////////////////////////////////////////
  handle_group_or_meta_group: {

    // Groups simply evaluate their contents, and can evaluate to GHOST! if
    // the contents completely disappear.
    //
    // 1. For an explanation of starting this particular Evaluator_Executor()
    //    as being unafraid of ghosts, see notes at the top of %c-eval.c
    //    Simply put, we want `expr` and `(expr)` to behave similarly, and
    //    the constraints forcing `eval [expr] to be different from `expr`
    //    w.r.t. GHOST! don't apply to inline groups.

    Invalidate_Gotten(L_next_gotten_raw);  // arbitrary code changes variables

    Flags flags = LEVEL_MASK_NONE
        | (not LEVEL_FLAG_AFRAID_OF_GHOSTS);  // group semantics, not EVAL [1]

    require (
      Level* sub = Make_Level_At_Inherit_Const(
        &Evaluator_Executor,
        CURRENT,
        L_binding,
        flags
    ));

    Value* primed = Evaluator_Primed_Cell(sub);
    Init_Ghost(primed);  // want to vaporize if all ghosts [1]

    Push_Level_Erase_Out_If_State_0(OUT, sub);
    return CONTINUE_SUBLEVEL(sub);

} group_or_meta_group_result_in_out: {

    if (Is_Group(CURRENT))
        goto lookahead;  // not decayed, result is good

    assert(Is_Meta_Form_Of(GROUP, CURRENT));

    panic ("^(...) behavior is likely to act like ^ (...), in time");

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
    //
    // 1. Cases like (obj.^lifted-error) and (obj.missing-field) will return
    //    an ERROR! antiform, and (obj.^lifted-pack) can return a PACK!, etc.

    Element* spare = Copy_Sequence_At(SPARE, CURRENT, 0);
    bool blank_at_head = Is_Space(spare);
    if (
        not blank_at_head  // `.a` means pick member from "self"
        and Any_Inert(spare)  // `1.2.3` is inert
    ){
        Derelativize(OUT, CURRENT, L_binding);
        goto lookahead;
    }

    heeded (Bind_If_Unbound(CURRENT, L_binding));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    require (
      Get_Var_In_Scratch_To_Out(L, GROUPS_OK)
    );
    possibly(Not_Cell_Stable(OUT));  // last step or unmeta'd item [1]

    goto lookahead;


} case TYPE_PATH: { //// PATH! [ a/  b/c/d  e/ ] /////////////////////////////

  // Ren-C moved to member access with "dots instead of slashes" (TUPLE!)
  // and refinements are done with "colons instead of slashes" (CHAIN!).  So
  // PATH!s role has come to be specificially dealing with functions:
  //
  //   * abc/     - means ABC is a function, return it as-is
  //   * /abc     - means ensure ABC is a function and run it
  //   * abc/def  - means ABC is a context, DEF is a function, run it
  //   * abc/def/ - means ABC and DEF are functions, compose them
  //
  // 1. It's likely that paths like 1/2 or otherwise inert-headed will be
  //    inert and evaluate to themselves.
  //
  // 2. Slash at head will signal running actions soon enough.  But for the
  //    moment it is still refinement.  Let's try not binding it by default
  //    just to see what headaches that causes...if any.

} handle_path_where_action_lookups_are_active: {

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

        Length len = Sequence_Len(CURRENT);
        spare = Copy_Sequence_At(SPARE, CURRENT, len - 1);
        slash_at_tail = Is_Space(spare);
    }
    else switch (unwrap single) {
      case LEADING_SPACE_AND(WORD): {
        assume (
          Unsingleheart_Sequence(CURRENT)
        );
        Set_Cell_Flag(CURRENT, CURRENT_NOTE_RUN_WORD);
        goto handle_word_where_action_lookups_are_active; }

      case LEADING_SPACE_AND(CHAIN): {  // /abc: or /?:?:?
        assume (
          Unsingleheart_Sequence(CURRENT)
        );

        switch (opt Try_Get_Sequence_Singleheart(CURRENT)) {
          case TRAILING_SPACE_AND(WORD): {  // /abc: is set actions only
            assume (
              Unsingleheart_Sequence(CURRENT)
            );
            Set_Cell_Flag(CURRENT, SCRATCH_VAR_NOTE_ONLY_ACTION);
            goto handle_generic_set; }

          case TRAILING_SPACE_AND(TUPLE): {  // /a.b.c: is set actions only
            assume (
              Unsingleheart_Sequence(CURRENT)
            );
            Set_Cell_Flag(CURRENT, SCRATCH_VAR_NOTE_ONLY_ACTION);
            goto handle_generic_set; }

          default:
            panic ("/a:b:c will guarantee a function call, in time");
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
  //    an arity-0 operation.  Returning a definitional error isn't complete
  //    nonsense, but still might not be great.  Review the choice.
  //
  // 3. Trailing slash notation is a particularly appealing way of denoting
  //    that something is an action, and that you'd like to fetch it in a
  //    way that does not take arguments:
  //
  //         for-next: specialize for-skip/ [skip: 1]
  //         ;                         ---^
  //         ; slash helps show block is not argument
  //
  // 4. Left hand sides don't not look ahead at paths to find infix functions.
  //    This is because PATH! dispatch is costly and can error in more ways
  //    than sniffing a simple WORD! for infix can.  So the prescribed way of
  //    running infix with paths is `left ->- right/side`, which uses an infix
  //    WORD! to mediate the interaction.

    heeded (Bind_If_Unbound(CURRENT, L_binding));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    Get_Path_Push_Refinements(LEVEL) except (Error* e) {
        possibly(slash_at_tail);  // ...or, exception for arity-0? [2]
        panic (e);  // don't FAIL, PANIC [1]
    }

    Stable* out = Known_Stable(OUT);
    assert(Is_Action(out));

    if (slash_at_tail) {  // do not run action, just return it [3]
        if (STACK_BASE != TOP_INDEX) {
            if (Specialize_Action_Throws(
                SPARE, out, nullptr, STACK_BASE
            )){
                goto return_thrown;
            }
            Move_Value(OUT, SPARE);
        }
        Packify_Action(OUT);  // foo/ is always ACTION!
        goto lookahead;
    }

    if (Is_Frame_Infix(out)) {  // too late, left already evaluated [4]
        Drop_Data_Stack_To(STACK_BASE);
        panic ("Use `->-` to shove left infix operands into PATH!s");
    }

    UNUSED(slash_at_head);  // !!! should e.g. enforce /1.2.3 as warning?
    goto handle_action_in_out_with_refinements_pushed;

}} handle_generic_set: { /////////////////////////////////////////////////////

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
        Is_Word(CURRENT) or Is_Meta_Form_Of(WORD, CURRENT)
        or Is_Tuple(CURRENT)
        or Is_Lifted_Void(CURRENT)
    );
    STATE = ST_STEPPER_GENERIC_SET;

    Level* right = Maybe_Rightward_Continuation_Needed(L);
    if (not right)
        goto generic_set_rightside_dual_in_out;

    return CONTINUE_SUBLEVEL(right);

} generic_set_rightside_dual_in_out: {

    if (Is_Endlike_Unset(OUT))
        panic (Error_Need_Non_End_Raw(CURRENT));

    if (Is_Lifted_Void(CURRENT))  // e.g. `(void): ...`  !!! use space var!
        goto lookahead;  // pass through everything

    heeded (Bind_If_Unbound(CURRENT, L_binding));
    heeded (Corrupt_Cell_If_Needful(SPARE));

    require (
      Set_Var_In_Scratch_To_Out(LEVEL, GROUPS_OK)
    );

    Invalidate_Gotten(L_next_gotten_raw);  // cache tampers with lookahead [1]

    goto lookahead;

} set_group_result_in_spare: {

    assert(not L_current_gotten);

    if (Is_Void(SPARE)) {
        Init_Lifted_Void(CURRENT);  // can't put voids in feed position
        goto handle_generic_set;
    }
    else switch (opt Type_Of(SPARE)) {
      case TYPE_BLOCK:
        Copy_Cell(CURRENT, cast(Element*, SPARE));
        STATE = ST_STEPPER_SET_BLOCK;
        goto handle_set_block;

      case TYPE_WORD:
        Copy_Cell(CURRENT, cast(Element*, SPARE));
        goto handle_generic_set;

      case TYPE_TUPLE:
        Copy_Cell(CURRENT, cast(Element*, SPARE));
        goto handle_generic_set;

      default:
        panic ("Unknown type for use in SET-GROUP!");
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
    // 1. Empty SET-BLOCK follows the same rules as any other block receiving
    //    more values than it wants: it ignores the extra values, and passes
    //    through the original assignment.  That's technically *all* potential
    //    states that might come up on the right hand side--including ERROR!
    //    The behavior naturally "falls out" of the implementation.

    assert(STATE == ST_STEPPER_SET_BLOCK and Is_Block(CURRENT));

    possibly(Series_Len_At(CURRENT) == 0);  // pass through everything [1]

    const Element* tail;
    const Element* check = List_At(&tail, CURRENT);
    Context* check_binding = Derive_Binding(L_binding, CURRENT);

    // we've extracted the array at and tail, can reuse current now

    Option(StackIndex) circled = 0;

  push_variables_loop: for (; check != tail; ++check) {

    // We pre-process the SET-BLOCK! first and collect the variables to write
    // on the stack.  (It makes more sense for any GROUP!s in the set-block to
    // be evaluated on the left before the right.)
    //
    // !!! Should the block be locked while the advancement happens?  It
    // wouldn't need to be since everything is on the stack before code is run
    // on the right...but it might reduce confusion.

    if (Is_Quoted(check))
        panic ("QUOTED? not currently permitted in SET-BLOCK!s");

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
        panic ("Can only {Circle} one multi-return result");

    Length len_at = Series_Len_At(check);
    if (len_at == 1) {
        Derelativize(
            CURRENT,
            List_Item_At(check),
            check_binding
        );
    }
    else  // !!! should {} be a synonym for {_}?
        panic ("{Circle} only one element in multi-return");

    circle_this = true;

} circle_detection_finished: {

    bool is_optional;
    bool is_action;

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
        panic (
            "Only leading SPACE CHAIN! in SET BLOCK! dialect"
        );
    }
    assume (
      Unsingleheart_Sequence(CURRENT)
    );
    is_optional = true;

} optional_detection_finished: {

    if (not Is_Path(CURRENT)) {
        is_action = false;
        goto path_detection_finished;
    }

    Option(SingleHeart) single;
    if (
        not (single = Try_Get_Sequence_Singleheart(CURRENT))
        or not Singleheart_Has_Leading_Space(unwrap single)
    ){
        panic (
            "Only leading SPACE PATH! in SET BLOCK! dialect"
        );
    }
    assume (
      Unsingleheart_Sequence(CURRENT)
    );
    is_action = true;

} path_detection_finished: {

    if (
        Is_Group(CURRENT)
        or Is_Pinned_Form_Of(GROUP, CURRENT)
        or Is_Meta_Form_Of(GROUP, CURRENT)
    ){
        if (Eval_Any_List_At_Throws(SPARE, CURRENT, SPECIFIED)) {
            Drop_Data_Stack_To(STACK_BASE);
            goto return_thrown;
        }
        if (Is_Void(SPARE) and Is_Group(CURRENT)) {
            Init_Quasar(PUSH());  // [(void)]: ... pass thru
        }
        else {
            require (
              Stable* spare = Decay_If_Unstable(SPARE)
            );
            if (Is_Antiform(spare))
                panic (Error_Bad_Antiform(spare));

            if (Is_Pinned_Form_Of(GROUP, CURRENT)) {
                Pinify(Known_Element(spare));  // add @ decoration
            }
            else if (Is_Meta_Form_Of(GROUP, CURRENT)) {
                Metafy(Known_Element(spare));  // add ^ decoration
            }
            else
                assert(Is_Group(CURRENT));

            Copy_Cell(PUSH(), spare);
        }
    }
    else
        Copy_Cell(PUSH(), CURRENT);

    UNUSED(*CURRENT);  // look at stack top now

    if (is_optional)  // so next phase won't worry about leading slash
        Set_Cell_Flag(TOP, STACK_HINT_OPTIONAL);

    if (is_action)
        Set_Cell_Flag(TOP, SCRATCH_VAR_NOTE_ONLY_ACTION);

    if (circle_this)
        circled = TOP_INDEX;

    if (Is_Metaform_Space(TOP) or Is_Meta_Form_Of(WORD, TOP))  // meta-assign result
        continue;

    if (Is_Word(TOP) or Is_Tuple(TOP))
        continue;

    if (Is_Space(TOP))
        continue;

    panic (
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
    //        rescue [[a b]: transcode "1&aa"]

    if (Is_Error(OUT))  // don't assign variables [1]
        goto set_block_drop_stack_and_continue;

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

    Copy_Cell(PUSH(), OUT);  // free up OUT cell [1]

    const Source* pack_array;  // needs GC guarding when OUT overwritten
    const Element* pack_at_lifted;  // individual pack block items are lifted
    const Element* pack_tail;

    if (Is_Pack(OUT)) {  // antiform block
        pack_at_lifted = List_At(&pack_tail, OUT);

        pack_array = Cell_Array(OUT);
        Push_Lifeguard(pack_array);
    }
    else {  // single item
        Copy_Lifted_Cell(SPARE, OUT);
        pack_at_lifted = cast(Element*, SPARE);
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
        STACK_HINT_OPTIONAL
    );

    bool is_action = Get_Cell_Flag(
        Data_Stack_Cell_At(stackindex_var),
        SCRATCH_VAR_NOTE_ONLY_ACTION
    );

    Element* var = CURRENT;  // stable location (scratch), safe across SET
    Copy_Cell(var, Data_Stack_At(Element, stackindex_var));
    if (is_action) {
        assert(var == &level_->scratch);
        heeded (Set_Cell_Flag(var, SCRATCH_VAR_NOTE_ONLY_ACTION));
    }

    assert(LIFT_BYTE(var) == NOQUOTE_2);

    if (pack_at_lifted == pack_tail) {  // no more multi-return values
        if (not is_optional) {
            if (circled == stackindex_var)
                panic ("Circled item has no multi-return value to use");

            Init_Dual_Word_Unset_Signal(OUT);
            heeded (Corrupt_Cell_If_Needful(SPARE));
            Option(Error*) e = Trap_Tweak_Var_In_Scratch_With_Dual_Out(
                LEVEL,
                NO_STEPS
            );
            if (e)
                panic (unwrap e);
            goto skip_circled_check;  // we checked it wasn't circled
        }

        // match typical input of lift which will be Unliftify'd
        // (special handling ^WORD! below will actually use plain null to
        // distinguish)
        //
        Init_Nulled(OUT);
    }
    else {
        Copy_Cell(OUT, pack_at_lifted);
        require (
          Unliftify_Undecayed(OUT)  // unlift for output...
        );
    }

    if (Is_Metaform_Space(var))
        goto circled_check;

    if (Is_Meta_Form_Of(WORD, var)) {
        heeded (Corrupt_Cell_If_Needful(SPARE));
        require (
          Set_Var_In_Scratch_To_Out(LEVEL, NO_STEPS)
        );
        goto circled_check;  // ...because we may have circled this
    }

    if (Is_Error(OUT))  // don't pass thru errors if not ^ sigil
        panic (Cell_Error(OUT));

    require (
      Decay_If_Unstable(OUT)
    );

    if (Is_Space(var))
        goto circled_check;

    if (Is_Word(var) or Is_Tuple(var) or Is_Pinned_Form_Of(WORD, var)) {
        heeded (Corrupt_Cell_If_Needful(SPARE));
        require (
          Set_Var_In_Scratch_To_Out(LEVEL, GROUPS_OK)
        );
    }
    else
        assert(false);

    goto circled_check;

} circled_check: { ///////////////////////////////////////////////////////////

  // Note: no circling passes through the original PACK!

    if (circled == stackindex_var)
        Copy_Cell(TOP_ATOM, OUT);

} skip_circled_check: { //////////////////////////////////////////////////////

    ++stackindex_var;
    if (pack_at_lifted != pack_tail)
        ++pack_at_lifted;
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

    Move_Value(OUT, TOP_ATOM);  // restore OUT (or circled) from stack [1]

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

    Invalidate_Gotten(L_next_gotten_raw);

    Drop_Data_Stack_To(STACK_BASE);  // drop writeback variables
    goto lookahead;


} case TYPE_FENCE: { ///// FENCE! {...} //////////////////////////////////////

    // FENCE! is the guinea pig for a technique of calling a function defined
    // in the local environment to do the handling.

    Element* out = Inertly_Derelativize_Inheriting_Const(OUT, CURRENT, L->feed);
    Quotify(out);

    Element* spare = Init_Word(SPARE, CANON(FENCE_X_EVAL));
    dont(Quotify(Known_Element(SPARE)));  // want to run word

    Api(Value*) temp = rebValue_helper(  // pass binding explicitly to helper
        cast(RebolContext*, Level_Binding(L)),
        spare, out,
        rebEND  // must pass END explicitly to helper
    );
    Copy_Cell(OUT, temp);
    rebRelease(temp);
    goto lookahead;


} case TYPE_0_constexpr: //// "INERT" TYPES (EXTENSIBILITY TBD) //////////////
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
        panic (Error_Literal_Left_Path_Raw());  // [1]

    if (Is_Feed_At_End(L->feed)) {
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
        goto finished;  // hitting end is common, avoid do_next's switch()
    }

    switch (opt Type_Of_Unchecked(L_next)) {
      case TYPE_WORD:  // only WORD! does infix now (TBD: CHAIN!) [2]
        if (not L_next_gotten) {
            Get_Word(
                L_next_gotten_raw, L_next, Feed_Binding(L->feed)
            ) except (Error* e) {
                Erase_Cell(L_next_gotten_raw);
                UNUSED(e);
            }
        }
        else {
            DECLARE_STABLE (check);
            assume (
              Get_Word(check, L_next, Feed_Binding(L->feed))
            );
            assert(
                memcmp(check, L_next_gotten_raw, 4 * sizeof(uintptr_t)) == 0
            );
        }
        break;  // need to check for lookahead

      case TYPE_FRAME:
        Copy_Cell(L_next_gotten_raw, L_next);
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
            not (Is_Word(L_next) and Is_Action(L_next_gotten_raw))
            and not Is_Frame(L_next)
        )
        or not (infix_mode = Frame_Infix_Mode(L_next_gotten_raw))
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

    Phase* infixed = Frame_Phase(L_next_gotten_raw);
    ParamList* paramlist = Phase_Paramlist(infixed);

    if (Get_Flavor_Flag(VARLIST, paramlist, PARAMLIST_LITERAL_FIRST)) {  // [1]
        assert(Not_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH));
        if (Get_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH))
            panic (Error_Literal_Left_Path_Raw());

        const Param* first = First_Unspecialized_Param(nullptr, infixed);
        if (Parameter_Class(first) == PARAMCLASS_SOFT) {
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
            panic (Error_Ambiguous_Infix_Raw());
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
            and not Is_Level_Fulfilling_Or_Typechecking(L->prior)
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

    require (
      Level* sub = Make_Action_Sublevel(L_next_gotten_raw)
    );
    require (
      Push_Action(sub, L_next_gotten_raw, infix_mode)
    );
    Fetch_Next_In_Feed(L->feed);

    Push_Level_Erase_Out_If_State_0(OUT, sub);  // infix_mode sets state
    goto process_action;


}} finished: { ///////////////////////////////////////////////////////////////

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
