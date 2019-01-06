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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This file contains Eval_Core_Throws(), which is the central evaluator which
// is behind DO.  It can execute single evaluation steps (e.g. EVALUATE/EVAL)
// or it can run the array to the end of its content.  A flag controls that
// behavior, and there are EVAL_FLAG_XXX for controlling other behaviors.
//
// For comprehensive notes on the input parameters, output parameters, and
// internal state variables...see %sys-rebfrm.h.
//
// NOTES:
//
// * Eval_Core_Throws() is a long routine.  That is largely on purpose, as it
//   doesn't contain repeated portions.  If it were broken into functions that
//   would add overhead for little benefit, and prevent interesting tricks
//   and optimizations.  Note that it is separated into sections, and
//   the invariants in each section are made clear with comments and asserts.
//
// * The evaluator only moves forward, and it consumes exactly one element
//   from the input at a time.  Input is held read-only (SERIES_INFO_HOLD) for
//   the duration of execution.  At the moment it can be an array tracked by
//   index and incrementation, or it may be a C va_list which tracks its own
//   position on each fetch through a forward-only iterator.
//

#include "sys-core.h"


#if defined(DEBUG_COUNT_TICKS)
    //
    // The evaluator `tick` should be visible in the C debugger watchlist as a
    // local variable in Eval_Core_Throws() on each stack level.  So if fail()
    // happens at a deterministic moment in a run, capture the number from
    // the level of interest and recompile with it here to get a breakpoint
    // at that tick.
    //
    // On the command-line, you can also request to break at a particular tick
    // using the `--breakpoint NNN` option.
    //
    // *Plus* you can get the initialization tick for nulled cells, BLANK!s,
    // LOGIC!s, and most end markers by looking at the `track` payload of
    // the REBVAL cell.  Series contain the `REBSER.tick` where they were
    // created as well.  See also TOUCH_SERIES() and TOUCH_CELL().
    //
    //      *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
    #define TICK_BREAKPOINT        0
    //      *** DON'T COMMIT THIS --^ KEEP IT AT ZERO! ***
    //
    // Note also there is `Dump_Frame_Location()` if there's a trouble spot
    // and you want to see what the state is.  It will reify C va_list
    // input for you, so you can see what the C caller passed as an array.
    //
#endif


//
//  Dispatcher_Core: C
//
// Default function provided for the hook at the moment of action application.
// All arguments are gathered, and this gets access to the return result.
//
// As this is the default, it does nothing besides call the phase dispatcher.
// Debugging and instrumentation might want to do other things...e.g TRACE
// wants to preface the call by dumping the frame, and postfix it by showing
// the evaluative result.
//
// This adds one level of C function into every dispatch--but well worth it
// for the functionality.  Note also that R3-Alpha had `if (Trace_Flags)`
// in the main loop before and after function dispatch, which was more costly
// and much less flexible.  Nevertheless, sneaky lower-level-than-C tricks
// might be used to patch the machine code and avoid cost when not hooked.
//
REB_R Dispatcher_Core(REBFRM * const f) {
    return ACT_DISPATCHER(FRM_PHASE(f))(f);
}


static inline bool Start_New_Expression_Throws(REBFRM *f) {
    assert(Eval_Count >= 0);
    if (--Eval_Count == 0) {
        //
        // Note that Do_Signals_Throws() may do a recycle step of the GC, or
        // it may spawn an entire interactive debugging session via
        // breakpoint before it returns.  It may also FAIL and longjmp out.
        //
        if (Do_Signals_Throws(f->out))
            return true;
    }

    UPDATE_EXPRESSION_START(f); // !!! See FRM_INDEX() for caveats

    if (NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT))
        SET_CELL_FLAG(f->out, OUT_MARKED_STALE);

    // Want to keep this flag between an operation and an ensuing enfix in
    // the same frame, so can't clear in Drop_Action(), e.g. due to:
    //
    //     left-lit: enfix :lit
    //     o: make object! [f: does [1]]
    //     o/f left-lit ;--- want error mentioning -> here, need flag for that
    //
    CLEAR_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH);

    if (NOT_EVAL_FLAG(f, FULFILLING_ARG))
        assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD));

    assert(NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX));

    return false;
}


#if !defined(NDEBUG)
    #define START_NEW_EXPRESSION_MAY_THROW(f,g) \
        Eval_Core_Expression_Checks_Debug(f); \
        if (Start_New_Expression_Throws(f)) \
            g;
#else
    #define START_NEW_EXPRESSION_MAY_THROW(f,g) \
        if (Start_New_Expression_Throws(f)) \
            g;
#endif

// Either we're NOT evaluating and there's NO special exemption, or we ARE
// evaluating and there IS a special exemption on the value saying not to.
//
// (Note: EVAL_FLAG_EXPLICIT_EVALUATE is same bit as CELL_FLAG_EVAL_FLIP)
//
#define EVALUATING(v) \
    ((f->flags.bits & EVAL_FLAG_EXPLICIT_EVALUATE) \
        == ((v)->header.bits & CELL_FLAG_EVAL_FLIP))


#ifdef DEBUG_COUNT_TICKS
    //
    // Macro for same stack level as Eval_Core when debugging TICK_BREAKPOINT
    // Note that it uses a *signed* maximum due to the needs of the unreadable
    // blank, which doesn't want to steal a bit for its unreadable state...
    // so it negates the sign of the unsigned tick for unreadability.
    //
    #define UPDATE_TICK_DEBUG(cur) \
        do { \
            if (TG_Tick < INTPTR_MAX) /* avoid rollover (may be 32-bit!) */ \
                tick = f->tick = ++TG_Tick; \
            else \
                tick = f->tick = INTPTR_MAX; /* unsigned tick, signed max */ \
            if ( \
                (TG_Break_At_Tick != 0 and tick >= TG_Break_At_Tick) \
                or tick == TICK_BREAKPOINT \
            ){ \
                Debug_Fmt("TICK_BREAKPOINT at %d", tick); \
                Dump_Frame_Location((cur), f); \
                debug_break(); /* see %debug_break.h */ \
                TG_Break_At_Tick = 0; \
            } \
        } while (false)
#else
    #define UPDATE_TICK_DEBUG(cur) \
        NOOP
#endif


// ARGUMENT LOOP MODES
//
// The settings of f->special are chosen purposefully.  It is kept in sync
// with one of three possibilities:
//
// * f->param to indicate ordinary argument fulfillment for all the relevant
//   args, refinements, and refinement args of the function
//
// * f->arg, in order to indicate that the arguments should only be
//   type-checked.
//
// * some other pointer to an array of REBVAL which is the same length as the
//   argument list.  This indicates that any non-void values in that array
//   should be used in lieu of an ordinary argument...e.g. that argument has
//   been "specialized".
//
// By having all the states able to be incremented and hold the invariant, one
// can blindly do `++f->special` without doing something like checking for a
// null value first.
//
// Additionally, in the f->param state, f->special will never register as
// anything other than a typeset.  This increases performance of some checks,
// e.g. `IS_NULLED(f->special)` can only match the other two cases.
//

inline static bool In_Typecheck_Mode(REBFRM *f) {
    return f->special == f->arg;
}

inline static bool In_Unspecialized_Mode(REBFRM *f) {
    return f->special == f->param;
}


inline static void Revoke_Refinement_Arg(REBFRM *f) {
    assert(IS_NULLED(f->arg)); // may be "endish nulled"
    assert(IS_REFINEMENT(f->refine));

    // We can only revoke the refinement if this is the first refinement arg.
    // If it's a later arg, then the first didn't trigger revocation, or
    // refine wouldn't be a refinement.
    //
    if (f->refine + 1 != f->arg)
        fail (Error_Bad_Refine_Revoke(f->param, f->arg));

    Init_Blank(f->refine); // can't re-enable...
    SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);

    f->refine = ARG_TO_REVOKED_REFINEMENT;
}

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
        if (IS_REFINEMENT(f->refine)) {
            Revoke_Refinement_Arg(f);
            return;
        }

        SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
        return;
    }

  #if defined(DEBUG_STALE_ARGS) // see notes on flag definition
    assert(NOT_CELL_FLAG(f->arg, ARG_MARKED_CHECKED));
  #endif

    assert(
        f->refine == ORDINARY_ARG // check arg type
        or f->refine == ARG_TO_UNUSED_REFINEMENT // ensure arg null
        or f->refine == ARG_TO_REVOKED_REFINEMENT // ensure arg null
        or IS_REFINEMENT(f->refine) // ensure arg not null
    );

    if (kind_byte == REB_MAX_NULLED) {
        if (IS_REFINEMENT(f->refine)) {
            Revoke_Refinement_Arg(f);
            return; // don't check for optionality, refinement args always are
        }

        if (IS_FALSEY(f->refine)) {
            //
            // BLANK! means refinement already revoked, null is okay
            // false means refinement was never in use, so also okay
            //
            SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
            return;
        }

        assert(f->refine == ORDINARY_ARG); // fall through, check if <opt> ok
    }
    else { // argument is set...
        if (IS_FALSEY(f->refine)) // ...so refinement is not revoked/unused
            fail (Error_Bad_Refine_Revoke(f->param, f->arg));
    }

    if (
        kind_byte == REB_BLANK
        and TYPE_CHECK(f->param, REB_TS_NOOP_IF_BLANK) // e.g. <blank> param
    ){
        SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
        SET_EVAL_FLAG(f, FULFILL_ONLY);
        return;
    }

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
        if (CELL_KIND(VAL_UNESCAPED(f->arg)) == REB_MAX_NULLED)
            SET_EVAL_FLAG(f, REQUOTE_NULL);

        Dequotify(f->arg);
    }

    if (not Typecheck_Including_Quoteds(f->param, f->arg))
        fail (Error_Arg_Type(f, f->param, VAL_TYPE(f->arg)));

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
    PAYLOAD(Varargs, f->arg).signed_param_index =
        enfix
            ? -(f->arg - FRM_ARGS_HEAD(f) + 1)
            : f->arg - FRM_ARGS_HEAD(f) + 1;

    PAYLOAD(Varargs, f->arg).phase = FRM_PHASE(f);
    SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
}

