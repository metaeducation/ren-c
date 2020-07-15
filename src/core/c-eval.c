//
//  File: %c-eval.c
//  Summary: "Central Interpreter Evaluator"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// behavior, and there are DO_FLAG_XXX for controlling other behaviors.
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
    //
    // Callers can "lie" to make the dispatch a no-op by substituting the
    // "Dummy" native in the frame, even though it doesn't match the args,
    // in order to build the frame of a function without running it.  This
    // is one of the few places tolerant of the lie...hence _OR_DUMMY()
    //
    return ACT_DISPATCHER(FRM_PHASE_OR_DUMMY(f))(f);
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

    f->out->header.bits |= OUT_MARKED_STALE;
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
// (Note: DO_FLAG_EXPLICIT_EVALUATE is same bit as VALUE_FLAG_EVAL_FLIP)
//
#define EVALUATING(v) \
    ((f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE) \
        == ((v)->header.bits & VALUE_FLAG_EVAL_FLIP))


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


// Typechecking has to be broken out into a subroutine because it is not
// always the case that one is typechecking the current argument.  See the
// documentation on REBFRM.deferred for why.
//
// It's called "Finalize" because in addition to checking, any other handling
// that an argument needs once being put into a frame is handled.  VARARGS!,
// for instance, that may come from an APPLY need to have their linkage
// updated to the parameter they are now being used in.
//
inline static void Finalize_Arg(
    REBFRM *f_state, // name helps avoid accidental references to f->arg, etc.
    const RELVAL *param,
    REBVAL *arg,
    REBVAL *refine
){
    if (IS_END(arg)) {

        // This is a legal result for COMMENT in `do [1 + comment "foo"]`.
        // No different from `do [1 +]`, where Eval_Core_Throws() gives END.

        if (not Is_Param_Endable(param))
            fail (Error_No_Arg(f_state, param));

        Init_Endish_Nulled(arg);
        SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
        return;
    }

  #if defined(DEBUG_STALE_ARGS) // see notes on flag definition
    assert(NOT_VAL_FLAG(arg, ARG_MARKED_CHECKED));
  #endif

    assert(
        refine == ORDINARY_ARG // check arg type
        or refine == LOOKBACK_ARG // check arg type
        or refine == ARG_TO_UNUSED_REFINEMENT // ensure arg null
        or refine == ARG_TO_REVOKED_REFINEMENT // ensure arg null
        or IS_REFINEMENT(refine) // ensure arg not null
    );

    if (IS_NULLED(arg)) {
        if (IS_REFINEMENT(refine)) {
            //
            // We can only revoke the refinement if this is the 1st
            // refinement arg.  If it's a later arg, then the first
            // didn't trigger revocation, or refine wouldn't be logic.
            //
            if (refine + 1 != arg)
                fail (Error_Bad_Refine_Revoke(param, arg));

            Init_Blank(refine); // can't re-enable...
            SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);

            refine = ARG_TO_REVOKED_REFINEMENT;
            return; // don't type check for optionality
        }

        if (IS_FALSEY(refine)) {
            //
            // BLANK! means refinement already revoked, null is okay
            // false means refinement was never in use, so also okay
            //
            SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
            return;
        }

        // fall through to check arg for if <opt> is ok
        //
        assert(refine == ORDINARY_ARG or refine == LOOKBACK_ARG);
    }
    else {
        // If the argument is set, then the refinement shouldn't be
        // in a revoked or unused state.
        //
        if (IS_FALSEY(refine))
            fail (Error_Bad_Refine_Revoke(param, arg));
    }

    if (IS_BLANK(arg) and TYPE_CHECK(param, REB_TS_NOOP_IF_BLANK)) {
        SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
        FRM_PHASE_OR_DUMMY(f_state) = PG_Dummy_Action;
        return;
    }

    if (not Is_Param_Variadic(param)) {
        if (TYPE_CHECK(param, VAL_TYPE(arg))) {
            SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
            return;
        }

        fail (Error_Arg_Type(f_state, param, VAL_TYPE(arg)));
    }

    // Varargs are odd, because the type checking doesn't actually check the
    // types inside the parameter--it always has to be a VARARGS!.
    //
    if (not IS_VARARGS(arg))
        fail (Error_Not_Varargs(f_state, param, VAL_TYPE(arg)));

    // While "checking" the variadic argument we actually re-stamp it with
    // this parameter and frame's signature.  It reuses whatever the original
    // data feed was (this frame, another frame, or just an array from MAKE
    // VARARGS!)
    //
    // Store the offset so that both the arg and param locations can
    // be quickly recovered, while using only a single slot in the REBVAL.
    //
    arg->payload.varargs.param_offset = arg - FRM_ARGS_HEAD(f_state);
    if (FRM_PHASE_OR_DUMMY(f_state) == PG_Dummy_Action) {
        //
        // If the function is not going to be run immediately, it might be
        // getting deferred just for capturing arguments before running (e.g.
        // with `match even? x`) or it could be a means of generating a
        // specialization to be used many times (`does dump var`).  The
        // former case might have variadics work, the latter can't.  Let
        // frame expiration or not be the judge later.
        //
        arg->payload.varargs.phase = f_state->original;
    }
    else
        arg->payload.varargs.phase = FRM_PHASE(f_state);
    SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
}

inline static void Finalize_Current_Arg(REBFRM *f) {
    Finalize_Arg(f, f->param, f->arg, f->refine);
}


// !!! Somewhat hacky mechanism for getting the first argument of an action,
// used when doing typechecks for Is_Param_Skippable() on functions that
// quote their first argument.  Must take into account specialization, as
// that may have changed the first actual parameter to something other than
// the first paramlist parameter.
//
// Despite being implemented less elegantly than it should be, this is an
// important feature, since it's how `case [true [a] default [b]]` gets the
// enfixed DEFAULT function to realize the left side is a BLOCK! and not
// either a SET-WORD! or a SET-PATH!, so it <skip>s the opportunity to hard
// quote it and defers execution...in this case, meaning it won't run at all.
//
inline static void Seek_First_Param(REBFRM *f, REBACT *action) {
    f->param = ACT_PARAMS_HEAD(action);
    f->special = ACT_SPECIALTY_HEAD(action);
    for (; NOT_END(f->param); ++f->param, ++f->special) {
        if (
            f->special != f->param
            and GET_VAL_FLAG(f->special, ARG_MARKED_CHECKED)
        ){
            continue;
        }
        if (VAL_PARAM_CLASS(f->param) == PARAM_CLASS_LOCAL)
            continue;
        return;
    }
    fail ("Seek_First_Param() failed");
}


#ifdef DEBUG_EXPIRED_LOOKBACK
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (f->fake_lookback != nullptr)
#else
    #define CURRENT_CHANGES_IF_FETCH_NEXT \
        (current == FRM_CELL(f))
#endif


