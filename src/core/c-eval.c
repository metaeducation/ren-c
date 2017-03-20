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
//   from the input at a time.  This input may be a source where the index
//   needs to be tracked and care taken to contain the index within its
//   boundaries in the face of change (e.g. a mutable ARRAY).  Or it may be
//   an entity which tracks its own position on each fetch (e.g. a C va_list)
//

#include "sys-core.h"

#include "tmp-evaltypes.inc"


#if !defined(NDEBUG)
    //
    // The `do_count` should be visible in the C debugger watchlist as a
    // local variable in Do_Core() for each stack level.  So if a fail()
    // happens at a deterministic moment in a run, capture the number from
    // the level of interest and recompile with it here to get a breakpoint
    // at that tick.
    //
    // Notice also that in debug builds, frames carry this value in them.
    // *Plus* you can get the initialization tick for void cells, BLANK!s,
    // LOGIC!s, and most end markers by looking at the `track` payload of
    // the REBVAL cell.  And series contain the do_count where they were
    // created as well.
    //
    //      *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
    #define DO_COUNT_BREAKPOINT    0
    //      *** DON'T COMMIT THIS --^ KEEP IT AT ZERO! ***
    //
    // Note: Taking this number on the command line sounds convenient, though
    // with command line processing in usermode it would throw the number off
    // between runs.
    //
    // Note also there is `Dump_Frame_Location()` if there's a trouble spot
    // and you want to see what the state is.  It will reify C va_list
    // input for you, so you can see what the C caller passed as an array.
    //
#endif


static inline REBOOL Start_New_Expression_Throws(REBFRM *f) {
    assert(Eval_Count >= 0);
    if (--Eval_Count == 0) {
        //
        // Note that Do_Signals_Throws() may do a recycle step of the GC, or
        // it may spawn an entire interactive debugging session via
        // breakpoint before it returns.  It may also FAIL and longjmp out.
        //
        if (Do_Signals_Throws(&f->cell)) {
            Move_Value(f->out, &f->cell);
            return TRUE;
        }

        if (!IS_VOID(&f->cell)) {
            //
            // !!! What to do with something like a Ctrl-C-based breakpoint
            // session that does something like `resume/with 10`?  We are
            // "in-between" evaluations, so that 10 really has no meaning
            // and is just going to get discarded.  FAIL for now to alert
            // the user that something is off, but perhaps the failure
            // should be contained in a sandbox and restart the break?
            //
            fail (Error(RE_MISC));
        }
    }

    UPDATE_EXPRESSION_START(f); // !!! See FRM_INDEX() for caveats
    if (Trace_Flags)
        Trace_Line(f);

    return FALSE;
}

#define START_NEW_EXPRESSION_MAY_THROW_COMMON(f,g) \
    if (Start_New_Expression_Throws(f)) \
        g; \
    args_evaluate = NOT((f)->flags.bits & DO_FLAG_NO_ARGS_EVALUATE); \

#ifdef NDEBUG
    #define START_NEW_EXPRESSION_MAY_THROW(f,g) \
        START_NEW_EXPRESSION_MAY_THROW_COMMON(f, g)
#else
    // Macro is used to mutate local do_count variable in Do_Core (for easier
    // browsing in the watchlist) as well as to not be in a deeper stack level
    // than Do_Core when a DO_COUNT_BREAKPOINT is hit.
    //
    #define START_NEW_EXPRESSION_MAY_THROW(f,g) \
        do { \
            START_NEW_EXPRESSION_MAY_THROW_COMMON(f, g); \
            do_count = Do_Core_Expression_Checks_Debug(f); \
            if (do_count == DO_COUNT_BREAKPOINT) { \
                Debug_Fmt("DO_COUNT_BREAKPOINT hit at %d", f->do_count); \
                Dump_Frame_Location(f); \
                debug_break(); /* see %debug_break.h */ \
            } \
        } while (FALSE)
#endif

static inline void Drop_Function_Args_For_Frame(REBFRM *f) {
    Drop_Function_Args_For_Frame_Core(f, TRUE);
}

static inline void Abort_Function_Args_For_Frame(REBFRM *f) {
    Drop_Function_Args_For_Frame(f);

    // If a function call is aborted, there may be pending refinements (if
    // in the gathering phase) or functions (if running a chainer) on the
    // data stack.  They must be dropped to balance.
    //
    DS_DROP_TO(f->dsp_orig);
}