#define Finalize_Variadic_Arg(f) \
    Finalize_Variadic_Arg_Core((f), false)

#define Finalize_Enfix_Variadic_Arg(f) \
    Finalize_Variadic_Arg_Core((f), true)


#ifdef DEBUG_EXPIRED_LOOKBACK
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (f->fake_lookback != nullptr)
#else
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (current == &f->lookback)
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
// quote ran, it would get deferred until after the RETURN.  This is not
// consistent with the pattern people expect.
//
void Lookahead_To_Sync_Enfix_Defer_Flag(REBFRM *f) {
    assert(NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX));
    assert(not f->gotten);

    CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

    if (not IS_WORD(f->value))
        return;

    f->gotten = Try_Get_Opt_Var(f->value, f->specifier);

    if (not f->gotten or not IS_ACTION(f->gotten))
        return;

    if (GET_ACTION_FLAG(VAL_ACTION(f->gotten), DEFERS_LOOKBACK))
        SET_FEED_FLAG(f->feed, DEFERRING_ENFIX);
}


inline static bool Dampen_Lookahead(REBFRM *f) {
    if (GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)) {
        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
        return true;
    }
    return false;
}


// SET-WORD!, SET-PATH!, SET-GROUP!, and SET-BLOCK! all want to do roughly
// the same thing as the first step of their evaluation.  They want to make
// sure they don't corrupt whatever is in current (e.g. remember the WORD!
// or PATH! so they can set it), while evaluating the right hand side to
// know what to put there.
//
// What makes this slightly complicated is that the current value may be in
// a place that doing a Fetch_Next_In_Frame() might corrupt it.  This must
// be accounted for by using a subframe.
//
inline static bool Rightward_Evaluate_Nonvoid_Into_Out_Throws(
    REBFRM *f,
    const RELVAL *current
){
    if (IS_END(f->value)) // `do [x:]`, `do [o/x:]`, etc. are illegal
        fail (Error_Need_Non_End_Core(current, f->specifier));

    // !!! While assigning `x: #[void]` is not legal, we make a special
    // exemption for quoted voids, e.g. '#[void]`.  This means a molded
    // object with void fields can be safely MAKE'd back.
    //
    if (KIND_BYTE(f->value) == REB_VOID + REB_64) {
        Init_Void(f->out);
        Fetch_Next_In_Frame(nullptr, f); // advances f->value
        return false;
    }

    // Using a SET-XXX! means you always have at least two elements; it's like
    // an arity-1 function.  `1 + x: whatever ...`.  This overrides the no
    // lookahead behavior flag right up front.
    //
    CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);

    REBFLGS flags =
        (DO_MASK_DEFAULT & ~EVAL_FLAG_CONST)
        | (f->flags.bits & EVAL_FLAG_EXPLICIT_EVALUATE)
        | (f->flags.bits & EVAL_FLAG_CONST)
        | (f->flags.bits & EVAL_FLAG_FULFILLING_ARG); // if f was, we are

    Init_Void(f->out); // `1 x: comment "hi"` shouldn't set x to 1!

    if (CURRENT_CHANGES_IF_FETCH_NEXT) { // must use new frame
        DECLARE_SUBFRAME(child, f);
        if (Eval_Step_In_Subframe_Throws(f->out, f, flags, child))
            return true;
    }
    else {
        if (Eval_Step_Mid_Frame_Throws(f, flags)) // light reuse of `f`
            return true;
    }

    if (IS_VOID(f->out)) // some set operations accept null, none take void
        fail (Error_Need_Non_Void_Core(current, f->specifier));

    return false;
}


