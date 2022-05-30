//
//  File: %c-eval.c
//  Summary: "Central Interpreter Evaluator"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
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
//   carry a stale marking.  This also means that END is a possible product
//   of the evaluation.  Higher-level helpers blend in Reify_Eval_Out_Plain() or
//   Reify_Eval_Out_Meta() to avoid having to deal with these low-level issues,
//   but core code wanting to be efficient can get all the insight of a ^META
//   detection without going through quoting/etc.
//
// * _Throws => The return result is a boolean which all callers *must* heed.
//   There is no "thrown value" data type or cell flag, so the only indication
//   that a throw happened comes from this flag.  See %sys-throw.h
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * See %sys-eval.h for wrappers that make it easier to set up frames and
//   use the evaluator for a single step.
//
// * See %sys-do.h for wrappers that make it easier to run multiple evaluator
//   steps in a frame and return the final result, giving ~void~ by default.
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


#if DEBUG_COUNT_TICKS  // <-- THIS IS VERY USEFUL, SEE %sys-eval.h!
    //
    // TG_Tick counter is incremented each time a function dispatcher is run
    // or a parse rule is executed.  See UPDATE_TICK_COUNT().
    //

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

// We make the macro for getting specifier a bit more complex here, to
// account for reevaluation.  To help annotate why it's weird, we call it
// `v_specifier` instead.
//
// https://forum.rebol.info/t/should-reevaluate-apply-let-bindings/1521
//
#undef f_specifier
#define v_specifier \
    (STATE_BYTE(f) == ST_EVALUATOR_REEVALUATING \
        ? SPECIFIED \
        : FEED_SPECIFIER(f->feed))

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
// !!! Evil Macro, repeats parent!
//
#define DECLARE_ACTION_SUBFRAME(f,parent) \
    DECLARE_FRAME (f, (parent)->feed, \
        EVAL_MASK_DEFAULT | /*EVAL_FLAG_KEEP_STALE_BIT |*/ ((parent)->flags.bits \
            & (EVAL_FLAG_FULFILLING_ARG | EVAL_FLAG_RUNNING_ENFIX \
                | EVAL_FLAG_DIDNT_LEFT_QUOTE_PATH))) \
    if (GET_EVAL_FLAG((parent), META_OUT)) \
        SET_EVAL_FLAG(f, META_OUT)


