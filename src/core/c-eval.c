//
//  File: %c-eval.c
//  Summary: "Central Interpreter Evaluator"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
#define kind_current KIND_BYTE_UNCHECKED(v)


//=//// ARGUMENT LOOP MODES ///////////////////////////////////////////////=//
//
// f->special is kept in sync with one of three possibilities:
//
// * f->param to indicate ordinary argument fulfillment for all the relevant
//   args, refinements, and refinement args of the function.
//
// * f->arg, to indicate that the arguments should only be type-checked.
//
// * some other pointer to an array of REBVAL which is the same length as the
//   argument list.  Any non-null values in that array should be used in lieu
//   of an ordinary argument...e.g. that argument has been "specialized".
//
// All the states can be incremented across the length of the frame.  This
// means `++f->special` can be done without checking for null values.
//
// Additionally, in the f->param state, f->special will never register as
// anything other than a parameter.  This can speed up some checks, such as
// where `IS_NULLED(f->special)` can only match the other two cases.
//
// Done with macros for speed in the debug build (which does not inline).
// The name of the trigger condition is included since reinforcing what's true
// at the callsite is good to help understand the state.

#define SPECIAL_IS_ARG_SO_TYPECHECKING \
    (f->special == f->arg)

#define SPECIAL_IS_PARAM_SO_UNSPECIALIZED \
    (f->special == f->param)

#define SPECIAL_IS_ARBITRARY_SO_SPECIALIZED \
    (f->special != f->param and f->special != f->arg)


// It's called "Finalize" because in addition to checking, any other handling
// that an argument needs once being put into a frame is handled.  VARARGS!,
// for instance, that may come from an APPLY need to have their linkage
// updated to the parameter they are now being used in.
//
inline static void Finalize_Arg(REBFRM *f) {
    assert(not Is_Param_Variadic(f->param)); // Use Finalize_Variadic_Arg()

    REBYTE kind_byte = KIND_BYTE(f->arg);

    if (kind_byte == REB_0_END) {
        //
        // Note: `1 + comment "foo"` => `1 +`, arg is END
        //
        if (not Is_Param_Endable(f->param))
            fail (Error_No_Arg(f, f->param));

        Init_Endish_Nulled(f->arg);
        SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
        return;
    }

  #if defined(DEBUG_STALE_ARGS) // see notes on flag definition
    assert(NOT_CELL_FLAG(f->arg, ARG_MARKED_CHECKED));
  #endif

    if (
        kind_byte == REB_BLANK
        and TYPE_CHECK(f->param, REB_TS_NOOP_IF_BLANK) // e.g. <blank> param
    ){
        SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
        SET_EVAL_FLAG(f, FULFILL_ONLY);
        return;
    }

    // If we're not just typechecking, apply constness if requested.
    //
    // !!! Should explicit mutability override, so people can say things like
    // `foo: func [...] mutable [...]` ?  This seems bad, because the contract
    // of the function hasn't been "tweaked", e.g. with reskinning.
    //
    if (not SPECIAL_IS_ARG_SO_TYPECHECKING)
        if (TYPE_CHECK(f->param, REB_TS_CONST))
            SET_CELL_FLAG(f->arg, CONST);

    // If the <dequote> tag was used on an argument, we want to remove the
    // quotes (and queue them to be added back in if the return was marked
    // with <requote>).
    //
    if (TYPE_CHECK(f->param, REB_TS_DEQUOTE_REQUOTE) and IS_QUOTED(f->arg)) {
        if (GET_EVAL_FLAG(f, FULFILL_ONLY)) {
            //
            // We can only take the quote levels off now if the function is
            // going to be run now.  Because if we are filling a frame to
            // reuse later, it would forget the f->dequotes count.
            //
            if (not TYPE_CHECK(f->param, CELL_KIND(VAL_UNESCAPED(f->arg))))
                fail (Error_Arg_Type(f, f->param, VAL_TYPE(f->arg)));

            SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
            return;
        }

        // Some routines want to requote but also want to be able to
        // return a null without turning it into a single apostrophe.
        // Use the heuristic that if the argument wasn't legally null,
        // then a returned null should duck the requote.
        //
        f->requotes += VAL_NUM_QUOTES(f->arg);
        if (CELL_KIND(VAL_UNESCAPED(f->arg)) == REB_NULL)
            SET_EVAL_FLAG(f, REQUOTE_NULL);

        Dequotify(f->arg);
    }

    if (TYPE_CHECK(f->param, REB_TS_REFINEMENT)) {
        Typecheck_Refinement_And_Canonize(f->param, f->arg);
        return;
    }

    if (not Typecheck_Including_Constraints(f->param, f->arg)) {
        fail (Error_Arg_Type(f, f->param, VAL_TYPE(f->arg)));
    }

    SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
}


// While "checking" the variadic argument we actually re-stamp it with
// this parameter and frame's signature.  It reuses whatever the original
// data feed was (this frame, another frame, or just an array from MAKE
// VARARGS!)
//
inline static void Finalize_Variadic_Arg_Core(REBFRM *f, bool enfix) {
    assert(Is_Param_Variadic(f->param)); // use Finalize_Arg()

    // Varargs are odd, because the type checking doesn't actually check the
    // types inside the parameter--it always has to be a VARARGS!.
    //
    if (not IS_VARARGS(f->arg))
        fail (Error_Not_Varargs(f, f->param, VAL_TYPE(f->arg)));

    // Store the offset so that both the arg and param locations can quickly
    // be recovered, while using only a single slot in the REBVAL.  But make
    // the sign denote whether the parameter was enfixed or not.
    //
    VAL_VARARGS_SIGNED_PARAM_INDEX(f->arg) =
        enfix
            ? -(f->arg - FRM_ARGS_HEAD(f) + 1)
            : f->arg - FRM_ARGS_HEAD(f) + 1;

    VAL_VARARGS_PHASE_NODE(f->arg) = NOD(FRM_PHASE(f));
    SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
}

#define Finalize_Variadic_Arg(f) \
    Finalize_Variadic_Arg_Core((f), false)

#define Finalize_Enfix_Variadic_Arg(f) \
    Finalize_Variadic_Arg_Core((f), true)


#ifdef DEBUG_EXPIRED_LOOKBACK
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (f->feed->stress != nullptr)
#else
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (v == &f->feed->lookback)
#endif


inline static void Expire_Out_Cell_Unless_Invisible(REBFRM *f) {
    REBACT *phase = FRM_PHASE(f);
    if (GET_ACTION_FLAG(phase, IS_INVISIBLE)) {
        if (NOT_ACTION_FLAG(f->original, IS_INVISIBLE))
            fail ("All invisible action phases must be invisible");
        return;
    }

    if (GET_ACTION_FLAG(f->original, IS_INVISIBLE))
        return;

  #ifdef DEBUG_UNREADABLE_VOIDS
    //
    // The f->out slot should be initialized well enough for GC safety.
    // But in the debug build, if we're not running an invisible function
    // set it to END here, to make sure the non-invisible function writes
    // *something* to the output.
    //
    // END has an advantage because recycle/torture will catch cases of
    // evaluating into movable memory.  But if END is always set, natives
    // might *assume* it.  Fuzz it with unreadable voids.
    //
    // !!! Should natives be able to count on f->out being END?  This was
    // at one time the case, but this code was in one instance.
    //
    if (NOT_ACTION_FLAG(FRM_PHASE(f), IS_INVISIBLE)) {
        if (SPORADICALLY(2))
            Init_Unreadable_Void(f->out);
        else
            SET_END(f->out);
        SET_CELL_FLAG(f->out, OUT_MARKED_STALE);
    }
  #endif
}


