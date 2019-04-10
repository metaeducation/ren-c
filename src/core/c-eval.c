//
//  File: %c-eval.c
//  Summary: "Central Interpreter Evaluator"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This file contains Eval_Internal_Maybe_Stale_Throws(), which is the central
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
// * _Internal_ => This is the fundamental C code for the evaluator, but it
//   can be "hooked".  Those hooks provide services like debug stepping and
//   tracing.  So most calls to this routine should be through a function
//   pointer and not directly.
//
// * _Throws => The return result is a boolean which all callers *must* heed.
//   There is no "thrown value" data type or cell flag, so the only indication
//   that a throw happened comes from this flag.  See %sys-throw.h
//
// Eval_Throws() is a small stub which takes care of the first two concerns,
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
// * Eval_Internal_Maybe_Stale_Throws() is LONG.  That's largely on purpose.
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


//
//  Dispatch_Internal: C
//
// Default function provided for the hook at the moment of action application,
// with all arguments gathered.
//
// As this is the default, it does nothing besides call the phase dispatcher.
// Debugging and instrumentation might want to do other things...e.g TRACE
// wants to preface the call by dumping the frame, and postfix it by showing
// the evaluative result.
//
// !!! Review if lower-level than C tricks could be used to patch code in
// some builds to not pay the cost for calling through a pointer.
//
REB_R Dispatch_Internal(REBFRM * const f)
  { return ACT_DISPATCHER(FRM_PHASE(f))(f); }


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
        if (CELL_KIND(VAL_UNESCAPED(f->arg)) == REB_NULLED)
            SET_EVAL_FLAG(f, REQUOTE_NULL);

        Dequotify(f->arg);
    }

    if (TYPE_CHECK(f->param, REB_TS_REFINEMENT)) {
        Typecheck_Refinement_And_Canonize(f->param, f->arg);
        return;
    }

    if (not Typecheck_Including_Quoteds(f->param, f->arg)) {
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

  #ifdef DEBUG_UNREADABLE_BLANKS
    //
    // The f->out slot should be initialized well enough for GC safety.
    // But in the debug build, if we're not running an invisible function
    // set it to END here, to make sure the non-invisible function writes
    // *something* to the output.
    //
    // END has an advantage because recycle/torture will catch cases of
    // evaluating into movable memory.  But if END is always set, natives
    // might *assume* it.  Fuzz it with unreadable blanks.
    //
    // !!! Should natives be able to count on f->out being END?  This was
    // at one time the case, but this code was in one instance.
    //
    if (NOT_ACTION_FLAG(FRM_PHASE(f), IS_INVISIBLE)) {
        if (SPORADICALLY(2))
            Init_Unreadable_Blank(f->out);
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
void Lookahead_To_Sync_Enfix_Defer_Flag(struct Reb_Feed *feed) {
    assert(NOT_FEED_FLAG(feed, DEFERRING_ENFIX));
    assert(not feed->gotten);

    CLEAR_FEED_FLAG(feed, NO_LOOKAHEAD);

    if (not IS_WORD(feed->value))
        return;

    feed->gotten = Try_Get_Opt_Var(feed->value, feed->specifier);

    if (not feed->gotten or not IS_ACTION(feed->gotten))
        return;

    if (GET_ACTION_FLAG(VAL_ACTION(feed->gotten), DEFERS_LOOKBACK))
        SET_FEED_FLAG(feed, DEFERRING_ENFIX);
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
    SHORTHAND (next, f->feed->value, NEVERNULL(const RELVAL*));
    SHORTHAND (specifier, f->feed->specifier, REBSPC*);

    if (GET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT))  { // e.g. `10 -> x:`
        if (IS_VOID(f->out))  // some set-xxx! accept null, none take void
            fail (Error_Need_Non_Void_Core(v, *specifier));

        CLEAR_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);
        CLEAR_CELL_FLAG(f->out, UNEVALUATED);  // this helper counts as eval
        return false;
    }

    if (IS_END(*next)) // `do [x:]`, `do [o/x:]`, etc. are illegal
        fail (Error_Need_Non_End_Core(v, *specifier));

    // !!! While assigning `x: #[void]` is not legal, we make a special
    // exemption for quoted voids, e.g. '#[void]`.  This means a molded
    // object with void fields can be safely MAKE'd back.
    //
    if (KIND_BYTE(*next) == REB_VOID + REB_64) {
        Init_Void(f->out);
        Fetch_Next_Forget_Lookback(f);  // advances f->value
        return false;
    }

    // Using a SET-XXX! means you always have at least two elements; it's like
    // an arity-1 function.  `1 + x: whatever ...`.  This overrides the no
    // lookahead behavior flag right up front.
    //
    CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

    REBFLGS flags = EVAL_MASK_DEFAULT
            | (f->flags.bits & EVAL_FLAG_FULFILLING_ARG);  // if f was, we are

    Init_Void(f->out); // `1 x: comment "hi"` shouldn't set x to 1!
    SET_CELL_FLAG(f->out, OUT_MARKED_STALE);  // ...but distinguish that case

    if (CURRENT_CHANGES_IF_FETCH_NEXT) { // must use new frame
        if (Eval_Step_In_Subframe_Throws(f->out, f, flags))
            return true;
    }
    else {  // !!! Reusing the frame, would inert optimization be worth it?
        if ((*PG_Eval_Maybe_Stale_Throws)(f))  // reuse `f`
            return true;
    }

    if (IS_VOID(f->out))  { // some set-xxx! accept null, none take void
        if (NOT_CELL_FLAG(f->out, OUT_MARKED_STALE))
            fail (Error_Need_Non_Void_Core(v, *specifier));

        // We preload with a stale void so we don't need a separate IS_END()
        // test on the common case.  But we want a distinct error if the
        // code was actually `x: ()` or `x: comment "hi"`.
        //
        fail (Error_Need_Non_End_Core(v, *specifier));
    }

    CLEAR_CELL_FLAG(f->out, UNEVALUATED);  // this helper counts as eval
    return false;
}