static inline void Link_Vararg_Param_To_Frame(REBFRM *f, REBOOL make) {
    assert(GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC));

    // Note that this varlist is to a context with bad cells in
    // any unfilled arg slots.  Because of this, there needs to
    // be special handling in the GC that knows *not* to try
    // and walk these incomplete arrays sitting in the argument
    // slots if they're not ready...
    //
    if (
        f->varlist == NULL
        || NOT_SER_FLAG(f->varlist, ARRAY_FLAG_VARLIST)
    ){
        // Don't use ordinary call to Context_For_Frame_May_Reify
        // because this special case allows reification even
        // though the frame is pending.
        //
        Reify_Frame_Context_Maybe_Fulfilling(f);
    }
    f->arg->extra.binding = f->varlist;

    // Store the offset so that both the f->arg and f->param locations can
    // be quickly recovered, while using only a single slot in the REBVAL.
    //
    f->arg->payload.varargs.param_offset = f->arg - f->args_head;

    // The data feed doesn't necessarily come from the frame
    // that has the parameter and the argument.  A varlist may be
    // chained such that its data came from another frame, or just
    // an ordinary array.
    //
    if (make) {
        VAL_RESET_HEADER(f->arg, REB_VARARGS);
        f->arg->payload.varargs.feed = f->varlist;
    }
    else
        assert(VAL_TYPE(f->arg) == REB_VARARGS);

    assert(GET_SER_FLAG(f->arg->payload.varargs.feed, SERIES_FLAG_ARRAY));
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
// * If VOID_CELL, then refinements are being skipped and the arguments
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
// Because of how this lays out, IS_CONDITIONAL_TRUE() can be used to
// determine if an argument should be type checked normally...while
// IS_CONDITIONAL_FALSE() means that the arg's bits must be set to void.
//
// These special values are all pointers to read-only cells, but are cast to
// mutable in order to be held in the same pointer that might write to a
// refinement to revoke it.  Note that since literal pointers are used, tests
// like `f->refine == BLANK_VALUE` are faster than `IS_BLANK(f->refine)`.
//
#define SKIPPING_REFINEMENT_ARGS m_cast(REBVAL*, VOID_CELL)
#define ARG_TO_UNUSED_REFINEMENT m_cast(REBVAL*, BLANK_VALUE)
#define ARG_TO_REVOKED_REFINEMENT m_cast(REBVAL*, FALSE_VALUE)
#define ORDINARY_ARG m_cast(REBVAL*, EMPTY_BLOCK)
#define LOOKBACK_ARG m_cast(REBVAL*, EMPTY_STRING)


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
//     f->out*
//     REBVAL pointer to which the evaluation's result should be written,
//     must point to initialized bits, and that needs to be an END marker,
//     unless it's in lookback mode, in which case it must be the REBVAL to
//     use as first argument (infix/postfix/"enfixed" functions)
//
//     f->value
//     Fetched first value to execute (cannot be an END marker)
//
//     f->source
//     Contains the REBARR* or C va_list of subsequent values to fetch
//
//     f->index
//     Needed if f->source is an array (can be garbage if it's a C va_list)
//
//     f->pending
//     Must be VA_LIST_PENDING if source is a va_list, else starts out NULL
//
//     f->specifier
//     Resolver for bindings of values in f->source, SPECIFIED if all resolved
//
//     f->gotten
//     Must be either be the Get_Var() lookup of f->value, or NULL
//
// More detailed assertions of the preconditions, postconditions, and state
// at each evaluation step are contained in %d-eval.c
//
void Do_Core(REBFRM * const f)
{
#if !defined(NDEBUG)
    REBUPT do_count = f->do_count = TG_Do_Count; // snapshot initial state
#endif

    REBOOL args_evaluate; // set on every iteration (varargs do, EVAL/ONLY...)

    // APPLY and a DO of a FRAME! both use this same code path.
    //
    if (f->flags.bits & DO_FLAG_APPLYING) {
        f->eval_type = REB_FUNCTION;
        args_evaluate = NOT(f->flags.bits & DO_FLAG_NO_ARGS_EVALUATE);
        f->refine = ORDINARY_ARG; // "APPLY infix" not supported
        goto do_function_arglist_in_progress;
    }

    // Some initialized bit pattern is needed to check to see if a
    // function call is actually in progress, or if eval_type is just
    // REB_FUNCTION but doesn't have valid args/state.  The label is a
    // good choice because it is only affected by the function call case,
    // see Is_Function_Frame_Fulfilling().
    //
    f->label = NULL;
    f->eval_type = VAL_TYPE(f->value);

#if !defined(NDEBUG)
    SNAP_STATE(&f->state); // to make sure stack balances, etc.
    Do_Core_Entry_Checks_Debug(f); // run once per Do_Core()
#endif

    // This is an important guarantee...the out slot needs to have some form
    // of initialization to allow GC.  END is chosen because that is what
    // natives can count on the f->out slot to be, but lookback arguments
    // also are passed by way of the out slot.
    //
    assert(NOT(IS_TRASH_DEBUG(f->out)));

    // Capture the data stack pointer on entry (used by debug checks, but
    // also refinements are pushed to stack and need to be checked if there
    // are any that are not processed)
    //
    f->dsp_orig = DSP;

do_next:;

    START_NEW_EXPRESSION_MAY_THROW(f, goto finished);
    // ^-- sets args_evaluate, do_count, Ctrl-C may abort

reevaluate:;
    //
    // ^-- doesn't advance expression index, so `eval x` starts with `eval`
    // also EVAL/ONLY may change args_evaluate to FALSE for a cycle

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
    //
    // Additionally, it attempts to reuse any lookahead fetching done with
    // Get_Var.  In the general case, this is not going to be possible, e.g.:
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

    const RELVAL *current;
    const REBVAL *current_gotten;

    current = f->value; // <-- DO_COUNT_BREAKPOINT landing spot
    current_gotten = f->gotten;
    f->gotten = NULL;
    Fetch_Next_In_Frame(f);

    // VAL_TYPE_RAW is used to avoid a separate check for IS_END().
    //
    if (VAL_TYPE_RAW(f->value) == REB_WORD && IS_WORD_BOUND(f->value)) {
        //
        // While the next item may be a WORD! that looks up to an enfixed
        // function, and it may want to quote what's on its left...there
        // could be a conflict.  This happens if the current item is also
        // a WORD!, but one that looks up to a prefix function that wants
        // to quote what's on its right!
        //
        if (f->eval_type == REB_WORD && IS_WORD_BOUND(current)) {
            if (current_gotten == NULL)
                current_gotten = Get_Opt_Var_May_Fail(current, f->specifier);
            else
                assert(
                    current_gotten
                    == Get_Opt_Var_May_Fail(current, f->specifier)
                );

            if (
                IS_FUNCTION(current_gotten)
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
                f->eval_type = REB_FUNCTION;
                SET_FRAME_LABEL(f, VAL_WORD_SPELLING(current));
                f->refine = ORDINARY_ARG;
                goto do_function_in_current_gotten;
            }
        }

        f->gotten = Get_Opt_Var_May_Fail(f->value, f->specifier);

        if (
            IS_FUNCTION(f->gotten)
            && ALL_VAL_FLAGS(
                f->gotten, VALUE_FLAG_ENFIXED | FUNC_FLAG_QUOTES_FIRST_ARG
            )
        ){
            f->eval_type = REB_FUNCTION;
            SET_FRAME_LABEL(f, VAL_WORD_SPELLING(f->value));

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

            current_gotten = f->gotten; // the function

            // We don't want the WORD! that invoked the function to act like
            // an argument, so we have to advance the frame once more.
            //
            f->gotten = NULL;
            Fetch_Next_In_Frame(f);

            goto do_function_in_current_gotten;
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

//==//////////////////////////////////////////////////////////////////////==//
//
// [FUNCTION!] (lookback or non-lookback)
//
// If a function makes it to the SWITCH statement, that means it is either
// literally a function value in the array (`do compose [(:+) 1 2]`) or is
// being retriggered via EVAL
//
// Most function evaluations are triggered from a SWITCH on a WORD! or PATH!,
// which jumps in at the `do_function_in_current_gotten` label.
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_0:
        assert(FALSE); // internal type.
        break;

    case REB_FUNCTION: // literal function in a block
        current_gotten = const_KNOWN(current);
        SET_FRAME_LABEL(f, Canon(SYM___ANONYMOUS__)); // nameless literal

        if (GET_VAL_FLAG(current_gotten, VALUE_FLAG_ENFIXED)) {
            //
            // f->out can't be trash, but it can be an END.
            //
            f->refine = LOOKBACK_ARG;
        }
        else {
            SET_END(f->out); // clear out previous result (needs GC-safe data)
            f->refine = ORDINARY_ARG;
        }

    do_function_in_current_gotten:
        assert(IS_FUNCTION(current_gotten));
        assert(f->eval_type == REB_FUNCTION);
        TRASH_POINTER_IF_DEBUG(current); // shouldn't be used below

        // There may be refinements pushed to the data stack to process, if
        // the call originated from a path dispatch.
        //
        assert(DSP >= f->dsp_orig);

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! NORMAL ARGUMENT FULFILLMENT PROCESS
    //
    //==////////////////////////////////////////////////////////////////==//

        // We assume you can enumerate both the formal parameters (in the
        // spec) and the actual arguments (in the call frame) using pointer
        // incrementation, that they are both terminated by END, and
        // that there are an equal number of values in both.
        //
        // Push_Or_Alloc_Args sets the frame's function, sets args_head...
        //
        Push_Or_Alloc_Args_For_Underlying_Func(f, current_gotten);

    do_function_arglist_in_progress:

        assert(f->label != NULL); // must be something (even "anonymous")
    #if !defined(NDEBUG)
        assert(f->label_debug != NULL); // SET_FRAME_LABEL sets (C debugging)
    #endif

        Eval_Functions++; // this isn't free...is it worth tracking?

        // Now that we have extracted f->func, we do not have to worry that
        // f->value might have lived in f->cell.eval.  We can't overwrite
        // f->out during the argument evaluations, in case that is holding the
        // first argument to an infix function, so f->cell gets used for
        // temporary evaluations up until the point the function gets called.

        assert(f->refine == ORDINARY_ARG || f->refine == LOOKBACK_ARG);

        f->arg = f->args_head;
        f->param = FUNC_FACADE_HEAD(f->func);
        // f->special is END, f->args_head, or first specialized value

        // Same as check before switch.  (do_function_arglist_in_progress:
        // might have a goto from another point, so we check it again here)
        //
        assert(IS_END(f->out) || f->refine == LOOKBACK_ARG);

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! NORMAL ARGUMENT FULFILLMENT LOOP
    //
    //==////////////////////////////////////////////////////////////////==//

        // This loop goes through the parameter and argument slots.  Though
        // the argument slots must be protected from garbage collection once
        // they are filled, they start out uninitialized.  (The GC has access
        // to the frame list, so it can examine f->arg and avoid trying to
        // protect slots that come after it.)
        //
        // Based on the parameter type, it may be necessary to "consume" an
        // expression from values that come after the invocation point.  But
        // not all params will consume arguments for all calls.
        //
        // This one body of code to is able to handle both function
        // specialization and ordinary invocation.  f->special is used to
        // either step through a list of specialized values (with void as a
        // signal of no specialization), to step through the arguments if
        // they are just being type checked, or END otherwise.

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

                if (f->special != END) {
                    if (f->special == f->arg) {
                        //
                        // We're just checking the values already in the
                        // frame, so fall through and test the arg slot.
                        // However, offer a special tolerance here for void
                        // since MAKE FRAME! fills all arg slots with void.
                        //
                        if (IS_VOID(f->arg))
                            SET_FALSE(f->arg);
                    }
                    else {
                        // Voids in specializations mean something different,
                        // that the refinement is left up to the caller.
                        //
                        if (IS_VOID(f->special)) {
                            ++f->special;
                            goto unspecialized_refinement;
                        }

                        Move_Value(f->arg, f->special);
                    }

                    if (!IS_LOGIC(f->arg))
                        fail (Error_Non_Logic_Refinement(f));

                    if (IS_CONDITIONAL_TRUE(f->arg))
                        f->refine = f->arg; // remember so we can revoke!
                    else
                        f->refine = ARG_TO_UNUSED_REFINEMENT; // (read-only)

                    ++f->special;
                    goto continue_arg_loop;
                }

    //=//// UNSPECIALIZED REFINEMENT SLOT (no consumption) ////////////////=//

            unspecialized_refinement:

                if (f->dsp_orig == DSP) { // no refinements left on stack
                    SET_FALSE(f->arg);
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

                    SET_TRUE(f->arg); // marks refinement used
                    f->refine = f->arg; // "consume args (can be revoked)"
                    goto continue_arg_loop;
                }

                --f->refine; // not lucky: if in use, this is out of order

                for (; f->refine > DS_AT(f->dsp_orig); --f->refine) {
                    if (!IS_WORD(f->refine)) continue; // non-refinement
                    if (
                        VAL_WORD_SPELLING(f->refine) // canon when pushed
                        == VAL_PARAM_CANON(f->param) // #2258
                    ){
                        // The call uses this refinement but we'll have to
                        // come back to it when the expression index to
                        // consume lines up.  Make a note of the param
                        // and arg and poke them into the stack value.
                        //
                        VAL_RESET_HEADER(f->refine, REB_0_PICKUP);
                        f->refine->payload.pickup.param
                            = const_KNOWN(f->param);
                        f->refine->payload.pickup.arg = f->arg;

                        SET_TRUE(f->arg); // marks refinement used
                        // "consume args later" (promise not to change)
                        f->refine = SKIPPING_REFINEMENT_ARGS;
                        goto continue_arg_loop;
                    }
                }

                // Wasn't in the path and not specialized, so not present
                //
                SET_FALSE(f->arg);
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
                SET_VOID(f->arg); // faster than checking bad specializations
                if (f->special != END)
                    ++f->special;
                goto continue_arg_loop;

            case PARAM_CLASS_RETURN:
                assert(VAL_PARAM_SYM(f->param) == SYM_RETURN);

                if (NOT_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_RETURN)) {
                    SET_VOID(f->arg);
                    goto continue_arg_loop;
                }

                Move_Value(f->arg, NAT_VALUE(return));

                if (f->varlist) // !!! in specific binding, always for Plain
                    f->arg->extra.binding = f->varlist;
                else
                    f->arg->extra.binding =
                        FUNC_PARAMLIST(FUNC_UNDERLYING(f->func));

                if (f->special != END)
                    ++f->special; // specialization being overwritten is right
                goto continue_arg_loop;

            case PARAM_CLASS_LEAVE:
                assert(VAL_PARAM_SYM(f->param) == SYM_LEAVE);

                if (NOT_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_LEAVE)) {
                    SET_VOID(f->arg);
                    goto continue_arg_loop;
                }

                Move_Value(f->arg, NAT_VALUE(leave));

                if (f->varlist) // !!! in specific binding, always for Plain
                    f->arg->extra.binding = f->varlist;
                else
                    f->arg->extra.binding =
                        FUNC_PARAMLIST(FUNC_UNDERLYING(f->func));

                if (f->special != END)
                    ++f->special; // specialization being overwritten is right
                goto continue_arg_loop;

            default:
                break;
            }

    //=//// IF COMING BACK TO REFINEMENT ARGS LATER, MOVE ON FOR NOW //////=//

            if (f->refine == SKIPPING_REFINEMENT_ARGS) {
                //
                // The GC will protect values up through how far we have
                // enumerated, so the argument slot cannot be uninitialized
                // bits once we pass it.  Use a safe trash so that the debug
                // build will be able to tell if we don't come back and
                // overwrite it correctly during the pickups phase.
                //
                SET_UNREADABLE_BLANK(f->arg);

                if (f->special != END)
                    ++f->special;
                goto continue_arg_loop;
            }

            if (f->special != END) {
                if (f->special == f->arg) {
                    //
                    // Just running the loop to verify arguments/refinements...
                    //
                    ++f->special;
                    goto check_arg;
                }

    //=//// SPECIALIZED ARG (already filled, so does not consume) /////////=//

                if (IS_VOID(f->special)) {
                    //
                    // A void specialized value means this particular argument
                    // is not specialized.  Still must increment the pointer
                    // before falling through to ordinary fulfillment.
                    //
                    ++f->special;
                }
                else {
                    Move_Value(f->arg, f->special);

                    ++f->special;
                    goto check_arg; // normal checking, handles errors also
                }
            }

    //=//// IF UNSPECIALIZED ARG IS INACTIVE, SET VOID AND MOVE ON ////////=//

            // Unspecialized arguments that do not consume do not need any
            // further processing or checking.  void will always be fine.
            //
            if (f->refine == ARG_TO_UNUSED_REFINEMENT) {
                SET_VOID(f->arg);
                goto continue_arg_loop;
            }

    //=//// IF LOOKBACK, THEN USE PREVIOUS EXPRESSION RESULT FOR ARG //////=//

            if (f->refine == LOOKBACK_ARG) {
                //
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

                if (IS_END(f->out)) {
                    //
                    // Seeing an END in the output slot could mean that there was
                    // really "nothing" to the left, or it could be a consequence of
                    // a frame being in an argument gathering mode, e.g.
                    //
                    //     if 1 then [2] ;-- error, THEN cannot complete `if 1`
                    //
                    // The difference can be told by the frame flag.

                    if (f->flags.bits & DO_FLAG_FULFILLING_ARG)
                        fail (Error_Partial_Lookback(f));

                    if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                        fail (Error_No_Arg(FRM_LABEL(f), f->param));

                    SET_VOID(f->arg);
                    goto continue_arg_loop;
                }

                // Note: for lookback, the distinction between NORMAL and
                // TIGHT needs to have already been handled by this point.
                //
                switch (pclass) {
                case PARAM_CLASS_NORMAL:
                    //
                    // e.g. FUNC_FLAG_DEFERS_LOOKBACK_ARG
                    //
                    // !!! Should this error include the IS_END() case above?
                    //
                    if (f->flags.bits & DO_FLAG_FULFILLING_ARG)
                        fail (Error(RE_EXPRESSION_BARRIER)); // !!! message?
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

                    if (IS_SET_WORD(f->out) || IS_GET_WORD(f->out))
                        DS_PUSH(f->out); // signal refetch after operation
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
                            Abort_Function_Args_For_Frame(f);
                            goto finished;
                        }
                    }
                    else {
                        Move_Value(f->arg, f->out);
                        SET_VAL_FLAG(f->arg, VALUE_FLAG_UNEVALUATED);
                    }

                    if (IS_SET_WORD(f->out) || IS_GET_WORD(f->out))
                        DS_PUSH(f->out); // signal refetch after operation
                    break;

                default:
                    assert(FALSE);
                }

                f->refine = ORDINARY_ARG;
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
                const REBOOL make = TRUE;
                Link_Vararg_Param_To_Frame(f, make);
                goto continue_arg_loop; // new value, type guaranteed correct
            }

    //=//// AFTER THIS, PARAMS CONSUME FROM CALLSITE IF NOT APPLY ////////=//

            assert(
                f->refine == ORDINARY_ARG
                || (IS_LOGIC(f->refine) && IS_CONDITIONAL_TRUE(f->refine))
            );

    //=//// ERROR ON END MARKER, BAR! IF APPLICABLE //////////////////////=//

            if (IS_END(f->value)) {
                if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                    fail (Error_No_Arg(FRM_LABEL(f), f->param));

                SET_VOID(f->arg);
                goto continue_arg_loop;
            }

    //=//// IF EVAL/ONLY SEMANTICS, TAKE NEXT ARG WITHOUT EVALUATION //////=//

            if (NOT(args_evaluate)) {
                Quote_Next_In_Frame(f->arg, f); // has VALUE_FLAG_UNEVALUATED
                goto check_arg;
            }

    //=//// IF EVAL SEMANTICS, DISALLOW LITERAL EXPRESSION BARRIERS ///////=//

            if (IS_BAR(f->value)) {
                //
                // Not even legal if arg is quoted.  It must come through
                // other means (e.g. literal as `'|` or `first [|]`)

                if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                    fail (Error(RE_EXPRESSION_BARRIER));

                SET_VOID(f->arg);
                goto continue_arg_loop;
            }

            switch (pclass) {

   //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes a DO/NEXT's worth) ////=//

            case PARAM_CLASS_NORMAL:
                if (Do_Next_In_Subframe_Throws(
                    f->arg,
                    f,
                    DO_FLAG_FULFILLING_ARG
                )){
                    Move_Value(f->out, f->arg);
                    Abort_Function_Args_For_Frame(f);
                    goto finished;
                }
                break;

            case PARAM_CLASS_TIGHT:
                //
                // The default for evaluated parameters is to do normal
                // infix lookahead, e.g. `square 1 + 2` would pass 3
                // to a single-arity function "square".  But if the
                // argument to square is declared #tight, it will act as
                // `(square 1) + 2`, by not applying lookahead to
                // see the + during the argument evaluation.
                //
                if (Do_Next_In_Subframe_Throws(
                    f->arg,
                    f,
                    DO_FLAG_NO_LOOKAHEAD | DO_FLAG_FULFILLING_ARG
                )){
                    Move_Value(f->out, f->arg);
                    Abort_Function_Args_For_Frame(f);
                    goto finished;
                }
                break;

    //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG /////////////////////////////=//

            case PARAM_CLASS_HARD_QUOTE: {
                Quote_Next_In_Frame(f->arg, f); // has VALUE_FLAG_UNEVALUATED
                break; }

    //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  ////////////////////////////=//

            case PARAM_CLASS_SOFT_QUOTE:
                if (!IS_QUOTABLY_SOFT(f->value)) {
                    Quote_Next_In_Frame(f->arg, f); // VALUE_FLAG_UNEVALUATED
                    goto check_arg;
                }

                if (Eval_Value_Core_Throws(f->arg, f->value, f->specifier)) {
                    Move_Value(f->out, f->arg);
                    Abort_Function_Args_For_Frame(f);
                    goto finished;
                }

                Fetch_Next_In_Frame(f);
                break;

            default:
                assert(FALSE);
        }

    //=//// TYPE CHECKING FOR (MOST) ARGS AT END OF ARG LOOP //////////////=//

        check_arg:;

            // Some arguments can be fulfilled and skip type checking or
            // take care of it themselves.  But normal args pass through
            // this code which checks the typeset and also handles it when
            // a void arg signals the revocation of a refinement usage.

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
                (IS_LOGIC(f->refine) && IS_CONDITIONAL_TRUE(f->refine)) // used
            );

            if (IS_VOID(f->arg)) {
                if (IS_LOGIC(f->refine)) {
                    //
                    // We can only revoke the refinement if this is the 1st
                    // refinement arg.  If it's a later arg, then the first
                    // didn't trigger revocation, or refine wouldn't be logic.
                    //
                    if (f->refine + 1 != f->arg)
                        fail (Error_Bad_Refine_Revoke(f));

                    SET_FALSE(f->refine); // can't re-enable...
                    f->refine = ARG_TO_REVOKED_REFINEMENT;
                    goto continue_arg_loop; // don't type check for optionality
                }
                else if (IS_CONDITIONAL_FALSE(f->refine)) {
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
                if (IS_CONDITIONAL_FALSE(f->refine))
                    fail (Error_Bad_Refine_Revoke(f));
            }

            if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
                if (NOT(TYPE_CHECK(f->param, VAL_TYPE(f->arg))))
                    fail (Error_Arg_Type(
                        FRM_LABEL(f), f->param, VAL_TYPE(f->arg))
                    );
            }
            else {
                // Varargs are odd, because the type checking doesn't
                // actually check the type of the parameter--it's always
                // a VARARGS!.  Also since the "types accepted" are a lie
                // (an [integer! <...>] takes VARARGS!, not INTEGER!) then
                // an "honest" parameter has to be made to give the error.
                //
                if (!IS_VARARGS(f->arg)) {
                    DECLARE_LOCAL (honest_param);
                    Init_Typeset(
                        honest_param,
                        FLAGIT_KIND(REB_VARARGS), // actually expected
                        VAL_PARAM_SPELLING(f->param)
                    );

                    fail (Error_Arg_Type(
                        FRM_LABEL(f), honest_param, VAL_TYPE(f->arg))
                    );
                }

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
        // been marched to an end cell...or just be the unwritable canon END
        // node to start with
        //
        assert(IS_END(f->special));

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
                fail (Error(RE_BAD_REFINE, DS_TOP));
            }

            if (VAL_TYPE(DS_TOP) == REB_0_PICKUP) {
                assert(f->special == END); // no specialization "pickups"
                f->param = DS_TOP->payload.pickup.param;
                f->refine = f->arg = DS_TOP->payload.pickup.arg;
                assert(IS_LOGIC(f->refine) && VAL_LOGIC(f->refine));
                DS_DROP;
                f->doing_pickups = TRUE;
                goto continue_arg_loop; // leaves refine, but bumps param+arg
            }

            assert(
                IS_FUNCTION(DS_TOP) // chains push these, and R_REDO_CHECKED
                || IS_SET_WORD(DS_TOP) // request for refetch after enfix
                || IS_SET_PATH(DS_TOP) // request for refetch after enfix
            );
        }

    #if !defined(NDEBUG)
        if (GET_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_LEGACY_DEBUG))
            Legacy_Convert_Function_Args(f); // BLANK!+NONE! vs. FALSE+UNSET!
    #endif

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! ARGUMENTS NOW GATHERED, DISPATCH CALL
    //
    //==////////////////////////////////////////////////////////////////==//

    execute_func:
        assert(IS_END(f->param));
        // refine can be anything.
        assert(
            IS_END(f->value)
            || (f->flags.bits & DO_FLAG_VA_LIST)
            || IS_VALUE_IN_ARRAY_DEBUG(f->source.array, f->value)
        );

        if (Trace_Flags)
            Trace_Func(FRM_LABEL(f));

        // The out slot needs initialization for GC safety during the function
        // run.  Choosing an END marker should be legal because places that
        // you can use as output targets can't be visible to the GC (that
        // includes argument arrays being fulfilled).  This offers extra
        // perks, because it means a recycle/torture will catch you if you
        // try to Do_Core into movable memory...*and* a native can tell if it
        // has written the out slot yet or not (e.g. WHILE/? refinement).
        //
        assert(IS_END(f->out));

        // Running arbitrary native code can manipulate the bindings or cache
        // of a variable.  It's very conservative to say this, but any word
        // fetches that were done for lookahead are potentially invalidated
        // by every function call.
        //
        f->gotten = NULL;

        // Cases should be in enum order for jump-table optimization
        // (R_FALSE first, R_TRUE second, etc.)
        //
        // The dispatcher may push functions to the data stack which will be
        // used to process the return result after the switch.
        //
        REBNAT dispatcher; // goto would cross initialization
        dispatcher = FUNC_DISPATCHER(f->func);
        switch (dispatcher(f)) {
        case R_FALSE:
            SET_FALSE(f->out); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_TRUE:
            SET_TRUE(f->out); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_VOID:
            SET_VOID(f->out); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_BLANK:
            SET_BLANK(f->out); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_BAR:
            SET_BAR(f->out); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_OUT:
            CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break; // checked as NOT_END() after switch()

        case R_OUT_UNEVALUATED: // returned by QUOTE and SEMIQUOTE
            SET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break;

        case R_OUT_IS_THROWN: {
            assert(THROWN(f->out));

            if (!IS_FUNCTION(f->out) || VAL_FUNC(f->out) != NAT_FUNC(exit)) {
                //
                // Do_Core only catches "definitional exits" to current frame
                //
                Abort_Function_Args_For_Frame(f);
                goto finished;
            }

            ASSERT_ARRAY(VAL_BINDING(f->out));

            if (VAL_BINDING(f->out) == FUNC_PARAMLIST(f->func)) {
                //
                // The most recent instance of a function on the stack (if
                // any) will catch a FUNCTION! style exit.
                //
                CATCH_THROWN(f->out, f->out);
                assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
            }
            else if (VAL_BINDING(f->out) == f->varlist) {
                //
                // This identifies an exit from a *specific* function
                // invocation.  We'll only match it if we have a reified
                // frame context.  (Note f->varlist may be null here.)
                //
                CATCH_THROWN(f->out, f->out);
            }
            else {
                Abort_Function_Args_For_Frame(f);
                goto finished; // stay THROWN and try to exit frames above...
            }
            CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break; }

        case R_OUT_TRUE_IF_WRITTEN:
            if (IS_END(f->out))
                SET_FALSE(f->out); // no VALUE_FLAG_UNEVALUATED
            else
                SET_TRUE(f->out); // no VALUE_FLAG_UNEVALUATED
            break;

        case R_OUT_VOID_IF_UNWRITTEN:
            if (IS_END(f->out))
                SET_VOID(f->out); // no VALUE_FLAG_UNEVALUATED
            else
                CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break;

        case R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY:
            if (IS_END(f->out))
                SET_VOID(f->out);
            else if (IS_VOID(f->out) || IS_CONDITIONAL_FALSE(f->out))
                SET_BAR(f->out);
            else
                CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break;

        case R_OUT_BLANK_IF_VOID:
            if (IS_VOID(f->out))
                SET_BLANK(f->out);
            else
                CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break;

        case R_OUT_VOID_IF_UNWRITTEN_BLANK_IF_VOID:
            if (IS_END(f->out))
                SET_VOID(f->out);
            else if (IS_VOID(f->out))
                SET_BLANK(f->out);
            else
                CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
            break;

        case R_REDO_CHECKED:
            SET_END(f->out);
            f->special = f->args_head;
            f->refine = ORDINARY_ARG; // no gathering, but need for assert
            goto do_function_arglist_in_progress;

        case R_REDO_UNCHECKED:
            //
            // This instruction represents the idea that it is desired to
            // run the f->func again.  The dispatcher may have changed the
            // value of what f->func is, for instance.
            //
            SET_END(f->out);
            goto execute_func;

        case R_REEVALUATE:
            args_evaluate = TRUE; // unnecessary?
            Drop_Function_Args_For_Frame(f);
            CLEAR_FRAME_LABEL(f);
            goto reevaluate; // we don't move index!

        case R_REEVALUATE_ONLY:
            args_evaluate = FALSE;
            Drop_Function_Args_For_Frame(f);
            CLEAR_FRAME_LABEL(f);
            goto reevaluate; // we don't move index!

        case R_UNHANDLED: // internal use only, shouldn't be returned
            assert(FALSE);

        default:
            assert(FALSE);
        }

        assert(NOT_END(f->out)); // should have overwritten
        assert(NOT(THROWN(f->out))); // throws must be R_OUT_IS_THROWN

        assert(f->eval_type == REB_FUNCTION); // shouldn't have changed

    //==////////////////////////////////////////////////////////////////==//
    //
    // DEBUG CHECK RETURN OF ALL FUNCTIONS (not just user functions)
    //
    //==////////////////////////////////////////////////////////////////==//

    // Here we know the function finished and did not throw or exit.
    // Generally the return type is validated by the Returner_Dispatcher()
    // with everything else assumed to return the correct type.  But this
    // double checks any function marked with RETURN in the debug build.