// When arguments are hard quoted or soft-quoted, they don't call into the
// evaluator to do it.  But they need to use the logic of the evaluator for
// noticing when to defer enfix:
//
//     foo: func [...] [
//          return lit 1 then ["this needs to be returned"]
//     ]
//
// If the first time the THEN was seen was not after the 1, but when the
// LIT ran, it would get deferred until after the RETURN.  This is not
// consistent with the pattern people expect.
//
// Returns TRUE if it set the flag.
//
bool Lookahead_To_Sync_Enfix_Defer_Flag(struct Reb_Feed *feed) {
    assert(NOT_FEED_FLAG(feed, DEFERRING_ENFIX));
    assert(not feed->gotten);

    CLEAR_FEED_FLAG(feed, NO_LOOKAHEAD);

    if (not IS_WORD(feed->value))
        return false;

    feed->gotten = Try_Lookup_Word(feed->value, feed->specifier);

    if (not feed->gotten or not IS_ACTION(feed->gotten))
        return false;

    if (NOT_ACTION_FLAG(VAL_ACTION(feed->gotten), ENFIXED))
        return false;

    if (GET_ACTION_FLAG(VAL_ACTION(feed->gotten), DEFERS_LOOKBACK))
        SET_FEED_FLAG(feed, DEFERRING_ENFIX);
    return true;
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
    if (GET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT))  { // e.g. `10 -> x:`
        CLEAR_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);
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

    if (CURRENT_CHANGES_IF_FETCH_NEXT) { // must use new frame
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
        EVAL_FLAG_POST_SWITCH
        | EVAL_FLAG_PROCESS_ACTION
        | EVAL_FLAG_REEVALUATE_CELL
        | EVAL_FLAG_NEXT_ARG_FROM_OUT
        | EVAL_FLAG_FULFILL_ONLY  // can be requested or <blank> can trigger
        | EVAL_FLAG_RUNNING_ENFIX  // can be requested with REEVALUATE_CELL
        | EVAL_FLAG_TOOK_HOLD  // can be set by va_list reification
    );  // should be unchanged on exit
  #endif

    assert(DSP >= f->dsp_orig);  // REDUCE accrues, APPLY adds refinements
    assert(not IS_TRASH_DEBUG(f->out));  // all invisible will preserve output
    assert(f->out != f_spare);  // overwritten by temporary calculations
    assert(NOT_FEED_FLAG(f->feed, BARRIER_HIT));

    const RELVAL *v;  // shorthand for the value we are switch()-ing on
    TRASH_POINTER_IF_DEBUG(v);

    const REBVAL *gotten;
    TRASH_POINTER_IF_DEBUG(gotten);

    // Given how the evaluator is written, it's inevitable that there will
    // have to be a test for points to `goto` before running normal eval.
    // This cost is paid on every entry to Eval_Core().
    //
    // Trying alternatives (such as a synthetic REB_XXX type to signal it,
    // to fold along in a switch) seem to only make it slower.  Using flags
    // and testing them together as a group seems the fastest option.
    //
    if (f->flags.bits & (
        EVAL_FLAG_POST_SWITCH
        | EVAL_FLAG_PROCESS_ACTION
        | EVAL_FLAG_REEVALUATE_CELL
    )){
        if (GET_EVAL_FLAG(f, POST_SWITCH)) {
            CLEAR_EVAL_FLAG(f, POST_SWITCH); // !!! necessary?
            goto post_switch;
        }

        if (GET_EVAL_FLAG(f, PROCESS_ACTION)) {
            CLEAR_EVAL_FLAG(f, PROCESS_ACTION);

            SET_CELL_FLAG(f->out, OUT_MARKED_STALE); // !!! necessary?
            goto process_action;
        }

        CLEAR_EVAL_FLAG(f, REEVALUATE_CELL);

        // The re-evaluate functionality may not want to heed the enfix state
        // in the action itself.  See REBNATIVE(shove)'s /ENFIX for instance.
        // So we go by the state of EVAL_FLAG_RUNNING_ENFIX on entry.
        //
        if (GET_EVAL_FLAG(f, RUNNING_ENFIX)) {
            CLEAR_EVAL_FLAG(f, RUNNING_ENFIX);  // for assertion
            Push_Action(
                f,
                VAL_ACTION(f->u.reval.value),
                VAL_BINDING(f->u.reval.value)
            );
            Begin_Enfix_Action(f, nullptr);  // invisible cache NO_LOOKAHEAD

            Fetch_Next_Forget_Lookback(f);

            goto process_action;
        }

        v = f->u.reval.value;
        gotten = nullptr;
        goto reevaluate;
    }

  #if !defined(NDEBUG)
    Eval_Core_Expression_Checks_Debug(f);
    assert(NOT_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH));
    if (NOT_EVAL_FLAG(f, FULFILLING_ARG))
        assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD));
    assert(NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX));
  #endif

//=//// START NEW EXPRESSION //////////////////////////////////////////////=//

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

    assert(NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT));
    SET_CELL_FLAG(f->out, OUT_MARKED_STALE);  // internal use flag only

    UPDATE_EXPRESSION_START(f); // !!! See FRM_INDEX() for caveats

    // If asked to evaluate `[]` then we have now done all the work the
    // evaluator needs to do--including marking the output stale.
    //
    // See DEBUG_ENSURE_FRAME_EVALUATES for why an empty array does not
    // bypass calling into the evaluator.
    //
    if (KIND_BYTE(f_next) == REB_0_END)
        goto finished;

    gotten = f_next_gotten;
    v = Lookback_While_Fetching_Next(f);
    // ^-- can't just `v = f_next`, fetch may overwrite--request lookback!

  reevaluate: ;  // meaningful semicolon--subsequent macro may declare things

    // ^-- doesn't advance expression index: `reeval x` starts with `reeval`

//=//// LOOKAHEAD FOR ENFIXED FUNCTIONS THAT QUOTE THEIR LEFT ARG /////////=//

    // Ren-C has an additional lookahead step *before* an evaluation in order
    // to take care of this scenario.  To do this, it pre-emptively feeds the
    // frame one unit that f->value is the *next* value, and a local variable
    // called "current" holds the current head of the expression that the
    // main switch would process.

    UPDATE_TICK_DEBUG(v);

    // v-- This is the TG_Break_At_Tick or C-DEBUG-BREAK landing spot --v

    if (KIND_BYTE(f_next) != REB_WORD) // right's kind - END would be REB_0
        goto give_up_backward_quote_priority;

    assert(not f_next_gotten);  // Fetch_Next_In_Frame() cleared it
    f_next_gotten = Try_Lookup_Word(f_next, f_specifier);

    if (not f_next_gotten or not IS_ACTION(f_next_gotten))
        goto give_up_backward_quote_priority;  // note only ACTION! is ENFIXED

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
            goto give_up_backward_quote_priority; // yield as an exemption
        }
    }

    // Let the <skip> flag allow the right hand side to gracefully decline
    // interest in the left hand side due to type.  This is how DEFAULT works,
    // such that `case [condition [...] default [...]]` does not interfere
    // with the BLOCK! on the left, but `x: default [...]` gets the SET-WORD!
    //
    if (GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), SKIPPABLE_FIRST)) {
        REBVAL *first = First_Unspecialized_Param(VAL_ACTION(f_next_gotten));
        if (not TYPE_CHECK(first, kind_current)) // left's kind
            goto give_up_backward_quote_priority;
    }

    // Lookback args are fetched from f->out, then copied into an arg
    // slot.  Put the backwards quoted value into f->out.
    //
    Derelativize(f->out, v, f_specifier); // for NEXT_ARG_FROM_OUT
    SET_CELL_FLAG(f->out, UNEVALUATED); // so lookback knows it was quoted

    // We skip over the word that invoked the action (e.g. ->-, OF, =>).
    // v will then hold a pointer to that word (possibly now resident in the
    // frame spare).  (f->out holds what was the left)
    //
    gotten = f_next_gotten;
    v = Lookback_While_Fetching_Next(f);

    if (
        IS_END(f_next)  // v-- out is what used to be on left
        and (KIND_BYTE(f->out) == REB_WORD or KIND_BYTE(f->out) == REB_PATH)
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

        SET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH); // for better error message
        SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT); // literal right op is arg

        goto give_up_backward_quote_priority; // run PATH!/WORD! normal
    }

    // Wasn't the at-end exception, so run normal enfix with right winning.

    Push_Action(f, VAL_ACTION(gotten), VAL_BINDING(gotten));
    Begin_Enfix_Action(f, VAL_WORD_SPELLING(v));

    goto process_action;

  give_up_backward_quote_priority:

