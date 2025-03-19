//
//  File: %c-step.c
//  Summary: "Code for Evaluation of one Step in the Main Interpreter"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// This file contains code for the `Stepper_Executor()`.  It is responsible
// for the typical interpretation of BLOCK! or GROUP!, in terms of giving
// sequences like `x: 1 + 2` a meaning for how SET-WORD! or INTEGER! behaves.
//
// By design the evaluator is not recursive at the C level--it is "stackless".
// At points where a sub-expression must be evaluated in a new level, it will
// heap-allocate that level and then do a C `return` of BOUNCE_CONTINUE.
// Processing then goes through the "Trampoline" (see %c-trampoline.c), which
// later re-enters the suspended level's executor with the result.  Setting
// the level's STATE byte prior to suspension is a common way of letting a
// level know where to pick up from when it left off.
//
// When it encounters something that needs to be handled as a function
// application, it defers to %c-action.c for the Action_Executor().  The
// action gets its own level.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Stepper_Executor() is LONG.  That's largely on purpose.  Breaking it
//   into functions would add overhead (in the RUNTIME_CHECKS build, if not
//   also NO_RUNTIME_CHECKS builds) and prevent interesting optimizations.
//
//   It is separated into sections, and the invariants in each section are
//   made clear with comments and asserts.
//
// * See %d-eval.c for more detailed assertions of the preconditions,
//   postconditions, and state...which are broken out to help keep this file
//   a more manageable length.
//
// * The evaluator only moves forward, and operates on a strict window of
//   visibility of two elements at a time (current position and "lookback").
//   See `Feed` for the code that provides this abstraction over Ren-C
//   arrays as well as C va_list.
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

// !!! Evil Macro, repeats parent!
//
STATIC_ASSERT(
    EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH
    == ACTION_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH
);

#define Make_Action_Sublevel(parent) \
    Make_Level(&Action_Executor, (parent)->feed, \
        LEVEL_FLAG_RAISED_RESULT_OK \
        | ((parent)->flags.bits & EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH))