inline static void Expire_Out_Cell_Unless_Invisible(REBFRM *f) {
    REBACT *phase = FRM_PHASE_OR_DUMMY(f);
    if (phase != PG_Dummy_Action)
        if (GET_ACT_FLAG(phase, ACTION_FLAG_INVISIBLE)) {
            if (not GET_ACT_FLAG(f->original, ACTION_FLAG_INVISIBLE))
                fail ("All invisible action phases must be invisible");
            return;
        }

    if (GET_ACT_FLAG(f->original, ACTION_FLAG_INVISIBLE))
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
    if (not GET_ACT_FLAG(FRM_PHASE_OR_DUMMY(f), ACTION_FLAG_INVISIBLE)) {
        if (SPORADICALLY(2))
            Init_Unreadable_Blank(f->out);
        else
            SET_END(f->out);
        f->out->header.bits |= OUT_MARKED_STALE;
    }
  #endif
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
//     f->source
//     Contains the REBARR* or C va_list of subsequent values to fetch.
//
//     f->specifier
//     Resolver for bindings of values in f->source, SPECIFIED if all resolved
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

  #if defined(DEBUG_COUNT_TICKS)
    REBTCK tick = f->tick = TG_Tick; // snapshot start tick
  #endif

    assert(DSP >= f->dsp_orig); // REDUCE accrues, APPLY adds refinements, >=
    assert(not IS_TRASH_DEBUG(f->out)); // all invisibles preserves output
    assert(f->out != FRM_CELL(f)); // overwritten by temporary calculations

    // Caching VAL_TYPE_RAW(f->value) in a local can make a slight performance
    // difference, though how much depends on what the optimizer figures out.
    // Either way, it's useful to have handy in the debugger.
    //
    enum Reb_Kind eval_type;

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
        DO_FLAG_POST_SWITCH
        | DO_FLAG_PROCESS_ACTION
        | DO_FLAG_REEVALUATE_CELL
    )){
        if (f->flags.bits & DO_FLAG_POST_SWITCH) {
            assert(f->prior->u.defer.arg); // !!! EVAL-ENFIX crudely preserves
            assert(NOT_END(f->out));

            f->flags.bits &= ~DO_FLAG_POST_SWITCH;
            goto post_switch;
        }

        if (f->flags.bits & DO_FLAG_PROCESS_ACTION) {
            assert(f->refine == ORDINARY_ARG); // !!! should APPLY do enfix?

            f->out->header.bits |= OUT_MARKED_STALE;

            f->flags.bits &= ~DO_FLAG_PROCESS_ACTION;
            goto process_action;
        }

        current = f->u.reval.value;
        TRASH_POINTER_IF_DEBUG(f->u.defer.arg); // same memory location
        current_gotten = nullptr;
        eval_type = VAL_TYPE(current);

        f->flags.bits &= ~DO_FLAG_REEVALUATE_CELL;
        goto reevaluate;
    }

    eval_type = VAL_TYPE_RAW(f->value);

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

    assert(eval_type != REB_0_END and eval_type == VAL_TYPE_RAW(current));

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

    if (VAL_TYPE_RAW(f->value) != REB_WORD) // END would be REB_0
        goto give_up_backward_quote_priority;

    if (not EVALUATING(f->value))
        goto give_up_backward_quote_priority;

    assert(not f->gotten); // Fetch_Next_In_Frame() cleared it
    f->gotten = Try_Get_Opt_Var(f->value, f->specifier);
    if (not f->gotten or NOT_VAL_FLAG(f->gotten, VALUE_FLAG_ENFIXED))
        goto give_up_backward_quote_priority;

    // SHOVE says it quotes its left argument, even if it doesn't know that
    // is what it ultimately wants...because it wants a shot at its most
    // aggressive scenario.  Once it finds out the enfixee wants normal or
    // tight, though, it could get in trouble.
    //
    if (VAL_ACTION(f->gotten) == NAT_ACTION(shove)) {
        Fetch_Next_In_Frame(nullptr, f);
        if (IS_END(f->value))
            goto finished; // proposed behavior, drop out result...

        Prep_Stack_Cell(FRM_SHOVE(f));

        REBSTR *opt_label = nullptr;
        if (IS_WORD(f->value) or IS_PATH(f->value)) {
            //
            // We've only got one shot for the value.  If we don't push the
            // refinements here, we'll lose them.  Start by biting the
            // bullet and letting it synthesize a specialization (?)
            //
            if (Get_If_Word_Or_Path_Throws(
                FRM_SHOVE(f),
                &opt_label,
                f->value,
                f->specifier,
                false // ok, crazypants, don't push refinements (?)
            )){
                Move_Value(f->out, FRM_SHOVE(f));
                goto return_thrown;
            }
        }
        else if (IS_GROUP(f->value)) {
            REBIXO indexor = Eval_Array_At_Core(
                SET_END(FRM_SHOVE(f)),
                nullptr, // opt_first (null means nothing, not nulled cell)
                VAL_ARRAY(f->value),
                VAL_INDEX(f->value),
                Derive_Specifier(f->specifier, f->value),
                DO_FLAG_TO_END
            );
            if (indexor == THROWN_FLAG) {
                Move_Value(f->out, FRM_SHOVE(f));
                goto return_thrown;
            }
            if (IS_END(FRM_SHOVE(f))) // !!! need SHOVE frame for type error
                fail ("GROUP! passed to SHOVE did not evaluate to content");
        }
        else if (IS_ACTION(f->value)) {
            Move_Value(FRM_SHOVE(f), KNOWN(f->value));
        }
        else
            fail ("SHOVE only accepts WORD!, PATH!, GROUP!, or ACTION!");

        // Even if the function isn't enfix, say it is.  This permits things
        // like `5 + 5 -> subtract 7` to give 3.
        //
        SET_VAL_FLAG(FRM_SHOVE(f), VALUE_FLAG_ENFIXED);
        f->gotten = FRM_SHOVE(f);
    }

    // It's known to be an ACTION! since only actions can be enfix...
    //
    if (NOT_VAL_FLAG(f->gotten, ACTION_FLAG_QUOTES_FIRST_ARG))
        goto give_up_backward_quote_priority;

    // It's a backward quoter!  But...before allowing it to try, first give an
    // operation on the left which quotes to the right priority.  So:
    //
    //     foo: quote => [print quote]
    //
    // Would be interpreted as:
    //
    //     foo: (quote =>) [print quote]
    //
    // This is a good argument for not making enfixed operations that
    // hard-quote things that can dispatch functions.  A soft-quote would give
    // more flexibility to override the left hand side's precedence:
    //
    //     foo: ('quote) => [print quote]

    if (eval_type == REB_WORD and EVALUATING(current)) {
        if (not current_gotten)
            current_gotten = Try_Get_Opt_Var(current, f->specifier);
        else
            assert(
                current_gotten == Try_Get_Opt_Var(current, f->specifier)
            );

        if (
            current_gotten
            and IS_ACTION(current_gotten)
            and NOT_VAL_FLAG(current_gotten, VALUE_FLAG_ENFIXED)
            and GET_VAL_FLAG(current_gotten, ACTION_FLAG_QUOTES_FIRST_ARG)
        ){
            Seek_First_Param(f, VAL_ACTION(current_gotten));
            if (Is_Param_Skippable(f->param))
                if (not TYPE_CHECK(f->param, VAL_TYPE(f->value)))
                    goto give_up_forward_quote_priority;

            goto give_up_backward_quote_priority;
        }
        goto give_up_forward_quote_priority;
    }

    if (eval_type == REB_PATH and EVALUATING(current)) {
        //
        // !!! Words aren't the only way that functions can be dispatched,
        // one can also use paths.  It gets tricky here, because path GETs
        // are dodgier than word fetches.  Not only can it have GROUP!s and
        // have side effects to "examining" what it looks up to, but there are
        // other implications.
        //
        // As a temporary workaround to make HELP/DOC DEFAULT work, where
        // DEFAULT hard quotes left, we have to recognize that path as a
        // function call which quotes its first argument...so splice in some
        // handling here that peeks at the head of the path and sees if it
        // applies.  Note this is very brittle, and can be broken as easily as
        // saying `o: make object! [h: help]` and then `o/h/doc default`.
        //
        // There are ideas on the table for how to remedy this long term.
        // For now, see comments in the WORD branch above for more details.
        //
        if (
            VAL_LEN_AT(current) > 0
            and IS_WORD(VAL_ARRAY_AT(current))
        ){
            assert(not current_gotten); // no caching for paths

            REBSPC *derived = Derive_Specifier(f->specifier, current);

            RELVAL *path_at = VAL_ARRAY_AT(current);
            const REBVAL *var_at = Try_Get_Opt_Var(path_at, derived);

            if (
                var_at
                and IS_ACTION(var_at)
                and NOT_VAL_FLAG(var_at, VALUE_FLAG_ENFIXED)
                and GET_VAL_FLAG(var_at, ACTION_FLAG_QUOTES_FIRST_ARG)
            ){
                goto give_up_backward_quote_priority;
            }
        }
        goto give_up_forward_quote_priority;
    }

    if (eval_type == REB_ACTION and EVALUATING(current)) {
        //
        // A literal ACTION! in a BLOCK! may also forward quote
        //
        assert(NOT_VAL_FLAG(current, VALUE_FLAG_ENFIXED)); // not WORD!/PATH!
        if (GET_VAL_FLAG(current, ACTION_FLAG_QUOTES_FIRST_ARG))
            goto give_up_backward_quote_priority;
    }

  give_up_forward_quote_priority:

    // Okay, right quoting left wins out!  But if its parameter is <skip>able,
    // let it voluntarily opt out of it the type doesn't match its interests.

    Seek_First_Param(f, VAL_ACTION(f->gotten));
    if (Is_Param_Skippable(f->param))
        if (not TYPE_CHECK(f->param, VAL_TYPE(current)))
            goto give_up_backward_quote_priority;

    Push_Action(f, VAL_ACTION(f->gotten), VAL_BINDING(f->gotten));
    Begin_Action(f, VAL_WORD_SPELLING(f->value), LOOKBACK_ARG);

    // Lookback args are fetched from f->out, then copied into an arg
    // slot.  Put the backwards quoted value into f->out, and in the
    // debug build annotate it with the unevaluated flag, to indicate
    // the lookback value was quoted, for some double-check tests.
    //
    Derelativize(f->out, current, f->specifier); // lookback in f->out
  #if !defined(NDEBUG)
    SET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
  #endif

    Fetch_Next_In_Frame(nullptr, f); // skip the WORD! that invoked the action
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

    assert(eval_type == VAL_TYPE_RAW(current));

    switch (eval_type) {

      case REB_0_END:
        goto finished;

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
        assert(NOT_VAL_FLAG(current, VALUE_FLAG_ENFIXED)); // WORD!/PATH! only

        if (not EVALUATING(current))
            goto inert;

        REBSTR *opt_label = nullptr; // not invoked through a word, "nameless"

        Push_Action(f, VAL_ACTION(current), VAL_BINDING(current));
        Begin_Action(f, opt_label, ORDINARY_ARG);
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
        assert(f->refine == LOOKBACK_ARG or f->refine == ORDINARY_ARG);

        TRASH_POINTER_IF_DEBUG(current); // shouldn't be used below
        TRASH_POINTER_IF_DEBUG(current_gotten);

        f->flags.bits &= ~DO_FLAG_DOING_PICKUPS;

      process_args_for_pickup_or_to_end:;

        for (; NOT_END(f->param); ++f->param, ++f->arg, ++f->special) {
            enum Reb_Param_Class pclass = VAL_PARAM_CLASS(f->param);

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
            if (
                not (f->flags.bits & DO_FLAG_DOING_PICKUPS)
                and f->special != f->arg
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
                SET_VAL_FLAG(f->arg, CELL_FLAG_STACK);
            }

            assert(f->arg->header.bits & NODE_FLAG_CELL);
            assert(f->arg->header.bits & CELL_FLAG_STACK);

    //=//// A /REFINEMENT ARG /////////////////////////////////////////////=//

            // Refinements are checked first for a reason.  This is to
            // short-circuit based on DO_FLAG_DOING_PICKUPS before redoing
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

            if (pclass == PARAM_CLASS_REFINEMENT) {
                if (f->flags.bits & DO_FLAG_DOING_PICKUPS) {
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
                    assert(NOT_VAL_FLAG(f->special, ARG_MARKED_CHECKED));
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
                if (NOT_VAL_FLAG(f->special, ARG_MARKED_CHECKED)) {
                    if (IS_FALSEY(f->special)) // !!! error on void, needed?
                        goto unused_refinement;

                    f->refine = f->arg; // remember, as we might revoke!
                    goto used_refinement;
                }

                if (IS_REFINEMENT(f->special)) {
                    assert(
                        VAL_WORD_SPELLING(f->special)
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

                    DS_PUSH_TRASH;
                    Init_Issue(DS_TOP, partial_canon);
                    INIT_BINDING(DS_TOP, f->varlist);
                    DS_TOP->payload.any_word.index = partial_index;

                    f->refine = SKIPPING_REFINEMENT_ARGS;
                    goto used_refinement;
                }

                assert(IS_INTEGER(f->special)); // DO FRAME! leaves these

                assert(f->flags.bits & DO_FLAG_FULLY_SPECIALIZED);
                f->refine = f->arg; // remember so we can revoke!
                goto used_refinement;

    //=//// UNSPECIALIZED REFINEMENT SLOT (no consumption) ////////////////=//

              unspecialized_refinement:;

                if (f->dsp_orig == DSP) // no refinements left on stack
                    goto unused_refinement;

                if (VAL_STORED_CANON(ordered) == param_canon) {
                    DS_DROP; // we're lucky: this was next refinement used
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
                SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;

              used_refinement:;

                assert(not IS_POINTER_TRASH_DEBUG(f->refine)); // must be set
                Init_Refinement(f->arg, VAL_PARAM_SPELLING(f->param));
                SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
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
              case PARAM_CLASS_LOCAL:
                Init_Nulled(f->arg); // !!! f->special?
                SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;

              case PARAM_CLASS_RETURN:
                assert(VAL_PARAM_SYM(f->param) == SYM_RETURN);
                Move_Value(f->arg, NAT_VALUE(return)); // !!! f->special?
                INIT_BINDING(f->arg, f->varlist);
                SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;

              default:
                break;
            }

    //=//// IF COMING BACK TO REFINEMENT ARGS LATER, MOVE ON FOR NOW //////=//

            if (f->refine == SKIPPING_REFINEMENT_ARGS)
                goto skip_this_arg_for_now;

            if (GET_VAL_FLAG(f->special, ARG_MARKED_CHECKED)) {

    //=//// SPECIALIZED OR OTHERWISE TYPECHECKED ARG //////////////////////=//

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
                    (f->refine != ORDINARY_ARG and IS_NULLED(f->special))
                    or TYPE_CHECK(f->param, VAL_TYPE(f->special))
                );

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
                    SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
                }
                goto continue_arg_loop;
            }

            // !!! This is currently a hack for APPLY.  It doesn't do a type
            // checking pass after filling the frame, but it still wants to
            // treat all values (nulls included) as fully specialized.
            //
            if (
                f->arg == f->special // !!! should this ever allow gathering?
                /* f->flags.bits & DO_FLAG_FULLY_SPECIALIZED */
            ){
                Finalize_Current_Arg(f);
                goto continue_arg_loop; // looping to verify args/refines
            }

    //=//// IF UNSPECIALIZED ARG IS INACTIVE, SET NULL AND MOVE ON ////////=//

            // Unspecialized arguments that do not consume do not need any
            // further processing or checking.  null will always be fine.
            //
            if (f->refine == ARG_TO_UNUSED_REFINEMENT) {
                //
                // Overwrite if !(DO_FLAG_FULLY_SPECIALIZED) faster than check
                //
                Init_Nulled(f->arg);
                SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;
            }

    //=//// IF LOOKBACK, THEN USE PREVIOUS EXPRESSION RESULT FOR ARG //////=//

            if (f->refine == LOOKBACK_ARG) {
                //
                // Switch to ordinary arg up front, so gotos below are good to
                // go for the next argument
                //
                f->refine = ORDINARY_ARG;

                if (f->out->header.bits & OUT_MARKED_STALE) {
                    //
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
                        RESET_VAL_HEADER_EXTRA(
                            f->arg,
                            REB_VARARGS,
                            VARARGS_FLAG_ENFIXED // in case anyone cares
                        );
                        INIT_BINDING(f->arg, EMPTY_ARRAY); // feed finished

                        Finalize_Current_Arg(f);
                        goto continue_arg_loop;
                    }

                    // The NODE_FLAG_MARKED flag is also used by BAR! to keep
                    // a result in f->out, so that the barrier doesn't destroy
                    // data in cases like `(1 + 2 | comment "hi")` => 3, but
                    // left enfix should treat that just like an end.
                    //
                    if (not Is_Param_Endable(f->param))
                        fail (Error_No_Arg(f, f->param));

                    Init_Endish_Nulled(f->arg);
                    SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
                    goto continue_arg_loop;
                }

                // The argument might be variadic, but even if it is we only
                // have one argument to be taken from the left.  So start by
                // calculating that one value into f->arg.
                //
                // !!! See notes on potential semantics problem below.

                switch (pclass) {
                  case PARAM_CLASS_NORMAL:
                    Move_Value(f->arg, f->out);
                    if (GET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED))
                        SET_VAL_FLAG(f->arg, VALUE_FLAG_UNEVALUATED);
                    break;

                  case PARAM_CLASS_TIGHT:
                    Move_Value(f->arg, f->out);
                    if (GET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED))
                        SET_VAL_FLAG(f->arg, VALUE_FLAG_UNEVALUATED);
                    break;

                  case PARAM_CLASS_HARD_QUOTE:
                  #if !defined(NDEBUG)
                    //
                    // Only in debug builds, the before-switch lookahead sets
                    // this flag to help indicate that's where it came from.
                    //
                    assert(GET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
                  #endif

                    // Is_Param_Skippable() accounted for in pre-lookback

                    Move_Value(f->arg, f->out);
                    SET_VAL_FLAG(f->arg, VALUE_FLAG_UNEVALUATED);
                    break;

                  case PARAM_CLASS_SOFT_QUOTE:
                  #if !defined(NDEBUG)
                    //
                    // Only in debug builds, the before-switch lookahead sets
                    // this flag to help indicate that's where it came from.
                    //
                    assert(GET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
                  #endif

                    if (IS_QUOTABLY_SOFT(f->out)) {
                        if (Eval_Value_Throws(f->arg, f->out)) {
                            Move_Value(f->out, f->arg);
                            goto abort_action;
                        }
                    }
                    else if (IS_BAR(f->out)) {
                        //
                        // Hard quotes take BAR!s but they should look like an
                        // <end> to a soft quote.
                        //
                        SET_END(f->arg);
                    }
                    else {
                        Move_Value(f->arg, f->out);
                        SET_VAL_FLAG(f->arg, VALUE_FLAG_UNEVALUATED);
                    }
                    break;

                  default:
                    assert(false);
                }

                Expire_Out_Cell_Unless_Invisible(f);

                // Now that we've gotten the argument figured out, make a
                // singular array to feed it to the variadic.
                //
                // !!! See notes on VARARGS_FLAG_ENFIXED about how this is
                // somewhat shady, as any evaluations happen *before* the
                // TAKE on the VARARGS.  Experimental feature.
                //
                if (Is_Param_Variadic(f->param)) {
                    REBARR *array1;
                    if (IS_END(f->arg))
                        array1 = EMPTY_ARRAY;
                    else {
                        REBARR *feed = Alloc_Singular(NODE_FLAG_MANAGED);
                        Move_Value(ARR_SINGLE(feed), f->arg);

                        array1 = Alloc_Singular(NODE_FLAG_MANAGED);
                        Init_Block(ARR_SINGLE(array1), feed); // index 0
                    }

                    RESET_VAL_HEADER_EXTRA(
                        f->arg,
                        REB_VARARGS,
                        VARARGS_FLAG_ENFIXED // don't evaluate *again* on TAKE
                    );
                    INIT_BINDING(f->arg, array1);
                }

                Finalize_Current_Arg(f);
                goto continue_arg_loop;
            }

    //=//// VARIADIC ARG (doesn't consume anything *yet*) /////////////////=//

            // Evaluation argument "hook" parameters (marked in MAKE ACTION!
            // by a `[[]]` in the spec, and in FUNC by `<...>`).  They point
            // back to this call through a reified FRAME!, and are able to
            // consume additional arguments during the function run.
            //
            if (Is_Param_Variadic(f->param)) {
                RESET_CELL(f->arg, REB_VARARGS);
                INIT_BINDING(f->arg, f->varlist); // frame-based VARARGS!

                Finalize_Current_Arg(f); // sets VARARGS! offset and paramlist
                goto continue_arg_loop;
            }

    //=//// AFTER THIS, PARAMS CONSUME FROM CALLSITE IF NOT APPLY ////////=//

            assert(f->refine == ORDINARY_ARG or IS_REFINEMENT(f->refine));

    //=//// START BY HANDLING ANY DEFERRED ENFIX PROCESSING //////////////=//

            // `if 10 and (20) [...]` starts by filling IF's `condition` slot
            // with 10, because AND has a "non-tight" (normal) left hand
            // argument.  Were `if 10` a complete expression, that's allowed.
            //
            // But now we're consuming another argument at the callsite, e.g.
            // the `branch`.  So by definition `if 10` wasn't finished.
            //
            // We kept a `f->defer` field that points at the previous filled
            // slot.  So we can re-enter a sub-frame and give the IF's
            // `condition` slot a second chance to run the enfix processing it
            // put off before, this time using the 10 as AND's left-hand arg.
            //
            if (f->u.defer.arg) {
                REBFLGS flags =
                    DO_FLAG_FULFILLING_ARG
                    | (f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);

                DECLARE_SUBFRAME (child, f); // capture DSP *now*

                if (Is_Frame_Gotten_Shoved(f)) {
                    Prep_Stack_Cell(FRM_SHOVE(child));
                    Move_Value(FRM_SHOVE(child), f->gotten);
                    SET_VAL_FLAG(FRM_SHOVE(child), VALUE_FLAG_ENFIXED);
                    f->gotten = FRM_SHOVE(child);
                }

                if (Eval_Step_In_Subframe_Throws(
                    f->u.defer.arg, // preload previous f->arg as left enfix
                    f,
                    flags | DO_FLAG_POST_SWITCH,
                    child
                )){
                    Move_Value(f->out, f->u.defer.arg);
                    goto abort_action;
                }

                Finalize_Arg(
                    f,
                    f->u.defer.param,
                    f->u.defer.arg,
                    f->u.defer.refine
                );

                f->u.defer.arg = nullptr;
                TRASH_POINTER_IF_DEBUG(f->u.defer.param);
                TRASH_POINTER_IF_DEBUG(f->u.defer.refine);
            }

    //=//// ERROR ON END MARKER, BAR! IF APPLICABLE //////////////////////=//

            if (IS_END(f->value) or (f->flags.bits & DO_FLAG_BARRIER_HIT)) {
                if (not Is_Param_Endable(f->param))
                    fail (Error_No_Arg(f, f->param));

                Init_Endish_Nulled(f->arg);
                SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;
            }

            switch (pclass) {

   //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes 1 EVALUATE's worth) ////=//

              case PARAM_CLASS_NORMAL: {
                REBFLGS flags = DO_FLAG_FULFILLING_ARG
                    | (f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);

                DECLARE_SUBFRAME (child, f); // capture DSP *now*
                SET_END(f->arg); // Finalize_Arg() sets to Endish_Nulled
                if (Eval_Step_In_Subframe_Throws(f->arg, f, flags, child)) {
                    Move_Value(f->out, f->arg);
                    goto abort_action;
                }
                break; }

              case PARAM_CLASS_TIGHT: {
                //
                // PARAM_CLASS_NORMAL does "normal" normal infix lookahead,
                // e.g. `square 1 + 2` would pass 3 to single-arity `square`.
                // But if the argument to square is declared #tight, it will
                // act as `(square 1) + 2`, by not applying lookahead to see
                // the `+` during the argument evaluation.
                //
                REBFLGS flags =
                    DO_FLAG_NO_LOOKAHEAD
                    | DO_FLAG_FULFILLING_ARG
                    | (f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);

                DECLARE_SUBFRAME (child, f);
                SET_END(f->arg); // Finalize_Arg() sets to Endish_Nulled
                if (Eval_Step_In_Subframe_Throws(f->arg, f, flags, child)) {
                    Move_Value(f->out, f->arg);
                    goto abort_action;
                }
                break; }

    //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG /////////////////////////////=//

              case PARAM_CLASS_HARD_QUOTE:
                if (Is_Param_Skippable(f->param)) {
                    if (not TYPE_CHECK(f->param, VAL_TYPE(f->value))) {
                        assert(Is_Param_Endable(f->param));
                        Init_Endish_Nulled(f->arg); // not DO_FLAG_BARRIER_HIT
                        SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
                        goto continue_arg_loop;
                    }
                    Quote_Next_In_Frame(f->arg, f);
                    SET_VAL_FLAGS(
                        f->arg,
                        ARG_MARKED_CHECKED | VALUE_FLAG_UNEVALUATED
                    );
                    goto continue_arg_loop;
                }
                Quote_Next_In_Frame(f->arg, f); // has VALUE_FLAG_UNEVALUATED
                break;

    //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  ////////////////////////////=//

              case PARAM_CLASS_SOFT_QUOTE:
                if (IS_BAR(f->value)) { // BAR! stops a soft quote
                    f->flags.bits |= DO_FLAG_BARRIER_HIT;
                    Fetch_Next_In_Frame(nullptr, f);
                    SET_END(f->arg);
                    Finalize_Current_Arg(f);
                    goto continue_arg_loop;
                }

                if (not IS_QUOTABLY_SOFT(f->value)) {
                    Quote_Next_In_Frame(f->arg, f); // VALUE_FLAG_UNEVALUATED
                    Finalize_Current_Arg(f);
                    goto continue_arg_loop;
                }

                if (Eval_Value_Core_Throws(f->arg, f->value, f->specifier)) {
                    Move_Value(f->out, f->arg);
                    goto abort_action;
                }

                Fetch_Next_In_Frame(nullptr, f);
                break;

              default:
                assert(false);
            }

    //=//// TYPE CHECKING FOR (MOST) ARGS AT END OF ARG LOOP //////////////=//

            // Some arguments can be fulfilled and skip type checking or
            // take care of it themselves.  But normal args pass through
            // this code which checks the typeset and also handles it when
            // a void arg signals the revocation of a refinement usage.

            assert(pclass != PARAM_CLASS_REFINEMENT);
            assert(pclass != PARAM_CLASS_LOCAL);
            assert(
                not In_Typecheck_Mode(f) // already handled, unless...
                or not (f->flags.bits & DO_FLAG_FULLY_SPECIALIZED) // ...this!
            );

            assert(not IS_POINTER_TRASH_DEBUG(f->u.defer.arg));
            if (f->u.defer.arg)
                continue; // don't do typechecking on this *yet*...

            Finalize_Arg(f, f->param, f->arg, f->refine);

          continue_arg_loop:;

            assert(GET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED));
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
                CHANGE_VAL_TYPE_BITS(DS_TOP, REB_REFINEMENT);
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
                    VAL_WORD_SPELLING(f->refine)
                    == VAL_PARAM_SPELLING(f->param - 1)
                )
            );

            assert(VAL_STORED_CANON(DS_TOP) == VAL_PARAM_CANON(f->param - 1));
            assert(VAL_PARAM_CLASS(f->param - 1) == PARAM_CLASS_REFINEMENT);

            DS_DROP;
            f->flags.bits |= DO_FLAG_DOING_PICKUPS;
            goto process_args_for_pickup_or_to_end;
        }

      arg_loop_and_any_pickups_done:;

        assert(IS_END(f->param)); // signals !Is_Action_Frame_Fulfilling()

        if (not In_Typecheck_Mode(f)) { // was fulfilling...
            assert(not IS_POINTER_TRASH_DEBUG(f->u.defer.arg));
            if (f->u.defer.arg) {
                //
                // We deferred typechecking, but still need to do it...
                //
                Finalize_Arg(
                    f,
                    f->u.defer.param,
                    f->u.defer.arg,
                    f->u.defer.refine
                );
                TRASH_POINTER_IF_DEBUG(f->u.defer.param);
                TRASH_POINTER_IF_DEBUG(f->u.defer.refine);
            }
            TRASH_POINTER_IF_DEBUG(f->u.defer.arg);
        }

    //==////////////////////////////////////////////////////////////////==//
    //
    // ACTION! ARGUMENTS NOW GATHERED, DISPATCH PHASE
    //
    //==////////////////////////////////////////////////////////////////==//

      redo_unchecked:;

        assert(IS_END(f->param));
        // refine can be anything.
        assert(
            IS_END(f->value)
            or FRM_IS_VALIST(f)
            or IS_VALUE_IN_ARRAY_DEBUG(f->source->array, f->value)
        );

        Expire_Out_Cell_Unless_Invisible(f);
        assert(IS_POINTER_TRASH_DEBUG(f->u.defer.arg));

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
        /*assert(f->out->header.bits & (CELL_FLAG_STACK | NODE_FLAG_ROOT)); */

        if (not Is_Frame_Gotten_Shoved(f))
            f->gotten = nullptr; // arbitrary code changes fetched variables

        // Note that the dispatcher may push ACTION! values to the data stack
        // which are used to process the return result after the switch.
        //
        const REBVAL *r; // initialization would be skipped by gotos
        r = (*PG_Dispatcher)(f); // default just calls FRM_PHASE(f)

        if (r == f->out) {
            assert(not (f->out->header.bits & OUT_MARKED_STALE));
            assert(not THROWN(f->out));
        }
        else if (not r) { // API and internal code can both return `nullptr`
            Init_Nulled(f->out);
        }
        else if (VAL_TYPE_RAW(r) <= REB_MAX) { // should be an API value
            Handle_Api_Dispatcher_Result(f, r);
        }
        else switch (VAL_TYPE_RAW(r)) { // it's a "pseudotype" instruction
            //
            // !!! Thrown values used to be indicated with a bit on the value
            // itself, but now it's conveyed through a return value.  This
            // means typical return values don't have to run through a test
            // for if they're thrown or not, but it means Eval_Core has to
            // return a boolean to pass up the state.  It may not be much of
            // a performance win either way, but recovering the bit in the
            // values is a definite advantage--as header bits are scarce!
            //
          case REB_R_THROWN:
            assert(THROWN(f->out));
            if (IS_ACTION(f->out)) {
                if (
                    VAL_ACTION(f->out) == NAT_ACTION(unwind)
                    and VAL_BINDING(f->out) == NOD(f->varlist)
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
                    VAL_ACTION(f->out) == NAT_ACTION(redo)
                    and VAL_BINDING(f->out) == NOD(f->varlist)
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
                        FRM_PHASE(f) != f->out->payload.any_context.phase
                        and did (exemplar = ACT_EXEMPLAR(
                            f->out->payload.any_context.phase
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

                    FRM_PHASE(f) = f->out->payload.any_context.phase;
                    FRM_BINDING(f) = VAL_BINDING(f->out);
                    goto redo_checked;
                }
            }

            // Stay THROWN and let stack levels above try and catch
            //
            goto abort_action;

          case REB_R_REDO:
            //
            // This instruction represents the idea that it is desired to
            // run the f->phase again.  The dispatcher may have changed the
            // value of what f->phase is, for instance.

            if (GET_VAL_FLAG(r, VALUE_FLAG_FALSEY)) // R_REDO_UNCHECKED
                goto redo_unchecked;

          redo_checked:; // R_REDO_CHECKED

            Expire_Out_Cell_Unless_Invisible(f);
            assert(IS_POINTER_TRASH_DEBUG(f->u.defer.arg));

            f->param = ACT_PARAMS_HEAD(FRM_PHASE(f));
            f->arg = FRM_ARGS_HEAD(f);
            f->special = f->arg;
            f->refine = ORDINARY_ARG; // no gathering, but need for assert
            goto process_action;

          case REB_R_INVISIBLE: {
            assert(GET_ACT_FLAG(FRM_PHASE(f), ACTION_FLAG_INVISIBLE));

            // !!! Ideally we would check that f->out hadn't changed, but
            // that would require saving the old value somewhere...

            if (
                not (f->out->header.bits & OUT_MARKED_STALE)
                or IS_END(f->value)
            ){
                goto skip_output_check;
            }

            // If an invisible is at the start of a frame and nothing is
            // after it, it has to retrigger until it finds something (or
            // until it hits the end of the frame).  It should not do a
            // START_NEW_EXPRESSION()...the expression index doesn't update.
            //
            //     do [comment "a" 1] => 1

            current_gotten = f->gotten;
            Fetch_Next_In_Frame(&current, f);
            eval_type = VAL_TYPE(current);

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
            DS_DROP;

            // We use the same mechanism as enfix operations do...give the
            // next chain step its first argument coming from f->out
            //
            // !!! One side effect of this is that unless CHAIN is changed
            // to check, your chains can consume more than one argument.
            // This might be interesting or it might be bugs waiting to
            // happen, trying it out of curiosity for now.
            //
            Begin_Action(f, opt_label, LOOKBACK_ARG);
            goto process_action;
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
        if (not EVALUATING(current))
            goto inert;

        if (not current_gotten)
            current_gotten = Get_Opt_Var_May_Fail(current, f->specifier);

        if (IS_ACTION(current_gotten)) { // before IS_NULLED() is common case
            Push_Action(
                f,
                VAL_ACTION(current_gotten),
                VAL_BINDING(current_gotten)
            );

            // Note: The usual dispatch of enfix functions is not via a
            // REB_WORD in this switch, it's by some code at the end of
            // the switch.  So you only see enfix in cases like `(+ 1 2)`,
            // or after ACTION_FLAG_INVISIBLE e.g. `10 comment "hi" + 20`.
            //
            Begin_Action(
                f,
                VAL_WORD_SPELLING(current), // use word as stack frame label
                GET_VAL_FLAG(current_gotten, VALUE_FLAG_ENFIXED)
                    ? LOOKBACK_ARG
                    : ORDINARY_ARG
            );
            goto process_action;
        }

        if (IS_VOID(current_gotten))  // need `:x` if `x` is unset
            fail (Error_Need_Non_Void_Core(current, f->specifier));

        Move_Value(f->out, current_gotten); // no copy VALUE_FLAG_UNEVALUATED
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
// Note that Ren-C deemed it better to allow NULL and VOID! cells to be
// assigned via SET-WORD! without erroring.  Use ENSURE or NON to check value.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_SET_WORD: {
        if (not EVALUATING(current))
            goto inert;

        if (IS_END(f->value)) // `do [a:]` is illegal
            fail (Error_Need_Non_End_Core(current, f->specifier));

        REBFLGS flags = (f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);

        Init_Void(f->out); // `1 x: comment "hi"` shouldn't set x to 1!

        if (CURRENT_CHANGES_IF_FETCH_NEXT) { // must use new frame
            DECLARE_SUBFRAME(child, f);
            if (Eval_Step_In_Subframe_Throws(f->out, f, flags, child))
                goto return_thrown;
        }
        else {
            if (Eval_Step_Mid_Frame_Throws(f, flags)) // light reuse of `f`
                goto return_thrown;
        }

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
        if (not EVALUATING(current))
            goto inert;

        Move_Opt_Var_May_Fail(f->out, current, f->specifier);
        assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
        break;

//==/////////////////////////////////////////////////////////////////////==//
//
// [LIT-WORD!]
//
// Note we only want to reset the type bits in the header, not the whole
// header--because header bits may contain other flags.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_LIT_WORD:
        if (not EVALUATING(current))
            goto inert;

        Derelativize(f->out, current, f->specifier);
        CHANGE_VAL_TYPE_BITS(f->out, REB_WORD);
        assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
        break;

//==//// INERT WORD AND STRING TYPES /////////////////////////////////////==//

    case REB_REFINEMENT:
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
// Empty groups vaporize, as do ones that only consist of invisibles.  If
// this is not desired, one should use DO or lead with `(void ...)`
//
//     >> 1 + 2 (comment "vaporize")
//     == 3
//
//     >> 1 + () 2
//     == 3

      case REB_GROUP: {
        if (not EVALUATING(current))
            goto inert;

        if (not Is_Frame_Gotten_Shoved(f))
            f->gotten = nullptr; // arbitrary code changes fetched variables

        // Since current may be f->cell, extract properties to reuse it.
        //
        REBARR *array = VAL_ARRAY(current); // array of the GROUP!
        REBCNT index = VAL_INDEX(current); // index may not be @ head
        REBSPC *derived = Derive_Specifier(f->specifier, current);

        // We want `3 = (1 + 2 ()) 4` to not treat the 1 + 2 as "stale", thus
        // skipping it and trying to compare `3 = 4`.  But `3 = () 1 + 2`
        // should consider the empty group stale.
        //
        // Note we might have something like (1 + 2 elide "Hi") that would
        // show up as having the stale bit.
        //
        REBIXO indexor = Eval_Array_At_Core(
            SET_END(FRM_CELL(f)),
            nullptr, // opt_first (null means nothing, not nulled cell)
            array,
            index,
            derived,
            DO_FLAG_TO_END
        );
        if (indexor == THROWN_FLAG) {
            Move_Value(f->out, FRM_CELL(f));
            goto return_thrown;
        }
        if (IS_END(FRM_CELL(f))) {
            current = f->value;
            eval_type = VAL_TYPE_RAW(f->value);
            if (eval_type != REB_0_END) {
                Fetch_Next_In_Frame(nullptr, f); // advances f->value
                goto reevaluate;
            }
            goto finished;
        }

        Move_Value(f->out, FRM_CELL(f)); // no VALUE_FLAG_UNEVALUATED
        break; }

//==//////////////////////////////////////////////////////////////////////==//
//
// [PATH!]
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_PATH: {
        if (not EVALUATING(current))
            goto inert;

        // Length-0 paths look like `/`, and do a special dispatch (currently
        // hacked up to just act as the DIVIDE native, but ultimtely would
        // be another form of dispatch based on the left type...and numbers
        // would use this for division).  This dispatch happens after the
        // switch statement along with enfix, so if we see it here that means
        // there was nothing to the left.
        //
        if (VAL_LEN_AT(current) == 0)
            fail ("Empty path must have left argument for 'split' behavior");

        REBSTR *opt_label;
        if (Eval_Path_Throws_Core(
            f->out,
            &opt_label, // requesting says we run functions (not GET-PATH!)
            VAL_ARRAY(current),
            VAL_INDEX(current),
            Derive_Specifier(f->specifier, current),
            nullptr, // `setval`: null means don't treat as SET-PATH!
            DO_FLAG_PUSH_PATH_REFINEMENTS
        )){
            goto return_thrown;
        }

        if (IS_NULLED_OR_VOID(f->out)) { // need `:x/y` if `y` is unset
            if (IS_NULLED(f->out))
                fail (Error_No_Value_Core(current, f->specifier));
            fail (Error_Need_Non_Void_Core(current, f->specifier));
        }

        if (IS_ACTION(f->out)) {
            //
            // !!! While it is (or would be) possible to fetch an enfix or
            // invisible function from a PATH!, at this point it would be too
            // late in the current scheme...since the lookahead step only
            // honors WORD!.  PATH! support is expected for the future, but
            // requires overhaul of the R3-Alpha path implementation.
            //
            if (ANY_VAL_FLAGS(
                f->out,
                ACTION_FLAG_INVISIBLE | VALUE_FLAG_ENFIXED
            )){
                fail ("Use `->` to shove left enfix operands into PATH!s");
            }

            Push_Action(f, VAL_ACTION(f->out), VAL_BINDING(f->out));

            // !!! Paths are currently never enfixed.  It's a problem which is
            // difficult to do efficiently, as well as introduces questions of
            // running GROUP! in paths twice--once for lookahead, and then
            // possibly once again if the lookahead reported non-enfix.  It's
            // something that really should be made to work *when it can*.
            //
            Begin_Action(f, opt_label, ORDINARY_ARG);
            Expire_Out_Cell_Unless_Invisible(f);
            goto process_action;
        }

        // !!! Usually not true but seems true for path evaluation in varargs,
        // e.g. while running `-- "a" "a"`.  Review.
        //
        CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
        /* assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED)); */
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
// Note that Ren-C deemed it better to allow NULL and VOID! cells to be
// assigned via SET-PATH! without erroring, use ENSURE or NON to check value.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_SET_PATH: {
        if (not EVALUATING(current))
            goto inert;

        if (IS_END(f->value)) // `do [a/b:]` is illegal
            fail (Error_Need_Non_End_Core(current, f->specifier));

        REBFLGS flags = (f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);

        Init_Void(f->out); // `1 o/x: comment "hi"` shouldn't set o/x to 1!

        if (CURRENT_CHANGES_IF_FETCH_NEXT) { // must use new frame
            DECLARE_SUBFRAME(child, f);
            if (Eval_Step_In_Subframe_Throws(f->out, f, flags, child))
                goto return_thrown;
        }
        else {
            if (Eval_Step_Mid_Frame_Throws(f, flags)) // light reuse of `f`
                goto return_thrown;
        }

        if (Eval_Path_Throws_Core(
            FRM_CELL(f), // output if thrown, used as scratch space otherwise
            NULL, // not requesting symbol means refinements not allowed
            VAL_ARRAY(current),
            VAL_INDEX(current),
            f->specifier,
            f->out,
            DO_MASK_NONE // evaluating GROUP!s ok
        )){
            Move_Value(f->out, FRM_CELL(f));
            goto return_thrown;
        }

        assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
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
        if (not EVALUATING(current))
            goto inert;

        if (Get_Path_Throws_Core(f->out, current, f->specifier))
            goto return_thrown;

        // !!! This didn't appear to be true for `-- "hi" "hi"`, processing
        // GET-PATH! of a variadic.  Review if it should be true.
        //
        /* assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED)); */
        CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [LIT-PATH!]
//
// We only set the type, in order to preserve the header bits... (there
// currently aren't any for ANY-PATH!, but there might be someday.)
//
// !!! Aliases a REBSER under two value types, likely bad, see #2233
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_LIT_PATH:
        if (not EVALUATING(current))
            goto inert;

        Derelativize(f->out, current, f->specifier);
        CHANGE_VAL_TYPE_BITS(f->out, REB_PATH);
        assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
        break;

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

      inert:;

        Derelativize(f->out, current, f->specifier);
        SET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [BAR!]
//
// Expression barriers prevent non-hard-quoted operations from picking up
// parameters, e.g. `do [1 | + 2]` is an error.  But they don't erase values,
// so `do [1 + 2 |]` is 3.  In that sense, they are like "invisible" actions.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_BAR:
        if (not EVALUATING(current))
            goto inert;

        if (f->flags.bits & DO_FLAG_FULFILLING_ARG) {
            //
            // May be fulfilling a variadic argument (or an argument to an
            // argument of a variadic, etc.)  Let this appear to give back
            // an END...though if the frame is not at an END then it has
            // more potential evaluation after the current action invocation.
            //
            f->flags.bits |= DO_FLAG_BARRIER_HIT;
            goto finished;
        }

        eval_type = VAL_TYPE_RAW(f->value);
        if (eval_type == REB_0_END)
            goto finished;
        goto do_next; // quickly process next item, no infix test needed

//==//////////////////////////////////////////////////////////////////////==//
//
// [LIT-BAR!]
//
// LIT-BAR! decays into an ordinary BAR! if seen here by the evaluator.
//
// !!! Considerations of the "lit-bit" proposal would add a literal form
// for every type, which would make this datatype unnecssary.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_LIT_BAR:
        if (not EVALUATING(current))
            goto inert;

        Init_Bar(f->out);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [VOID!]
//
// VOID is "evaluatively unfriendly", and unlike NULL is an actual value.
//
//==//////////////////////////////////////////////////////////////////////==//

      case REB_VOID:
        if (not EVALUATING(current))
            goto inert;

        fail ("VOID! cells cannot be evaluated");

//==//////////////////////////////////////////////////////////////////////==//
//
// [NULL]
//
// NULLs are not an ANY-VALUE!.  Usually a DO shouldn't be able to see them.
// An exception is in API calls, such as `rebRun("null?", some_null)`.  That
// is legal due to VALUE_FLAG_EVAL_FLIP, which avoids "double evaluation",
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
        if (not EVALUATING(current))
            goto inert;

        fail (Error_Evaluate_Null_Raw());

//==//////////////////////////////////////////////////////////////////////==//
//
// If garbage, panic on the value to generate more debug information about
// its origins (what series it lives in, where the cell was assigned...)
//
//==//////////////////////////////////////////////////////////////////////==//

      default:
        panic (current);
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
    // Next, there's a subtlety with DO_FLAG_NO_LOOKAHEAD which explains why
    // processing of the 2 argument doesn't greedily continue to advance, but
    // waits for `1 + 2` to finish.  This is because the right hand argument
    // of math operations tend to be declared #tight.
    //
    // Slightly more nuanced is why ACTION_FLAG_INVISIBLE functions have to be
    // considered in the lookahead also.  Consider this case:
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

    assert(IS_POINTER_TRASH_DEBUG(f->u.defer.arg));

//=//// IF NOT A WORD!, IT DEFINITELY STARTS A NEW EXPRESSION /////////////=//

    // For long-pondered technical reasons, only WORD! is able to dispatch
    // enfix.  If it's necessary to dispatch an enfix function via path, then
    // a word must be used to do it, e.g. `x: -> lib/method [...] [...]`.
    // That word can be an action with a variadic left argument, that can
    // decide what parameter convention to use to the left based on what it
    // sees to the right.

    if (Is_Frame_Gotten_Shoved(f)) {
        //
        // Tried to SHOVE, and didn't hit a situation like `add -> + 1`.  So
        // now the shoving process falls through, as in `10 -> + 1`.
        //
        assert(NOT_VAL_FLAG(f->gotten, ACTION_FLAG_QUOTES_FIRST_ARG));
        goto post_switch_shove_gotten;
    }

    eval_type = VAL_TYPE_RAW(f->value);

    if (eval_type == REB_0_END)
        goto finished; // hitting end is common, avoid do_next's switch()

    if (eval_type == REB_PATH) {
        if (
            VAL_LEN_AT(f->value) != 0
            or (f->flags.bits & DO_FLAG_NO_LOOKAHEAD)
            or not EVALUATING(f->value)
        ){
            if (not (f->flags.bits & DO_FLAG_TO_END))
                goto finished; // just 1 step of work, so stop evaluating
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
        Begin_Action(f, opt_label, LOOKBACK_ARG);

        Fetch_Next_In_Frame(nullptr, f); // advances f->value
        goto process_action;
    }

    if (eval_type != REB_WORD or not EVALUATING(f->value)) {
        if (not (f->flags.bits & DO_FLAG_TO_END))
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
        // plus a VALUE_FLAG_ENFIXED.  We discern this hacky case by noting
        // if f->u.defer.arg is precisely equal to BLANK_VALUE.
        //
        assert(
            f->gotten == Try_Get_Opt_Var(f->value, f->specifier)
            or (f->prior->u.defer.arg == BLANK_VALUE) // !!! hack
        );
    }

//=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-ENFIX /////////////=//

    // These cases represent finding the start of a new expression, which
    // continues the evaluator loop if DO_FLAG_TO_END, but will stop with
    // `goto finished` if not (DO_FLAG_TO_END).
    //
    // Fall back on word-like "dispatch" even if f->gotten is null (unset or
    // unbound word).  It'll be an error, but that code path raises it for us.

    if (
        not f->gotten // note that only ACTIONs have VALUE_FLAG_ENFIXED
        or NOT_VAL_FLAG(VAL(f->gotten), VALUE_FLAG_ENFIXED)
    ){
      lookback_quote_too_late:; // run as if starting new expression

        if (not (f->flags.bits & DO_FLAG_TO_END)) {
            //
            // Since it's a new expression, EVALUATE doesn't want to run it
            // even if invisible, as it's not completely invisible (enfixed)
            //
            goto finished;
        }

        if (
            f->gotten
            and IS_ACTION(VAL(f->gotten))
            and GET_VAL_FLAG(VAL(f->gotten), ACTION_FLAG_INVISIBLE)
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

    if (GET_VAL_FLAG(f->gotten, ACTION_FLAG_QUOTES_FIRST_ARG)) {
        //
        // Left-quoting by enfix needs to be done in the lookahead before an
        // evaluation, not this one that's after.  This happens in cases like:
        //
        //     left-quote: enfix func [:value] [:value]
        //     quote <something> left-quote
        //
        // But due to the existence of <end>-able and <skip>-able parameters,
        // the left quoting function might be okay with seeing nothing on the
        // left.  Start a new expression and let it error if that's not ok.
        //
        goto lookback_quote_too_late;
    }

  post_switch_shove_gotten:; // assert(!ACTION_FLAG_QUOTES_FIRST_ARG) pre-goto

    if (
        (f->flags.bits & DO_FLAG_NO_LOOKAHEAD)
        and NOT_VAL_FLAG(f->gotten, ACTION_FLAG_INVISIBLE)
    ){
        // Don't do enfix lookahead if asked *not* to look.  See the
        // PARAM_CLASS_TIGHT parameter convention for the use of this, as
        // well as it being set if DO_FLAG_TO_END wants to clear out the
        // invisibles at this frame level before returning.
        //
        if (Is_Frame_Gotten_Shoved(f)) {
            Prep_Stack_Cell(FRM_SHOVE(f->prior));
            Move_Value(FRM_SHOVE(f->prior), f->gotten);
            SET_VAL_FLAGS(FRM_SHOVE(f->prior), VALUE_FLAG_ENFIXED);
            f->gotten = FRM_SHOVE(f->prior);
        }
        goto finished;
    }

    // !!! Once checked `not f->deferred` because it only deferred once:
    //
    //    "If we get there and there's a deferral, it doesn't matter if it
    //     was this frame or the parent frame who deferred it...it's the
    //     same enfix function in the same spot, and it's only willing to
    //     give up *one* of its chances to run."
    //
    // But it now defers indefinitely so long as it is fulfilling arguments,
    // until it finds an <end>able one...which <- (identity) is.  Having
    // endability control this may not be the best idea, but it keeps from
    // introducing a new parameter convention or recognizing the specific
    // function.  It's a rare enough property that one might imagine it to be
    // unlikely such functions would want to run before deferred enfix.
    //
    if (
        GET_VAL_FLAG(f->gotten, ACTION_FLAG_DEFERS_LOOKBACK)
        and (f->flags.bits & DO_FLAG_FULFILLING_ARG)
        and not f->prior->u.defer.arg
        and not Is_Param_Endable(f->prior->param)
    ){
        assert(not (f->flags.bits & DO_FLAG_TO_END));
        assert(Is_Action_Frame_Fulfilling(f->prior));

        // Must be true if fulfilling an argument that is *not* a deferral
        //
        assert(f->out == f->prior->arg);

        f->prior->u.defer.arg = f->prior->arg; // see defer comments in REBFRM
        f->prior->u.defer.param = f->prior->param;
        f->prior->u.defer.refine = f->prior->refine;

        if (Is_Frame_Gotten_Shoved(f)) {
            Prep_Stack_Cell(FRM_SHOVE(f->prior));
            Move_Value(FRM_SHOVE(f->prior), f->gotten);
            SET_VAL_FLAG(FRM_SHOVE(f->prior), VALUE_FLAG_ENFIXED);
            f->gotten = FRM_SHOVE(f->prior);
        }

        // Leave the enfix operator pending in the frame, and it's up to the
        // parent frame to decide whether to use DO_FLAG_POST_SWITCH to jump
        // back in and finish fulfilling this arg or not.  If it does resume
        // and we get to this check again, f->prior->deferred can't be null,
        // otherwise it would be an infinite loop.
        //
        goto finished;
    }

    // This is a case for an evaluative lookback argument we don't want to
    // defer, e.g. a #tight argument or a normal one which is not being
    // requested in the context of parameter fulfillment.  We want to reuse
    // the f->out value and get it into the new function's frame.

    Push_Action(f, VAL_ACTION(f->gotten), VAL_BINDING(f->gotten));

    if (IS_WORD(f->value))
        Begin_Action(f, VAL_WORD_SPELLING(f->value), LOOKBACK_ARG);
    else {
        // Should be a SHOVE.  There needs to be a way to telegraph the label
        // on the value if it was a PATH! to here.
        //
        assert(Is_Frame_Gotten_Shoved(f));
        assert(IS_PATH(f->value) or IS_GROUP(f->value) or IS_ACTION(f->value));
        REBSTR *opt_label = nullptr;
        Begin_Action(f, opt_label, LOOKBACK_ARG);
    }

    Fetch_Next_In_Frame(nullptr, f); // advances f->value
    goto process_action;

  abort_action:;

    Drop_Action(f);
    DS_DROP_TO(f->dsp_orig); // any unprocessed refinements or chains on stack

  return_thrown:;

    threw = true;

  finished:;

    assert(THROWN(f->out) == threw);

    // The unevaluated flag is meaningless outside of arguments to functions.

    if (not (f->flags.bits & DO_FLAG_FULFILLING_ARG))
        f->out->header.bits &= ~VALUE_FLAG_UNEVALUATED; // may be an END cell

    // Most clients would prefer not to read the stale flag, and be burdened
    // with clearing it (can't be present on frame output).  Also, argument
    // fulfillment can't read it (ARG_MARKED_CHECKED and OUT_MARKED_STALE are
    // the same bit)...but it doesn't need to, since it always starts END.
    //
    assert(not (
        (f->flags.bits & DO_FLAG_FULFILLING_ARG)
        & (f->flags.bits & DO_FLAG_PRESERVE_STALE)
    ));
    if (not (f->flags.bits & DO_FLAG_PRESERVE_STALE))
        f->out->header.bits &= ~OUT_MARKED_STALE;

  #if !defined(NDEBUG)
    Eval_Core_Exit_Checks_Debug(f); // will get called unless a fail() longjmps
  #endif

    return threw; // most callers should inspect for IS_END(f->value)
}