//=//// BEGIN MAIN SWITCH STATEMENT ///////////////////////////////////////=//

    // This switch is done with a case for all REB_XXX values, in order to
    // facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
    //
    // Subverting the jump table optimization with specialized branches for
    // fast tests like ANY_INERT() and IS_NULLED_OR_VOID_OR_END() has shown
    // to reduce performance in practice.  The compiler does the right thing.

    switch (KIND_BYTE(v)) {  // checked version (done once, else kind_current)

      case REB_0_END:
        goto finished;


//==//// NULL ////////////////////////////////////////////////////////////==//
//
// Since nulled cells can't be in BLOCK!s, the evaluator shouldn't usually see
// them.  Plus Q APIs quotes spliced values, so `rebValueQ("null?", nullptr)`
// gets a QUOTED! that evaluates to null--it's not a null being evaluated.
//
// But plain `rebValue("null?", nullptr)` would be an error.  Another way
// the evaluator can see NULL is REEVAL, such as `reeval first []`.

      case REB_NULL:
        fail (Error_Evaluate_Null_Raw());


//==//// VOID! ///////////////////////////////////////////////////////////==//
//
// "A void! is a means of giving a hot potato back that is a warning about
//  something, but you don't want to force an error 'in the moment'...in case
//  the returned information wasn't going to be used anyway."
//
// https://forum.rebol.info/t/947
//
// If we get here, the evaluator is actually seeing it, and it's time to fail.

      case REB_VOID:
        fail (Error_Void_Evaluation_Raw());


//==//// ACTION! /////////////////////////////////////////////////////////==//
//
// If an action makes it to the SWITCH statement, that means it is either
// literally an action value in the array (`do compose [1 (:+) 2]`) or is
// being retriggered via EVAL.
//
// Most action evaluations are triggered from a WORD! or PATH!, which jumps in
// at the `process_action` label.

      case REB_ACTION: {
        Push_Action(f, VAL_ACTION(v), VAL_BINDING(v));
        Begin_Prefix_Action(f, VAL_ACTION_LABEL(v));  // may cache label

        // We'd like `10 -> = 5 + 5` to work, and to do so it reevaluates in
        // a new frame, but has to run the `=` as "getting its next arg from
        // the output slot, but not being run in an enfix mode".
        //
        if (NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT))
            Expire_Out_Cell_Unless_Invisible(f);

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

      process_action: // Note: Also jumped to by the redo_checked code

      #if !defined(NDEBUG)
        assert(f->original);  // set by Begin_Action()
        Do_Process_Action_Checks_Debug(f);
      #endif

        assert(DSP >= f->dsp_orig); // path processing may push REFINEMENT!s

        TRASH_POINTER_IF_DEBUG(v); // shouldn't be used below
        TRASH_POINTER_IF_DEBUG(gotten);

        assert(NOT_EVAL_FLAG(f, DOING_PICKUPS));

        for (; NOT_END(f->param); ++f->param, ++f->arg, ++f->special) {

    //=//// CONTINUES (AT TOP SO GOTOS DO NOT CROSS INITIALIZATIONS ///////=//

            goto loop_body;  // optimized out

          continue_arg_loop:

            assert(GET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED));

            if (GET_EVAL_FLAG(f, DOING_PICKUPS)) {
                if (DSP != f->dsp_orig)
                    goto next_pickup;

                f->param = END_NODE;  // don't need f->param in paramlist
                goto arg_loop_and_any_pickups_done;
            }
            continue;

          skip_this_arg_for_now:  // the GC marks args up through f->arg...

            Init_Unreadable_Void(f->arg);  // ...so cell must have valid bits
            continue;

    //=//// ACTUAL LOOP BODY //////////////////////////////////////////////=//

          loop_body:

            // !!! If not an APPLY or a typecheck of existing values, the data
            // array which backs the frame may not have any initialization of
            // its bits.  The goal is to make it so that the GC uses the
            // f->param position to know how far the frame fulfillment is
            // gotten, and only mark those values.  This just blindly formats
            // them with NODE_FLAG_STACK to make the arg initialization work.
            //
            if (
                NOT_EVAL_FLAG(f, DOING_PICKUPS)
                and not SPECIAL_IS_ARG_SO_TYPECHECKING
            ){
                Prep_Cell(f->arg);  // improve...
            }


    //=//// A /REFINEMENT ARG /////////////////////////////////////////////=//

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
            // If the refinements can be popped off the stack in the order
            // that they are encountered, then this can be done in one pass.
            // Otherwise a second pass is needed.  But it is accelerated by
            // storing the parameter indices to revisit in the binding of the
            // words (e.g. /B and /C above) on the data stack.

            if (TYPE_CHECK(f->param, REB_TS_REFINEMENT)) {
                assert(NOT_EVAL_FLAG(f, DOING_PICKUPS));  // jump lower

                if (SPECIAL_IS_PARAM_SO_UNSPECIALIZED)  // args from callsite
                    goto unspecialized_refinement;  // most common case (?)

                if (SPECIAL_IS_ARG_SO_TYPECHECKING) {
                    if (NOT_CELL_FLAG(f->arg, ARG_MARKED_CHECKED))
                        Typecheck_Refinement_And_Canonize(f->param, f->arg);
                    goto continue_arg_loop;
                }

                // A specialization....

                if (GET_CELL_FLAG(f->special, ARG_MARKED_CHECKED)) {
                    Move_Value(f->arg, f->special);
                    SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                    goto continue_arg_loop;  // !!! Double-check?
                }

                // A non-checked SYM-WORD! with binding indicates a partial
                // refinement with parameter index that needs to be pushed
                // to top of stack, hence HIGHER priority for fulfilling
                // @ the callsite than any refinements added by a PATH!.
                //
                if (IS_SYM_WORD(f->special)) {
                    REBLEN partial_index = VAL_WORD_INDEX(f->special);
                    REBSTR *partial_canon = VAL_STORED_CANON(f->special);

                    Init_Sym_Word(DS_PUSH(), partial_canon);
                    INIT_BINDING(DS_TOP, f->varlist);
                    INIT_WORD_INDEX(DS_TOP, partial_index);
                }
                else
                    assert(IS_NULLED(f->special));

    //=//// UNSPECIALIZED REFINEMENT SLOT /////////////////////////////////=//

        // We want to fulfill all normal parameters before any refinements
        // that take arguments.  Ren-C allows normal parameters *after* any
        // refinement, that are not "refinement arguments".  So a refinement
        // that takes an argument must always fulfill using "pickups".

              unspecialized_refinement: {

                REBVAL *ordered = DS_TOP;  // v-- #2258
                const REBSTR *param_canon = VAL_PARAM_CANON(f->param);

                for (; ordered != DS_AT(f->dsp_orig); --ordered) {
                    if (VAL_STORED_CANON(ordered) != param_canon)
                        continue;

                    REBLEN offset = f->arg - FRM_ARGS_HEAD(f);
                    INIT_BINDING(ordered, f->varlist);
                    INIT_WORD_INDEX(ordered, offset + 1);

                    if (Is_Typeset_Invisible(f->param)) {
                        //
                        // There's no argument, so we won't need to come back
                        // for this one.  But we did need to set its index
                        // so we knew it was valid (errors later if not set).
                        //
                        goto used_refinement;
                    }

                    goto skip_this_arg_for_now;
                }

                goto unused_refinement; }  // not in path, not specialized yet

              unused_refinement:  // Note: might get pushed by a later slot

                Init_Nulled(f->arg);
                SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;

              used_refinement:  // can hit this on redo, copy its argument

                if (f->special == f->arg) {
                    /* type checking */
                }
                else {
                    Refinify(Init_Word(f->arg, VAL_PARAM_SPELLING(f->param)));
                }
                SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;
            }

    //=//// "PURE" LOCAL: ARG /////////////////////////////////////////////=//

            // This takes care of locals, including "magic" RETURN cells that
            // need to be pre-filled.  !!! Note nuances with compositions:
            //
            // https://github.com/metaeducation/ren-c/issues/823

        fulfill_arg: ;  // semicolon needed--next statement is declaration

            Reb_Param_Class pclass = VAL_PARAM_CLASS(f->param);

            switch (pclass) {
              case REB_P_LOCAL:
                //
                // When REDOing a function frame, it is sent back up to do
                // SPECIAL_IS_ARG_SO_TYPECHECKING, and the check takes care
                // of clearing the locals, they may not be null...
                //
                if (SPECIAL_IS_ARBITRARY_SO_SPECIALIZED)
                    assert(IS_NULLED(f->special) or IS_VOID(f->special));

                Init_Void(f->arg);
                SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;

              default:
                break;
            }

            if (GET_CELL_FLAG(f->special, ARG_MARKED_CHECKED)) {

    //=//// SPECIALIZED OR OTHERWISE TYPECHECKED ARG //////////////////////=//

                if (not SPECIAL_IS_ARG_SO_TYPECHECKING) {
                    assert(SPECIAL_IS_ARBITRARY_SO_SPECIALIZED);

                    // Specializing with VARARGS! is generally not a good
                    // idea unless that is an empty varargs...because each
                    // call will consume from it.  Specializations you use
                    // only once might make sense (?)
                    //
                    assert(
                        not Is_Param_Variadic(f->param)
                        or IS_VARARGS(f->special)
                    );

                    Move_Value(f->arg, f->special); // won't copy the bit
                    SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                }

                if (
                    TYPE_CHECK(f->param, REB_TS_DEQUOTE_REQUOTE)
                    and IS_QUOTED(f->arg)
                    and NOT_EVAL_FLAG(f, FULFILL_ONLY)
                ){
                    f->requotes += VAL_NUM_QUOTES(f->arg);
                    Dequotify(f->arg);
                }

                // The flag's whole purpose is that it's not set if the type
                // is invalid (excluding the narrow purpose of slipping types
                // used for partial specialization into refinement slots).
                // But this isn't a refinement slot.  Double check it's true.
                //
                // Note SPECIALIZE checks types at specialization time, to
                // save us the time of doing it on each call.  Also note that
                // NULL is not technically in the valid argument types for
                // refinement arguments, but is legal in fulfilled frames.
                //
                assert(Typecheck_Including_Constraints(f->param, f->arg));

                goto continue_arg_loop;
            }

            // !!! This is currently a hack for APPLY.  It doesn't do a type
            // checking pass after filling the frame, but it still wants to
            // treat all values (nulls included) as fully specialized.
            //
            if (
                SPECIAL_IS_ARG_SO_TYPECHECKING  // !!! ever allow gathering?
                /* GET_EVAL_FLAG(f, FULLY_SPECIALIZED) */
            ){
                if (Is_Param_Variadic(f->param))
                    Finalize_Variadic_Arg(f);
                else
                    Finalize_Arg(f);
                goto continue_arg_loop; // looping to verify args/refines
            }

    //=//// HANDLE IF NEXT ARG IS IN OUT SLOT (e.g. ENFIX, CHAIN) /////////=//

            if (GET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT)) {
                CLEAR_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);

                if (GET_CELL_FLAG(f->out, OUT_MARKED_STALE)) {
                    //
                    // Something like `lib/help left-lit` is allowed to work,
                    // but if it were just `obj/int-value left-lit` then the
                    // path evaluation won...but LEFT-LIT still gets run.
                    // It appears it has nothing to its left, but since we
                    // remembered what happened we can give an informative
                    // error message vs. a perplexing one.
                    //
                    if (GET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH))
                        fail (Error_Literal_Left_Path_Raw());

                    // Seeing an END in the output slot could mean that there
                    // was really "nothing" to the left, or it could be a
                    // consequence of a frame being in an argument gathering
                    // mode, e.g. the `+` here will perceive "nothing":
                    //
                    //     if + 2 [...]
                    //
                    // If an enfixed function finds it has a variadic in its
                    // first slot, then nothing available on the left is o.k.
                    // It means we have to put a VARARGS! in that argument
                    // slot which will react with TRUE to TAIL?, so feed it
                    // from the global empty array.
                    //
                    if (Is_Param_Variadic(f->param)) {
                        RESET_CELL(f->arg, REB_VARARGS, CELL_MASK_VARARGS);
                        INIT_BINDING(f->arg, EMPTY_ARRAY); // feed finished

                        Finalize_Enfix_Variadic_Arg(f);
                        goto continue_arg_loop;
                    }

                    // The OUT_MARKED_STALE flag is also used by BAR! to keep
                    // a result in f->out, so that the barrier doesn't destroy
                    // data in cases like `(1 + 2 | comment "hi")` => 3, but
                    // left enfix should treat that just like an end.

                    SET_END(f->arg);
                    Finalize_Arg(f);
                    goto continue_arg_loop;
                }

                if (Is_Param_Variadic(f->param)) {
                    //
                    // Stow unevaluated cell into an array-form variadic, so
                    // the user can do 0 or 1 TAKEs of it.
                    //
                    // !!! It be evaluated when they TAKE (it if it's an
                    // evaluative arg), but not if they don't.  Should failing
                    // to TAKE be seen as an error?  Failing to take first
                    // gives out-of-order evaluation.
                    //
                    assert(NOT_END(f->out));
                    REBARR *array1;
                    if (IS_END(f->out))
                        array1 = EMPTY_ARRAY;
                    else {
                        REBARR *feed = Alloc_Singular(NODE_FLAG_MANAGED);
                        Move_Value(ARR_SINGLE(feed), f->out);

                        array1 = Alloc_Singular(NODE_FLAG_MANAGED);
                        Init_Block(ARR_SINGLE(array1), feed); // index 0
                    }

                    RESET_CELL(f->arg, REB_VARARGS, CELL_MASK_VARARGS);
                    INIT_BINDING(f->arg, array1);
                    Finalize_Enfix_Variadic_Arg(f);
                }
                else switch (pclass) {
                  case REB_P_NORMAL:
                  enfix_normal_handling:

                    Move_Value(f->arg, f->out);
                    if (GET_CELL_FLAG(f->out, UNEVALUATED))
                        SET_CELL_FLAG(f->arg, UNEVALUATED);

                    Finalize_Arg(f);
                    break;

                  case REB_P_HARD_QUOTE:
                    if (not GET_CELL_FLAG(f->out, UNEVALUATED)) {
                        //
                        // This can happen e.g. with `x: 10 | x >- lit`.  We
                        // raise an error in this case, while still allowing
                        // `10 >- lit` to work, so people don't have to go
                        // out of their way rethinking operators if it could
                        // just work out for inert types.
                        //
                        fail (Error_Evaluative_Quote_Raw());
                    }

                    // Is_Param_Skippable() accounted for in pre-lookback

                    Move_Value(f->arg, f->out);
                    SET_CELL_FLAG(f->arg, UNEVALUATED);
                    Finalize_Arg(f);
                    break;

                  case REB_P_MODAL: {
                    if (not GET_CELL_FLAG(f->out, UNEVALUATED))
                        goto enfix_normal_handling;

                  handle_modal_in_out:

                    switch (VAL_TYPE(f->out)) {
                      case REB_SYM_WORD:
                      case REB_SYM_PATH:
                        Getify(f->out);  // don't run @append or @append/only
                        goto enable_modal;

                      case REB_SYM_GROUP:
                      case REB_SYM_BLOCK:
                        Plainify(f->out);  // run GROUP!, pass block as-is
                        goto enable_modal;

                      default:
                        goto skip_enable_modal;
                    }

                  enable_modal: {
                    //
                    // !!! We could (should?) pre-check the paramlists to
                    // make sure users don't try and make a modal argument
                    // not followed by a refinement.  That would cost
                    // extra, but avoid the test on every call.
                    //
                    const RELVAL *enable = f->param + 1;
                    if (
                        IS_END(enable)
                        or not TYPE_CHECK(enable, REB_TS_REFINEMENT)
                    ){
                        fail ("Refinement must follow modal parameter");
                    }
                    if (not Is_Typeset_Invisible(enable))
                        fail ("Modal refinement cannot take arguments");

                    // Signal refinement as being in use.
                    //
                    Init_Sym_Word(DS_PUSH(), VAL_PARAM_CANON(enable));
                  }

                  skip_enable_modal:
                    //
                    // Because the possibility of needing to see the uneval'd
                    // value existed, the parameter had to act quoted.  Eval.
                    //
                    if (Eval_Value_Throws(f->arg, f->out, SPECIFIED)) {
                        Move_Value(f->arg, f->out);
                        goto abort_action;
                    }

                    Finalize_Arg(f);
                    break; }

                  case REB_P_SOFT_QUOTE:
                    //
                    // Note: This permits f->out to not carry the UNEVALUATED
                    // flag--enfixed operations which have evaluations on
                    // their left are treated as if they were in a GROUP!.
                    // This is important to `1 + 2 ->- lib/* 3` being 9, while
                    // also allowing `1 + x: ->- lib/default [...]` to work.

                    if (IS_QUOTABLY_SOFT(f->out)) {
                        if (Eval_Value_Throws(f->arg, f->out, SPECIFIED)) {
                            Move_Value(f->out, f->arg);
                            goto abort_action;
                        }
                    }
                    else {
                        Move_Value(f->arg, f->out);
                        SET_CELL_FLAG(f->arg, UNEVALUATED);
                    }
                    Finalize_Arg(f);
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
                if (GET_EVAL_FLAG(f, RUNNING_ENFIX)) {
                    assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD));
                    if (
                        NOT_ACTION_FLAG(FRM_PHASE(f), POSTPONES_ENTIRELY)
                        and
                        NOT_ACTION_FLAG(FRM_PHASE(f), DEFERS_LOOKBACK)
                    ){
                        SET_FEED_FLAG(f->feed, NO_LOOKAHEAD);
                    }
                }

                Expire_Out_Cell_Unless_Invisible(f);

                goto continue_arg_loop;
            }

    //=//// NON-ENFIX VARIADIC ARG (doesn't consume anything *yet*) ///////=//

            // Evaluation argument "hook" parameters (marked in MAKE ACTION!
            // by a `[[]]` in the spec, and in FUNC by `<variadic>`).  They point
            // back to this call through a reified FRAME!, and are able to
            // consume additional arguments during the function run.
            //
            if (Is_Param_Variadic(f->param)) {
                RESET_CELL(f->arg, REB_VARARGS, CELL_MASK_VARARGS);
                INIT_BINDING(f->arg, f->varlist); // frame-based VARARGS!

                Finalize_Variadic_Arg(f);
                goto continue_arg_loop;
            }

    //=//// AFTER THIS, PARAMS CONSUME FROM CALLSITE IF NOT APPLY /////////=//

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
            if (NOT_EVAL_FLAG(f, RUNNING_ENFIX))
                CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

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
            if (GET_FEED_FLAG(f->feed, DEFERRING_ENFIX))
                fail (Error_Ambiguous_Infix_Raw());

    //=//// ERROR ON END MARKER, BAR! IF APPLICABLE ///////////////////////=//

            if (IS_END(f_next) or GET_FEED_FLAG(f->feed, BARRIER_HIT)) {
                if (not Is_Param_Endable(f->param))
                    fail (Error_No_Arg(f, f->param));

                Init_Endish_Nulled(f->arg);
                SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;
            }

            switch (pclass) {

   //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes 1 EVALUATE's worth) ////=//

              case REB_P_NORMAL:
              normal_handling: {
                REBFLGS flags = EVAL_MASK_DEFAULT
                    | EVAL_FLAG_FULFILLING_ARG;

                if (IS_VOID(f_next))  // Eval_Step() has callers test this
                    fail (Error_Void_Evaluation_Raw());  // must be quoted

                if (Eval_Step_In_Subframe_Throws(f->arg, f, flags)) {
                    Move_Value(f->out, f->arg);
                    goto abort_action;
                }
                break; }

    //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG /////////////////////////////=//

              case REB_P_HARD_QUOTE:
                if (not Is_Param_Skippable(f->param))
                    Literal_Next_In_Frame(f->arg, f); // CELL_FLAG_UNEVALUATED
                else {
                    if (not Typecheck_Including_Constraints(f->param, f_next)) {
                        assert(Is_Param_Endable(f->param));
                        Init_Endish_Nulled(f->arg); // not EVAL_FLAG_BARRIER_HIT
                        SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                        goto continue_arg_loop;
                    }
                    Literal_Next_In_Frame(f->arg, f);
                    SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                    SET_CELL_FLAG(f->arg, UNEVALUATED);
                }

                // Have to account for enfix deferrals in cases like:
                //
                //     return lit 1 then (x => [x + 1])
                //
                Lookahead_To_Sync_Enfix_Defer_Flag(f->feed);

                if (GET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED))
                    goto continue_arg_loop;

                break;

    //=//// MODAL ARG  ////////////////////////////////////////////////////=//

            case REB_P_MODAL: {
                if (not ANY_SYM_KIND(VAL_TYPE(f_next)))  // not an @xxx
                    goto normal_handling;  // acquire as a regular argument

                Literal_Next_In_Frame(f->out, f);  // f->value is read-only...
                goto handle_modal_in_out; }  // ...in out so we can Unsymify()

    //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  ////////////////////////////=//

        // Quotes from the right already "win" over quotes from the left, in
        // a case like `help left-quoter` where they point at teach other.
        // But there's also an issue where something sits between quoting
        // constructs like the `[x]` in between the `else` and `->`:
        //
        //     if condition [...] else [x] -> [...]
        //
        // Here the neutral [x] is meant to be a left argument to the lambda,
        // producing the effect of:
        //
        //     if condition [...] else ([x] -> [...])
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

              case REB_P_SOFT_QUOTE:
                Literal_Next_In_Frame(f->arg, f);  // CELL_FLAG_UNEVALUATED

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
                    Lookahead_To_Sync_Enfix_Defer_Flag(f->feed) and
                    GET_ACTION_FLAG(VAL_ACTION(f->feed->gotten), QUOTES_FIRST)
                ){
                    // We need to defer and let the right hand quote that is
                    // quoting leftward win.  We use the EVAL_FLAG_POST_SWITCH
                    // flag to jump into a subframe where subframe->out is
                    // the f->arg, and it knows to get the arg from there.

                    REBFLGS flags = EVAL_MASK_DEFAULT
                        | EVAL_FLAG_FULFILLING_ARG
                        | EVAL_FLAG_POST_SWITCH
                        | EVAL_FLAG_INERT_OPTIMIZATION;

                    if (IS_VOID(f_next))  // Eval_Step() has callers test this
                        fail (Error_Void_Evaluation_Raw());  // must be quoted

                    DECLARE_FRAME (subframe, f->feed, flags);

                    Push_Frame(f->arg, subframe);
                    bool threw = Eval_Throws(subframe);
                    Drop_Frame(subframe);

                    if (threw) {
                        Move_Value(f->out, f->arg);
                        goto abort_action;
                    }
                }
                else if (IS_QUOTABLY_SOFT(f->arg)) {
                    //
                    // We did not defer the quoted argument.  If the argument
                    // is something like a GROUP!, GET-WORD!, or GET-PATH!...
                    // it has to be evaluated.
                    //
                    Move_Value(f_spare, f->arg);
                    if (Eval_Value_Throws(f->arg, f_spare, f_specifier)) {
                        Move_Value(f->out, f->arg);
                        goto abort_action;
                    }
                }
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
            assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD));

    //=//// TYPE CHECKING FOR (MOST) ARGS AT END OF ARG LOOP //////////////=//

            // Some arguments can be fulfilled and skip type checking or
            // take care of it themselves.  But normal args pass through
            // this code which checks the typeset and also handles it when
            // a void arg signals the revocation of a refinement usage.

            assert(pclass != REB_P_LOCAL);
            assert(
                not SPECIAL_IS_ARG_SO_TYPECHECKING  // was handled, unless...
                or NOT_EVAL_FLAG(f, FULLY_SPECIALIZED)  // ...this!
            );

            // !!! In GCC 9.3.0-10 at -O2 optimization level in the C++ build
            // this Finalize_Arg() call seems to trigger:
            //
            //   error: array subscript 2 is outside array bounds
            //      of 'const char [9]'
            //
            // It points to the problem being at VAL_STRING_AT()'s line:
            //
            //     const REBSTR *s = VAL_STRING(v);
            //
            // There was no indication that this Finalize_Arg() was involved,
            // but commenting it out makes the complaint go away.  Attempts
            // to further isolate it down were made by deleting and inlining
            // bits of code until one low-level line would trigger it.  This
            // led to seemingly unrelated declaration of an unused byte
            // variable being able to cause it or not.  It may be a compiler
            // optimization bug...in any cae, that warning is disabled for
            // now on this file.  Review.
            //
            Finalize_Arg(f);
            goto continue_arg_loop;
        }

        assert(IS_END(f->arg)); // arg can otherwise point to any arg cell

        // There may have been refinements that were skipped because the
        // order of definition did not match the order of usage.  They were
        // left on the stack with a pointer to the `param` and `arg` after
        // them for later fulfillment.
        //
        // Note that there may be functions on the stack if this is the
        // second time through, and we were just jumping up to check the
        // parameters in response to a R_REDO_CHECKED; if so, skip this.
        //
        if (DSP != f->dsp_orig and IS_SYM_WORD(DS_TOP)) {

          next_pickup:

            assert(IS_SYM_WORD(DS_TOP));

            if (not IS_WORD_BOUND(DS_TOP)) { // the loop didn't index it
                mutable_KIND_BYTE(DS_TOP) = REB_WORD;
                mutable_MIRROR_BYTE(DS_TOP) = REB_WORD;
                fail (Error_Bad_Refine_Raw(DS_TOP)); // so duplicate or junk
            }

            // FRM_ARGS_HEAD offsets are 0-based, while index is 1-based.
            // But +1 is okay, because we want the slots after the refinement.
            //
            REBINT offset =
                VAL_WORD_INDEX(DS_TOP) - (f->arg - FRM_ARGS_HEAD(f)) - 1;
            f->param += offset;
            f->arg += offset;
            f->special += offset;

            assert(VAL_STORED_CANON(DS_TOP) == VAL_PARAM_CANON(f->param));
            assert(TYPE_CHECK(f->param, REB_TS_REFINEMENT));
            DS_DROP();

            if (Is_Typeset_Invisible(f->param)) {  /* no callsite arg, just drop */
                if (DSP != f->dsp_orig)
                    goto next_pickup;

                f->param = END_NODE; // don't need f->param in paramlist
                goto arg_loop_and_any_pickups_done;
            }

            assert(IS_UNREADABLE_DEBUG(f->arg) or IS_NULLED(f->arg));
            SET_EVAL_FLAG(f, DOING_PICKUPS);
            goto fulfill_arg;
        }

      arg_loop_and_any_pickups_done:

        CLEAR_EVAL_FLAG(f, DOING_PICKUPS);  // reevaluate may set flag again
        assert(IS_END(f->param));  // signals !Is_Action_Frame_Fulfilling()

    //==////////////////////////////////////////////////////////////////==//
    //
    // ACTION! ARGUMENTS NOW GATHERED, DISPATCH PHASE
    //
    //==////////////////////////////////////////////////////////////////==//

        if (GET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT)) {
            if (GET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH))
                fail (Error_Literal_Left_Path_Raw());
        }

      redo_unchecked:

        // This happens if you have something intending to act as enfix but
        // that does not consume arguments, e.g. `x: enfixed func [] []`.
        // An enfixed function with no arguments might sound dumb, but that
        // can be useful as a convenience way of saying "takes the left hand
        // argument but ignores it" (e.g. with skippable args).  Allow it.
        //
        if (GET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT)) {
            assert(GET_EVAL_FLAG(f, RUNNING_ENFIX));
            CLEAR_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);
        }

        assert(IS_END(f->param));
        assert(
            IS_END(f_next)
            or FRM_IS_VARIADIC(f)
            or IS_VALUE_IN_ARRAY_DEBUG(f->feed->array, f_next)
        );

        if (GET_EVAL_FLAG(f, FULFILL_ONLY)) {
            Init_Nulled(f->out);
            goto skip_output_check;
        }

        Expire_Out_Cell_Unless_Invisible(f);

        f_next_gotten = nullptr; // arbitrary code changes fetched variables

        // Note that the dispatcher may push ACTION! values to the data stack
        // which are used to process the return result after the switch.
        //
      blockscope {
        REBNAT dispatcher = ACT_DISPATCHER(FRM_PHASE(f));

        const REBVAL *r = (*dispatcher)(f);

        if (r == f->out) {
            assert(NOT_CELL_FLAG(f->out, OUT_MARKED_STALE));
            CLEAR_CELL_FLAG(f->out, UNEVALUATED);  // other cases Move_Value()
        }
        else if (not r) { // API and internal code can both return `nullptr`
            Init_Nulled(f->out);
        }
        else if (GET_CELL_FLAG(r, ROOT)) { // API, from Alloc_Value()
            Handle_Api_Dispatcher_Result(f, r);
        }
        else switch (KIND_BYTE(r)) { // it's a "pseudotype" instruction
            //
            // !!! Thrown values used to be indicated with a bit on the value
            // itself, but now it's conveyed through a return value.  This
            // means typical return values don't have to run through a test
            // for if they're thrown or not, but it means Eval_Core has to
            // return a boolean to pass up the state.  It may not be much of
            // a performance win either way, but recovering the bit in the
            // values is a definite advantage--as header bits are scarce!
            //
          case REB_R_THROWN: {
            const REBVAL *label = VAL_THROWN_LABEL(f->out);
            if (IS_ACTION(label)) {
                if (
                    VAL_ACTION(label) == NATIVE_ACT(unwind)
                    and VAL_BINDING(label) == NOD(f->varlist)
                ){
                    // Eval_Core catches unwinds to the current frame, so throws
                    // where the "/name" is the JUMP native with a binding to
                    // this frame, and the thrown value is the return code.
                    //
                    // !!! This might be a little more natural if the name of
                    // the throw was a FRAME! value.  But that also would mean
                    // throws named by frames couldn't be taken advantage by
                    // the user for other features, while this only takes one
                    // function away.
                    //
                    CATCH_THROWN(f->out, f->out);
                    goto dispatch_completed;
                }
                else if (
                    VAL_ACTION(label) == NATIVE_ACT(redo)
                    and VAL_BINDING(label) == NOD(f->varlist)
                ){
                    // This was issued by REDO, and should be a FRAME! with
                    // the phase and binding we are to resume with.
                    //
                    CATCH_THROWN(f->out, f->out);
                    assert(IS_FRAME(f->out));

                    // !!! We are reusing the frame and may be jumping to an
                    // "earlier phase" of a composite function, or even to
                    // a "not-even-earlier-just-compatible" phase of another
                    // function.  Type checking is necessary, as is zeroing
                    // out any locals...but if we're jumping to any higher
                    // or different phase we need to reset the specialization
                    // values as well.
                    //
                    // Since dispatchers run arbitrary code to pick how (and
                    // if) they want to change the phase on each redo, we
                    // have no easy way to tell if a phase is "earlier" or
                    // "later".  The only thing we have is if it's the same
                    // we know we couldn't have touched the specialized args
                    // (no binding to them) so no need to fill those slots
                    // in via the exemplar.  Otherwise, we have to use the
                    // exemplar of the phase.
                    //
                    // REDO is a fairly esoteric feature to start with, and
                    // REDO of a frame phase that isn't the running one even
                    // more esoteric, with REDO/OTHER being *extremely*
                    // esoteric.  So having a fourth state of how to handle
                    // f->special (in addition to the three described above)
                    // seems like more branching in the baseline argument
                    // loop.  Hence, do a pre-pass here to fill in just the
                    // specializations and leave everything else alone.
                    //
                    REBCTX *exemplar;
                    if (
                        FRM_PHASE(f) != VAL_PHASE(f->out)
                        and did (exemplar = ACT_EXEMPLAR(VAL_PHASE(f->out)))
                    ){
                        f->special = CTX_VARS_HEAD(exemplar);
                        f->arg = FRM_ARGS_HEAD(f);
                        for (; NOT_END(f->arg); ++f->arg, ++f->special) {
                            if (IS_NULLED(f->special)) // no specialization
                                continue;
                            Move_Value(f->arg, f->special); // reset it
                        }
                    }

                    INIT_FRM_PHASE(f, VAL_PHASE(f->out));
                    FRM_BINDING(f) = VAL_BINDING(f->out);
                    goto redo_checked;
                }
            }

            // Stay THROWN and let stack levels above try and catch
            //
            goto abort_action; }

          case REB_R_REDO:
            //
            // This instruction represents the idea that it is desired to
            // run the f->phase again.  The dispatcher may have changed the
            // value of what f->phase is, for instance.

            if (not EXTRA(Any, r).flag) // R_REDO_UNCHECKED
                goto redo_unchecked;

          redo_checked:  // R_REDO_CHECKED

            Expire_Out_Cell_Unless_Invisible(f);

            f->param = ACT_PARAMS_HEAD(FRM_PHASE(f));
            f->arg = FRM_ARGS_HEAD(f);
            f->special = f->arg;

            goto process_action;

          case REB_R_INVISIBLE: {
            assert(GET_ACTION_FLAG(FRM_PHASE(f), IS_INVISIBLE));

            if (NOT_SERIES_INFO(f->varlist, TELEGRAPH_NO_LOOKAHEAD))
                CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
            else {
                SET_FEED_FLAG(f->feed, NO_LOOKAHEAD);
                CLEAR_SERIES_INFO(f->varlist, TELEGRAPH_NO_LOOKAHEAD);
            }

            // !!! Ideally we would check that f->out hadn't changed, but
            // that would require saving the old value somewhere...

            // If a "good" output is in `f->out`, the invisible should have
            // had no effect on it.  So jump to the position after output
            // would be checked by a normal function.
            //
            if (NOT_CELL_FLAG(f->out, OUT_MARKED_STALE) or IS_END(f_next)) {
                //
                // Note: could be an END that is not "stale", example:
                //
                //     is-barrier?: func [x [<end> integer!]] [null? x]
                //     is-barrier? (<| 10)
                //
                goto skip_output_check;
            }

            // If the evaluation is being called by something like EVALUATE,
            // they may want to see the next value literally.  Refer to this
            // explanation:
            //
            // https://forum.rebol.info/t/1173/4
            //
            // But argument evaluation isn't customizable at that level, and
            // wants all the invisibles processed.  So only do one-at-a-time
            // invisibles if we're not fulfilling arguments.
            //
            if (NOT_EVAL_FLAG(f, FULFILLING_ARG))
                goto skip_output_check;

            // Note that we do not do START_NEW_EXPRESSION() here when an
            // invisible is being processed as part of an argument.  They
            // all get lumped into one step.
            //
            // !!! Review debugging implications.

            gotten = f_next_gotten;
            v = Lookback_While_Fetching_Next(f);

            Drop_Action(f);
            goto reevaluate; }

          default:
            assert(!"Invalid pseudotype returned from action dispatcher");
        }
      }

      dispatch_completed:

    //==////////////////////////////////////////////////////////////////==//
    //
    // ACTION! CALL COMPLETION
    //
    //==////////////////////////////////////////////////////////////////==//

        // Here we know the function finished and nothing threw past it or
        // FAIL / fail()'d.  It should still be in REB_ACTION evaluation
        // type, and overwritten the f->out with a non-thrown value.  If the
        // function composition is a CHAIN, the chained functions are still
        // pending on the stack to be run.

      #if !defined(NDEBUG)
        Do_After_Action_Checks_Debug(f);
      #endif

      skip_output_check:

        // If we have functions pending to run on the outputs (e.g. this was
        // the result of a CHAIN) we can run those chained functions in the
        // same REBFRM, for efficiency.
        //
        while (DSP != f->dsp_orig) {
            //
            // We want to keep the label that the function was invoked with,
            // because the other phases in the chain are implementation
            // details...and if there's an error, it should still show the
            // name the user invoked the function with.  But we have to drop
            // the action args, as the paramlist is likely be completely
            // incompatible with this next chain step.
            //
            Drop_Action(f);
            Push_Action(f, VAL_ACTION(DS_TOP), VAL_BINDING(DS_TOP));

            // We use the same mechanism as enfix operations do...give the
            // next chain step its first argument coming from f->out
            //
            // !!! One side effect of this is that unless CHAIN is changed
            // to check, your chains can consume more than one argument.
            // This might be interesting or it might be bugs waiting to
            // happen, trying it out of curiosity for now.
            //
            Begin_Prefix_Action(f, VAL_ACTION_LABEL(DS_TOP));
            assert(NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT));
            SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);

            DS_DROP();

            goto process_action;
        }

        // We assume that null return results don't count for the requoting,
        // unless the dequoting was explicitly of a quoted null parameter.
        // Just a heuristic--if it doesn't work for someone, they'll have to
        // take QUOTED! themselves and do whatever specific logic they need.
        //
        if (GET_ACTION_FLAG(f->original, RETURN_REQUOTES)) {
            if (
                KIND_BYTE_UNCHECKED(f->out) != REB_NULL
                or GET_EVAL_FLAG(f, REQUOTE_NULL)
            ){
                Quotify(f->out, f->requotes);
            }
        }

        Drop_Action(f);
        break;


