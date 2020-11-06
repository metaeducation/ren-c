//
//  File: %c-eval.c
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
// This file contains Eval_Maybe_Stale_Throws(), which is the central
// evaluator implementation.  Most callers should use higher level wrappers,
// because the long name conveys any direct caller must handle the following:
//
// * _Maybe_Stale_ => The evaluation targets an output cell which must be
//   preloaded or set to END.  If there is no result (e.g. due to being just
//   comments) then whatever was in that cell will still be there -but- will
//   carry OUT_MARKED_STALE.  This is just an alias for NODE_FLAG_MARKED, and
//   it must be cleared off before passing pointers to the cell to a routine
//   which may interpret that flag differently.
//
// * _Throws => The return result is a boolean which all callers *must* heed.
//   There is no "thrown value" data type or cell flag, so the only indication
//   that a throw happened comes from this flag.  See %sys-throw.h
//
// Eval_Throws() is a small stub which takes care of the first concern,
// though some low-level clients actually want the stale flag.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * See %sys-eval.h for wrappers that make it easier to set up frames and
//   use the evaluator for a single step.
//
// * See %sys-do.h for wrappers that make it easier to run multiple evaluator
//   steps in a frame and return the final result, giving VOID! by default.
//
// * Eval_Maybe_Stale_Throws() is LONG.  That is largely a purposeful choice.
//   Breaking it into functions would add overhead (in the debug build if not
//   also release builds) and prevent interesting tricks and optimizations.
//   It is separated into sections, and the invariants in each section are
//   made clear with comments and asserts.
//
// * The evaluator only moves forward, and operates on a strict window of
//   visibility of two elements at a time (current position and "lookback").
//   See `Reb_Feed` for the code that provides this abstraction over Rebol
//   arrays as well as C va_list.
//

#include "sys-core.h"


#if defined(DEBUG_COUNT_TICKS)  // <-- THIS IS VERY USEFUL, SEE %sys-eval.h!
    //
    // This counter is incremented each time a function dispatcher is run
    // or a parse rule is executed.  See UPDATE_TICK_COUNT().
    //
    REBTCK TG_Tick;

    //      *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
    REBTCK TG_Break_At_Tick =      0;
    //      *** DON'T COMMIT THIS --^ KEEP IT AT ZERO! ***

#endif  // ^-- SERIOUSLY: READ ABOUT C-DEBUG-BREAK AND PLACES TICKS ARE STORED


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
#define kind_current KIND3Q_BYTE_UNCHECKED(v)


// In the early development of FRAME!, the REBFRM* for evaluating across a
// block was reused for each ACTION! call.  Since no more than one action was
// running at a time, this seemed to work.  However, that didn't allow for
// a separate "reified" entry for users to point at.  While giving each
// action its own REBFRM* has performance downsides, it makes the objects
// correspond to what they are...and may be better for cohering the "executor"
// pattern by making it possible to use a constant executor per frame.
//
#define DECLARE_ACTION_SUBFRAME(f,parent) \
    DECLARE_FRAME (f, (parent)->feed, \
        EVAL_MASK_DEFAULT | /*EVAL_FLAG_KEEP_STALE_BIT |*/ ((parent)->flags.bits \
            & (EVAL_FLAG_FULFILLING_ARG | EVAL_FLAG_RUNNING_ENFIX \
                | EVAL_FLAG_DIDNT_LEFT_QUOTE_PATH)))


#ifdef DEBUG_EXPIRED_LOOKBACK
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (f->feed->stress != nullptr)
#else
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (v == &f->feed->lookback)
#endif


inline static void Expire_Out_Cell_Unless_Invisible(REBFRM *f) {
    SET_CELL_FLAG(f->out, OUT_MARKED_STALE);
}


// SET-WORD!, SET-PATH!, SET-GROUP!, and SET-BLOCK! all want to do roughly
// the same thing as the first step of their evaluation.  They evaluate the
// right hand side into f->out.
//
// -but- because you can be asked to evaluate something like `x: y: z: ...`,
// there could be any number of SET-XXX! before the value to assign is found.
//
// This inline function attempts to keep that stack by means of the local
// variable `v`, if it points to a stable location.  If so, it simply reuses
// the frame it already has.
//
// What makes this slightly complicated is that the current value may be in
// a place that doing a Fetch_Next_In_Frame() might corrupt it.  This could
// be accounted for by pushing the value to some other stack--e.g. the data
// stack.  But for the moment this (uncommon?) case uses a new frame.
//
inline static bool Rightward_Evaluate_Nonvoid_Into_Out_Throws(
    REBFRM *f,
    const RELVAL *v
){
    if (GET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT))  {  // e.g. `10 -> x:`
        CLEAR_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);
        CLEAR_CELL_FLAG(f->out, UNEVALUATED);  // this helper counts as eval
        return false;
    }

    if (IS_END(f_next))  // `do [x:]`, `do [o/x:]`, etc. are illegal
        fail (Error_Need_Non_End_Core(v, f_specifier));

    // Using a SET-XXX! means you always have at least two elements; it's like
    // an arity-1 function.  `1 + x: whatever ...`.  This overrides the no
    // lookahead behavior flag right up front.
    //
    CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

    REBFLGS flags = EVAL_MASK_DEFAULT
            | (f->flags.bits & EVAL_FLAG_FULFILLING_ARG);  // if f was, we are

    SET_END(f->out);  // `1 x: comment "hi"` shouldn't set x to 1!

    if (CURRENT_CHANGES_IF_FETCH_NEXT) {  // must use new frame
        if (Eval_Step_In_Subframe_Throws(f->out, f, flags))
            return true;
    }
    else {  // !!! Reusing the frame, would inert optimization be worth it?
        do {
            if (Eval_Maybe_Stale_Throws(f))  // reuse `f`
                return true;

            // Keep evaluating as long as evaluations vanish, e.g.
            // `x: comment "hi" 2` shouldn't fail.
            //
            // !!! Note this behavior is already handled by FULFILLING_ARG
            // but but we are reusing a frame that may-or-may-not be
            // fulfilling.  There may be a better way of centralizing this.
            //
        } while (IS_END(f->out) and NOT_END(f_next));
    }

    if (IS_END(f->out))  // e.g. `do [x: ()]` or `(x: comment "hi")`.
        fail (Error_Need_Non_End_Core(v, f_specifier));

    CLEAR_CELL_FLAG(f->out, UNEVALUATED);  // this helper counts as eval
    return false;
}