//
//  Eval_Core_Throws: C
//
// While this routine looks very complex, it's actually not that difficult
// to step through.  A lot of it is assertions, debug tracking, and comments.
//
// Comments on the definition of Reb_Frame are a good place to start looking
// to understand what's going on.  See %sys-rebfrm.h for full details.
//
// These fields are required upon initialization:
//
//     f->out
//     REBVAL pointer to which the evaluation's result should be written.
//     Should be to writable memory in a cell that lives above this call to
//     Eval_Core in stable memory that is not user-visible (e.g. DECLARE_LOCAL
//     or the frame's f->cell).  This can't point into an array whose memory
//     may move during arbitrary evaluation, and that includes cells on the
//     expandable data stack.  It also usually can't write a function argument
//     cell, because that could expose an unfinished calculation during this
//     Eval_Core_Throws() through its FRAME!...though a Eval_Core_Throws(f)
//     must write f's *own* arg slots to fulfill them.
//
//     f->value
//     Pre-fetched first value to execute (cannot be an END marker)
//
//     f->feed
//     Contains the REBARR* or C va_list of subsequent values to fetch.
//
//     f->specifier
//     Resolver for bindings of values in f->feed, SPECIFIED if all resolved
//
//     f->gotten
//     Must be either be the Get_Var() lookup of f->value, or END
//
//     f->dsp_orig
//     Must be set to the base stack location of the operation (this may be
//     a deeper stack level than current DSP if this is an apply, and
//     refinements were preloaded onto the stack)
//
// More detailed assertions of the preconditions, postconditions, and state
// at each evaluation step are contained in %d-eval.c
//
bool Eval_Core_Throws(REBFRM * const f)
{
    bool threw = false;

    REBVAL * const cell = cast(REBVAL*, &(f->cell)); // briefer, optimized out

  #if defined(DEBUG_COUNT_TICKS)
    REBTCK tick = f->tick = TG_Tick; // snapshot start tick
  #endif

    assert(DSP >= f->dsp_orig); // REDUCE accrues, APPLY adds refinements, >=
    assert(not IS_TRASH_DEBUG(f->out)); // all invisibles preserves output
    assert(f->out != cell); // overwritten by temporary calculations
    assert(GET_EVAL_FLAG(f, DEFAULT_DEBUG)); // must use DO_MASK_DEFAULT

    // Caching KIND_BYTE(f->value) in a local can make a slight performance
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

    const REBVAL *current_gotten;
    TRASH_POINTER_IF_DEBUG(current_gotten);
    const RELVAL *current;
    TRASH_POINTER_IF_DEBUG(current);

    // Given how the evaluator is written, it's inevitable that there will
    // have to be a test for points to `goto` before running normal eval.
    // This cost is paid on every entry to Eval_Core_Throws().
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

        if (GET_CELL_FLAG(f->u.reval.value, ENFIXED)) {
            f->value = VOID_VALUE; // !!! special signal to push_enfix_action
            f->gotten = f->u.reval.value;
            goto push_enfix_action;
        }

        current = f->u.reval.value;
        current_gotten = nullptr;
        kind.byte = KIND_BYTE(current);
        goto reevaluate;
    }

    kind.byte = KIND_BYTE(f->value);

  do_next:;

    START_NEW_EXPRESSION_MAY_THROW(f, goto return_thrown);
    // ^-- resets local `tick` count, Ctrl-C may abort

    // We attempt to reuse any lookahead fetching done with Get_Var.  In the
    // general case, this is not going to be possible, e.g.:
    //
    //     obj: make object! [x: 10]
    //     foo: does [append obj [y: 20]]
    //     do in obj [foo x]
    //
    // Consider the lookahead fetch for `foo x`.  It will get x to f->gotten,
    // and see that it is not a lookback function.  But then when it runs foo,
    // the memory location where x had been found before may have moved due
    // to expansion.  Basically any function call invalidates f->gotten, as
    // does obviously any Fetch_Next_In_Frame (because the position changes)
    //
    // !!! Review how often gotten has hits vs. misses, and what the benefit
    // of the feature actually is.
    //
    current_gotten = f->gotten;

    // Most calls to Fetch_Next_In_Frame() are no longer interested in the
    // cell backing the pointer that used to be in f->value (this is enforced
    // by a rigorous test in DEBUG_EXPIRED_LOOKBACK).  Special care must be
    // taken when one is interested in that data, because it may have to be
    // moved.  So current is returned from Fetch_Next_In_Frame().
    //
    Fetch_Next_In_Frame(&current, f);

    assert(
        kind.byte != REB_0_END
        and kind.byte == KIND_BYTE_UNCHECKED(current)
    );

  reevaluate:;

    // ^-- doesn't advance expression index, so `eval x` starts with `eval`

    UPDATE_TICK_DEBUG(current);
    // v-- This is the TICK_BREAKPOINT or C-DEBUG-BREAK landing spot --v

    //==////////////////////////////////////////////////////////////////==//
    //
    // LOOKAHEAD TO ENABLE ENFIXED FUNCTIONS THAT QUOTE THEIR LEFT ARG
    //
    //==////////////////////////////////////////////////////////////////==//

    // Ren-C has an additional lookahead step *before* an evaluation in order
    // to take care of this scenario.  To do this, it pre-emptively feeds the
    // frame one unit that f->value is the *next* value, and a local variable
    // called "current" holds the current head of the expression that the
    // main switch would process.

    if (KIND_BYTE(f->value) != REB_WORD) // right's kind - END would be REB_0
        goto give_up_backward_quote_priority;

    if (not EVALUATING(f->value))
        goto give_up_backward_quote_priority;

    assert(not f->gotten); // Fetch_Next_In_Frame() cleared it
    f->gotten = Try_Get_Opt_Var(f->value, f->specifier);
    if (not f->gotten or NOT_CELL_FLAG(f->gotten, ENFIXED))
        goto give_up_backward_quote_priority;

    if (NOT_ACTION_FLAG(VAL_ACTION(f->gotten), QUOTES_FIRST)) // enfix=>action
        goto give_up_backward_quote_priority;

    // If the action soft quotes its left, that means it's aware that its
    // "quoted" argument may be evaluated sometimes.  If there's evaluative
    // material on the left, treat it like it's in a group.
    //
    if (
        GET_ACTION_FLAG(VAL_ACTION(f->gotten), POSTPONES_ENTIRELY)
        or (
            GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)
            and (kind.byte != REB_SET_WORD and kind.byte != REB_SET_PATH)
        )
    ){
        // !!! cache this test?
        //
        REBVAL *first = First_Unspecialized_Param(VAL_ACTION(f->gotten));
        if (VAL_PARAM_CLASS(first) == REB_P_SOFT_QUOTE)
            goto give_up_backward_quote_priority; // yield as an exemption
    }

    // Let the <skip> flag allow the right hand side to gracefully decline
    // interest in the left hand side due to type.  This is how DEFAULT works,
    // such that `case [condition [...] default [...]]` does not interfere
    // with the BLOCK! on the left, but `x: default [...]` gets the SET-WORD!
    //
    if (GET_ACTION_FLAG(VAL_ACTION(f->gotten), SKIPPABLE_FIRST)) {
        REBVAL *first = First_Unspecialized_Param(VAL_ACTION(f->gotten));
        if (not TYPE_CHECK(first, kind.byte)) // left's kind
            goto give_up_backward_quote_priority;
    }

    // Lookback args are fetched from f->out, then copied into an arg
    // slot.  Put the backwards quoted value into f->out.
    //
    Derelativize(f->out, current, f->specifier); // for NEXT_ARG_FROM_OUT
    SET_CELL_FLAG(f->out, UNEVALUATED); // so lookback knows it was quoted

    // We skip over the word that invoked the action (e.g. <-, OF, =>).
    // current will then hold a pointer to that word (possibly now
    // resident in the frame cell).  (f->out holds what was the left)
    //
    current_gotten = f->gotten;
    Fetch_Next_In_Frame(&current, f);

    if (
        IS_END(f->value)
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

        Move_Value(&f->lookback, f->out);
        Derelativize(f->out, current, f->specifier);
        SET_CELL_FLAG(f->out, UNEVALUATED);

        // leave f->value at END
        current = &f->lookback;
        current_gotten = nullptr;

        SET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH); // for better error message
        SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT); // literal right op is arg

        goto give_up_backward_quote_priority; // run PATH!/WORD! normal
    }

    // Wasn't the at-end exception, so run normal enfix with right winning.

    Push_Action(f, VAL_ACTION(current_gotten), VAL_BINDING(current_gotten));
    Begin_Action(f, VAL_WORD_SPELLING(current));

    SET_EVAL_FLAG(f, RUNNING_ENFIX);
    SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);

    CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
    goto process_action;

  give_up_backward_quote_priority:;

    //==////////////////////////////////////////////////////////////////==//
    //
    // BEGIN MAIN SWITCH STATEMENT
    //
    //==////////////////////////////////////////////////////////////////==//

    // This switch is done via contiguous REB_XXX values, in order to
    // facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables

    assert(kind.byte == KIND_BYTE_UNCHECKED(current));

    if (not EVALUATING(current)) {
        Derelativize(f->out, current, f->specifier);
        SET_CELL_FLAG(f->out, UNEVALUATED);

        // Unlike the `inert` branch, when we are not evaluating we do not
        // inherit the `const` bits from the evaluation.  This is so that
        // when you say things like `rebElide("append", block, "10");` in the
        // API, it acts like `append block 10` instead of `append [] 10`,
        // avoiding the consting of the variable.
        //
        goto post_switch;
    }

    switch (kind.byte) {

      case REB_0_END:
        goto finished;

//==//////////////////////////////////////////////////////////////////////==//
//
// [QUOTED!] (at 4 or more levels of escaping)
//
// This is the form of literal that's too escaped to just overlay in the cell
// by using a higher kind byte.  See the `default:` case in this switch for
// handling of the more compact forms, that are much more common.
//
// (Highly escaped literals should be rare, but for completeness you need to
// be able to escape any value, including any escaped one...!)
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_QUOTED: {
        Derelativize(f->out, current, f->specifier);
        Unquotify(f->out, 1); // take off one level of quoting
        break; }

//==//////////////////////////////////////////////////////////////////////==//
//
// [ACTION!] (lookback or non-lookback)
//
// If an action makes it to the SWITCH statement, that means it is either
// literally an action value in the array (`do compose [(:+) 1 2]`) or is
// being retriggered via EVAL.
//
// Most action evaluations are triggered from a WORD! or PATH!, which jumps in
// at the `process_action` label.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_ACTION: {
        assert(NOT_CELL_FLAG(current, ENFIXED)); // come from WORD!/PATH! only

        REBSTR *opt_label = nullptr; // not invoked through a word, "nameless"

        Push_Action(f, VAL_ACTION(current), VAL_BINDING(current));
        Begin_Action(f, opt_label);

        // We'd like `10 -> = 5 + 5` to work, and to do so it reevaluates in
        // a new frame, but has to run the `=` as "getting its next arg from
        // the output slot, but not being run in an enfix mode".
        //
        if (NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT))
            Expire_Out_Cell_Unless_Invisible(f);

        goto process_action; }

    //==////////////////////////////////////////////////////////////////==//
    //
    // ACTION! ARGUMENT FULFILLMENT AND/OR TYPE CHECKING PROCESS
    //
    //==////////////////////////////////////////////////////////////////==//

        // This one processing loop is able to handle ordinary action
        // invocation, specialization, and type checking of an already filled
        // action frame.  It walks through both the formal parameters (in
        // the spec) and the actual arguments (in the call frame) using
        // pointer incrementation.
        //
        // Based on the parameter type, it may be necessary to "consume" an
        // expression from values that come after the invocation point.  But
        // not all parameters will consume arguments for all calls.

      process_action:; // Note: Also jumped to by the redo_checked code

      #if !defined(NDEBUG)
        assert(f->original); // set by Begin_Action()
        Do_Process_Action_Checks_Debug(f);
      #endif

        assert(DSP >= f->dsp_orig); // path processing may push REFINEMENT!s
        assert(f->refine == ORDINARY_ARG);

        TRASH_POINTER_IF_DEBUG(current); // shouldn't be used below
        TRASH_POINTER_IF_DEBUG(current_gotten);

        CLEAR_EVAL_FLAG(f, DOING_PICKUPS);

      process_args_for_pickup_or_to_end:;

        for (; NOT_END(f->param); ++f->param, ++f->arg, ++f->special) {
            Reb_Param_Class pclass = VAL_PARAM_CLASS(f->param);

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
            if (NOT_EVAL_FLAG(f, DOING_PICKUPS) and f->special != f->arg)
                Prep_Stack_Cell(f->arg); // improve...
            else {
                // If the incoming series came from a heap frame, just put
                // a bit on it saying its a stack node for now--this will
                // stop some asserts.  The optimization is not enabled yet
                // which avoids reification on stack nodes of lower stack
                // levels--so it's not going to cause problems -yet-
                //
                f->arg->header.bits |= CELL_FLAG_STACK_LIFETIME; // unreadable blank ok
            }

            assert(f->arg->header.bits & NODE_FLAG_CELL);
            assert(f->arg->header.bits & CELL_FLAG_STACK_LIFETIME);

    //=//// A /REFINEMENT ARG /////////////////////////////////////////////=//

            // Refinements are checked first for a reason.  This is to
            // short-circuit based on EVAL_FLAG_DOING_PICKUPS before redoing
            // fulfillments on arguments that have already been handled.
            //
            // Pickups are needed because the "visitation order" of the
            // parameters while walking across the parameter array might not
            // match the "consumption order" of the expressions that need to
            // be fetched from the callsite.  For instance:
            //
            //     foo: func [aa /b bb /c cc] [...]
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
            // REFINEMENT! words (e.g. /B and /C above) on the data stack.

            if (pclass == REB_P_REFINEMENT) {
                if (GET_EVAL_FLAG(f, DOING_PICKUPS)) {
                    if (DSP != f->dsp_orig)
                        goto next_pickup;

                    f->param = END_NODE; // don't need f->param in paramlist
                    goto arg_loop_and_any_pickups_done;
                }

                TRASH_POINTER_IF_DEBUG(f->refine); // must update to new value

                REBVAL *ordered = DS_TOP;
                REBSTR *param_canon = VAL_PARAM_CANON(f->param); // #2258

                if (f->special == f->param) // acquire all args at callsite
                    goto unspecialized_refinement; // most common case

                // All tests below are on special, but if f->special is not
                // the same as f->arg then f->arg must get assigned somehow
                // (jumping to unspecialized_refinement will take care of it)

                if (IS_NULLED(f->special)) {
                    assert(NOT_CELL_FLAG(f->special, ARG_MARKED_CHECKED));
                    goto unspecialized_refinement; // second most common
                }

                if (IS_BLANK(f->special)) // either specialized or not...
                    goto unused_refinement; // will get ARG_MARKED_CHECKED

                // If arguments in the frame haven't already gone through
                // some kind of processing, use the truthiness of the value.
                //
                // !!! This must accept what it puts out--the /REFINE-NAME
                // or a BLANK!, to work with pre-built frames.  Accepting
                // #[true] and #[false] are a given as well.  It seems that
                // doing more typechecking than that has limited benefit,
                // since at minimum it needs to accept any other refinement
                // name to control it, but it could be considered.
                //
                if (NOT_CELL_FLAG(f->special, ARG_MARKED_CHECKED)) {
                    if (IS_FALSEY(f->special)) // !!! error on void, needed?
                        goto unused_refinement;

                    f->refine = f->arg; // remember, as we might revoke!
                    goto used_refinement;
                }

                if (IS_REFINEMENT(f->special)) {
                    assert(
                        VAL_REFINEMENT_SPELLING(f->special)
                        == VAL_PARAM_SPELLING(f->param)
                    ); // !!! Maybe not, if REDESCRIBE renamed args, but...
                    f->refine = f->arg;
                    goto used_refinement; // !!! ...this would fix it up.
                }

                // A "typechecked" void means it's unspecialized, but partial
                // refinements are still coming that may have higher priority
                // in taking arguments at the callsite than the current
                // refinement, if it's in use due to a PATH! invocation.
                //
                if (IS_VOID(f->special))
                    goto unspecialized_refinement_must_pickup; // defer this

                // A "typechecked" ISSUE! with binding indicates a partial
                // refinement with parameter index that needs to be pushed
                // to top of stack, hence HIGHER priority for fulfilling
                // @ the callsite than any refinements added by a PATH!.
                //
                if (IS_ISSUE(f->special)) {
                    REBCNT partial_index = VAL_WORD_INDEX(f->special);
                    REBSTR *partial_canon = VAL_STORED_CANON(f->special);

                    Init_Issue(DS_PUSH(), partial_canon);
                    INIT_BINDING(DS_TOP, f->varlist);
                    PAYLOAD(Word, DS_TOP).index = partial_index;

                    f->refine = SKIPPING_REFINEMENT_ARGS;
                    goto used_refinement;
                }

                assert(IS_INTEGER(f->special)); // DO FRAME! leaves these

                assert(GET_EVAL_FLAG(f, FULLY_SPECIALIZED));
                f->refine = f->arg; // remember so we can revoke!
                goto used_refinement;

    //=//// UNSPECIALIZED REFINEMENT SLOT (no consumption) ////////////////=//

              unspecialized_refinement:;

                if (f->dsp_orig == DSP) // no refinements left on stack
                    goto unused_refinement;

                if (VAL_STORED_CANON(ordered) == param_canon) {
                    DS_DROP(); // we're lucky: this was next refinement used
                    f->refine = f->arg; // remember so we can revoke!
                    goto used_refinement;
                }

                --ordered; // not lucky: if in use, this is out of order

              unspecialized_refinement_must_pickup:; // fulfill on 2nd pass

                for (; ordered != DS_AT(f->dsp_orig); --ordered) {
                    if (VAL_STORED_CANON(ordered) != param_canon)
                        continue;

                    // The call uses this refinement but we'll have to
                    // come back to it when the expression index to
                    // consume lines up.  Save the position to come back to,
                    // as binding information on the refinement.
                    //
                    REBCNT offset = f->arg - FRM_ARGS_HEAD(f);
                    INIT_BINDING(ordered, f->varlist);
                    INIT_WORD_INDEX(ordered, offset + 1);
                    f->refine = SKIPPING_REFINEMENT_ARGS; // fill args later
                    goto used_refinement;
                }

                goto unused_refinement; // not in path, not specialized

              unused_refinement:;

                f->refine = ARG_TO_UNUSED_REFINEMENT; // "don't consume"
                Init_Blank(f->arg);
                SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;

              used_refinement:;

                assert(not IS_POINTER_TRASH_DEBUG(f->refine)); // must be set
                Refinify(Init_Word(f->arg, VAL_PARAM_SPELLING(f->param)));
                SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;
            }

    //=//// "PURE" LOCAL: ARG /////////////////////////////////////////////=//

            // This takes care of locals, including "magic" RETURN cells that
            // need to be pre-filled.  !!! Note nuances with compositions:
            //
            // https://github.com/metaeducation/ren-c/issues/823
            //
            // Also note that while it might seem intuitive to take care of
            // these "easy" fills before refinement checking--checking for
            // refinement pickups ending prevents double-doing this work.

            switch (pclass) {
              case REB_P_LOCAL:
                //
                // When REDOing a function frame, it is sent back up to do
                // typechecking (f->special == f->arg), and the check takes
                // care of clearing the locals, they may not be null...
                //
                if (f->special != f->param and f->special != f->arg)
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
                if (f->special != f->param and f->special != f->arg)
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

    //=//// IF COMING BACK TO REFINEMENT ARGS LATER, MOVE ON FOR NOW //////=//

            if (f->refine == SKIPPING_REFINEMENT_ARGS)
                goto skip_this_arg_for_now;

            if (GET_CELL_FLAG(f->special, ARG_MARKED_CHECKED)) {

    //=//// SPECIALIZED OR OTHERWISE TYPECHECKED ARG //////////////////////=//

                if (f->arg != f->special) {
                    //
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
                assert(
                    (f->refine != ORDINARY_ARG and IS_NULLED(f->arg))
                    or Typecheck_Including_Quoteds(f->param, f->arg)
                );

                goto continue_arg_loop;
            }

            // !!! This is currently a hack for APPLY.  It doesn't do a type
            // checking pass after filling the frame, but it still wants to
            // treat all values (nulls included) as fully specialized.
            //
            if (
                f->arg == f->special // !!! should this ever allow gathering?
                /* GET_EVAL_FLAG(f, FULLY_SPECIALIZED) */
            ){
                if (Is_Param_Variadic(f->param))
                    Finalize_Variadic_Arg(f);
                else
                    Finalize_Arg(f);
                goto continue_arg_loop; // looping to verify args/refines
            }

    //=//// IF UNSPECIALIZED ARG IS INACTIVE, SET NULL AND MOVE ON ////////=//

            // Unspecialized arguments that do not consume do not need any
            // further processing or checking.  null will always be fine.
            //
            if (f->refine == ARG_TO_UNUSED_REFINEMENT) {
                //
                // Overwrite if !(EVAL_FLAG_FULLY_SPECIALIZED) faster than check
                //
                Init_Nulled(f->arg);
                SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;
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
                        RESET_CELL(f->arg, REB_VARARGS);
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

                    RESET_CELL(f->arg, REB_VARARGS);
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
                        if (NOT_ACTION_FLAG(FRM_PHASE(f), DEFERS_LOOKBACK))
                            SET_FEED_FLAG(f->feed, NO_LOOKAHEAD);
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
                        if (Eval_Value_Throws(f->arg, f->out)) {
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
                RESET_CELL(f->arg, REB_VARARGS);
                INIT_BINDING(f->arg, f->varlist); // frame-based VARARGS!

                Finalize_Variadic_Arg(f);
                goto continue_arg_loop;
            }

    //=//// AFTER THIS, PARAMS CONSUME FROM CALLSITE IF NOT APPLY /////////=//

            assert(f->refine == ORDINARY_ARG or IS_REFINEMENT(f->refine));

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

            if (IS_END(f->value) or GET_FEED_FLAG(f->feed, BARRIER_HIT)) {
                if (not Is_Param_Endable(f->param))
                    fail (Error_No_Arg(f, f->param));

                Init_Endish_Nulled(f->arg);
                SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;
            }

            switch (pclass) {

   //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes 1 EVALUATE's worth) ////=//

              case REB_P_NORMAL: {
                REBFLGS flags = (DO_MASK_DEFAULT & ~EVAL_FLAG_CONST)
                    | EVAL_FLAG_FULFILLING_ARG
                    | (f->flags.bits & EVAL_FLAG_EXPLICIT_EVALUATE)
                    | (f->flags.bits & EVAL_FLAG_CONST);

                DECLARE_SUBFRAME (child, f); // capture DSP *now*
                SET_END(f->arg); // Finalize_Arg() sets to Endish_Nulled
                if (Eval_Step_In_Subframe_Throws(f->arg, f, flags, child)) {
                    Move_Value(f->out, f->arg);
                    goto abort_action;
                }
                break; }

    //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG /////////////////////////////=//

              case REB_P_HARD_QUOTE:
                if (not Is_Param_Skippable(f->param))
                    Quote_Next_In_Frame(f->arg, f); // CELL_FLAG_UNEVALUATED
                else {
                    if (not Typecheck_Including_Quoteds(f->param, f->value)) {
                        assert(Is_Param_Endable(f->param));
                        Init_Endish_Nulled(f->arg); // not EVAL_FLAG_BARRIER_HIT
                        SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                        goto continue_arg_loop;
                    }
                    Quote_Next_In_Frame(f->arg, f);
                    SET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED);
                    SET_CELL_FLAG(f->arg, UNEVALUATED);
                }

                // Have to account for enfix deferrals in cases like:
                //
                //     return lit 1 then (x => [x + 1])
                //
                Lookahead_To_Sync_Enfix_Defer_Flag(f);

                if (GET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED))
                    goto continue_arg_loop;

                break;

    //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  ////////////////////////////=//

              case REB_P_SOFT_QUOTE:
                if (not IS_QUOTABLY_SOFT(f->value)) {
                    Quote_Next_In_Frame(f->arg, f); // CELL_FLAG_UNEVALUATED
                }
                else {
                    if (Eval_Value_Core_Throws(
                        f->arg,
                        f->value,
                        f->specifier
                    )){
                        Move_Value(f->out, f->arg);
                        goto abort_action;
                    }
                    Fetch_Next_In_Frame(nullptr, f);
                }

                // Have to account for enfix deferrals in cases like:
                //
                //     return if false '[foo] else '[bar]
                //
                Lookahead_To_Sync_Enfix_Defer_Flag(f);

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

            assert(pclass != REB_P_REFINEMENT);
            assert(pclass != REB_P_LOCAL);
            assert(
                not In_Typecheck_Mode(f) // already handled, unless...
                or NOT_EVAL_FLAG(f, FULLY_SPECIALIZED) // ...this!
            );

            Finalize_Arg(f);

          continue_arg_loop:;

            assert(GET_CELL_FLAG(f->arg, ARG_MARKED_CHECKED));
            continue;

          skip_this_arg_for_now:;

            // The GC will protect values up through how far we have
            // enumerated, so we need to put *something* in this slot when
            // skipping, since we're going past it in the enumeration.
            //
            Init_Unreadable_Blank(f->arg);
            continue;
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

          next_pickup:;

            assert(IS_ISSUE(DS_TOP));

            if (not IS_WORD_BOUND(DS_TOP)) { // the loop didn't index it
                mutable_KIND_BYTE(DS_TOP) = REB_WORD;
                fail (Error_Bad_Refine_Raw(DS_TOP)); // so duplicate or junk
            }

            // FRM_ARGS_HEAD offsets are 0-based, while index is 1-based.
            // But +1 is okay, because we want the slots after the refinement.
            //
            REBINT offset =
                VAL_WORD_INDEX(DS_TOP) - (f->arg - FRM_ARGS_HEAD(f));
            f->param += offset;
            f->arg += offset;
            f->special += offset;

            f->refine = f->arg - 1; // this refinement may still be revoked
            assert(
                IS_REFINEMENT(f->refine)
                and (
                    VAL_REFINEMENT_SPELLING(f->refine)
                    == VAL_PARAM_SPELLING(f->param - 1)
                )
            );

            assert(VAL_STORED_CANON(DS_TOP) == VAL_PARAM_CANON(f->param - 1));
            assert(VAL_PARAM_CLASS(f->param - 1) == REB_P_REFINEMENT);

            DS_DROP();
            SET_EVAL_FLAG(f, DOING_PICKUPS);
            goto process_args_for_pickup_or_to_end;
        }

      arg_loop_and_any_pickups_done:;

        assert(IS_END(f->param)); // signals !Is_Action_Frame_Fulfilling()

    //==////////////////////////////////////////////////////////////////==//
    //
    // ACTION! ARGUMENTS NOW GATHERED, DISPATCH PHASE
    //
    //==////////////////////////////////////////////////////////////////==//

        if (GET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT)) {
            if (GET_EVAL_FLAG(f, DIDNT_LEFT_QUOTE_PATH))
                fail (Error_Literal_Left_Path_Raw());
        }

      redo_unchecked:;

        assert(NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT));

        assert(IS_END(f->param));
        // refine can be anything.
        assert(
            IS_END(f->value)
            or FRM_IS_VALIST(f)
            or IS_VALUE_IN_ARRAY_DEBUG(f->feed->array, f->value)
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
        // like a rebRun() which targets a cell passed in by the user.  But if
        // such a thing ever existed it would have that problem...and would
        // need to take a "hold" on the cell to prevent a rebFree() while the
        // evaluation was in progress.
        //
        /*assert(f->out->header.bits & (CELL_FLAG_STACK_LIFETIME | NODE_FLAG_ROOT)); */

        f->gotten = nullptr; // arbitrary code changes fetched variables

        // Note that the dispatcher may push ACTION! values to the data stack
        // which are used to process the return result after the switch.
        //
        const REBVAL *r; // initialization would be skipped by gotos
        r = (*PG_Dispatcher)(f); // default just calls FRM_PHASE(f)

        if (r == f->out) {
            assert(NOT_CELL_FLAG(f->out, OUT_MARKED_STALE));
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
                        FRM_PHASE(f) != PAYLOAD(Context, f->out).phase
                        and did (exemplar = ACT_EXEMPLAR(
                            PAYLOAD(Context, f->out).phase
                        ))
                    ){
                        f->special = CTX_VARS_HEAD(exemplar);
                        f->arg = FRM_ARGS_HEAD(f);
                        for (; NOT_END(f->arg); ++f->arg, ++f->special) {
                            if (IS_NULLED(f->special)) // no specialization
                                continue;
                            Move_Value(f->arg, f->special); // reset it
                        }
                    }

                    FRM_PHASE(f) = PAYLOAD(Context, f->out).phase;
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

            if (GET_CELL_FLAG(r, FALSEY)) // R_REDO_UNCHECKED
                goto redo_unchecked;

          redo_checked:; // R_REDO_CHECKED

            Expire_Out_Cell_Unless_Invisible(f);

            f->param = ACT_PARAMS_HEAD(FRM_PHASE(f));
            f->arg = FRM_ARGS_HEAD(f);
            f->special = f->arg;
            f->refine = ORDINARY_ARG; // no gathering, but need for assert

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

            if (NOT_CELL_FLAG(f->out, OUT_MARKED_STALE) or IS_END(f->value))
                goto skip_output_check;

            // If an invisible is at the start of a frame and nothing is
            // after it, it has to retrigger until it finds something (or
            // until it hits the end of the frame).  It should not do a
            // START_NEW_EXPRESSION()...the expression index doesn't update.
            //
            //     do [comment "a" 1] => 1

            current_gotten = f->gotten;
            Fetch_Next_In_Frame(&current, f);
            kind.byte = KIND_BYTE(current);

            Drop_Action(f);
            goto reevaluate; }

          default:
            assert(!"Invalid pseudotype returned from action dispatcher");
        }

      dispatch_completed:;

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

      skip_output_check:;

        // If we have functions pending to run on the outputs (e.g. this was
        // the result of a CHAIN) we can run those chained functions in the
        // same REBFRM, for efficiency.
        //
        // !!! There is also a feature where the QUOTED! dispatcher wants to
        // run through ordinary dispatch for generic dispatch, but then add
        // its level of "literality" to the output result.  Right now that's
        // done by having it push a plain integer to the stack, saying how
        // many levels of escaping to add to the output.  This is in the
        // experimental phase, but would probably be done with a similar
        // mechanism based on some information from any action's signature.
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
        if (
            KIND_BYTE_UNCHECKED(f->out) != REB_0_END
            and (
                KIND_BYTE_UNCHECKED(f->out) != REB_MAX_NULLED
                or GET_EVAL_FLAG(f, REQUOTE_NULL)
            )
            and GET_ACTION_FLAG(f->original, HAS_RETURN)
        ){
            REBVAL *return_param = ACT_PARAM(
                f->original,
                ACT_NUM_PARAMS(f->original)
            );
            assert(VAL_PARAM_SYM(return_param) == SYM_RETURN);

            if (TYPE_CHECK(return_param, REB_TS_DEQUOTE_REQUOTE))
                Quotify(f->out, f->requotes);
        }

        Drop_Action(f);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [WORD!]