void Push_Enfix_Action(REBFRM *f, const REBVAL *action, REBSTR *opt_label)
{
    Push_Action(f, VAL_ACTION(action), VAL_BINDING(action));

    CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);  // *after* push, invisibles cache

    Begin_Action(f, opt_label);

    SET_EVAL_FLAG(f, RUNNING_ENFIX);
    SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);
}


//
//  Eval_Internal_Maybe_Stale_Throws: C
//
// See notes at top of file for general remarks on this central functions'
// name, and that wrappers should nearly always be used to call it.
//
// More detailed assertions of the preconditions, postconditions, and state
// at each evaluation step are contained in %d-eval.c, to keep this file
// more manageable in length.
//
bool Eval_Internal_Maybe_Stale_Throws(REBFRM * const f)
{
    // These shorthands help readability, and any decent compiler optimizes
    // such things out.  Note it means you refer to `next` via `*next`.
    // (This is ensured by the C++ build, that you don't say `if (next)...`)
    //
    REBVAL * const spare = FRM_SPARE(f);  // pointer is const (not the cell)
    SHORTHAND (next, f->feed->value, NEVERNULL(const RELVAL*));
    SHORTHAND (next_gotten, f->feed->gotten, const REBVAL*);
    SHORTHAND (specifier, f->feed->specifier, REBSPC*);

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
    );  // should be unchanged on exit
  #endif

    assert(DSP >= f->dsp_orig);  // REDUCE accrues, APPLY adds refinements
    assert(not IS_TRASH_DEBUG(f->out));  // all invisible will preserve output
    assert(f->out != spare);  // overwritten by temporary calculations
    assert(GET_EVAL_FLAG(f, DEFAULT_DEBUG));  // must use EVAL_MASK_DEFAULT
    assert(NOT_FEED_FLAG(f->feed, BARRIER_HIT));

    // Caching KIND_BYTE(*at) in a local can make a slight performance
    // difference, though how much depends on what the optimizer figures out.
    // Either way, it's useful to have handy in the debugger.
    //
    // Note: int8_fast_t picks `char` on MSVC, shouldn't `int` be faster?
    // https://stackoverflow.com/a/5069643/
    //
    union {
        int byte; // values bigger than REB_64 are used for in-situ literals
        enum Reb_Kind pun; // for debug viewing *if* byte < REB_MAX_PLUS_MAX
    } kind;

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
            kind.byte = REB_ACTION;  // must init for UNEVALUATED check
            goto process_action;
        }

        CLEAR_EVAL_FLAG(f, REEVALUATE_CELL);

        if (GET_CELL_FLAG(f->u.reval.value, ENFIXED)) {
            Push_Enfix_Action(f, f->u.reval.value, nullptr);
            Fetch_Next_Forget_Lookback(f);  // advances f->at

            kind.byte = REB_ACTION;  // must init for UNEVALUATED check
            goto process_action;
        }

        v = f->u.reval.value;
        gotten = nullptr;
        kind.byte = KIND_BYTE(v);
        goto reevaluate;
    }

    kind.byte = KIND_BYTE(*next);

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

    gotten = *next_gotten;
    v = Lookback_While_Fetching_Next(f);
    // ^-- can't just `v = *next` as fetch may overwrite--request a lookback!

    assert(kind.byte != REB_0_END);
    assert(kind.byte == KIND_BYTE_UNCHECKED(v));

    UPDATE_EXPRESSION_START(f); // !!! See FRM_INDEX() for caveats

  reevaluate: ;  // meaningful semicolon--subsequent macro may declare things

    // ^-- doesn't advance expression index, so `eval x` starts with `eval`