//
//  Eval_Maybe_Stale_Throws: C
//
// See notes at top of file for general remarks on this central function's
// name, and that wrappers should nearly always be used to call it.
//
// More detailed assertions of the preconditions, postconditions, and state
// at each evaluation step are contained in %d-eval.c, to keep this file
// more manageable in length.
//
bool Eval_Maybe_Stale_Throws(REBFRM * const f)
{
  #ifdef DEBUG_ENSURE_FRAME_EVALUATES
    f->was_eval_called = true;  // see definition for why this flag exists
  #endif

  #if defined(DEBUG_COUNT_TICKS)
    REBTCK tick = f->tick = TG_Tick;  // snapshot tick for C watchlist viewing
  #endif

  #if !defined(NDEBUG)
    REBFLGS initial_flags = f->flags.bits & ~(
        EVAL_FLAG_FULFILL_ONLY  // can be requested or <blank> can trigger
        | EVAL_FLAG_RUNNING_ENFIX  // can be requested with REEVALUATE_CELL
        | EVAL_FLAG_TOOK_HOLD  // can be set by va_list reification
    );  // should be unchanged on exit
  #endif

    assert(DSP >= f->dsp_orig);  // REDUCE accrues, APPLY adds refinements
    assert(not IS_TRASH_DEBUG(f->out));  // all invisible will preserve output
    assert(f->out != f_spare);  // overwritten by temporary calculations

    // A barrier shouldn't cause an error in evaluation if code would be
    // willing to accept an <end>.  So we allow argument gathering to try to
    // run, but it may error if that's not acceptable.
    //
    if (GET_FEED_FLAG(f->feed, BARRIER_HIT)) {
        if (GET_EVAL_FLAG(f, FULFILLING_ARG)) {
            f->out->header.bits |= CELL_FLAG_OUT_MARKED_STALE;
            return false;
        }
        CLEAR_FEED_FLAG(f->feed, BARRIER_HIT);  // not an argument, clear flag
    }

    const RELVAL *v;  // shorthand for the value we are switch()-ing on
    TRASH_POINTER_IF_DEBUG(v);

    const REBVAL *gotten;
    TRASH_POINTER_IF_DEBUG(gotten);

    // Given how the evaluator is written, it's inevitable that there will
    // have to be a test for points to `goto` before running normal eval.
    // This cost is paid on every entry to Eval_Core().
    //
    switch (STATE_BYTE(f)) {
      case ST_EVALUATOR_INITIAL_ENTRY:
        break;

      case ST_EVALUATOR_LOOKING_AHEAD:
        goto lookahead;

      case ST_EVALUATOR_REEVALUATING: {
        //
        // The re-evaluate functionality may not want to heed the enfix state
        // in the action itself.  See REBNATIVE(shove)'s /ENFIX for instance.
        // So we go by the state of EVAL_FLAG_RUNNING_ENFIX on entry.
        //
        if (GET_EVAL_FLAG(f, RUNNING_ENFIX)) {
            CLEAR_EVAL_FLAG(f, RUNNING_ENFIX);  // for assertion

            DECLARE_ACTION_SUBFRAME (subframe, f);
            Push_Frame(f->out, subframe);
            Push_Action(
                subframe,
                VAL_ACTION(f->u.reval.value),
                VAL_BINDING(f->u.reval.value)
            );
            Begin_Enfix_Action(subframe, VAL_ACTION_LABEL(f->u.reval.value));
                // ^-- invisibles cache NO_LOOKAHEAD

            goto process_action;
        }

        if (NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT))
            SET_CELL_FLAG(f->out, OUT_MARKED_STALE);

        v = f->u.reval.value;
        gotten = nullptr;
        goto evaluate; }

      default:
        assert(false);
    }

  #if !defined(NDEBUG)
    Eval_Core_Expression_Checks_Debug(f);
    assert(NOT_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH));
    if (NOT_EVAL_FLAG(f, FULFILLING_ARG))
        assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD));
    assert(NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX));
  #endif

  //=//// START NEW EXPRESSION ////////////////////////////////////////////=//

    assert(Eval_Count >= 0);
    if (--Eval_Count == 0) {
        //
        // Note that Do_Signals_Throws() may do a recycle step of the GC, or
        // it may spawn an entire interactive debugging session via
        // breakpoint before it returns.  It may also FAIL and longjmp out.
        //
        if (Do_Signals_Throws(f->out))
            goto return_thrown;
    }

    assert(NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT));
    SET_CELL_FLAG(f->out, OUT_MARKED_STALE);  // out won't act as enfix input

    UPDATE_EXPRESSION_START(f);  // !!! See FRM_INDEX() for caveats

    // If asked to evaluate `[]` then we have now done all the work the
    // evaluator needs to do--including marking the output stale.
    //
    // See DEBUG_ENSURE_FRAME_EVALUATES for why an empty array does not
    // bypass calling into the evaluator.
    //
    if (KIND3Q_BYTE(f_next) == REB_0_END)
        goto finished;

    gotten = f_next_gotten;
    v = Lookback_While_Fetching_Next(f);
    // ^-- can't just `v = f_next`, fetch may overwrite--request lookback!

  evaluate: ;  // meaningful semicolon--subsequent macro may declare things

    // ^-- doesn't advance expression index: `reeval x` starts with `reeval`

  //=//// LOOKAHEAD FOR ENFIXED FUNCTIONS THAT QUOTE THEIR LEFT ARG ///////=//

    // Ren-C has an additional lookahead step *before* an evaluation in order
    // to take care of this scenario.  To do this, it pre-emptively feeds the
    // frame one unit that f->value is the *next* value, and a local variable
    // called "current" holds the current head of the expression that the
    // main switch would process.

    UPDATE_TICK_DEBUG(v);

    // v-- This is the TG_Break_At_Tick or C-DEBUG-BREAK landing spot --v

    if (KIND3Q_BYTE(f_next) != REB_WORD)  // right's kind - END would be REB_0
        goto give_up_backward_quote_priority;

    assert(not f_next_gotten);  // Fetch_Next_In_Frame() cleared it
    f_next_gotten = Try_Lookup_Word(f_next, f_specifier);

    if (not f_next_gotten or not IS_ACTION(f_next_gotten))
        goto give_up_backward_quote_priority;  // note only ACTION! is ENFIXED

    if (GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), IS_BARRIER)) {
        //
        // In a situation like `foo |`, we want FOO to be able to run...it
        // may take 0 args or it may be able to tolerate END.  But we should
        // not be required to run the barrier in the same evaluative step
        // as the left hand side.  (It can be enfix, or it can not be.)
        //
        SET_FEED_FLAG(f->feed, BARRIER_HIT);
        goto give_up_backward_quote_priority;
    }

    if (NOT_ACTION_FLAG(VAL_ACTION(f_next_gotten), ENFIXED))
        goto give_up_backward_quote_priority;

    if (NOT_ACTION_FLAG(VAL_ACTION(f_next_gotten), QUOTES_FIRST))
        goto give_up_backward_quote_priority;

    // If the action soft quotes its left, that means it's aware that its
    // "quoted" argument may be evaluated sometimes.  If there's evaluative
    // material on the left, treat it like it's in a group.
    //
    if (
        GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), POSTPONES_ENTIRELY)
        or (
            GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)
            and not ANY_SET_KIND(kind_current)  // not SET-WORD!, SET-PATH!...
        )
    ){
        // !!! cache this test?
        //
        REBVAL *first = First_Unspecialized_Param(VAL_ACTION(f_next_gotten));
        if (
            VAL_PARAM_CLASS(first) == REB_P_SOFT_QUOTE
            or VAL_PARAM_CLASS(first) == REB_P_MODAL
        ){
            goto give_up_backward_quote_priority;  // yield as an exemption
        }
    }

    // Let the <skip> flag allow the right hand side to gracefully decline
    // interest in the left hand side due to type.  This is how DEFAULT works,
    // such that `case [condition [...] default [...]]` does not interfere
    // with the BLOCK! on the left, but `x: default [...]` gets the SET-WORD!
    //
    if (GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), SKIPPABLE_FIRST)) {
        REBVAL *first = First_Unspecialized_Param(VAL_ACTION(f_next_gotten));
        if (not TYPE_CHECK(first, kind_current))  // left's kind
            goto give_up_backward_quote_priority;
    }

    // Lookback args are fetched from f->out, then copied into an arg
    // slot.  Put the backwards quoted value into f->out.
    //
    Derelativize(f->out, v, f_specifier);  // for NEXT_ARG_FROM_OUT
    SET_CELL_FLAG(f->out, UNEVALUATED);  // so lookback knows it was quoted

    // We skip over the word that invoked the action (e.g. ->-, OF, =>).
    // v will then hold a pointer to that word (possibly now resident in the
    // frame spare).  (f->out holds what was the left)
    //
    gotten = f_next_gotten;
    v = Lookback_While_Fetching_Next(f);

    if (
        IS_END(f_next)  // v-- out is what used to be on left
        and (
            KIND3Q_BYTE(f->out) == REB_WORD
            or KIND3Q_BYTE(f->out) == REB_PATH
        )
    ){
        // We make a special exemption for left-stealing arguments, when
        // they have nothing to their right.  They lose their priority
        // and we run the left hand side with them as a priority instead.
        // This lets us do e.g. `(lit =>)` or `help of`
        //
        // Swap it around so that what we had put in the f->out goes back
        // to being in the lookback cell and can be used as current.  Then put
        // what was current into f->out so it can be consumed as the first
        // parameter of whatever that was.

        Move_Value(&f->feed->lookback, f->out);
        Derelativize(f->out, v, f_specifier);
        SET_CELL_FLAG(f->out, UNEVALUATED);

        // leave *next at END
        v = &f->feed->lookback;
        gotten = nullptr;

        SET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH);  // for better error message
        SET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);  // literal right op is arg

        goto give_up_backward_quote_priority;  // run PATH!/WORD! normal
    }

    // Wasn't the at-end exception, so run normal enfix with right winning.
    //
  blockscope {
    DECLARE_ACTION_SUBFRAME (subframe, f);
    Push_Frame(f->out, subframe);
    Push_Action(subframe, VAL_ACTION(gotten), VAL_BINDING(gotten));
    Begin_Enfix_Action(subframe, VAL_WORD_SPELLING(v));

    goto process_action; }

  give_up_backward_quote_priority:

  //=//// BEGIN MAIN SWITCH STATEMENT /////////////////////////////////////=//

    // This switch is done with a case for all REB_XXX values, in order to
    // facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
    //
    // Subverting the jump table optimization with specialized branches for
    // fast tests like ANY_INERT() and IS_NULLED_OR_VOID_OR_END() has shown
    // to reduce performance in practice.  The compiler does the right thing.

    switch (KIND3Q_BYTE(v)) {  // checked version (once, else kind_current)

      case REB_0_END:
        goto finished;


    //=//// NULL //////////////////////////////////////////////////////////=//
    //
    // Since nulled cells can't be in BLOCK!s, the evaluator shouldn't usually
    // see them.  It is technically possible to see one using REEVAL, such as
    // with `reeval first []`.  However, the more common way to encounter this
    // situation would be in the API:
    //
    //     REBVAL *v = nullptr;
    //     bool is_null = rebDid("null?", v);  // oops, should be rebQ(v)
    //
    // Note: It seems tempting to let NULL evaluate to NULL as a convenience
    // for such cases.  But this breaks the system in subtle ways--like
    // making it impossible to "reify" the instruction stream as a BLOCK!
    // for the debugger.  Mechanically speaking, this is best left an error.

      case REB_NULL:
        fail (Error_Evaluate_Null_Raw());


    //=//// COMMA! ////////////////////////////////////////////////////////=//
    //
    // A comma is a lightweight looking expression barrier.

       case REB_COMMA:
        if (GET_EVAL_FLAG(f, FULFILLING_ARG)) {
            CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
            SET_FEED_FLAG(f->feed, BARRIER_HIT);
            goto finished;
        }
        break;


    //=//// ACTION! ///////////////////////////////////////////////////////=//
    //
    // If an action makes it to the SWITCH statement, that means it is either
    // literally an action value in the array (`do compose [1 (:+) 2]`) or is
    // being retriggered via REEVAL.
    //
    // Most action evaluations are triggered from a WORD! or PATH! case.

      case REB_ACTION: {
        DECLARE_ACTION_SUBFRAME (subframe, f);
        Push_Frame(f->out, subframe);
        Push_Action(subframe, VAL_ACTION(v), VAL_BINDING(v));
        Begin_Prefix_Action(subframe, VAL_ACTION_LABEL(v));

        // We'd like `10 -> = 5 + 5` to work, and to do so it reevaluates in
        // a new frame, but has to run the `=` as "getting its next arg from
        // the output slot, but not being run in an enfix mode".
        //
        if (NOT_FEED_FLAG(subframe->feed, NEXT_ARG_FROM_OUT))
            Expire_Out_Cell_Unless_Invisible(subframe);

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
        FS_TOP->dsp_orig = f->dsp_orig;  // !!! How did this work in stackless

        // Gather args and execute function (the arg gathering makes nested
        // eval calls that lookahead, but no lookahead after the action runs)
        //
        bool threw = Process_Action_Maybe_Stale_Throws(FS_TOP);

        assert(NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT));  // must consume

        if (threw) {
            Abort_Frame(FS_TOP);
            goto return_thrown;
        }

        Drop_Frame(FS_TOP);

        // The Action_Executor does not get involved in Lookahead; so you
        // only get lookahead behavior when an action has been spawned from
        // a parent frame (such as one evaluating a block, or evaluating an
        // action's arguments).  Trying to dispatch lookahead from the
        // Action_Executor causes pain with `null then [x] => [1] else [2]`
        // cases (for instance).
        //
        // However, the evaluation of an invisible can leave a stale value
        // which indicates a need to invoke another evaluation.  Consider
        // `do [comment "hi" 10]`.
        //
        if (
            GET_EVAL_FLAG(f, FULFILLING_ARG)
            and GET_CELL_FLAG(f->out, OUT_MARKED_STALE)
            and NOT_END(f_next)
        ){
            gotten = f_next_gotten;
            v = Lookback_While_Fetching_Next(f);
            goto evaluate;
        }
        break; }


    //=//// WORD! /////////////////////////////////////////////////////////=//
    //
    // A plain word tries to fetch its value through its binding.  It fails
    // if the word is unbound (or if the binding is to a variable which is
    // set, but VOID!).  Should the word look up to an action, then that
    // action will be invoked.
    //
    // NOTE: The usual dispatch of enfix functions is *not* via a REB_WORD in
    // this switch, it's by some code at the `lookahead:` label.  You only see
    // enfix here when there was nothing to the left, so cases like `(+ 1 2)`
    // or in "stale" left hand situations like `10 comment "hi" + 20`.

      process_word:
      case REB_WORD:
        if (not gotten)
            gotten = Lookup_Word_May_Fail(v, f_specifier);

        if (IS_ACTION(gotten)) {  // before IS_VOID() is common case
            REBACT *act = VAL_ACTION(gotten);

            if (GET_ACTION_FLAG(act, ENFIXED)) {
                if (
                    GET_ACTION_FLAG(act, POSTPONES_ENTIRELY)
                    or GET_ACTION_FLAG(act, DEFERS_LOOKBACK)
                ){
                    if (GET_EVAL_FLAG(f, FULFILLING_ARG)) {
                        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
                        SET_FEED_FLAG(f->feed, DEFERRING_ENFIX);
                        SET_END(f->out);
                        goto finished;
                    }
                }
            }

            DECLARE_ACTION_SUBFRAME (subframe, f);
            Push_Frame(f->out, subframe);
            Push_Action(subframe, act, VAL_BINDING(gotten));
            Begin_Action_Core(
                subframe,
                VAL_WORD_SPELLING(v),  // use word as label
                GET_ACTION_FLAG(act, ENFIXED)
            );
            goto process_action;
        }

        if (IS_VOID(gotten))  // need GET/ANY if it's void ("undefined")
            fail (Error_Need_Non_Void_Core(v, f_specifier, gotten));

        Move_Value(f->out, gotten);  // no copy CELL_FLAG_UNEVALUATED
        break;


    //=//// SET-WORD! /////////////////////////////////////////////////////=//
    //
    // Right side is evaluated into `out`, and then copied to the variable.
    //
    // Null and void assigns are allowed: https://forum.rebol.info/t/895/4

      process_set_word:
      case REB_SET_WORD: {
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, v))  // see notes
            goto return_thrown;

      set_word_with_out:

        Move_Value(Sink_Word_May_Fail(v, f_specifier), f->out);
        break; }


    //=//// GET-WORD! /////////////////////////////////////////////////////=//
    //
    // A GET-WORD! does no dispatch on functions.  It will fetch other values
    // as normal, but will error on VOID! and direct you to GET/ANY.
    //
    // This handling of voids matches Rebol2 behavior, choosing to break with
    // R3-Alpha and Red which will give back "voided" values ("UNSET!").
    // The choice was made to make typos less likely to bite those whose
    // intent with GET-WORD! was merely to use ACTION!s inertly:
    //
    // https://forum.rebol.info/t/1301

      process_get_word:
      case REB_GET_WORD:
        if (not gotten)
            gotten = Lookup_Word_May_Fail(v, f_specifier);

        if (IS_VOID(gotten))
            fail (Error_Need_Non_Void_Core(v, f_specifier, gotten));

        Move_Value(f->out, gotten);

        if (IS_ACTION(gotten))  // cache the word's label in the cell
            INIT_ACTION_LABEL(f->out, VAL_WORD_SPELLING(v));
        break;


    //=//// GROUP! ////////////////////////////////////////////////////////=//
    //
    // A GROUP! whose contents wind up vaporizing wants to be invisible:
    //
    //     >> 1 + 2 ()
    //     == 3
    //
    //     >> 1 + 2 (comment "hi")
    //     == 3
    //
    // But there's a limit with group invisibility and enfix.  A single step
    // of the evaluator only has one lookahead, because it doesn't know if it
    // wants to evaluate the next thing or not:
    //
    //     >> evaluate [1 (2) + 3]
    //     == [(2) + 3]  ; takes one step...so next step will add 2 and 3
    //
    //     >> evaluate [1 (comment "hi") + 3]
    //     == [(comment "hi") + 3]  ; next step errors: + has no left argument
    //
    // It is supposed to be possible for DO to be implemented as a series of
    // successive single EVALUATE steps, giving no input beyond the block.  So
    // that means even though the `f->out` may technically still hold bits of
    // the last evaluation such that `do [1 (comment "hi") + 3]` *could* draw
    // from them to give a left hand argument, it should not do so...and it's
    // why those bits are marked "stale".
    //
    // The right of the operator is different story.  Turning up no result,
    // the group can just invoke a reevaluate without breaking any rules:
    //
    //     >> evaluate [1 + (2) 3]
    //     == [3]
    //
    //     >> evaluate [1 + (comment "hi") 3]
    //     == []
    //
    // This subtlety means running a GROUP! must be able to notice when no
    // result was produced (an output of END) and then re-trigger a step in
    // the parent frame, e.g. to pick up the 3 above.

      case REB_GROUP:
      eval_group: {
        f_next_gotten = nullptr;  // arbitrary code changes fetched variables

        // The IS_VOID() case here is specifically for REEVAL with invisibles,
        // because it's desirable for `void? reeval :comment "hi" 1` to be
        // 1 and not #[false].  The problem is that REEVAL is not invisible,
        // and hence it wants to make sure something is written to the output
        // so that standard invisibility doesn't kick in...hence it preloads
        // with a non-stale void.
        //
        assert(
            IS_END(f->out)
            or GET_CELL_FLAG(f->out, OUT_MARKED_STALE)
            or IS_VOID(f->out)
        );

        DECLARE_FEED_AT_CORE (subfeed, v, f_specifier);

        // "Maybe_Stale" variant leaves f->out as-is if no result generated
        // However, it sets OUT_MARKED_STALE in that case (note we may be
        // leaving an END in f->out by doing this.)
        //
        if (Do_Feed_To_End_Maybe_Stale_Throws(f->out, subfeed))
            goto return_thrown;

        // We want `3 = (1 + 2 ()) 4` to not treat the 1 + 2 as "stale", thus
        // skipping it and trying to compare `3 = 4`.  But `3 = () 1 + 2`
        // should consider the empty group stale.
        //
        if (IS_END(f->out)) {
            if (IS_END(f_next))
                goto finished;  // nothing after to try evaluating

            gotten = f_next_gotten;
            v = Lookback_While_Fetching_Next(f);
            goto evaluate;
        }

        CLEAR_CELL_FLAG(f->out, UNEVALUATED);  // `(1)` considered evaluative
        break; }


    //=//// PATH! and TUPLE! //////////////////////////////////////////////=//
    //
    // PATH! and GET-PATH! have similar mechanisms, with the difference being
    // that if a PATH! looks up to an action it will execute it.
    //
    // Paths looking up to VOID! are handled consistently with WORD! and
    // GET-WORD!, and will error...directing you use GET/ANY if fetching
    // voids is what you actually intended.
    //
    // PATH!s starting with inert values do not evaluate.  `/foo/bar` has a
    // blank at its head, and it evaluates to itself.
    //
    // !!! The dispatch of TUPLE! is a work in progress, with concepts about
    // being less willing to execute functions under some notations.

      case REB_PATH:
      case REB_TUPLE: {
        if (HEART_BYTE(v) == REB_WORD)
            goto process_word;  // special `/` or `.` case with hidden word

        const RELVAL *head = VAL_SEQUENCE_AT(f_spare, v, 0);
        if (ANY_INERT(head)) {
            Derelativize(f->out, v, f_specifier);
            break;
        }

        // !!! This is a special exemption added so that BLANK!-headed tuples
        // at the head of a PATH! carry over the inert evaluative behavior.
        // (The concept of evaluator treatment of PATH!s and TUPLE!s is to
        // not heed them structurally, but merely to see them as a sequence
        // of ordered dots and slashes...it will have to be seen how this
        // ultimately plays out.)
        //
        if (IS_TUPLE(head)) {
            //
            // VAL_SEQUENCE_AT() allows the same use of the `store` as the
            // sequence, which may be the case if it wrote spare above.
            //
            if (IS_BLANK(VAL_SEQUENCE_AT(f_spare, head, 0))) {
                Derelativize(f->out, v, f_specifier);
                break;
            }
        }

        REBVAL *where = GET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT)
            ? f_spare
            : f->out;

        if (Eval_Path_Throws_Core(
            where,
            v,  // !!! may not be array-based
            f_specifier,
            nullptr, // `setval`: null means don't treat as SET-PATH!
            EVAL_MASK_DEFAULT | EVAL_FLAG_PUSH_PATH_REFINES
        )){
            if (where != f->out)
                Move_Value(f->out, where);
            goto return_thrown;
        }

        if (IS_ACTION(where)) {  // try this branch before fail on void+null
            REBACT *act = VAL_ACTION(where);

            // PATH! dispatch is costly and can error in more ways than WORD!:
            //
            //     e: trap [do make block! ":a"] e/id = 'not-bound
            //                                   ^-- not ready @ lookahead
            //
            // Plus with GROUP!s in a path, their evaluations can't be undone.
            //
            if (GET_ACTION_FLAG(act, ENFIXED))
                fail ("Use `<-` to shove left enfix operands into PATH!s");

            DECLARE_ACTION_SUBFRAME (subframe, f);
            Push_Frame(f->out, subframe);
            Push_Action(subframe, VAL_ACTION(where), VAL_BINDING(where));
            Begin_Prefix_Action(subframe, VAL_ACTION_LABEL(where));

            if (where == subframe->out)
                Expire_Out_Cell_Unless_Invisible(subframe);

            goto process_action;
        }

        if (IS_VOID(where))  // need `:x/y` if it's void (unset)
            fail (Error_Need_Non_Void_Core(v, f_specifier, where));

        if (where != f->out)
            Move_Value(f->out, where);  // won't move CELL_FLAG_UNEVALUATED
        else
            CLEAR_CELL_FLAG(f->out, UNEVALUATED);
        break; }


    //=//// SET-PATH! /////////////////////////////////////////////////////=//
    //
    // See notes on SET-WORD!  SET-PATH!s are handled in a similar way.
    //
    // !!! The evaluation ordering is dictated by the fact that there isn't a
    // separate "evaluate path to target location" and "set target' step.
    // This is because some targets of assignments (e.g. gob/size/x:) do not
    // correspond to a cell that can be returned; the path operation "encodes
    // as it goes" and requires the value to set as a parameter.  Yet it is
    // counterintuitive given the "left-to-right" nature of the language:
    //
    //     >> foo: make object! [[bar][bar: 10]]
    //
    //     >> foo/(print "left" 'bar): (print "right" 20)
    //     right
    //     left
    //     == 20
    //
    // VOID! and NULL assigns are allowed: https://forum.rebol.info/t/895/4

      case REB_SET_PATH:
      case REB_SET_TUPLE: {
        if (HEART_BYTE(v) == REB_WORD) {
            assert(VAL_WORD_SYM(v) == SYM__SLASH_1_);
            goto process_set_word;
        }

        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, v))
            goto return_thrown;

      set_path_with_out:

        if (Eval_Path_Throws_Core(
            f_spare,  // output if thrown, used as scratch space otherwise
            v,  // !!! may not be array-based
            f_specifier,
            f->out,
            EVAL_MASK_DEFAULT  // evaluating GROUP!s ok
        )){
            Move_Value(f->out, f_spare);
            goto return_thrown;
        }

        break; }


    //=//// GET-PATH! and GET-TUPLE! //////////////////////////////////////=//
    //
    // Note that the GET native on a PATH! won't allow GROUP! execution:
    //
    //    foo: [X]
    //    path: 'foo/(print "side effect!" 1)
    //    get path  ; not allowed, due to surprising side effects
    //
    // However a source-level GET-PATH! allows them, since they are at the
    // callsite and you are assumed to know what you are doing:
    //
    //    :foo/(print "side effect" 1)  ; this is allowed
    //
   // Consistent with GET-WORD!, a GET-PATH! won't allow VOID! access.

      case REB_GET_PATH:
      case REB_GET_TUPLE:
        if (HEART_BYTE(v) == REB_WORD) {
            assert(VAL_WORD_SYM(v) == SYM__SLASH_1_);
            goto process_get_word;
        }

        if (Get_Path_Throws_Core(f->out, v, f_specifier))
            goto return_thrown;

        if (IS_VOID(f->out))  // need GET/ANY if it's void ("undefined")
            fail (Error_Need_Non_Void_Core(v, f_specifier, f->out));

        // !!! This didn't appear to be true for `-- "hi" "hi"`, processing
        // GET-PATH! of a variadic.  Review if it should be true.
        //
        /* assert(NOT_CELL_FLAG(f->out, CELL_FLAG_UNEVALUATED)); */
        CLEAR_CELL_FLAG(f->out, UNEVALUATED);
        break;


    //=//// GET-GROUP! ////////////////////////////////////////////////////=//
    //
    // This was initially conceived such that `:(x)` was a shorthand for the
    // expression `get x`.  But that's already pretty short--and arguably a
    // cleaner way of saying the same thing.  So instead, it's given the same
    // meaning in the evaluator as plain GROUP!...which seems wasteful on the
    // surface, but it means dialects can be free to use it to make a
    // distinction.  For instance, it's used to escape soft quoted slots.

      case REB_GET_GROUP:
        goto eval_group;


    //=//// SET-GROUP! ////////////////////////////////////////////////////=//
    //
    // Synonym for SET on the produced thing, unless it's an action...in which
    // case an arity-1 function is allowed to be called and passed the right.

      case REB_SET_GROUP: {
        //
        // Protocol for all the REB_SET_XXX is to evaluate the right before
        // the left.  Same with SET_GROUP!.  (Consider in particular the case
        // of PARSE, where it has to hold the SET-GROUP! in suspension while
        // it looks on the right in order to decide if it will run it at all!)
        //
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, v))
            goto return_thrown;

        f_next_gotten = nullptr;  // arbitrary code changes fetched variables

        if (Do_Any_Array_At_Throws(f_spare, v, f_specifier)) {
            Move_Value(f->out, f_spare);
            goto return_thrown;
        }

        if (IS_ACTION(f_spare)) {
            //
            // Indicate that next argument should be taken from f->out
            //
            assert(NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT));
            SET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);

            // Apply the function, and we can reuse this frame to do it.
            //
            // !!! But really it should not be allowed to take more than one
            // argument.  Hence rather than go through reevaluate, channel
            // it through a variant of the enfix machinery (the way that
            // CHAIN does, which similarly reuses the frame but probably
            // should also be restricted to a single value...though it's
            // being experimented with letting it take more.)
            //
            DECLARE_ACTION_SUBFRAME (subframe, f);
            Push_Frame(f->out, subframe);
            Push_Action(subframe, VAL_ACTION(f_spare), VAL_BINDING(f_spare));
            Begin_Prefix_Action(subframe, nullptr);  // no label

            goto process_action;
        }

        v = f_spare;

        if (ANY_WORD(f_spare)) {
            goto set_word_with_out;
        }
        else if (ANY_PATH(f_spare)) {
            goto set_path_with_out;
        }
        else if (ANY_BLOCK(f_spare)) {
            fail ("Retriggering multi-returns not implemented ATM");
        }

        fail (Error_Bad_Set_Group_Raw()); }


    //=//// GET-BLOCK! ////////////////////////////////////////////////////=//
    //
    // !!! Currently just inert, which may end up being its ultimate usage

      case REB_GET_BLOCK:
        Derelativize(f->out, v, f_specifier);
        break;


    //=//// SET-BLOCK! ////////////////////////////////////////////////////=//
    //
    // The evaluator treats SET-BLOCK! specially as a means for implementing
    // multiple return values.  The trick is that it does so by pre-loading
    // arguments in the frame with variables to update, in a way that could've
    // historically been achieved with passing WORD! or PATH! to a refinement.
    // So if there was a function that updates a variable you pass in by name:
    //
    //     result: updating-function/update arg1 arg2 'var
    //
    // The /UPDATE parameter is marked as being effectively a "return value",
    // so that equivalent behavior can be achieved with:
    //
    //     [result var]: updating-function arg1 arg2
    //
    // !!! This is a very slow-running prototype of the desired behavior.  It
    // is a mock up intended to find any flaws in the concept before writing
    // faster native code that would require rewiring the evaluator somewhat.

      case REB_SET_BLOCK: {
        assert(NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT));

        if (VAL_LEN_AT(v) == 0)
            fail ("SET-BLOCK! must not be empty for now.");

        const RELVAL *check = VAL_ARRAY_AT(v);
        for (; NOT_END(check); ++check) {
            if (IS_BLANK(check) or IS_WORD(check) or IS_PATH(check))
                continue;
            if (Is_Blackhole(check))
                continue;
            fail ("SET-BLOCK! elements must be WORD/PATH/BLANK/ISSUE for now");
        }

        if (not (IS_WORD(f_next) or IS_PATH(f_next) or IS_ACTION(f_next)))
            fail ("SET_BLOCK! must be followed by WORD/PATH/ACTION for now.");

        // Turn SET-BLOCK! into a BLOCK! in `f->out` for easier processing.
        //
        Derelativize(f->out, v, f_specifier);
        mutable_KIND3Q_BYTE(f->out) = REB_BLOCK;
        mutable_HEART_BYTE(f->out) = REB_BLOCK;

        // Get the next argument as an ACTION!, specialized if necessary, into
        // the `spare`.  We'll specialize it further to set any output
        // arguments to words from the left hand side.
        //
        if (Get_If_Word_Or_Path_Throws(
            f_spare,
            f_next,
            f_specifier,
            false
        )){
            goto return_thrown;
        }

        if (not IS_ACTION(f_spare))
            fail ("SET-BLOCK! is only allowed to have ACTION! on right ATM.");

        // Find all the "output" parameters.  Right now that's any parameter
        // which is marked as being legal to be word! or path! *specifically*.
        //
        const REBU64 ts_out = FLAGIT_KIND(REB_TS_REFINEMENT)
            | FLAGIT_KIND(REB_NULL)
            | FLAGIT_KIND(REB_ISSUE)  // for Is_Blackhole() use with SET
            | FLAGIT_KIND(REB_WORD)
            | FLAGIT_KIND(REB_PATH);

        REBDSP dsp_outputs = DSP;
        REBVAL *temp = ACT_PARAMS_HEAD(VAL_ACTION(f_spare));
        for (; NOT_END(temp); ++temp) {
            if (not TYPE_CHECK_EXACT_BITS(temp, ts_out))
                continue;
            Init_Word(DS_PUSH(), VAL_TYPESET_STRING(temp));
        }

        DECLARE_LOCAL(outputs);
        Init_Block(outputs, Pop_Stack_Values(dsp_outputs));
        PUSH_GC_GUARD(outputs);

        // Now create a function to splice in to the execution stream that
        // specializes what we are calling so the output parameters have
        // been preloaded with the words or paths from the left block.
        //
        REBVAL *specialized = rebValue(
            "enclose specialize", rebQ(f_spare), "collect [ use [block] [",
                "block: next", f->out,
                "for-each output", outputs, "["
                    "if tail? block [break]",  // no more outputs wanted
                    "if block/1 [",  // interested in this result
                        "keep setify output",
                        "keep quote compose block/1",  // pre-compose, safety
                    "]",
                    "block: next block",
                "]",
                "if not tail? block [fail {Too many multi-returns}]",
            "] ] func [f] [",
                "for-each output", outputs, "[",
                    "if f/(output) [",  // void in case func doesn't (null?)
                        "set f/(output) ~undefined~",
                    "]",
                "]",
                "either first", f->out, "@[",
                    "set first", f->out, "do f",
                "] @[do f]",
            "]",
        rebEND);

        DROP_GC_GUARD(outputs);

        Move_Value(f_spare, specialized);
        rebRelease(specialized);

        // Toss away the pending WORD!/PATH!/ACTION! that was in the execution
        // stream previously.
        //
        Fetch_Next_Forget_Lookback(f);

        // Interject the function with our multiple return arguments and
        // return value assignment step.
        //
        gotten = f_spare;
        v = f_spare;

        goto evaluate; }


    //=////////////////////////////////////////////////////////////////////=//
    //
    // Treat all the other Is_Bindable() types as inert
    //
    //=////////////////////////////////////////////////////////////////////=//

      case REB_BLOCK:
        //
      case REB_SYM_BLOCK:
      case REB_SYM_GROUP:
      case REB_SYM_PATH:
      case REB_SYM_WORD:
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
      case REB_FRAME:
      case REB_MODULE:
      case REB_ERROR:
      case REB_PORT:
        goto inert;


    //=///////////////////////////////////////////////////////////////////=//
    //
    // Treat all the other NOT Is_Bindable() types as inert
    //
    //=///////////////////////////////////////////////////////////////////=//

      case REB_VOID:
        //
        // "A void! is a means of giving a hot potato back that is a warning
        //  something, but you don't want to force an error 'in the moment'...
        //  in case the returned information wasn't going to be used anyway."
        //
        // https://forum.rebol.info/t/947
        //
        // So it was thought that voids might be something to error on if
        // seen by the evaluator.  But in the balance of things, making them
        // inert is more useful:
        //
        // https://forum.rebol.info/t/1383/8
        //
      case REB_BLANK:
        //
      case REB_LOGIC:
      case REB_INTEGER:
      case REB_DECIMAL:
      case REB_PERCENT:
      case REB_MONEY:
      case REB_PAIR:
      case REB_TIME:
      case REB_DATE:
        //
      case REB_DATATYPE:
      case REB_TYPESET:
        //
      case REB_EVENT:
      case REB_HANDLE:

      case REB_CUSTOM:  // custom types (IMAGE!, VECTOR!) are all inert

      inert:

        Inertly_Derelativize_Inheriting_Const(f->out, v, f->feed);
        break;


    //=//// QUOTED! (at 4 or more levels of escaping) /////////////////////=//
    //
    // This is the form of literal that's too escaped to just overlay in the
    // cell by using a higher kind byte.  See the `default:` case in this
    // switch for handling the more compact forms, that are much more common.
    //
    // (Highly escaped literals should be rare, but for completeness you need
    // to be able to escape any value, including any escaped one...!)

      case REB_QUOTED:
        Derelativize(f->out, v, f_specifier);
        Unquotify(f->out, 1);  // take off one level of quoting
        break;


    //=//// QUOTED! (at 3 levels of escaping or less...or garbage) ////////=//
    //
    // All the values for types at >= REB_64 currently represent the special
    // compact form of literals, which overlay inside the cell they escape.
    // The real type comes from the type modulo 64.

      default:
        Derelativize(f->out, v, f_specifier);
        Unquotify_In_Situ(f->out, 1);  // checks for illegal REB_XXX bytes
        break;
    }


  //=//// END MAIN SWITCH STATEMENT ///////////////////////////////////////=//

    // The UNEVALUATED flag is one of the bits that doesn't get copied by
    // Move_Value() or Derelativize().  Hence it can be overkill to clear it
    // off if one knows a value came from doing those things.  This test at
    // the end checks to make sure that the right thing happened.
    //
    // !!! This check requires caching the kind of `v` at the start of switch.
    // Is it worth it to do so?
    //
    /*if (ANY_INERT_KIND(kind_current)) {  // if() to check which part failed
        assert(GET_CELL_FLAG(f->out, UNEVALUATED));
    }
    else if (GET_CELL_FLAG(f->out, UNEVALUATED)) {
        //
        // !!! Should ONLY happen if we processed a WORD! that looked up to
        // an invisible function, and left something behind that was not
        // previously evaluative.  To track this accurately, we would have
        // to use an EVAL_FLAG_DEBUG_INVISIBLE_UNEVALUATIVE here, because we
        // don't have the word anymore to look up (and even if we did, what
        // it looks up to may have changed).
        //
        assert(kind_current == REB_WORD or ANY_INERT(f->out));
    }*/

    // We're sitting at what "looks like the end" of an evaluation step.
    // But we still have to consider enfix.  e.g.
    //
    //    [pos val]: evaluate [1 + 2 * 3]
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
    // Note that invisible functions have to be considered in the lookahead
    // also.  Consider this case:
    //
    //    [pos val]: evaluate [1 + 2 * comment ["hi"] 3 4 / 5]
    //
    // We want `val = 9`, with `pos = [4 / 5]`.  To do this, we
    // can't consider an evaluation finished until all the "invisibles" have
    // been processed.
    //
    // If that's not enough to consider :-) it can even be the case that
    // subsequent enfix gets "deferred".  Then, possibly later the evaluated
    // value gets re-fed back in, and we jump right to this post-switch point
    // to give it a "second chance" to take the enfix.  (See 'deferred'.)
    //
    // So this post-switch step is where all of it happens, and it's tricky!

  lookahead:

    // If something was run with the expectation it should take the next arg
    // from the output cell, and an evaluation cycle ran that wasn't an
    // ACTION! (or that was an arity-0 action), that's not what was meant.
    // But it can happen, e.g. `x: 10 | x ->-`, where ->- doesn't get an
    // opportunity to quote left because it has no argument...and instead
    // retriggers and lets x run.

    if (GET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT)) {
        if (GET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH))
            fail (Error_Literal_Left_Path_Raw());

        assert(!"Unexpected lack of use of NEXT_ARG_FROM_OUT");
    }

  //=//// IF NOT A WORD!, IT DEFINITELY STARTS A NEW EXPRESSION ///////////=//

    // For long-pondered technical reasons, only WORD! is able to dispatch
    // enfix.  If it's necessary to dispatch an enfix function via path, then
    // a word is used to do it, like `>-` in `x: >- lib/method [...] [...]`.

    switch (KIND3Q_BYTE_UNCHECKED(f_next)) {
      case REB_0_END:
        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
        goto finished;  // hitting end is common, avoid do_next's switch()

      case REB_WORD:
        break;  // need to check for lookahead

      case REB_PATH: {  // need to check for lookahead *if* just a `/`
        if (
            GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)
            or HEART_BYTE(f_next) != REB_WORD
        ){
            CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
            goto finished;
        }

        // Although the `/` case appears to be a PATH!, it is actually a
        // WORD! under the hood and can have a binding.  The "spelling" of
        // this word is an alias, because `/` is purposefully not legal in
        // words.)  Operations based on VAL_TYPE() or CELL_TYPE() will see it
        // as PATH!, but CELL_KIND() will interpret the cell bits as a word.
        //
        if (VAL_WORD_SPELLING(f_next) != PG_Slash_1_Canon)
            goto finished;  // optimized refinement (see IS_REFINEMENT())
        break; }

      default:
        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
        goto finished;
    }

  //=//// FETCH WORD! TO PERFORM SPECIAL HANDLING FOR ENFIX/INVISIBLES ////=//

    // First things first, we fetch the WORD! (if not previously fetched) so
    // we can see if it looks up to any kind of ACTION! at all.

    if (not f_next_gotten)
        f_next_gotten = Try_Lookup_Word(f_next, f_specifier);
    else
        assert(f_next_gotten == Try_Lookup_Word(f_next, f_specifier));

  //=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-ENFIX ///////////=//

    // These cases represent finding the start of a new expression.
    //
    // Fall back on word-like "dispatch" even if ->gotten is null (unset or
    // unbound word).  It'll be an error, but that code path raises it for us.

    if (
        not f_next_gotten
        or not IS_ACTION(f_next_gotten)
        or not GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), ENFIXED)
    ){
      lookback_quote_too_late: // run as if starting new expression

        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

        // Since it's a new expression, EVALUATE doesn't want to run it
        // even if invisible, as it's not completely invisible (enfixed)
        //
        goto finished;
    }

  //=//// IS WORD ENFIXEDLY TIED TO A FUNCTION (MAY BE "INVISIBLE") ///////=//

    if (GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), QUOTES_FIRST)) {
        //
        // Left-quoting by enfix needs to be done in the lookahead before an
        // evaluation, not this one that's after.  This happens in cases like:
        //
        //     left-lit: enfix func [:value] [:value]
        //     lit <something> left-lit
        //
        // But due to the existence of <end>-able and <skip>-able parameters,
        // the left quoting function might be okay with seeing nothing on the
        // left.  Start a new expression and let it error if that's not ok.
        //
        assert(NOT_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH));
        if (GET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH))
            fail (Error_Literal_Left_Path_Raw());

        REBVAL *first = First_Unspecialized_Param(VAL_ACTION(f_next_gotten));
        if (VAL_PARAM_CLASS(first) == REB_P_SOFT_QUOTE) {
            if (GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)) {
                CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
                goto finished;
            }
        }
        else if (NOT_EVAL_FLAG(f, INERT_OPTIMIZATION))
            goto lookback_quote_too_late;
    }

    if (
        GET_EVAL_FLAG(f, FULFILLING_ARG)
        and not (
            GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), DEFERS_LOOKBACK)
                                       // ^-- `1 + if false [2] else [3]` => 4
