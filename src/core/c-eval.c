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
    // between runs.  It could be an environment variable, but sometimes it's
    // just easier to set the number here.
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
    assert(Eval_Count >= 0);
    if (--Eval_Count == 0) {
        //
        // Note that Do_Signals_Throws() may do a recycle step of the GC, or
        // it may spawn an entire interactive debugging session via
        // breakpoint before it returns.  It may also FAIL and longjmp out.
        //
        SET_END(&f->cell);
        if (Do_Signals_Throws(KNOWN(&f->cell))) {
            Move_Value(f->out, KNOWN(&f->cell));
            return TRUE;
        }

        assert(IS_END(&f->cell));
    }

    UPDATE_EXPRESSION_START(f); // !!! See FRM_INDEX() for caveats

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
            if (do_count == TG_Break_At || do_count == DO_COUNT_BREAKPOINT) { \
                Debug_Fmt("DO_COUNT_BREAKPOINT at %d", f->do_count_debug); \
                Dump_Frame_Location(f); \
                debug_break(); /* see %debug_break.h */ \
            } \
        } while (FALSE)
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
//     Must be NULL if source is a va_list, else next value to be fetched
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
    REBUPT do_count = f->do_count_debug = TG_Do_Count; // snapshot start tick
#endif

    // Capture the data stack pointer on entry.  Refinements are pushed to
    // the stack and need to be checked if any are not processed.  Also things
    // like the CHAIN dispatcher uses it to queue pending functions.  This
    // cannot be done in Push_Frame(), because some routines (like Reduce_XXX)
    // reuse the frame across multiple calls and accrue stack state, and that
    // stack state should be skipped when considering the usages in Do_Core()
    //
    f->dsp_orig = DSP;

    REBOOL args_evaluate; // set on every iteration (varargs do, EVAL/ONLY...)

    // APPLY and a DO of a FRAME! both use process_function.
    //
    if (f->flags.bits & DO_FLAG_APPLYING) {
        args_evaluate = NOT(f->flags.bits & DO_FLAG_NO_ARGS_EVALUATE);

        f->refine = ORDINARY_ARG; // "APPLY infix" not supported
        assert(IS_END(f->out));
        goto process_function;
    }

    f->eval_type = VAL_TYPE(f->value);

#if !defined(NDEBUG)
    SNAP_STATE(&f->state_debug); // to make sure stack balances, etc.
    Do_Core_Entry_Checks_Debug(f); // run once per Do_Core()
#endif

    // This is an important guarantee...the out slot needs to have some form
    // of initialization to allow GC.  END is chosen because that is what
    // natives can count on the f->out slot to be, but lookback arguments
    // also are passed by way of the out slot.
    //
    assert(NOT(IS_TRASH_DEBUG(f->out)));