#if DEBUG_EXPIRED_LOOKBACK
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (f->feed->stress != nullptr)
#else
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (v == &f->feed->lookback)
#endif


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
    // This flag is used for enfix processing, but it's also applied as an
    // internal trick to make SHOVE (>-) work:
    //
    //    >> 10 >- x:
    //    == 10
    //
    //    >> x
    //    == 10
    //
    if (GET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT))  {
        CLEAR_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);
        CLEAR_CELL_FLAG(f->out, UNEVALUATED);  // this helper counts as eval
        return false;
    }

    // Beyond the trick for `>-` the output cell should not be visible to the
    // assignment:
    //
    //    >> (1 + 2 x: comment "x should not be three")
    //    == <the prior value of X, cool feature!, awesome used with MAYBE!>
    //
    // So all rightward evaluations set the output to end.  Note that any
    // enfix quoting operators that would quote backwards to see the `x:` would
    // have intercepted it during a lookahead...pre-empting this code.
    //
    SET_END(f->out);

    if (IS_END(f_next)) {
        if (IS_META(v))  // allow (@), case makes END into ~void~
            return false;

        // `do [x:]`, `do [o.x:]`, etc. are illegal
        fail (Error_Need_Non_End(v));
    }

    // Using a SET-XXX! means you always have at least two elements; it's like
    // an arity-1 function.  `1 + x: whatever ...`.  This overrides the no
    // lookahead behavior flag right up front.
    //
    CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

    REBFLGS flags = EVAL_MASK_DEFAULT
            | (f->flags.bits & EVAL_FLAG_FULFILLING_ARG);  // if f was, we are

    if (CURRENT_CHANGES_IF_FETCH_NEXT) {  // must use new frame
        if (Eval_Step_In_Subframe_Maybe_Stale_Throws(f->out, f, flags))
            return true;
    }
    else {  // !!! Reusing the frame, would inert optimization be worth it?
        // !!! If reevaluating, this will forget that we are doing so.
        //
        STATE_BYTE(f) = ST_EVALUATOR_INITIAL_ENTRY;

        if (Eval_Maybe_Stale_Throws(f))  // reuse `f`
            return true;

        // We *could* keep evaluating as long as evaluations vanish:
        //
        //    >> x: 1020
        //
        //    >> x: comment "hi" 2
        //    == 2
        //
        //    >> x
        //    == 2
        //
        // But this is not done.  Instead, we treat an invisible evaluation
        // step as a no-op that evaluates to the value of the variable.
        //
        // https://forum.rebol.info/t/1582/5
    }

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
    if (GET_EVAL_FLAG(f, NO_EVALUATIONS)) {  // see flag for why this exists
        if (IS_END(f->feed->value))
            return false;
        Derelativize(f->out, f->feed->value, FEED_SPECIFIER(f->feed));
        SET_CELL_FLAG(f->out, UNEVALUATED);
        Fetch_Next_Forget_Lookback(f);
        return false;
    }

  #if DEBUG_ENSURE_FRAME_EVALUATES
    f->was_eval_called = true;  // see definition for why this flag exists
  #endif

  #if DEBUG_COUNT_TICKS
    REBTCK tick = f->tick = TG_Tick;  // snapshot tick for C watchlist viewing
  #endif

  #if !defined(NDEBUG)
    REBFLGS initial_flags = f->flags.bits & ~(
        EVAL_FLAG_FULFILL_ONLY  // can be requested or <blank> can trigger
        | EVAL_FLAG_RUNNING_ENFIX  // can be requested with REEVALUATE_CELL
        | FLAG_STATE_BYTE(255)  // state is forgettable
    );  // should be unchanged on exit
  #endif

    assert(DSP >= f->dsp_orig);  // REDUCE accrues, APPLY adds refinements
    assert(INITABLE(f->out));  // all invisible will preserve output
    assert(f->out != f_spare);  // overwritten by temporary calculations

    // A barrier shouldn't cause an error in evaluation if code would be
    // willing to accept an <end>.  So we allow argument gathering to try to
    // run, but it may error if that's not acceptable.
    //
    if (GET_FEED_FLAG(f->feed, BARRIER_HIT)) {
        if (GET_EVAL_FLAG(f, FULFILLING_ARG)) {
            Mark_Eval_Out_Stale(f->out);
            return false;
        }
        CLEAR_FEED_FLAG(f->feed, BARRIER_HIT);  // not an argument, clear flag
    }

    const RELVAL *v;  // shorthand for the value we are switch()-ing on
    TRASH_POINTER_IF_DEBUG(v);

    option(const REBVAL*) gotten;
    TRASH_OPTION_IF_DEBUG(gotten);

    // Given how the evaluator is written, it's inevitable that there will
    // have to be a test for points to `goto` before running normal eval.
    // This cost is paid on every entry to Eval_Core().
    //
    switch (STATE_BYTE(f)) {
      case ST_EVALUATOR_INITIAL_ENTRY:
        break;

      case ST_EVALUATOR_LOOKING_AHEAD:
        goto lookahead;

      case ST_EVALUATOR_REEVALUATING: {  // v-- IMPORTANT: Keep STATE_BYTE()
        //
        // It's important to leave STATE_BYTE() as ST_EVALUATOR_REEVALUATING
        // during the switch state, because that's how the evaluator knows
        // not to redundantly apply LET bindings.  See `v_specifier` above.

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
                VAL_ACTION_BINDING(f->u.reval.value)
            );
            Begin_Enfix_Action(subframe, VAL_ACTION_LABEL(f->u.reval.value));
                // ^-- invisibles cache NO_LOOKAHEAD

            SET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);

            assert(Is_Fresh(f_spare));
            goto process_action;
        }

        if (NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT))
            Mark_Eval_Out_Stale(f->out);

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

    #if !defined(NDEBUG)  // Total_Eval_Cycles is periodically reconciled
        ++Total_Eval_Cycles_Doublecheck;
    #endif

    if (--Eval_Countdown <= 0) {
        //
        // Note that Do_Signals_Throws() may do a recycle step of the GC, or
        // it may spawn an entire interactive debugging session via
        // breakpoint before it returns.  It may also FAIL and longjmp out.
        //
        if (Do_Signals_Throws(f->out))
            goto return_thrown;
    }

    assert(NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT));

    // f->out might be merely "prepped" in which case the header is all 0 bits.
    // This is considered INITABLE() but not WRITABLE(), so the SET_CELL_FLAG()
    // routines will reject it.  While we are already doing a flag masking
    // operation to add CELL_FLAG_STALE, ensure the cell carries the NODE and
    // CELL flags (we already checked that it was INITABLE()).  This promotes
    // 0 prep cells to a readable END state for checking after the eval.
    //
    // Note that adding CELL_FLAG_STALE means the out cell won't act as the
    // input to an enfix operation.
    //
    f->out->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL | CELL_FLAG_STALE
    );

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
    f_next_gotten = Lookup_Word(f_next, FEED_SPECIFIER(f->feed));

    if (not f_next_gotten or not IS_ACTION(unwrap(f_next_gotten)))
        goto give_up_backward_quote_priority;  // note only ACTION! is ENFIXED

    if (GET_ACTION_FLAG(VAL_ACTION(unwrap(f_next_gotten)), IS_BARRIER)) {
        //
        // In a situation like `foo |`, we want FOO to be able to run...it
        // may take 0 args or it may be able to tolerate END.  But we should
        // not be required to run the barrier in the same evaluative step
        // as the left hand side.  (It can be enfix, or it can not be.)
        //
        SET_FEED_FLAG(f->feed, BARRIER_HIT);
        goto give_up_backward_quote_priority;
    }

    if (NOT_ACTION_FLAG(VAL_ACTION(unwrap(f_next_gotten)), ENFIXED))
        goto give_up_backward_quote_priority;

  blockscope {
    REBACT *enfixed = VAL_ACTION(unwrap(f_next_gotten));

    if (NOT_ACTION_FLAG(enfixed, QUOTES_FIRST))
        goto give_up_backward_quote_priority;

    // If the action soft quotes its left, that means it's aware that its
    // "quoted" argument may be evaluated sometimes.  If there's evaluative
    // material on the left, treat it like it's in a group.
    //
    if (
        GET_ACTION_FLAG(enfixed, POSTPONES_ENTIRELY)
        or (
            GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)
            and not ANY_SET_KIND(kind_current)  // not SET-WORD!, SET-PATH!...
        )
    ){
        // !!! cache this test?
        //
        const REBPAR *first = First_Unspecialized_Param(nullptr, enfixed);
        if (
            VAL_PARAM_CLASS(first) == PARAM_CLASS_SOFT
            or VAL_PARAM_CLASS(first) == PARAM_CLASS_META
        ){
            goto give_up_backward_quote_priority;  // yield as an exemption
        }
    }

    // Let the <skip> flag allow the right hand side to gracefully decline
    // interest in the left hand side due to type.  This is how DEFAULT works,
    // such that `case [condition [...] default [...]]` does not interfere
    // with the BLOCK! on the left, but `x: default [...]` gets the SET-WORD!
    //
    if (GET_ACTION_FLAG(enfixed, SKIPPABLE_FIRST)) {
        const REBPAR *first = First_Unspecialized_Param(nullptr, enfixed);
        if (not TYPE_CHECK(first, kind_current))  // left's kind
            goto give_up_backward_quote_priority;
    }

    // Lookback args are fetched from f->out, then copied into an arg
    // slot.  Put the backwards quoted value into f->out.
    //
    Derelativize(f->out, v, v_specifier);  // for NEXT_ARG_FROM_OUT
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
        // This lets us do e.g. `(just =>)` or `help of`
        //
        // Swap it around so that what we had put in the f->out goes back
        // to being in the lookback cell and can be used as current.  Then put
        // what was current into f->out so it can be consumed as the first
        // parameter of whatever that was.

        Move_Cell(&f->feed->lookback, f->out);
        Derelativize(f->out, v, v_specifier);
        SET_CELL_FLAG(f->out, UNEVALUATED);

        // leave *next at END
        v = &f->feed->lookback;
        gotten = nullptr;

        SET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH);  // for better error message
        SET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);  // literal right op is arg

        goto give_up_backward_quote_priority;  // run PATH!/WORD! normal
    }
  }

    // Wasn't the at-end exception, so run normal enfix with right winning.
    //
  blockscope {
    DECLARE_ACTION_SUBFRAME (subframe, f);
    Push_Frame(f->out, subframe);
    Push_Action(
        subframe,
        VAL_ACTION(unwrap(gotten)),
        VAL_ACTION_BINDING(unwrap(gotten))
    );
    Begin_Enfix_Action(subframe, VAL_WORD_SYMBOL(v));

    SET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);

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
    // with `reeval first []`.
    //
    // Note: It seems tempting to let NULL evaluate to NULL as a convenience
    // for such cases.  But this breaks the system in subtle ways--like
    // making it impossible to "reify" the instruction stream as a BLOCK!
    // for the debugger.  Mechanically speaking, this is best left an error.
    //
    // Note: The API can't allow splicing of null values in the instruction
    // streams:
    //
    //     REBVAL *v = nullptr;
    //     bool is_null = rebUnboxLogic("null?", v);  // should be rebQ(v)
    //
    // But what it does as a compromise is it will make the spliced values
    // into ~null~ BAD-WORD!s.  This will sometimes work out due to decay
    // (though not the above case, as NULL? will error when passed isotopes).
    // Yet a convenience is supplied by making the @ operator turn ~null~
    // BAD-WORD!s into proper nulls:
    //
    //     bool is_null = rebUnboxLogic("null?", rebQ(v));
    //     bool is_null = rebUnboxLogic("null? @", v);  // equivalent, shorter

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
        Push_Action(subframe, VAL_ACTION(v), VAL_ACTION_BINDING(v));
        Begin_Prefix_Action(subframe, VAL_ACTION_LABEL(v));

        // We'd like `10 -> = 5 + 5` to work, and to do so it reevaluates in
        // a new frame, but has to run the `=` as "getting its next arg from
        // the output slot, but not being run in an enfix mode".
        //
        if (NOT_FEED_FLAG(subframe->feed, NEXT_ARG_FROM_OUT))
            Mark_Eval_Out_Stale(subframe->out);

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
        // Note: However, the evaluation of an invisible can leave a stale
        // value, e.g. `do [comment "hi" 10]`...there is no prior value in
        // f->out to serve as a return.  At one time this retriggered, but the
        // new simplifying rule is that we do not support such non-interstitial
        // "looping" within one evaluator step.  The invisible result must
        // be dealt with by DO itself.
        //
        // When the idea was more ambitious, like `1 + comment [2 * 3] 4`,
        // the retriggering was done like this:
        //
        //      if (
        //          GET_EVAL_FLAG(f, FULFILLING_ARG)
        //          and Is_Stale(f->out)
        //          and NOT_END(f_next)
        //      ){
        //          gotten = f_next_gotten;
        //          v = Lookback_While_Fetching_Next(f);
        //          goto evaluate;
        //      }
        //
        // But experience taught us this led to brittle, confusing, and
        // unsustainable behavior.  It certainly can't be done by UPARSE, as
        // it wires together combinators in advance; so the warping of
        // structure implied by this is impossible.  In the name of simplicity,
        // only interstitial invisibility is supported (between function calls)
        // and you must put invisibles in groups to collect them as part of
        // processing a single argument.
        //
        break; }


    //=//// WORD! and SYMBOL! //////////////////////////////////////////////=//
    //
    // A plain word tries to fetch its value through its binding.  It fails
    // if the word is unbound (or if the binding is to a variable which is
    // set, but to a non-isotope form of bad-word!).  Should the word look up
    // to an action, then that action will be invoked.
    //
    // SYMBOL! behaves the same way.  The only reason SYMBOL! is in a different
    // category is to communicate that it does not come in SET-/GET-/META-/THE-
    // variations...but it can be overriden in the same way.  (It's possible
    // to SET a symbol or to put it in a SET-BLOCK!, e.g. `[@]: 10`)
    //
    // NOTE: The usual dispatch of enfix functions is *not* via a REB_WORD in
    // this switch, it's by some code at the `lookahead:` label.  You only see
    // enfix here when there was nothing to the left, so cases like `(+ 1 2)`
    // or in "stale" left hand situations like `10 comment "hi" + 20`.

      case REB_SYMBOL:
      process_word:
      case REB_WORD:
        if (not gotten)
            gotten = Lookup_Word_May_Fail(v, v_specifier);

        if (IS_ACTION(unwrap(gotten))) {  // before IS_BAD_WORD() is common case
            REBACT *act = VAL_ACTION(unwrap(gotten));

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
            Push_Action(subframe, act, VAL_ACTION_BINDING(unwrap(gotten)));
            Begin_Action_Core(
                subframe,
                VAL_WORD_SYMBOL(v),  // use word as label
                GET_ACTION_FLAG(act, ENFIXED)
            );

            if (GET_ACTION_FLAG(act, ENFIXED))
                SET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);

            goto process_action;
        }

        if (Is_Isotope(unwrap(gotten)))
            fail (Error_Bad_Word_Get(v, unwrap(gotten)));

        Copy_Cell(f->out, unwrap(gotten));  // no CELL_FLAG_UNEVALUATED
        break;


    //=//// SET-WORD! /////////////////////////////////////////////////////=//
    //
    // Right side is evaluated into `out`, and then copied to the variable.
    //
    // Null and void assigns are allowed: https://forum.rebol.info/t/895/4

      case REB_SET_WORD: {
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, v))  // see notes
            goto return_thrown;

        if (Is_Voided(f->out)) {
            //
            // Could have been none or void isotope...we leave it in its
            // quantum state for others to collapse.  But unset the variable
            // in either case.
            //
            Init_None(Sink_Word_May_Fail(v, v_specifier));
        }
        else if (Is_Invisible(f->out)) {
            //
            // When you assign an invisible to an output, it has no effect, and
            // returns the old value of the variable:
            //
            //     >> x: 10
            //
            //     >> x: comment "hi"
            //     == 10
            //
            //     >> x
            //     == 10
            //
            // This isn't frivolous; it is foundationally useful; for instance
            // MAYBE will convert light ~null~ isotopes to pure invisible:
            //
            //     >> x: <a>
            //
            //     >> x: maybe case [1 = 2 [<b>] 3 = 4 [<c>]]
            //     == <a>
            //
            //     >> x
            //     == <a>
            //
            // It becomes a truly generic feature for being able to opt-out
            // of assignments, just as pure invisibles can opt out of adding
            // a value to the chain of evaluation.
            //
            gotten = Lookup_Word_May_Fail(v, v_specifier);

            Copy_Cell(f->out, unwrap(gotten));
            assert(NOT_CELL_FLAG(f->out, UNEVALUATED));
        }
        else {
            Clear_Stale_Flag(f->out);

            if (IS_ACTION(f->out))  // cache the word's label in the cell
                INIT_VAL_ACTION_LABEL(f->out, VAL_WORD_SYMBOL(v));

            // Decay the isotope in variable, but don't decay overall result!
            //
            Copy_Cell(
                Sink_Word_May_Fail(v, v_specifier),
                Pointer_To_Decayed(f->out)
            );
        }

        // Running functions flushes the f_next_gotten cache.  But a plain
        // assignment can cause trouble too, if it doesn't trigger a function
        // call:
        //
        //     >> x: <before> x: 1 x
        //                         ^-- x value was cachedd in infix lookahead
        //
        // It used to not be a problem, when variables didn't just pop into
        // existence or have their caching states changed.  But INDEX_ATTACHED
        // and the various complexities involved with that means we have to
        // flush here if the symbols match.
        //
        if (f_next_gotten and VAL_WORD_SYMBOL(f_next) == VAL_WORD_SYMBOL(v))
            f_next_gotten = nullptr;

        break; }


    //=//// GET-WORD! /////////////////////////////////////////////////////=//
    //
    // A GET-WORD! does no dispatch on functions.  It will fetch other values
    // as normal, but will error on unfriendly BAD-WORD!.
    //
    // This handling matches Rebol2 behavior, choosing to break with R3-Alpha
    // and Red which will give back "UNSET!" if you use a GET-WORD!.  The
    // mechanics of @word are a completely new approach.
    //
    // https://forum.rebol.info/t/1301

      case REB_META_WORD:
        STATE_BYTE(f) = ST_EVALUATOR_META_WORD;
        goto process_get_word;

      case REB_GET_WORD:
        STATE_BYTE(f) = ST_EVALUATOR_GET_WORD;
        goto process_get_word;

      process_get_word:
        assert(
            STATE_BYTE(f) == ST_EVALUATOR_META_WORD
            || STATE_BYTE(f) == ST_EVALUATOR_GET_WORD
        );

        if (not gotten)
            gotten = Lookup_Word_May_Fail(v, v_specifier);

        Copy_Cell(f->out, unwrap(gotten));
        assert(NOT_CELL_FLAG(f->out, UNEVALUATED));

        // !!! All isotopic decay should have already happened.
        // Lookup_Word() should be asserting this!

        if (STATE_BYTE(f) == ST_EVALUATOR_META_WORD)
            Meta_Quotify(f->out);
        else {
            if (IS_BAD_WORD(f->out) and GET_CELL_FLAG(f->out, ISOTOPE))
                fail (Error_Bad_Word_Get(v, f->out));
        }

        STATE_BYTE(f) = ST_EVALUATOR_INITIAL_ENTRY;
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

      case REB_META_GROUP:
        STATE_BYTE(f) = ST_EVALUATOR_META_GROUP;
        goto eval_group;

      case REB_GROUP:
        STATE_BYTE(f) = ST_EVALUATOR_GROUP;
        goto eval_group;

      eval_group: {

        assert(
            STATE_BYTE(f) == ST_EVALUATOR_GROUP
            || STATE_BYTE(f) == ST_EVALUATOR_META_GROUP
        );

        f_next_gotten = nullptr;  // arbitrary code changes fetched variables

        // The IS_BAD_WORD() case here is specifically for REEVAL with invisibles,
        // because it's desirable for `bad-word? reeval :comment "hi" 1` to be
        // 1 and not #[false].  The problem is that REEVAL is not invisible,
        // and hence it wants to make sure something is written to the output
        // so that standard invisibility doesn't kick in...hence it preloads
        // with a non-stale void.
        //
        assert(
            Is_Stale(f->out)
            or Is_Voided(f->out)
            or Is_Invisible(f->out)
            or IS_BAD_WORD(f->out)
        );

        DECLARE_FEED_AT_CORE (subfeed, v, v_specifier);

        if (STATE_BYTE(f) == ST_EVALUATOR_META_GROUP) {
            CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);  // !!! asserts otherwise?
            SET_END(f->out);  // guaranteed to make a value, not transparent
        }
        else {
            // We want to allow *non* ^META groups to be transparent if it's
            // invisible, so instead of using Do_Any_Array_At() we call the
            // lower-level variant and leave what's in f->out there.
        }

      blockscope {
        //
        // We don't use Do_Feed_To_End_Maybe_Stale_Throws() here because we
        // have a rare usage scenario where we are overlapping output and
        // running multiple evaluations, where we know how to interpret the
        // stale flag correctly.  (Most scenarios can't)

        DECLARE_FRAME (
            subframe,
            subfeed,
            EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED
        );

        bool threw;
        Push_Frame(f->out, subframe);
        do {
            threw = Eval_Maybe_Stale_Throws(subframe);
        } while (not threw and NOT_END(subfeed->value));
        Drop_Frame(subframe);
        if (threw)
            goto return_thrown;
      }

        if (STATE_BYTE(f) == ST_EVALUATOR_META_GROUP) {
            //
            // !!! The current concept is that the ^ operators do not conflate
            // pure invisibility with translucency.
            //
            //     >> ^(if false [<a>])
            //     == ~void~  ; isotope
            //
            //     >> ^(comment "hi")
            //     == _
            //
            bool is_invisible_ok = true;
            Clear_Stale_Flag(f->out);
            Reify_Eval_Out_Meta(f->out, is_invisible_ok);
        }
        else {
            // We evaluated a group into the output, but if it was something
            // like (1) the output will hold a non-evaluative 1.  We need to
            // make it clear that this group overhead itself counts as eval.
            //
            // !!! What about `1 ()` ?  It's stale, so does it matter any more
            // if it's evaluative or not?  If we ran the 1 -and- ran the group
            // then we're in an evaluative context anyway.  Just clear always.
            //
            CLEAR_CELL_FLAG(f->out, UNEVALUATED);

            // What was in the group might have been stale from inside the
            // group, e.g.:
            //
            //     (1 + 2 comment "left stale" also [print "nothing on left"])
            //
            // But from outside the group, it's not stale...it's valid input.
            //
            //     (1 + 2 comment "we clear stale") also [print "3 comes in"]
            //
            // !!! How do we catch the case of vaporizing left hand sides?
            //
            //     1 + 2 (comment "stale") + 3
            //
            // Is there a cheaper way to prohibit this that doesn't require
            // writing into the spare cell and seeing if it was stale, and
            // then copying it over?  Is it worth it to prohibit, when this
            // feature was previously considered desirable?
            //
            // We actually don't want to clear the stale flag on end, otherwise
            // it's too easy to make an unintentional "variable assignment
            // skipper"...this is why the resolve is conservative:
            //
            //    >> eval []
            //    == ~void~  ; isotope (safer than something that no-op assigns)
            //
            //    >> ()
            //    == ~void~  ; isotope (we want parity between these two)
            //
            //    >> (maybe if false [<a>])
            //    ; void  (okay, this seems intentional)
            //
        }

        STATE_BYTE(f) = ST_EVALUATOR_INITIAL_ENTRY;
        break; }


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
    // Tuples looking up to BAD-WORD! isotopes are handled consistently with
    // WORD! and GET-WORD!, and will error...directing you use GET/ANY if
    // fetching isotopes is what you actually intended.

      case REB_TUPLE: {
        if (HEART_BYTE(v) == REB_WORD)
            goto process_word;  // special `.` case with hidden word

        const RELVAL *head = VAL_SEQUENCE_AT(f_spare, v, 0);
        if (ANY_INERT(head)) {
            Derelativize(f->out, v, v_specifier);
            break;
        }

        DECLARE_LOCAL (steps);
        if (Get_Var_Core_Throws(f_spare, steps, v, v_specifier)) {
            Move_Cell(f->out, f_spare);
            goto return_thrown;
        }

        if (IS_ACTION(f_spare)) {  // try this branch before fail on void+null
            REBACT *act = VAL_ACTION(f_spare);

            // PATH! dispatch is costly and can error in more ways than WORD!:
            //
            //     e: trap [do make block! ":a"] e.id = 'not-bound
            //                                   ^-- not ready @ lookahead
            //
            // Plus with GROUP!s in a path, their evaluations can't be undone.
            //
            if (GET_ACTION_FLAG(act, ENFIXED))
                fail ("Use `>-` to shove left enfix operands into PATH!s");

            DECLARE_ACTION_SUBFRAME (subframe, f);
            Push_Frame(f->out, subframe);
            Push_Action(
                subframe,
                VAL_ACTION(f_spare),
                VAL_ACTION_BINDING(f_spare)
            );
            Begin_Prefix_Action(subframe, VAL_ACTION_LABEL(f_spare));
            goto process_action;
        }

        if (IS_BAD_WORD(f_spare) and GET_CELL_FLAG(f_spare, ISOTOPE))
            fail (Error_Bad_Word_Get(v, f_spare));

        Move_Cell(f->out, f_spare);  // won't move CELL_FLAG_UNEVALUATED
        break; }


    //=//// PATH! //////////////////////////////////////////////////////////=//
    //
    // Ren-C has moved to member-access model of "dots instead of slashes".
    // So by default, PATH! should only be used for picking refinements on
    // functions.  TUPLE! should be used for picking members out of structures.
    // This has benefits because function dispatch is more complex than the
    // usual PICK process, and being able to say that "slashing" is not
    // methodized the way "dotting" is gives some hope of optimizing it.
    //
    // *BUT* for compatibility with Redbol, there has to be a mode where the
    // paths are effectively turned into TUPLE!s.  This is implemented and
    // controlled by the flag SYSTEM.OPTIONS.REDBOL-PATHS - but it is slow
    // because the method has to actually synthesize a specialized function,
    // instead of pushing refinements to the stack.  (Going through a method
    // call would not allow accrual of data stack elements.)
    //
    // PATH!s starting with inert values do not evaluate.  `/foo/bar` has a
    // blank at its head, and it evaluates to itself.

      case REB_PATH: {
        if (HEART_BYTE(v) != REB_WORD) {
            const RELVAL *temp = VAL_SEQUENCE_AT(f_spare, v, 0);
            if (ANY_INERT(temp)) {
                Derelativize(f->out, v, v_specifier);
                break;
            }
        }

        if (Get_Path_Push_Refinements_Throws(f_spare, f->out, v, v_specifier)) {
            Move_Cell(f->out, f_spare);
            goto return_thrown;
        }

        if (not IS_ACTION(f_spare)) {
            Move_Cell(f->out, f_spare);
            break;
        }

        REBACT *act = VAL_ACTION(f_spare);

        // PATH! dispatch is costly and can error in more ways than WORD!:
        //
        //     e: trap [do make block! ":a"] e.id = 'not-bound
        //                                   ^-- not ready @ lookahead
        //
        // Plus with GROUP!s in a path, their evaluations can't be undone.
        //
        if (GET_ACTION_FLAG(act, ENFIXED))
            fail ("Use `>-` to shove left enfix operands into PATH!s");

        DECLARE_ACTION_SUBFRAME (subframe, f);
        Push_Frame(f->out, subframe);
        Push_Action(
            subframe,
            VAL_ACTION(f_spare),
            VAL_ACTION_BINDING(f_spare)
        );
        Begin_Prefix_Action(subframe, VAL_ACTION_LABEL(f_spare));
        goto process_action; }


    //=//// SET-PATH! /////////////////////////////////////////////////////=//
    //
    // See notes on PATH! for why Ren-C aligns itself with the general idea of
    // "dots instead of slashes" for member selection.  For now, those who
    // try to use SET-PATH! will receive a warning once...then it will set
    // a switch to where it runs the SET-TUPLE! code instead.
    //
    // Future uses of SET-PATH! could do things like verify that the thing
    // being assigned is an ACTION!:
    //
    //     >> foo/: 10
    //     ** Expected an ACTION! but got an INTEGER!
    //
    // Or it might be a way of installing a "getter/setter" function:
    //
    //     >> obj.field/: func [/value] [
    //            either value [print ["Assigning" value]] [print "Getting"]]
    //        ]
    //
    //     >> obj.field: 10
    //     Assigning 10
    //
    // But for the moment, it is just used in Redbol emulation.

      case REB_SET_PATH: {
        REBVAL *redbol = Get_System(SYS_OPTIONS, OPTIONS_REDBOL_PATHS);
        if (not IS_LOGIC(redbol) or VAL_LOGIC(redbol) == false) {
            Derelativize(f->out, v, v_specifier);
            mutable_KIND3Q_BYTE(f->out) = REB_SET_TUPLE;

            Derelativize(f_spare, v, v_specifier);
            rebElide(
                "echo [The SET-PATH!", f_spare, "is no longer the preferred",
                    "way to do member assignments.]",
                "echo [SYSTEM.OPTIONS.REDBOL-PATHS is FALSE, so SET-PATH!",
                    "is not allowed by default.]",
                "echo [For now, we'll enable it automatically...but it",
                    "will slow down the system!]",
                "echo [Please use TUPLE! instead, like", f->out, "]",

                "system.options.redbol-paths: true",
                "wait 3"
            );
        }
        goto set_common; }


    //=//// SET-TUPLE! /////////////////////////////////////////////////////=//
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
    // Isotope and NULL assigns are allowed: https://forum.rebol.info/t/895/4

      set_common:
      case REB_SET_GROUP:
      case REB_SET_TUPLE: {
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, v))
            goto return_thrown;

        if (Is_Voided(f->out)) {  // ^-- also see REB_SET_WORD
            if (Set_Var_Core_Throws(
                f_spare,
                f_spare,
                v,
                v_specifier,
                NONE_ISOTOPE  // was ~void~ isotope or none, can only set none
            )){
                Move_Cell(f->out, f_spare);
                goto return_thrown;
            }
        }
        else if (Is_Stale(f->out)) {
            //
            // When you assign an invisible to an output, it has no effect, and
            // returns the old value of the variable.  See REB_SET_WORD for
            // a writeup of the rationale.

            if (Get_Var_Core_Throws(f->out, f_spare, v, v_specifier)) {
                goto return_thrown;
            }
            assert(NOT_CELL_FLAG(f->out, UNEVALUATED));
        }
        else {
            /*  // !!! Should we figure out how to cache a label in the cell?
            if (IS_ACTION(f->out))
                INIT_VAL_ACTION_LABEL(f->out, VAL_WORD_SYMBOL(v));
            */

            if (Set_Var_Core_Throws(
                f_spare,
                f_spare,
                v,
                v_specifier,
                Pointer_To_Decayed(f->out)
            )){
                Move_Cell(f->out, f_spare);
                goto return_thrown;
            }
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
    // Consistent with GET-WORD!, a GET-PATH! won't allow BAD-WORD! access on
    // the plain (unfriendly) forms.

      case REB_META_PATH:
      case REB_META_TUPLE:
        STATE_BYTE(f) = ST_EVALUATOR_META_PATH_OR_META_TUPLE;
        goto eval_path_or_tuple;

      case REB_GET_PATH:
      case REB_GET_TUPLE:
        STATE_BYTE(f) = ST_EVALUATOR_PATH_OR_TUPLE;
        goto eval_path_or_tuple;

      eval_path_or_tuple:

        assert(
            STATE_BYTE(f) == ST_EVALUATOR_PATH_OR_TUPLE
            or STATE_BYTE(f) == ST_EVALUATOR_META_PATH_OR_META_TUPLE
        );

        if (HEART_BYTE(v) == REB_WORD) {
            assert(VAL_WORD_ID(v) == SYM__SLASH_1_);
            STATE_BYTE(f) = ST_EVALUATOR_GET_WORD;
            goto process_get_word;
        }

        SET_END(f->out);  // !!! Not needed, should there be debug only TRASH()
        if (Get_Var_Core_Throws(f->out, f_spare, v, v_specifier))
            goto return_thrown;

        // !!! This didn't appear to be true for `-- "hi" "hi"`, processing
        // GET-PATH! of a variadic.  Review if it should be true.
        //
        /* assert(NOT_CELL_FLAG(f->out, CELL_FLAG_UNEVALUATED)); */
        CLEAR_CELL_FLAG(f->out, UNEVALUATED);

        if (STATE_BYTE(f) == ST_EVALUATOR_META_PATH_OR_META_TUPLE)
            Meta_Quotify(f->out);
        else {
            if (IS_BAD_WORD(f->out) and GET_CELL_FLAG(f->out, ISOTOPE))
                fail (Error_Bad_Word_Get(v, f->out));
        }

        STATE_BYTE(f) = ST_EVALUATOR_INITIAL_ENTRY;
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
        STATE_BYTE(f) = ST_EVALUATOR_GROUP;
        goto eval_group;


    //=//// GET-BLOCK! ////////////////////////////////////////////////////=//
    //
    // The most useful evaluative operation for GET-BLOCK! was deemed to be
    // a REDUCE.  This does not correspond to what one would think of as an
    // "itemwise get" of a block as GET of BLOCK! acted in historical Rebol.
    // But most usages of that have been superseded by the UNPACK operation.
    //
    // The existence of GET-BLOCK! means that operations like "REPEND" are not
    // necessary, as it's very easy for users to ask for blocks to be reduced.

      case REB_GET_BLOCK: {
        Derelativize(f_spare, v, v_specifier);
        mutable_HEART_BYTE(f_spare) = mutable_KIND3Q_BYTE(f_spare) = REB_BLOCK;
        if (rebRunThrows(f->out, true, Lib(REDUCE), f_spare))
            goto return_thrown;
        break; }


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
    // It supports `_` in slots whose results you don't want to ask for, `#`
    // in slots you want to ask for (but don't want to name), will evaluate
    // GROUP!s, and also allows THE-WORD! to `@circle` which result you want
    // to be the overall result of the expression (defaults to the normal
    // main return value).

      case REB_SET_BLOCK: {
        assert(NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT));

        // As with the other SET-XXX! variations, we don't want to be able to
        // see what's to the left of the assignment in the case of the right
        // hand side vanishing:
        //
        //     >> (10 [x]: comment "we don't want this to be 10")
        //     ** This should be an error.
        //
        SET_END(f->out);

        // We pre-process the SET-BLOCK! first, because we are going to
        // advance the feed in order to build a frame for the following code.
        // It makes more sense for any GROUP!s to be evaluated on the left
        // before on the right, so push the results to the stack.
        //
        // !!! Should the block be locked while the advancement happens?  It
        // wouldn't need to be since everything is on the stack before code
        // is run on the right...but it might reduce confusion.

        if (VAL_LEN_AT(v) == 0)
            fail ("SET-BLOCK! must not be empty for now.");

        REBDSP dsp_circled = 0;  // which pushed address is main return

      blockscope {
        const RELVAL *tail;
        const RELVAL *check = VAL_ARRAY_AT(&tail, v);
        REBSPC *derived = Derive_Specifier(v_specifier, v);
        for (; tail != check; ++check) {
            //
            // THE-XXX! types are used to mark which result should be the
            // overall return of the expression.  But a GROUP! can't resolve
            // to that and make the decision, so handle it up front.
            //
            if (IS_SYMBOL(check) and VAL_SYMBOL(check) == PG_At_Symbol) {
                if (dsp_circled != 0) {
                  too_many_circled:
                    fail ("Can't circle more than one multi-return result");
                }
                REBVAL *let = rebValue("let temp");  // have to fabricate var
                Move_Cell(DS_PUSH(), let);
                rebRelease(let);
                dsp_circled = DSP;
                continue;
            }
            if (
                IS_THE_WORD(check)
                or IS_THE_PATH(check)
                or IS_THE_TUPLE(check)
            ){
                if (dsp_circled != 0)
                    goto too_many_circled;
                Derelativize(DS_PUSH(), check, v_specifier);
                Plainify(DS_TOP);
                dsp_circled = DSP;
                continue;
            }

            // Carets indicate a desire to get a "meta" result.
            //
            // !!! The multi-return mechanism doesn't allow an arbitrary number
            // of meta steps, just one.  Should you be able to say ^(^(x)) or
            // something like that to add more?  :-/
            //
            // !!! Should both @(^) and ^(@) be allowed?
            //
            if (IS_SYMBOL(check) and VAL_SYMBOL(check) == PG_Caret_Symbol) {
                Init_Blackhole(DS_PUSH());
                SET_CELL_FLAG(DS_TOP, STACK_NOTE_METARETURN);
                continue;
            }
            if (
                IS_META_WORD(check)
                or IS_META_PATH(check)
                or IS_META_TUPLE(check)
            ){
                Derelativize(DS_PUSH(), check, v_specifier);
                Plainify(DS_TOP);
                SET_CELL_FLAG(DS_TOP, STACK_NOTE_METARETURN);
                continue;
            }

            const RELVAL *item;
            REBSPC *specifier;
            if (
                IS_GROUP(check)
                or IS_THE_GROUP(check)
                or IS_META_GROUP(check)
            ){
                if (Do_Any_Array_At_Throws(SET_END(f_spare), check, derived)) {
                    Move_Cell(f->out, f_spare);
                    DS_DROP_TO(f->dsp_orig);
                    goto return_thrown;
                }
                item = f_spare;
                specifier = SPECIFIED;
            }
            else {
                item = check;
                specifier = v_specifier;
            }
            if (IS_BLANK(item)) {
                Init_Blank(DS_PUSH());
            }
            else if (Is_Blackhole(item)) {
                //
                // !!! If someone writes `[... @(#) ...]: ...`, then there is
                // a problem if it's not the first slot; as the function needs
                // a variable location to write the result to.  For now, just
                // fabricate a LET variable.
                //
                if (DSP == f->dsp_orig or not IS_THE_GROUP(check))
                    Init_Blackhole(DS_PUSH());
                else {
                    REBVAL *let = rebValue("let temp");
                    assert(IS_WORD(let));
                    Move_Cell(DS_PUSH(), let);
                    rebRelease(let);
                }
            }
            else if (
                IS_WORD(item)
                or IS_PATH(item)
                or IS_TUPLE(item)
            ){
                Derelativize(DS_PUSH(), item, specifier);
            }
            else
                fail ("SET-BLOCK! elements are WORD/PATH/TUPLE/BLANK/ISSUE");

            if (IS_THE_GROUP(check))
                dsp_circled = DSP;
            else if (IS_META_GROUP(check))
                SET_CELL_FLAG(DS_TOP, STACK_NOTE_METARETURN);
        }

        // By default, the ordinary return result will be returned.  Indicate
        // this with dsp_circled = 0, as if no circling were active.  (We had
        // to set it to nonzero in the case of `[@ ...]: ...` to give an error
        // if more than one return were circled.)
        //
        if (dsp_circled == f->dsp_orig + 1)
            dsp_circled = 0;
     }

        // Build a frame for the function call by fulfilling its arguments.
        // The function will be in a state that it can be called, but not
        // invoked yet.
        //
        // !!! This function can currently return a QUOTED! of the next value
        // if it's not an ACTION!; consider that an error for multi-return.
        //
        bool error_on_deferred = false;
        if (Make_Frame_From_Feed_Throws(
            f_spare,
            END_CELL,
            f->feed,
            error_on_deferred
        )){
            Move_Cell(f->out, f_spare);
            DS_DROP_TO(f->dsp_orig);
            goto return_thrown;
        }
        if (not IS_FRAME(f_spare))  // can return QUOTED! if not action atm
            fail ("SET-BLOCK! is only allowed to have ACTION! on right ATM.");

        // Now we want to enumerate through the outputs, and fill them with
        // words/paths/_/# from the data stack.  Note the first slot is set
        // from the "primary" output so it doesn't go in a slot.
        //
        REBDSP dsp_output = f->dsp_orig + 2;

      blockscope {
        REBCTX *c = VAL_CONTEXT(f_spare);
        const REBKEY *key_tail;
        const REBKEY *key = CTX_KEYS(&key_tail, c);
        REBVAR *var = CTX_VARS_HEAD(c);
        const REBPAR *param = ACT_PARAMS_HEAD(CTX_FRAME_ACTION(c));
        for (; key != key_tail; ++key, ++var, ++param) {
            if (dsp_output == DSP + 1)
                break;  // no more outputs requested
            if (Is_Specialized(param))
                continue;
            if (VAL_PARAM_CLASS(param) != PARAM_CLASS_OUTPUT)
                continue;
            if (not IS_BLANK(DS_AT(dsp_output))) {
                Copy_Cell(var, DS_AT(dsp_output));
                Set_Var_May_Fail(var, SPECIFIED, NONE_ISOTOPE);
            }
            ++dsp_output;
        }
      }

        // Now run the frame...
        //
        SET_END(f->out);  // clear prior output to detect ~end~/~void~
        if (Do_Frame_Maybe_Stale_Throws(f->out, f_spare)) {
            DS_DROP_TO(f->dsp_orig);
            goto return_thrown;
        }

        // We called arbitrary code, so we have to toss the cache (in case
        // e.g. ELSE comes next and it got redefined to 3 or something)
        //
        f_next_gotten = nullptr;

        // Now we have to look ahead in case there is enfix code afterward.
        // We want parity, for instance:
        //
        //    >> x: find "abc" 'b then [10]
        //    == 10
        //
        //    >> x
        //    == 10
        //
        //    >> [y]: find "abc" 'b then [10]
        //    == 10
        //
        //    >> y
        //    == 10
        //
        // But at this point we've only run the FIND part, so we'd just have
        // "bc" in the output.  We used `error_on_deferred` as false, so the
        // feed will be in a waiting state for enfix that we can continue by
        // jumping into the evaluator at the ST_EVALUATOR_LOOKING_AHEAD state.
        //
      blockscope {
        REBFLGS flags = EVAL_MASK_DEFAULT
            | FLAG_STATE_BYTE(ST_EVALUATOR_LOOKING_AHEAD)
            | EVAL_FLAG_INERT_OPTIMIZATION;

        DECLARE_FRAME (subframe, f->feed, flags);

        Push_Frame(f->out, subframe);  // offer potential enfix previous f->out
        bool threw = Eval_Maybe_Stale_Throws(subframe);
        Drop_Frame(subframe);

        if (threw) {
            DS_DROP_TO(f->dsp_orig);
            goto return_thrown;
        }
      }

        // Take care of the SET for the main result.  For the moment, if you
        // asked to opt out of the main result this will give you a ~blank~
        // isotope...but there is not currently any way for the invoked
        // routine to be aware that the caller opted out of the return.
        //
        // !!! Could there be a way of a function indicating it was willing
        // to accept RETURN being not requested, somehow?  It would be unsafe
        // having it be NULL as `return` would be a no-op, but if it were an
        // isotope like `~unrequested~` then that could be tested for by
        // a particular kind of routine...
        //
        // !!! Move the main result set part off the stack and into the spare
        // in case there are GROUP! evaluations in the assignment; though that
        // needs more thinking (e.g. what if they throw?)
        //
        Copy_Cell(f_spare, DS_AT(f->dsp_orig + 1));
        if (IS_BLANK(f_spare))
            Init_Isotope(f->out, Canon(BLANK));
        else {
            if (GET_CELL_FLAG(DS_AT(f->dsp_orig + 1), STACK_NOTE_METARETURN)) {
                bool is_invisible_ok = true;
                Reify_Eval_Out_Meta(f->out, is_invisible_ok);

                Set_Var_May_Fail(
                    f_spare, SPECIFIED,
                    f->out
                );
            }
            else if (Is_Voided(f->out)) {
                Set_Var_May_Fail(
                    f_spare, SPECIFIED,
                    NONE_ISOTOPE  // still want to return the ~void~ isotope
                );
            }
            else if (Is_Invisible(f->out)) {
                //
                // We can't assign an invisible state to a variable, so we
                // evaluate to the prior value of it...e.g. the assignment is
                // a no-op.  Note this can only happen with the primary output
                // as you can't set the other multi-return variables to END.
                //
                // !!! If there's a TUPLE! with groups in it, then the push
                // process should capture the steps as a block and pass the
                // steps in here.
                //
                // !!! Edge cases not currently handled with # and _:
                //
                //     >> [#]: comment "What should this do?
                //     == ???
                //
                //     >> [_]: comment "And what about this?"
                //     == ???
                //
                if (Get_Var_Core_Throws(f->out, nullptr, f_spare, SPECIFIED)) {
                    goto return_thrown;
                }
            }
            else {  // ordinary assignment
                Clear_Stale_Flag(f->out);
                Set_Var_May_Fail(
                    f_spare, SPECIFIED,
                    Pointer_To_Decayed(f->out)  // still want to return isotope
                );
            }
        }

        // Iterate the other return slots.  For any variables besides the
        // original marked with meta, then meta them.  And if a return element
        // was "circled" then it becomes the overall return.
        //
        REBDSP dsp = DSP;
        for (; dsp != f->dsp_orig + 1; --dsp) {
            if (
                GET_CELL_FLAG(DS_AT(dsp), STACK_NOTE_METARETURN)
                or dsp_circled == dsp
            ){
                DECLARE_LOCAL (temp);
                PUSH_GC_GUARD(temp);
                Copy_Cell(f_spare, DS_AT(dsp));  // see note on GROUP! eval
                Get_Var_May_Fail(
                    temp,
                    f_spare,
                    SPECIFIED,
                    true  // any
                );
                if (GET_CELL_FLAG(DS_AT(dsp), STACK_NOTE_METARETURN))
                    Meta_Quotify(temp);
                Set_Var_May_Fail(
                    f_spare, SPECIFIED,
                    temp
                );
                if (dsp_circled == dsp)
                    Move_Cell(f->out, temp);
                DROP_GC_GUARD(temp);
            }
        }

        DS_DROP_TO(f->dsp_orig);

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
        f_next_gotten = nullptr;

        break; }


    //=//// META-BLOCK! ////////////////////////////////////////////////////=//
    //
    // Just produces a quoted version of the block it is given:
    //
    //    >> ^[a b c]
    //    == '[a b c]
    //

      case REB_META_BLOCK:
        Inertly_Derelativize_Inheriting_Const(f->out, v, f->feed);
        mutable_KIND3Q_BYTE(f->out) = REB_BLOCK + REB_64;  // quoted
        mutable_HEART_BYTE(f->out) = REB_BLOCK;
        break;


    //=////////////////////////////////////////////////////////////////////=//
    //
    // Treat all the other Is_Bindable() types as inert
    //
    //=////////////////////////////////////////////////////////////////////=//

      case REB_THE_BLOCK:
      case REB_THE_WORD:
      case REB_THE_PATH:
      case REB_THE_TUPLE:
      case REB_THE_GROUP:
        //
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
      case REB_FRAME:
      case REB_MODULE:
      case REB_ERROR:
      case REB_PORT:
        goto inert;


    //=//// BAD-WORD! //////////////////////////////////////////////////////=//
    //
    // Two different forms of BAD-WORD!s can be put into variables.  The plain
    // form can be produced by quoting, and it is safe to fetch via WORD!:
    //
    //     >> foo: '~whatever~
    //     == ~whatever~
    //
    //     >> foo
    //     == ~whatever~
    //
    // The other form arises from evaluation, and are called "isotopes".  If
    // you try to access these from a variable, you will get an error:
    //
    //     >> bar: ~whatever~
    //     == ~whatever~  ; isotope
    //
    //     >> bar
    //     ** Error: bar is a ~whatever~ isotope
    //
    // To bypass the error, use GET/ANY.
    //
    // Note that some special isotopes will "decay" when written to variables,
    // to produce ordinary safe values.

      case REB_BAD_WORD:
        //
        // Source coming through the feed should be guaranteed to not be the
        // isotope form of a BAD-WORD!.  Only objects/frames/etc. have them.
        //
        assert(NOT_CELL_FLAG(v, ISOTOPE));

        if (VAL_BAD_WORD_ID(v) == SYM_VOID) {
            //
            // Doesn't "vanish" but produces a signal of void intent.  If the
            // user wants actual invisibility, a function must be used (the
            // VOID function is included for this purpose)
            //
            Mark_Eval_Out_Voided(f->out);
        }
        else if (VAL_BAD_WORD_ID(v) == SYM_END) {
            //
            // We skip through the rest of the feed when hitting ~end~, and
            // also do not overwrite the result:
            //
            //    3 = do [1 + 2 ~end~ a * <b> c]
            //
            while (NOT_END(f_next))
                Fetch_Next_Forget_Lookback(f);

            // !!! This needs more thought, because as written it means that
            // `x: ~end~` and `x: void` act the same.  Evaluation steps need
            // to be taken to determine endness... not testing f->feed->value
            // for END at the beginning of each step.  It's a big rework that
            // will have to be done at some point to implement this feature.
        }
        else {
            // All other isotopes can be said to actually "exist", but some
            // of them decay automatically in variable assignment...such as
            // ~null~, ~none~, ~false~, ~blank~, and ~blackhole~ isotopes.
            //
            Derelativize(f->out, v, v_specifier);
            SET_CELL_FLAG(f->out, ISOTOPE);
        }
        break;


    //=///////////////////////////////////////////////////////////////////=//
    //
    // Treat all the other NOT Is_Bindable() types as inert
    //
    //=///////////////////////////////////////////////////////////////////=//

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
        Derelativize(f->out, v, v_specifier);
        Unquotify(f->out, 1);  // take off one level of quoting
        if (IS_BAD_WORD(f->out))
            assert(NOT_CELL_FLAG(f->out, ISOTOPE));
        break;


    //=//// QUOTED! (at 3 levels of escaping or less...or garbage) ////////=//
    //
    // All the values for types at >= REB_64 currently represent the special
    // compact form of literals, which overlay inside the cell they escape.
    // The real type comes from the type modulo 64.

      default:
        Derelativize(f->out, v, v_specifier);
        Unquotify_In_Situ(f->out, 1);  // checks for illegal REB_XXX bytes

        // Note: the evaluator decays a lone quote to plain null:
        //
        //    >> '
        //    ; null
        //
        // So you don't get "null isotopes" from dequoting, only via the
        // BAD-WORD! evaluation:
        //
        //   >> ~null~
        //   == ~null~  ; isotope
        //
        if (IS_BAD_WORD(f->out))
            assert(NOT_CELL_FLAG(f->out, ISOTOPE));
        break;
    }

  //=//// END MAIN SWITCH STATEMENT ///////////////////////////////////////=//

    // The UNEVALUATED flag is one of the bits that doesn't get copied by
    // Copy_Cell() or Derelativize().  Hence it can be overkill to clear it
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
    // a word is used to do it, like `>-` in `x: >- lib.method [...] [...]`.

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
        if (VAL_WORD_SYMBOL(f_next) != PG_Slash_1_Canon)
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
        f_next_gotten = Lookup_Word(f_next, FEED_SPECIFIER(f->feed));
    else {
        if (f_next_gotten != Lookup_Word(f_next, FEED_SPECIFIER(f->feed)))
            assert(false);
        assert(f_next_gotten == Lookup_Word(f_next, FEED_SPECIFIER(f->feed)));
    }

  //=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-ENFIX ///////////=//

    // These cases represent finding the start of a new expression.
    //
    // Fall back on word-like "dispatch" even if ->gotten is null (unset or
    // unbound word).  It'll be an error, but that code path raises it for us.

    if (
        not f_next_gotten
        or not IS_ACTION(unwrap(f_next_gotten))
        or NOT_ACTION_FLAG(VAL_ACTION(unwrap(f_next_gotten)), ENFIXED)
    ){
      lookback_quote_too_late: // run as if starting new expression

        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

        // Since it's a new expression, EVALUATE doesn't want to run it
        // even if invisible, as it's not completely invisible (enfixed)
        //
        goto finished;
    }

  //=//// IS WORD ENFIXEDLY TIED TO A FUNCTION (MAY BE "INVISIBLE") ///////=//

  blockscope {
    REBACT *enfixed = VAL_ACTION(unwrap(f_next_gotten));

    if (GET_ACTION_FLAG(enfixed, QUOTES_FIRST)) {
        //
        // Left-quoting by enfix needs to be done in the lookahead before an
        // evaluation, not this one that's after.  This happens in cases like:
        //
        //     left-the: enfix func [:value] [:value]
        //     the <something> left-the
        //
        // But due to the existence of <end>-able and <skip>-able parameters,
        // the left quoting function might be okay with seeing nothing on the
        // left.  Start a new expression and let it error if that's not ok.
        //
        assert(NOT_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH));
        if (GET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH))
            fail (Error_Literal_Left_Path_Raw());

        const REBPAR *first = First_Unspecialized_Param(nullptr, enfixed);
        if (VAL_PARAM_CLASS(first) == PARAM_CLASS_SOFT) {
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
        and not (GET_ACTION_FLAG(enfixed, DEFERS_LOOKBACK)
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
            GET_ACTION_FLAG(enfixed, POSTPONES_ENTIRELY)
            or (
                GET_ACTION_FLAG(enfixed, DEFERS_LOOKBACK)
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

    DECLARE_ACTION_SUBFRAME (subframe, f);
    Push_Frame(f->out, subframe);
    Push_Action(subframe, enfixed, VAL_ACTION_BINDING(unwrap(f_next_gotten)));
    Begin_Enfix_Action(subframe, VAL_WORD_SYMBOL(f_next));

    Fetch_Next_Forget_Lookback(f);  // advances next

    SET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);

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
    //     left-the: enfix :the
    //     o: make object! [f: does [1]]
    //     o.f left-the  ; want error suggesting >- here, need flag for that
    //
    CLEAR_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH);
    assert(NOT_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT));  // must be consumed

  #if !defined(NDEBUG)
    Eval_Core_Exit_Checks_Debug(f);  // called unless a fail() longjmps
    assert(NOT_EVAL_FLAG(f, DOING_PICKUPS));
    assert(
        (f->flags.bits & ~FLAG_STATE_BYTE(255)) == initial_flags
    );  // any change should be restored
  #endif

    return false;  // false => not thrown
}