/*            or GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), IS_INVISIBLE)
                                       // ^-- `1 + 2 + comment "foo" 3` => 6 */
        )
    ){
        if (GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)) {
            // Don't do enfix lookahead if asked *not* to look.

            CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

            assert(NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX));
            SET_FEED_FLAG(f->feed, DEFERRING_ENFIX);

            goto finished;
        }

        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
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
        GET_EVAL_FLAG(f, FULFILLING_ARG)
        and (
            GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), POSTPONES_ENTIRELY)
            or (
                GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), DEFERS_LOOKBACK)
                and NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX)
            )
        )
    ){
        if (GET_EVAL_FLAG(f->prior, ERROR_ON_DEFERRED_ENFIX)) {
            //
            // Operations that inline functions by proxy (such as MATCH and
            // ENSURE) cannot directly interoperate with THEN or ELSE...they
            // are building a frame with PG_Dummy_Action as the function, so
            // running a deferred operation in the same step is not an option.
            // The expression to the left must be in a GROUP!.
            //
            fail (Error_Ambiguous_Infix_Raw());
        }

        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

        if (not Is_Action_Frame_Fulfilling(f->prior)) {
            //
            // This should mean it's a variadic frame, e.g. when we have
            // the 2 in the output slot and are at the THEN in:
            //
            //     variadic2 1 2 then (t => [print ["t is" t] <then>])
            //
            // We want to treat this like a barrier.
            //
            SET_FEED_FLAG(f->feed, BARRIER_HIT);
            goto finished;
        }

        SET_FEED_FLAG(f->feed, DEFERRING_ENFIX);

        // Leave enfix operator pending in the frame.  It's up to the parent
        // frame to decide whether to ST_EVALUATOR_LOOKING_AHEAD to jump
        // back in and finish fulfilling this arg or not.  If it does resume
        // and we get to this check again, f->prior->deferred can't be null,
        // otherwise it would be an infinite loop.
        //
        goto finished;
    }

    CLEAR_FEED_FLAG(f->feed, DEFERRING_ENFIX);

    // An evaluative lookback argument we don't want to defer, e.g. a normal
    // argument or a deferable one which is not being requested in the context
    // of parameter fulfillment.  We want to reuse the f->out value and get it
    // into the new function's frame.

  blockscope {
    DECLARE_ACTION_SUBFRAME (subframe, f);
    Push_Frame(f->out, subframe);
    Push_Action(subframe,
        VAL_ACTION(f_next_gotten),
        VAL_BINDING(f_next_gotten)
    );
    Begin_Enfix_Action(subframe, VAL_WORD_SPELLING(f_next));

    Fetch_Next_Forget_Lookback(f);  // advances next
    goto process_action; }

  return_thrown:

  #if !defined(NDEBUG)
    Eval_Core_Exit_Checks_Debug(f);   // called unless a fail() longjmps
    // don't care if f->flags has changes; thrown frame is not resumable
  #endif

    return true;  // true => thrown

  finished:

    // Want to keep this flag between an operation and an ensuing enfix in
    // the same frame, so can't clear in Drop_Action(), e.g. due to:
    //
    //     left-lit: enfix :lit
    //     o: make object! [f: does [1]]
    //     o/f left-lit  ; want error suggesting >- here, need flag for that
    //
    CLEAR_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH);
    assert(NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT));  // must be consumed

  #if !defined(NDEBUG)
    Eval_Core_Exit_Checks_Debug(f);  // called unless a fail() longjmps
    assert(NOT_EVAL_FLAG(f, DOING_PICKUPS));
    assert(
        (f->flags.bits & ~EVAL_FLAG_TOOK_HOLD) == initial_flags
    );  // any change should be restored, but va_list reification may hold
  #endif

    return false;  // false => not thrown
}