//
// A plain word tries to fetch its value through its binding.  It will fail
// and longjmp out of this stack if the word is unbound (or if the binding is
// to a variable which is not set).  Should the word look up to a function,
// then that function will be called by jumping to the ANY-ACTION! case.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_WORD:
        if (not current_gotten)
            current_gotten = Get_Opt_Var_May_Fail(current, f->specifier);

        if (IS_ACTION(current_gotten)) { // before IS_NULLED() is common case
            REBACT *act = VAL_ACTION(current_gotten);

            // Note: The usual dispatch of enfix functions is not via a
            // REB_WORD in this switch, it's by some code at the end of
            // the switch.  So you only see enfix in cases like `(+ 1 2)`,
            // or after PARAMLIST_IS_INVISIBLE e.g. `10 comment "hi" + 20`.
            //
            if (GET_CELL_FLAG(current_gotten, ENFIXED)) {
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

            Push_Action(f, act, VAL_BINDING(current_gotten));
            Begin_Action(f, VAL_WORD_SPELLING(current)); // use word as label

            if (GET_CELL_FLAG(current_gotten, ENFIXED)) {
                SET_EVAL_FLAG(f, RUNNING_ENFIX); // Push_Action() disallows
                SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);
            }
            goto process_action;
        }

        if (IS_NULLED_OR_VOID(current_gotten)) { // need `:x` if `x` is unset
            if (IS_NULLED(current_gotten))
                fail (Error_No_Value_Core(current, f->specifier));
            fail (Error_Need_Non_Void_Core(current, f->specifier));
        }

        Move_Value(f->out, current_gotten); // no copy CELL_FLAG_UNEVALUATED
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [SET-WORD!]
//
// A chain of `x: y: z: ...` may happen, so there could be any number of
// SET-WORD!s before the value to assign is found.  Some kind of list needs to
// be maintained.
//
// Recursion into Eval_Core_Throws() is used, but a new frame is not created.
// So it reuses `f` in a lighter-weight approach, gathering state only on the
// data stack (which provides GC protection).  Eval_Step_Mid_Frame_Throws()
// has remarks on how this is done.
//
// Note that nulled cells are allowed: https://forum.rebol.info/t/895/4
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_SET_WORD: {
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, current))
            goto return_thrown;

      set_word_with_out:;

        Move_Value(Sink_Var_May_Fail(current, f->specifier), f->out);
        break; }