do_next:;

    START_NEW_EXPRESSION_MAY_THROW(f, goto finished);
    // ^-- sets args_evaluate, do_count, Ctrl-C may abort

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

    current = f->value; // <-- DO_COUNT_BREAKPOINT landing spot
    current_gotten = f->gotten;
    f->gotten = END;
    Fetch_Next_In_Frame(f);

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

    // !!! We never want to do infix processing if the args aren't evaluating
    // (e.g. arguments in a va_list from a C function calling into Rebol)
    // But this is distinct from DO_FLAG_NO_LOOKAHEAD (which Apply_Only also
    // sets), which really controls the after lookahead step.  Consider this
    // edge case.
    //
    if (NOT_END(f->value) && IS_WORD(f->value) && args_evaluate) {
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
                assert(IS_END(f->out));
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

        SET_END(f->out); // clear out previous result (needs GC-safe data)
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
        TRASH_POINTER_IF_DEBUG(current); // shouldn't be used below
        TRASH_POINTER_IF_DEBUG(current_gotten);

        assert(
            (f->refine == ORDINARY_ARG && IS_END(f->out))
            || f->refine == LOOKBACK_ARG
        ); // ORDINARY_ARG and LOOKBACK_ARG are local to this file

        // There may be refinements pushed to the data stack to process, if
        // the call originated from a path dispatch.
        //
        assert(DSP >= f->dsp_orig);

        f->arg = f->args_head;
        f->param = FUNC_FACADE_HEAD(f->phase);

        // We want the frame's "scratch" cell to be GC safe during a live
        // function call.
        //
        // !!! Might it be possible to avoid this initialization if the cell
        // was used to calculate a temporary or eval, and only initialize it
        // if not?  This might be more trouble than it's worth, given that
        // having natives take for granted that it's IS_END() has value.
        //
        SET_END(&f->cell);

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

                    if (!IS_LOGIC(f->arg))
                        fail (Error_Non_Logic_Refinement(f));

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
                    if (!IS_WORD(f->refine)) continue; // non-refinement
                    if (
                        VAL_WORD_SPELLING(f->refine) // canon when pushed
                        == VAL_PARAM_CANON(f->param) // #2258
                    ){
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
                        f->refine = SKIPPING_REFINEMENT_ARGS;
                        goto continue_arg_loop;
                    }
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

                if (IS_END(f->out)) {
                    //
                    // Seeing an END in the output slot could mean that there
                    // was really "nothing" to the left, or it could be a
                    // consequence of a frame being in an argument gathering
                    // mode, e.g.
                    //
                    //     if 1 then [2] ;-- error, THEN can't complete `if 1`
                    //
                    // The difference can be told by the frame flag.

                    if (f->flags.bits & DO_FLAG_FULFILLING_ARG)
                        fail (Error_Partial_Lookback(f));

                    // If an enfixed function finds it has a variadic in its
                    // first slot, then nothing available on the left is o.k.
                    // It means we have to put a VARARGS! in that argument
                    // slot which will react with TRUE to TAIL?, so feed it
                    // from the global empty array.
                    //
                    if (GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
                        VAL_RESET_HEADER_EXTRA(
                            f->arg,
                            REB_VARARGS,
                            VARARGS_FLAG_ENFIXED // in case anyone cares
                        );
                        INIT_BINDING(f->arg, EMPTY_ARRAY); // feed finished

                        const REBOOL make = FALSE; // use existing array
                        Link_Vararg_Param_To_Frame(f, make);
                        goto continue_arg_loop;
                    }

                    if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                        fail (Error_No_Arg(f, f->param));

                    Init_Void(f->arg);
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

                SET_END(f->out);

                // Now that we've gotten the argument figured out, make a
                // singular array to feed it to the variadic.
                //
                // !!! See notes on VARARGS_FLAG_ENFIXED about how this is
                // somewhat shady, as any evaluations happen *before* the
                // TAKE on the VARARGS.  Experimental feature.
                //
                if (GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
                    REBARR *feed = Alloc_Singular_Array();
                    Move_Value(ARR_HEAD(feed), f->arg);
                    MANAGE_ARRAY(feed);

                    REBARR *array1 = Alloc_Singular_Array();
                    Init_Block(ARR_HEAD(array1), feed); // index 0 for feeding
                    MANAGE_ARRAY(array1);

                    VAL_RESET_HEADER_EXTRA(
                        f->arg,
                        REB_VARARGS,
                        VARARGS_FLAG_ENFIXED // don't evaluate *again* on TAKE
                    );
                    INIT_BINDING(f->arg, array1);

                    const REBOOL make = FALSE; // use existing array
                    Link_Vararg_Param_To_Frame(f, make);
                    goto continue_arg_loop;

                    // As written, the type will be checked when (and if) a
                    // TAKE happens.  Whether that's good or bad depends on
                    // the bigger issue of whether this feature is misguided.
                    //
                    goto continue_arg_loop;
                }

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
                Prep_Stack_Cell(f->arg);
                Link_Vararg_Param_To_Frame(f, make);
                goto continue_arg_loop; // new value, type guaranteed correct
            }

    //=//// AFTER THIS, PARAMS CONSUME FROM CALLSITE IF NOT APPLY ////////=//

            assert(
                f->refine == ORDINARY_ARG
                || (IS_LOGIC(f->refine) && IS_TRUTHY(f->refine))
            );

    //=//// ERROR ON END MARKER, BAR! IF APPLICABLE //////////////////////=//

            if (IS_END(f->value)) {
                if (NOT_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                    fail (Error_No_Arg(f, f->param));

                Prep_Stack_Cell(f->arg);
                Init_Void(f->arg);
                goto continue_arg_loop;
            }

    //=//// IF EVAL/ONLY SEMANTICS, TAKE NEXT ARG WITHOUT EVALUATION //////=//

            if (NOT(args_evaluate)) {
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

            case PARAM_CLASS_NORMAL:
                Prep_Stack_Cell(f->arg);
                if (Do_Next_In_Subframe_Throws(
                    f->arg,
                    f,
                    DO_FLAG_FULFILLING_ARG
                )){
                    Move_Value(f->out, f->arg);
                    Abort_Function(f);
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
                Prep_Stack_Cell(f->arg);

                if (Do_Next_In_Subframe_Throws(
                    f->arg,
                    f,
                    DO_FLAG_NO_LOOKAHEAD | DO_FLAG_FULFILLING_ARG
                )){
                    Move_Value(f->out, f->arg);
                    Abort_Function(f);
                    goto finished;
                }
                break;

    //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG /////////////////////////////=//

            case PARAM_CLASS_HARD_QUOTE:
                Prep_Stack_Cell(f->arg);
                Quote_Next_In_Frame(f->arg, f); // has VALUE_FLAG_UNEVALUATED
                break;

    //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  ////////////////////////////=//

            case PARAM_CLASS_SOFT_QUOTE:
                if (!IS_QUOTABLY_SOFT(f->value)) {
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
                        fail (Error_Bad_Refine_Revoke(f));

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
                    fail (Error_Bad_Refine_Revoke(f));
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

    #if !defined(NDEBUG)
        if (GET_VAL_FLAG(FUNC_VALUE(f->phase), FUNC_FLAG_LEGACY_DEBUG))
            Legacy_Convert_Function_Args(f); // BLANK!+NONE! vs. FALSE+UNSET!
    #endif

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! ARGUMENTS NOW GATHERED, DISPATCH PHASE
    //
    //==////////////////////////////////////////////////////////////////==//

    redo_unchecked:
        assert(IS_END(f->param));
        // refine can be anything.
        assert(
            IS_END(f->value)
            || (f->flags.bits & DO_FLAG_VA_LIST)
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
        assert(IS_END(f->out));
        assert(f->out->header.bits & VALUE_FLAG_STACK);

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

            if (
                IS_FUNCTION(f->out)
                && VAL_FUNC(f->out) == NAT_FUNC(exit)
                && Same_Binding(VAL_BINDING(f->out), f)
            ){
                // Do_Core catches "definitional exits" to current frame, e.g.
                // throws where the "/name" is the EXIT native with a binding
                // to this frame, and the thrown value is the return code.
                //
                // !!! This might be a little more natural if the name of the
                // throw was a FRAME! value.  But that also would mean throws
                // named by frames couldn't be taken advantage by the user for
                // other features, while this only takes one function away.
                //
                CATCH_THROWN(f->out, f->out);
                assert(NOT_VAL_FLAG(f->out, VALUE_FLAG_UNEVALUATED));
            }
            else {
                // Stay THROWN and let stack levels above try and catch
                //
                Abort_Function(f);
                goto finished;
            }
            break; }

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
            f->special = f->args_head;
            f->refine = ORDINARY_ARG; // no gathering, but need for assert
            SET_END(f->out);
            goto process_function;

        case R_REDO_UNCHECKED:
            //
            // This instruction represents the idea that it is desired to
            // run the f->phase again.  The dispatcher may have changed the
            // value of what f->phase is, for instance.
            //
            SET_END(f->out);
            goto redo_unchecked;

        case R_REEVALUATE_CELL:
            args_evaluate = TRUE; // unnecessary?
            goto prep_for_reevaluate;

        case R_REEVALUATE_CELL_ONLY:
            args_evaluate = FALSE;
            goto prep_for_reevaluate;

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

        assert(NOT_END(f->out)); // should have overwritten
        assert(NOT(THROWN(f->out))); // throws must be R_OUT_IS_THROWN
        if (Is_Bindable(f->out))
            assert(f->out->extra.binding != NULL);

        assert(f->eval_type == REB_FUNCTION); // shouldn't have changed

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
    if (GET_VAL_FLAG(FUNC_VALUE(f->phase), FUNC_FLAG_RETURN)) {
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

            if (Apply_Only_Throws(f->out, TRUE, fun, &f->cell, END)) {
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
        if (current_gotten == END) {
            current_gotten = Get_Opt_Var_May_Fail(current, f->specifier);
            goto do_word_in_current_unchecked;
        }

    do_word_in_current:
        assert(current_gotten == Get_Opt_Var_May_Fail(current, f->specifier));

    do_word_in_current_unchecked:
        if (IS_FUNCTION(current_gotten)) { // before IS_VOID() is common case
            Push_Function(
                f,
                VAL_WORD_SPELLING(current),
                VAL_FUNC(current_gotten),
                VAL_BINDING(current_gotten)
            );

            if (GET_VAL_FLAG(current_gotten, VALUE_FLAG_ENFIXED))
                f->refine = LOOKBACK_ARG;
            else {
                f->refine = ORDINARY_ARG;
                SET_END(f->out);
            }
            goto process_function;
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
            fail (Error_Need_Value_Raw(specific)); // `do [a:]` is illegal
        }

        if (NOT(args_evaluate)) { // e.g. `eval/only quote x: 1 + 2`, x => 1
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

            if (Do_Next_Mid_Frame_Throws(f)) { // lightweight reuse of `f`
                DS_DROP;
                goto finished;
            }

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
            current,
            f->specifier,
            NULL // `setval`: null means don't treat as SET-PATH!
        )){
            goto finished;
        }

        if (IS_VOID(f->out)) // need `:x/y` if `y` is unset
            fail (Error_No_Value_Core(current, f->specifier));

        if (IS_FUNCTION(f->out)) {
            Push_Function(
                f,
                opt_label, // NULL label means anonymous
                VAL_FUNC(f->out),
                VAL_BINDING(f->out)
            );

            f->refine = ORDINARY_ARG; // paths are never enfixed (for now)
            SET_END(f->out);
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

        if (IS_END(f->value)) {
            DECLARE_LOCAL (specific);
            Derelativize(specific, current, f->specifier);
            fail (Error_Need_Value_Raw(specific)); // `do [a/b:]` is illegal
        }

        if (NOT(args_evaluate)) {
            Derelativize(f->out, f->value, f->specifier);

            // !!! Due to the way this is currently designed, throws need to
            // be written to a location distinct from the path and also
            // distinct from the value being set.  Review.
            //
            DECLARE_LOCAL (temp);

            if (Do_Path_Throws_Core(
                temp, // output location if thrown
                NULL, // not requesting symbol means refinements not allowed
                current, // still holding SET-PATH! we got in
                f->specifier, // specifier for current
                f->out // value to set (already in f->out)
            )) {
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

            // !!! Due to the way this is currently designed, throws need to
            // be written to a location distinct from the path and also
            // distinct from the value being set.  Review.
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

        if (NOT_END(f->value)) {
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
        if (NOT(args_evaluate)) {
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
    else if (f->eval_type == REB_WORD) {

        if (f->gotten == END)
            f->gotten = Get_Opt_Var_Else_End(f->value, f->specifier);
        else
            assert(
                f->gotten == Get_Opt_Var_Else_End(f->value, f->specifier)
            );

    //=//// DO/NEXT WON'T RUN MORE CODE UNLESS IT'S AN INFIX FUNCTION /////=//

        if (
            NOT(f->flags.bits & DO_FLAG_TO_END)
            && (
                f->gotten == END // could fold the END check in with masking
                || NOT_VAL_FLAG(f->gotten, VALUE_FLAG_ENFIXED)
            )
        ){
            goto finished;
        }

    //=//// IT'S INFIX OR WE'RE DOING TO THE END...DISPATCH LIKE WORD /////=//

        START_NEW_EXPRESSION_MAY_THROW(f, goto finished);
        // ^-- sets args_evaluate, do_count, Ctrl-C may abort

        if (VAL_TYPE_OR_0(f->gotten) != REB_FUNCTION) { // END is REB_0
            current = f->value;
            current_gotten = f->gotten; // if END, the word will error
            f->gotten = END;
            Fetch_Next_In_Frame(f);
            goto do_word_in_current;
        }

        if (GET_VAL_FLAG(f->gotten, VALUE_FLAG_ENFIXED)) {
            if (GET_VAL_FLAG(f->gotten, FUNC_FLAG_QUOTES_FIRST_ARG)) {
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

            if (
                GET_VAL_FLAG(f->gotten, FUNC_FLAG_DEFERS_LOOKBACK)
                && (f->flags.bits & DO_FLAG_FULFILLING_ARG)
                && NOT(f->flags.bits & DO_FLAG_DAMPEN_DEFER)
            ){
                assert(NOT(f->flags.bits & DO_FLAG_TO_END));
                assert(Is_Function_Frame_Fulfilling(f->prior));

                // We have a lookback function pending, but it wants its first
                // argument to be one "complete expression".  Consider ELSE in
                // the case of:
                //
                //     print if false ["a"] else ["b"]
                //
                // The first time ELSE is seen, PRINT and IF are on the stack
                // above it, fulfilling their arguments...and we've just
                // written `["a"]` into f->out in the switch() above.  ELSE
                // wants us to let IF finish before it runs, but it doesn't
                // want to repeat the deferment a second time, such that PRINT
                // completes also before running.
                //
                // Defer this lookahead, but tell the frame above (e.g. IF in
                // the above example) not to continue this pattern when it
                // finishes and sees itself in the same position.
                //
                assert(NOT(f->prior->flags.bits & DO_FLAG_DAMPEN_DEFER));
                f->prior->flags.bits |= DO_FLAG_DAMPEN_DEFER;
            }
            else {
                // The DAMPEN_DEFER bit should only be set if we're taking
                // the result now for a deferred lookback function.  Clear it
                // in any case.
                //
                assert(
                    NOT(f->flags.bits & DO_FLAG_DAMPEN_DEFER)
                    || GET_VAL_FLAG(f->gotten, FUNC_FLAG_DEFERS_LOOKBACK)
                );
                f->flags.bits &= ~DO_FLAG_DAMPEN_DEFER;

                // This is a case for an evaluative lookback argument we
                // don't want to defer, e.g. a #tight argument or a normal
                // one which is not being requested in the context of
                // parameter fulfillment.  We want to reuse the f->out

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
            }
        }
        else {
            Push_Function(
                f,
                VAL_WORD_SPELLING(f->value),
                VAL_FUNC(f->gotten),
                VAL_BINDING(f->gotten)
            );

            f->refine = ORDINARY_ARG;
            SET_END(f->out);

            f->gotten = END;
            Fetch_Next_In_Frame(f); // advances f->value
            goto process_function;
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
