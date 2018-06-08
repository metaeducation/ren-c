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
// This file contains `Do_Core()`, which is the central evaluator which
// is behind DO.  It can execute single evaluation steps (e.g. a DO/NEXT)
// or it can run the array to the end of its content.  A flag controls that
// behavior, and there are DO_FLAG_XXX for controlling other behaviors.
//
// For comprehensive notes on the input parameters, output parameters, and
// internal state variables...see %sys-rebfrm.h.
//
// NOTES:
//
// * Do_Core() is a very long routine.  That is largely on purpose, because it
//   doesn't contain repeated portions.  If it were broken into functions that
//   would add overhead for little benefit, and prevent interesting tricks
//   and optimizations.  Note that it is separated into sections, and
//   the invariants in each section are made clear with comments and asserts.
//
// * The evaluator only moves forward, and it consumes exactly one element
//   from the input at a time.  This input must be locked read-only for the
//   duration of the execution.  At the moment it can be an array tracked by
//   index and incrementation, or it may be a C va_list which tracks its own
//   position on each fetch through a forward-only iterator.
//

#include "sys-core.h"


#if defined(DEBUG_COUNT_TICKS)
    //
    // The evaluator `tick` should be visible in the C debugger watchlist as a
    // local variable in Do_Core() for each stack level.  So if a fail()
    // happens at a deterministic moment in a run, capture the number from
    // the level of interest and recompile with it here to get a breakpoint
    // at that tick.
    //
    // On the command-line, you can also request to break at a particular tick
    // using the `--breakpoint NNN` option.
    //
    // Notice also that in debug builds, `REBSER.tick` carries this value.
    // *Plus* you can get the initialization tick for void cells, BLANK!s,
    // LOGIC!s, and most end markers by looking at the `track` payload of
    // the REBVAL cell.  And series contain the `REBSER.tick` where they were
    // created as well.
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
//  Apply_Core: C
//
// It is desirable to be able to hook the moment of function application,
// when all the parameters are gathered, and to be able to monitor the return
// result.  This is the default function put into PG_Apply, but it can be
// overridden e.g. by TRACE, which would like to preface the apply by dumping
// the frame and postfix it by showing the evaluative result.
//
REB_R Apply_Core(REBFRM * const f) {
    return ACT_DISPATCHER(f->phase)(f);
}


static inline REBOOL Start_New_Expression_Throws(REBFRM *f) {
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

  #if defined(DEBUG_UNREADABLE_BLANKS)
    assert(
        IS_UNREADABLE_DEBUG(f->out) or IS_END(f->out) or IS_BAR(f->value)
    );
  #endif

    return false;
}


#if !defined(NDEBUG)
    #define START_NEW_EXPRESSION_MAY_THROW(f,g) \
        Do_Core_Expression_Checks_Debug(f); \
        if (Start_New_Expression_Throws(f)) \
            g; \
        evaluating = not ((f)->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);
#else
    #define START_NEW_EXPRESSION_MAY_THROW(f,g) \
        if (Start_New_Expression_Throws(f)) \
            g; \
        evaluating = not ((f)->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);
#endif