//==//////////////////////////////////////////////////////////////////////==//
//
// [GET-WORD!]
//
// A GET-WORD! does no checking for unsets, no dispatch on functions, and
// will return void if the variable is not set.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_GET_WORD:
        Move_Opt_Var_May_Fail(f->out, current, f->specifier);
        break;

//==//// INERT WORD AND STRING TYPES /////////////////////////////////////==//

    case REB_ISSUE:
        // ^-- ANY-WORD!
        goto inert;

//==//////////////////////////////////////////////////////////////////////==//
//
// [GROUP!]
//
// If a GROUP! is seen then it generates another call into Eval_Core_Throws().
// The current frame is not reused, as the source array from which values are
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
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_GROUP: {
        f->gotten = nullptr; // arbitrary code changes fetched variables

        // Since current may be f->cell, extract properties to reuse it.
        //
        REBARR *array = VAL_ARRAY(current); // array of the GROUP!
        REBCNT index = VAL_INDEX(current); // index may not be @ head
        REBSPC *derived = Derive_Specifier(f->specifier, current);

        if (IS_END(f->out)) {
            //
            // No need for a temporary cell...we know we're starting from an
            // END cell so determining if the GROUP! is invisible is easy.
            //
            REBIXO indexor = Eval_Array_At_Core(
                f->out,
                nullptr, // opt_first (null means nothing, not nulled cell)
                array,
                index,
                derived,
                (DO_MASK_DEFAULT & ~EVAL_FLAG_CONST)
                    | EVAL_FLAG_TO_END
                    | (f->flags.bits & EVAL_FLAG_CONST)
                    | (current->header.bits & EVAL_FLAG_CONST)
            );
            if (indexor == THROWN_FLAG)
                goto return_thrown;
            if (GET_CELL_FLAG(f->out, OUT_MARKED_STALE))
                goto finished;
            f->out->header.bits &= ~CELL_FLAG_UNEVALUATED; // (1) "evaluates"
        }
        else {
            // Not as lucky... we might have something like (1 + 2 elide "Hi")
            // that would show up as having the stale bit.
            //
            REBIXO indexor = Eval_Array_At_Core(
                SET_END(cell),
                nullptr, // opt_first (null means nothing, not nulled cell)
                array,
                index,
                derived,
                (DO_MASK_DEFAULT & ~EVAL_FLAG_CONST)
                    | EVAL_FLAG_TO_END
                    | (f->flags.bits & EVAL_FLAG_CONST)
                    | (current->header.bits & EVAL_FLAG_CONST)
            );
            if (indexor == THROWN_FLAG) {
                Move_Value(f->out, cell);
                goto return_thrown;
            }
            if (IS_END(cell)) {
                kind.byte = KIND_BYTE(f->value);
                if (kind.byte == REB_0_END)
                    goto finished;
                goto do_next; // quickly process next item, no infix test
            }

            Move_Value(f->out, cell); // no CELL_FLAG_UNEVALUATED
        }
        break; }

