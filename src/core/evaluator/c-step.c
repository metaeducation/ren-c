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
//   into functions would add overhead (in the debug build if not also release
//   builds) and prevent interesting tricks and optimizations.  It is
//   separated into sections, and the invariants in each section are made
//   clear with comments and asserts.
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
#define L_current           cast(const Element*, &L->u.eval.current)
#define L_current_gotten    L->u.eval.current_gotten

#define level_ L  // for OUT, SPARE, STATE macros

#define CURRENT cast(Element*, &(L->u.eval.current))

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
// leading-blank paths as in ([foo /bar]: 10).  Once the work of extracting
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
// 1. Note that any enfix quoting operators that would quote backwards to see
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
        fail (Error_Need_Non_End(L_current));

    Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);  // always >= 2 elements [2]

    Flags flags =  // v-- if L was fulfilling, we are
        (L->flags.bits & EVAL_EXECUTOR_FLAG_FULFILLING_ARG)
        | LEVEL_FLAG_RAISED_RESULT_OK;  // trap [e: transcode "1&aa"] works

    Level* sub = Make_Level(
        &Stepper_Executor,
        L->feed,
        flags  // inert optimize adjusted the flags to jump in mid-eval
    );
    Push_Level(OUT, sub);

    return sub;
}

bool Is_Action_Deferring(Value* v) {
    return Get_Action_Flag(VAL_ACTION(v), DEFERS_LOOKBACK);
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

    assert(TOP_INDEX >= BASELINE->stack_base);  // e.g. REDUCE accrues
    assert(OUT != SPARE);  // overwritten by temporary calculations

    if (Get_Eval_Executor_Flag(L, NO_EVALUATIONS)) {  // see flag for rationale
        if (Is_Feed_At_End(L->feed))
            return OUT;
        Derelativize(OUT, At_Feed(L->feed), FEED_BINDING(L->feed));
        Fetch_Next_In_Feed(L->feed);
        return OUT;
    }

    // Given how the evaluator is written, it's inevitable that there will
    // have to be a test for points to `goto` before running normal eval.
    // This cost is paid on every entry to Eval_Core().
    //
    switch (STATE) {
      case ST_STEPPER_INITIAL_ENTRY:
        goto initial_entry;

      case ST_STEPPER_LOOKING_AHEAD:
        goto lookahead;

      case ST_STEPPER_REEVALUATING: {  // v-- IMPORTANT: Keep STATE
        //
        // It's important to leave STATE as ST_STEPPER_REEVALUATING
        // during the switch state, because that's how the evaluator knows
        // not to redundantly apply LET bindings.  See `L_binding` above.

        // The re-evaluate functionality may not want to heed the enfix state
        // in the action itself.  See DECLARE_NATIVE(shove)'s /ENFIX for instance.
        // So we go by the state of a flag on entry.
        //
        if (L->u.eval.enfix_reevaluate == 'N') {
            // either not enfix or not an action
        }
        else {
            assert(L->u.eval.enfix_reevaluate == 'Y');

            Level* sub = Make_Action_Sublevel(L);
            Push_Level(OUT, sub);
            Push_Action(
                sub,
                VAL_ACTION(L_current),
                Cell_Frame_Coupling(L_current)
            );
            Begin_Enfix_Action(sub, VAL_FRAME_LABEL(L_current));
                // ^-- invisibles cache NO_LOOKAHEAD

            assert(Is_Fresh(SPARE));
            goto process_action;
        }

        Freshen_Cell(OUT);

        L_current_gotten = nullptr;  // !!! allow/require to be passe in?
        goto evaluate; }

      intrinsic_arg_in_spare:
      case ST_STEPPER_CALCULATING_INTRINSIC_ARG: {
        Action* action = VAL_ACTION(L_current);
        assert(Is_Stub_Details(action));
        Intrinsic* intrinsic = Extract_Intrinsic(cast(Phase*, action));
        Param* param = ACT_PARAM(action, 2);

        if (Cell_ParamClass(param) == PARAMCLASS_META)
            Meta_Quotify(SPARE);
        else
            Decay_If_Unstable(SPARE);  // error parity with non-intrinsic
        if (not Typecheck_Coerce_Argument(param, SPARE)) {
            Option(const Symbol*) label = VAL_FRAME_LABEL(L_current);
            const Key* key = ACT_KEY(action, 2);
            fail (Error_Arg_Type(label, key, param, stable_SPARE));
        }
        (*intrinsic)(OUT, cast(Phase*, action), stable_SPARE);
        goto lookahead; }

      case REB_SIGIL:
        goto sigil_rightside_in_out;

      case REB_GROUP:
      case REB_META_GROUP:
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

      case REB_FRAME:
        goto lookahead;

      default:
        assert(false);
    }

  #if !defined(NDEBUG)
    Evaluator_Expression_Checks_Debug(L);
  #endif

  initial_entry: {  //////////////////////////////////////////////////////////

  // This starts a new expression.

    Sync_Feed_At_Cell_Or_End_May_Fail(L->feed);

    Update_Expression_Start(L);  // !!! See Level_Array_Index() for caveats

    if (Is_Level_At_End(L)) {
        Init_Void(OUT);
        goto finished;
    }

    L_current_gotten = L_next_gotten;  // Lookback clears it
    Copy_Cell(CURRENT, L_next);
    Fetch_Next_In_Feed(L->feed);

} evaluate: ;  // meaningful semicolon--subsequent macro may declare things

    // ^-- doesn't advance expression index: `reeval x` starts with `reeval`

  //=//// LOOKAHEAD FOR ENFIXED FUNCTIONS THAT QUOTE THEIR LEFT ARG ///////=//

    if (Is_Level_At_End(L))
        goto give_up_backward_quote_priority;

    assert(not L_next_gotten);  // Fetch_Next_In_Feed() cleared it

    if (VAL_TYPE_UNCHECKED(L_next) == REB_WORD) {  // right's kind
        //
        // !!! Using L_binding here instead of FEED_BINDING(L->feed)
        // seems to break `let x: me + 1`, due to something about the
        // conditionality on reevaluation.  L_binding's conditionality
        // should be reviewed for relevance in the modern binding model.
        //
        L_next_gotten = Lookup_Word(L_next, FEED_BINDING(L->feed));

        if (
            not L_next_gotten
            or not Is_Action(unwrap L_next_gotten)
        ){
            goto give_up_backward_quote_priority;  // note only ACTION! is ENFIXED
        }
    }
    else
        goto give_up_backward_quote_priority;

    if (Not_Enfixed(unwrap L_next_gotten))
        goto give_up_backward_quote_priority;

  blockscope {
    Action* enfixed = VAL_ACTION(unwrap L_next_gotten);
    Array* paramlist = ACT_PARAMLIST(enfixed);

    if (Not_Subclass_Flag(VARLIST, paramlist, PARAMLIST_QUOTES_FIRST))
        goto give_up_backward_quote_priority;

    ParamClass pclass = Cell_ParamClass(  // !!! Should cache this in frame
        First_Unspecialized_Param(nullptr, enfixed)
    );

    // If the action soft quotes its left, that means it's aware that its
    // "quoted" argument may be evaluated sometimes.  If there's evaluative
    // material on the left, treat it like it's in a group.
    //
    if (
        Get_Action_Flag(enfixed, POSTPONES_ENTIRELY)
        or (
            Get_Feed_Flag(L->feed, NO_LOOKAHEAD)
            and not Any_Set_Value(L_current)
        )
    ){
        if (pclass == PARAMCLASS_NORMAL or pclass == PARAMCLASS_META)
            goto give_up_backward_quote_priority;  // yield as an exemption
    }

    // Lookback args are fetched from OUT, then copied into an arg slot.
    // Put the backwards quoted value into OUT.  (Do this before next
    // step because we need value for type check)
    //
    if (pclass == PARAMCLASS_JUST)  // enfix func ['x ...] [...]
        Copy_Cell(OUT, L_current);
    else {
        assert(
            pclass == PARAMCLASS_THE  // enfix func [@x ...] [...]
            or pclass == PARAMCLASS_SOFT
        );
        Derelativize(OUT, L_current, L_binding);
    }

    // We skip over the word that invoked the action (e.g. ->-, OF, =>).
    // CURRENT will then hold that word.  (OUT holds what was to the left)
    //
    L_current_gotten = L_next_gotten;
    Copy_Cell(CURRENT, L_next);
    Fetch_Next_In_Feed(L->feed);

    if (
        Is_Feed_At_End(L->feed)  // v-- OUT is what used to be on left
        and (
            VAL_TYPE_UNCHECKED(OUT) == REB_WORD
            or VAL_TYPE_UNCHECKED(OUT) == REB_PATH
        )
    ){
        // We make a special exemption for left-stealing arguments, when
        // they have nothing to their right.  They lose their priority
        // and we run the left hand side with them as a priority instead.
        // This lets us do (the ->) or (help of)
        //
        // Swap it around so that what we had put in OUT goes to being in
        // CURRENT, and the current is put back into the feed.

        Move_Cell(&L->feed->fetched, CURRENT);
        L->feed->p = &L->feed->fetched;
        L->feed->gotten = L_current_gotten;

        Move_Cell(CURRENT, cast(Element*, OUT));
        L_current_gotten = nullptr;

        Set_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH);

        if (Is_Word(CURRENT)) {
            STATE = REB_WORD;
            goto word_common;
        }

        assert(Is_Path(CURRENT));
        STATE = REB_PATH;
        goto path_common;
    }
  }

    // Wasn't the at-end exception, so run normal enfix with right winning.
    //
  blockscope {
    Level* sub = Make_Action_Sublevel(L);
    Push_Level(OUT, sub);
    Push_Action(
        sub,
        VAL_ACTION(unwrap L_current_gotten),
        Cell_Frame_Coupling(unwrap L_current_gotten)
    );
    if (Is_Word(L_current))
        Begin_Enfix_Action(sub, Cell_Word_Symbol(L_current));
    else
        Begin_Enfix_Action(sub, VAL_FRAME_LABEL(L_current));

    goto process_action; }

  give_up_backward_quote_priority:

  //=//// BEGIN MAIN SWITCH STATEMENT /////////////////////////////////////=//

    // This switch is done with a case for all REB_XXX values, in order to
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
    // 2. The Stepper_Executor()'s state bytes are a superset of the VAL_TYPE
    //    of processed values.  See the ST_STEPPER_XXX enumeration.

    assert(Is_Fresh(OUT));

    if (QUOTE_BYTE(L_current) != NOQUOTE_1) {  // quasiform or quoted [1]
        assert(QUOTE_BYTE(L_current) != ANTIFORM_0);
        Copy_Cell(OUT, L_current);
        QUOTE_BYTE(OUT) -= 2;  // QUASI_2 => ANTIFORM_0, or drops one quote
    }
    else switch ((STATE = HEART_BYTE(L_current))) {  // states include type [2]

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

      case REB_COMMA:
        Init_Barrier(OUT);
        goto skip_lookahead;  // skip lookahead, see notes there


    //=//// FRAME! ////////////////////////////////////////////////////////=//
    //
    // If a FRAME! makes it to the SWITCH statement, that means it is either
    // literally a frame in the array (eval compose [(unrun :add) 1 2]) or is
    // being retriggered via REEVAL.
    //
    // Most FRAME! evaluations come from the antiform ("actions") triggered
    // from a WORD! or PATH! case.)
    //
    // 1. If an enfix function is run at this moment, it will not have a left
    //    hand side argument.

      case REB_FRAME: {
        if (IS_FRAME_PHASED(L_current))  // running frame if phased
            fail ("Use REDO to restart a running FRAME! (can't EVAL)");

        Level* sub = Make_Action_Sublevel(L);
        Push_Level(OUT, sub);
        Push_Action(
            sub,
            VAL_ACTION(L_current),
            Cell_Frame_Coupling(L_current)
        );
        bool enfix = Is_Enfixed(L_current);
        assert(Is_Fresh(OUT));  // so nothing on left [1]
        Begin_Action_Core(sub, VAL_FRAME_LABEL(L_current), enfix);

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
        STATE = REB_FRAME;
        return CATCH_CONTINUE_SUBLEVEL(TOP_LEVEL); }


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

      case REB_SIGIL: {
        Sigil sigil = Cell_Sigil(L_current);
        switch (sigil) {
          case SIGIL_QUOTE:
          case SIGIL_THE: {
            if (Is_Feed_At_End(L->feed))  // no literal to take if (@), (')
                fail (Error_Need_Non_End(L_current));

            assert(Not_Feed_Flag(L->feed, NEEDS_SYNC));
            const Element* elem = c_cast(Element*, L->feed->p);

            bool antiform = Get_Cell_Flag(elem, FEED_NOTE_META);  // [2]
            Clear_Cell_Flag(m_cast(Element*, elem), FEED_NOTE_META);  // [3]

            if (sigil == SIGIL_THE)
                The_Next_In_Feed(L->out, L->feed);  // !!! review enfix interop
            else {
                assert(sigil == SIGIL_QUOTE);
                Just_Next_In_Feed(L->out, L->feed);  // !!! review enfix
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

            return CATCH_CONTINUE_SUBLEVEL(right); }

          case SIGIL_QUASI:  // ~~
            fail ("No evaluator behavior defined for ~~ yet");

          default:
            assert(false);
        }
        goto lookahead; }

      sigil_rightside_in_out: {
        switch (Cell_Sigil(L_current)) {
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
                fail ("$ operator cannot bind antiforms");
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
    // NOTE: The usual dispatch of enfix functions is *not* via a REB_WORD in
    // this switch, it's by some code at the `lookahead:` label.  You only see
    // enfix here when there was nothing to the left, so cases like `(+ 1 2)`
    // or in "stale" left hand situations like `10 comment "hi" + 20`.

      word_common: ///////////////////////////////////////////////////////////

      case REB_WORD: {
        Option(Error*) error = Trap_Get_Any_Word(OUT, L_current, L_binding);
        if (error)
            fail (unwrap error);  // else could conflate with function result

        if (Is_Action(OUT)) {
            Action* action = VAL_ACTION(OUT);
            bool enfixed = Is_Enfixed(OUT);
            Option(VarList*) coupling = Cell_Frame_Coupling(OUT);
            const Symbol* label = Cell_Word_Symbol(L_current);  // use WORD!
            Erase_Cell(OUT);  // sanity check, plus don't want enfix to see

            if (enfixed) {
                if (
                    Get_Action_Flag(action, POSTPONES_ENTIRELY)
                    or Get_Action_Flag(action, DEFERS_LOOKBACK)
                ){
                    if (Get_Eval_Executor_Flag(L, FULFILLING_ARG)) {
                        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
                        Set_Feed_Flag(L->feed, DEFERRING_ENFIX);
                        goto finished;
                    }
                }
            }

            if (Get_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH)) {
                if (enfixed)
                    assert(false);  // !!! this won't work, can it happen?

                Clear_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH);
            }

            if (
                not enfixed  // too rare a case for intrinsic optimization
                and ACT_DISPATCHER(action) == &Intrinsic_Dispatcher
                and Is_Stub_Details(action)  // don't do specializations
                and Not_Level_At_End(L)  // can't do <end>, fallthru to error
                and not SPORADICALLY(10)  // debug build bypass every 10th call
            ){
                Init_Frame_Details_Core(
                    CURRENT,
                    cast(Phase*, action),  // !!! is this legitimate?
                    label,
                    coupling
                );
                Param* param = ACT_PARAM(action, 2);
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
                    fail ("Unsupported parameter convention for intrinsic");
                }

                Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);  // when non-enfix call

                Level* sub = Make_Level(&Stepper_Executor, L->feed, flags);
                Push_Level(SPARE, sub);
                STATE = ST_STEPPER_CALCULATING_INTRINSIC_ARG;
                return CATCH_CONTINUE_SUBLEVEL(sub);
            }

            Level* sub = Make_Action_Sublevel(L);
            Push_Level(OUT, sub);
            Push_Action(sub, action, coupling);
            Begin_Action_Core(sub, label, enfixed);

            goto process_action;
        }

        if (Any_Vacancy(stable_OUT))  // checked second
            fail (Error_Bad_Word_Get(L_current, OUT));

        goto lookahead; }


    //=//// CHAIN! ////////////////////////////////////////////////////////=//
    //
    // Due to the consolidation of all the SET-XXX! and GET-XXX! types as
    // CHAIN! with leading or trailing blanks, CHAIN! has to break that down
    // and dispatch to the appropriate behavior.

      case REB_CHAIN: {
        bool leading_blank;
        switch (
            Try_Get_Sequence_Singleheart(&leading_blank, L_current)
        ){
          case REB_0:
            break;  // wasn't xxx: or :xxx where xxx is BLOCK!/CHAIN!/WORD!/etc

          case REB_WORD:  // GET-WORD! or SET-WORD!
            if (leading_blank) {  // will be refinement, likely error on eval
                Unchain(CURRENT);
                STATE = ST_STEPPER_GET_WORD;
                goto handle_get_word;
            }
            Derelativize(  // !!! binding may be sensitive to "set-words only"
                SPARE, L_current, L_binding
            );
            Unchain(Copy_Cell(CURRENT, cast(Element*, SPARE)));
            STATE = ST_STEPPER_SET_WORD;
            goto handle_generic_set;

          case REB_BLOCK:  // GET-BLOCK! or SET-BLOCK!
            Unchain(CURRENT);
            if (leading_blank) {  // REDUCE, not the best idea...
                Derelativize(SPARE, L_current, L_binding);
                if (rebRunThrows(
                    cast(Value*, OUT),  // <-- output, API won't make atoms
                    Canon(REDUCE), SPARE
                )){
                    goto return_thrown;
                }
                goto lookahead;
            }
            STATE = ST_STEPPER_SET_BLOCK;
            goto handle_set_block;

          case REB_TUPLE:
            Unchain(CURRENT);
            if (leading_blank) {
                STATE = ST_STEPPER_GET_TUPLE;
                goto handle_get_tuple;
            }
            STATE = ST_STEPPER_SET_TUPLE;
            goto handle_generic_set;

          case REB_GROUP: {
            Unchain(CURRENT);
            if (leading_blank)
                fail ("GET-GROUP! has no evaluator meaning at this time");

            L_next_gotten = nullptr;  // arbitrary code changes fetched vars
            Init_Void(Alloc_Evaluator_Primed_Result());
            Level* sub = Make_Level_At_Core(
                &Evaluator_Executor,
                L_current,
                L_binding,
                LEVEL_MASK_NONE
            );
            Push_Level(SPARE, sub);

            STATE = ST_STEPPER_SET_GROUP;
            return CATCH_CONTINUE_SUBLEVEL(sub); }

          default:  // it's just something like :1 or <tag>:
            fail ("No current eval behavior for things like :1 or <tag>:");
        }

        Option(Error*) error = Trap_Get_Chain_Push_Refinements(
            OUT,  // where to write action
            SPARE,  // temporary GC-safe scratch space
            L_current,
            L_binding
        );
        if (error)  // lookup failed, a GROUP! in path threw, etc.
            fail (unwrap error);  // don't definitional error for now

        assert(Is_Action(OUT));

        if (Is_Enfixed(OUT)) {  // too late, left already evaluated
            Drop_Data_Stack_To(BASELINE->stack_base);
            fail ("Use `->-` to shove left enfix operands into CHAIN!s");
        }
        goto handle_action_in_out_with_refinements_pushed; }

     handle_action_in_out_with_refinements_pushed: { /////////////////////////

        Level* sub = Make_Action_Sublevel(L);
        sub->baseline.stack_base = BASELINE->stack_base;  // refinements

        Action* action = VAL_ACTION(OUT);
        Option(VarList*) coupling = Cell_Frame_Coupling(OUT);
        Option(const Symbol*) label = VAL_FRAME_LABEL(OUT);

        Push_Level(OUT, sub);
        Push_Action(sub, action, coupling);
        Begin_Prefix_Action(sub, label);
        goto process_action; }


    //=//// GET-WORD! /////////////////////////////////////////////////////=//
    //
    // A GET-WORD! gives you the contents of a variable as-is, with no
    // dispatch on functions.  This includes antiforms.
    //
    // https://forum.rebol.info/t/1301

      handle_get_word:  // jumps here for CHAIN! that's like a GET-WORD!
      case REB_META_WORD: {
        assert(
            (STATE == ST_STEPPER_GET_WORD and Is_Word(L_current))
            or (STATE == REB_META_WORD and Is_Meta_Word(L_current))
        );
        Option(Error*) error = Trap_Get_Any_Word_Maybe_Vacant(
            OUT,
            L_current,
            L_binding
        );
        if (error)
            fail (unwrap error);

        if (STATE == REB_META_WORD)
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

      case REB_GROUP:
      case REB_META_GROUP: {
        L_next_gotten = nullptr;  // arbitrary code changes fetched variables

        Flags flags = LEVEL_FLAG_RAISED_RESULT_OK;  // [2]

        if (STATE == REB_META_GROUP)
            flags |= LEVEL_FLAG_META_RESULT;

        Init_Nihil(Alloc_Evaluator_Primed_Result());
        Level* sub = Make_Level_At_Core(
            &Evaluator_Executor,
            L_current,
            L_binding,
            flags
        );
        Push_Level(OUT, sub);

        return CATCH_CONTINUE_SUBLEVEL(sub); }


    //=//// TUPLE! /////////////////////////////////////////////////////////=//
    //
    // TUPLE! runs through an extensible mechanism based on PICK* and POKE*.
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

      case REB_TUPLE: {
        Copy_Sequence_At(SPARE, L_current, 0);
        if (
            not Is_Blank(SPARE)  // `.a` means pick member from "self"
            and Any_Inert(SPARE)  // `1.2.3` is inert
        ){
            Derelativize(OUT, L_current, L_binding);
            goto lookahead;
        }

        Option(Error*) error = Trap_Get_Any_Tuple(  // vacant will cause error
            OUT,
            GROUPS_OK,
            L_current,
            L_binding
        );
        if (error) {  // tuples never run actions, won't conflate to raise it
            Init_Error(OUT, unwrap error);
            Raisify(OUT);
            goto lookahead;  // e.g. EXCEPT might want error
        }

        if (Is_Action(OUT))  // don't conflate with NOT-FOUND for TRY
            fail ("Can't fetch actions (FRAME! antiform) with TUPLE!");

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
    // 6. The left hand side does not look ahead at paths to find enfix
    //    functions.  This is because PATH! dispatch is costly and can error
    //    in more ways than sniffing a simple WORD! for enfix can.  So the
    //    prescribed way of running enfix with paths is `left ->- right/side`,
    //    which uses an infix WORD! to mediate the interaction.

      path_common:
      case REB_PATH: {
        bool slash_at_head;
        bool slash_at_tail;
        Heart path_heart = maybe Try_Get_Sequence_Singleheart(
            &slash_at_head, L_current
        );
        switch (path_heart) {
          case REB_0: {
            Copy_Sequence_At(SPARE, L_current, 0);
            if (Any_Inert(SPARE)) {
                if (Is_Blank(SPARE))
                    slash_at_head = true;
                else {
                    Derelativize(OUT, L_current, L_binding);  // inert [2]
                    goto lookahead;
                }
            }
            else
                slash_at_head = false;

            Length len = Cell_Sequence_Len(L_current);
            Copy_Sequence_At(SPARE, L_current, len - 1);
            slash_at_tail = Is_Blank(SPARE);
            break; }

          case REB_WORD: {
            if (slash_at_head)
                fail ("Killing off refinement evaluations!");
            slash_at_tail = true;
            break; }

          case REB_CHAIN:  // /abc: or /abc:def or abc:def/ or /abc.def: ...
            if (slash_at_head) {
                Unpath(CURRENT);
                bool colon_at_head;
                Option(Heart) chain_heart = Try_Get_Sequence_Singleheart(
                    &colon_at_head, L_current
                );
                if (colon_at_head)
                    fail ("No evaluator meaning for /:xxx at this time");
                if (chain_heart == REB_WORD) {
                    Unchain(CURRENT);
                    Set_Cell_Flag(CURRENT, CURRENT_NOTE_SET_ACTION);
                    STATE = ST_STEPPER_SET_WORD;
                    goto handle_generic_set;
                }
                if (chain_heart == REB_TUPLE) {
                    Unchain(CURRENT);
                    Set_Cell_Flag(CURRENT, CURRENT_NOTE_SET_ACTION);
                    STATE = ST_STEPPER_SET_TUPLE;
                    goto handle_generic_set;
                }
                fail ("Unknown evaluator case for /xxx:...");
            }
            slash_at_tail = true;
            break;

          default:
            slash_at_tail = not slash_at_head;
            break;
        }

        Option(Error*) error = Trap_Get_Path_Push_Refinements(
            OUT,  // where to write action
            SPARE,  // temporary GC-safe scratch space
            L_current,
            L_binding
        );
        if (error) {  // lookup failed, a GROUP! in path threw, etc.
            if (not slash_at_tail)
                fail (unwrap error);  // definitional error would conflate [3]
            fail (unwrap error);  // don't definitional error for now [4]
        }

        assert(Is_Action(OUT));
        if (slash_at_tail) {  // do not run action, just return it [5]
            if (BASELINE->stack_base != TOP_INDEX) {
                if (Specialize_Action_Throws(
                    SPARE, stable_OUT, nullptr, BASELINE->stack_base
                )){
                    goto return_thrown;
                }
                Move_Cell(OUT, SPARE);
            }
            goto lookahead;
        }

        if (Is_Enfixed(OUT)) {  // too late, left already evaluated [6]
            Drop_Data_Stack_To(BASELINE->stack_base);
            fail ("Use `->-` to shove left enfix operands into PATH!s");
        }

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
            (STATE == ST_STEPPER_SET_WORD and Is_Word(L_current))
            or (STATE == ST_STEPPER_SET_TUPLE and Is_Tuple(L_current))
            or (STATE == ST_STEPPER_SET_VOID and Is_Meta_Of_Void(L_current))
        );

        Level* right = Maybe_Rightward_Continuation_Needed(L);
        if (not right)
            goto generic_set_rightside_in_out;

        return CATCH_CONTINUE_SUBLEVEL(right);

    } generic_set_rightside_in_out: {  ///////////////////////////////////////

        if (Is_Barrier(OUT))  // even `(void):,` needs to error
            fail (Error_Need_Non_End(L_current));  // !!! vs. return_thrown ?

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
                    INIT_VAL_ACTION_LABEL(OUT, Cell_Word_Symbol(L_current));
            }
            else {  // assignments of /foo: or /obj.field: require action
                if (Get_Cell_Flag(L_current, CURRENT_NOTE_SET_ACTION))
                    fail ("/word: and /obj.field: assignments need action");
            }

            if (Set_Var_Core_Throws(  // cheaper on fail vs. Set_Var_May_Fail()
                SPARE,
                GROUPS_OK,
                L_current,
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
        else switch (VAL_TYPE(SPARE)) {
          case REB_BLOCK :
            Copy_Cell(CURRENT, cast(Element*, SPARE));
            STATE = ST_STEPPER_SET_BLOCK;
            goto handle_set_block;

          case REB_WORD :
            Copy_Cell(CURRENT, cast(Element*, SPARE));
            STATE = ST_STEPPER_SET_WORD;
            goto handle_generic_set;

          case REB_TUPLE :
            Copy_Cell(CURRENT, cast(Element*, SPARE));
            STATE = ST_STEPPER_SET_TUPLE;
            goto handle_generic_set;

          default:
            fail ("Unknown type for use in SET-GROUP!");
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
      case REB_META_TUPLE: {
        assert(
            (STATE == ST_STEPPER_GET_TUPLE and Is_Tuple(L_current))
            or (STATE == REB_META_TUPLE and Is_Meta_Tuple(L_current))
        );
        Option(Error*) error = Trap_Get_Any_Tuple_Maybe_Vacant(
            OUT,
            GROUPS_OK,
            L_current,
            L_binding
        );
        if (error) {
            Init_Error(OUT, unwrap error);
            Raisify(OUT);
            goto lookahead;  // e.g. EXCEPT might want to see raised error
        }

        if (STATE == REB_META_TUPLE)
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
      // 3. @xxx indicates a desire for a "circled" result.  By default, the
      //    ordinary return result will be returned.  (While checking we set
      //    stackindex_circled when we see `[@ ...]: ...` to give an error if
      //    more than one return were circled.)
      //
      // 4. ^xxx indicate a desire to get a "meta" result.
      //
      //    !!! How to circle a ^META result?  Should it be legal to write
      //    ^(@) or @(^) and not call into the evaluator so those cases do
      //    not fail on missing arguments?  (Real solution is anticipated
      //    as changing from using @ to using {fence} once it exists.)
      //
      //    !!! The multi-return mechanism doesn't allow an arbitrary number
      //    of meta steps, just one.  Should you be able to say ^(^(x)) or
      //    something like that to add more?  :-/
      //

      handle_set_block: {
        assert(STATE == ST_STEPPER_SET_BLOCK and Is_Block(L_current));

        if (Cell_Series_Len_At(L_current) == 0)  // not supported [1]
            fail ("SET-BLOCK! must not be empty for now.");

        const Element* tail;
        const Element* check = Cell_List_At(&tail, L_current);
        Context* check_binding = Derive_Binding(L_binding, L_current);

        // we've extracted the array at and tail, can reuse current now

        StackIndex stackindex_circled = 0;

        for (; check != tail; ++check) {  // push variables first [2]
            if (Is_Quoted(check))
                fail ("QUOTED? not currently permitted in SET-BLOCK!s");

            Heart heart = Cell_Heart(check);

            bool is_optional;
            if (
                (heart == REB_CHAIN or heart == REB_META_CHAIN)
                and Cell_Sequence_Len(check) == 2
                and Is_Blank(Copy_Sequence_At(CURRENT, check, 0))
            ){
                is_optional = true;  // leading colon means optional
                Derelativize_Sequence_At(
                    CURRENT,
                    check,
                    check_binding,
                    1
                );
                if (heart == REB_META_CHAIN)
                    Metafy(CURRENT);
                heart = Cell_Heart(CURRENT);
            }
            else {
                is_optional = false;  // no leading slash means required
                Derelativize(CURRENT, check, check_binding);
            }

            if (
                heart == REB_GROUP
                or heart == REB_THE_GROUP
                or heart == REB_META_GROUP
            ){
                if (Eval_Any_List_At_Throws(SPARE, CURRENT, SPECIFIED)) {
                    Drop_Data_Stack_To(BASELINE->stack_base);
                    goto return_thrown;
                }
                Decay_If_Unstable(SPARE);
                if (heart == REB_THE_GROUP)
                    Theify(stable_SPARE);  // transfer @ decoration to product
                else if (heart == REB_META_GROUP)
                    Metafy(stable_SPARE);  // transfer ^ decoration to product
                else if (heart == REB_GROUP and Is_Void(SPARE))
                    Init_Blank(SPARE);  // [(void)]: ... opts out of return

                heart = Cell_Heart(SPARE);
                Copy_Cell(PUSH(), stable_SPARE);
            }
            else
                Copy_Cell(PUSH(), CURRENT);

            if (is_optional)  // so next phase won't worry about leading slash
                Set_Cell_Flag(TOP, STACK_NOTE_OPTIONAL);

            if (
                // @xxx is indicator of circled result [3]
                //
                (heart == REB_SIGIL and Cell_Sigil(TOP) == SIGIL_THE)
                or heart == REB_THE_WORD
                or heart == REB_THE_TUPLE
            ){
                if (stackindex_circled != 0)
                    fail ("Can't circle more than one multi-return result");
                stackindex_circled = TOP_INDEX;
                continue;
            }
            if (
                // ^xxx is indicator of a ^META result [4]
                //
                (heart == REB_SIGIL and Cell_Sigil(check) == SIGIL_META)
                or heart == REB_META_WORD
                or heart == REB_META_TUPLE
            ){
                continue;
            }

            if (heart == REB_BLANK or heart == REB_WORD or heart == REB_TUPLE)
                continue;  // check this *after* special WORD! checks!

            fail ("SET-BLOCK! items are (@THE, ^META) WORD/TUPLE or BLANK");
        }

        if (stackindex_circled == 0)
            stackindex_circled = BASELINE->stack_base + 1;  // main [3]

        level_->u.eval.stackindex_circled = stackindex_circled;  // remember it

        Level* sub = Maybe_Rightward_Continuation_Needed(L);
        if (not sub)
            goto set_block_rightside_result_in_out;

        return CATCH_CONTINUE_SUBLEVEL(sub);

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
                    fail (Error_No_Catch_For_Throw(TOP_LEVEL));
                Drop_Level(TOP_LEVEL);
            }
            if (Is_Lazy(OUT))  // Lazy -> Lazy not allowed, Lazy -> Pack is ok
                fail ("Lazy Object Reified to Lazy Object: Not Allowed");
        }

        const Array* pack_array;  // needs GC guarding when OUT overwritten
        const Element* pack_meta_at;  // pack block items are ^META'd
        const Element* pack_meta_tail;

        if (Is_Barrier(OUT))  // !!! Hack, wnat ([/foo]: eval) to always work
            Init_Nihil(OUT);

        if (Is_Pack(OUT)) {  // antiform block
            pack_meta_at = Cell_List_At(&pack_meta_tail, OUT);

            pack_array = Cell_Array(OUT);
            Push_GC_Guard(pack_array);
        }
        else {
            Meta_Quotify(OUT);  // standardize to align with pack items

            pack_meta_at = cast(Element*, OUT);
            pack_meta_tail = cast(Element*, OUT) + 1;  // not a valid cell

            pack_array = nullptr;
        }

        StackIndex stackindex_var = BASELINE->stack_base + 1;  // [2]
        StackIndex stackindex_circled = level_->u.eval.stackindex_circled;

        for (
            ;
            stackindex_var != TOP_INDEX + 1;
            ++stackindex_var, ++pack_meta_at
        ){
            bool is_optional = Get_Cell_Flag(
                Data_Stack_At(stackindex_var),
                STACK_NOTE_OPTIONAL
            );

            Element* var = CURRENT;  // stable location, safe across SET of var
            Copy_Cell(var, cast(Element*, Data_Stack_At(stackindex_var)));

            assert(not Is_Quoted(var));
            Heart var_heart = Cell_Heart(var);

            if (pack_meta_at == pack_meta_tail) {
                if (not is_optional)
                    fail ("Not enough values for required multi-return");

                // match typical input of meta which will be Meta_Unquotify'd
                // (special handling in REB_META_WORD and REB_META_TUPLE
                // below will actually use plain null to distinguish)
                //
                Init_Meta_Of_Null(SPARE);
            }
            else
                Copy_Cell(SPARE, pack_meta_at);

            if (var_heart == REB_SIGIL and Cell_Sigil(var) == SIGIL_META)
                goto circled_check;  // leave as meta the way it came in

            if (
                var_heart == REB_META_WORD
                or var_heart == REB_META_TUPLE
            ){
                if (pack_meta_at == pack_meta_tail) {  // special detection
                    Set_Var_May_Fail(var, SPECIFIED, Lib(NULL));
                    goto circled_check;
                }
                Set_Var_May_Fail(var, SPECIFIED, stable_SPARE);  // is meta'd
                goto circled_check;
            }

            Meta_Unquotify_Undecayed(SPARE);

            if (
                var_heart == REB_SIGIL
                and Cell_Sigil(var) == SIGIL_THE  // [@ ...]:
            ){
                goto circled_check;
            }

            if (Is_Raised(SPARE))  // don't pass thru raised errors if not @
                fail (Cell_Error(SPARE));

            Decay_If_Unstable(SPARE);  // if pack in slot, resolve it

            if (var_heart == REB_BLANK)  // [_ ...]:
                goto circled_check;

            if (
                var_heart == REB_WORD or var_heart == REB_TUPLE
                or var_heart == REB_THE_WORD or var_heart == REB_THE_TUPLE
            ){
                DECLARE_VALUE (dummy);
                if (Set_Var_Core_Throws(
                    dummy,
                    GROUPS_OK,
                    var,
                    SPECIFIED,
                    stable_SPARE
                )){
                    fail (Error_No_Catch_For_Throw(L));
                }
            }
            else
                assert(false);

          circled_check :

            if (stackindex_circled == stackindex_var) {
                assert(
                    stackindex_circled == BASELINE->stack_base + 1
                    or (
                        var_heart == REB_SIGIL
                        and Cell_Sigil(var) == SIGIL_THE
                    )
                    or var_heart == REB_THE_WORD
                    or var_heart == REB_THE_TUPLE
                );
                Copy_Cell(OUT, SPARE);  // Note: might be void
            }
        }

        // We've just changed the values of variables, and these variables
        // might be coming up next.  Consider:
        //
        //     304 = [a]: test 1020
        //     a = 304
        //
        // The `a` was fetched and found to not be enfix, and in the process
        // its value was known.  But then we assigned that a with a new value
        // in the implementation of SET-BLOCK! here, so, it's incorrect.
        //
        L_next_gotten = nullptr;

        if (pack_array)
            Drop_GC_Guard(pack_array);

    } set_block_drop_stack_and_continue: {  //////////////////////////////////

        Drop_Data_Stack_To(BASELINE->stack_base);  // drop writeback variables
        goto lookahead; }


    //=//// META-BLOCK! ////////////////////////////////////////////////////=//
    //
    // Just produces a quoted version of the block it is given:
    //
    //    >> ^[a b c]
    //    == '[a b c]
    //
    // (It's hard to think of another meaning that would be sensible.)

      case REB_META_BLOCK:
        Inertly_Derelativize_Inheriting_Const(OUT, L_current, L->feed);
        HEART_BYTE(OUT) = REB_BLOCK;
        Quotify(OUT, 1);
        goto lookahead;


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
    //
    //=////////////////////////////////////////////////////////////////////=//

      case REB_THE_BLOCK:
      case REB_THE_WORD:
      case REB_THE_PATH:
      case REB_THE_TUPLE:
      case REB_THE_GROUP:
        Inertly_Derelativize_Inheriting_Const(OUT, L_current, L->feed);
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

      case REB_VAR_BLOCK:
      case REB_VAR_WORD:
      case REB_VAR_PATH:
      case REB_VAR_TUPLE:
      case REB_VAR_CHAIN:
      case REB_VAR_GROUP:
        Inertly_Derelativize_Inheriting_Const(OUT, L_current, L->feed);
        HEART_BYTE(OUT) = Plainify_Any_Var_Kind(STATE);
        goto lookahead;


      case REB_BLOCK:
        //
      case REB_BINARY:
        //
      case REB_TEXT:
      case REB_FILE:
      case REB_EMAIL:
      case REB_URL:
      case REB_TAG:
      case REB_ISSUE:
        //
      case REB_BITSET:
        //
      case REB_MAP:
        //
      case REB_VARARGS:
        //
      case REB_OBJECT:
      case REB_MODULE:
      case REB_ERROR:
      case REB_PORT:
        goto inert;


    //=///////////////////////////////////////////////////////////////////=//
    //
    // Treat all the other NOT Is_Bindable() types as inert
    //
    //=///////////////////////////////////////////////////////////////////=//

    inert:
      case REB_BLANK:  // once blanks evaluated to null, but that was panned
      case REB_INTEGER:
      case REB_DECIMAL:
      case REB_PERCENT:
      case REB_MONEY:
      case REB_PAIR:
      case REB_TIME:
      case REB_DATE:
        //
      case REB_PARAMETER:
        //
      case REB_TYPE_WORD:
      case REB_TYPE_BLOCK:
      case REB_TYPE_GROUP:
      case REB_TYPE_PATH:
      case REB_TYPE_TUPLE:
        //
      case REB_HANDLE:

        Inertly_Derelativize_Inheriting_Const(OUT, L_current, L->feed);
        goto lookahead;


    //=//// GARBAGE (pseudotypes or otherwise //////////////////////////////=//

      default:
        panic (L_current);
    }

  //=//// END MAIN SWITCH STATEMENT ///////////////////////////////////////=//

    // We're sitting at what "looks like the end" of an evaluation step.
    // But we still have to consider enfix.  e.g.
    //
    //    [pos val]: evaluate:step [1 + 2 * 3]
    //
    // We want that to give a position of [] and `val = 9`.  The evaluator
    // cannot just dispatch on REB_INTEGER in the switch() above, give you 1,
    // and consider its job done.  It has to notice that the word `+` looks up
    // to an ACTION! that was assigned with SET/ENFIX, and keep going.
    //
    // Next, there's a subtlety with FEED_FLAG_NO_LOOKAHEAD which explains why
    // processing of the 2 argument doesn't greedily continue to advance, but
    // waits for `1 + 2` to finish.  This is because the right hand argument
    // of math operations tend to be declared #tight.
    //
    // If that's not enough to consider :-) it can even be the case that
    // subsequent enfix gets "deferred".  Then, possibly later the evaluated
    // value gets re-fed back in, and we jump right to this post-switch point
    // to give it a "second chance" to take the enfix.  (See 'deferred'.)
    //
    // So this post-switch step is where all of it happens, and it's tricky!

  lookahead:

    if (Is_Barrier(OUT)) {
        //
        // With COMMA!, we skip the lookahead step, which means (then [...])
        // will have the same failure mode as (1 + 2, then [...]).  In order
        // to make this the same behavior anything else that evaluates to
        // a barrier (COMMA! antiform) we make this hinge on producing a
        // barrier--not on being a source level comma.  Note it's different
        // from what would happen with (nihil then [...]) which shows a nuance
        // between barriers and nihils.

      skip_lookahead:
        assert(Is_Barrier(OUT));  // only jump in for barriers
        goto finished;
    }

    // If something was run with the expectation it should take the next arg
    // from the output cell, and an evaluation cycle ran that wasn't an
    // ACTION! (or that was an arity-0 action), that's not what was meant.
    // But it can happen, e.g. `x: 10 | x ->-`, where ->- doesn't get an
    // opportunity to quote left because it has no argument...and instead
    // retriggers and lets x run.

    if (Get_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH))
        fail (Error_Literal_Left_Path_Raw());


  //=//// IF NOT A WORD!, IT DEFINITELY STARTS A NEW EXPRESSION ///////////=//

    // For long-pondered technical reasons, only WORD! is able to dispatch
    // enfix.  If it's necessary to dispatch an enfix function via path, then
    // a word is used to do it, like `>-` in `x: >- lib/method [...] [...]`.

    if (Is_Feed_At_End(L->feed)) {
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
        goto finished;  // hitting end is common, avoid do_next's switch()
    }

    switch (VAL_TYPE_UNCHECKED(L_next)) {
      case REB_WORD:
        if (not L_next_gotten)
            L_next_gotten = Lookup_Word(L_next, FEED_BINDING(L->feed));
        else
            assert(L_next_gotten == Lookup_Word(L_next, FEED_BINDING(L->feed)));
        break;  // need to check for lookahead

      case REB_FRAME:
        L_next_gotten = L_next;
        break;

      default:
        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
        goto finished;
    }

  //=//// FETCH WORD! TO PERFORM SPECIAL HANDLING FOR ENFIX/INVISIBLES ////=//

    // First things first, we fetch the WORD! (if not previously fetched) so
    // we can see if it looks up to any kind of ACTION! at all.


  //=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-ENFIX ///////////=//

    // These cases represent finding the start of a new expression.
    //
    // Fall back on word-like "dispatch" even if ->gotten is null (unset or
    // unbound word).  It'll be an error, but that code path raises it for us.

    if (
        not L_next_gotten
        or (
            not (Is_Word(L_next) and Is_Action(unwrap L_next_gotten))
            and not Is_Frame(L_next)
            and not Is_Sigil(L_next)
        )
        or Not_Enfixed(unwrap L_next_gotten)
    ){
      lookback_quote_too_late: // run as if starting new expression

        Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);
        Clear_Eval_Executor_Flag(L, INERT_OPTIMIZATION);

        // Since it's a new expression, EVALUATE doesn't want to run it
        // even if invisible, as it's not completely invisible (enfixed)
        //
        goto finished;
    }

  //=//// IS WORD ENFIXEDLY TIED TO A FUNCTION (MAY BE "INVISIBLE") ///////=//

  blockscope {
    Action* enfixed = VAL_ACTION(unwrap L_next_gotten);
    Array* paramlist = ACT_PARAMLIST(enfixed);

    if (Get_Subclass_Flag(VARLIST, paramlist, PARAMLIST_QUOTES_FIRST)) {
        //
        // Left-quoting by enfix needs to be done in the lookahead before an
        // evaluation, not this one that's after.  This happens in cases like:
        //
        //     /left-the: enfix func [@value] [value]
        //     the <something> left-the
        //
        // But due to the existence of <end>-able parameters, the left quoting
        // function might be okay with seeing nothing on the left.  Start a
        // new expression and let it error if that's not ok.
        //
        assert(Not_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH));
        if (Get_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH))
            fail (Error_Literal_Left_Path_Raw());

        const Param* first = First_Unspecialized_Param(nullptr, enfixed);
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
        and not (Get_Action_Flag(enfixed, DEFERS_LOOKBACK)
                                       // ^-- `1 + if null [2] else [3]` => 4
        )
    ){
        if (Get_Feed_Flag(L->feed, NO_LOOKAHEAD)) {
            // Don't do enfix lookahead if asked *not* to look.

            Clear_Feed_Flag(L->feed, NO_LOOKAHEAD);

            assert(Not_Feed_Flag(L->feed, DEFERRING_ENFIX));
            Set_Feed_Flag(L->feed, DEFERRING_ENFIX);

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
            Get_Action_Flag(enfixed, POSTPONES_ENTIRELY)
            or (
                Get_Action_Flag(enfixed, DEFERS_LOOKBACK)
                and Not_Feed_Flag(L->feed, DEFERRING_ENFIX)
            )
        )
    ){
        if (
            Is_Action_Level(L->prior)
            and Get_Executor_Flag(ACTION, L->prior, ERROR_ON_DEFERRED_ENFIX)
        ){
            // Operations that inline functions by proxy (such as MATCH and
            // ENSURE) cannot directly interoperate with THEN or ELSE...they
            // are building a frame with PG_Dummy_Action as the function, so
            // running a deferred operation in the same step is not an option.
            // The expression to the left must be in a GROUP!.
            //
            fail (Error_Ambiguous_Infix_Raw());
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

        Set_Feed_Flag(L->feed, DEFERRING_ENFIX);

        // Leave enfix operator pending in the feed.  It's up to the parent
        // level to decide whether to ST_STEPPER_LOOKING_AHEAD to jump
        // back in and finish fulfilling this arg or not.  If it does resume
        // and we get to this check again, L->prior->deferred can't be null,
        // otherwise it would be an infinite loop.
        //
        goto finished;
    }

    Clear_Feed_Flag(L->feed, DEFERRING_ENFIX);

    // An evaluative lookback argument we don't want to defer, e.g. a normal
    // argument or a deferable one which is not being requested in the context
    // of parameter fulfillment.  We want to reuse the OUT value and get it
    // into the new function's frame.

    Level* sub = Make_Action_Sublevel(L);
    Push_Level(OUT, sub);
    Push_Action(sub, enfixed, Cell_Frame_Coupling(unwrap L_next_gotten));
    Begin_Enfix_Action(
        sub,
        Is_Frame(L_next) ? VAL_FRAME_LABEL(L_next) : Cell_Word_Symbol(L_next)
    );

    Fetch_Next_In_Feed(L->feed);

    goto process_action; }

  finished:

    // Want to keep this flag between an operation and an ensuing enfix in
    // the same level, so can't clear in Drop_Action(), e.g. due to:
    //
    //     /left-the: enfix :the
    //     o: make object! [/f: does [1]]
    //     o.f left-the  ; want error suggesting >- here, need flag for that
    //
    Clear_Eval_Executor_Flag(L, DIDNT_LEFT_QUOTE_PATH);

  #if !defined(NDEBUG)
    Evaluator_Exit_Checks_Debug(L);
  #endif

    assert(not Is_Fresh(OUT));  // should have been assigned
    return OUT;

  return_thrown:

  #if !defined(NDEBUG)
    Evaluator_Exit_Checks_Debug(L);
  #endif

    return BOUNCE_THROWN;
}