//==//// WORD! ///////////////////////////////////////////////////////////==//
//
// A plain word tries to fetch its value through its binding.  It will fail
// and longjmp out of this stack if the word is unbound (or if the binding is
// to a variable which is not set).  Should the word look up to an action,
// then that action will be called by jumping to the ACTION! case.
//
// NOTE: The usual dispatch of enfix functions is *not* via a REB_WORD in this
// switch, it's by some code at the `post_switch:` label.  So you only see
// enfix in cases like `(+ 1 2)`, or after PARAMLIST_IS_INVISIBLE e.g.
// `10 comment "hi" + 20`.

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

            Push_Action(f, act, VAL_BINDING(gotten));
            Begin_Action_Core(
                f,
                VAL_WORD_SPELLING(v),  // use word as label
                GET_ACTION_FLAG(act, ENFIXED)
            );
            goto process_action;
        }

        if (IS_VOID(gotten))  // need GET/ANY if it's void ("undefined")
            fail (Error_Need_Non_Void_Core(v, f_specifier));

        Move_Value(f->out, gotten);  // no copy CELL_FLAG_UNEVALUATED
        break;


//==//// SET-WORD! ///////////////////////////////////////////////////////==//
//
// Right hand side is evaluated into `out`, and then copied to the variable.
//
// All values are allowed in these assignments, including NULL and VOID!
// https://forum.rebol.info/t/1206

      process_set_word:
      case REB_SET_WORD: {
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, v))  // see notes
            goto return_thrown;

      set_word_with_out:

        Move_Value(Sink_Word_May_Fail(v, f_specifier), f->out);
        break; }