//==//////////////////////////////////////////////////////////////////////==//
//
// [PATH!]
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_PATH: {
        //
        // Length-0 paths look like `/`, and do a special dispatch (currently
        // hacked up to just act as the DIVIDE native, but ultimtely would
        // be another form of dispatch based on the left type...and numbers
        // would use this for division).  This dispatch happens after the
        // switch statement along with enfix, so if we see it here that means
        // there was nothing to the left.
        //
        if (VAL_LEN_AT(current) == 0)
            fail ("Empty path must have left argument for 'split' behavior");

        REBVAL *where = GET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT) ? cell : f->out;

        REBSTR *opt_label;
        if (Eval_Path_Throws_Core(
            where,
            &opt_label, // requesting says we run functions (not GET-PATH!)
            VAL_ARRAY(current),
            VAL_INDEX(current),
            Derive_Specifier(f->specifier, current),
            nullptr, // `setval`: null means don't treat as SET-PATH!
            EVAL_FLAG_PUSH_PATH_REFINEMENTS
        )){
            if (where != f->out)
                Move_Value(f->out, where);
            goto return_thrown;
        }

        if (IS_NULLED_OR_VOID(where)) { // need `:x/y` if `y` is unset
            if (IS_NULLED(where))
                fail (Error_No_Value_Core(current, f->specifier));
            fail (Error_Need_Non_Void_Core(current, f->specifier));
        }

        if (IS_ACTION(where)) {
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

        if (where != f->out)
            Move_Value(f->out, where);
        else {
            // !!! Usually not true but seems true for path evaluation in
            // varargs, e.g. while running `-- "a" "a"`.  Review.
            //
            CLEAR_CELL_FLAG(f->out, UNEVALUATED);
            /* assert(NOT_CELL_FLAG(f->out, CELL_FLAG_UNEVALUATED)); */
        }
        break; }

//==//////////////////////////////////////////////////////////////////////==//
//
// [SET-PATH!]
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

