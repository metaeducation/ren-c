//
//  File: %c-eval.c
//  Summary: "Central Interpreter Evaluator"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
    return FUNC_DISPATCHER(f->phase)(f);
}


static inline REBOOL Start_New_Expression_Throws(REBFRM *f) {
  #if defined(DEBUG_UNREADABLE_BLANKS)
    assert(IS_UNREADABLE_DEBUG(f->out) || IS_END(f->out));
  #endif

    assert(Eval_Count >= 0);
    if (--Eval_Count == 0) {
        //
        // Note that Do_Signals_Throws() may do a recycle step of the GC, or
        // it may spawn an entire interactive debugging session via
        // breakpoint before it returns.  It may also FAIL and longjmp out.
        //
        if (Do_Signals_Throws(f->out))
            return TRUE;
    }

    UPDATE_EXPRESSION_START(f); // !!! See FRM_INDEX() for caveats

  #if defined(DEBUG_UNREADABLE_BLANKS)
    assert(IS_UNREADABLE_DEBUG(f->out) || IS_END(f->out));
  #endif

    return FALSE;
}


#if !defined(NDEBUG)
    #define START_NEW_EXPRESSION_MAY_THROW(f,g) \
        Do_Core_Expression_Checks_Debug(f); \
        if (Start_New_Expression_Throws(f)) \
            g; \
        evaluating = NOT((f)->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);
#else
    #define START_NEW_EXPRESSION_MAY_THROW(f,g) \
        if (Start_New_Expression_Throws(f)) \
            g; \
        evaluating = NOT((f)->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);
#endif


#ifdef DEBUG_COUNT_TICKS
    //
    // Macro is used to mutate local `tick` variable in Do_Core (for easier
    // browsing in the watchlist) as well as to not be in a deeper stack level
    // than Do_Core when a TICK_BREAKPOINT is hit.
    //
    // We bound the count at the max unsigned 32-bit, since otherwise it would
    // roll over to zero and print a message that wasn't asked for, which
    // is annoying even in a debug build.  (It's actually a REBUPT, so this
    // wastes possible bits in the 64-bit build, but there's no MAX_REBUPT.)
    //
    #define UPDATE_TICK_DEBUG(cur) \
        do { \
            if (TG_Tick < MAX_U32) \
                tick = f->tick = ++TG_Tick; \
            else \
                tick = f->tick = MAX_U32; \
            if ( \
                (TG_Break_At_Tick != 0 && tick >= TG_Break_At_Tick) \
                || tick == TICK_BREAKPOINT \
            ){ \
                Debug_Fmt("TICK_BREAKPOINT at %d", tick); \
                Dump_Frame_Location((cur), f); \
                debug_break(); /* see %debug_break.h */ \
                TG_Break_At_Tick = 0; \
            } \
        } while (FALSE)
#else
    #define UPDATE_TICK_DEBUG(cur) \
        NOOP
#endif


static inline void Drop_Function(REBFRM *f) {
    assert(NOT(THROWN(f->out)));

    const REBOOL drop_chunks = TRUE;
    Drop_Function_Core(f, drop_chunks);
}

static inline void Abort_Function(REBFRM *f) {
    assert(THROWN(f->out));

    const REBOOL drop_chunks = TRUE;
    Drop_Function_Core(f, drop_chunks);

    // If a function call is aborted, there may be pending refinements (if
    // in the gathering phase) or functions (if running a chainer) on the
    // data stack.  They must be dropped to balance.
    //
    DS_DROP_TO(f->dsp_orig);
}

static inline void Link_Vararg_Param_To_Frame(REBFRM *f, REBOOL make) {
    assert(GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC));

    // Store the offset so that both the f->arg and f->param locations can
    // be quickly recovered, while using only a single slot in the REBVAL.
    //
    f->arg->payload.varargs.param_offset = f->arg - f->args_head;
    f->arg->payload.varargs.facade = FUNC_FACADE(f->phase);

    // The data feed doesn't necessarily come from the frame
    // that has the parameter and the argument.  A varlist may be
    // chained such that its data came from another frame, or just
    // an ordinary array.
    //
    if (make) {
        VAL_RESET_HEADER(f->arg, REB_VARARGS);
        INIT_BINDING(f->arg, f); // may reify later
    }
    else
        assert(VAL_TYPE(f->arg) == REB_VARARGS);
}


//
// f->refine is a bit tricky.  If it IS_LOGIC() and TRUE, then this means that
// a refinement is active but revokable, having its arguments gathered.  So
// it actually points to the f->arg of the active refinement slot.  If
// evaluation of an argument in this state produces no value, the refinement
// must be revoked, and its value mutated to be FALSE.
//
// But all the other values that f->refine can hold are read-only pointers
// that signal something about the argument gathering state:
//
// * If NULL, then refinements are being skipped and the arguments
//   that follow should not be written to.
//
// * If BLANK_VALUE, this is an arg to a refinement that was not used in
//   the invocation.  No consumption should be performed, arguments should
//   be written as unset, and any non-unset specializations of arguments
//   should trigger an error.
//
// * If FALSE_VALUE, this is an arg to a refinement that was used in the
//   invocation but has been *revoked*.  It still consumes expressions
//   from the callsite for each remaining argument, but those expressions
//   must not evaluate to any value.
//
// * If EMPTY_BLOCK, it's an ordinary arg...and not a refinement.  It will
//   be evaluated normally but is not involved with revocation.
//
// * If EMPTY_STRING, the evaluator's next argument fulfillment is the
//   left-hand argument of a lookback operation.  After that fulfillment,
//   it will be transitioned to EMPTY_BLOCK.
//
// Because of how this lays out, IS_TRUTHY() can be used to determine if an
// argument should be type checked normally...while IS_FALSEY() means that the
// arg's bits must be set to void.  Since the skipping-refinement-args case
// doesn't write to arguments at all, it doesn't get to the point where the
// decision of type checking needs to be made...so using NULL for that means
// the comparison is a little bit faster.
//
// These special values are all pointers to read-only cells, but are cast to
// mutable in order to be held in the same pointer that might write to a
// refinement to revoke it.  Note that since literal pointers are used, tests
// like `f->refine == BLANK_VALUE` are faster than `IS_BLANK(f->refine)`.
//

#define SKIPPING_REFINEMENT_ARGS \
    NULL // NULL comparison is generally faster than to arbitrary pointer

#define ARG_TO_UNUSED_REFINEMENT \
    m_cast(REBVAL*, BLANK_VALUE)

#define ARG_TO_REVOKED_REFINEMENT \
    m_cast(REBVAL*, FALSE_VALUE)

#define ORDINARY_ARG \
    m_cast(REBVAL*, EMPTY_BLOCK)