//==//// GET-WORD! ///////////////////////////////////////////////////////==//
//
// A GET-WORD! does no dispatch on functions.  It will fetch other values as
// normal, but will error on VOID! and direct you to GET/ANY.  This matches
// Rebol2 behavior, choosing to break with R3-Alpha and Red which will give
// back "voided" values ("UNSET!")...to make typos less likely to bite those
// who wanted to use ACTION!s inertly:
// https://forum.rebol.info/t/should-get-word-of-a-void-raise-an-error/1301

      process_get_word:
      case REB_GET_WORD:
        if (not gotten)
            gotten = Lookup_Word_May_Fail(v, f_specifier);

        if (IS_VOID(gotten))
            fail (Error_Need_Non_Void_Core(v, f_specifier));

        Move_Value(f->out, gotten);

        if (IS_ACTION(gotten))  // cache the word's label in the cell
            INIT_ACTION_LABEL(f->out, VAL_WORD_SPELLING(v));
        break;


//==//// GROUP! ///////////////////////////////////////////////////////////=//
//
// If a GROUP! is seen then it generates another call into Eval_Core().  The
// current frame is not reused, as the source array from which values are
// being gathered changes.
//
// Empty groups vaporize, as do ones that only consist of invisibles.  If
// this is not desired, one should use DO or lead with `(void ...)`
//
//     >> 1 + 2 (comment "vaporize")
//     == 3
//
//     >> 1 + () 2
//     == 3

      case REB_GROUP:
      eval_group: {
        f_next_gotten = nullptr;  // arbitrary code changes fetched variables

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
            goto reevaluate;
        }

        CLEAR_CELL_FLAG(f->out, UNEVALUATED);  // `(1)` considered evaluative
        break; }