//==//////////////////////////////////////////////////////////////////////==//

      case REB_SET_PATH: {
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, current))
            goto return_thrown;

      set_path_with_out:;

        if (Eval_Path_Throws_Core(
            cell, // output if thrown, used as scratch space otherwise
            nullptr, // not requesting symbol means refinements not allowed
            VAL_ARRAY(current),
            VAL_INDEX(current),
            f->specifier,
            f->out,
            DO_MASK_DEFAULT // evaluating GROUP!s ok
        )){
            Move_Value(f->out, cell);
            goto return_thrown;
        }

        // !!! May have passed something into Eval_Path_Throws_Core() with
        // the unevaluated flag, so it could tell `x/y: 2` from `x/y: 1 + 1`.
        // But now that the SET-PATH! ran, consider the result "evaluated".
        //
        CLEAR_CELL_FLAG(f->out, UNEVALUATED);
        break; }

//==//////////////////////////////////////////////////////////////////////==//
//
// [GET-PATH!]
//
// Note that the GET native on a PATH! won't allow GROUP! execution:
//
//    foo: [X]
//    path: 'foo/(print "side effect!" 1)
//    get path ;-- not allowed, due to surprising side effects
//
// However a source-level GET-PATH! allows them, since they are at the
// callsite and you are assumed to know what you are doing:
//
//    :foo/(print "side effect" 1) ;-- this is allowed
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_GET_PATH:
        if (Get_Path_Throws_Core(f->out, current, f->specifier))
            goto return_thrown;

        // !!! This didn't appear to be true for `-- "hi" "hi"`, processing
        // GET-PATH! of a variadic.  Review if it should be true.
        //
        /* assert(NOT_CELL_FLAG(f->out, CELL_FLAG_UNEVALUATED)); */
        CLEAR_CELL_FLAG(f->out, UNEVALUATED);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [GET-GROUP!]
//
// Evaluates the group, and then executes GET-WORD!/GET-PATH!/GET-BLOCK!
// operation on it, if it's a WORD! or a PATH! or BLOCK!.  If it's an arity-0
// action, it is allowed to execute as a form of "functional getter".
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_GET_GROUP: {
        f->gotten = nullptr; // arbitrary code changes fetched variables

        if (Do_At_Throws(
            cell,
            VAL_ARRAY(current),
            VAL_INDEX(current),
            f->specifier
        )){
            Move_Value(f->out, cell);
            goto return_thrown;
        }

        if (ANY_WORD(cell))
            kind.byte = mutable_KIND_BYTE(cell) = REB_GET_WORD;
        else if (ANY_PATH(cell))
            kind.byte = mutable_KIND_BYTE(cell) = REB_GET_PATH;
        else if (ANY_BLOCK(cell))
            kind.byte = mutable_KIND_BYTE(cell) = REB_GET_BLOCK;
        else if (IS_ACTION(cell)) {
            if (Eval_Value_Throws(f->out, cell)) // only arity-0 allowed
                goto return_thrown;
            goto post_switch;
        }
        else
            fail (Error_Bad_Get_Group_Raw());

        current = cell;
        current_gotten = nullptr;

        goto reevaluate; }

//==//////////////////////////////////////////////////////////////////////==//
//
// [SET-GROUP!]
//
// Synonym for SET on the produced thing, unless it's an action...in which
// case an arity-1 function is allowed to be called and passed the right.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_SET_GROUP: {
        //
        // Protocol for all the REB_SET_XXX is to evaluate the right before
        // the left.  Same with SET_GROUP!.  (Consider in particular the case
        // of PARSE, where it has to hold the SET-GROUP! in suspension while
        // it looks on the right in order to decide if it will run it at all!)
        //
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, current))
            goto return_thrown;

        f->gotten = nullptr; // arbitrary code changes fetched variables

        if (Do_At_Throws(
            cell,
            VAL_ARRAY(current),
            VAL_INDEX(current),
            f->specifier
        )){
            Move_Value(f->out, cell);
            goto return_thrown;
        }

        if (IS_ACTION(cell)) {
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
            Push_Action(f, VAL_ACTION(cell), VAL_BINDING(cell));
            Begin_Action(f, nullptr); // no label

            kind.byte = REB_ACTION;
            assert(NOT_EVAL_FLAG(f, NEXT_ARG_FROM_OUT));
            SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);

            goto process_action;
        }

        current = cell;

        if (ANY_WORD(cell)) {
            kind.byte = mutable_KIND_BYTE(cell) = REB_SET_WORD;
            goto set_word_with_out;
        }
        else if (ANY_PATH(cell)) {
            kind.byte = mutable_KIND_BYTE(cell) = REB_SET_PATH;
            goto set_path_with_out;
        }
        else if (ANY_BLOCK(cell)) {
            kind.byte = mutable_KIND_BYTE(cell) = REB_SET_BLOCK;
            goto set_block_with_out;
        }

        fail (Error_Bad_Set_Group_Raw()); }

//==//////////////////////////////////////////////////////////////////////==//
//
// [GET-BLOCK!]
//
// Synonym for REDUCE.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_GET_BLOCK:
        f->gotten = nullptr; // arbitrary evaluation can move fetched pointers

        if (Reduce_To_Stack_Throws(f->out, current, f->specifier))
            goto return_thrown;

        Init_Block(f->out, Pop_Stack_Values(f->dsp_orig));
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [SET-BLOCK!]
//
// Synonym for SET on the produced thing.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_SET_BLOCK: {
        if (Rightward_Evaluate_Nonvoid_Into_Out_Throws(f, current))
            goto return_thrown;

      set_block_with_out:;

        if (IS_NULLED(f->out)) // `[x y]: null` is illegal
            fail (Error_Need_Non_Null_Core(current, f->specifier));

        const RELVAL *item = VAL_ARRAY_AT(current);

        const RELVAL *v;
        if (IS_BLOCK(f->out))
            v = VAL_ARRAY_AT(f->out);
        else
            v = f->out;

        for (
            ;
            NOT_END(item);
            ++item, IS_END(v) or not IS_BLOCK(f->out) ? NOOP : (++v, NOOP)
        ){
            Set_Opt_Polymorphic_May_Fail(
                item,
                f->specifier,
                IS_END(v) ? BLANK_VALUE : v, // R3-Alpha/Red blank after END
                IS_BLOCK(f->out)
                    ? VAL_SPECIFIER(f->out)
                    : SPECIFIED,
                false, // doesn't set enfixedly
                false // doesn't use "hard" semantics on groups in paths
            );
        }

        assert(NOT_CELL_FLAG(f->out, UNEVALUATED));

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
      case REB_IMAGE:
      case REB_VECTOR:
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
      case REB_GOB:
      case REB_EVENT:
      case REB_HANDLE:
      case REB_STRUCT:
      case REB_LIBRARY:

      inert:; // SEE ALSO: Quote_Next_In_Frame()...similar behavior

        Derelativize(f->out, current, f->specifier);
        SET_CELL_FLAG(f->out, UNEVALUATED); // CELL_FLAG_INERT ??

        // `rebRun("append", "[]", "10");` should error, passing on the const
        // of the va_arg frame.  That is a case of a block with neutral bits
        // (no const, no mutable), so the constness brought by the execution
        // via DO-ing it should win.  But `rebRun("append", block, "10");`
        // needs to get the mutability bits of that block, more like as if
        // you had the plain Rebol code `append block 10`.  This is why the
        // non-EVALUATING() case--which applies to non-text-scanned arguments
        // in API calls--is checked before the switch.  So it does not
        // run this line that only applies to "evaluated inert values".
        //
        f->out->header.bits |= (f->flags.bits & EVAL_FLAG_CONST);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [VOID!]
//
// "A void! is a means of giving a hot potato back that is a warning about
//  something, but you don't want to force an error 'in the moment'...in case
//  the returned information wasn't going to be used anyway."
//
// https://forum.rebol.info/t/947
//
// If we get here, the evaluator is actually seeing it, and it's time to fail.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_VOID:
        fail ("VOID! cells cannot be evaluated");

//==//////////////////////////////////////////////////////////////////////==//
//
// [NULL]
//
// NULLs are not an ANY-VALUE!.  Usually a DO shouldn't be able to see them.
// An exception is in API calls, such as `rebRun("null?", some_null)`.  That
// is legal due to CELL_FLAG_EVAL_FLIP, which avoids "double evaluation",
// and is used by the API when constructing runs of values from C va_args.
//
// Another way the evaluator can see NULL is EVAL, such as `eval first []`.
// An error is given there, for consistency:
//
//     :foo/bar => pick foo 'bar (null if not present)
//     foo/bar => eval :foo/bar (should be an error if not present)
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_MAX_NULLED:
        fail (Error_Evaluate_Null_Raw());