//=//// LOOKAHEAD FOR ENFIXED FUNCTIONS THAT QUOTE THEIR LEFT ARG /////////=//

    // Ren-C has an additional lookahead step *before* an evaluation in order
    // to take care of this scenario.  To do this, it pre-emptively feeds the
    // frame one unit that f->value is the *next* value, and a local variable
    // called "current" holds the current head of the expression that the
    // main switch would process.

    UPDATE_TICK_DEBUG(v);

    // v-- This is the TG_Break_At_Tick or C-DEBUG-BREAK landing spot --v

    if (KIND_BYTE(*next) != REB_WORD) // right's kind - END would be REB_0
        goto give_up_backward_quote_priority;

    assert(not *next_gotten);  // Fetch_Next_In_Frame() cleared it
    *next_gotten = Try_Get_Opt_Var(*next, *specifier);

    if (not *next_gotten or NOT_CELL_FLAG(*next_gotten, ENFIXED))
        goto give_up_backward_quote_priority;  // note only ACTION! is ENFIXED

    if (NOT_ACTION_FLAG(VAL_ACTION(*next_gotten), QUOTES_FIRST))
        goto give_up_backward_quote_priority;

    // If the action soft quotes its left, that means it's aware that its
    // "quoted" argument may be evaluated sometimes.  If there's evaluative
    // material on the left, treat it like it's in a group.
    //
    if (
        GET_ACTION_FLAG(VAL_ACTION(*next_gotten), POSTPONES_ENTIRELY)
        or (
            GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)
            and not ANY_SET_KIND(kind.byte)  // not SET-WORD!, SET-PATH!, etc.
        )
    ){
        // !!! cache this test?
        //
        REBVAL *first = First_Unspecialized_Param(VAL_ACTION(*next_gotten));
        if (VAL_PARAM_CLASS(first) == REB_P_SOFT_QUOTE)
            goto give_up_backward_quote_priority; // yield as an exemption
    }

    // Let the <skip> flag allow the right hand side to gracefully decline
    // interest in the left hand side due to type.  This is how DEFAULT works,
    // such that `case [condition [...] default [...]]` does not interfere
    // with the BLOCK! on the left, but `x: default [...]` gets the SET-WORD!
    //
    if (GET_ACTION_FLAG(VAL_ACTION(*next_gotten), SKIPPABLE_FIRST)) {
        REBVAL *first = First_Unspecialized_Param(VAL_ACTION(*next_gotten));
        if (not TYPE_CHECK(first, kind.byte)) // left's kind
            goto give_up_backward_quote_priority;
    }

    // Lookback args are fetched from f->out, then copied into an arg
    // slot.  Put the backwards quoted value into f->out.
    //
    Derelativize(f->out, v, *specifier); // for NEXT_ARG_FROM_OUT
    SET_CELL_FLAG(f->out, UNEVALUATED); // so lookback knows it was quoted

    // We skip over the word that invoked the action (e.g. <-, OF, =>).
    // v will then hold a pointer to that word (possibly now resident in the
    // frame spare).  (f->out holds what was the left)
    //
    gotten = *next_gotten;
    v = Lookback_While_Fetching_Next(f);

    if (
        IS_END(*next)
        and (kind.byte == REB_WORD or kind.byte == REB_PATH) // left kind
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
        Derelativize(f->out, v, *specifier);
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
    Begin_Action(f, VAL_WORD_SPELLING(v));

    SET_EVAL_FLAG(f, RUNNING_ENFIX);
    SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);

    CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
    kind.byte = REB_ACTION;  // for consistency in the UNEVALUATED check
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

    assert(kind.byte == KIND_BYTE_UNCHECKED(v));

    switch (kind.byte) {

      case REB_0_END:
        goto finished;


//==//// NULL ////////////////////////////////////////////////////////////==//
//
// Since nulled cells can't be in BLOCK!s, the evaluator shouldn't usually see
// them.  Plus the API quotes spliced values, so `rebValue("null?", nullptr)`
// gets a QUOTED! that evaluates to null--it's not a null being evaluated.
//
// But one way the evaluator can see NULL is EVAL, such as `eval first []`.

      case REB_NULLED:
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
// literally an action value in the array (`do compose [(:+) 1 2]`) or is
// being retriggered via EVAL.
//
// Most action evaluations are triggered from a WORD! or PATH!, which jumps in
// at the `process_action` label.

      case REB_ACTION: {
        assert(NOT_CELL_FLAG(v, ENFIXED));  // only WORD!s, via process_action

        REBSTR *opt_label = nullptr;  // not run from WORD!/PATH!, "nameless"

        Push_Action(f, VAL_ACTION(v), VAL_BINDING(v));
        Begin_Action(f, opt_label);

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
        assert(f->original); // set by Begin_Action()
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
            
            Init_Unreadable_Blank(f->arg);  // ...so cell must have valid bits
            continue;

    //=//// ACTUAL LOOP BODY //////////////////////////////////////////////=//

          loop_body:

            // !!! If not an APPLY or a typecheck of existing values, the data
            // array which backs the frame may not have any initialization of
            // its bits.  The goal is to make it so that the GC uses the
            // f->param position to know how far the frame fulfillment is
            // gotten, and only mark those values.  Hoewver, there is also
            // a desire to differentiate cell formatting between "stack"
            // and "heap" to do certain optimizations.  After a recent change,
            // it's becoming more integrated by using pooled memory for the
            // args...however issues of stamping the bits remain.  This just
            // blindly formats them with NODE_FLAG_STACK to make the arg
            // initialization work, but it's in progress to do this more
            // subtly so that the frame can be left formatted as non-stack.
            //
            if (
                NOT_EVAL_FLAG(f, DOING_PICKUPS)
                and not SPECIAL_IS_ARG_SO_TYPECHECKING
            ){
                Prep_Stack_Cell(f->arg); // improve...
            }
            else {
                // If the incoming series came from a heap frame, just put
                // a bit on it saying its a stack node for now--this will
                // stop some asserts.  The optimization is not enabled yet
                // which avoids reification on stack nodes of lower stack
                // levels--so it's not going to cause problems -yet-
                //
                f->arg->header.bits |= CELL_FLAG_STACK_LIFETIME;
            }

            assert(f->arg->header.bits & NODE_FLAG_CELL);
            assert(f->arg->header.bits & CELL_FLAG_STACK_LIFETIME);

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

                // A non-checked ISSUE! with binding indicates a partial
                // refinement with parameter index that needs to be pushed
                // to top of stack, hence HIGHER priority for fulfilling
                // @ the callsite than any refinements added by a PATH!.
                //
                if (IS_ISSUE(f->special)) {
                    REBCNT partial_index = VAL_WORD_INDEX(f->special);
                    REBSTR *partial_canon = VAL_STORED_CANON(f->special);

                    Init_Issue(DS_PUSH(), partial_canon);
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

                REBVAL *ordered = DS_TOP;
                REBSTR *param_canon = VAL_PARAM_CANON(f->param);  // #2258

                for (; ordered != DS_AT(f->dsp_orig); --ordered) {
                    if (VAL_STORED_CANON(ordered) != param_canon)
                        continue;

                    REBCNT offset = f->arg - FRM_ARGS_HEAD(f);
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

                if (TYPE_CHECK(f->param, REB_NULLED))
                    Init_Nulled(f->arg);  // <opt> refinements null if unused
                else
                    Init_Blank(f->arg);
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
                    assert(IS_NULLED(f->special));

                Init_Nulled(f->arg);
                SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;

              case REB_P_RETURN:
                //
                // Not always null in specialized frames, if that frame was
                // filled by inline specialization that ran the evaluator
                // (e.g. `f: does lit 'x` makes an f with an expired return)
                // Also, as with locals, could be modified during a call and
                // then a REDO of the frame could happen.
                //
                if (SPECIAL_IS_ARBITRARY_SO_SPECIALIZED)
                    assert(
                        IS_NULLED(f->special)
                        or NAT_ACTION(return) == VAL_ACTION(f->special)
                    );
                assert(VAL_PARAM_SYM(f->param) == SYM_RETURN);

                Move_Value(f->arg, NAT_VALUE(return));
                INIT_BINDING(f->arg, f->varlist);
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
                assert(Typecheck_Including_Quoteds(f->param, f->arg));

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
                    ASSERT_NOT_END(f->out);
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
                    Move_Value(f->arg, f->out);
                    if (GET_CELL_FLAG(f->out, UNEVALUATED))
                        SET_CELL_FLAG(f->arg, UNEVALUATED);

                    // When we see `1 + 2 * 3`, when we're at the 2, we don't
                    // want to let the * run yet.  So set a flag which says we
                    // won't do lookahead that will be cleared when function
                    // takes an argument *or* when a new expression starts.
                    //
                    // This flag is only set for evaluative left enfix.  What
                    // it does is puts the enfix into a *single step defer*.
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
                    Finalize_Arg(f);
                    break;

                  case REB_P_HARD_QUOTE:
                    if (not GET_CELL_FLAG(f->out, UNEVALUATED)) {
                        //
                        // This can happen e.g. with `x: 10 | x -> lit`.  We
                        // raise an error in this case, while still allowing
                        // `10 -> lit` to work, so people don't have to go
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

                  case REB_P_SOFT_QUOTE:
                    //
                    // Note: This permits f->out to not carry the UNEVALUATED
                    // flag--enfixed operations which have evaluations on
                    // their left are treated as if they were in a GROUP!.
                    // This is important to `1 + 2 <- lib/* 3` being 9, while
                    // also allowing `1 + x: <- lib/default [...]` to work.

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

                Expire_Out_Cell_Unless_Invisible(f);

                goto continue_arg_loop;
            }

    //=//// NON-ENFIX VARIADIC ARG (doesn't consume anything *yet*) ///////=//

            // Evaluation argument "hook" parameters (marked in MAKE ACTION!
            // by a `[[]]` in the spec, and in FUNC by `<...>`).  They point
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

            if (IS_END(*next) or GET_FEED_FLAG(f->feed, BARRIER_HIT)) {
                if (not Is_Param_Endable(f->param))
                    fail (Error_No_Arg(f, f->param));

                Init_Endish_Nulled(f->arg);
                SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;
            }

            switch (pclass) {

   //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes 1 EVALUATE's worth) ////=//

              case REB_P_NORMAL: {
                REBFLGS flags = EVAL_MASK_DEFAULT
                    | EVAL_FLAG_FULFILLING_ARG;

                if (IS_VOID(*next))  // Eval_Step() has callers test this
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
                    if (not Typecheck_Including_Quoteds(f->param, *next)) {
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

    //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  ////////////////////////////=//

              case REB_P_SOFT_QUOTE:
                if (not IS_QUOTABLY_SOFT(*next)) {
                    Literal_Next_In_Frame(f->arg, f); // CELL_FLAG_UNEVALUATED
                }
                else {
                    if (Eval_Value_Throws(f->arg, *next, *specifier)) {
                        Move_Value(f->out, f->arg);
                        goto abort_action;
                    }
                    Fetch_Next_Forget_Lookback(f);
                }

                // Have to account for enfix deferrals in cases like:
                //
                //     return if false '[foo] else '[bar]
                //
                Lookahead_To_Sync_Enfix_Defer_Flag(f->feed);

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
        if (DSP != f->dsp_orig and IS_ISSUE(DS_TOP)) {

          next_pickup:

            assert(IS_ISSUE(DS_TOP));

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

            assert(IS_UNREADABLE_DEBUG(f->arg) or IS_BLANK(f->arg));
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

        assert(NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT));

        assert(IS_END(f->param));
        assert(
            IS_END(*next)
            or FRM_IS_VALIST(f)
            or IS_VALUE_IN_ARRAY_DEBUG(f->feed->array, *next)
        );

        if (GET_EVAL_FLAG(f, FULFILL_ONLY)) {
            Init_Nulled(f->out);
            goto skip_output_check;
        }

        Expire_Out_Cell_Unless_Invisible(f);

        // While you can't evaluate into an array cell (because it may move)
        // an evaluation is allowed to be performed into stable cells on the
        // stack -or- API handles.
        //
        // !!! Could get complicated if a manual lifetime is used and freed
        // during an evaluation.  Not currently possible since there's nothing
        // like a rebValue() which targets a cell passed in by the user.  But if
        // such a thing ever existed it would have that problem...and would
        // need to take a "hold" on the cell to prevent a rebFree() while the
        // evaluation was in progress.
        //
        assert(f->out->header.bits & (
            CELL_FLAG_STACK_LIFETIME
            | NODE_FLAG_TRANSIENT
            | NODE_FLAG_ROOT
        ));

        *next_gotten = nullptr; // arbitrary code changes fetched variables

        // Note that the dispatcher may push ACTION! values to the data stack
        // which are used to process the return result after the switch.
        //
      blockscope {
        const REBVAL *r = (*PG_Dispatch)(f);  // default just calls FRM_PHASE

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
                    VAL_ACTION(label) == NAT_ACTION(unwind)
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
                    VAL_ACTION(label) == NAT_ACTION(redo)
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
            //
            // !!! Why is this test a NOT?

            if (NOT_CELL_FLAG(f->out, OUT_MARKED_STALE) or IS_END(*next))
                goto skip_output_check;

            // If an invisible is at the start of a frame and nothing is
            // after it, it has to retrigger until it finds something (or
            // until it hits the end of the frame).  It should not do a
            // START_NEW_EXPRESSION()...the expression index doesn't update.
            //
            //     do [comment "a" 1] => 1

            gotten = *next_gotten;
            v = Lookback_While_Fetching_Next(f);
            kind.byte = KIND_BYTE(v);

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
            REBSTR *opt_label = f->opt_label;
            Drop_Action(f);
            Push_Action(f, VAL_ACTION(DS_TOP), VAL_BINDING(DS_TOP));
            DS_DROP();

            // We use the same mechanism as enfix operations do...give the
            // next chain step its first argument coming from f->out
            //
            // !!! One side effect of this is that unless CHAIN is changed
            // to check, your chains can consume more than one argument.
            // This might be interesting or it might be bugs waiting to
            // happen, trying it out of curiosity for now.
            //
            Begin_Action(f, opt_label);
            assert(NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT));
            SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);

            goto process_action;
        }

        // We assume that null return results don't count for the requoting,
        // unless the dequoting was explicitly of a quoted null parameter.
        // Just a heuristic--if it doesn't work for someone, they'll have to
        // take QUOTED! themselves and do whatever specific logic they need.
        //
        if (GET_ACTION_FLAG(f->original, RETURN_REQUOTES)) {
            if (
                KIND_BYTE_UNCHECKED(f->out) != REB_NULLED
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

      case REB_WORD:
        if (not gotten)
            gotten = Get_Opt_Var_May_Fail(v, *specifier);

        if (IS_ACTION(gotten)) {  // before IS_NULLED() is common case
            REBACT *act = VAL_ACTION(gotten);

            if (GET_CELL_FLAG(gotten, ENFIXED)) {
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
            Begin_Action(f, VAL_WORD_SPELLING(v)); // use word as label

            if (GET_CELL_FLAG(gotten, ENFIXED)) {
                SET_EVAL_FLAG(f, RUNNING_ENFIX); // Push_Action() disallows
                SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);
            }
            goto process_action;
        }

        if (IS_NULLED_OR_VOID(gotten)) { // need `:x` if it's unset or void
            if (IS_NULLED(gotten))
                fail (Error_No_Value_Core(v, *specifier));
            fail (Error_Need_Non_Void_Core(v, *specifier));
        }

        Move_Value(f->out, gotten); // no copy CELL_FLAG_UNEVALUATED
        break;


//==//// SET_WORD! ///////////////////////////////////////////////////////==//
//
// Right hand side is evaluated into `out`, and then copied to the variable.
//
// Nulled cells are allowed: https://forum.rebol.info/t/895/4

      case REB_SET_WORD: {
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, v))  // see notes
            goto return_thrown;

      set_word_with_out:

        Move_Value(Sink_Var_May_Fail(v, *specifier), f->out);
        break; }


//==//// GET-WORD! ///////////////////////////////////////////////////////==//
//
// A GET-WORD! does no dispatch on functions, and will return NULL if the
// variable is not set.  The GET native operation requires the /ANY refinement
// to retrieve a VOID! value, but a GET-WORD! acts with an implicit /ANY.

      case REB_GET_WORD:
        Move_Opt_Var_May_Fail(f->out, v, *specifier);
        break;


//==//// INERT WORD AND STRING TYPES /////////////////////////////////////==//

    case REB_ISSUE:
        // ^-- ANY-WORD!
        goto inert;


//==//// GROUP! ///////////////////////////////////////////////////////////=//
//
// If a GROUP! is seen then it generates another call into Eval_Core().  The
// current frame is not reused, as the source array from which values are
// being gathered changes.
//
// Empty groups vaporize, as do ones that only consist of invisibles.
// However, they cannot combine with surrounding code, e.g.
//
//     >> 1 + 2 (comment "vaporize")
//     == 3
//
//     >> 1 + () 2
//     ** Script error: + is missing its value2 argument

      case REB_GROUP: {
        *next_gotten = nullptr;  // arbitrary code changes fetched variables

        DECLARE_FEED_AT_CORE (subfeed, v, *specifier);

        // "Maybe_Stale" variant leaves f->out as-is if no result generated
        // However, it sets OUT_MARKED_STALE in that case (note we may be
        // leaving an END in f->out by doing this.)
        //
        if (Do_Feed_To_End_Maybe_Stale_Throws(f->out, subfeed))
            goto return_thrown;

        CLEAR_CELL_FLAG(f->out, UNEVALUATED);  // `(1)` considered evaluative
        break; }


//==//// PATH! ///////////////////////////////////////////////////////////==//
//
// Paths starting with inert values do not evaluate.  `/foo/bar` has a blank
// at its head, and it evaluates to itself.
//
// Other paths run through the GET-PATH! mechanism and then EVAL the result.
// If the get of the path is null, then it will be an error.

      case REB_PATH: {
        assert(VAL_INDEX_UNCHECKED(v) == 0);  // this is the rule for now

        if (ANY_INERT(ARR_HEAD(VAL_ARRAY(v)))) {
            //
            // !!! TODO: Make special exception for `/` here, look up function
            // it is bound to.
            //
            Derelativize(f->out, v, *specifier);
            break;
        }

        REBVAL *where = GET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT) ? spare : f->out;

        REBSTR *opt_label;
        if (Eval_Path_Throws_Core(
            where,
            &opt_label,  // requesting says we run functions (not GET-PATH!)
            VAL_ARRAY(v),
            VAL_INDEX(v),
            Derive_Specifier(*specifier, v),
            nullptr, // `setval`: null means don't treat as SET-PATH!
            EVAL_FLAG_PUSH_PATH_REFINES
        )){
            if (where != f->out)
                Move_Value(f->out, where);
            goto return_thrown;
        }

        if (IS_ACTION(where)) {  // try this branch before fail on void+null
            //
            // PATH! dispatch is costly and can error in more ways than WORD!:
            //
            //     e: trap [do make block! ":a"] e/id = 'not-bound
            //                                   ^-- not ready @ lookahead
            //
            // Plus with GROUP!s in a path, their evaluations can't be undone.
            //
            if (GET_CELL_FLAG(where, ENFIXED))
                fail ("Use `<-` to shove left enfix operands into PATH!s");

            // !!! Review if invisibles can be supported without ->
            //
            if (GET_ACTION_FLAG(VAL_ACTION(where), IS_INVISIBLE))
                fail ("Use `<-` with invisibles fetched from PATH!");

            Push_Action(f, VAL_ACTION(where), VAL_BINDING(where));
            Begin_Action(f, opt_label);

            if (where == f->out)
                Expire_Out_Cell_Unless_Invisible(f);

            goto process_action;
        }

        if (IS_NULLED_OR_VOID(where)) {  // need `:x/y` if it's unset or void
            if (IS_NULLED(where))
                fail (Error_No_Value_Core(v, *specifier));
            fail (Error_Need_Non_Void_Core(v, *specifier));
        }

        if (where != f->out)
            Move_Value(f->out, where);  // won't move CELL_FLAG_UNEVALUATED
        else
            CLEAR_CELL_FLAG(f->out, UNEVALUATED);
        break; }


//==//// SET-PATH! ///////////////////////////////////////////////////////==//
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

      case REB_SET_PATH: {
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, v))
            goto return_thrown;

      set_path_with_out:

        if (Eval_Path_Throws_Core(
            spare,  // output if thrown, used as scratch space otherwise
            nullptr,  // not requesting symbol means refinements not allowed
            VAL_ARRAY(v),
            VAL_INDEX(v),
            Derive_Specifier(*specifier, v),
            f->out,
            EVAL_MASK_DEFAULT  // evaluating GROUP!s ok
        )){
            Move_Value(f->out, spare);
            goto return_thrown;
        }

        break; }


//==//// GET-PATH! ///////////////////////////////////////////////////////==//
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
// Consistent with GET-WORD!, a GET-PATH! acts as GET/ANY and permits VOID!
// and NULL return results.

      case REB_GET_PATH:
        if (Get_Path_Throws_Core(f->out, v, *specifier))
            goto return_thrown;

        // !!! This didn't appear to be true for `-- "hi" "hi"`, processing
        // GET-PATH! of a variadic.  Review if it should be true.
        //
        /* assert(NOT_CELL_FLAG(f->out, CELL_FLAG_UNEVALUATED)); */
        CLEAR_CELL_FLAG(f->out, UNEVALUATED);
        break;


//==//// GET-GROUP! //////////////////////////////////////////////////////==//
//
// Evaluates the group, and then executes GET-WORD!/GET-PATH!/GET-BLOCK!
// operation on it, if it's a WORD! or a PATH! or BLOCK!.  If it's an arity-0
// action, it is allowed to execute as a form of "functional getter".

      case REB_GET_GROUP: {
        *next_gotten = nullptr; // arbitrary code changes fetched variables

        if (Do_Any_Array_At_Throws(spare, v, *specifier)) {
            Move_Value(f->out, spare);
            goto return_thrown;
        }

        if (ANY_WORD(spare))
            kind.byte
                = mutable_KIND_BYTE(spare)
                = mutable_MIRROR_BYTE(spare)
                = REB_GET_WORD;
        else if (ANY_PATH(spare))
            kind.byte
                = mutable_KIND_BYTE(spare)
                = mutable_MIRROR_BYTE(spare)
                = REB_GET_PATH;
        else if (ANY_BLOCK(spare))
            kind.byte
                = mutable_KIND_BYTE(spare)
                = mutable_MIRROR_BYTE(spare)
                = REB_GET_BLOCK;
        else if (IS_ACTION(spare)) {
            if (Eval_Value_Throws(f->out, spare, SPECIFIED))  // only arity-0
                goto return_thrown;
            goto post_switch;
        }
        else
            fail (Error_Bad_Get_Group_Raw());

        v = spare;
        *next_gotten = nullptr;

        goto reevaluate; }


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

        *next_gotten = nullptr; // arbitrary code changes fetched variables

        if (Do_Any_Array_At_Throws(spare, v, *specifier)) {
            Move_Value(f->out, spare);
            goto return_thrown;
        }

        if (IS_ACTION(spare)) {
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
            Push_Action(f, VAL_ACTION(spare), VAL_BINDING(spare));
            Begin_Action(f, nullptr); // no label

            kind.byte = REB_ACTION;
            assert(NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT));
            SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);

            goto process_action;
        }

        v = spare;

        if (ANY_WORD(spare)) {
            kind.byte
                = mutable_KIND_BYTE(spare)
                = mutable_MIRROR_BYTE(spare)
                = REB_SET_WORD;
            goto set_word_with_out;
        }
        else if (ANY_PATH(spare)) {
            kind.byte
                = mutable_KIND_BYTE(spare)
                = mutable_MIRROR_BYTE(spare)
                = REB_SET_PATH;
            goto set_path_with_out;
        }
        else if (ANY_BLOCK(spare)) {
            kind.byte
                = mutable_KIND_BYTE(spare)
                = mutable_MIRROR_BYTE(spare)
                = REB_SET_BLOCK;
            goto set_block_with_out;
        }

        fail (Error_Bad_Set_Group_Raw()); }