#define LOOKBACK_ARG \
    m_cast(REBVAL*, EMPTY_STRING)


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
//     REBVAL pointer to which the evaluation's result should be written,
//     should be to writable memory in a cell that lives above this call to
//     Do_Core on the stack (e.g. not in an array).
//
//     f->value
//     Fetched first value to execute (cannot be an END marker)
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
// More detailed assertions of the preconditions, postconditions, and state
// at each evaluation step are contained in %d-eval.c
//
void Do_Core(REBFRM * const f)
{
  #if defined(DEBUG_COUNT_TICKS)
    REBUPT tick = f->tick = TG_Tick; // snapshot start tick
  #endif

    // !!! Experimental feature that can kick the evaluator into a "mock" mode
    // where it attempts to complete expressions without actually having any
    // side effects.  Flag can't change during this call to Do_Core, so cache
    // it in a local variable.
    //
    REBOOL neutral = LOGICAL(f->flags.bits & DO_FLAG_NEUTRAL);

    // Capture the data stack pointer on entry.  Refinements are pushed to
    // the stack and need to be checked if any are not processed.  Also things
    // like the CHAIN dispatcher uses it to queue pending functions.  This
    // cannot be done in Push_Frame(), because some routines (like Reduce_XXX)
    // reuse the frame across multiple calls and accrue stack state, and that
    // stack state should be skipped when considering the usages in Do_Core()
    //
    f->dsp_orig = DSP;

    REBOOL evaluating; // set on every iteration (varargs do, EVAL/ONLY...)

    // Handling of deferred lookbacks may need to re-enter the frame and get
    // back to the processing it had put off.
    //
    if (f->flags.bits & DO_FLAG_POST_SWITCH) {
        evaluating = NOT(f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);

        // !!! Note EVAL-ENFIX does a crude workaround to preserve this check.
        //
        assert(f->prior->deferred != NULL);

        f->deferred = NULL;
        assert(NOT_END(f->out));
        f->flags.bits &= ~DO_FLAG_POST_SWITCH; // !!! unnecessary?
        goto post_switch;
    }

    // END signals no evaluations have produced a result yet, even if some
    // functions have run (e.g. COMMENT with FUNC_FLAG_INVISIBLE).  It also
    // is initialized bits to be safe for the GC to inspect and protect, and
    // triggers noisy alarms to help detect when someone attempts to evaluate
    // into a cell in an array (which may have its memory moved).
    //
    SET_END(f->out);

    // APPLY and a DO of a FRAME! both use process_function.
    //
    if (f->flags.bits & DO_FLAG_APPLYING) {
        evaluating = NOT(f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE);

        assert(NOT(neutral)); // !!! This shouldn't happen

        f->deferred = NULL;
        f->refine = ORDINARY_ARG; // "APPLY infix" not supported
        goto process_function;
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
    //
    // ^-- doesn't advance expression index, so `eval x` starts with `eval`
    // also EVAL/ONLY may change `evaluating` to FALSE for a cycle

    UPDATE_TICK_DEBUG(current);
    // v-- This is the TICK_BREAKPOINT or C-DEBUG-BREAK landing spot --v

    f->deferred = NULL;

    if (NOT(evaluating) == NOT_VAL_FLAG(current, VALUE_FLAG_EVAL_FLIP)) {
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
    if (FRM_HAS_MORE(f) && IS_WORD(f->value) && evaluating) {
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
                VAL_TYPE_OR_0(current_gotten) == REB_FUNCTION // END is REB_0
                && NOT_VAL_FLAG(current_gotten, VALUE_FLAG_ENFIXED)
                && GET_VAL_FLAG(current_gotten, FUNC_FLAG_QUOTES_FIRST_ARG)
            ){
                // Yup, it quotes.  We could look for a conflict and call
                // it an error, but instead give the left hand side precedence
                // over the right.  This means something like:
                //
                //     foo: quote -> [print quote]
                //
                // Would be interpreted as:
                //
                //     foo: (quote ->) [print quote]
                //
                // This is a good argument for not making enfixed operations
                // that hard-quote things that can dispatch functions.  A
                // soft-quote would give more flexibility to override the
                // left hand side's precedence, e.g. the user writes:
                //
                //     foo: ('quote) -> [print quote]
                //
                Push_Function(
                    f,
                    VAL_WORD_SPELLING(current),
                    VAL_FUNC(current_gotten),
                    VAL_BINDING(current_gotten)
                );

                f->refine = ORDINARY_ARG;
                if (NOT_VAL_FLAG(current_gotten, FUNC_FLAG_INVISIBLE)) {
                  #if defined(DEBUG_UNREADABLE_BLANKS)
                    assert(IS_UNREADABLE_DEBUG(f->out) || IS_END(f->out));
                  #endif
                    SET_END(f->out);
                }
                goto process_function;
            }
        }
        else if (
            f->eval_type == REB_PATH
            && VAL_LEN_AT(current) > 0
            && IS_WORD(VAL_ARRAY_AT(current))
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
                VAL_TYPE_OR_0(var_at) == REB_FUNCTION // END is REB_0
                && NOT_VAL_FLAG(var_at, VALUE_FLAG_ENFIXED)
                && GET_VAL_FLAG(var_at, FUNC_FLAG_QUOTES_FIRST_ARG)
            ){
                goto do_path_in_current;
            }
        }

        f->gotten = Get_Opt_Var_Else_End(f->value, f->specifier);

        if (
            VAL_TYPE_OR_0(f->gotten) == REB_FUNCTION // END is REB_0
            && ALL_VAL_FLAGS(
                f->gotten, VALUE_FLAG_ENFIXED | FUNC_FLAG_QUOTES_FIRST_ARG
            )
        ){
            Push_Function(
                f,
                VAL_WORD_SPELLING(f->value),
                VAL_FUNC(f->gotten),
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
            goto process_function;
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
// [FUNCTION!] (lookback or non-lookback)
//
// If a function makes it to the SWITCH statement, that means it is either
// literally a function value in the array (`do compose [(:+) 1 2]`) or is
// being retriggered via EVAL
//
// Most function evaluations are triggered from a SWITCH on a WORD! or PATH!,
// which jumps in at the `process_function` label.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_FUNCTION: // literal function in a block
        Push_Function(
            f,
            NULL, // no label, nameless literal function direct in source
            VAL_FUNC(current),
            VAL_BINDING(current)
        );

        // It should not be possible to encounter a literal FUNCTION! value
        // with the enfix bit set, as this bit can only be retrieved from
        // words that are assigned in contexts via SET/ENFIX.
        //
        assert(NOT_VAL_FLAG(current, VALUE_FLAG_ENFIXED));

        if (NOT_VAL_FLAG(current, FUNC_FLAG_INVISIBLE))
            SET_END(f->out); // clear out previous result
        f->refine = ORDINARY_ARG;

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! ARGUMENT FULFILLMENT AND/OR TYPE CHECKING PROCESS
    //
    //==////////////////////////////////////////////////////////////////==//

        // This one processing loop is able to handle ordinary function
        // invocation, specialization, and type checking of an already filled
        // function frame.  It walks through both the formal parameters (in
        // the spec) and the actual arguments (in the call frame) using
        // pointer incrementation.
        //
        // At this point, f->special can be set to:
        //
        // * NULL to indicate ordinary argument fulfillment for all the
        //   relevant args, refinements, and refinement args of the function
        //
        // * f->args_head, in order to indicate that the arguments should
        //   only be type-checked.  The f->special pointer is incremented
        //   along so that at each point `f->special == f->arg` will be
        //   a valid test for this intent.
        //
        // * some other pointer to an array of REBVAL which is the same
        //   length as the argument list.  This indicates that any non-void
        //   values in that array should be used in lieu of an ordinary
        //   argument...e.g. that argument has been "specialized".
        //
        // Hence one can say `if (f->special != NULL) ++f->special`, and
        // not need an extra branch to differentiate the second two intents
        // if there is no need to differentiate them.
        //
        // If arguments are actually being fulfilled into the slots, those
        // slots start out as trash.  Yet the GC has access to the frame list,
        // so it can examine f->arg and avoid trying to protect the random
        // bits that haven't been fulfilled yet.
        //
        // Based on the parameter type, it may be necessary to "consume" an
        // expression from values that come after the invocation point.  But
        // not all params will consume arguments for all calls.

    process_function:
    #if !defined(NDEBUG)
        if (f->refine == ORDINARY_ARG) {
            if (NOT_END(f->out))
                assert(GET_FUN_FLAG(f->phase, FUNC_FLAG_INVISIBLE));
        }
        else {
            assert(f->refine == LOOKBACK_ARG);
            ASSERT_NOT_TRASH_IF_DEBUG(f->out);
        }
    #endif

        TRASH_POINTER_IF_DEBUG(current); // shouldn't be used below
        TRASH_POINTER_IF_DEBUG(current_gotten);

        assert(f->deferred == NULL);

        // There may be refinements pushed to the data stack to process, if
        // the call originated from a path dispatch.
        //
        assert(DSP >= f->dsp_orig);

        f->arg = f->args_head;
        f->param = FUNC_FACADE_HEAD(f->phase);

        // DECLARE_FRAME() starts out f->cell as valid GC-visible bits, and
        // as it's used for various temporary purposes it should remain valid.
        // But its contents could be anything, based on that temporary
        // purpose.  Help hint functions not to try to read from it before
        // they overwrite it with their own content.
        //
        // !!! Allowing the release build to "leak" temporary cell state to
        // natives may be bad, and there are advantages to being able to
        // count on this being an END.  However, unless one wants to get in
        // the habit of zeroing out all temporary state for "security" reasons
        // then clients who call Do_Next_In_Frame() would be able to see it
        // anyway.  For now, do the more performant thing and leak whatever
        // is in f->cell to the function in the release build, to avoid
        // paying for the initialization.
        //
    #if !defined(NDEBUG)
        Init_Unreadable_Blank(&f->cell);
    #endif

        enum Reb_Param_Class pclass; // gotos would cross it if inside loop

        f->doing_pickups = FALSE; // still looking for way to encode in refine

        while (NOT_END(f->param)) {
            pclass = VAL_PARAM_CLASS(f->param);

    //=//// A /REFINEMENT ARG /////////////////////////////////////////////=//

            // Refinements are checked first for a reason.  This is to
            // short-circuit based on the `doing_pickups` flag before redoing
            // fulfillments on arguments that have already been handled.
            //
            // The reason an argument might have already been handled is
            // that some refinements have to reach back and be revisited after
            // the original parameter walk.  They can't be fulfilled in a
            // single pass because these two calls mean different things:
            //
            //     foo: func [a /b c /d e] [...]
            //
            //     foo/b/d (1 + 2) (3 + 4) (5 + 6)
            //     foo/d/b (1 + 2) (3 + 4) (5 + 6)
            //
            // The order of refinements in the definition (b d) may not match
            // what order the refinements are invoked in the path.  This means
            // the "visitation order" of the parameters while walking across
            // parameters in the array might not match the "consumption order"
            // of the expressions that are being fetched from the callsite.
            //
            // Hence refinements are targeted to be revisited by "pickups"
            // after the initial parameter walk.  An out-of-order refinement
            // makes a note in the stack about a parameter and arg position
            // it sees that it will need to come back to.  A REB_0_PICKUP
            // is used to track this (it holds a cache of the parameter and
            // argument position).

            if (pclass == PARAM_CLASS_REFINEMENT) {

                if (f->doing_pickups) {
                    f->param = END; // !Is_Function_Frame_Fulfilling
                #if !defined(NDEBUG)
                    f->arg = m_cast(REBVAL*, END); // checked after
                #endif
                    break;
                }

                if (f->special != NULL) {
                    if (f->special == f->arg) {
                        //
                        // We're just checking the values already in the
                        // frame, so fall through and test the arg slot.
                        // However, offer a special tolerance here for void
                        // since MAKE FRAME! fills all arg slots with void.
                        //
                        if (IS_VOID(f->arg)) {
                            Prep_Stack_Cell(f->arg);
                            Init_Logic(f->arg, FALSE);
                        }
                    }
                    else {
                        // Voids in specializations mean something different,
                        // that the refinement is left up to the caller.
                        //
                        if (IS_VOID(f->special)) {
                            ++f->special;
                            goto unspecialized_refinement;
                        }

                        Prep_Stack_Cell(f->arg);
                        Move_Value(f->arg, f->special);
                    }

                    if (NOT(IS_LOGIC(f->arg)))
                        fail (Error_Non_Logic_Refinement(f->param, f->arg));

                    if (IS_TRUTHY(f->arg))
                        f->refine = f->arg; // remember so we can revoke!
                    else
                        f->refine = ARG_TO_UNUSED_REFINEMENT; // (read-only)

                    ++f->special;
                    goto continue_arg_loop;
                }

    //=//// UNSPECIALIZED REFINEMENT SLOT (no consumption) ////////////////=//

            unspecialized_refinement:

                if (f->dsp_orig == DSP) { // no refinements left on stack
                    Prep_Stack_Cell(f->arg);
                    Init_Logic(f->arg, FALSE);
                    f->refine = ARG_TO_UNUSED_REFINEMENT; // "don't consume"
                    goto continue_arg_loop;
                }

                f->refine = DS_TOP;

                if (
                    IS_WORD(f->refine) &&
                    (
                        VAL_WORD_SPELLING(f->refine) // canon when pushed
                        == VAL_PARAM_CANON(f->param) // #2258
                    )
                ){
                    DS_DROP; // we're lucky: this was next refinement used

                    Prep_Stack_Cell(f->arg);
                    Init_Logic(f->arg, TRUE); // marks refinement used
                    f->refine = f->arg; // "consume args (can be revoked)"
                    goto continue_arg_loop;
                }

                --f->refine; // not lucky: if in use, this is out of order

                for (; f->refine > DS_AT(f->dsp_orig); --f->refine) {
                    if (
                        NOT(IS_WORD(f->refine)) // non-refinement
                        || (
                            VAL_WORD_SPELLING(f->refine) // canon when pushed
                            != VAL_PARAM_CANON(f->param) // #2258
                        )
                    ){
                        continue;
                    }

                    Prep_Stack_Cell(f->arg);
                    Init_Logic(f->arg, TRUE); // marks refinement used

                    // The call uses this refinement but we'll have to
                    // come back to it when the expression index to
                    // consume lines up.  Make a note of the param
                    // and arg and poke them into the stack value.
                    //
                    f->refine->header.bits &= CELL_MASK_RESET;
                    f->refine->header.bits |= HEADERIZE_KIND(REB_0_PICKUP);
                    f->refine->payload.pickup.param
                        = const_KNOWN(f->param);
                    f->refine->payload.pickup.arg = f->arg;

                    // "consume args later" (promise not to change)
                    //
                    f->refine = SKIPPING_REFINEMENT_ARGS;
                    goto continue_arg_loop;
                }

                // Wasn't in the path and not specialized, so not present
                //
                Prep_Stack_Cell(f->arg);
                Init_Logic(f->arg, FALSE);
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
                Init_Void(f->arg); // faster than checking bad specializations
                if (f->special != NULL)
                    ++f->special;
                goto continue_arg_loop;

            case PARAM_CLASS_RETURN:
                assert(VAL_PARAM_SYM(f->param) == SYM_RETURN);

                if (NOT_VAL_FLAG(FUNC_VALUE(f->phase), FUNC_FLAG_RETURN)) {
                    Prep_Stack_Cell(f->arg);
                    Init_Void(f->arg);
                    goto continue_arg_loop;
                }

                Prep_Stack_Cell(f->arg);
                Move_Value(f->arg, NAT_VALUE(return));

                INIT_BINDING(f->arg, f); // may reify later

                if (f->special != NULL)
                    ++f->special; // specialization being overwritten is right
                goto continue_arg_loop;

            case PARAM_CLASS_LEAVE:
                assert(VAL_PARAM_SYM(f->param) == SYM_LEAVE);

                if (NOT_VAL_FLAG(FUNC_VALUE(f->phase), FUNC_FLAG_LEAVE)) {
                    Prep_Stack_Cell(f->arg);
                    Init_Void(f->arg);
                    goto continue_arg_loop;
                }

                Prep_Stack_Cell(f->arg);
                Move_Value(f->arg, NAT_VALUE(leave));

                INIT_BINDING(f->arg, f); // may reify later

                if (f->special != NULL)
                    ++f->special; // specialization being overwritten is right
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

                if (f->special != NULL)
                    ++f->special;
                goto continue_arg_loop;
            }

            if (f->special != NULL) {
                if (f->special == f->arg) {
                    //
                    // Just running the loop to verify arguments/refinements...
                    //
                    ++f->special;
                    goto check_arg;
                }

    //=//// SPECIALIZED ARG (already filled, so does not consume) /////////=//

                if (
                    IS_VOID(f->special)
                    && NOT(f->flags.bits & DO_FLAG_APPLYING)
                ){
                    // When not doing an APPLY (or DO of a FRAME!), then
                    // a void specialized value means this particular argument
                    // is not specialized.  Still must increment the pointer
                    // before falling through to ordinary fulfillment.
                    //
                    ++f->special;
                }
                else {
                    Prep_Stack_Cell(f->arg);
                    Move_Value(f->arg, f->special);

                    ++f->special;
                    goto check_arg; // normal checking, handles errors also

                    // !!! If SPECIALIZE checked the argument slots at
                    // specialization-time, it would not be necessary to check
                    // it here.  It could just `goto continue_arg_loop`.
                    // Since there are many common specializations in use,
                    // it's probably worth it to do the optimization and then
                    // just assert types match here in the debug build.
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

                // !!! Can a variadic lookback argument be meaningful?
                // Arguably, if you have an arity-1 function which is variadic
                // and you enfix it, then giving it a feed of either 0 or 1
                // values and only letting it take from the left would make
                // sense.  But if it's arity-2 (e.g. multiple variadic taps)
                // does that make any sense?
                //
                // It may be too wacky to worry about, and SET/LOOKBACK should
                // just prohibit it.
                //
                assert(NOT_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC));

                Prep_Stack_Cell(f->arg);

                if (IS_END(f->out)) {
                    //
                    // Seeing an END in the output slot could mean that there
                    // was really "nothing" to the left, or it could be a
                    // consequence of a frame being in an argument gathering
                    // mode, e.g. the `+` here will perceive "nothing":
                    //
                    //     if + 2 [...]

                    if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                        fail (Error_No_Arg(f, f->param));

                    Init_Void(f->arg);
                    goto continue_arg_loop;
                }

                switch (pclass) {
                case PARAM_CLASS_NORMAL:
                    Move_Value(f->arg, f->out);
                    break;

                case PARAM_CLASS_TIGHT:
                    Move_Value(f->arg, f->out);
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
                            Abort_Function(f);
                            goto finished;
                        }
                    }
                    else {
                        Move_Value(f->arg, f->out);
                        SET_VAL_FLAG(f->arg, VALUE_FLAG_UNEVALUATED);
                    }
                    break;

                default:
                    assert(FALSE);
                }

                if (NOT(GET_FUN_FLAG(f->phase, FUNC_FLAG_INVISIBLE)))
                    SET_END(f->out);
                goto check_arg;
            }

    //=//// VARIADIC ARG (doesn't consume anything *yet*) /////////////////=//

            // Evaluation argument "hook" parameters (marked in MAKE FUNCTION!
            // by a `[[]]` in the spec, and in FUNC by `<...>`).  They point
            // back to this call through a reified FRAME!, and are able to
            // consume additional arguments during the function run.
            //
            if (GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
                //
                // This is *the* killer case for neutral.  It cannot keep
                // skipping without running code.  (Note: EVAL has been tagged
                // variadic, because it's in the same category.)  We throw
                // to unwind all the way up and tell the neutral request
                // we got nothing.
                //
                if (neutral) {
                    Move_Value(f->out, BLANK_VALUE);
                    CONVERT_NAME_TO_THROWN(f->out, BAR_VALUE);

                    Abort_Function(f);
                    goto finished;
                }

                const REBOOL make = TRUE;
                Prep_Stack_Cell(f->arg);
                Link_Vararg_Param_To_Frame(f, make);
                goto continue_arg_loop; // new value, type guaranteed correct
            }

    //=//// AFTER THIS, PARAMS CONSUME FROM CALLSITE IF NOT APPLY ////////=//

            assert(
                f->refine == ORDINARY_ARG
                || (IS_LOGIC(f->refine) && IS_TRUTHY(f->refine))
            );

    //=//// START BY HANDLING ANY DEFERRED ENFIX PROCESSING //////////////=//

            // With an expression like `if 10 and 20` we may have filled IF's
            // first arg slot with 10 then returned, hoping `if 10` could
            // form a complete expression, and take care of the enfix after
            // the function call is finished (at the end of the switch).
            //
            // But if we looped back to here, it would mean we're now trying
            // to consume another argument at the callsite.  That means we
            // won't be getting to the end of the switch before handling
            // this deferred enfix.  In the case of `if 10 and 20`, we'd be
            // trying to fill the `body` slot with `and 20`...which will fail,
            // saying AND has no left hand argument.
            //
            // Rather than raise an error, we kept a `f->deferred` field that
            // points at the previously filled f->arg slot.  We go back and
            // re-enter a sub-frame for the argument via DO_FLAG_POST_SWITCH.
            // Continuing the example, this gives the IF's `condition` slot a
            // second chance to be fulfilled--this time using the 10 as the
            // AND's left-hand argument.
            //
            if (f->deferred != NULL) {
                //
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

                REBFLGS flags = DO_FLAG_FULFILLING_ARG | DO_FLAG_POST_SWITCH;
                if (NOT(evaluating))
                    flags |= DO_FLAG_EXPLICIT_EVALUATE;
                if (neutral)
                    flags |= DO_FLAG_NEUTRAL;

                if (Do_Next_In_Subframe_Throws(
                    f->deferred, // old f->arg preload for DO_FLAG_POST_SWITCH
                    f,
                    flags
                )){
                    Move_Value(f->out, f->deferred);
                    Abort_Function(f);
                    goto finished;
                }

                // Don't clear until after the call (not being NULL is how the
                // subframe knows not to defer again.)
                //
                f->deferred = NULL;

                // Compensate for the param and arg change earlier.
                //
                ++f->param;
                ++f->arg;
            }

    //=//// ERROR ON END MARKER, BAR! IF APPLICABLE //////////////////////=//

            if (FRM_AT_END(f)) {
                if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                    fail (Error_No_Arg(f, f->param));

                Prep_Stack_Cell(f->arg);
                Init_Void(f->arg);
                goto continue_arg_loop;
            }

    //=//// IF EVAL/ONLY SEMANTICS, TAKE NEXT ARG WITHOUT EVALUATION //////=//

            if (
                NOT(evaluating)
                == NOT_VAL_FLAG(f->value, VALUE_FLAG_EVAL_FLIP)
            ){
                // Either we're NOT evaluating and there's NO special
                // exemption, or we ARE evaluating and there IS A special
                // exemption.  Treat this as if it's quoted.
                //
                Prep_Stack_Cell(f->arg);
                Quote_Next_In_Frame(f->arg, f); // has VALUE_FLAG_UNEVALUATED
                goto check_arg;
            }

    //=//// IF EVAL SEMANTICS, DISALLOW LITERAL EXPRESSION BARRIERS ///////=//

            if (IS_BAR(f->value) && pclass != PARAM_CLASS_HARD_QUOTE) {
                //
                // Only legal if arg is *hard quoted*.  Else, it must come via
                // other means (e.g. literal as `'|` or `first [|]`)

                if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                    fail (Error_Expression_Barrier_Raw());

                Prep_Stack_Cell(f->arg);
                Init_Void(f->arg);
                goto continue_arg_loop;
            }

            switch (pclass) {

   //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes a DO/NEXT's worth) ////=//

            case PARAM_CLASS_NORMAL: {
                REBFLGS flags = DO_FLAG_FULFILLING_ARG;
                if (NOT(evaluating))
                    flags |= DO_FLAG_EXPLICIT_EVALUATE;
                if (neutral)
                    flags |= DO_FLAG_NEUTRAL;

                Prep_Stack_Cell(f->arg);
                if (Do_Next_In_Subframe_Throws(f->arg, f, flags)) {
                    Move_Value(f->out, f->arg);
                    Abort_Function(f);
                    goto finished;
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
                if (NOT(evaluating))
                    flags |= DO_FLAG_EXPLICIT_EVALUATE;
                if (neutral)
                    flags |= DO_FLAG_NEUTRAL;

                Prep_Stack_Cell(f->arg);
                if (Do_Next_In_Subframe_Throws(f->arg, f, flags)) {
                    Move_Value(f->out, f->arg);
                    Abort_Function(f);
                    goto finished;
                }
                break; }

    //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG /////////////////////////////=//

            case PARAM_CLASS_HARD_QUOTE:
                Prep_Stack_Cell(f->arg);
                Quote_Next_In_Frame(f->arg, f); // has VALUE_FLAG_UNEVALUATED
                break;

    //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  ////////////////////////////=//

            case PARAM_CLASS_SOFT_QUOTE:
                if (neutral || NOT(IS_QUOTABLY_SOFT(f->value))) {
                    Prep_Stack_Cell(f->arg);
                    Quote_Next_In_Frame(f->arg, f); // VALUE_FLAG_UNEVALUATED
                    goto check_arg;
                }

                Prep_Stack_Cell(f->arg);
                if (Eval_Value_Core_Throws(f->arg, f->value, f->specifier)) {
                    Move_Value(f->out, f->arg);
                    Abort_Function(f);
                    goto finished;
                }

                Fetch_Next_In_Frame(f);
                break;

            default:
                assert(FALSE);
            }

    //=//// TYPE CHECKING FOR (MOST) ARGS AT END OF ARG LOOP //////////////=//

            // Some arguments can be fulfilled and skip type checking or
            // take care of it themselves.  But normal args pass through
            // this code which checks the typeset and also handles it when
            // a void arg signals the revocation of a refinement usage.

        check_arg:;
            ASSERT_VALUE_MANAGED(f->arg);
            assert(pclass != PARAM_CLASS_REFINEMENT);
            assert(pclass != PARAM_CLASS_LOCAL);

            // f->refine may point to the applicable refinement slot for the
            // current arg being fulfilled, or it might just be a signal of
            // information about the mode (see `Reb_Frame.refine` in %sys-do.h)
            //
            assert(
                f->refine == ORDINARY_ARG ||
                f->refine == LOOKBACK_ARG ||
                f->refine == ARG_TO_UNUSED_REFINEMENT ||
                f->refine == ARG_TO_REVOKED_REFINEMENT ||
                (IS_LOGIC(f->refine) && IS_TRUTHY(f->refine)) // used
            );

            if (IS_VOID(f->arg)) {
                if (IS_LOGIC(f->refine)) {
                    //
                    // We can only revoke the refinement if this is the 1st
                    // refinement arg.  If it's a later arg, then the first
                    // didn't trigger revocation, or refine wouldn't be logic.
                    //
                    if (f->refine + 1 != f->arg)
                        fail (Error_Bad_Refine_Revoke(f->param, f->arg));

                    Init_Logic(f->refine, FALSE); // can't re-enable...
                    f->refine = ARG_TO_REVOKED_REFINEMENT;
                    goto continue_arg_loop; // don't type check for optionality
                }
                else if (IS_FALSEY(f->refine)) {
                    //
                    // FALSE means the refinement has already been revoked so
                    // the void is okay.  BLANK! means the refinement was
                    // never in use in the first place.  Don't type check.
                    //
                    goto continue_arg_loop;
                }
                else {
                    // fall through to check arg for if <opt> is ok
                    //
                    assert(
                        f->refine == ORDINARY_ARG
                        || f->refine == LOOKBACK_ARG
                    );
                }
            }
            else {
                // If the argument is set, then the refinement shouldn't be
                // in a revoked or unused state.
                //
                if (IS_FALSEY(f->refine))
                    fail (Error_Bad_Refine_Revoke(f->param, f->arg));
            }

            if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
                if (NOT(TYPE_CHECK(f->param, VAL_TYPE(f->arg))))
                    fail (Error_Arg_Type(f, f->param, VAL_TYPE(f->arg)));
            }
            else {
                // Varargs are odd, because the type checking doesn't
                // actually check the types inside the parameter--it always
                // has to be a VARARGS!.
                //
                if (!IS_VARARGS(f->arg))
                    fail (Error_Not_Varargs(f, f->param, VAL_TYPE(f->arg)));

                // While "checking" the variadic argument we actually re-stamp
                // it with this parameter and frame's signature.  It reuses
                // whatever the original data feed was (this frame, another
                // frame, or just an array from MAKE VARARGS!)
                //
                const REBOOL make = FALSE; // reuse feed in f->arg
                Link_Vararg_Param_To_Frame(f, make);
            }

        continue_arg_loop: // `continue` might bind to the wrong scope
            ++f->param;
            ++f->arg;
            // f->special is incremented while already testing it for END
        }

        // If there was a specialization of the arguments, it should have
        // been marched to an end cell...or just be the NULL it started with
        //
        assert(f->special == NULL || IS_END(f->special));

        // While having the rule that arg terminates isn't strictly necessary,
        // it is a useful tool...and implicit termination makes it as cheap
        // as not doing it.
        //
        assert(IS_END(f->arg));

        // There may have been refinements that were skipped because the
        // order of definition did not match the order of usage.  They were
        // left on the stack with a pointer to the `param` and `arg` after
        // them for later fulfillment.
        //
        // Note that there may be functions on the stack if this is the
        // second time through, and we were just jumping up to check the
        // parameters in response to a R_REDO_CHECKED; if so, skip this.
        //
        if (DSP != f->dsp_orig) {
            if (IS_WORD(DS_TOP)) {
                //
                // The walk through the arguments didn't fill in any
                // information for this word, so it was either a duplicate of
                // one that was fulfilled or not a refinement the function
                // has at all.
                //
                assert(IS_WORD(DS_TOP));
                fail (Error_Bad_Refine_Raw(DS_TOP));
            }

            if (VAL_TYPE(DS_TOP) == REB_0_PICKUP) {
                assert(f->special == NULL); // no specialization "pickups"
                f->param = DS_TOP->payload.pickup.param;
                f->refine = f->arg = DS_TOP->payload.pickup.arg;
                assert(IS_LOGIC(f->refine) && VAL_LOGIC(f->refine));
                DS_DROP;
                f->doing_pickups = TRUE;
                goto continue_arg_loop; // leaves refine, but bumps param+arg
            }

            // chains push functions, and R_REDO_CHECKED
            assert(IS_FUNCTION(DS_TOP));
        }

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! ARGUMENTS NOW GATHERED, DISPATCH PHASE
    //
    //==////////////////////////////////////////////////////////////////==//

    redo_unchecked:
        assert(IS_END(f->param));
        // refine can be anything.
        assert(
            FRM_AT_END(f)
            || FRM_IS_VALIST(f)
            || IS_VALUE_IN_ARRAY_DEBUG(f->source.array, f->value)
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
            IS_END(f->out) || GET_FUN_FLAG(f->phase, FUNC_FLAG_INVISIBLE)
        );

        // !!! In theory an assert can be developed here, but we need to allow
        // API handles which may have non-stack lifetimes here.  This could
        // get complicated if a manual lifetime is used and freed during eval
        // so rethink what the assert should be and if perhaps the API handle
        // should get a hold taken on it.
        //
        /* assert(f->out->header.bits & VALUE_FLAG_STACK); */

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
        // !!! It currently doesn't matter what we choose for the neutral case
        // as long as it's cheap.  We keep writing inert values into the out
        // slot (it is probably not worth it to slow down the non-neutral
        // evaluator code by checking to elide those writes.)
        //
        switch (neutral ? R_BAR : (*PG_Apply)(f)) {
        case R_FALSE:
            Init_Logic(f->out, FALSE); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_TRUE:
            Init_Logic(f->out, TRUE); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_VOID:
            Init_Void(f->out); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_BLANK:
            Init_Blank(f->out); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_BAR:
            Init_Bar(f->out); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_OUT:
            CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break; // checked as NOT_END() after switch()

        case R_OUT_UNEVALUATED: // returned by QUOTE and SEMIQUOTE
            SET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break;

        case R_OUT_IS_THROWN: {
            assert(THROWN(f->out));

            if (IS_FUNCTION(f->out)) {
                if (
                    VAL_FUNC(f->out) == NAT_FUNC(exit)
                    && Same_Binding(VAL_BINDING(f->out), f)
                ){
                    // Do_Core catches "definitional exits" to current frame,
                    // e.g. throws where the "/name" is the EXIT native with a
                    // binding to this frame, and the thrown value is the
                    // return code.
                    //
                    // !!! This might be a little more natural if the name of
                    // the throw was a FRAME! value.  But that also would mean
                    // throws named by frames couldn't be taken advantage by
                    // the user for other features, while this only takes one
                    // function away.
                    //
                    CATCH_THROWN(f->out, f->out);
                    assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
                    goto apply_completed;
                }
                else if (
                    VAL_FUNC(f->out) == NAT_FUNC(redo)
                    && Same_Binding(VAL_BINDING(f->out), f)
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
                        && NULL != (exemplar = FUNC_EXEMPLAR(
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
            Abort_Function(f);
            goto finished; }

        case R_OUT_TRUE_IF_WRITTEN:
            if (IS_END(f->out))
                Init_Logic(f->out, FALSE); // no VALUE_FLAG_UNEVALUATED
            else
                Init_Logic(f->out, TRUE); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_OUT_VOID_IF_UNWRITTEN:
            if (IS_END(f->out))
                Init_Void(f->out); // no VALUE_FLAG_UNEVALUATED
            else
                CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break;

        case R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY:
            if (IS_END(f->out))
                Init_Void(f->out);
            else if (IS_VOID(f->out) || IS_FALSEY(f->out))
                Init_Bar(f->out);
            else
                CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break;

        case R_REDO_CHECKED:
        redo_checked:
            f->special = f->args_head;
            f->refine = ORDINARY_ARG; // no gathering, but need for assert
            assert(NOT(GET_FUN_FLAG(f->phase, FUNC_FLAG_INVISIBLE)));
            f->deferred = NULL; // frame filling decisions already made
            SET_END(f->out);
            goto process_function;

        case R_REDO_UNCHECKED:
            //
            // This instruction represents the idea that it is desired to
            // run the f->phase again.  The dispatcher may have changed the
            // value of what f->phase is, for instance.
            //
            assert(NOT(GET_FUN_FLAG(f->phase, FUNC_FLAG_INVISIBLE)));
            f->deferred = NULL; // frame filling decisions already made
            SET_END(f->out);
            goto redo_unchecked;

        case R_REEVALUATE_CELL:
            evaluating = TRUE; // unnecessary?
            goto prep_for_reevaluate;

        case R_REEVALUATE_CELL_ONLY:
            evaluating = FALSE;
            goto prep_for_reevaluate;

        case R_INVISIBLE: {
            assert(GET_FUN_FLAG(f->phase, FUNC_FLAG_INVISIBLE));

            // It is possible that when the elider ran, that there really was
            // no output in the cell yet (e.g. `do [comment "hi" ...]`) so it
            // would still be END after the fact.
            //
          #if defined(DEBUG_UNREADABLE_BLANKS)
            assert(IS_END(f->out) || NOT(IS_UNREADABLE_DEBUG(f->out)));
          #endif

            // If we hit the frame end, there are two different behaviors:
            //
            //     do [2 comment "should evaluate to 2"]
            //     do [print comment "acts like an <end>"]
            //
            if (FRM_AT_END(f)) {
                if (IS_END(f->out)) {
                    Init_Void(f->out);

                    // !!! A theory was that the "evaluated" flag would help a
                    // function that took an <opt> <end> distinguish which kind
                    // of void it was.  This may or may not be a good idea, but
                    // unevaluating it here just to make a note of the concept.
                    //
                    SET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
                }
            }
            else {
                // We need to continue the post-switch processing, e.g. we want
                // `do [1 comment "a" comment "b" + 2]` to give 3.  But we can
                // only do that if there is a starter value in the output cell.
                // otherwise we can't satisfy an argument request.
                //
                // Use same mechanic as EVAL, but retrigger whatever is next in
                // the frame (`f->value` is our current pending value)
                //
                if (IS_END(f->out)) {
                    Derelativize(&f->cell, f->value, f->specifier);
                    Fetch_Next_In_Frame(f);

                    evaluating = TRUE; // unnecessary?
                    goto prep_for_reevaluate;
                }
            }

            break; } // do normal fallthrough

        prep_for_reevaluate: {
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

            Drop_Function(f);
            goto reevaluate; } // we don't move index!

        case R_UNHANDLED: // internal use only, shouldn't be returned
            assert(FALSE);

        default:
            assert(FALSE);
        }

    apply_completed:;

    //==////////////////////////////////////////////////////////////////==//
    //
    // DEBUG CHECK RETURN OF ALL FUNCTIONS (not just user functions)
    //
    //==////////////////////////////////////////////////////////////////==//

    // Here we know the function finished and nothing threw past it or
    // FAIL / fail()'d.
    //
    // Generally the return type is validated by the Returner_Dispatcher()
    // with everything else assumed to return the correct type.  But this
    // double checks any function marked with RETURN in the debug build,
    // so native return types are checked instead of just trusting the C.

    #if !defined(NDEBUG)
        assert(f->eval_type == REB_FUNCTION); // shouldn't have changed

        assert(NOT_END(f->out)); // should have overwritten
        assert(NOT(THROWN(f->out))); // throws must be R_OUT_IS_THROWN
        if (Is_Bindable(f->out))
            assert(f->out->extra.binding != NULL);

        if (GET_FUN_FLAG(f->phase, FUNC_FLAG_INVISIBLE)) {
            //
            // Invisible functions do not have their return types checked,
            // because all they're doing is leaving behind whatever was there
            // before (if it was an END, they'd have had to reevaluate above).
            //
            // !!! As written, you can't even *use* the definitional RETURN as
            // it accepts no types...should probably have a LEAVE.  Review.
            //
        }
        else if (GET_FUN_FLAG(f->phase, FUNC_FLAG_RETURN)) {
            REBVAL *typeset = FUNC_PARAM(f->phase, FUNC_NUM_PARAMS(f->phase));
            assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);
            if (!TYPE_CHECK(typeset, VAL_TYPE(f->out)))
                fail (Error_Bad_Return_Type(f, VAL_TYPE(f->out)));
        }
    #endif

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! CALL COMPLETION
    //
    //==////////////////////////////////////////////////////////////////==//

        // If we have functions pending to run on the outputs, then do so.
        //
        while (DSP != f->dsp_orig) {
            assert(IS_FUNCTION(DS_TOP));

            Move_Value(&f->cell, f->out);

            // Data stack values cannot be used directly in an apply, because
            // the evaluator uses DS_PUSH, which could relocate the stack
            // and invalidate the pointer.
            //
            DECLARE_LOCAL (fun);
            Move_Value(fun, DS_TOP);

            const REBOOL fully = TRUE;
            if (Apply_Only_Throws(f->out, fully, fun, &f->cell, END)) {
                Abort_Function(f);
                goto finished;
            }

            DS_DROP;
        }

        // !!! It would technically be possible to drop the arguments before
        // running chains... and if the chained function were to run *in*
        // this frame that could be even more optimal.  However, having the
        // original function still on the stack helps make errors clearer.
        //
        Drop_Function(f);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [WORD!]
//
// A plain word tries to fetch its value through its binding.  It will fail
// and longjmp out of this stack if the word is unbound (or if the binding is
// to a variable which is not set).  Should the word look up to a function,
// then that function will be called by jumping to the ANY-FUNCTION! case.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_WORD:
        if (current_gotten == END)
            current_gotten = Get_Opt_Var_May_Fail(current, f->specifier);

        if (IS_FUNCTION(current_gotten)) { // before IS_VOID() is common case
            Push_Function(
                f,
                VAL_WORD_SPELLING(current),
                VAL_FUNC(current_gotten),
                VAL_BINDING(current_gotten)
            );

            if (GET_VAL_FLAG(current_gotten, VALUE_FLAG_ENFIXED)) {
                //
                // Note: The usual dispatch of enfix functions is not via a
                // REB_WORD in this switch, it's by some code at the end of
                // the switch.  So you only see this in cases like `(+ 1 2)`,
                // -OR- after FUNC_FLAG_INVISIBLE e.g. `10 comment "hi" + 20`.
                //
                f->refine = LOOKBACK_ARG;

              #if defined(DEBUG_UNREADABLE_BLANKS)
                assert(IS_END(f->out) || NOT(IS_UNREADABLE_DEBUG(f->out)));
              #endif
            }
            else {
                f->refine = ORDINARY_ARG;
                if (NOT_VAL_FLAG(current_gotten, FUNC_FLAG_INVISIBLE))
                    SET_END(f->out);
            }

            goto process_function;
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
        if (NOT(evaluating) == NOT_VAL_FLAG(f->value, VALUE_FLAG_EVAL_FLIP)) {
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

            REBFLGS flags = DO_FLAG_NORMAL | DO_FLAG_FULFILLING_SET; // /NEXT
            if (neutral)
                flags |= DO_FLAG_NEUTRAL;

            if (Do_Next_Mid_Frame_Throws(f, flags)) { // light reuse of `f`
                DS_DROP;
                goto finished;
            }

            if (NOT(neutral)) // must avoid writing to variables
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
        Copy_Opt_Var_May_Fail(f->out, current, f->specifier);
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
        //
        // Note that we only get here if the GROUP! is evaluative--and that
        // means the whole group.  So no non-evaluation flag passing.
        //
        if (neutral)
            Init_Bar(f->out); // it's one unit of evaluation, easily skipped
        else {
            REBSPC *derived = Derive_Specifier(f->specifier, current);
            if (Do_At_Throws(
                f->out,
                VAL_ARRAY(current), // the GROUP!'s array
                VAL_INDEX(current), // index in REBVAL (may not be head)
                derived
            )){
                goto finished;
            }
        }

        // Leave VALUE_FLAG_UNEVALUATED as it was.  If we added it, then
        // things like `if condition (semiquote do [1 + 1])` wouldn't work, so
        // one would be forced to write `if condition semiquote do [1 + 1]`.
        // This would limit the flexiblity of grouping.
        //
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
        //
    do_path_in_current:;

        REBSTR *opt_label;
        if (Do_Path_Throws_Core(
            f->out,
            &opt_label, // requesting says we run functions (not GET-PATH!)
            REB_PATH,
            VAL_ARRAY(current),
            VAL_INDEX(current),
            Derive_Specifier(f->specifier, current),
            NULL, // `setval`: null means don't treat as SET-PATH!
            neutral ? DO_FLAG_NEUTRAL : 0
        )){
            goto finished;
        }

        if (IS_VOID(f->out)) // need `:x/y` if `y` is unset
            fail (Error_No_Value_Core(current, f->specifier));

        if (IS_FUNCTION(f->out)) {
            //
            // !!! While it is (or would be) possible to fetch an enfix or
            // invisible function from a PATH!, at this point it would be too
            // late in the current scheme...since the lookahead step only
            // honors WORD!.  PATH! support is expected for the future, but
            // requires overhaul of the R3-Alpha path implementation.
            //
            // Note this error must come *before* Push_Function(), as fail()
            // expects f->param to be valid for f->eval_type = REB_FUNCTION,
            // and Push_Function() trashes that.
            //
            if (ANY_VAL_FLAGS(
                f->out, FUNC_FLAG_INVISIBLE | VALUE_FLAG_ENFIXED
            )){
                fail ("ENFIX/INVISIBLE dispatch w/PATH! not yet supported");
            }

            Push_Function(
                f,
                opt_label, // NULL label means anonymous
                VAL_FUNC(f->out),
                VAL_BINDING(f->out)
            );

            f->refine = ORDINARY_ARG; // paths are never enfixed (for now)
            SET_END(f->out); // loses enfix left hand side, invisible passthru
            goto process_function;
        }

        CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
        break;
    }

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
        if (NOT(evaluating) == NOT_VAL_FLAG(f->value, VALUE_FLAG_EVAL_FLIP)) {
            //
            // Either we're NOT evaluating and there's NO special exemption,
            // or we ARE evaluating and there IS A special exemption.  Treat
            // the f->value as inert.

            Derelativize(f->out, f->value, f->specifier);

            // !!! Due to the way this is currently designed, throws need to
            // be written to a location distinct from the path and also
            // distinct from the value being set.  Review.
            //
            if (NOT(neutral)) {
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
        }
        else if (neutral) {
            //
            // For an evaluative SET-PATH! just try to neutrally run the
            // right hand side.
            //
            REBFLGS flags = DO_FLAG_NORMAL | DO_FLAG_FULFILLING_SET;
            flags |= DO_FLAG_NEUTRAL;

            if (Do_Next_Mid_Frame_Throws(f, flags)) // light reuse of `f`
                goto finished;
        }
        else {
            // f->value is guarded implicitly by the frame, but `current` is a
            // transient local pointer that might be to a va_list REBVAL* that
            // has already been fetched.  The bits will stay live until
            // va_end(), but a GC wouldn't see it.
            //
            DS_PUSH_RELVAL(current, f->specifier);

            REBFLGS flags = DO_FLAG_NORMAL | DO_FLAG_FULFILLING_SET; // /NEXT

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
        // !!! Should a GET-PATH! be able to call into the evaluator, by
        // evaluating GROUP!s in the path?  It's clear that `get path`
        // shouldn't be able to evaluate (a GET should not have side effects).
        // But perhaps source-level GET-PATH!s can be more liberal, as one can
        // visibly see the GROUP!s.
        //
        if (neutral)
            Get_Path_Core(f->out, current, f->specifier);
        else {
            if (Get_Path_Throws_Core(f->out, current, f->specifier))
                goto finished;

            CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
        }
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
    case REB_STRING:
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
// [BAR!]
//
// If an expression barrier is seen in-between expressions (as it will always
// be if hit in this switch), it evaluates to void.  It only errors in
// argument fulfillment during the switch case for ANY-FUNCTION!.
//
// Note that `DO/NEXT [| | | | 1 + 2]` will skip the bars and yield 3.  This
// helps give BAR!s their lightweight character.  It also means that code
// doing DO/NEXTs will not see them as generating voids, which might have
// a specific meaning to the caller.  (They can check for BAR!s explicitly
// if they want to give BAR!s a meaning.)
//
// Note also that natives and dialects frequently do their own interpretation
// of BAR!--rather than just evaluate it and let it mean something equivalent
// to an unset.  For instance:
//
//     case [false [print "F"] | true [print ["T"]]
//
// If CASE did not specially recognize BAR!, it would complain that the
// "second condition" had no value.  So if you are looking for a BAR! behavior
// and it's not passing through here, check the construct you are using.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_BAR:
        assert(IS_BAR(current));

        if (FRM_HAS_MORE(f)) {
            SET_END(f->out); // skipping the post loop where this is done
            f->eval_type = VAL_TYPE(f->value);
            goto do_next; // quickly process next item, no infix test needed
        }

        Init_Void(f->out); // no VALUE_FLAG_UNEVALUATED
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
        assert(IS_LIT_BAR(current));

        Init_Bar(f->out); // no VALUE_FLAG_UNEVALUATED
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// Treat all the other NOT(Is_Bindable()) types as inert
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
        //
    inert:
        Derelativize(f->out, current, f->specifier);
        SET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
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
//     :foo/bar => pick* foo 'bar (void if not present)
//     foo/bar => eval :foo/bar (should be an error if not present)
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_MAX_VOID:
        if (NOT(evaluating)) {
            Init_Void(f->out);
        }
        else {
            // must be EVAL, so the value must be living in the frame cell
            //
            assert(current == &f->cell);
            fail (Error_Evaluate_Void_Raw());
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
    // looks up to has been set with VALUE_FLAG_ENFIX, and keep going.
    //
    // Next, there's a subtlety with DO_FLAG_NO_LOOKAHEAD which explains why
    // processing of the 2 argument doesn't greedily continue to advance, but
    // waits for `1 + 2` to finish.  This is because the right hand argument
    // of math operations tend to be declared #tight.
    //
    // Slightly more nuanced is why FUNC_FLAG_INVISIBLE functions have to be
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
    f->eval_type = VAL_TYPE(f->value);

//=//// IF NOT A WORD!, IT DEFINITELY STARTS A NEW EXPRESSION /////////////=//

    // !!! Our lookahead step currently only works with WORD!, but it should
    // be retrofitted in the future to support PATH! dispatch also (for both
    // enfix and invisible/comment-like behaviors).  But in the meantime, if
    // you use a PATH! and look up to an enfixed word or "invisible" result
    // function, that's an error (or should be).

    if (f->eval_type != REB_WORD) {
        if (NOT(f->flags.bits & DO_FLAG_TO_END))
            goto finished; // only want DO/NEXT of work, so stop evaluating

        START_NEW_EXPRESSION_MAY_THROW(f, goto finished);
        // ^-- resets evaluating + tick, corrupts f->out, Ctrl-C may abort

        UPDATE_TICK_DEBUG(NULL);
        // v-- The TICK_BREAKPOINT or C-DEBUG-BREAK landing spot --v

        goto do_next;
    }

//=//// FETCH WORD! TO PERFORM SPECIAL HANDLING FOR ENFIX/INVISIBLES //////=//

    // First things first, we fetch the WORD! (if not previously fetched) so
    // we can see if it looks up to any kind of FUNCTION! at all.

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
            || (f->prior && f->prior->deferred == BLANK_VALUE) // !!! for hack
        );
    }

//=//// NEW EXPRESSION IF UNBOUND, NON-FUNCTION, OR NON-ENFIX /////////////=//

    // These cases represent finding the start of a new expression, which
    // continues the evaluator loop if DO_FLAG_TO_END, but will stop with
    // `goto finished` if NOT(DO_FLAG_TO_END).
    //
    // We fall back on word-like "dispatch" even if f->gotten == END (unset or
    // unbound word).  It'll be an error, but that code path raises it for us.

    if (
        VAL_TYPE_OR_0(f->gotten) != REB_FUNCTION // END is REB_0 (UNBOUND)
        || NOT_VAL_FLAG(f->gotten, VALUE_FLAG_ENFIXED)
    ){
        if (NOT(f->flags.bits & DO_FLAG_TO_END)) {
            //
            // Since it's a new expression, a DO/NEXT doesn't want to run it
            // *unless* it's "invisible"
            if (
                VAL_TYPE_OR_0(f->gotten) != REB_FUNCTION
                || NOT_VAL_FLAG(f->gotten, FUNC_FLAG_INVISIBLE)
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
            VAL_TYPE_OR_0(f->gotten) == REB_FUNCTION
            && GET_VAL_FLAG(f->gotten, FUNC_FLAG_INVISIBLE)
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

            UPDATE_TICK_DEBUG(NULL);
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
        && NOT_VAL_FLAG(f->gotten, FUNC_FLAG_INVISIBLE)
    ){
        // Don't do enfix lookahead if asked *not* to look.  See the
        // PARAM_CLASS_TIGHT parameter convention for the use of this, as
        // well as it being set if DO_FLAG_TO_END wants to clear out the
        // invisibles at this frame level before returning.
        //
        goto finished;
    }

    if (GET_VAL_FLAG(f->gotten, FUNC_FLAG_QUOTES_FIRST_ARG)) {
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

    // Note that we're only willing to defer any given lookback *once*.
    // If we get there and there's a deferral, it doesn't matter if it was
    // this frame or the parent frame who deferred it...it's the same enfix
    // function in the same spot, and it's only willing to give up *one*
    // of its chances to run.
    //
    if (
        GET_VAL_FLAG(f->gotten, FUNC_FLAG_DEFERS_LOOKBACK)
        && f->deferred == NULL
    ){
        if (
            (f->flags.bits & DO_FLAG_FULFILLING_ARG)
            && f->prior->deferred == NULL
        ){
            assert(NOT(f->flags.bits & DO_FLAG_TO_END));
            assert(Is_Function_Frame_Fulfilling(f->prior));

            f->prior->deferred = f->out; // see remarks on deferred in REBFRM

            // Leave enfix operator pending in the frame, and it's up to the
            // parent frame to decide whether to use DO_FLAG_POST_SWITCH to
            // jump back in and finish fulfilling this arg or not.  If it does
            // resume and we get to this check again, f->prior->deferred can't
            // be NULL, otherwise it would be an infinite loop.
            //
            goto finished;
        }
        else if (f->flags.bits & DO_FLAG_VARIADIC_TAKE) {
            //
            // We assume all variadic arg TAKEs don't defer, but this can
            // lead to ambiguity.  Set this bit, which each variadic TAKE will
            // clear before running, and the bit is checked when the function
            // completes.
            //
            f->prior->flags.bits |= DO_FLAG_AMBIGUOUS_DEFER;
        }
    }

    f->deferred = NULL;

    // This is a case for an evaluative lookback argument we don't want to
    // defer, e.g. a #tight argument or a normal one which is not being
    // requested in the context of parameter fulfillment.  We want to reuse
    // the f->out value and get it into the new function's frame.

    Push_Function(
        f,
        VAL_WORD_SPELLING(f->value),
        VAL_FUNC(f->gotten),
        VAL_BINDING(f->gotten)
    );
    f->refine = LOOKBACK_ARG;

    f->gotten = END;
    Fetch_Next_In_Frame(f); // advances f->value
    goto process_function;

finished:;

#if !defined(NDEBUG)
    Do_Core_Exit_Checks_Debug(f); // will get called unless a fail() longjmps
#endif

    // All callers must inspect for THROWN(f->out), and most should also
    // inspect for FRM_AT_END(f)
}