// When a SET-BLOCK! is being processed for multi-returns, it may encounter
// leading-blank chains as in ([foo :bar]: 10).  Once the work of extracting
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
        fail (Error_Need_Non_End(CURRENT));

    Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);  // always >= 2 elements [2]

    Flags flags =  // v-- if L was fulfilling, we are
        (L->flags.bits & EVAL_EXECUTOR_FLAG_FULFILLING_ARG)
        | LEVEL_FLAG_RAISED_RESULT_OK;  // trap [e: transcode "1&aa"] works

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
    assert(not Is_Feed_At_End(L->feed));

    Derelativize(OUT, At_Feed(L->feed), Feed_Binding(L->feed));
    Fetch_Next_In_Feed(L->feed);
    STATE = ST_INERT_STEPPER_FINISHED;
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
      intrinsic_arg_in_spare:
      case ST_STEPPER_CALCULATING_INTRINSIC_ARG: {
        Details* details = Ensure_Cell_Frame_Details(CURRENT);
        Dispatcher* dispatcher = Details_Dispatcher(details);

        possibly(Is_Antiform_Unstable(SPARE));  // intrinsic typechecks/decays
        assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
        Set_Level_Flag(L, DISPATCHING_INTRINSIC);
        Bounce bounce = (*dispatcher)(L);  // flag says level_ is not its Level
        if (bounce == nullptr)
            Init_Nulled(OUT);
        else if (bounce == BOUNCE_OKAY)
            Init_Okay(OUT);
        else if (bounce == L->out) {
            if (Is_Raised(OUT))
                return FAIL(Cell_Error(OUT));
        }
        else if (bounce == BOUNCE_BAD_INTRINSIC_ARG)
            return Native_Fail_Result(L, Error_Bad_Intrinsic_Arg_1(L));
        else {
            assert(bounce == BOUNCE_FAIL);  // no BOUNCE_CONTINUE, API handles
            return bounce;
        }

        Clear_Level_Flag(L, DISPATCHING_INTRINSIC);
        goto lookahead; }
      #endif

      case TYPE_SIGIL:
        goto sigil_rightside_in_out;

      case TYPE_GROUP:
      case TYPE_META_GROUP:
        goto lookahead;

      case ST_STEPPER_SET_GROUP:
        goto set_group_result_in_spare;

      case ST_STEPPER_SET_WORD:
      case ST_STEPPER_SET_TUPLE:
      case ST_STEPPER_SET_VOID:
        goto generic_set_rightside_in_out;

      case ST_STEPPER_SET_BLOCK:
        if (Is_Raised(OUT))  // don't assign variables [1]
            goto set_block_drop_stack_and_continue;

        goto set_block_rightside_result_in_out;

      case TYPE_FRAME:
        goto lookahead;

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

    // 1. !!! There is a current edge case with rebValue(""), where a bad mix
    //    of FEED_FLAG_NEEDS_SYNC and end testing means that the stepper can
    //    be called on an end Level.  It is non-trivial to sort out the set
    //    of concerns so for now just return void...but ultimately this
    //    should be fixed.

    Sync_Feed_At_Cell_Or_End_May_Fail(L->feed);

    Update_Expression_Start(L);  // !!! See Level_Array_Index() for caveats

    /* assert(Not_Level_At_End(L)); */  // edge case with rebValue("") [1]
    if (Is_Level_At_End(L)) {
        Init_Void(OUT);
        STATE = TYPE_BLANK;  // can't leave as STATE_0
        goto finished;
    }

    L_current_gotten = L_next_gotten;  // Lookback clears it
    Copy_Cell(CURRENT, L_next);
    Fetch_Next_In_Feed(L->feed);

} look_ahead_for_left_literal_infix: { ///////////////////////////////////////

    // The first thing we do in an evaluation step has to be to look ahead for
    // any function that takes its left hand side literally.  Lambda functions
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

    if (Is_Level_At_End(L))
        goto give_up_backward_quote_priority;

    assert(not L_next_gotten);  // Fetch_Next_In_Feed() cleared it

    if (QUOTE_BYTE(L_next) != NOQUOTE_1)  // quoted right can't look back
        goto give_up_backward_quote_priority;

    Option(InfixMode) infix_mode;
    Phase* infixed;

    switch (HEART_BYTE(L_next)) {  // words and chains on right may look back
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
            STATE = TYPE_WORD;
            goto word_common;
        }

        assert(Is_Path(CURRENT));
        STATE = TYPE_PATH;
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

}} give_up_backward_quote_priority:

  //=//// BEGIN MAIN SWITCH STATEMENT /////////////////////////////////////=//

    // This switch is done with a case for all TYPE_XXX values, in order to
    // facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
    //
    // Subverting the jump table optimization with specialized branches for
    // fast tests like Any_Inert() and IS_NULLED_OR_VOID_OR_END() has shown
    // to reduce performance in practice.  The compiler does the right thing.
    //
    // 1. Quasiforms produce antiforms, and quoted values drop one quote
    //    level.  Binding is left as-is in both cases, and not influenced by
    //    the current binding of the evaluator (antiforms are always unbound).
    //
    // 2. The Stepper_Executor()'s state bytes are a superset of the Type_Of()
    //    of processed values.  See the ST_STEPPER_XXX enumeration.

    assert(Is_Cell_Erased(OUT));

    if (QUOTE_BYTE(CURRENT) != NOQUOTE_1) {  // quasiform or quoted [1]
        Copy_Cell(OUT, CURRENT);
        if (QUOTE_BYTE(CURRENT) == QUASIFORM_2) {
            Coerce_To_Antiform(OUT);  // checks that antiform is legal
            STATE = TYPE_QUASIFORM;  // can't leave as STATE_0
        }
        else {
            QUOTE_BYTE(OUT) -= Quote_Shift(1);
            STATE = TYPE_QUOTED;  // can't leave as STATE_0
        }
    }
    else switch ((STATE = HEART_BYTE(CURRENT))) {  // states include type [2]

    //=//// COMMA! ////////////////////////////////////////////////////////=//
    //
    // A comma is a lightweight looking expression barrier, which evaluates
    // to antiform comma.  It acts much like a vaporizing COMMENT or ELIDE,
    // but has the distinction of appearing like an <end> to most evaluative
    // parameters.  We can debate the wisdom of the exceptions:
    //
    //    >> the,
    //    == ,
    //
    //    >> meta,
    //    == ~,~
    //
    // At one point the evaluator tried to maintain a BARRIER_HIT state to
    // give extra protection, but this was deemed to confuse the mechanics
    // more than it actually helped.
    //
    //   https://forum.rebol.info/t/1387/6
    //

      case TYPE_COMMA:
        Init_Barrier(OUT);
        goto skip_lookahead;  // skip lookahead, see notes there


    //=//// FRAME! ////////////////////////////////////////////////////////=//
    //
    // If a FRAME! makes it to the SWITCH statement, that means it is either
    // literally a frame in the array (eval compose $() [(unrun :add) 1 2]) or
    // is being retriggered via REEVAL.
    //
    // Most FRAME! evaluations come from the antiform ("actions") triggered
    // from a WORD! or PATH! case.)
    //
    // 1. If an infix function is run at this moment, it will not have a left
    //    hand side argument.

      case TYPE_FRAME: {
        if (Cell_Frame_Lens(CURRENT))  // running frame if lensed
            return FAIL("Use REDO to restart a running FRAME! (can't EVAL)");

        Level* sub = Make_Action_Sublevel(L);
        Push_Action(sub, CURRENT);
        Option(InfixMode) infix_mode = Cell_Frame_Infix_Mode(CURRENT);
        assert(Is_Cell_Erased(OUT));  // so nothing on left [1]
        Begin_Action(sub, Cell_Frame_Label_Deep(CURRENT), infix_mode);
        Push_Level_Erase_Out_If_State_0(OUT, sub);  // infix_mode sets state

        goto process_action; }

    //=//// ACTION! ARGUMENT FULFILLMENT AND/OR TYPE CHECKING PROCESS /////=//

        // This one processing loop is able to handle ordinary action
        // invocation, specialization, and type checking of an already filled
        // action frame.  It walks through both the formal parameters (in
        // the spec) and the actual arguments (in the call frame) using
        // pointer incrementation.
        //
        // Based on the parameter type, it may be necessary to "consume" an
        // expression from values that come after the invocation point.  But
        // not all parameters will consume arguments for all calls.

      process_action: {
        //
        // Gather args and execute function (the arg gathering makes nested
        // eval calls that lookahead, but no lookahead after the action runs)
        //
        STATE = TYPE_FRAME;
        return CONTINUE_SUBLEVEL(TOP_LEVEL); }


    //=//// SIGIL! ////////////////////////////////////////////////////////=//
    //
    // ^ acts like META
    //
    // & acts like TYPE OF
    //
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
    // ' acts like JUST (literal, no added binding)
    //
    //      >> abc: 10
    //
    //      >> word: ' abc
    //
    //      >> get word
    //      ** Script Error: abc word is not bound to a context
    //
    // ~~ has no use at time of writing.
    //
    // 2. There's a twist, that @ can actually handle antiforms if they are
    //    coming in via an API feed.  This is a convenience so you can write:
    //
    //        rebElide("append block maybe @", value_might_be_null);
    //
    //     ...instead of:
    //
    //        rebElide("append block maybe", rebQ(value_might_be_null));
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

      case TYPE_SIGIL: {
        Sigil sigil = Cell_Sigil(CURRENT);
        switch (sigil) {
          case SIGIL_QUOTE:
          case SIGIL_THE: {
            if (Is_Feed_At_End(L->feed))  // no literal to take if (@), (')
                return FAIL(Error_Need_Non_End(CURRENT));

            assert(Not_Feed_Flag(L->feed, NEEDS_SYNC));
            const Element* elem = c_cast(Element*, L->feed->p);

            bool antiform = Get_Cell_Flag(elem, FEED_NOTE_META);  // [2]
            Clear_Cell_Flag(m_cast(Element*, elem), FEED_NOTE_META);  // [3]

            if (sigil == SIGIL_THE)
                The_Next_In_Feed(L->out, L->feed);  // !!! review infix interop
            else {
                assert(sigil == SIGIL_QUOTE);
                Just_Next_In_Feed(L->out, L->feed);  // !!! review infix
            }

            if (antiform)  // exception [2]
                Meta_Unquotify_Known_Stable(cast(Element*, L->out));
            break; }

          case SIGIL_META:  // ^
          case SIGIL_TYPE:  // &
          case SIGIL_VAR: {  // $
            Level* right = Maybe_Rightward_Continuation_Needed(L);
            if (not right)
                goto sigil_rightside_in_out;

            return CONTINUE_SUBLEVEL(right); }

          case SIGIL_QUASI:  // ~~
            return FAIL("No evaluator behavior defined for ~~ yet");

          default:
            assert(false);
        }
        goto lookahead; }

      sigil_rightside_in_out: {
        switch (Cell_Sigil(CURRENT)) {
          case SIGIL_META:  // ^
            Meta_Quotify(OUT);
            break;

          case SIGIL_TYPE:  // &
            Copy_Cell(SPARE, OUT);
            Decay_If_Unstable(SPARE);
            if (rebRunThrows(stable_OUT, "try type of", stable_SPARE))
                goto return_thrown;
            break;

          case SIGIL_VAR:  // $
            if (Is_Antiform(OUT))
                return FAIL("$ operator cannot bind antiforms");
            Derelativize(SPARE, cast(Element*, OUT), Level_Binding(L));
            Copy_Cell(OUT, SPARE);  // !!! inefficient
            break;

          default:
            assert(false);
        }
        goto lookahead; }


    //=//// WORD! //////////////////////////////////////////////////////////=//
    //
    // A plain word tries to fetch its value through its binding.  It fails
    // if the word is unbound (or if the binding is to a variable which is
    // set, but to the antiform of blank e.g. NOTHING).  Should the word
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

      word_common: ///////////////////////////////////////////////////////////

      case TYPE_WORD: {
        Option(Error*) error = Trap_Get_Any_Word(OUT, CURRENT, L_binding);
        if (error)
            return FAIL(unwrap error);  // don't conflate with function result

        if (Is_Action(OUT))
            goto run_action_in_out;

        if (Any_Vacancy(stable_OUT))  // checked second
            return FAIL(Error_Bad_Word_Get(CURRENT, OUT));

        goto lookahead; }

      run_action_in_out: { ///////////////////////////////////////////////////

        Option(InfixMode) infix_mode = Cell_Frame_Infix_Mode(OUT);
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
        Details* details = maybe Try_Cell_Frame_Details(OUT);
        if (
            not infix_mode  // too rare a case for intrinsic optimization
            and details
            and Get_Details_Flag(details, CAN_DISPATCH_AS_INTRINSIC)
            and Not_Level_At_End(L)  // can't do <end>, fallthru to error
            and not SPORADICALLY(10)  // checked builds sometimes bypass
        ){
            Option(VarList*) coupling = Cell_Frame_Coupling(OUT);
            Init_Frame(
                CURRENT,
                details,
                label,
                coupling
            );
            Param* param = Phase_Param(details, 1);
            Flags flags = EVAL_EXECUTOR_FLAG_FULFILLING_ARG;

            switch (Cell_ParamClass(param)) {
                case PARAMCLASS_NORMAL:
                break;

                case PARAMCLASS_META:
                flags |= LEVEL_FLAG_RAISED_RESULT_OK;
                break;

                case PARAMCLASS_JUST:
                Just_Next_In_Feed(SPARE, L->feed);
                goto intrinsic_arg_in_spare;

                case PARAMCLASS_THE:
                The_Next_In_Feed(SPARE, L->feed);
                goto intrinsic_arg_in_spare;

                default:
                return FAIL("Unsupported Intrinsic parameter convention");
            }

            Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);  // when non-infix call

            Level* sub = Make_Level(&Stepper_Executor, L->feed, flags);
            Push_Level_Erase_Out_If_State_0(SPARE, sub);
            STATE = ST_STEPPER_CALCULATING_INTRINSIC_ARG;
            return CONTINUE_SUBLEVEL(sub);
        }
      #endif

        Level* sub = Make_Action_Sublevel(L);
        Push_Action(sub, OUT);
        Push_Level_Erase_Out_If_State_0(OUT, sub);  // *always* clear out
        Begin_Action(sub, label, infix_mode);
        unnecessary(Push_Level_Erase_Out_If_State_0(OUT, sub)); // see [1]

        goto process_action; }


    //=//// CHAIN! ////////////////////////////////////////////////////////=//
    //
    // Due to the consolidation of all the SET-XXX! and GET-XXX! types as
    // CHAIN! with leading or trailing blanks, CHAIN! has to break that down
    // and dispatch to the appropriate behavior.

      case TYPE_CHAIN: {
        switch (Try_Get_Sequence_Singleheart(CURRENT)) {
          case NOT_SINGLEHEART_0:
            break;  // wasn't xxx: or :xxx where xxx is BLOCK!/CHAIN!/WORD!/etc

          case TRAILING_BLANK_AND(WORD):  // FOO:, set word
            Derelativize(  // !!! binding may be sensitive to "set-words only"
                SPARE, CURRENT, L_binding
            );
            Unchain(Copy_Cell(CURRENT, cast(Element*, SPARE)));
            STATE = ST_STEPPER_SET_WORD;
            goto handle_generic_set;

          case TRAILING_BLANK_AND(TUPLE):  // a.b.c: is a set tuple
            Unchain(CURRENT);
            STATE = ST_STEPPER_SET_TUPLE;
            goto handle_generic_set;

          case TRAILING_BLANK_AND(BLOCK):  // [a b]: multi-return assign
            Unchain(CURRENT);
            STATE = ST_STEPPER_SET_BLOCK;
            goto handle_set_block;

          case TRAILING_BLANK_AND(GROUP): {  // (xxx): -- generic retrigger set
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

          case LEADING_BLANK_AND(WORD):  // :FOO, refinement, error on eval?
            Unchain(CURRENT);
            STATE = ST_STEPPER_GET_WORD;
            goto handle_get_word;

          case LEADING_BLANK_AND(TUPLE):  // :a.b.c -- what will this do?
            Unchain(CURRENT);
            STATE = ST_STEPPER_GET_TUPLE;
            goto handle_get_tuple;

          case LEADING_BLANK_AND(BLOCK):  // !!! :[a b] reduces, not great...
            Unchain(CURRENT);
            Derelativize(SPARE, CURRENT, L_binding);
            if (rebRunThrows(
                cast(Value*, OUT),  // <-- output, API won't make atoms
                CANON(REDUCE), SPARE
            )){
                goto return_thrown;
            }
            goto lookahead;

          case LEADING_BLANK_AND(GROUP):
            Unchain(CURRENT);
            return FAIL("GET-GROUP! has no evaluator meaning at this time");

          default:  // it's just something like :1 or <tag>:
            return FAIL("No current evaluation for things like :1 or <tag>:");
        }

        Option(Error*) error = Trap_Get_Chain_Push_Refinements(
            OUT,  // where to write action
            SPARE,  // temporary GC-safe scratch space
            CURRENT,
            L_binding
        );
        if (error)  // lookup failed, a GROUP! in path threw, etc.
            return FAIL(unwrap error);  // don't definitional error for now

        assert(Is_Action(OUT));

        if (Is_Cell_Frame_Infix(OUT)) {  // too late, left already evaluated
            Drop_Data_Stack_To(STACK_BASE);
            return FAIL("Use `->-` to shove left infix operands into CHAIN!s");
        }
        goto handle_action_in_out_with_refinements_pushed; }

     handle_action_in_out_with_refinements_pushed: { /////////////////////////

        Level* sub = Make_Action_Sublevel(L);
        sub->baseline.stack_base = STACK_BASE;  // refinements

        Option(const Symbol*) label = Cell_Frame_Label_Deep(OUT);

        Push_Action(sub, OUT);
        Begin_Action(sub, label, PREFIX_0);  // not infix so, sub state is 0
        Push_Level_Erase_Out_If_State_0(OUT, sub);
        goto process_action; }


    //=//// GET-WORD! /////////////////////////////////////////////////////=//
    //
    // A GET-WORD! gives you the contents of a variable as-is, with no
    // dispatch on functions.  This includes antiforms.
    //
    // https://forum.rebol.info/t/1301

      handle_get_word:  // jumps here for CHAIN! that's like a GET-WORD!
      case TYPE_META_WORD: {
        assert(
            (STATE == ST_STEPPER_GET_WORD and Is_Word(CURRENT))
            or (STATE == TYPE_META_WORD and Is_Meta_Word(CURRENT))
        );
        Option(Error*) error = Trap_Get_Any_Word_Maybe_Vacant(
            OUT,
            CURRENT,
            L_binding
        );
        if (error)
            return FAIL(unwrap error);

        if (STATE == TYPE_META_WORD)
            Meta_Quotify(OUT);

        goto lookahead; }


    //=//// GROUP!, GET-GROUP!, and META-GROUP! ///////////////////////////=//
    //
    // Groups simply evaluate their contents, and can evaluate to nihil if
    // the contents completely disappear.
    //
    // GET-GROUP! currently acts as a synonym for group [1].
    //
    //////////////////////////////////////////////////////////////////////////
    //
    // 2. We prime the array executor with nihil in order to avoid generating
    //    voids from thin air when using GROUP!s
    //
    //        >> 1 + 2 (comment "hi")
    //        == 3  ; e.g. not void

      case TYPE_GROUP:
      case TYPE_META_GROUP: {
        L_next_gotten = nullptr;  // arbitrary code changes fetched variables

        Flags flags = LEVEL_FLAG_RAISED_RESULT_OK;  // [2]

        if (STATE == TYPE_META_GROUP)
            flags |= LEVEL_FLAG_META_RESULT;

        Level* sub = Make_Level_At_Inherit_Const(
            &Evaluator_Executor,
            CURRENT,
            L_binding,
            flags
        );
        Init_Nihil(Evaluator_Primed_Cell(sub));
        Push_Level_Erase_Out_If_State_0(OUT, sub);

        return CONTINUE_SUBLEVEL(sub); }


    //=//// TUPLE! /////////////////////////////////////////////////////////=//
    //
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

      case TYPE_TUPLE: {
        Copy_Sequence_At(SPARE, CURRENT, 0);
        bool blank_at_head = Is_Blank(SPARE);
        if (
            not blank_at_head  // `.a` means pick member from "self"
            and Any_Inert(SPARE)  // `1.2.3` is inert
        ){
            Derelativize(OUT, CURRENT, L_binding);
            goto lookahead;
        }

        Option(Error*) error = Trap_Get_Any_Tuple(  // vacant will cause error
            OUT,
            GROUPS_OK,
            CURRENT,
            L_binding
        );
        if (error) {  // tuples never run actions, won't conflate to raise it
            Init_Error(OUT, unwrap error);
            Raisify(OUT);
            goto lookahead;  // e.g. EXCEPT might want error
        }

        if (Is_Action(OUT)) {  // don't RAISE, conflates
            if (blank_at_head)
                goto run_action_in_out;

            return FAIL(Error_Action_Tuple_Raw(CURRENT));
        }

        goto lookahead; }


    //=//// PATH! //////////////////////////////////////////////////////////=//
    //
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
    //
    // 3. It would not make sense to return a definitional error when a path
    //    lookup does not exist.  Imagine making null back for `try lib/append`
    //    if you wrote `try lib/append [a b c] [d e]` when lib/append did not
    //    exist--that's completely broken.
    //
    // 4. Since paths with trailing slashes just return the action as-is, it's
    //    an arity-0 operation.  So returning a definitional error isn't
    //    complete nonsense, but still might not be great.  Review the choice.
    //
    // 5. Trailing slash notation is a particularly appealing way of denoting
    //    that something is an action, and that you'd like to fetch it in a
    //    way that does not take arguments:
    //
    //         /for-next: specialize for-skip/ [skip: 1]
    //         ;                         ---^
    //         ; slash helps show block is not argument
    //
    // 6. The left hand side does not look ahead at paths to find infix
    //    functions.  This is because PATH! dispatch is costly and can error
    //    in more ways than sniffing a simple WORD! for infix can.  So the
    //    prescribed way of running infix with paths is `left ->- right/side`,
    //    which uses an infix WORD! to mediate the interaction.

      path_common:
      case TYPE_PATH: {
        bool slash_at_head;
        bool slash_at_tail;
        Option(SingleHeart) single = Try_Get_Sequence_Singleheart(CURRENT);

        if (not single) {
            Copy_Sequence_At(SPARE, CURRENT, 0);
            if (Any_Inert(SPARE)) {
                if (Is_Blank(SPARE))
                    slash_at_head = true;
                else {
                    Derelativize(OUT, CURRENT, L_binding);  // inert [2]
                    goto lookahead;
                }
            }
            else
                slash_at_head = false;

            Length len = Cell_Sequence_Len(CURRENT);
            Copy_Sequence_At(SPARE, CURRENT, len - 1);
            slash_at_tail = Is_Blank(SPARE);
        }
        else switch (unwrap single) {
          case LEADING_BLANK_AND(WORD):
            return FAIL("Killing off refinement evaluations!");

          case LEADING_BLANK_AND(CHAIN): {  // /abc: or /?:?:?
            Unpath(CURRENT);

            switch (Try_Get_Sequence_Singleheart(CURRENT)) {
              case TRAILING_BLANK_AND(WORD):  // /abc: is set actions only
                Unchain(CURRENT);
                Set_Cell_Flag(CURRENT, CURRENT_NOTE_SET_ACTION);
                STATE = ST_STEPPER_SET_WORD;
                goto handle_generic_set;

              case TRAILING_BLANK_AND(TUPLE):  // /a.b.c: is set actions only
                Unchain(CURRENT);
                Set_Cell_Flag(CURRENT, CURRENT_NOTE_SET_ACTION);
                STATE = ST_STEPPER_SET_TUPLE;
                goto handle_generic_set;

              default:
                return FAIL("/a:b:c will guarantee a function call, in time");
            }
            break; }

          default:
            slash_at_tail = Singleheart_Has_Trailing_Blank(unwrap single);
            slash_at_head = Singleheart_Has_Leading_Blank(unwrap single);
            assert(slash_at_head == not slash_at_tail);
            break;
        }

        Option(Error*) error = Trap_Get_Path_Push_Refinements(
            OUT,  // where to write action
            SPARE,  // temporary GC-safe scratch space
            CURRENT,
            L_binding
        );
        if (error) {  // lookup failed, a GROUP! in path threw, etc.
            if (not slash_at_tail)
                return FAIL(unwrap error);  // RAISE error would conflate [3]
            return FAIL(unwrap error);  // don't RAISE error for now [4]
        }

        assert(Is_Action(OUT));
        if (slash_at_tail) {  // do not run action, just return it [5]
            if (STACK_BASE != TOP_INDEX) {
                if (Specialize_Action_Throws(
                    SPARE, stable_OUT, nullptr, STACK_BASE
                )){
                    goto return_thrown;
                }
                Move_Atom(OUT, SPARE);
            }
            goto lookahead;
        }

        if (Is_Cell_Frame_Infix(OUT)) {  // too late, left already evaluated [6]
            Drop_Data_Stack_To(STACK_BASE);
            return FAIL("Use `->-` to shove left infix operands into PATH!s");
        }

        UNUSED(slash_at_head);  // !!! should e.g. enforce /1.2.3 as error?
        goto handle_action_in_out_with_refinements_pushed; }


    //=//// TUPLE! or WORD! VARIABLE ASSIGNMENT ///////////////////////////=//
    //
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

    handle_generic_set: { ////////////////////////////////////////////////////
        assert(
            (STATE == ST_STEPPER_SET_WORD and Is_Word(CURRENT))
            or (STATE == ST_STEPPER_SET_TUPLE and Is_Tuple(CURRENT))
            or (STATE == ST_STEPPER_SET_VOID and Is_Meta_Of_Void(CURRENT))
        );

        Level* right = Maybe_Rightward_Continuation_Needed(L);
        if (not right)
            goto generic_set_rightside_in_out;

        return CONTINUE_SUBLEVEL(right);

    } generic_set_rightside_in_out: {  ///////////////////////////////////////

        if (Is_Barrier(OUT))  // even `(void):,` needs to error
            return FAIL(Error_Need_Non_End(CURRENT));

        if (STATE == ST_STEPPER_SET_VOID) {
            // can happen with SET-GROUP! e.g. `(void): ...`, current in spare
        }
        else if (Is_Raised(OUT)) {
            // Don't assign, but let (trap [a.b: transcode "1&aa"]) work
        }
        else {
            Decay_If_Unstable(OUT);  // !!! should likely pass through packs

            if (Is_Action(OUT)) {  // !!! Review: When to update labels?
                if (STATE == ST_STEPPER_SET_WORD)
                    Update_Frame_Cell_Label(OUT, Cell_Word_Symbol(CURRENT));
            }
            else {  // assignments of /foo: or /obj.field: require action
                if (Get_Cell_Flag(CURRENT, CURRENT_NOTE_SET_ACTION))
                    return FAIL(
                        "/word: and /obj.field: assignments require Action"
                    );
            }

            if (Set_Var_Core_Throws(  // cheaper on fail vs. Set_Var_May_Fail()
                SPARE,
                GROUPS_OK,
                CURRENT,
                L_binding,
                stable_OUT  // should take unstable?  handle blocks?
            )){
                goto return_thrown;
            }

            L_next_gotten = nullptr;  // cache can tamper with lookahead [1]
        }

        goto lookahead; }

      set_group_result_in_spare: {  ////////////////////////////////////////

        assert(L_current_gotten == nullptr);

        if (Is_Void(SPARE)) {
            STATE = ST_STEPPER_SET_VOID;
            Init_Meta_Of_Void(CURRENT);  // can't put voids in feed position
            goto handle_generic_set;
        }
        else switch (Type_Of(SPARE)) {
          case TYPE_BLOCK :
            Copy_Cell(CURRENT, cast(Element*, SPARE));
            STATE = ST_STEPPER_SET_BLOCK;
            goto handle_set_block;

          case TYPE_WORD :
            Copy_Cell(CURRENT, cast(Element*, SPARE));
            STATE = ST_STEPPER_SET_WORD;
            goto handle_generic_set;

          case TYPE_TUPLE :
            Copy_Cell(CURRENT, cast(Element*, SPARE));
            STATE = ST_STEPPER_SET_TUPLE;
            goto handle_generic_set;

          default:
            return FAIL("Unknown type for use in SET-GROUP!");
        }
        goto lookahead; }


    //=//// GET-TUPLE! and META-TUPLE! ////////////////////////////////////=//
    //
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

      handle_get_tuple:
      case TYPE_META_TUPLE: {
        assert(
            (STATE == ST_STEPPER_GET_TUPLE and Is_Tuple(CURRENT))
            or (STATE == TYPE_META_TUPLE and Is_Meta_Tuple(CURRENT))
        );
        Option(Error*) error = Trap_Get_Any_Tuple_Maybe_Vacant(
            OUT,
            GROUPS_OK,
            CURRENT,
            L_binding
        );
        if (error) {
            Init_Error(OUT, unwrap error);
            Raisify(OUT);
            goto lookahead;  // e.g. EXCEPT might want to see raised error
        }

        if (STATE == TYPE_META_TUPLE)
            Meta_Quotify(OUT);

        goto lookahead; }


    //=//// SET-BLOCK! ////////////////////////////////////////////////////=//
    //
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
    // GROUP!s, and also allows THE-WORD! to `@circle` which result you want
    // to be the overall result of the expression (defaults to the normal
    // main return value).

      // 1. Empty SET-BLOCK! are not supported, although it could be argued
      //    that an empty set-block could receive a NIHIL (~[]~) pack.
      //
      // 2. We pre-process the SET-BLOCK! first and collect the variables to
      //    write on the stack.  (It makes more sense for any GROUP!s in the
      //    set-block to be evaluated on the left before the right.)
      //
      //    !!! Should the block be locked while the advancement happens?  It
      //    wouldn't need to be since everything is on the stack before code
      //    is run on the right...but it might reduce confusion.
      //
      // 3. {xxx} indicates a desire for a "circled" result.  By default, the
      //    whole input is returned.  (While checking we set stackindex_circled
      //    when we see `[{...} ...]: ...` to give an error if more than one
      //    return were circled.)
      //
      // 4. ^xxx indicate a desire to get a "meta" result.
      //
      //    !!! ^META composition with use-existing-binding is proposed as
      //    ^[@x] but this has not been implemented yet.
      //
      //    !!! The multi-return mechanism doesn't allow an arbitrary number
      //    of meta steps, just one.  Should you be able to say ^(^(x)) or
      //    something like that to add more?  :-/
      //

      handle_set_block: {
        assert(STATE == ST_STEPPER_SET_BLOCK and Is_Block(CURRENT));

        if (Cell_Series_Len_At(CURRENT) == 0)  // not supported [1]
            return FAIL("SET-BLOCK! must not be empty for now.");

        const Element* tail;
        const Element* check = Cell_List_At(&tail, CURRENT);
        Context* check_binding = Derive_Binding(L_binding, CURRENT);

        // we've extracted the array at and tail, can reuse current now

        Option(StackIndex) circled = 0;

        for (; check != tail; ++check) {  // push variables first [2]
            if (Is_Quoted(check))
                return FAIL("QUOTED? not currently permitted in SET-BLOCK!s");

            Heart heart = Cell_Heart(check);

            bool circle_this;

            if (heart == TYPE_FENCE) {  // [x {y}]: ... fence means eval to that
                if (circled)
                    return FAIL("Can only {Circle} one multi-return result");
                Length len_at = Cell_Series_Len_At(check);
                if (len_at == 1) {
                    Derelativize(
                        CURRENT,
                        Cell_List_Item_At(check),
                        check_binding
                    );
                }
                else  // !!! should {} be a synonym for {#} or {~} ?
                    return FAIL("{Circle} only one element in multi-return");

                circle_this = true;
                heart = Cell_Heart(CURRENT);
            }
            else {
                circle_this = false;
                Derelativize(CURRENT, check, check_binding);  // same heart
            }

            bool is_optional;

            if (heart == TYPE_CHAIN) {
                Option(SingleHeart) single;
                if (
                    not (single = Try_Get_Sequence_Singleheart(CURRENT))
                    or not Singleheart_Has_Leading_Blank(unwrap single)
                ){
                    return FAIL(
                        "Only leading blank CHAIN! in SET BLOCK! dialect"
                    );
                }
                Unchain(CURRENT);
                heart = Heart_Of_Singleheart(unwrap single);
                assert(heart == Cell_Heart(CURRENT));
                is_optional = true;
            }
            else
                is_optional = false;

            if (
                heart == TYPE_GROUP
                or heart == TYPE_THE_GROUP
                or heart == TYPE_META_GROUP
            ){
                if (Eval_Any_List_At_Throws(SPARE, CURRENT, SPECIFIED)) {
                    Drop_Data_Stack_To(STACK_BASE);
                    goto return_thrown;
                }
                Decay_If_Unstable(SPARE);
                if (heart == TYPE_THE_GROUP)
                    Theify(stable_SPARE);  // transfer @ decoration to product
                else if (heart == TYPE_META_GROUP)
                    Metafy(stable_SPARE);  // transfer ^ decoration to product
                else if (heart == TYPE_GROUP and Is_Void(SPARE))
                    Init_Trash(SPARE);  // [(void)]: ... pass thru

                heart = Cell_Heart(SPARE);
                Copy_Cell(PUSH(), stable_SPARE);
            }
            else
                Copy_Cell(PUSH(), CURRENT);

            if (is_optional)  // so next phase won't worry about leading slash
                Set_Cell_Flag(TOP, STACK_NOTE_OPTIONAL);

            if (circle_this)
                circled = TOP_INDEX;

            if (
                // ^xxx is indicator of a ^META result [4]
                //
                (heart == TYPE_SIGIL and Cell_Sigil(TOP) == SIGIL_META)
                or heart == TYPE_META_WORD
                or heart == TYPE_META_TUPLE
            ){
                continue;
            }

            if (heart == TYPE_WORD or heart == TYPE_TUPLE)
                continue;

            if (Is_Space(TOP) or Is_Trash(TOP))  // nameless decay vs. no decay
                continue;

            return FAIL("SET-BLOCK! items are (@THE, ^META) WORD/TUPLE or ~/#");
        }

        level_->u.eval.stackindex_circled = circled;  // remember it

        Level* sub = Maybe_Rightward_Continuation_Needed(L);
        if (not sub)
            goto set_block_rightside_result_in_out;

        return CONTINUE_SUBLEVEL(sub);

    } set_block_rightside_result_in_out: {  //////////////////////////////////

      // 1. On definitional errors we don't assign variables, yet we pass the
      //    raised error through.  That permits code like this to work:
      //
      //        trap [[a b]: transcode "1&aa"]
      //
      // 2. We enumerate from left to right in the SET-BLOCK!, with the "main"
      //    being the first assigned to any variables.  This has the benefit
      //    that if any of the multi-returns were marked as "circled" then the
      //    overwrite of the returned OUT for the whole evaluation will happen
      //    *after* the original OUT was captured into any desired variable.

        if (Is_Lazy(OUT)) {
            //
            // A Lazy Object has a methodization moment here to turn itself
            // into multiple values--potentially a pack.  Ultimately we'd
            // want to be stackless about the reification, but for now make
            // it easy.
            //
            if (Pushed_Decaying_Level(OUT, OUT, LEVEL_MASK_NONE)) {
                if (Trampoline_With_Top_As_Root_Throws())
                    return FAIL(Error_No_Catch_For_Throw(TOP_LEVEL));
                Drop_Level(TOP_LEVEL);
            }
            if (Is_Lazy(OUT))  // Lazy -> Lazy not allowed, Lazy -> Pack is ok
                return FAIL("Lazy Object Reified to Lazy Object: Not Allowed");
        }

        const Source* pack_array;  // needs GC guarding when OUT overwritten
        const Element* pack_meta_at;  // pack block items are ^META'd
        const Element* pack_meta_tail;

        if (Is_Barrier(OUT))  // !!! Hack, want ([:foo]: eval) to always work
            Init_Nihil(OUT);

        if (Is_Pack(OUT)) {  // antiform block
            pack_meta_at = Cell_List_At(&pack_meta_tail, OUT);

            pack_array = Cell_Array(OUT);
            Push_Lifeguard(pack_array);
        }
        else {
            Meta_Quotify(OUT);  // standardize to align with pack items

            pack_meta_at = cast(Element*, OUT);
            pack_meta_tail = cast(Element*, OUT) + 1;  // not a valid cell

            pack_array = nullptr;
        }

        StackIndex stackindex_var = STACK_BASE + 1;  // [2]
        Option(StackIndex) circled = level_->u.eval.stackindex_circled;

        for (
            ;
            stackindex_var != TOP_INDEX + 1;
            ++stackindex_var, ++pack_meta_at
        ){
            bool is_optional = Get_Cell_Flag(
                Data_Stack_Cell_At(stackindex_var),
                STACK_NOTE_OPTIONAL
            );

            Element* var = CURRENT;  // stable location, safe across SET of var
            Copy_Cell(var, Data_Stack_At(Element, stackindex_var));

            assert(QUOTE_BYTE(var) == NOQUOTE_1 or Is_Trash(var));
            Heart var_heart = Cell_Heart(var);

            if (pack_meta_at == pack_meta_tail) {
                if (not is_optional)
                    return FAIL("Not enough values for required multi-return");

                // match typical input of meta which will be Meta_Unquotify'd
                // (special handling in TYPE_META_WORD and TYPE_META_TUPLE
                // below will actually use plain null to distinguish)
                //
                Init_Meta_Of_Null(SPARE);
            }
            else
                Copy_Cell(SPARE, pack_meta_at);

            if (var_heart == TYPE_SIGIL and Cell_Sigil(var) == SIGIL_META)
                goto circled_check;  // leave as meta the way it came in

            if (
                var_heart == TYPE_META_WORD
                or var_heart == TYPE_META_TUPLE
            ){
                if (pack_meta_at == pack_meta_tail) {  // special detection
                    Set_Var_May_Fail(var, SPECIFIED, LIB(NULL));
                    goto circled_check;
                }
                Set_Var_May_Fail(var, SPECIFIED, stable_SPARE);  // is meta'd
                goto circled_check;
            }

            Meta_Unquotify_Undecayed(SPARE);

            if (var_heart == TYPE_BLANK) {
                assert(Is_Trash(var));  // [~ ...]: -> no name, but don't decay
                goto circled_check;
            }

            if (Is_Raised(SPARE))  // don't pass thru raised errors if not @
                return FAIL(Cell_Error(SPARE));

            Decay_If_Unstable(SPARE);  // if pack in slot, resolve it

            if (var_heart == TYPE_ISSUE) {
                assert(Is_Space(var));  // [# ...]: -> no name, but decay
                goto circled_check;
            }

            if (
                var_heart == TYPE_WORD or var_heart == TYPE_TUPLE
                or var_heart == TYPE_THE_WORD or var_heart == TYPE_THE_TUPLE
            ){
                DECLARE_VALUE (dummy);
                if (Set_Var_Core_Throws(
                    dummy,
                    GROUPS_OK,
                    var,
                    SPECIFIED,
                    stable_SPARE
                )){
                    return FAIL(Error_No_Catch_For_Throw(L));
                }
            }
            else
                assert(false);

          circled_check:  // Note: no circling passes through the original OUT

            if (circled == stackindex_var)
                Copy_Cell(OUT, SPARE);
        }

        // We've just changed the values of variables, and these variables
        // might be coming up next.  Consider:
        //
        //     304 = [a]: test 1020
        //     a = 304
        //
        // The `a` was fetched and found to not be infix, and in the process
        // its value was known.  But then we assigned that a with a new value
        // in the implementation of SET-BLOCK! here, so, it's incorrect.
        //
        L_next_gotten = nullptr;

        if (pack_array)
            Drop_Lifeguard(pack_array);

        if (not circled and not Is_Pack(OUT))  // reverse quotification
            Meta_Unquotify_Undecayed(OUT);

    } set_block_drop_stack_and_continue: {  //////////////////////////////////

        Drop_Data_Stack_To(STACK_BASE);  // drop writeback variables
        goto lookahead; }


    //=//// META-BLOCK! ////////////////////////////////////////////////////=//
    //
    // Just produces a quoted version of the block it is given:
    //
    //    >> ^[a b c]
    //    == '[a b c]
    //
    // (It's hard to think of another meaning that would be sensible.)

      case TYPE_META_BLOCK:
        Inertly_Derelativize_Inheriting_Const(OUT, CURRENT, L->feed);
        HEART_BYTE(OUT) = TYPE_BLOCK;
        Quotify(OUT);
        goto lookahead;


    //=//// FENCE! ////////////////////////////////////////////////////////=//
    //
    // FENCE! is the newest part in the box, and it's not clear exactly how
    // it will work yet.

      case TYPE_FENCE:
        return FAIL("Precise behavior of FENCE! not known yet");


    //=//// META-FENCE! ///////////////////////////////////////////////////=//

      case TYPE_META_FENCE:
        return FAIL("Don't know what META-FENCE! is going to do yet");


    //=//// THE-XXX! //////////////////////////////////////////////////////=//
    //
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
    // Leaving the sigil means IMPORT can typecheck for THE-WORD! + THE-PATH!
    // and not have a degree of freedom that it can't distinguish from being
    // called as (import 'xml) or (import 'json/1.1.2)

      case TYPE_THE_BLOCK:
      case TYPE_THE_FENCE:
      case TYPE_THE_GROUP:
      case TYPE_THE_WORD:
      case TYPE_THE_PATH:
      case TYPE_THE_CHAIN:
      case TYPE_THE_TUPLE:
        Inertly_Derelativize_Inheriting_Const(OUT, CURRENT, L->feed);
        goto lookahead;


    //=///// VAR-XXX! /////////////////////////////////////////////////////=//
    //
    // The $xxx types evaluate to remove the decoration, but be bound:
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

      case TYPE_VAR_BLOCK:
      case TYPE_VAR_FENCE:
      case TYPE_VAR_GROUP:
      case TYPE_VAR_WORD:
      case TYPE_VAR_PATH:
      case TYPE_VAR_TUPLE:
      case TYPE_VAR_CHAIN:
        Inertly_Derelativize_Inheriting_Const(OUT, CURRENT, L->feed);
        HEART_BYTE(OUT) = Plainify_Any_Var_Heart(cast(Heart, STATE));
        goto lookahead;


      case TYPE_BLOCK:
        //
      case TYPE_BLOB:
        //
      case TYPE_TEXT:
      case TYPE_FILE:
      case TYPE_EMAIL:
      case TYPE_URL:
      case TYPE_TAG:
      case TYPE_ISSUE:
        //
      case TYPE_BITSET:
        //
      case TYPE_MAP:
        //
      case TYPE_VARARGS:
        //
      case TYPE_OBJECT:
      case TYPE_MODULE:
      case TYPE_ERROR:
      case TYPE_PORT:
        goto inert;


    //=///////////////////////////////////////////////////////////////////=//
    //
    // Treat all the other NOT Is_Bindable() types as inert
    //
    //=///////////////////////////////////////////////////////////////////=//

    inert:
      case TYPE_BLANK:  // once blanks evaluated to null, but that was panned
      case TYPE_INTEGER:
      case TYPE_DECIMAL:
      case TYPE_PERCENT:
      case TYPE_MONEY:
      case TYPE_PAIR:
      case TYPE_TIME:
      case TYPE_DATE:
        //
      case TYPE_PARAMETER:
        //
      case TYPE_TYPE_BLOCK:
      case TYPE_TYPE_FENCE:
      case TYPE_TYPE_GROUP:
      case TYPE_TYPE_WORD:
      case TYPE_TYPE_PATH:
      case TYPE_TYPE_CHAIN:
      case TYPE_TYPE_TUPLE:
        //
      case TYPE_HANDLE:

        Inertly_Derelativize_Inheriting_Const(OUT, CURRENT, L->feed);
        goto lookahead;


    //=//// GARBAGE (pseudotypes or otherwise //////////////////////////////=//

      default:
        panic (CURRENT);
    }

  //=//// END MAIN SWITCH STATEMENT ///////////////////////////////////////=//

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
    // 1. With COMMA!, we skip the lookahead step, which means (then [...])
    //    will have the same failure mode as (1 + 2, then [...]).  In order
    //    to make this the same behavior anything else that evaluates to
    //    a barrier (COMMA! antiform) we make this hinge on producing a
    //    barrier--not on being a source level comma.  Note it's different
    //    from what would happen with (nihil then [...]) which shows a nuance
    //    between barriers and nihils.
    //
    // 2. If something was run with the expectation it should take the next
    //    arg from the output cell, and an evaluation cycle ran that wasn't
    //    an ACTION! (or that was an arity-0 action), that's not what was
    //    meant.  But it can happen, e.g. `x: 10 | x ->-`, where ->- doesn't
    //    get an opportunity to quote left because it has no argument...and
    //    instead retriggers and lets x run.

  lookahead:

    if (Is_Barrier(OUT)) {
      skip_lookahead:
        assert(Is_Barrier(OUT));  // only jump in for barriers [1]
        goto finished;
    }

    if (Get_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH))
        return FAIL(Error_Literal_Left_Path_Raw());


  //=//// IF NOT A WORD!, IT DEFINITELY STARTS A NEW EXPRESSION ///////////=//

    // For long-pondered technical reasons, only WORD! is able to dispatch
    // infix.  If it's necessary to dispatch an infix function via path, then
    // a word is used to do it, like `>-` in `x: >- lib/method [...] [...]`.

    if (Is_Feed_At_End(L->feed)) {
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
        goto finished;  // hitting end is common, avoid do_next's switch()
    }

    switch (Type_Of_Unchecked(L_next)) {
      case TYPE_WORD:
        if (not L_next_gotten)
            L_next_gotten = Lookup_Word(L_next, Feed_Binding(L->feed));
        else
            assert(L_next_gotten == Lookup_Word(L_next, Feed_Binding(L->feed)));
        break;  // need to check for lookahead

      case TYPE_FRAME:
        L_next_gotten = L_next;
        break;

      default:
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
        goto finished;
    }

  //=//// FETCH WORD! TO PERFORM SPECIAL HANDLING FOR INFIX/INVISIBLES ////=//

    // First things first, we fetch the WORD! (if not previously fetched) so
    // we can see if it looks up to any kind of ACTION! at all.


  //=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-INFIX ///////////=//

    // These cases represent finding the start of a new expression.
    //
    // Fall back on word-like "dispatch" even if ->gotten is null (unset or
    // unbound word).  It'll be an error, but that code path raises it for us.

 { Option(InfixMode) infix_mode;

    if (
        not L_next_gotten
        or (
            not (Is_Word(L_next) and Is_Action(unwrap L_next_gotten))
            and not Is_Frame(L_next)
            and not Is_Sigil(L_next)
        )
        or not (infix_mode = Cell_Frame_Infix_Mode(unwrap L_next_gotten))
    ){
      lookback_quote_too_late: // run as if starting new expression

        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
        Clear_Eval_Executor_Flag(L, INERT_OPTIMIZATION);

        goto finished;
    }

  //=//// IS WORD INFIXEDLY TIED TO A FUNCTION (MAY BE "INVISIBLE") ///////=//

  blockscope {
    Phase* infixed = Cell_Frame_Phase(unwrap L_next_gotten);
    ParamList* paramlist = Phase_Paramlist(infixed);

    if (Get_Flavor_Flag(VARLIST, paramlist, PARAMLIST_LITERAL_FIRST)) {
        //
        // Left-quoting by infix needs to be done in the lookahead before an
        // evaluation, not this one that's after.  This happens in cases like:
        //
        //     /left-the: infix func [@value] [value]
        //     the <something> left-the
        //
        // But due to the existence of <end>-able parameters, the left quoting
        // function might be okay with seeing nothing on the left.  Start a
        // new expression and let it error if that's not ok.
        //
        assert(Not_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH));
        if (Get_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH))
            return FAIL(Error_Literal_Left_Path_Raw());

        const Param* first = First_Unspecialized_Param(nullptr, infixed);
        if (Cell_ParamClass(first) == PARAMCLASS_SOFT) {
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
            return FAIL(Error_Ambiguous_Infix_Raw());
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
    goto process_action; }}

  finished:

    // Want to keep this flag between an operation and an ensuing infix in
    // the same level, so can't clear in Drop_Action(), e.g. due to:
    //
    //     /left-the: infix the/
    //     o: make object! [/f: does [1]]
    //     o.f left-the  ; want error suggesting >- here, need flag for that
    //
    Clear_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH);

  #if RUNTIME_CHECKS
    Evaluator_Exit_Checks_Debug(L);

    assert(STATE != ST_STEPPER_INITIAL_ENTRY);
    STATE = ST_STEPPER_FINISHED_DEBUG;  // must reset to STATE_0 if reused
  #endif

    return OUT;  // trampoline checks that OUT is not unreadable/erased

  return_thrown:

  #if RUNTIME_CHECKS
    Evaluator_Exit_Checks_Debug(L);
  #endif

    return BOUNCE_THROWN;
}