//==//// GET-BLOCK! //////////////////////////////////////////////////////==//
//
// !!! This code path should be unified with GET/ANY of BLOCK!.  Temporarily
// does a REDUCE, but that was an experiment to see if perhaps GET of a
// BLOCK! should actually be what REDUCE was.  That thought experiment is
// probably over--it shouldn't, functions should not run (at least ones that
// are not zero arity)

      case REB_GET_BLOCK:
        *next_gotten = nullptr; // arbitrary code changes fetched variables

        if (Reduce_To_Stack_Throws(f->out, v, *specifier))
            goto return_thrown;

        Init_Block(f->out, Pop_Stack_Values(f->dsp_orig));
        break;


//==//// SET-BLOCK! //////////////////////////////////////////////////////==//
//
// Synonym for SET on the produced thing.

      case REB_SET_BLOCK: {
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, v))
            goto return_thrown;

      set_block_with_out:

        if (IS_NULLED(f->out)) // `[x y]: null` is illegal
            fail (Error_Need_Non_Null_Core(v, *specifier));

        const RELVAL *dest = VAL_ARRAY_AT(v);

        const RELVAL *src;
        if (IS_BLOCK(f->out))
            src = VAL_ARRAY_AT(f->out);
        else
            src = f->out;

        for (
            ;
            NOT_END(dest);
            ++dest, IS_END(src) or not IS_BLOCK(f->out) ? NOOP : (++src, NOOP)
        ){
            Set_Opt_Polymorphic_May_Fail(
                dest,
                *specifier,
                IS_END(src) ? BLANK_VALUE : src,  // R3-Alpha blanks after END
                IS_BLOCK(f->out)
                    ? VAL_SPECIFIER(f->out)
                    : SPECIFIED,
                false,  // not /ANY, e.g. voids are not legal
                false,  // doesn't set enfixedly
                false  // doesn't use "hard" semantics on groups in paths
            );
        }

        break; }