#ifdef DEBUG_COUNT_TICKS
    //
    // Macro for same stack level as Do_Core when debugging at TICK_BREAKPOINT
    //
    #define UPDATE_TICK_DEBUG(cur) \
        do { \
            if (TG_Tick < UINTPTR_MAX) /* avoid rollover (may be 32-bit!) */ \
                tick = f->tick = ++TG_Tick; \
            else \
                tick = f->tick = UINTPTR_MAX; \
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
// e.g. `IS_VOID(f->special)` can only match the other two cases.
//

inline static REBOOL In_Typecheck_Mode(REBFRM *f) {
    return f->special == f->arg;
}

inline static REBOOL In_Unspecialized_Mode(REBFRM *f) {
    return f->special == f->param;
}


// Typechecking has to be broken out into a subroutine because it is not
// always the case that one is typechecking the current argument.  See the
// documentation on REB_0_DEFERRED for why.
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

        // Consider Do_Core() result for COMMENT in `do [1 + comment "foo"]`.
        // Should be no different from `do [1 +]`, when Do_Core() gives END.

        if (NOT_VAL_FLAG(param, TYPESET_FLAG_ENDABLE))
            fail (Error_No_Arg(f_state, param));

        Init_Endish_Void(arg);
        return;
    }

    assert(
        refine == ORDINARY_ARG // check arg type
        or refine == LOOKBACK_ARG // check arg type
        or refine == ARG_TO_UNUSED_REFINEMENT // ensure arg void
        or refine == ARG_TO_REVOKED_REFINEMENT // ensure arg void
        or (IS_LOGIC(refine) and IS_TRUTHY(refine)) // ensure arg not void
    );

    if (IS_VOID(arg)) {
        if (IS_LOGIC(refine)) {
            //
            // We can only revoke the refinement if this is the 1st
            // refinement arg.  If it's a later arg, then the first
            // didn't trigger revocation, or refine wouldn't be logic.
            //
            if (refine + 1 != arg)
                fail (Error_Bad_Refine_Revoke(param, arg));

            Init_Logic(refine, false); // can't re-enable...
            refine = ARG_TO_REVOKED_REFINEMENT;
            return; // don't type check for optionality
        }

        if (IS_FALSEY(refine)) {
            //
            // FALSE means refinement already revoked, void is okay
            // BLANK! means refinement was never in use, so also okay
            //
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

    if (NOT_VAL_FLAG(param, TYPESET_FLAG_VARIADIC)) {
        if (TYPE_CHECK(param, VAL_TYPE(arg)))
            return;

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
    arg->payload.varargs.param_offset = arg - f_state->args_head;
    arg->payload.varargs.facade = ACT_FACADE(f_state->phase);
}

inline static void Finalize_Current_Arg(REBFRM *f) {
    Finalize_Arg(f, f->param, f->arg, f->refine);
}


//
//  Do_Core: C
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
//     Do_Core in stable memory that is not user-visible (e.g. DECLARE_LOCAL
//     or the frame's f->cell).  This can't point into an array whose memory
//     may move during arbitrary evaluation, and that includes cells on the
//     expandable data stack.  It also usually can't write a function argument
//     cell, because that could expose an unfinished calculation during this
//     Do_Core() through its FRAME!...though a Do_Core(f) must write f's *own*
//     arg slots to fulfill them.
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
void Do_Core(REBFRM * const f)
{
  #if defined(DEBUG_COUNT_TICKS)
    REBTCK tick = f->tick = TG_Tick; // snapshot start tick
  #endif

    // Some routines (like Reduce_XXX) reuse the frame across multiple calls
    // and accrue stack state, and that stack state should be skipped when
    // considering the usages in Do_Core().  Hence Do_Next_In_Frame_Throws()
    // will set it on each call.  However, some routines want to slip the
    // DSP in with refinements on the stack (e.g. APPLY or MY).  The
    // compromise is that it is also done in DECLARE_FRAME(); that way it
    // can be captured before a Push_Frame operation is done.
    //
    assert(f->dsp_orig <= DSP);

    REBOOL evaluating; // set on every iteration (varargs do, EVAL/ONLY...)

    // Handling of deferred lookbacks may need to re-enter the frame and get
    // back to the processing it had put off.
    //
    if (f->flags.bits & DO_FLAG_POST_SWITCH) {
        evaluating = not (f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);

        // !!! Note EVAL-ENFIX does a crude workaround to preserve this check.
        //
        assert(f->prior->deferred);

        assert(NOT_END(f->out));
        f->flags.bits &= ~DO_FLAG_POST_SWITCH; // !!! unnecessary?
        goto post_switch;
    }

    // END signals no evaluations have produced a result yet, even if some
    // functions have run (e.g. COMMENT with ACTION_FLAG_INVISIBLE).  It also
    // is initialized bits to be safe for the GC to inspect and protect, and
    // triggers noisy alarms to help detect when someone attempts to evaluate
    // into a cell in an array (which may have its memory moved).
    //
    SET_END(f->out);

    // APPLY and a DO of a FRAME! both use process_action.
    //
    if (f->flags.bits & DO_FLAG_APPLYING) {
        evaluating = not (f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);

        assert(f->refine == ORDINARY_ARG); // APPLY infix not (yet?) supported
        goto process_action;
    }

    f->eval_type = VAL_TYPE(f->value);

do_next:;

    START_NEW_EXPRESSION_MAY_THROW(f, goto finished);
    // ^-- resets local `evaluating` flag, `tick` count, Ctrl-C may abort

    const RELVAL *current;
    const REBVAL *current_gotten;

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

    current_gotten = f->gotten;
    f->gotten = END;

    // Most calls to Fetch_Next_In_Frame() are no longer interested in the
    // cell backing the pointer that used to be in f->value (this is enforced
    // by a rigorous test in STRESS_EXPIRED_FETCH).  Special care must be
    // taken when one is interested in that data, because it may have to be
    // moved.  See notes in Fetch_Next_In_Frame.
    //
    current = Fetch_Next_In_Frame(f);

reevaluate:;

    // ^-- doesn't advance expression index, so `eval x` starts with `eval`
    // also EVAL/ONLY may change `evaluating` to FALSE for a cycle

    UPDATE_TICK_DEBUG(current);
    // v-- This is the TICK_BREAKPOINT or C-DEBUG-BREAK landing spot --v

    if (evaluating == GET_VAL_FLAG(current, VALUE_FLAG_EVAL_FLIP)) {
        //
        // Either we're NOT evaluating and there's NO special exemption, or we
        // ARE evaluating and there IS A special exemption.  Treat this as
        // inert.
        //
        // !!! This check is repeated in function argument fulfillment, and
        // as this is new and experimental code it's not clear exactly what
        // the consequences should be to lookahead.  There needs to be
        // reconsideration now that evaluating-ness is a property that can
        // be per-frame, per operation, and per-value.
        //
        goto inert;
    }

    //==////////////////////////////////////////////////////////////////==//
    //
    // LOOKAHEAD TO ENABLE ENFIXED FUNCTIONS THAT QUOTE THEIR LEFT ARG
    //
    //==////////////////////////////////////////////////////////////////==//

    // Ren-C has an additional lookahead step *before* an evaluation in order
    // to take care of this scenario.  To do this, it pre-emptively feeds the
    // frame one unit that f->value is the *next* value, and a local variable
    // called "current" holds the current head of the expression that the
    // switch will be processing.

    // !!! We never want to do infix processing if the args aren't evaluating
    // (e.g. arguments in a va_list from a C function calling into Rebol)
    // But this is distinct from DO_FLAG_NO_LOOKAHEAD (which Apply_Only also
    // sets), which really controls the after lookahead step.  Consider this
    // edge case.
    //
    if (
        FRM_HAS_MORE(f)
        and IS_WORD(f->value)
        and evaluating == NOT_VAL_FLAG(f->value, VALUE_FLAG_EVAL_FLIP)
    ){
        //
        // While the next item may be a WORD! that looks up to an enfixed
        // function, and it may want to quote what's on its left...there
        // could be a conflict.  This happens if the current item is also
        // a WORD!, but one that looks up to a prefix function that wants
        // to quote what's on its right!
        //
        if (f->eval_type == REB_WORD) {
            if (current_gotten == END)
                current_gotten = Get_Opt_Var_Else_End(current, f->specifier);
            else
                assert(
                    current_gotten
                    == Get_Opt_Var_Else_End(current, f->specifier)
                );

            if (
                VAL_TYPE_OR_0(current_gotten) == REB_ACTION // END is REB_0
                and NOT_VAL_FLAG(current_gotten, VALUE_FLAG_ENFIXED)
                and GET_VAL_FLAG(current_gotten, ACTION_FLAG_QUOTES_FIRST_ARG)
            ){
                // Yup, it quotes.  We could look for a conflict and call
                // it an error, but instead give the left hand side precedence
                // over the right.  This means something like:
                //
                //     foo: quote => [print quote]
                //
                // Would be interpreted as:
                //
                //     foo: (quote =>) [print quote]
                //
                // This is a good argument for not making enfixed operations
                // that hard-quote things that can dispatch functions.  A
                // soft-quote would give more flexibility to override the
                // left hand side's precedence, e.g. the user writes:
                //
                //     foo: ('quote) => [print quote]
                //
                Push_Action(
                    f,
                    VAL_WORD_SPELLING(current),
                    VAL_ACTION(current_gotten),
                    VAL_BINDING(current_gotten)
                );

                f->refine = ORDINARY_ARG;
                if (NOT_VAL_FLAG(current_gotten, ACTION_FLAG_INVISIBLE)) {
                  #if defined(DEBUG_UNREADABLE_BLANKS)
                    assert(IS_UNREADABLE_DEBUG(f->out) or IS_END(f->out));
                  #endif
                    SET_END(f->out);
                }
                goto process_action;
            }
        }
        else if (
            f->eval_type == REB_PATH
            and VAL_LEN_AT(current) > 0
            and IS_WORD(VAL_ARRAY_AT(current))
        ){
            // !!! Words aren't the only way that functions can be dispatched,
            // one can also use paths.  It gets tricky here, because path
            // mechanics are dodgier than word fetches.  Not only can it have
            // GROUP!s and have side effects to "examining" what it looks up
            // to, but there are other implications.
            //
            // As a temporary workaround to make HELP/DOC DEFAULT work, where
            // DEFAULT hard quotes left, we have to recognize that path as a
            // function call which quotes its first argument...so splice in
            // some handling here that peeks at the head of the path and sees
            // if it applies.  Note this is very brittle, and can be broken as
            // easily as saying `o: make object! [h: help]` and then doing
            // `o/h/doc default`.
            //
            // There are ideas on the table for how to remedy this long term.
            // For now, see comments in the WORD branch above for the
            // cloned mechanic.

            assert(current_gotten == END); // no caching for paths

            REBSPC *derived = Derive_Specifier(f->specifier, current);

            RELVAL *path_at = VAL_ARRAY_AT(current);
            const REBVAL *var_at = Get_Opt_Var_Else_End(path_at, derived);

            if (
                VAL_TYPE_OR_0(var_at) == REB_ACTION // END is REB_0
                and NOT_VAL_FLAG(var_at, VALUE_FLAG_ENFIXED)
                and GET_VAL_FLAG(var_at, ACTION_FLAG_QUOTES_FIRST_ARG)
            ){
                goto do_path_in_current;
            }
        }

        f->gotten = Get_Opt_Var_Else_End(f->value, f->specifier);

        if (
            VAL_TYPE_OR_0(f->gotten) == REB_ACTION // END is REB_0
            and ALL_VAL_FLAGS(
                f->gotten,
                VALUE_FLAG_ENFIXED | ACTION_FLAG_QUOTES_FIRST_ARG
            )
        ){
            Push_Action(
                f,
                VAL_WORD_SPELLING(f->value),
                VAL_ACTION(f->gotten),
                VAL_BINDING(f->gotten)
            );

            // The protocol for lookback is that the lookback argument is
            // consumed from the f->out slot.  It will ultimately wind up
            // moved into the frame, so having the quoting cases get
            // it there by way of the f->out is *slightly* inefficient.  But
            // since evaluative cases do wind up with the value in f->out,
            // and are much more common, it's not worth worrying about.
            //
            f->refine = LOOKBACK_ARG;
            Derelativize(f->out, current, f->specifier);

          #if !defined(NDEBUG)
            //
            // Since the value is going to be copied into an arg slot anyway,
            // setting the unevaluated flag here isn't necessary.  However,
            // it allows for an added debug check that if an enfixed parameter
            // is hard or soft quoted, it *probably* came from here.
            //
            SET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
          #endif

            // We don't want the WORD! that invoked the function to act like
            // an argument, so we have to advance the frame once more.
            //
            f->gotten = END;
            Fetch_Next_In_Frame(f);
            goto process_action;
        }
    }

    //==////////////////////////////////////////////////////////////////==//
    //
    // BEGIN MAIN SWITCH STATEMENT
    //
    //==////////////////////////////////////////////////////////////////==//

    // This switch is done via contiguous REB_XXX values, in order to
    // facilitate use of a "jump table optimization":
    //
    // http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables

    switch (f->eval_type) {

    case REB_0:
        panic ("REB_0 encountered in Do_Core"); // internal type, never DO it

//==//////////////////////////////////////////////////////////////////////==//
//
// [ACTION!] (lookback or non-lookback)
//
// If an action makes it to the SWITCH statement, that means it is either
// literally an action value in the array (`do compose [(:+) 1 2]`) or is
// being retriggered via EVAL
//
// Most action evaluations are triggered from a SWITCH on a WORD! or PATH!,
// which jumps in at the `process_action` label.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_ACTION: // literal action in a block
        Push_Action(
            f,
            nullptr, // no label, nameless literal action direct in source
            VAL_ACTION(current),
            VAL_BINDING(current)
        );

        // It should not be possible to encounter a literal ACTION! value
        // with the enfix bit set, as this bit can only be retrieved from
        // words that are assigned in contexts via SET/ENFIX.
        //
        assert(NOT_VAL_FLAG(current, VALUE_FLAG_ENFIXED));

        if (NOT_VAL_FLAG(current, ACTION_FLAG_INVISIBLE))
            SET_END(f->out); // clear out previous result
        f->refine = ORDINARY_ARG;

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

    process_action:;

        TRASH_POINTER_IF_DEBUG(current); // shouldn't be used below
        TRASH_POINTER_IF_DEBUG(current_gotten);

      #if !defined(NDEBUG)
        Do_Process_Action_Checks_Debug(f);
      #endif

        assert(DSP >= f->dsp_orig); // REFINEMENT!s pushed by path processing
        assert(f->refine == LOOKBACK_ARG or f->refine == ORDINARY_ARG);

        f->doing_pickups = false;

    process_args_for_pickup_or_to_end:;

        for (; NOT_END(f->param); ++f->param, ++f->arg, ++f->special) {

            enum Reb_Param_Class pclass = VAL_PARAM_CLASS(f->param);

    //=//// A /REFINEMENT ARG /////////////////////////////////////////////=//

            // Refinements are checked first for a reason.  This is to
            // short-circuit based on the `doing_pickups` flag before redoing
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
                if (f->doing_pickups) {
                    if (DSP != f->dsp_orig)
                        goto next_pickup;

                    f->param = END; // done, so f->param need not be in facade
                    goto arg_loop_and_any_pickups_done;
                }

                TRASH_POINTER_IF_DEBUG(f->refine); // updated to new value

                if (In_Unspecialized_Mode(f))
                    goto unspecialized_refinement; // most common case

                if (IS_VOID(f->special)) {
                    //
                    // Even just In_Typecheck_Mode(), we still may either
                    // APPLY a function with refinements (apply 'append/only)
                    // or a MAKE FRAME! may have refinements filled with void.
                    // So even if we are "just checking" we actually change
                    // voids in refinement slots, to FALSE if not refined
                    // or to TRUE if it is.
                    //
                    goto unspecialized_refinement;
                }

                if (IS_LOGIC(f->special)) { // similar for check vs. special
                    if (not In_Typecheck_Mode(f)) {
                        Prep_Stack_Cell(f->arg);
                        Init_Logic(f->arg, VAL_LOGIC(f->special));
                    }

                    if (VAL_LOGIC(f->special) == true)
                        f->refine = f->arg; // remember so we can revoke!
                    else
                        f->refine = ARG_TO_UNUSED_REFINEMENT; // (read-only)

                    goto continue_arg_loop;
                }

                if (IS_REFINEMENT(f->special) and not In_Typecheck_Mode(f)) {
                    //
                    // See REB_0_PARTIAL for explanations of how storing a
                    // REFINEMENT! with a binding in it indicates a partial
                    // refinement with parameter index (pushed to top of
                    // stack, hence *higher priority* for fulfilling at the
                    // callsite than any refinements added by a PATH!).
                    //
                    REBCNT partial_index = VAL_WORD_INDEX(f->special);
                    REBSTR *partial_canon = VAL_STORED_CANON(f->special);

                    // !!! Simplify this, but we need to make sure we don't
                    // cause reification or management code to run here
                    //
                    DS_PUSH_TRASH;
                    Init_Refinement(DS_TOP, partial_canon);
                    DS_TOP->extra.binding = NOD(f); // need unmanaged
                    DS_TOP->payload.any_word.index = partial_index;

                    if (not IS_REFINEMENT_SPECIALIZED(f->param)) {
                        assert(partial_canon != VAL_PARAM_CANON(f->param));
                        goto unspecialized_refinement; // !!! not top
                    }

                    Prep_Stack_Cell(f->arg);
                    Init_Logic(f->arg, true);
                    f->refine = SKIPPING_REFINEMENT_ARGS;
                    goto continue_arg_loop;
                }

                assert(In_Typecheck_Mode(f)); // specialization bug otherwise
                fail (Error_Non_Logic_Refinement(f->param, f->arg));

    //=//// UNSPECIALIZED REFINEMENT SLOT (no consumption) ////////////////=//

            unspecialized_refinement:

                if (f->dsp_orig == DSP) { // no refinements left on stack
                    Prep_Stack_Cell(f->arg);
                    Init_Logic(f->arg, false);
                    f->refine = ARG_TO_UNUSED_REFINEMENT; // "don't consume"
                    goto continue_arg_loop;
                }

                REBVAL *ordered = DS_TOP;

                REBSTR *param_canon = VAL_PARAM_CANON(f->param); // #2258

                if (VAL_STORED_CANON(ordered) == param_canon) {
                    DS_DROP; // we're lucky: this was next refinement used

                    Prep_Stack_Cell(f->arg);
                    Init_Logic(f->arg, true); // marks refinement used
                    f->refine = f->arg; // "consume args (can be revoked)"
                    goto continue_arg_loop;
                }

                --ordered; // not lucky: if in use, this is out of order

                for (; ordered != DS_AT(f->dsp_orig); --ordered) {
                    if (VAL_STORED_CANON(ordered) != param_canon)
                        continue;

                    Prep_Stack_Cell(f->arg);
                    Init_Logic(f->arg, true); // marks refinement used

                    // The call uses this refinement but we'll have to
                    // come back to it when the expression index to
                    // consume lines up.  Save the position to come back to,
                    // as binding information on the refinement.
                    //
                    // !!! INIT_BINDING manages, and INIT_WORD_INDEX reifies
                    //
                    REBCNT offset = f->arg - f->args_head;
                    ordered->extra.binding = NOD(f);
                    ordered->payload.any_word.index = offset + 1;

                    // "consume args later" (promise not to change)
                    //
                    f->refine = SKIPPING_REFINEMENT_ARGS;
                    goto continue_arg_loop;
                }

                // Wasn't in the path and not specialized, so not present
                //
                Prep_Stack_Cell(f->arg);
                Init_Logic(f->arg, false);
                f->refine = ARG_TO_UNUSED_REFINEMENT; // "don't consume"
                goto continue_arg_loop;
            }

    //=//// "PURE" LOCAL: ARG /////////////////////////////////////////////=//

            // This takes care of locals, including "magic" RETURN and LEAVE
            // cells that need to be pre-filled.  Notice that although the
            // parameter list may have RETURN and LEAVE slots, that parameter
            // list may be reused by an "adapter" or "hijacker" which would
            // technically happen *before* the "magic" (if the user had
            // implemented the definitinal returns themselves inside the
            // function body).  Hence they are not always filled.
            //
            // Also note that while it might seem intuitive to take care of
            // these "easy" fills before refinement checking--checking for
            // refinement pickups ending prevents double-doing this work.

            switch (pclass) {
            case PARAM_CLASS_LOCAL:
                Prep_Stack_Cell(f->arg);
                Init_Void(f->arg); // !!! f->special?
                goto continue_arg_loop;

            case PARAM_CLASS_RETURN:
                assert(VAL_PARAM_SYM(f->param) == SYM_RETURN);

                if (not GET_ACT_FLAG(f->phase, ACTION_FLAG_RETURN)) {
                    Prep_Stack_Cell(f->arg);
                    Init_Void(f->arg);
                    goto continue_arg_loop;
                }

                Prep_Stack_Cell(f->arg);
                Move_Value(f->arg, NAT_VALUE(return)); // !!! f->special?
                f->arg->extra.binding = NOD(f); // !!! INIT_BINDING reifies
                goto continue_arg_loop;

            case PARAM_CLASS_LEAVE:
                assert(VAL_PARAM_SYM(f->param) == SYM_LEAVE);

                if (not GET_ACT_FLAG(f->phase, ACTION_FLAG_LEAVE)) {
                    Prep_Stack_Cell(f->arg);
                    Init_Void(f->arg);
                    goto continue_arg_loop;
                }

                Prep_Stack_Cell(f->arg);
                Move_Value(f->arg, NAT_VALUE(leave)); // !!! f->special?
                f->arg->extra.binding = NOD(f); // !!! INIT_BINDING reifies
                goto continue_arg_loop;

            default:
                break;
            }

    //=//// IF COMING BACK TO REFINEMENT ARGS LATER, MOVE ON FOR NOW //////=//

            if (f->refine == SKIPPING_REFINEMENT_ARGS) {
                //
                // The GC will protect values up through how far we have
                // enumerated, and though we're leaving trash in this slot
                // it has special handling to tolerate that, so long as we're
                // doing pickups.

                Prep_Stack_Cell(f->arg);
                goto continue_arg_loop;
            }

            if (not In_Unspecialized_Mode(f)) {
               if (In_Typecheck_Mode(f)) {
                    Finalize_Current_Arg(f);
                    goto continue_arg_loop; // looping to verify args/refines
                }

                if (f->flags.bits & DO_FLAG_APPLYING) {
                    Prep_Stack_Cell(f->arg);
                    Move_Value(f->arg, f->special); // voids are literal
                    Finalize_Current_Arg(f);
                    goto continue_arg_loop;
                }

    //=//// SPECIALIZED ARG ///////////////////////////////////////////////=//

                if (not IS_VOID(f->special)) {
                    Prep_Stack_Cell(f->arg);
                    Move_Value(f->arg, f->special);

                    // SPECIALIZE checks types at specialization time, to
                    // save us the time of doing it on each call.  But double
                    // check to make sure.
                    //
                    assert(NOT_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC));
                    assert(TYPE_CHECK(f->param, VAL_TYPE(f->arg)));
                    goto continue_arg_loop;
                }
            }

    //=//// IF UNSPECIALIZED ARG IS INACTIVE, SET VOID AND MOVE ON ////////=//

            // Unspecialized arguments that do not consume do not need any
            // further processing or checking.  void will always be fine.
            //
            if (f->refine == ARG_TO_UNUSED_REFINEMENT) {
                Prep_Stack_Cell(f->arg);
                Init_Void(f->arg);
                goto continue_arg_loop;
            }

    //=//// IF LOOKBACK, THEN USE PREVIOUS EXPRESSION RESULT FOR ARG //////=//

            if (f->refine == LOOKBACK_ARG) {
                //
                // Switch to ordinary arg up front, so gotos below are good to
                // go for the next argument
                //
                f->refine = ORDINARY_ARG;

                Prep_Stack_Cell(f->arg);

                if (
                    (f->out->header.bits & NODE_FLAG_END)
                    or (f->flags.bits & DO_FLAG_BARRIER_HIT)
                ){
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
                    if (GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
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
                    if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                        fail (Error_No_Arg(f, f->param));

                    Init_Endish_Void(f->arg);
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
                    else {
                        Move_Value(f->arg, f->out);
                        SET_VAL_FLAG(f->arg, VALUE_FLAG_UNEVALUATED);
                    }

                    // Hard quotes can take BAR!s but they should look like an
                    // <end> to a soft quote.
                    //
                    if (IS_BAR(f->arg))
                        SET_END(f->arg);
                    break;

                default:
                    assert(false);
                }

                if (not GET_ACT_FLAG(f->phase, ACTION_FLAG_INVISIBLE))
                    SET_END(f->out);

                // Now that we've gotten the argument figured out, make a
                // singular array to feed it to the variadic.
                //
                // !!! See notes on VARARGS_FLAG_ENFIXED about how this is
                // somewhat shady, as any evaluations happen *before* the
                // TAKE on the VARARGS.  Experimental feature.
                //
                if (GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
                    REBARR *array1;
                    if (IS_END(f->arg))
                        array1 = EMPTY_ARRAY;
                    else {
                        REBARR *feed = Alloc_Singular_Array();
                        Move_Value(ARR_SINGLE(feed), f->arg);
                        MANAGE_ARRAY(feed);

                        array1 = Alloc_Singular_Array();
                        Init_Block(ARR_SINGLE(array1), feed); // index 0
                        MANAGE_ARRAY(array1);
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
            if (GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
                Prep_Stack_Cell(f->arg);
                RESET_VAL_HEADER(f->arg, REB_VARARGS);

                // !!! Doesn't use INIT_BINDING() because that conservatively
                // reifies, and not only do we know we don't have to here, it
                // would assert trying to reify a fulfilling frame.
                //
                f->arg->extra.binding = NOD(f);

                Finalize_Current_Arg(f); // sets VARARGS! offset and facade
                goto continue_arg_loop;
            }

    //=//// AFTER THIS, PARAMS CONSUME FROM CALLSITE IF NOT APPLY ////////=//

            assert(
                f->refine == ORDINARY_ARG
                or (IS_LOGIC(f->refine) and IS_TRUTHY(f->refine))
            );

    //=//// START BY HANDLING ANY DEFERRED ENFIX PROCESSING //////////////=//

            // `if 10 and 20` starts by filling the first arg slot with 10,
            // because AND has a "non-tight" (normal) left hand argument.
            // Were `if 10` a complete expression, it would allow that.
            //
            // But now we're consuming another argument at  the callsite, so
            // by definition `if 10` wasn't finished.  We kept a `f->deferred`
            // field that points at the previously filled f->arg slot.  So we
            // can re-enter a sub-frame and give the IF's `condition` slot a
            // second chance to run the enfix processing it put off before,
            // this time using the 10 as the AND's left-hand argument.
            //
            if (f->deferred) {
                assert(VAL_TYPE(&f->cell) == REB_0_DEFERRED);

                // The GC's understanding of how far to protect parameters is
                // based on how far f->param has gotten.  Yet we've advanced
                // f->param and f->arg, with END in arg, but are rewinding
                // time so that a previous parameter is being filled.  Back
                // off f->param one unit... it may not actually go to the
                // parameter before the current, but if f->doing_pickups this
                // will be okay (all cells at least prep'd w/initialized bits)
                // and if we're not, then it will be aligned with f->deferred
                //
                --f->param;
                --f->arg;
                --f->special;

                REBFLGS flags = DO_FLAG_FULFILLING_ARG | DO_FLAG_POST_SWITCH;
                if (not evaluating)
                    flags |= DO_FLAG_EXPLICIT_EVALUATE;

                DECLARE_FRAME (child); // capture DSP *now*
                if (Do_Next_In_Subframe_Throws(
                    f->deferred, // old f->arg preload for DO_FLAG_POST_SWITCH
                    f,
                    flags,
                    child
                )){
                    Move_Value(f->out, f->deferred);
                    goto abort_action;
                }

                // This frame's cell shouldn't have been disturbed by the
                // subframe processing, so it can still provide context for
                // typechecking the argument (it wasn't previously checked).
                //
                assert(VAL_TYPE(&f->cell) == REB_0_DEFERRED);
                Finalize_Arg(
                    f,
                    f->cell.payload.deferred.param,
                    f->deferred,
                    f->cell.payload.deferred.refine
                );

                Init_Unreadable_Blank(&f->cell);
                f->deferred = nullptr;

                // Compensate for the param and arg change earlier.
                //
                ++f->param;
                ++f->arg;
                ++f->special;
            }

    //=//// ERROR ON END MARKER, BAR! IF APPLICABLE //////////////////////=//

            if (FRM_AT_END(f)) {
                if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                    fail (Error_No_Arg(f, f->param));

                Prep_Stack_Cell(f->arg);
                Init_Endish_Void(f->arg);
                goto continue_arg_loop;
            }

    //=//// IF EVAL/ONLY SEMANTICS, TAKE NEXT ARG WITHOUT EVALUATION //////=//

            if (
                evaluating
                == GET_VAL_FLAG(f->value, VALUE_FLAG_EVAL_FLIP)
            ){
                // Either we're NOT evaluating and there's NO special
                // exemption, or we ARE evaluating and there IS A special
                // exemption.  Treat this as if it's quoted.
                //
                Prep_Stack_Cell(f->arg);
                Quote_Next_In_Frame(f->arg, f); // has VALUE_FLAG_UNEVALUATED
                Finalize_Current_Arg(f);
                goto continue_arg_loop;
            }

    //=//// IF EVAL SEMANTICS, DISALLOW LITERAL EXPRESSION BARRIERS ///////=//

            if (IS_BAR(f->value) and pclass != PARAM_CLASS_HARD_QUOTE) {
                //
                // Only legal if arg is *hard quoted*.  Else, it must come via
                // other means (e.g. literal as `'|` or `first [|]`)

                if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                    fail (Error_No_Arg(f, f->param));

                Prep_Stack_Cell(f->arg);
                Init_Endish_Void(f->arg);
                goto continue_arg_loop;
            }

            switch (pclass) {

   //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes a DO/NEXT's worth) ////=//

            case PARAM_CLASS_NORMAL: {
                REBFLGS flags = DO_FLAG_FULFILLING_ARG;
                if (not evaluating)
                    flags |= DO_FLAG_EXPLICIT_EVALUATE;

                Prep_Stack_Cell(f->arg);

                DECLARE_FRAME (child); // capture DSP *now*
                if (Do_Next_In_Subframe_Throws(f->arg, f, flags, child)) {
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
                REBFLGS flags = DO_FLAG_NO_LOOKAHEAD | DO_FLAG_FULFILLING_ARG;
                if (not evaluating)
                    flags |= DO_FLAG_EXPLICIT_EVALUATE;

                Prep_Stack_Cell(f->arg);

                DECLARE_FRAME (child);
                if (Do_Next_In_Subframe_Throws(f->arg, f, flags, child)) {
                    Move_Value(f->out, f->arg);
                    goto abort_action;
                }
                break; }

    //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG /////////////////////////////=//

            case PARAM_CLASS_HARD_QUOTE:
                Prep_Stack_Cell(f->arg);
                Quote_Next_In_Frame(f->arg, f); // has VALUE_FLAG_UNEVALUATED
                break;

    //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  ////////////////////////////=//

            case PARAM_CLASS_SOFT_QUOTE:
                if (not IS_QUOTABLY_SOFT(f->value)) {
                    Prep_Stack_Cell(f->arg);
                    Quote_Next_In_Frame(f->arg, f); // VALUE_FLAG_UNEVALUATED
                    Finalize_Current_Arg(f);
                    goto continue_arg_loop;
                }

                Prep_Stack_Cell(f->arg);
                if (Eval_Value_Core_Throws(f->arg, f->value, f->specifier)) {
                    Move_Value(f->out, f->arg);
                    goto abort_action;
                }

                Fetch_Next_In_Frame(f);
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
            assert(not In_Typecheck_Mode(f)); // already handled

            if (not f->deferred)
                Finalize_Arg(f, f->param, f->arg, f->refine);

        continue_arg_loop:;

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
        if (not In_Typecheck_Mode(f) and DSP != f->dsp_orig) {

        next_pickup:;

            assert(IS_REFINEMENT(DS_TOP));

            if (not IS_WORD_BOUND(DS_TOP)) // the loop didn't index it
                fail (Error_Bad_Refine_Raw(DS_TOP)); // so duplicate or junk

            // f->args_head offsets are 0-based, while index is 1-based.
            // But +1 is okay, because we want the slots after the refinement.
            //
            REBINT offset = VAL_WORD_INDEX(DS_TOP) - (f->arg - f->args_head);
            f->param += offset;
            f->arg += offset;
            f->special += offset;

            f->refine = f->arg - 1; // this refinement may still be revoked
            assert(IS_LOGIC(f->refine) and VAL_LOGIC(f->refine));

            assert(VAL_STORED_CANON(DS_TOP) == VAL_PARAM_CANON(f->param - 1));
            assert(VAL_PARAM_CLASS(f->param - 1) == PARAM_CLASS_REFINEMENT);

            DS_DROP;
            f->doing_pickups = true;
            goto process_args_for_pickup_or_to_end;
        }

    arg_loop_and_any_pickups_done:;

        assert(IS_END(f->param)); // signals !Is_Action_Frame_Fulfilling()

        if (In_Typecheck_Mode(f)) {
            if (f->varlist)
                assert(NOT_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE));

            assert(IS_POINTER_TRASH_DEBUG(f->deferred));
        }
        else { // was fulfilling...
            if (f->varlist) {
                assert(GET_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE));
                CLEAR_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE);
            }

            if (f->deferred) {
                //
                // We deferred typechecking, but still need to do it...
                // f->cell holds the necessary context for typechecking
                //
                assert(VAL_TYPE(&f->cell) == REB_0_DEFERRED);
                Finalize_Arg(
                    f,
                    f->cell.payload.deferred.param,
                    f->deferred,
                    f->cell.payload.deferred.refine
                );
                Init_Unreadable_Blank(&f->cell);
            }

            TRASH_POINTER_IF_DEBUG(f->deferred);
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
            FRM_AT_END(f)
            or FRM_IS_VALIST(f)
            or IS_VALUE_IN_ARRAY_DEBUG(f->source.array, f->value)
        );

        // The out slot needs initialization for GC safety during the function
        // run.  Choosing an END marker should be legal because places that
        // you can use as output targets can't be visible to the GC (that
        // includes argument arrays being fulfilled).  This offers extra
        // perks, because it means a recycle/torture will catch you if you
        // try to Do_Core into movable memory...*and* a native can tell if it
        // has written the out slot yet or not.
        //
        assert(
            IS_END(f->out) or GET_ACT_FLAG(f->phase, ACTION_FLAG_INVISIBLE)
        );

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
        assert(f->out->header.bits & (CELL_FLAG_STACK | NODE_FLAG_ROOT));

        // Running arbitrary native code can manipulate the bindings or cache
        // of a variable.  It's very conservative to say this, but any word
        // fetches that were done for lookahead are potentially invalidated
        // by every function call.
        //
        f->gotten = END;

        // Cases should be in enum order for jump-table optimization
        // (R_FALSE first, R_TRUE second, etc.)
        //
        // The dispatcher may push functions to the data stack which will be
        // used to process the return result after the switch.
        //
        switch ((*PG_Apply)(f)) {
        case R_FALSE:
            Init_Logic(f->out, false);
            break;

        case R_TRUE:
            Init_Logic(f->out, true);
            break;

        case R_VOID:
            Init_Void(f->out);
            break;

        case R_BLANK:
            Init_Blank(f->out);
            break;

        case R_BAR:
            Init_Bar(f->out);
            break;

        case R_OUT:
            break;

        case R_OUT_IS_THROWN: {
            assert(THROWN(f->out));

            if (IS_ACTION(f->out)) {
                if (
                    VAL_ACTION(f->out) == NAT_ACTION(unwind)
                    and Same_Binding(VAL_BINDING(f->out), f)
                ){
                    // Do_Core catches unwinds to the current frame, so throws
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
                    goto apply_completed;
                }
                else if (
                    VAL_ACTION(f->out) == NAT_ACTION(redo)
                    and Same_Binding(VAL_BINDING(f->out), f)
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
                        f->phase != f->out->payload.any_context.phase
                        and did (exemplar = ACT_EXEMPLAR(
                            f->out->payload.any_context.phase
                        ))
                    ){
                        f->special = CTX_VARS_HEAD(exemplar);
                        f->arg = f->args_head;
                        for (; NOT_END(f->arg); ++f->arg, ++f->special) {
                            if (IS_VOID(f->special)) // no specialization
                                continue;
                            Move_Value(f->arg, f->special); // reset it
                        }
                    }

                    f->phase = f->out->payload.any_context.phase;
                    f->binding = VAL_BINDING(f->out);
                    goto redo_checked;
                }
            }

            // Stay THROWN and let stack levels above try and catch
            //
            goto abort_action; }

        case R_REDO_CHECKED:

        redo_checked:

            f->param = ACT_FACADE_HEAD(f->phase);
            f->arg = f->args_head;
            f->special = f->arg;
            f->refine = ORDINARY_ARG; // no gathering, but need for assert
            assert(not GET_ACT_FLAG(f->phase, ACTION_FLAG_INVISIBLE));
            SET_END(f->out);
            goto process_action;

        case R_REDO_UNCHECKED:
            //
            // This instruction represents the idea that it is desired to
            // run the f->phase again.  The dispatcher may have changed the
            // value of what f->phase is, for instance.
            //
            assert(not GET_ACT_FLAG(f->phase, ACTION_FLAG_INVISIBLE));
            SET_END(f->out);
            goto redo_unchecked;

        case R_REEVALUATE_CELL:
            evaluating = true; // unnecessary?
            goto prep_for_reevaluate;

        case R_REEVALUATE_CELL_ONLY:
            evaluating = false;
            goto prep_for_reevaluate;

        case R_INVISIBLE: {
            assert(GET_ACT_FLAG(f->phase, ACTION_FLAG_INVISIBLE));

            // It is possible that when the elider ran, that there really was
            // no output in the cell yet (e.g. `do [comment "hi" ...]`) so it
            // would still be END after the fact.
            //
            // !!! Ideally we would check that f->out hadn't changed, but
            // that would require saving the old value somewhere...
            //
          #if defined(DEBUG_UNREADABLE_BLANKS)
            assert(IS_END(f->out) or not IS_UNREADABLE_DEBUG(f->out));
          #endif

            // If an invisible is at the start of a frame and there's nothing
            // after it, it has to retrigger until it finds something (or
            // until it hits the end of the frame).
            //
            //     do [comment "a" 1] => 1
            //
            // Use same mechanic as EVAL by loading next item.
            //
            if (IS_END(f->out) and not FRM_AT_END(f)) {
                Derelativize(&f->cell, f->value, f->specifier);
                Fetch_Next_In_Frame(f);

                evaluating = true; // unnecessary?
                goto prep_for_reevaluate;
            }

            goto skip_output_check; }

        prep_for_reevaluate:

            current = &f->cell;
            f->eval_type = VAL_TYPE(current);
            current_gotten = END;

            // The f->gotten (if any) was the fetch for f->value, not what we
            // just put in current.  We conservatively clear this cache:
            // assume for instance that f->value is a WORD! that looks up to
            // a value which is in f->gotten, and then f->cell contains a
            // zero-arity function which changes the value of that word.  It
            // might be possible to finesse use of this cache and clear it
            // only if such cases occur, but for now don't take chances.
            //
            f->gotten = END;

            Drop_Action_Core(f, true); // drop_chunks = true
            goto reevaluate; // we don't move index!

        case R_UNHANDLED: // internal use only, shouldn't be returned
            assert(false);
            break;

        default:
            assert(false);
        }

    apply_completed:;

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

        // If we have functions pending to run on the outputs, then do so.
        //
        while (DSP != f->dsp_orig) {
            assert(IS_ACTION(DS_TOP));

            Move_Value(&f->cell, f->out);

            // Data stack values cannot be used directly in an apply, because
            // the evaluator uses DS_PUSH, which could relocate the stack
            // and invalidate the pointer.
            //
            DECLARE_LOCAL (fun);
            Move_Value(fun, DS_TOP);

            if (Apply_Only_Throws(
                f->out,
                true, // fully = true
                fun,
                DEVOID(KNOWN(&f->cell)), // void cell => nullptr for API
                END
            )){
                goto abort_action;
            }

            DS_DROP;
        }

        // !!! It would technically be possible to drop the arguments before
        // running chains... and if the chained function were to run *in*
        // this frame that could be even more optimal.  However, having the
        // original function still on the stack helps make errors clearer.
        //
        Drop_Action_Core(f, true); // drop_chunks = true
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
        if (current_gotten == END)
            current_gotten = Get_Opt_Var_May_Fail(current, f->specifier);

        if (IS_ACTION(current_gotten)) { // before IS_VOID() is common case
            Push_Action(
                f,
                VAL_WORD_SPELLING(current),
                VAL_ACTION(current_gotten),
                VAL_BINDING(current_gotten)
            );

            if (GET_VAL_FLAG(current_gotten, VALUE_FLAG_ENFIXED)) {
                //
                // Note: The usual dispatch of enfix functions is not via a
                // REB_WORD in this switch, it's by some code at the end of
                // the switch.  So you only see this in cases like `(+ 1 2)`,
                // or after ACTION_FLAG_INVISIBLE e.g. `10 comment "hi" + 20`.
                //
                f->refine = LOOKBACK_ARG;

              #if defined(DEBUG_UNREADABLE_BLANKS)
                assert(IS_END(f->out) or not IS_UNREADABLE_DEBUG(f->out));
              #endif
            }
            else {
                f->refine = ORDINARY_ARG;
                if (NOT_VAL_FLAG(current_gotten, ACTION_FLAG_INVISIBLE))
                    SET_END(f->out);
            }

            goto process_action;
        }

        if (IS_VOID(current_gotten)) // need `:x` if `x` is unset
            fail (Error_No_Value_Core(current, f->specifier));

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
// Recursion into Do_Core() is used, but a new frame is not created.  Instead
// it reuses `f` with a lighter-weight approach.  Do_Next_Mid_Frame_Throws()
// has remarks on how this is done.
//
// !!! Note that `10 = 5 + 5` would be an error due to lookahead suppression
// from `=`, so it reads as `(10 = 5) + 5`.  However `10 = x: 5 + 5` will not
// be an error, as the SET-WORD! causes a recursion in the evaluator.  This
// is unusual, but there are advantages to seeing SET-WORD! as a kind of
// single-arity function.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_SET_WORD:
        assert(IS_SET_WORD(current));

        if (FRM_AT_END(f)) {
            DECLARE_LOCAL (specific);
            Derelativize(specific, current, f->specifier);
            fail (Error_Need_Value_Raw(specific)); // `do [a:]` is illegal
        }

        // The SET-WORD! was deemed active otherwise we wouldn't have entered
        // the switch for it.  But the right hand side f->value we're going to
        // set the path *to* can have its own twist coming from the evaluator
        // flip bit and evaluator state.
        //
        if (evaluating == GET_VAL_FLAG(f->value, VALUE_FLAG_EVAL_FLIP)) {
            //
            // Either we're NOT evaluating and there's NO special exemption,
            // or we ARE evaluating and there IS A special exemption.  Treat
            // the f->value as inert.
            //
            Derelativize(f->out, f->value, f->specifier);
            Move_Value(Sink_Var_May_Fail(current, f->specifier), f->out);
        }
        else {
            // f->value is guarded implicitly by the frame, but `current` is a
            // transient local pointer that might be to a va_list REBVAL* that
            // has already been fetched.  The bits will stay live until
            // va_end(), but a GC wouldn't see it.
            //
            DS_PUSH_RELVAL(current, f->specifier);

            REBFLGS flags = DO_FLAG_FULFILLING_SET; // not DO_FLAG_TO_END
            if (not evaluating)
                flags |= DO_FLAG_EXPLICIT_EVALUATE;

            if (Do_Next_Mid_Frame_Throws(f, flags)) { // light reuse of `f`
                DS_DROP;
                goto finished;
            }

            // if x: [1 < 2] [print "errors if set-word doesn't clear flag"]
            //
            CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);

            Move_Value(Sink_Var_May_Fail(DS_TOP, SPECIFIED), f->out);

            DS_DROP;
        }
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [GET-WORD!]
//
// A GET-WORD! does no checking for unsets, no dispatch on functions, and
// will return void if the variable is not set.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_GET_WORD:
        //
        // Note: copying values does not copy VALUE_FLAG_UNEVALUATED
        //
        Move_Opt_Var_May_Fail(f->out, current, f->specifier);
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
        //
        // Derelativize will clear VALUE_FLAG_UNEVALUATED
        //
        Derelativize(f->out, current, f->specifier);
        VAL_SET_TYPE_BITS(f->out, REB_WORD);
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
// If a GROUP! is seen then it generates another call into Do_Core().  The
// resulting value for this step will be the outcome of that evaluation.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_GROUP: {
        REBCNT len = VAL_LEN_AT(current);
        if (len == 0) {
            Init_Void(f->out);
            break; // no VALUE_FLAG_UNEVALUATED
        }

        if (len == 1 and ANY_INERT(current)) {
            //
            // (1) does not need to make a new frame to tell you that's 1,
            // ([a b c]) does not need a new frame to get [a b c], etc.
            //
            // Not worth it to optimize any evaluated types (GET-WORD!, etc.)
            // as if they fail, the frame should be there to tie errors to.
            //
            Move_Value(f->out, const_KNOWN(current));
            break; // VALUE_FLAG_UNEVALUATED does not get moved
        }

        REBSPC *derived = Derive_Specifier(f->specifier, current);
        if (Do_At_Throws(
            f->out,
            VAL_ARRAY(current), // the GROUP!'s array
            VAL_INDEX(current), // index in REBVAL (may not be head)
            derived
        )){
            goto finished;
        }

        // This has to set the evaluated flag to bypass checking.  e.g.
        // `if (1) [print "this is supposed to work"]`.  Unfortunately this
        // means you can't semiquote things inside groups, only outside of
        // them, e.g. `semiquote (a b c)` and not `(semiquote a b c)`.
        //
        CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
        break; }

//==//////////////////////////////////////////////////////////////////////==//
//
// [PATH!]
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_PATH: {
        //
        // !!! If a path's head indicates dispatch to a function and quotes
        // its first argument, it gets jumped down here to avoid allowing
        // a back-quoting word after it to quote it.  This is a hack to permit
        // `help/doc default` to work, but is a short term solution to a more
        // general problem.

    do_path_in_current:;

        REBSTR *opt_label;
        if (Do_Path_Throws_Core(
            f->out,
            &opt_label, // requesting says we run functions (not GET-PATH!)
            REB_PATH,
            VAL_ARRAY(current),
            VAL_INDEX(current),
            Derive_Specifier(f->specifier, current),
            nullptr, // `setval`: null means don't treat as SET-PATH!
            DO_FLAG_PUSH_PATH_REFINEMENTS
        )){
            goto finished;
        }

        if (IS_VOID(f->out)) // need `:x/y` if `y` is unset
            fail (Error_No_Value_Core(current, f->specifier));

        if (IS_ACTION(f->out)) {
            //
            // !!! While it is (or would be) possible to fetch an enfix or
            // invisible function from a PATH!, at this point it would be too
            // late in the current scheme...since the lookahead step only
            // honors WORD!.  PATH! support is expected for the future, but
            // requires overhaul of the R3-Alpha path implementation.
            //
            // Note this error must come *before* Push_Action(), as fail()
            // expects f->param to be valid for f->eval_type = REB_ACTION,
            // and Push_Action() trashes that.
            //
            if (ANY_VAL_FLAGS(
                f->out,
                ACTION_FLAG_INVISIBLE | VALUE_FLAG_ENFIXED
            )){
                fail ("ENFIX/INVISIBLE dispatch w/PATH! not yet supported");
            }

            Push_Action(
                f,
                opt_label, // null label means anonymous
                VAL_ACTION(f->out),
                VAL_BINDING(f->out)
            );

            f->refine = ORDINARY_ARG; // paths are never enfixed (for now)
            SET_END(f->out); // loses enfix left hand side, invisible passthru
            goto process_action;
        }

        CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
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
// and requires the value to set as a parameter to Do_Path.  Yet it is
// counterintuitive given the "left-to-right" nature of the language:
//
//     >> foo: make object! [[bar][bar: 10]]
//
//     >> foo/(print "left" 'bar): (print "right" 20)
//     right
//     left
//     == 20
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_SET_PATH: {
        assert(IS_SET_PATH(current));

        if (FRM_AT_END(f)) {
            DECLARE_LOCAL (specific);
            Derelativize(specific, current, f->specifier);
            fail (Error_Need_Value_Raw(specific)); // `do [a/b:]` is illegal
        }

        // The SET-PATH! was deemed active otherwise we wouldn't have entered
        // the switch for it.  But the right hand side f->value we're going to
        // set the path *to* can have its own twist coming from the evaluator
        // flip bit and evaluator state.
        //
        if (evaluating == GET_VAL_FLAG(f->value, VALUE_FLAG_EVAL_FLIP)) {
            //
            // Either we're NOT evaluating and there's NO special exemption,
            // or we ARE evaluating and there IS A special exemption.  Treat
            // the f->value as inert.

            Derelativize(f->out, f->value, f->specifier);

            // !!! Due to the way this is currently designed, throws need to
            // be written to a location distinct from the path and also
            // distinct from the value being set.  Review.
            //
            DECLARE_LOCAL (temp);

            if (Set_Path_Throws_Core(
                temp, // output location if thrown
                current, // still holding SET-PATH! we got in
                f->specifier, // specifier for current
                f->out // value to set (already in f->out)
            )){
                fail (Error_No_Catch_For_Throw(temp));
            }
        }
        else {
            // f->value is guarded implicitly by the frame, but `current` is a
            // transient local pointer that might be to a va_list REBVAL* that
            // has already been fetched.  The bits will stay live until
            // va_end(), but a GC wouldn't see it.
            //
            DS_PUSH_RELVAL(current, f->specifier);

            REBFLGS flags = DO_FLAG_FULFILLING_SET; // not DO_FLAG_TO_END
            if (not evaluating)
                flags |= DO_FLAG_EXPLICIT_EVALUATE;

            if (Do_Next_Mid_Frame_Throws(f, flags)) { // light reuse of `f`
                DS_DROP;
                goto finished;
            }

            // The path cannot be executed directly from the data stack, so
            // it has to be popped.  This could be changed by making the core
            // Do_Path_Throws take a VAL_ARRAY, index, and kind.  By moving
            // it into the f->cell, it is guaranteed garbage collected.
            //
            Move_Value(&f->cell, DS_TOP);
            DS_DROP;

            // !!! Due to the way this is currently designed, throws need to
            // be written to a location distinct from the path and also
            // distinct from the value being set.  Review.
            //
            DECLARE_LOCAL (temp);

            if (Set_Path_Throws_Core(
                temp, // output location if thrown
                &f->cell, // still holding SET-PATH! we got in
                SPECIFIED, // current derelativized when pushed to DS_TOP
                f->out // value to set (already in f->out)
            )){
                fail (Error_No_Catch_For_Throw(temp));
            }
        }

        break; }

//==//////////////////////////////////////////////////////////////////////==//
//
// [GET-PATH!]
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_GET_PATH:
        //
        // The GET native on a PATH! won't allow GROUP! execution:
        //
        //    foo: [X]
        //    path: 'foo/(print "side effect!" 1)
        //    get path ;-- not allowed, due to surprising side effects
        //
        // However a source-level GET-PATH! allows them, since they are at
        // the callsite and you are assumed to know what you are doing:
        //
        //    :foo/(print "side effect" 1) ;-- this is allowed
        //
        if (Get_Path_Throws_Core(f->out, current, f->specifier))
            goto finished;

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
        //
        // Derelativize will leave VALUE_FLAG_UNEVALUATED clear
        //
        Derelativize(f->out, current, f->specifier);
        VAL_SET_TYPE_BITS(f->out, REB_PATH);
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
// Expression barriers are "invisibles", and hence as many of them have to
// be processed at the end of the loop as there are--they can't be left in
// the source feed, else `do/next [1 + 2 | | | |] 'pos` would not be able
// to reconstitute 3 from `[| | | |]` for the next operation.
//
// Though they have to be processed at the end of the loop, they could also
// be at the beginning of an evaluation.  This handles that case.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_BAR:
        if (FRM_HAS_MORE(f)) {
            //
            // May be fulfilling a variadic argument (or an argument to an
            // argument of a variadic, etc.)  Make a note that a barrier was
            // hit so it can stop gathering evaluative arguments.
            //
            if (f->flags.bits & DO_FLAG_FULFILLING_ARG)
                f->flags.bits |= DO_FLAG_BARRIER_HIT;
            f->eval_type = VAL_TYPE(f->value);
            goto do_next; // quickly process next item, no infix test needed
        }
        break;

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
        Init_Bar(f->out);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [void]
//
// Void is not an ANY-VALUE!, and void cells are not allowed in ANY-ARRAY!
// exposed to the user.  So usually, a DO shouldn't be able to see them,
// unless they are un-evaluated...e.g. `Apply_Only_Throws()` passes in a
// VOID_CELL as an evaluation-already-accounted-for parameter to a function.
//
// The exception case is something like `eval ()`, which is the user
// deliberately trying to invoke the evaluator on a void.  (Not to be confused
// with `eval quote ()`, which is the evaluation of an empty GROUP!, which
// produces void, and that's fine).  We choose to deliver an error in the void
// case, which provides a consistency:
//
//     :foo/bar => pick foo 'bar (void if not present)
//     foo/bar => eval :foo/bar (should be an error if not present)
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_MAX_VOID:
        if (evaluating == GET_VAL_FLAG(current, VALUE_FLAG_EVAL_FLIP)) {
            Init_Void(f->out); // it's inert, treat as okay
        }
        else {
            // must be EVAL, so the value must be living in the frame cell
            //
            fail (Error_Evaluate_Null_Raw());
        }
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// If garbage, panic on the value to generate more debug information about
// its origins (what series it lives in, where the cell was assigned...)
//
//==//////////////////////////////////////////////////////////////////////==//

    default:
        panic (current);
    }

    if (FRM_AT_END(f))
        goto finished;

    //==////////////////////////////////////////////////////////////////==//
    //
    // END MAIN SWITCH STATEMENT
    //
    //==////////////////////////////////////////////////////////////////==//

    // We're sitting at what "looks like the end" of an evaluation step.
    // But we still have to consider enfix.  e.g.
    //
    //    do/next [1 + 2 * 3] 'pos
    //
    // We want that to come back as 9, with `pos = []`.  So the evaluator
    // cannot just dispatch on REB_INTEGER in the switch() above, give you 1,
    // and consider its job done.  It has to notice that the variable `+`
    // looks up to is an ACTION! assigned with SET/ENFIX, and keep going.
    //
    // Next, there's a subtlety with DO_FLAG_NO_LOOKAHEAD which explains why
    // processing of the 2 argument doesn't greedily continue to advance, but
    // waits for `1 + 2` to finish.  This is because the right hand argument
    // of math operations tend to be declared #tight.
    //
    // Slightly more nuanced is why ACTION_FLAG_INVISIBLE functions have to be
    // considered in the lookahead also.  Consider this case:
    //
    //    do/next [1 + 2 * 3 comment ["hi"] 4 / 5] 'pos
    //
    // We want this to evaluate to 9, with `pos = [4 / 5]`.  To do this, we
    // can't consider an evaluation finished until all the "invisibles" have
    // been processed.  That's because letting the comment wait until the next
    // evaluation would preclude `do/next [1 + 2 * 3 comment ["hi"]]` being
    // 9, since `comment ["hi"]` alone can't come up with 9 out of thin air.
    //
    // If that's not enough to consider :-) it can even be the case that
    // subsequent enfix gets "deferred".  Then, possibly later the evaluated
    // value gets re-fed back in, and we jump right to this post-switch point
    // to give it a "second chance" to take the enfix.  (See 'deferred'.)
    //
    // So this post-switch step is where all of it happens, and it's tricky.

post_switch:;

    assert(IS_POINTER_TRASH_DEBUG(f->deferred));

    f->eval_type = VAL_TYPE(f->value);

    // Because BAR! is effectively an "invisible", it must follow the same
    // rule of being consumed in the same step as its left hand side, as a
    // DO/NEXT of the BAR! alone can't bring back the lost value.
    //
    if (f->eval_type == REB_BAR) {
        if (f->flags.bits & DO_FLAG_FULFILLING_ARG)
            f->flags.bits |= DO_FLAG_BARRIER_HIT;
        do {
            Fetch_Next_In_Frame(f);
            if (FRM_AT_END(f))
                goto finished;
            f->eval_type = VAL_TYPE(f->value);
        } while (f->eval_type == REB_BAR);
    }

//=//// IF NOT A WORD!, IT DEFINITELY STARTS A NEW EXPRESSION /////////////=//

    // !!! Our lookahead step currently only works with WORD!, but it should
    // be retrofitted in the future to support PATH! dispatch also (for both
    // enfix and invisible/comment-like behaviors).  But in the meantime, if
    // you use a PATH! and look up to an enfixed word or "invisible" result
    // function, that's an error (or should be).

    if (f->eval_type != REB_WORD) {
        if (not (f->flags.bits & DO_FLAG_TO_END))
            goto finished; // only want DO/NEXT of work, so stop evaluating

        START_NEW_EXPRESSION_MAY_THROW(f, goto finished);
        // ^-- resets evaluating + tick, corrupts f->out, Ctrl-C may abort

        UPDATE_TICK_DEBUG(nullptr);
        // v-- The TICK_BREAKPOINT or C-DEBUG-BREAK landing spot --v

        goto do_next;
    }

//=//// FETCH WORD! TO PERFORM SPECIAL HANDLING FOR ENFIX/INVISIBLES //////=//

    // First things first, we fetch the WORD! (if not previously fetched) so
    // we can see if it looks up to any kind of ACTION! at all.

    if (f->gotten == END)
        f->gotten = Get_Opt_Var_Else_End(f->value, f->specifier);
    else {
        // !!! a particularly egregious hack in EVAL-ENFIX lets us simulate
        // enfix for a function whose value is not enfix.  This means the
        // value in f->gotten isn't the fetched function, but the function
        // plus a VALUE_FLAG_ENFIXED.  We discern this hacky case by noting
        // if f->deferred is precisely equal to BLANK_VALUE.
        //
        assert(
            f->gotten == Get_Opt_Var_Else_End(f->value, f->specifier)
            or (f->prior and f->prior->deferred == BLANK_VALUE) // !!! hack
        );
    }

//=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-ENFIX /////////////=//

    // These cases represent finding the start of a new expression, which
    // continues the evaluator loop if DO_FLAG_TO_END, but will stop with
    // `goto finished` if not (DO_FLAG_TO_END).
    //
    // We fall back on word-like "dispatch" even if f->gotten == END (unset or
    // unbound word).  It'll be an error, but that code path raises it for us.

    if (
        VAL_TYPE_OR_0(f->gotten) != REB_ACTION // END is REB_0 (UNBOUND)
        or NOT_VAL_FLAG(f->gotten, VALUE_FLAG_ENFIXED)
    ){
        if (not (f->flags.bits & DO_FLAG_TO_END)) {
            //
            // Since it's a new expression, a DO/NEXT doesn't want to run it
            // *unless* it's "invisible"
            if (
                VAL_TYPE_OR_0(f->gotten) != REB_ACTION
                or NOT_VAL_FLAG(f->gotten, ACTION_FLAG_INVISIBLE)
            ){
                goto finished;
            }

            // Though it's an "invisible" function, we don't want to call it
            // unless it's our *last* chance to do so for a fulfillment (e.g.
            // DUMP should be called for `do [x: 1 + 2 dump [x]]` only
            // after the assignment to X is complete.)
            //
            // The way we test for this is to see if there's no fulfillment
            // process above us which will get a later chance (that later
            // chance will occur for that higher frame at this code point.)
            //
            if (
                f->flags.bits
                & (DO_FLAG_FULFILLING_ARG | DO_FLAG_FULFILLING_SET)
            ){
                goto finished;
            }

            // Take our last chance to run the invisible function, but shift
            // into a mode where we *only* run such functions.  (Once this
            // flag is set, it will have it until termination, then erased
            // when the frame is discarded/reused.)
            //
            f->flags.bits |= DO_FLAG_NO_LOOKAHEAD; // might have set already
        }
        else if (
            VAL_TYPE_OR_0(f->gotten) == REB_ACTION
            and GET_VAL_FLAG(f->gotten, ACTION_FLAG_INVISIBLE)
        ){
            // Even if not a DO/NEXT, we do not want START_NEW_EXPRESSION on
            // "invisible" functions.  e.g. `do [1 + 2 comment "hi"]` should
            // consider that one whole expression.  Reason being that the
            // comment cannot be broken out and thought of as having a return
            // result... `comment "hi"` alone cannot have any basis for
            // evaluating to 3.
        }
        else {
            START_NEW_EXPRESSION_MAY_THROW(f, goto finished);
            // ^-- resets evaluating + tick, corrupts f->out, Ctrl-C may abort

            UPDATE_TICK_DEBUG(nullptr);
            // v-- The TICK_BREAKPOINT or C-DEBUG-BREAK landing spot --v
        }

        current = f->value;
        current_gotten = f->gotten; // if END, the word will error
        f->gotten = END;
        Fetch_Next_In_Frame(f);

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

    if (
        (f->flags.bits & DO_FLAG_NO_LOOKAHEAD)
        and NOT_VAL_FLAG(f->gotten, ACTION_FLAG_INVISIBLE)
    ){
        // Don't do enfix lookahead if asked *not* to look.  See the
        // PARAM_CLASS_TIGHT parameter convention for the use of this, as
        // well as it being set if DO_FLAG_TO_END wants to clear out the
        // invisibles at this frame level before returning.
        //
        goto finished;
    }

    if (GET_VAL_FLAG(f->gotten, ACTION_FLAG_QUOTES_FIRST_ARG)) {
        //
        // Left-quoting by enfix needs to be done in the lookahead before an
        // evaluation, not this one that's after.  This happens in cases like:
        //
        //     left-quote: enfix func [:value] [:value]
        //     quote <something> left-quote
        //
        // !!! Is this the ideal place to be delivering the error?
        //
        fail (Error_Lookback_Quote_Too_Late(f->value, f->specifier));
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
        and not f->prior->deferred
        and NOT_VAL_FLAG(f->prior->param, TYPESET_FLAG_ENDABLE)
    ){
        assert(not (f->flags.bits & DO_FLAG_TO_END));
        assert(Is_Action_Frame_Fulfilling(f->prior));

        // Must be true if fulfilling an argument that is *not* a deferral
        //
        assert(f->out == f->prior->arg);

        f->prior->deferred = f->prior->arg; // see deferred comments in REBFRM

        RESET_VAL_HEADER(&f->prior->cell, REB_0_DEFERRED);
        f->prior->cell.payload.deferred.param = f->prior->param;
        f->prior->cell.payload.deferred.refine = f->prior->refine;

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

    Push_Action(
        f,
        VAL_WORD_SPELLING(f->value),
        VAL_ACTION(f->gotten),
        VAL_BINDING(f->gotten)
    );
    f->refine = LOOKBACK_ARG;

    f->gotten = END;
    Fetch_Next_In_Frame(f); // advances f->value
    goto process_action;

abort_action:;

    assert(THROWN(f->out));

    Drop_Action_Core(f, true); // drop_chunks = true
    DS_DROP_TO(f->dsp_orig); // any unprocessed refinements or chains on stack

finished:;

  #if !defined(NDEBUG)
    Do_Core_Exit_Checks_Debug(f); // will get called unless a fail() longjmps
  #endif

    // All callers must inspect for THROWN(f->out), and most should also
    // inspect for FRM_AT_END(f)
}