//==//// PATH! and TUPLE! ////////////////////////////////////////////////==//
//
// Paths starting with inert values do not evaluate.  `/foo/bar` has a blank
// at its head, and it evaluates to itself.
//
// Other paths run through the GET-PATH! mechanism and then EVAL the result.
// If the get of the path is null, then it will be an error.

      case REB_PATH:
      case REB_TUPLE: {
        if (MIRROR_BYTE(v) == REB_WORD)
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

        REBVAL *where = GET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT) ? f_spare : f->out;

        if (Eval_Path_Throws_Core(
            where,
            v,  // !!! may not be array-based
            f_specifier,
            nullptr, // `setval`: null means don't treat as SET-PATH!
            EVAL_FLAG_PUSH_PATH_REFINES
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

            // !!! Review if invisibles can be supported without ->
            //
            if (GET_ACTION_FLAG(act, IS_INVISIBLE))
                fail ("Use `<-` with invisibles fetched from PATH!");

            Push_Action(f, VAL_ACTION(where), VAL_BINDING(where));
            Begin_Prefix_Action(f, VAL_ACTION_LABEL(where));

            if (where == f->out)
                Expire_Out_Cell_Unless_Invisible(f);

            goto process_action;
        }

        if (IS_VOID(where))  // need `:x/y` if it's void (unset)
            fail (Error_Need_Non_Void_Core(v, f_specifier));

        if (where != f->out)
            Move_Value(f->out, where);  // won't move CELL_FLAG_UNEVALUATED
        else
            CLEAR_CELL_FLAG(f->out, UNEVALUATED);
        break; }