//==//////////////////////////////////////////////////////////////////////==//
//
// Treat all the other Is_Bindable() types as inert
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_BLOCK:
        //
      case REB_BINARY:
      case REB_TEXT:
      case REB_FILE:
      case REB_EMAIL:
      case REB_URL:
      case REB_TAG:
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
      case REB_CHAR:
      case REB_PAIR:
      case REB_TUPLE:
      case REB_TIME:
      case REB_DATE:
        //
      case REB_DATATYPE:
      case REB_TYPESET:
        //
      case REB_EVENT:
      case REB_HANDLE:
      case REB_LIBRARY:

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
        Derelativize(f->out, v, *specifier);
        Unquotify(f->out, 1);  // take off one level of quoting
        break;


//==//// QUOTED! (at 3 levels of escaping or less...or just garbage) //////=//
//
// All the values for types at >= REB_64 currently represent the special
// compact form of literals, which overlay inside the cell they escape.
// The real type comes from the type modulo 64.

      default:
        Derelativize(f->out, v, *specifier);
        Unquotify_In_Situ(f->out, 1);  // checks for illegal REB_XXX bytes
        break;
    }


//=//// END MAIN SWITCH STATEMENT /////////////////////////////////////////=//

    // The UNEVALUATED flag is one of the bits that doesn't get copied by
    // Move_Value() or Derelativize().  Hence it can be overkill to clear it
    // off if one knows a value came from doing those things.  This test at
    // the end checks to make sure that the right thing happened.
    //
    if (ANY_INERT_KIND(kind.byte)) {  // if() so as to check which part failed
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
        assert(kind.byte == REB_WORD);
    }

    // We're sitting at what "looks like the end" of an evaluation step.
    // But we still have to consider enfix.  e.g.
    //
    //    evaluate/set [1 + 2 * 3] 'val
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
    //    evaluate/set [1 + 2 * comment ["hi"] 3 4 / 5] 'val
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
    // But it can happen, e.g. `x: 10 | x <-`, where <- doesn't get an
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
    // a word is used to do it, like `->` in `x: -> lib/method [...] [...]`.

    kind.byte = KIND_BYTE(*next);

    if (kind.byte == REB_0_END) {
        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
        goto finished; // hitting end is common, avoid do_next's switch()
    }

    if (kind.byte == REB_PATH) {
        if (
            GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)
            or VAL_LEN_AT(*next) != 2
            or not IS_BLANK(ARR_AT(VAL_ARRAY(*next), 0))
            or not IS_BLANK(ARR_AT(VAL_ARRAY(*next), 1))
        ){
            CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
            goto finished;
        }

        // We had something like `5 + 5 / 2 + 3`.  This is a special form of
        // path dispatch tentatively called "path splitting" (as opposed to
        // `a/b` which is "path picking").  For the moment, this is not
        // handled as a parameterization to the PD_Xxx() functions, nor is it
        // a separate dispatch like PS_Xxx()...but it just performs division
        // compatibly with history.

        REBNOD *binding = nullptr;
        Push_Action(f, NAT_ACTION(path_0), binding);

        REBSTR *opt_label = nullptr;
        Begin_Action(f, opt_label);

        SET_EVAL_FLAG(f, RUNNING_ENFIX);
        SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);

        Fetch_Next_Forget_Lookback(f);  // advances f->value
        goto process_action;
    }

    if (kind.byte != REB_WORD) {
        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
        goto finished;
    }