#if !defined(NDEBUG)
    if (GET_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_RETURN)) {
        REBVAL *typeset = FUNC_PARAM(f->func, FUNC_NUM_PARAMS(f->func));
        assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);
        if (!TYPE_CHECK(typeset, VAL_TYPE(f->out)))
            fail (Error_Bad_Return_Type(f->label, VAL_TYPE(f->out)));
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
            if (IS_FUNCTION(DS_TOP)) {

                Move_Value(&f->cell, f->out);

                if (Apply_Only_Throws(f->out, TRUE, DS_TOP, &f->cell, END)) {
                    Abort_Function_Args_For_Frame(f);
                    goto finished;
                }

                DS_DROP;
            }
            else if (IS_SET_WORD(DS_TOP)) {
                Move_Value(f->out, Get_Opt_Var_May_Fail(DS_TOP, SPECIFIED));
                DS_DROP;
                assert(DSP == f->dsp_orig); // should be last push
            }
            else if (IS_SET_PATH(DS_TOP)) {
                //
                // !!! Handling of SET-PATH! in the "refetch" adds questions
                // regarding possible double-evaluations of GROUP!s in the
                // path, among other things.  Review in light of decisions
                // on if this refetch is a good idea in the first place.
                //
                assert(FALSE);
                DS_DROP;
                assert(DSP == f->dsp_orig);
            }
            else
                assert(FALSE);
        }

        if (Trace_Flags)
            Trace_Return(FRM_LABEL(f), f->out);

        // !!! It would technically be possible to drop the arguments before
        // running chains... and if the chained function were to run *in*
        // this frame that could be even more optimal.  However, having the
        // original function still on the stack helps make errors clearer.
        //
        Drop_Function_Args_For_Frame(f);

        CLEAR_FRAME_LABEL(f);
        break;

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

        if (NOT_END(f->value)) {
            SET_END(f->out); // skipping the post loop where this is done
            f->eval_type = VAL_TYPE(f->value);
            goto do_next; // quickly process next item, no infix test needed
        }

        SET_VOID(f->out); // no VALUE_FLAG_UNEVALUATED
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

        SET_BAR(f->out); // no VALUE_FLAG_UNEVALUATED
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
        if (current_gotten == NULL) {
            current_gotten = Get_Opt_Var_May_Fail(current, f->specifier);
            goto do_word_in_current_unchecked;
        }

    do_word_in_current:
        assert(current_gotten == Get_Opt_Var_May_Fail(current, f->specifier));

    do_word_in_current_unchecked:
        if (IS_FUNCTION(current_gotten)) { // before IS_VOID() is common case
            f->eval_type = REB_FUNCTION;
            SET_FRAME_LABEL(f, VAL_WORD_SPELLING(current));

            if (GET_VAL_FLAG(current_gotten, VALUE_FLAG_ENFIXED)) {
                f->refine = LOOKBACK_ARG;
                goto do_function_in_current_gotten;
            }

            SET_END(f->out);
            f->refine = ORDINARY_ARG;
            goto do_function_in_current_gotten;
        }

        if (IS_VOID(current_gotten)) // need `:x` if `x` is unset
            fail (Error_No_Value_Core(current, f->specifier));

        Move_Value(f->out, current_gotten); // no copy VALUE_FLAG_UNEVALUATED

    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_LIT_WORD_DECAY) && IS_LIT_WORD(f->out))
            VAL_SET_TYPE_BITS(f->out, REB_WORD); // don't reset full header!
    #endif
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

        if (IS_END(f->value)) {
            DECLARE_LOCAL (specific);
            Derelativize(specific, current, f->specifier);
            fail (Error(RE_NEED_VALUE, specific)); // `do [a:]` is illegal
        }

        // f->value is guarded implicitly by the frame, but `current` is a
        // transient local pointer that might be to a va_list REBVAL* that
        // has already been fetched.  The bits will stay live until va_end(),
        // but a GC wouldn't see it.
        //
        DS_PUSH_RELVAL(current, f->specifier);

        if (Do_Next_Mid_Frame_Throws(f)) { // lightweight reuse of `f`
            DS_DROP;
            goto finished;
        }

        Move_Value(Sink_Var_May_Fail(DS_TOP, SPECIFIED), f->out);

        DS_DROP;
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
// header--because header bits contain information like WORD_FLAG_BOUND.
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
        // If the source array we are processing that is yielding values is
        // part of the deep copy of a function body, it's possible that this
        // GROUP! is a "relative ANY-ARRAY!" that needs the specifier to
        // resolve the relative any-words and other any-arrays inside it...
        //
        REBSPC *derived = Derive_Specifier(f->specifier, current);
        if (Do_At_Throws(
            f->out,
            VAL_ARRAY(current), // the GROUP!'s array
            VAL_INDEX(current), // index in group's REBVAL (may not be head)
            derived
        )){
            goto finished;
        }

        CLEAR_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
        break; }