//==//// SET-PATH! and SET-TUPLE! ////////////////////////////////////////==//
//
// See notes on SET-WORD!  SET-PATH!s are handled in a similar way, by
// pushing them to the stack, continuing the evaluation via a lightweight
// reuse of the current frame.
//
// !!! The evaluation ordering is dictated by the fact that there isn't a
// separate "evaluate path to target location" and "set target' step.  This
// is because some targets of assignments (e.g. gob/size/x:) do not correspond
// to a cell that can be returned; the path operation "encodes as it goes"
// and requires the value to set as a parameter to Eval_Path.  Yet it is
// counterintuitive given the "left-to-right" nature of the language:
//
//     >> foo: make object! [[bar][bar: 10]]
//
//     >> foo/(print "left" 'bar): (print "right" 20)
//     right
//     left
//     == 20
//
// Note that nulled cells are allowed: https://forum.rebol.info/t/895/4

      case REB_SET_PATH:
      case REB_SET_TUPLE: {
        if (MIRROR_BYTE(v) == REB_WORD) {
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


//==//// GET-PATH! and GET-TUPLE! ////////////////////////////////////////==//
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
// Consistent with GET-WORD!, a GET-PATH! acts as GET and won't return VOID!.

      case REB_GET_PATH:
      case REB_GET_TUPLE:
        if (MIRROR_BYTE(v) == REB_WORD) {
            assert(VAL_WORD_SYM(v) == SYM__SLASH_1_);
            goto process_get_word;
        }

        if (Get_Path_Throws_Core(f->out, v, f_specifier))
            goto return_thrown;

        if (IS_VOID(f->out))  // need GET/ANY if it's void ("undefined")
            fail (Error_Need_Non_Void_Core(v, f_specifier));

        // !!! This didn't appear to be true for `-- "hi" "hi"`, processing
        // GET-PATH! of a variadic.  Review if it should be true.
        //
        /* assert(NOT_CELL_FLAG(f->out, CELL_FLAG_UNEVALUATED)); */
        CLEAR_CELL_FLAG(f->out, UNEVALUATED);
        break;


//==//// GET-GROUP! //////////////////////////////////////////////////////==//
//
// This was initially conceived such that `:(x)` was a shorthand for the
// expression `get x`.  But that's already pretty short--and arguably a
// cleaner way of saying the same thing.  So instead, it's given the same
// meaning in the evaluator as plain GROUP!...which seems wasteful on the
// surface, but it means dialects can be free to use it to make a distinction.
// Branches use it to mean "do not voidify the result".

      case REB_GET_GROUP:
        goto eval_group;


//==//// SET-GROUP! //////////////////////////////////////////////////////==//
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
            // Apply the function, and we can reuse this frame to do it.
            //
            // !!! But really it should not be allowed to take more than one
            // argument.  Hence rather than go through reevaluate, channel
            // it through a variant of the enfix machinery (the way that
            // CHAIN does, which similarly reuses the frame but probably
            // should also be restricted to a single value...though it's
            // being experimented with letting it take more.)
            //
            Push_Action(f, VAL_ACTION(f_spare), VAL_BINDING(f_spare));
            Begin_Prefix_Action(f, nullptr);  // no label

            assert(NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT));
            SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);

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