//=//// FETCH WORD! TO PERFORM SPECIAL HANDLING FOR ENFIX/INVISIBLES //////=//

    // First things first, we fetch the WORD! (if not previously fetched) so
    // we can see if it looks up to any kind of ACTION! at all.

    if (not *next_gotten)
        *next_gotten = Try_Get_Opt_Var(*next, *specifier);
    else
        assert(*next_gotten == Try_Get_Opt_Var(*next, *specifier));

//=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-ENFIX /////////////=//

    // These cases represent finding the start of a new expression.
    //
    // Fall back on word-like "dispatch" even if ->gotten is null (unset or
    // unbound word).  It'll be an error, but that code path raises it for us.

    if (
        not *next_gotten  // v-- note that only ACTIONs have CELL_FLAG_ENFIXED
        or NOT_CELL_FLAG(*next_gotten, ENFIXED)
    ){
      lookback_quote_too_late: // run as if starting new expression

        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

        // Since it's a new expression, EVALUATE doesn't want to run it
        // even if invisible, as it's not completely invisible (enfixed)
        //
        goto finished;
    }

//=//// IT'S A WORD ENFIXEDLY TIED TO A FUNCTION (MAY BE "INVISIBLE") /////=//

    if (GET_ACTION_FLAG(VAL_ACTION(*next_gotten), QUOTES_FIRST)) {
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

        REBVAL *first = First_Unspecialized_Param(VAL_ACTION(*next_gotten));
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
            GET_ACTION_FLAG(VAL_ACTION(*next_gotten), DEFERS_LOOKBACK)
                                       // ^-- `1 + if false [2] else [3]` => 4
            or GET_ACTION_FLAG(VAL_ACTION(*next_gotten), IS_INVISIBLE)
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
            GET_ACTION_FLAG(VAL_ACTION(*next_gotten), POSTPONES_ENTIRELY)
            or (
                GET_ACTION_FLAG(VAL_ACTION(*next_gotten), DEFERS_LOOKBACK)
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
    CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

    // An evaluative lookback argument we don't want to defer, e.g. a normal
    // argument or a deferable one which is not being requested in the context
    // of parameter fulfillment.  We want to reuse the f->out value and get it
    // into the new function's frame.

    Push_Enfix_Action(f, *next_gotten, VAL_WORD_SPELLING(*next));
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
    assert(f->flags.bits == initial_flags);  // any change should be restored
  #endif

    return false;  // false => not thrown
}
