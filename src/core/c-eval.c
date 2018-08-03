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
    // *Plus* you can get the initialization tick for nulled cells, BLANK!s,
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
// This adds one level of function cost into every dispatch--but well worth it
// for the functionality.  Note also that R3-Alpha had `if (Trace_Flags)`
// in the main loop before and after function dispatch, which was more costly
// and much less flexible.  Nevertheless, sneaky lower-level-than-C tricks
// might be used to patch the machine code and avoid cost when not hooked.
//
REB_R Dispatcher_Core(REBFRM * const f) {
    //
    // Callers can "lie" to make the dispatch a no-op by substituting the
    // DEFER-0 native in the frame, even though it doesn't match the args,
    // in order to build the frame of a function without running it.  This
    // is one of the few places tolerant of the lie, so don't call FRM_PHASE()
    //
    return ACT_DISPATCHER(FRM_PHASE_OR_DUMMY(f))(f);
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

    assert(f->out->header.bits & OUT_MARKED_STALE);
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
// e.g. `IS_NULLED(f->special)` can only match the other two cases.
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
        or refine == ARG_TO_UNUSED_REFINEMENT // ensure arg void
        or refine == ARG_TO_REVOKED_REFINEMENT // ensure arg void
        or IS_REFINEMENT(refine) // ensure arg not void
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

    if (NOT_VAL_FLAG(param, TYPESET_FLAG_VARIADIC)) {
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
    arg->payload.varargs.facade = ACT_FACADE(FRM_PHASE(f_state));
    SET_VAL_FLAG(arg, ARG_MARKED_CHECKED);
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

    // When Do_Core() is entered, whatever is in the out slot isn't a
    // candidate for being fed into the left side of an enfix expression...
    // it will always be treated as an END.  However it may need to be left
    // there for use by the caller (e.g. ALL might want it if a final
    // evaluation step turns out to be all invisibles)
    //
    assert(not IS_TRASH_DEBUG(f->out));
    f->out->header.bits |= OUT_MARKED_STALE;

    if (f->flags.bits & DO_FLAG_GOTO_PROCESS_ACTION) {
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
        // While the next item may be a WORD! that looks up to an enfixed
        // function, and it may want to quote what's on its left...there
        // could be a conflict.  This happens if the current item is also
        // a WORD!, but one that looks up to a prefix function that wants
        // to quote what's on its right!
        //
        if (f->eval_type == REB_WORD) {
            if (not current_gotten)
                current_gotten = Try_Get_Opt_Var(current, f->specifier);
            else
                assert(
                    current_gotten == Try_Get_Opt_Var(current, f->specifier)
                );

            if (
                current_gotten
                and VAL_TYPE(current_gotten) == REB_ACTION
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
                    VAL_ACTION(current_gotten),
                    VAL_BINDING(current_gotten)
                );
                Begin_Action(f, VAL_WORD_SPELLING(current), ORDINARY_ARG);

                if (NOT_VAL_FLAG(current_gotten, ACTION_FLAG_INVISIBLE)) {
                    assert(f->out->header.bits & OUT_MARKED_STALE);
                    SET_END(f->out);
                    f->out->header.bits |= OUT_MARKED_STALE;
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

            assert(not current_gotten); // no caching for paths

            REBSPC *derived = Derive_Specifier(f->specifier, current);

            RELVAL *path_at = VAL_ARRAY_AT(current);
            const REBVAL *var_at = Try_Get_Opt_Var(path_at, derived);

            if (
                var_at
                and VAL_TYPE(var_at) == REB_ACTION
                and NOT_VAL_FLAG(var_at, VALUE_FLAG_ENFIXED)
                and GET_VAL_FLAG(var_at, ACTION_FLAG_QUOTES_FIRST_ARG)
            ){
                goto do_path_in_current;
            }
        }

        f->gotten = Try_Get_Opt_Var(f->value, f->specifier);

        if (f->gotten and (
            VAL_TYPE(f->gotten) == REB_ACTION
            and ALL_VAL_FLAGS(
                f->gotten,
                VALUE_FLAG_ENFIXED | ACTION_FLAG_QUOTES_FIRST_ARG
            )
        )){
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

            Fetch_Next_In_Frame(f); // skip the WORD! that invoked the action
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
// being retriggered via EVAL.
//
// Most action evaluations are triggered from a WORD! or PATH!, which jumps in
// at the `process_action` label.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_ACTION: { // literal action in a block
        REBSTR *opt_label = nullptr; // not invoked through a word, "nameless"

        Push_Action(f, VAL_ACTION(current), VAL_BINDING(current));

        // Enfix bit is only retrieved from vars assigned to words in contexts
        // via SET/ENFIX...a literal ACTION! value shouldn't have it.
        //
        assert(NOT_VAL_FLAG(current, VALUE_FLAG_ENFIXED));
        Begin_Action(f, opt_label, ORDINARY_ARG);

        if (NOT_VAL_FLAG(current, ACTION_FLAG_INVISIBLE))
            SET_END(f->out); // clear out previous result

        fallthrough; }

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
        assert(f->eval_type == REB_ACTION); // set by Begin_Action()
        Do_Process_Action_Checks_Debug(f);
      #endif

        assert(DSP >= f->dsp_orig); // path processing may push REFINEMENT!s
        assert(f->refine == LOOKBACK_ARG or f->refine == ORDINARY_ARG);

        TRASH_POINTER_IF_DEBUG(current); // shouldn't be used below
        TRASH_POINTER_IF_DEBUG(current_gotten);

        f->doing_pickups = false;

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
                not f->doing_pickups
                and f->special != f->arg
                and f->param != LOOKBACK_ARG
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

                    f->param = END_NODE; // don't need f->param in facade
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

            unspecialized_refinement:

                if (f->dsp_orig == DSP) // no refinements left on stack
                    goto unused_refinement;

                if (VAL_STORED_CANON(ordered) == param_canon) {
                    DS_DROP; // we're lucky: this was next refinement used
                    f->refine = f->arg; // remember so we can revoke!
                    goto used_refinement;
                }

                --ordered; // not lucky: if in use, this is out of order

            unspecialized_refinement_must_pickup: // only fulfill on 2nd pass

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
                    assert(NOT_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC));

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

                if (
                    (f->out->header.bits & OUT_MARKED_STALE)
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

                if (not GET_ACT_FLAG(FRM_PHASE(f), ACTION_FLAG_INVISIBLE)) {
                    SET_END(f->out);
                    f->out->header.bits |= OUT_MARKED_STALE;
                }

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
            if (GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
                RESET_VAL_HEADER(f->arg, REB_VARARGS);
                INIT_BINDING(f->arg, f->varlist); // frame-based VARARGS!

                Finalize_Current_Arg(f); // sets VARARGS! offset and facade
                goto continue_arg_loop;
            }

    //=//// AFTER THIS, PARAMS CONSUME FROM CALLSITE IF NOT APPLY ////////=//

            assert(f->refine == ORDINARY_ARG or IS_REFINEMENT(f->refine));

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
            assert(not IS_POINTER_TRASH_DEBUG(f->deferred));
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

                Init_Endish_Nulled(f->arg);
                SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
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

                Init_Endish_Nulled(f->arg);
                SET_VAL_FLAG(f->arg, ARG_MARKED_CHECKED);
                goto continue_arg_loop;
            }

            switch (pclass) {

   //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes a DO/NEXT's worth) ////=//

            case PARAM_CLASS_NORMAL: {
                REBFLGS flags = DO_FLAG_FULFILLING_ARG;
                if (not evaluating)
                    flags |= DO_FLAG_EXPLICIT_EVALUATE;

                DECLARE_FRAME (child); // capture DSP *now*
                SET_END(f->arg); // Finalize_Arg() sets to Endish_Nulled
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

                DECLARE_FRAME (child);
                SET_END(f->arg); // Finalize_Arg() sets to Endish_Nulled
                if (Do_Next_In_Subframe_Throws(f->arg, f, flags, child)) {
                    Move_Value(f->out, f->arg);
                    goto abort_action;
                }
                break; }

    //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG /////////////////////////////=//

            case PARAM_CLASS_HARD_QUOTE:
                Quote_Next_In_Frame(f->arg, f); // has VALUE_FLAG_UNEVALUATED
                break;

    //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  ////////////////////////////=//

            case PARAM_CLASS_SOFT_QUOTE:
                if (not IS_QUOTABLY_SOFT(f->value)) {
                    Quote_Next_In_Frame(f->arg, f); // VALUE_FLAG_UNEVALUATED
                    Finalize_Current_Arg(f);
                    goto continue_arg_loop;
                }

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
            assert(
                not In_Typecheck_Mode(f) // already handled, unless...
                or not (f->flags.bits & DO_FLAG_FULLY_SPECIALIZED) // ...this!
            );

            assert(not IS_POINTER_TRASH_DEBUG(f->deferred));
            if (f->deferred)
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
            f->doing_pickups = true;
            goto process_args_for_pickup_or_to_end;
        }

    arg_loop_and_any_pickups_done:;

        assert(IS_END(f->param)); // signals !Is_Action_Frame_Fulfilling()

        if (not In_Typecheck_Mode(f)) { // was fulfilling...
            assert(not IS_POINTER_TRASH_DEBUG(f->deferred));
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
            (f->out->header.bits & OUT_MARKED_STALE)
            or GET_ACT_FLAG(FRM_PHASE(f), ACTION_FLAG_INVISIBLE)
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
        /*assert(f->out->header.bits & (CELL_FLAG_STACK | NODE_FLAG_ROOT)); */

        // Running arbitrary native code can manipulate the bindings or cache
        // of a variable.  It's very conservative to say this, but any word
        // fetches that were done for lookahead are potentially invalidated
        // by every function call.
        //
        f->gotten = nullptr;

        // Cases should be in enum order for jump-table optimization
        // (R_FALSE first, R_TRUE second, etc.)
        //
        // The dispatcher may push functions to the data stack which will be
        // used to process the return result after the switch.
        //
        REB_R r; // initialization would be skipped by gotos
        r = (*PG_Dispatcher)(f); // default just calls FRM_PHASE(f)

        if (r == f->out) {
            //
            // This is the most common result, and taking it out of the
            // switch improves performance.  `Init_Void(D_OUT); return D_OUT`
            // will thus always be faster than `return R_VOID`, which in
            // turn will be faster than `return VOID_CELL` (copies full cell)
            //
            if (not THROWN(f->out))
                goto dispatch_completed;

        out_is_thrown:;

            if (IS_ACTION(f->out)) {
                if (
                    VAL_ACTION(f->out) == NAT_ACTION(unwind)
                    and VAL_BINDING(f->out) == NOD(f->varlist)
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
        }
        else if (not r) {
            //
            // It's not necessarily ideal to test null before the switch, but
            // there isn't really a choice, as dereferencing it would crash.
            // There could be an R_NULL and forbid actual nullptr, but that
            // ruins returning API results directly from dispatchers.
            //
            Init_Nulled(f->out);
        }
        else switch (const_FIRST_BYTE(r->header)) {

        case R_00_FALSE:
            Init_Logic(f->out, false);
            break;

        case R_01_TRUE:
            Init_Logic(f->out, true);
            break;

        case R_02_VOID:
            Init_Void(f->out);
            break;

        case R_03_BLANK:
            Init_Blank(f->out);
            break;

        case R_04_BAR:
            Init_Bar(f->out);
            break;

        case R_05_REDO_CHECKED:

        redo_checked:

            f->param = ACT_FACADE_HEAD(FRM_PHASE(f));
            f->arg = FRM_ARGS_HEAD(f);
            f->special = f->arg;
            f->refine = ORDINARY_ARG; // no gathering, but need for assert
            assert(not GET_ACT_FLAG(FRM_PHASE(f), ACTION_FLAG_INVISIBLE));
            SET_END(f->out);
            f->out->header.bits |= OUT_MARKED_STALE;
            assert(IS_POINTER_TRASH_DEBUG(f->deferred));
            goto process_action;

        case R_06_REDO_UNCHECKED:
            //
            // This instruction represents the idea that it is desired to
            // run the f->phase again.  The dispatcher may have changed the
            // value of what f->phase is, for instance.
            //
            assert(not GET_ACT_FLAG(FRM_PHASE(f), ACTION_FLAG_INVISIBLE));
            SET_END(f->out);
            f->out->header.bits |= OUT_MARKED_STALE;
            assert(IS_POINTER_TRASH_DEBUG(f->deferred));
            goto redo_unchecked;

        case R_07_REEVALUATE_CELL:
            evaluating = true; // unnecessary?
            goto prep_for_reevaluate;

        case R_08_REEVALUATE_CELL_ONLY:
            evaluating = false;
            goto prep_for_reevaluate;

        case R_09_INVISIBLE: {
            assert(GET_ACT_FLAG(FRM_PHASE(f), ACTION_FLAG_INVISIBLE));

            // It is possible that when the elider ran, that there really was
            // no output in the cell yet (e.g. `do [comment "hi" ...]`) so it
            // would still be END after the fact.
            //
            // !!! Ideally we would check that f->out hadn't changed, but
            // that would require saving the old value somewhere...
            //
          #if defined(DEBUG_UNREADABLE_BLANKS)
            assert(
                (f->out->header.bits & OUT_MARKED_STALE)
                or not IS_UNREADABLE_DEBUG(f->out)
            );
          #endif

            // If an invisible is at the start of a frame and there's nothing
            // after it, it has to retrigger until it finds something (or
            // until it hits the end of the frame).
            //
            //     do [comment "a" 1] => 1
            //
            // Use same mechanic as EVAL by loading next item.
            //
            if (
                (f->out->header.bits & OUT_MARKED_STALE)
                and not FRM_AT_END(f)
            ){
                Derelativize(&f->cell, f->value, f->specifier);
                Fetch_Next_In_Frame(f);

                evaluating = true; // unnecessary?
                goto prep_for_reevaluate;
            }

            goto skip_output_check; }

        prep_for_reevaluate:

            current = &f->cell;
            f->eval_type = VAL_TYPE(current);
            current_gotten = nullptr;

            // The f->gotten (if any) was the fetch for f->value, not what we
            // just put in current.  We conservatively clear this cache:
            // assume for instance that f->value is a WORD! that looks up to
            // a value which is in f->gotten, and then f->cell contains a
            // zero-arity function which changes the value of that word.  It
            // might be possible to finesse use of this cache and clear it
            // only if such cases occur, but for now don't take chances.
            //
            assert(not f->gotten);

            Drop_Action(f);
            goto reevaluate; // we don't move index!

        case R_0A_REFERENCE:
        case R_0B_IMMEDIATE:
        case R_0C_UNHANDLED: // internal use only, shouldn't be returned
        case R_0D_END:
            assert(false);
            break;

        default: {
            // can be any cell--including thrown value
            // !!! main focus here is auto-handling API cells, add that!
            //
            assert(r->header.bits & NODE_FLAG_CELL);
            Move_Value(f->out, r);
            if (GET_VAL_FLAG(r, NODE_FLAG_ROOT)) {
                assert(not THROWN(r)); // API values can't be thrown
                assert(not IS_NULLED(r)); // API values can't be null
                if (NOT_VAL_FLAG(r, NODE_FLAG_MANAGED))
                    rebRelease(r);
                break;
            }
            if (THROWN(f->out))
                goto out_is_thrown;
            break; }
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
                NULLIZE(KNOWN(&f->cell)), // nulled cell => nullptr for API
                rebEND
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
            Push_Action(
                f,
                VAL_ACTION(current_gotten),
                VAL_BINDING(current_gotten)
            );

            REBSTR *opt_label = VAL_WORD_SPELLING(current);
            if (GET_VAL_FLAG(current_gotten, VALUE_FLAG_ENFIXED)) {
                //
                // Note: The usual dispatch of enfix functions is not via a
                // REB_WORD in this switch, it's by some code at the end of
                // the switch.  So you only see this in cases like `(+ 1 2)`,
                // or after ACTION_FLAG_INVISIBLE e.g. `10 comment "hi" + 20`.
                //
                Begin_Action(f, opt_label, LOOKBACK_ARG);
                assert(f->out->header.bits & OUT_MARKED_STALE);
            }
            else {
                Begin_Action(f, opt_label, ORDINARY_ARG);
                if (NOT_VAL_FLAG(current_gotten, ACTION_FLAG_INVISIBLE)) {
                    SET_END(f->out);
                    f->out->header.bits |= OUT_MARKED_STALE;
                }
            }

            goto process_action;
        }

        if (IS_NULLED(current_gotten)) // need `:x` if `x` is unset
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
            if (IS_NULLED_OR_VOID(f->value))
                fail (Error_Need_Value_Core(current, f->specifier));

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

            if (IS_NULLED_OR_VOID(f->out))
                fail (Error_Need_Value_Raw(DS_TOP));

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
// If a GROUP! is seen then it generates another call into Do_Core().  The
// resulting value for this step will be the outcome of that evaluation.
//
// Empty groups evaluate to #[void].  If one considers `while [] [...]`, that
// is more appropriately an error than acting like the block is falsey, and
// for consistency this suggests () should produce something that is neither
// conditionally true nor false.  #[void] fits the bill, null does not.
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

        // The f->gotten we fetched for lookahead could become invalid when
        // we run the arbitrary code here.  Have to lose the cache.
        //
        f->gotten = nullptr;

        REBSPC *derived = Derive_Specifier(f->specifier, current);
        SET_END(f->out);
        if (Do_At_Throws(
            f->out,
            VAL_ARRAY(current), // the GROUP!'s array
            VAL_INDEX(current), // index in REBVAL (may not be head)
            derived
        )){
            goto finished;
        }

        CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED); // e.g. "endish" null
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

        if (IS_NULLED(f->out)) // need `:x/y` if `y` is unset
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

            Push_Action(f, VAL_ACTION(f->out), VAL_BINDING(f->out));

            // !!! Paths are currently never enfixed.  It's a problem which is
            // difficult to do efficiently, as well as introduces questions of
            // running GROUP! in paths twice--once for lookahead, and then
            // possibly once again if the lookahead reported non-enfix.  It's
            // something that really should be made to work *when it can*.
            //
            Begin_Action(f, opt_label, ORDINARY_ARG);
            SET_END(f->out); // loses enfix left hand side, invisible passthru
            f->out->header.bits |= OUT_MARKED_STALE;
            goto process_action;
        }

        assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
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

            if (IS_NULLED_OR_VOID(f->value))
                fail (Error_Need_Value_Core(current, f->specifier));

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

            if (IS_NULLED_OR_VOID(f->out))
                fail (Error_Need_Value_Raw(DS_TOP));

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

        assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
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

        assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
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
// Expression barriers are "invisibles", and hence as many of them have to
// be processed at the end of the loop as there are--they can't be left in
// the source feed, else `evaluate [1 + 2 | | | |] 'pos` would not be able
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
// [VOID!]
//
// VOID is "evaluatively unfriendly", and unlike NULL is an actual value.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_VOID:
        fail ("VOID! cells cannot be evaluated");

//==//////////////////////////////////////////////////////////////////////==//
//
// [NULL]
//
// NULL is not an ANY-VALUE!, and nulled cells are not allowed in ANY-ARRAY!
// exposed to the user.  So usually, a DO shouldn't be able to see them,
// unless they are un-evaluated...e.g. `Apply_Only_Throws()` passes in a
// NULLED_CELL as an evaluation-already-accounted-for parameter to a function.
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

    case REB_MAX_NULLED:
        if (evaluating == GET_VAL_FLAG(current, VALUE_FLAG_EVAL_FLIP)) {
            Init_Nulled(f->out); // it's inert, treat as okay
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

    if (not f->gotten)
        f->gotten = Try_Get_Opt_Var(f->value, f->specifier);
    else {
        // !!! a particularly egregious hack in EVAL-ENFIX lets us simulate
        // enfix for a function whose value is not enfix.  This means the
        // value in f->gotten isn't the fetched function, but the function
        // plus a VALUE_FLAG_ENFIXED.  We discern this hacky case by noting
        // if f->deferred is precisely equal to BLANK_VALUE.
        //
        assert(
            f->gotten == Try_Get_Opt_Var(f->value, f->specifier)
            or (f->prior->deferred == BLANK_VALUE) // !!! hack
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
        not f->gotten
        or VAL_TYPE(f->gotten) != REB_ACTION
        or NOT_VAL_FLAG(f->gotten, VALUE_FLAG_ENFIXED)
    ){
        if (not (f->flags.bits & DO_FLAG_TO_END)) {
            //
            // Since it's a new expression, a DO/NEXT doesn't want to run it
            // even if invisible, as it's not completely invisible (enfixed)
            //
            goto finished;
        }

        if (
            f->gotten
            and VAL_TYPE(f->gotten) == REB_ACTION
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
        current_gotten = f->gotten; // if nullptr, the word will error

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

    Push_Action(f, VAL_ACTION(f->gotten), VAL_BINDING(f->gotten));
    Begin_Action(f, VAL_WORD_SPELLING(f->value), LOOKBACK_ARG);

    Fetch_Next_In_Frame(f); // advances f->value
    goto process_action;

abort_action:;

    assert(THROWN(f->out));

    Drop_Action(f);
    DS_DROP_TO(f->dsp_orig); // any unprocessed refinements or chains on stack

finished:;

    // The unevaluated flag is meaningless outside of arguments to functions.

    if (not (f->flags.bits & DO_FLAG_FULFILLING_ARG))
        f->out->header.bits &= ~VALUE_FLAG_UNEVALUATED; // may be an END cell

  #if defined(DEBUG_STALE_ARGS) // see notes on flag definition
    if (f->flags.bits & DO_FLAG_FULFILLING_ARG)
        f->out->header.bits &= ~OUT_MARKED_STALE; // same as ARG_MARKED_CHECKED
  #endif

  #if defined(DEBUG_UNREADABLE_BLANKS)
    if (IS_UNREADABLE_DEBUG(f->out)) {
        assert(FRM_AT_END(f));
        SET_END(f->out);
        f->out->header.bits |= OUT_MARKED_STALE;
    }
  #endif

  #if !defined(NDEBUG)
    Do_Core_Exit_Checks_Debug(f); // will get called unless a fail() longjmps
  #endif

    // All callers must inspect for THROWN(f->out), and most should also
    // inspect for FRM_AT_END(f)
}