//==//// GET-BLOCK! //////////////////////////////////////////////////////==//
//
// !!! Currently just inert, awaiting future usage.

      case REB_GET_BLOCK:
        Derelativize(f->out, v, f_specifier);
        break;


//==//// SET-BLOCK! //////////////////////////////////////////////////////==//
//
// The evaluator treats SET-BLOCK! specially as a means for implementing
// multiple return values.  The trick is that it does so by pre-loading
// arguments in the frame with variables to update, in a way that could have
// historically been achieved with passing a WORD! or PATH! to a refinement.
// So if there was a function that updates a variable you pass in by name:
//
//     result: updating-function/update arg1 arg2 'var
//
// The /UPDATE parameter is marked as being effectively a "return value", so
// that equivalent behavior can be achieved with:
//
//     [result var]: updating-function arg1 arg2
//
// !!! This is an extremely slow-running prototype of the desired behavior.
// It is a mock up intended to find any flaws in the concept before writing
// a faster native version that would require rewiring the evaluator somewhat.

      case REB_SET_BLOCK: {
        assert(NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT));

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
        mutable_KIND_BYTE(f->out) = REB_BLOCK;
        mutable_MIRROR_BYTE(f->out) = REB_BLOCK;

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
                        "set f/(output) void",
                    "]",
                "]",
                "either first", f->out, "[",
                    "set first", f->out, "do f",
                "] [do f]",
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

        goto reevaluate; }


//==//////////////////////////////////////////////////////////////////////==//
//
// Treat all the other Is_Bindable() types as inert
//
//==//////////////////////////////////////////////////////////////////////==//

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


//==//////////////////////////////////////////////////////////////////////==//
//
// Treat all the other not Is_Bindable() types as inert
//
//==//////////////////////////////////////////////////////////////////////==//

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


//=//// QUOTED! (at 4 or more levels of escaping) /////////////////////////=//
//
// This is the form of literal that's too escaped to just overlay in the cell
// by using a higher kind byte.  See the `default:` case in this switch for
// handling of the more compact forms, that are much more common.
//
// (Highly escaped literals should be rare, but for completeness you need to
// be able to escape any value, including any escaped one...!)

      case REB_QUOTED:
        Derelativize(f->out, v, f_specifier);
        Unquotify(f->out, 1);  // take off one level of quoting
        break;


//==//// QUOTED! (at 3 levels of escaping or less...or just garbage) //////=//
//
// All the values for types at >= REB_64 currently represent the special
// compact form of literals, which overlay inside the cell they escape.
// The real type comes from the type modulo 64.

      default:
        Derelativize(f->out, v, f_specifier);
        Unquotify_In_Situ(f->out, 1);  // checks for illegal REB_XXX bytes
        break;
    }


//=//// END MAIN SWITCH STATEMENT /////////////////////////////////////////=//

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
    // Slightly more nuanced is why PARAMLIST_IS_INVISIBLE functions have to
    // be considered in the lookahead also.  Consider this case:
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

  post_switch:

    // If something was run with the expectation it should take the next arg
    // from the output cell, and an evaluation cycle ran that wasn't an
    // ACTION! (or that was an arity-0 action), that's not what was meant.
    // But it can happen, e.g. `x: 10 | x ->-`, where ->- doesn't get an
    // opportunity to quote left because it has no argument...and instead
    // retriggers and lets x run.

    if (GET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT)) {
        if (GET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH))
            fail (Error_Literal_Left_Path_Raw());

        assert(!"Unexpected lack of use of NEXT_ARG_FROM_OUT");
    }

//=//// IF NOT A WORD!, IT DEFINITELY STARTS A NEW EXPRESSION /////////////=//

    // For long-pondered technical reasons, only WORD! is able to dispatch
    // enfix.  If it's necessary to dispatch an enfix function via path, then
    // a word is used to do it, like `>-` in `x: >- lib/method [...] [...]`.

    switch (KIND_BYTE_UNCHECKED(f_next)) {
      case REB_0_END:
        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
        goto finished; // hitting end is common, avoid do_next's switch()

      case REB_WORD:
        break;  // need to check for lookahead

      case REB_PATH: {  // need to check for lookahead *if* just a `/`
        if (
            GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)
            or MIRROR_BYTE(f_next) != REB_WORD
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

//=//// FETCH WORD! TO PERFORM SPECIAL HANDLING FOR ENFIX/INVISIBLES //////=//

    // First things first, we fetch the WORD! (if not previously fetched) so
    // we can see if it looks up to any kind of ACTION! at all.

    if (not f_next_gotten)
        f_next_gotten = Try_Lookup_Word(f_next, f_specifier);
    else
        assert(f_next_gotten == Try_Lookup_Word(f_next, f_specifier));

//=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-ENFIX /////////////=//

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

//=//// IT'S A WORD ENFIXEDLY TIED TO A FUNCTION (MAY BE "INVISIBLE") /////=//

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
            or GET_ACTION_FLAG(VAL_ACTION(f_next_gotten), IS_INVISIBLE)
                                       // ^-- `1 + 2 + comment "foo" 3` => 6
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

        // Leave the enfix operator pending in the frame, and it's up to the
        // parent frame to decide whether to use EVAL_FLAG_POST_SWITCH to jump
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

    Push_Action(f, VAL_ACTION(f_next_gotten), VAL_BINDING(f_next_gotten));
    Begin_Enfix_Action(f, VAL_WORD_SPELLING(f_next));

    Fetch_Next_Forget_Lookback(f);  // advances next
    goto process_action;

  abort_action:

    Drop_Action(f);
    DS_DROP_TO(f->dsp_orig); // any unprocessed refinements or chains on stack

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
    //     o/f left-lit  ; want error suggesting -> here, need flag for that
    //
    CLEAR_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH);
    assert(NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT));  // must be consumed

  #if !defined(NDEBUG)
    Eval_Core_Exit_Checks_Debug(f);  // called unless a fail() longjmps
    assert(NOT_EVAL_FLAG(f, DOING_PICKUPS));
    assert(
        (f->flags.bits & ~EVAL_FLAG_TOOK_HOLD) == initial_flags
    );  // any change should be restored, but va_list reification may hold
  #endif

    return false;  // false => not thrown
}