//==//////////////////////////////////////////////////////////////////////==//
//
// [QUOTED!] (at 3 levels of escaping or less)...or a garbage type byte
//
// All the values for types at >= REB_64 currently represent the special
// compact form of literals, which overlay inside the cell they escape.
// The real type comes from the type modulo 64.
//
//==//////////////////////////////////////////////////////////////////////==//

      default:
        Derelativize(f->out, current, f->specifier);
        Unquotify_In_Situ(f->out, 1); // checks for illegal REB_XXX bytes
        break;
    }

    //==////////////////////////////////////////////////////////////////==//
    //
    // END MAIN SWITCH STATEMENT
    //
    //==////////////////////////////////////////////////////////////////==//

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

  post_switch:;

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

    kind.byte = KIND_BYTE(f->value);

    if (kind.byte == REB_0_END) {
        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
        goto finished; // hitting end is common, avoid do_next's switch()
    }

    if (kind.byte == REB_PATH) {
        if (
            Dampen_Lookahead(f)
            or VAL_LEN_AT(f->value) != 2
            or not IS_BLANK(ARR_AT(VAL_ARRAY(f->value), 0))
            or not IS_BLANK(ARR_AT(VAL_ARRAY(f->value), 1))
            or not EVALUATING(f->value)
        ){
            if (NOT_EVAL_FLAG(f, TO_END))
                goto finished; // just 1 step of work, so stop evaluating

            assert(NOT_EVAL_FLAG(f, FULFILLING_ARG)); // one only
            goto do_next;
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

        Fetch_Next_In_Frame(nullptr, f); // advances f->value
        goto process_action;
    }

    if (kind.byte != REB_WORD or not EVALUATING(f->value)) {
        Dampen_Lookahead(f);

        if (NOT_EVAL_FLAG(f, TO_END))
            goto finished; // only want 1 EVALUATE of work, so stop evaluating

        goto do_next;
    }

//=//// FETCH WORD! TO PERFORM SPECIAL HANDLING FOR ENFIX/INVISIBLES //////=//

    // First things first, we fetch the WORD! (if not previously fetched) so
    // we can see if it looks up to any kind of ACTION! at all.

    if (not f->gotten)
        f->gotten = Try_Get_Opt_Var(f->value, f->specifier);
    else {
        // !!! a particularly egregious hack in EVAL-ENFIX lets us simulate
        // enfix for a function whose value is not enfix.  This means the
        // value in f->gotten isn't the fetched function, but the function
        // plus a CELL_FLAG_ENFIXED.  Account for this hack.
        //
        assert(
            f->gotten == Try_Get_Opt_Var(f->value, f->specifier)
            or (f->prior->original == NAT_ACTION(eval_enfix)) // !!! hack
        );
    }

//=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-ENFIX /////////////=//

    // These cases represent finding the start of a new expression, which
    // continues the evaluator loop if EVAL_FLAG_TO_END, but will stop with
    // `goto finished` if not (EVAL_FLAG_TO_END).
    //
    // Fall back on word-like "dispatch" even if f->gotten is null (unset or
    // unbound word).  It'll be an error, but that code path raises it for us.

    if (
        not f->gotten // note that only ACTIONs have CELL_FLAG_ENFIXED
        or NOT_CELL_FLAG(VAL(f->gotten), ENFIXED)
    ){
      lookback_quote_too_late:; // run as if starting new expression

        Dampen_Lookahead(f);

        if (NOT_EVAL_FLAG(f, TO_END)) {
            //
            // Since it's a new expression, EVALUATE doesn't want to run it
            // even if invisible, as it's not completely invisible (enfixed)
            //
            goto finished;
        }

        if (
            f->gotten
            and IS_ACTION(VAL(f->gotten))
            and GET_ACTION_FLAG(VAL_ACTION(f->gotten), IS_INVISIBLE)
        ){
            // Even if not EVALUATE, we do not want START_NEW_EXPRESSION on
            // "invisible" functions.  e.g. `do [1 + 2 comment "hi"]` should
            // consider that one whole expression.  Reason being that the
            // comment cannot be broken out and thought of as having a return
            // result... `comment "hi"` alone cannot have any basis for
            // evaluating to 3.
        }
        else {
            START_NEW_EXPRESSION_MAY_THROW(f, goto return_thrown);
            // ^-- resets local tick, corrupts f->out, Ctrl-C may abort

            UPDATE_TICK_DEBUG(nullptr);
            // v-- The TICK_BREAKPOINT or C-DEBUG-BREAK landing spot --v
        }

        current_gotten = f->gotten; // if nullptr, the word will error
        Fetch_Next_In_Frame(&current, f);

        // Were we to jump to the REB_WORD switch case here, LENGTH would
        // cause an error in the expression below:
        //
        //     if true [] length of "hello"
        //
        // `reevaluate` accounts for the extra lookahead of after something
        // like IF TRUE [], where you have a case that even though LENGTH
        // isn't enfix itself, enfix accounting must be done by looking ahead
        // to see if something after it (like OF) is enfix and quotes back!
        //
        goto reevaluate;
    }

//=//// IT'S A WORD ENFIXEDLY TIED TO A FUNCTION (MAY BE "INVISIBLE") /////=//

    if (GET_ACTION_FLAG(VAL_ACTION(f->gotten), QUOTES_FIRST)) {
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

        REBVAL *first = First_Unspecialized_Param(VAL_ACTION(f->gotten));
        if (VAL_PARAM_CLASS(first) == REB_P_SOFT_QUOTE) {
            if (GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)) {
                CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
                goto finished;
            }
        }
        else
            goto lookback_quote_too_late;
    }

    if (
        GET_EVAL_FLAG(f, FULFILLING_ARG)
        and not (
            GET_ACTION_FLAG(VAL_ACTION(f->gotten), DEFERS_LOOKBACK)
                                       // ^-- `1 + if false [2] else [3]` => 4
            or GET_ACTION_FLAG(VAL_ACTION(f->gotten), IS_INVISIBLE)
                                       // ^-- `1 + 2 + comment "foo" 3` => 6
        )
    ){
        if (Dampen_Lookahead(f)) {
            assert(NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX));
            SET_FEED_FLAG(f->feed, DEFERRING_ENFIX);

            // Don't do enfix lookahead if asked *not* to look.  See the
            // REB_P_TIGHT parameter convention for the use of this, as
            // well as it being set if EVAL_FLAG_TO_END wants to clear out the
            // invisibles at this frame level before returning.

            goto finished;
        }
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
            GET_ACTION_FLAG(VAL_ACTION(f->gotten), POSTPONES_ENTIRELY)
            or (
                GET_ACTION_FLAG(VAL_ACTION(f->gotten), DEFERS_LOOKBACK)
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

        if (GET_ACTION_FLAG(VAL_ACTION(f->gotten), POSTPONES_ENTIRELY))
            SET_CELL_FLAG(f->out, OUT_MARKED_STALE);

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

  push_enfix_action:;

    Push_Action(f, VAL_ACTION(f->gotten), VAL_BINDING(f->gotten));

    Dampen_Lookahead(f); // not until after action pushed, invisibles cache it

    // Note we jump here from reevaluate, with no label, ATM
    //
    Begin_Action(
        f,
        f->value != VOID_VALUE ? VAL_WORD_SPELLING(f->value) : nullptr
    );

    SET_EVAL_FLAG(f, RUNNING_ENFIX);
    SET_EVAL_FLAG(f, NEXT_ARG_FROM_OUT);

    Fetch_Next_In_Frame(nullptr, f); // advances f->value
    goto process_action;

  abort_action:;

    Drop_Action(f);
    DS_DROP_TO(f->dsp_orig); // any unprocessed refinements or chains on stack

  return_thrown:;

    threw = true;

  finished:;

    assert(Is_Evaluator_Throwing_Debug() == threw);

    // The unevaluated flag is meaningless outside of arguments to functions.

    if (NOT_EVAL_FLAG(f, FULFILLING_ARG))
        f->out->header.bits &= ~CELL_FLAG_UNEVALUATED; // may be an END cell

    if (NOT_EVAL_FLAG(f, PRESERVE_STALE))
        f->out->header.bits &= ~CELL_FLAG_OUT_MARKED_STALE; // may be END
    else {
        // Most clients would prefer not to read the stale flag, and be
        // burdened with clearing it (can't be present on frame output).
        // But argument fulfillment *can't* read it (ARG_MARKED_CHECKED and
        // OUT_MARKED_STALE are the same bit)...but it doesn't need to,
        // since it always starts END.
        //
        assert(NOT_EVAL_FLAG(f, FULFILLING_ARG));
    }

  #if !defined(NDEBUG)
    Eval_Core_Exit_Checks_Debug(f); // will get called unless a fail() longjmps
  #endif

    return threw; // most callers should inspect for IS_END(f->value)
}