//==//////////////////////////////////////////////////////////////////////==//
//
// [PATH!]
//
//==//////////////////////////////////////////////////////////////////////==//

    case REB_PATH: {
        REBSTR *label;
        if (Do_Path_Throws_Core(
            f->out,
            &label, // requesting label says we run functions (not GET-PATH!)
            current,
            f->specifier,
            NULL // `setval`: null means don't treat as SET-PATH!
        )){
            goto finished;
        }

        if (IS_VOID(f->out)) // need `:x/y` if `y` is unset
            fail (Error_No_Value_Core(current, f->specifier));

        if (IS_FUNCTION(f->out)) {
            f->eval_type = REB_FUNCTION;
            if (label == NULL)
                SET_FRAME_LABEL(f, Canon(SYM___ANONYMOUS__));
            else
                SET_FRAME_LABEL(f, label);

            // object/func or func/refinements or object/func/refinement
            //
            // Because we passed in a label symbol, the path evaluator was
            // willing to assume we are going to invoke a function if it
            // is one.  Hence it left any potential refinements on data stack.
            //
            assert(DSP >= f->dsp_orig);

            Move_Value(&f->cell, f->out);
            current_gotten = &f->cell;
            SET_END(f->out);
            f->refine = ORDINARY_ARG; // paths are never enfixed (for now)
            goto do_function_in_current_gotten;
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

        if (IS_END(f->value)) {
            DECLARE_LOCAL (specific);
            Derelativize(specific, current, f->specifier);
            fail (Error(RE_NEED_VALUE, specific)); // `do [a/b:]` is illegal
        }

        // f->value is guarded implicitly by the frame, but `current` is a
        // transient local pointer that might be to a va_list REBVAL* that
        // has already been fetched.  The bits will stay live until va_end(),
        // but a GC wouldn't see it.
        //
        DS_PUSH_RELVAL(current, f->specifier);
        
        if (Do_Next_Mid_Frame_Throws(f)) { // lighweight reuse of `f`
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

        // !!! Due to the way this is currently designed, throws need to be
        // written to a location distinct from the path and also distinct from
        // the value being set.  Review.
        //
        DECLARE_LOCAL (temp);

        if (Do_Path_Throws_Core(
            temp, // output location if thrown
            NULL, // not requesting symbol means refinements not allowed
            &f->cell, // still holding SET-PATH! we got in
            SPECIFIED, // current derelativized when pushed to DS_TOP
            f->out // value to set (already in f->out)
        )) {
            fail (Error_No_Catch_For_Throw(temp));
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
        if (Do_Path_Throws_Core(
            f->out,
            NULL, // not requesting symbol means refinements not allowed
            current,
            f->specifier,
            NULL // `setval`: null means don't treat as SET-PATH!
        )){
            goto finished;
        }

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
// Treat everything else as inert
//
//==//////////////////////////////////////////////////////////////////////==//

    default:
    inert:
        assert(f->eval_type < REB_MAX);
        Derelativize(f->out, current, f->specifier);
        SET_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED);
        break;
    }

    //==////////////////////////////////////////////////////////////////==//
    //
    // END MAIN SWITCH STATEMENT
    //
    //==////////////////////////////////////////////////////////////////==//

    assert(!THROWN(f->out)); // should have jumped to exit sooner

    if (IS_END(f->value))
        goto finished;

    f->eval_type = VAL_TYPE(f->value);

    if (f->flags.bits & DO_FLAG_NO_LOOKAHEAD) {
        //
        // Don't do infix lookahead if asked *not* to look.  See the
        // PARAM_CLASS_TIGHT parameter convention for the use of this
        //
        assert(NOT(f->flags.bits & DO_FLAG_TO_END));
    }
    else if (f->eval_type == REB_WORD && IS_WORD_BOUND(f->value)) {

        if (f->gotten == NULL)
            f->gotten = Get_Opt_Var_May_Fail(f->value, f->specifier);
        else
            assert(f->gotten == Get_Opt_Var_May_Fail(f->value, f->specifier));

    //=//// DO/NEXT WON'T RUN MORE CODE UNLESS IT'S AN INFIX FUNCTION /////=//

        if (
            NOT_VAL_FLAG(f->gotten, VALUE_FLAG_ENFIXED)
            && NOT(f->flags.bits & DO_FLAG_TO_END)
        ){
            goto finished;
        }

    //=//// IT'S INFIX OR WE'RE DOING TO THE END...DISPATCH LIKE WORD /////=//

        START_NEW_EXPRESSION_MAY_THROW(f, goto finished);
        // ^-- sets args_evaluate, do_count, Ctrl-C may abort

        if (NOT(IS_FUNCTION(f->gotten))) {
            current = f->value;
            current_gotten = f->gotten;
            f->gotten = NULL;
            Fetch_Next_In_Frame(f);
            goto do_word_in_current;
        }

        f->eval_type = REB_FUNCTION;

        if (GET_VAL_FLAG(f->gotten, VALUE_FLAG_ENFIXED)) {
            if (
                GET_VAL_FLAG(f->gotten, FUNC_FLAG_DEFERS_LOOKBACK_ARG)
                && (f->flags.bits & DO_FLAG_FULFILLING_ARG)
            ){
                // This is the special case; we have a lookback function
                // pending but it wants to defer its first argument as
                // long as possible--and we're on the last parameter of
                // some function.  Skip the "lookahead" and let whoever
                // is gathering arguments (or whoever's above them) finish
                // the expression before taking the pending operation.
                //
                assert(NOT(f->flags.bits & DO_FLAG_TO_END));
            }
            else if (GET_VAL_FLAG(f->gotten, FUNC_FLAG_QUOTES_FIRST_ARG)) {
                //
                // Left-quoting by enfix needs to be done in the lookahead
                // before an evaluation, not this one that's after.  This
                // error happens in cases like:
                //
                //     left-quote: enfix func [:value] [:value]
                //     quote <something> left-quote
                //
                // !!! Is this the ideal place to be delivering the error?
                //
                fail (Error_Lookback_Quote_Too_Late(f->value, f->specifier));
            }
            else {
                // This is a case for an evaluative lookback argument we
                // don't want to defer, e.g. a #tight argument or a normal
                // one which is not being requested in the context of
                // parameter fulfillment.  We want to reuse the f->out
                //
                SET_FRAME_LABEL(f, VAL_WORD_SPELLING(f->value));
                f->refine = LOOKBACK_ARG;
                current = f->value;
                current_gotten = f->gotten;
                f->gotten = NULL;
                Fetch_Next_In_Frame(f);
                goto do_function_in_current_gotten;
            }
        }
        else {
            SET_END(f->out);
            SET_FRAME_LABEL(f, VAL_WORD_SPELLING(f->value));
            f->refine = ORDINARY_ARG;
            current = f->value;
            current_gotten = f->gotten;
            f->gotten = NULL;
            Fetch_Next_In_Frame(f);
            goto do_function_in_current_gotten;
        }
    }

    // Continue evaluating rest of block if not just a DO/NEXT
    //
    if (f->flags.bits & DO_FLAG_TO_END)
        goto do_next;

finished:;

#if !defined(NDEBUG)
    Do_Core_Exit_Checks_Debug(f); // will get called unless a fail() longjmps
#endif

    // All callers must inspect for THROWN(f->out), and most should also
    // inspect for IS_END(f->value)
}
