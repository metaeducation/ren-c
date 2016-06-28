//
//  File: %c-eval.c
//  Summary: "Central Interpreter Evaluator"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// behavior, and there are other flags for controlling its other behaviors.
//
// For comprehensive notes on the input parameters, output parameters, and
// internal state variables...see %sys-do.h and `struct Reb_Frame`.
//
// NOTES:
//
// * Do_Core() is a very long routine.  That is largely on purpose, because it
//   doesn't contain repeated portions.  If it were broken into functions that
//   would add overhead for little benefit, and prevent interesting tricks
//   and optimizations.  Note that it is broken down into sections, and
//   the invariants in each section are made clear with comments and asserts.
//
// * The evaluator only moves forward, and it consumes exactly one element
//   from the input at a time.  This input may be a source where the index
//   needs to be tracked and care taken to contain the index within its
//   boundaries in the face of change (e.g. a mutable ARRAY).  Or it may be
//   an entity which tracks its own position on each fetch, where "indexor"
//   is serving as a flag and should be left static.
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
    // !!! Taking this number on the command line could be convenient.
    //
    // Note also there is `Dump_Frame_Location()` if there's a trouble spot
    // and you want to see what the state is.  It will reify C va_list
    // input for you, so you can see what the C caller passed as an array.
    //
#endif


static inline void Start_New_Expression_Core(struct Reb_Frame *f) {
    f->expr_index = f->index; // !!! See FRM_INDEX() for caveats
    if (Trace_Flags)
        Trace_Line(f);
}

#ifdef NDEBUG
    #define START_NEW_EXPRESSION(f) \
        Start_New_Expression_Core(f)
#else
    // Macro is used to mutate local do_count variable in Do_Core (for easier
    // browsing in the watchlist) as well as to not be in a deeper stack level
    // than Do_Core when a DO_COUNT_BREAKPOINT is hit.
    //
    #define START_NEW_EXPRESSION(f) \
        do { \
            Start_New_Expression_Core(f); \
            do_count = Do_Core_Expression_Checks_Debug(f); \
            if (do_count == DO_COUNT_BREAKPOINT) { \
                Debug_Fmt("DO_COUNT_BREAKPOINT hit at %d", f->do_count); \
                Dump_Frame_Location(f); \
                debug_break(); /* see %debug_break.h */ \
            } \
        } while (FALSE)
#endif


static inline void Type_Check_Arg_For_Param_May_Fail(struct Reb_Frame * f) {
    if (!TYPE_CHECK(f->param, VAL_TYPE(f->arg)))
        fail (Error_Arg_Type(FRM_LABEL(f), f->param, VAL_TYPE(f->arg)));
}

static inline void Drop_Function_Args_For_Frame(struct Reb_Frame *f) {
    Drop_Function_Args_For_Frame_Core(f, TRUE);
}

static inline void Abort_Function_Args_For_Frame(struct Reb_Frame *f) {
    Drop_Function_Args_For_Frame(f);

    // If a function call is aborted, there may be pending refinements (if
    // in the gathering phase) or functions (if running a chainer) on the
    // data stack.  They must be dropped to balance.
    //
    DS_DROP_TO(f->dsp_orig);
}

static inline REBOOL Specialized_Arg(REBVAL *arg) {
    return NOT_END(arg); // END marker is used to indicate "pending" arg slots
}


//
//  Do_Core: C
//
// While this routine looks very complex, it's actually not that difficult
// to step through.  A lot of it is assertions, debug tracking, and comments.
//
// Comments on the definition of Reb_Frame are a good place to start looking
// to understand what's going on.  See %sys-frame.h for full details.
//
// To summarize, these fields are required upon initialization:
//
//     f->value
//     Fetched first value to execute (may not be an END marker)
//
//     f->eval_type
//     Kind of execution requested (should line up with f->value)
//
//     f->source
//     Contains the REBARR* or C va_list of subsequent values to fetch
//
//     f->index
//     Needed if f->source is an array (can be garbage if it's a C va_list)
//
//     f->pending
//     Must be VA_LIST_PENDING if source is a va_list, else NULL
//
//     f->specifier
//     Context where to look up relative values in f->source, else NULL
//
//     f->out*
//     REBVAL pointer to which the evaluation's result should be written
//     Can point to uninitialized bits, unless f->eval_type is ET_LOOKBACK,
//     in which case it must be the REBVAL to use as first infix argument
//
//     f->gotten
//     Must be either be the Get_Var() lookup of f->value, or NULL
//
// More detailed assertions of the preconditions, postconditions, and state
// at each evaluation step are contained in %d-eval.c
//
void Do_Core(struct Reb_Frame * const f)
{
#if !defined(NDEBUG)
    REBUPT do_count = f->do_count = TG_Do_Count; // snapshot initial state
#endif

    // Establish baseline for whether we are to evaluate function argumentsn
    // according to the flags passed in.  EVAL can change this with EVAL/ONLY
    //
    REBOOL args_evaluate = NOT(f->flags & DO_FLAG_NO_ARGS_EVALUATE);

    // APPLY and a DO of a FRAME! both use this same code path.
    //
    if (f->flags & DO_FLAG_APPLYING) {
        assert(f->eval_type != ET_LOOKBACK); // "apply infixedly" not supported
        goto do_function_arglist_in_progress;
    }

    PUSH_CALL(f);

#if !defined(NDEBUG)
    SNAP_STATE(&f->state); // to make sure stack balances, etc.
    Do_Core_Entry_Checks_Debug(f); // run once per Do_Core()
#endif

    // Check just once (stack level would be constant if checked in a loop).
    //
    // Note that the eval_type can be deceptive; it's preloaded by the caller
    // and may be something like ET_FUNCTION.  That suggests args pushed
    // that the error machinery needs to free...but we haven't gotten to the
    // code that does that yet by this point!  Say the frame is inert.
    //
    if (C_STACK_OVERFLOWING(&f)) {
        f->eval_type = ET_INERT;
        Trap_Stack_Overflow();
    }

    // Capture the data stack pointer on entry (used by debug checks, but
    // also refinements are pushed to stack and need to be checked if there
    // are any that are not processed)
    //
    f->dsp_orig = DSP;

do_next:

    assert(Eval_Count != 0);

    if (--Eval_Count == 0 || Eval_Signals) {
        //
        // Note that Do_Signals_Throws() may do a recycle step of the GC, or
        // it may spawn an entire interactive debugging session via
        // breakpoint before it returns.  It may also FAIL and longjmp out.
        //
        REBUPT eval_type_saved = f->eval_type;
        f->eval_type = ET_INERT;

        INIT_CELL_WRITABLE_IF_DEBUG(&f->cell.eval);
        if (Do_Signals_Throws(SINK(&f->cell.eval))) {
            *f->out = *KNOWN(&f->cell.eval);
            goto finished;
        }

        f->eval_type = eval_type_saved;

        if (!IS_VOID(&f->cell.eval)) {
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

reevaluate:
    // ^--
    // `reevaluate` is jumped to by EVAL, and must skip the possible Recycle()
    // from the above.  Whenever `eval` holds a REBVAL it is unseen by the GC
    // *by design*.  This avoids having to initialize it or GC-safe null it
    // each time through the evaluator loop.  It will only be protected by
    // the GC indirectly when its properties are extracted during the switch,
    // such as a function that gets stored into `f->func`.
    //
    // (We also want the debugger to consider the triggering EVAL as the
    // start of the expression, and don't want to advance `expr_index`).

    //==////////////////////////////////////////////////////////////////==//
    //
    // BEGIN MAIN SWITCH STATEMENT
    //
    //==////////////////////////////////////////////////////////////////==//

    // This switch is done via ET_XXX and not just switching on the VAL_TYPE()
    // (e.g. REB_XXX).  The reason is due to "jump table" optimizing--because
    // the REB_XXX types are sparse, the switch would be less efficient than
    // when switching on values that are packed consecutively (e.g. ET_XXX).
    //
    // Note that infix ("lookback") functions are dispatched *after* the
    // switch...unless DO_FLAG_NO_LOOKAHEAD is set.

    START_NEW_EXPRESSION(f);

    switch (f->eval_type) { // <-- DO_COUNT_BREAKPOINT landing spot

//==//////////////////////////////////////////////////////////////////////==//
//
// [no evaluation] (REB_BLOCK, REB_INTEGER, REB_STRING, etc.)
//
// Copy the value's bits to f->out and fetch the next value.
//
//==//////////////////////////////////////////////////////////////////////==//

    case ET_INERT:
        QUOTE_NEXT_REFETCH(f->out, f); // clears VALUE_FLAG_EVALUATED
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [BAR! and LIT-BAR!]
//
// If an expression barrier is seen in-between expressions (as it will always
// be if hit in this switch), it evaluates to void.  It only errors in argument
// fulfillment during the switch case for ANY-FUNCTION!.
//
// LIT-BAR! decays into an ordinary BAR! if seen here by the evaluator.
//
// Note that natives and dialects frequently do their own interpretation of
// BAR!--rather than just evaluate it and let it mean something equivalent
// to an unset.  For instance:
//
//     case [false [print "F"] | true [print ["T"]]
//
// If CASE did not specially recognize BAR!, it would complain that the
// "second condition" had no value.  So if you are looking for a BAR! behavior
// and it's not passing through here, check the construct you are using.
//
//==//////////////////////////////////////////////////////////////////////==//

    case ET_BAR:
        FETCH_NEXT_ONLY_MAYBE_END(f);
        if (NOT_END(f->value)) {
            f->eval_type = Eval_Table[VAL_TYPE(f->value)];
            goto do_next; // keep feeding BAR!s
        }

        SET_VOID(f->out);
        SET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);
        break;

    case ET_LIT_BAR:
        SET_BAR(f->out);
        SET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);
        FETCH_NEXT_ONLY_MAYBE_END(f);
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

    case ET_WORD:
        if (f->gotten == NULL) // no work to reuse from failed optimization
            f->gotten = Get_Var_Core(
                &f->eval_type, f->value, f->specifier, GETVAR_READ_ONLY
            );

        // eval_type will be set to either 1 (ET_LOOKBACK) or 0 (ET_FUNCTION)

        if (IS_FUNCTION(f->gotten)) { // before IS_VOID() speeds common case

            SET_FRAME_LABEL(f, VAL_WORD_CASED(f->value));

            if (f->eval_type != ET_LOOKBACK) { // ordinary "prefix" call
                assert(f->eval_type == ET_FUNCTION);
                SET_END(f->out);
                goto do_function_in_gotten;
            }

            if (f->prior)
                Try_Lookback_In_Prior_Frame(f->out, f->prior);
            else
                SET_END(f->out);

            goto do_function_in_gotten;
        }

    do_word_in_value_with_gotten:
        assert(!IS_FUNCTION(f->gotten)); // infix handling needs differ

        if (IS_VOID(f->gotten)) { // need `:x` if `x` is unset
            f->eval_type = ET_WORD; // we overwrote above, but error needs it
            fail (Error_No_Value_Core(f->value, f->specifier));
        }

        *f->out = *f->gotten;
        SET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);
        f->gotten = NULL;
        FETCH_NEXT_ONLY_MAYBE_END(f);

    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_LIT_WORD_DECAY) && IS_LIT_WORD(f->out))
            VAL_SET_TYPE_BITS(f->out, REB_WORD); // don't reset full header!
    #endif
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [SET-WORD!]
//
// Does the evaluation into `out`, then gets the variable indicated by the
// word and writes the result there as well.
//
//==//////////////////////////////////////////////////////////////////////==//

    case ET_SET_WORD:
        //
        // fetch writes f->value, so save SET-WORD! ptr.  Note that the nested
        // evaluation here might peek up at it if it contains an infix
        // function that quotes its first argument, e.g. `x: ++ 10`
        //
        assert(IS_SET_WORD(f->value));
        f->param = f->value;

        FETCH_NEXT_ONLY_MAYBE_END(f);
        if (IS_END(f->value))
            fail (Error(RE_NEED_VALUE, f->param)); // e.g. `do [foo:]`

        if (args_evaluate) {
            //
            // A SET-WORD! handles lookahead like a prefix function would;
            // so it uses lookahead on its arguments regardless of f->flags
            //
            DO_NEXT_REFETCH_MAY_THROW(f->out, f, DO_FLAG_LOOKAHEAD);

            if (THROWN(f->out)) goto finished;

            // leave VALUE_FLAG_EVALUATED as is
        }
        else
            QUOTE_NEXT_REFETCH(f->out, f); // clears VALUE_FLAG_EVALUATED

    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_SET_WORD_VOID_IS_ERROR) && IS_VOID(f->out))
            fail (Error(RE_NEED_VALUE, f->param)); // e.g. `foo: ()`
    #endif

        *GET_MUTABLE_VAR_MAY_FAIL(f->param, f->specifier) = *(f->out);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [GET-WORD!]
//
// A GET-WORD! does no checking for unsets, no dispatch on functions, and
// will return void if the variable is not set.
//
//==//////////////////////////////////////////////////////////////////////==//

    case ET_GET_WORD:
        *f->out = *GET_OPT_VAR_MAY_FAIL(f->value, f->specifier);
        SET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);
        FETCH_NEXT_ONLY_MAYBE_END(f);
        break;

//==/////////////////////////////////////////////////////////////////////==//
//
// [LIT-WORD!]
//
// Note we only want to reset the type bits in the header, not the whole
// header--because header bits contain information like WORD_FLAG_BOUND.
//
//==//////////////////////////////////////////////////////////////////////==//

    case ET_LIT_WORD:
        QUOTE_NEXT_REFETCH(f->out, f); // we're adding VALUE_FLAG_EVALUATED
        VAL_SET_TYPE_BITS(f->out, REB_WORD);
        SET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [GROUP!]
//
// If a GROUP! is seen then it generates another call into Do_Core().  The
// resulting value for this step will be the outcome of that evaluation.
//
//==//////////////////////////////////////////////////////////////////////==//

    case ET_GROUP:
        //
        // If the source array we are processing that is yielding values is
        // part of the deep copy of a function body, it's possible that this
        // GROUP! is a "relative ANY-ARRAY!" that needs the specifier to
        // resolve the relative any-words and other any-arrays inside it...
        //
        if (Do_At_Throws(
            f->out,
            VAL_ARRAY(f->value), // the GROUP!'s array
            VAL_INDEX(f->value), // index in group's REBVAL (may not be head)
            IS_RELATIVE(f->value)
                ? f->specifier // if relative, use parent specifier...
                : VAL_SPECIFIER(const_KNOWN(f->value)) // ...else use child's
        )) {
            goto finished;
        }

        SET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);
        FETCH_NEXT_ONLY_MAYBE_END(f);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [PATH!]
//
//==//////////////////////////////////////////////////////////////////////==//

    case ET_PATH: {
        REBSTR *label;
        if (Do_Path_Throws_Core(
            f->out,
            &label, // requesting label says we run functions (not GET-PATH!)
            f->value,
            f->specifier,
            NULL // `setval`: null means don't treat as SET-PATH!
        )) {
            goto finished;
        }

        if (IS_VOID(f->out)) // need `:x/y` if `y` is unset
            fail (Error_No_Value_Core(f->value, f->specifier));

        if (IS_FUNCTION(f->out)) {
            f->eval_type = ET_FUNCTION; // paths are never ET_LOOKBACK
            SET_FRAME_LABEL(f, label);

            // object/func or func/refinements or object/func/refinement
            //
            // Because we passed in a label symbol, the path evaluator was
            // willing to assume we are going to invoke a function if it
            // is one.  Hence it left any potential refinements on data stack.
            //
            assert(DSP >= f->dsp_orig);

            f->cell.eval = *f->out;
            f->gotten = KNOWN(&f->cell.eval);
            SET_END(f->out);
            goto do_function_in_gotten;
        }

        // Path should have been fully processed, no refinements on stack
        //
        assert(DSP == f->dsp_orig);

        SET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);
        FETCH_NEXT_ONLY_MAYBE_END(f);
        break;
    }

//==//////////////////////////////////////////////////////////////////////==//
//
// [SET-PATH!]
//
//==//////////////////////////////////////////////////////////////////////==//

    case ET_SET_PATH:
        //
        // fetch writes f->value, so save SET-PATH! ptr.  Note that the nested
        // evaluation here might peek up at it if it contains an infix
        // function that quotes its first argument, e.g. `x/y: ++ 10`
        //
        f->param = f->value;

        // f->out is held between a DO_NEXT and a Do_Path and expected to
        // stay valid.  The GC must therefore protect the f->out slot, so
        // it can't contain garbage.  (Similar issue with ET_FUNCTION)
        //
        SET_END(f->out);

        FETCH_NEXT_ONLY_MAYBE_END(f);

        // `do [a/b/c:]` is not legal
        //
        if (IS_END(f->value))
            fail (Error(RE_NEED_VALUE, f->param));

        // We want the result of the set path to wind up in `out`, so go
        // ahead and put the result of the evaluation there.  Do_Path_Throws
        // will *not* put this value in the output when it is making the
        // variable assignment!
        //
        if (args_evaluate) {
            //
            // A SET-PATH! handles lookahead like a prefix function would;
            // so it uses lookahead on its arguments regardless of f->flags
            //
            DO_NEXT_REFETCH_MAY_THROW(f->out, f, DO_FLAG_LOOKAHEAD);

            if (THROWN(f->out)) goto finished;
        }
        else {
            COPY_VALUE(f->out, f->value, f->specifier);
            FETCH_NEXT_ONLY_MAYBE_END(f);
        }

    #if !defined(NDEBUG)
        if (LEGACY(OPTIONS_SET_WORD_VOID_IS_ERROR) && IS_VOID(f->out))
            fail (Error(RE_NEED_VALUE, f->param)); // e.g. `a/b/c: ()`
    #endif

        // !!! The evaluation ordering of SET-PATH! evaluation seems to break
        // the "left-to-right" nature of the language:
        //
        //     >> foo: make object! [[bar][bar: 10]]
        //
        //     >> foo/(print "left" 'bar): (print "right" 20)
        //     right
        //     left
        //     == 20
        //
        // In addition to seeming "wrong" it also necessitates an extra cell
        // of storage.  This should be reviewed along with Do_Path generally.
        {
            REBVAL temp;
            if (Do_Path_Throws_Core(
                &temp, // output location
                NULL, // not requesting symbol means refinements not allowed
                f->param, // param is currently holding SET-PATH! we got in
                f->specifier, // needed to resolve relative array in path
                f->out // `setval`: non-NULL means do assignment as SET-PATH!
            )) {
                *(f->out) = temp;
                goto finished;
            }

            // leave VALUE_FLAG_EVALUATED as is
        }

        // We did not pass in a symbol, so not a call... hence we cannot
        // process refinements.  Should not get any back.
        //
        assert(DSP == f->dsp_orig);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [GET-PATH!]
//
//==//////////////////////////////////////////////////////////////////////==//

    case ET_GET_PATH:
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
            f->value,
            f->specifier,
            NULL // `setval`: null means don't treat as SET-PATH!
        )) {
            goto finished;
        }

        // We did not pass in a symbol ID
        //
        assert(DSP == f->dsp_orig);
        SET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);
        FETCH_NEXT_ONLY_MAYBE_END(f);
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

    case ET_LIT_PATH:
        QUOTE_NEXT_REFETCH(f->out, f);
        VAL_SET_TYPE_BITS(f->out, REB_PATH);
        SET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [FUNCTION!]
//
// If a function makes it to the SWITCH statement, that means it is either
// literally a function value in the array (`do compose [(:+) 1 2]`) or is
// being retriggered via EVAL.  Note that infix functions that are
// encountered in this way will behave as prefix--their infix behavior
// is only triggered when they are looked up from a word.  See #1934.
//
// Most function evaluations are triggered from a SWITCH on a WORD! or PATH!,
// which jumps in at the `do_function_in_gotten` label.
//
//==//////////////////////////////////////////////////////////////////////==//

    case ET_LOOKBACK:
        SET_FRAME_LABEL(f, VAL_WORD_CASED(f->value)); // failed WORD! optimize
        assert(NOT_END(f->out)); // must be the infix's left-hand-side arg
        goto do_function_in_gotten;

    case ET_FUNCTION:
        if (f->gotten == NULL) { // literal function in a block
            f->gotten = const_KNOWN(f->value);
            SET_FRAME_LABEL(f, Canon(SYM___ANONYMOUS__)); // nameless literal
        }
        else
            SET_FRAME_LABEL(f, VAL_WORD_CASED(f->value)); // failed optimization

        SET_END(f->out); // needs GC-safe data

    do_function_in_gotten:
        assert(IS_FUNCTION(f->gotten));

        assert(f->label != NULL); // must be something (even "anonymous")
    #if !defined(NDEBUG)
        assert(f->label_debug != NULL); // SET_FRAME_LABEL sets (C debugging)
    #endif

        // There may be refinements pushed to the data stack to process, if
        // the call originated from a path dispatch.
        //
        assert(DSP >= f->dsp_orig);

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! EVAL HANDLING
    //
    //==////////////////////////////////////////////////////////////////==//

        // The EVAL "native" is unique because it cannot be a function that
        // runs "under the evaluator"...because it *is the evaluator itself*.
        // Hence it is handled in a special way.
        //
        if (VAL_FUNC(f->gotten) == NAT_FUNC(eval)) {
            f->gotten = NULL;
            FETCH_NEXT_ONLY_MAYBE_END(f);

            // The garbage collector expects f->func to be valid during an
            // argument fulfillment, and f->param needs to be a typeset in
            // order to cue Is_Function_Frame_Fulfilling().
            //
            f->func = NAT_FUNC(eval);
            f->param = FUNC_PARAM(NAT_FUNC(eval), 1);

            // "DO/NEXT" full expression into the `eval` REBVAR slot
            // (updates index...).  (There is an /ONLY switch to suppress
            // normal evaluation but it does not apply to the value being
            // retriggered itself, just any arguments it consumes.)
            //
            if (f->eval_type == ET_LOOKBACK) {
                if (IS_END(f->out))
                    fail (Error_No_Arg(FRM_LABEL(f), f->param));

                f->cell.eval = *f->out;
                f->eval_type = ET_FUNCTION;
                SET_END(f->out);
            }
            else {
                if (IS_END(f->value)) // e.g. `do [eval]`
                    fail (Error_No_Arg(FRM_LABEL(f), f->param));

                DO_NEXT_REFETCH_MAY_THROW(
                    SINK(&f->cell.eval), f, DO_FLAG_LOOKAHEAD
                );

                if (THROWN(&f->cell.eval)) goto finished;
            }

            // There's only one refinement to EVAL and that is /ONLY.  It can
            // push one refinement to the stack or none.  The state will
            // twist up the evaluator for the next evaluation only.
            //
            if (DSP > f->dsp_orig) {
                assert(DSP == f->dsp_orig + 1);
                assert(VAL_WORD_SYM(DS_TOP) == SYM_ONLY);
                DS_DROP;
                args_evaluate = FALSE;
            }
            else
                args_evaluate = TRUE;

            CLEAR_FRAME_LABEL(f);

            // Jumping to the `reevaluate:` label will skip the fetch from the
            // array to get the next `value`.  So seed it with the address of
            // eval result, and step the index back by one so the next
            // increment will get our position sync'd in the block.
            //
            // If there's any reason to be concerned about the temporary
            // item being GC'd, it should be taken care of by the implicit
            // protection from the Do Stack.  (e.g. if it contains a function
            // that gets evaluated it will wind up in f->func, if it's a
            // GROUP! or PATH!-containing-GROUP! it winds up in f->array...)
            //
            f->pending = f->value; // may be END marker for next fetch

            // Since the evaluation result is a REBVAL and not a RELVAL, it
            // is specific.  This means the `f->specifier` (which can only
            // specify values from the source array) won't ever be applied
            // to it, since it only comes into play for IS_RELATIVE values.
            //
            SET_FRAME_VALUE(f, const_KNOWN(&f->cell.eval));
            f->eval_type = Eval_Table[VAL_TYPE(f->value)];

            // The f->gotten (if any) is the fetch for the f->value we just
            // put in pending...not the f->value we just set.  Not only is
            // it more expensive to hold onto that cache than to lose it,
            // but an eval can do anything...so the f->gotten might wind
            // up being completely different after the eval.  So forget it.
            //
            f->gotten = NULL;

            goto reevaluate; // we don't move index!
        }

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! NORMAL ARGUMENT FULFILLMENT PROCESS
    //
    //==////////////////////////////////////////////////////////////////==//

        // We assume you can enumerate both the formal parameters (in the
        // spec) and the actual arguments (in the call frame) using pointer
        // incrementation, that they are both terminated by END, and
        // that there are an equal number of values in both.

        Push_Or_Alloc_Args_For_Underlying_Func(f); // sets f's func, param, arg

        f->gotten = NULL;
        FETCH_NEXT_ONLY_MAYBE_END(f); // overwrites f->value

    do_function_arglist_in_progress:

        // Now that we have extracted f->func, we do not have to worry that
        // f->value might have lived in f->cell.eval.  We can't overwrite
        // f->out in case that is holding the first argument to an infix
        // function, so f->cell.eval gets used for temporary evaluations.

        assert(f->eval_type == ET_FUNCTION || f->eval_type == ET_LOOKBACK);

        // The f->out slot is guarded while a function is gathering its
        // arguments.  It cannot contain garbage, so it must either be END
        // or a lookback's first argument (which can also be END).
        //
        assert(IS_END(f->out) || f->eval_type == ET_LOOKBACK);

        // If a function doesn't want to act as an argument to a function
        // call or an assignment (e.g. `x: print "don't do this"`) we can
        // stop it by looking at the frame above.  Note that if a function
        // frame is running but not fulfilling arguments, that just means
        // that this is being used in the implementation.
        //
        // Must be positioned here to apply to infix, and also so that the
        // f->param field is initialized (checked by error machinery)
        //
        if (GET_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_PUNCTUATES) && f->prior)
            switch (f->prior->eval_type) {
            case ET_FUNCTION:
            case ET_LOOKBACK:
                if (Is_Function_Frame_Fulfilling(f->prior))
                    fail (Error_Punctuator_Hit(f));
                break;
            case ET_SET_PATH:
            case ET_SET_WORD:
                fail (Error_Punctuator_Hit(f));
            }

        // `10 = add 5 5` is `true`
        // `add 5 5 = 10` is `** Script error: expected logic! not integer!`
        //
        // `5 + 5 = 10` is `true`
        // `10 = 5 + 5` is `** Script error: expected logic! not integer!`
        //
        // We may consume the `lookahead` parameter, but if we *were* looking
        // ahead then it suppresses lookahead on all evaluated arguments.
        // Need a separate variable to track it.
        //
        REBUPT lookahead_flags; // `goto finished` would cross if initialized
        lookahead_flags =
            (f->eval_type == ET_LOOKBACK)
                ? DO_FLAG_NO_LOOKAHEAD
                : DO_FLAG_LOOKAHEAD;

        // "not a refinement arg, evaluate normally", won't be modified
        f->refine = m_cast(REBVAL*, BAR_VALUE);

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! NORMAL ARGUMENT FULFILLMENT LOOP
    //
    //==////////////////////////////////////////////////////////////////==//

        // This loop goes through the parameter and argument slots.  Based on
        // the parameter type, it may be necessary to "consume" an expression
        // from values that come after the invokation point.  But not all
        // params will consume arguments for all calls.  See notes below.
        //
        // For this one body of code to be able to handle both function
        // specialization and ordinary invocation, the void type is used as
        // a signal to have "unspecialized" behavior.  Hence a normal call
        // just pre-fills all the args with void--which will be overwritten
        // during the argument fulfillment process (unless they turn out to
        // be optional in the invocation).
        //
        //
        // To get around that, there's a trick.  An out-of-order refinement
        // makes a note in the stack about a parameter and arg position that
        // it sees that it will need to come back to.  It pokes those two
        // pointers into extra space in the refinement's word on the stack,
        // since that word isn't using its binding.  See WORD_FLAG_PICKUP for
        // the type of WORD! that is used to implement this.

        enum Reb_Param_Class pclass; // gotos would cross it if inside loop

        REBOOL doing_pickups; // case label would cross it if initialized
        doing_pickups = FALSE;

        for (; NOT_END(f->param); ++f->param, ++f->arg) {
            pclass = VAL_PARAM_CLASS(f->param);

    //=//// A /REFINEMENT ARG /////////////////////////////////////////////=//

            // Refinements are checked for first for a reason.  This is to
            // short-circuit based on the `doing_pickups` flag before redoing
            // fulfillments on arguments that have already been handled.
            //
            // The reason an argument might have already been handled is
            // because refinements have to reach back and be revisited after
            // the original parameter walk.  They can't be fulfilled in a
            // single pass because these two calls mean different things:
            //
            //     foo: func [a /b c /d e] [...]
            //
            //     foo/b/d (1 + 2) (3 + 4) (5 + 6)
            //     foo/d/b (1 + 2) (3 + 4) (5 + 6)
            //
            // The order of refinements in the definition (b d) might not match
            // what order the refinements are invoked in the path.  This means
            // the "visitation order" of the parameters while walking across
            // parameters in the array might not match the "consumption order"
            // of the expressions that are being fetched from the callsite.
            // Hence refinements are targeted to be revisited by "pickups"
            // after the initial parameter walk.

            if (pclass == PARAM_CLASS_REFINEMENT) {

                if (doing_pickups) {
                    f->param = END_CELL; // !Is_Function_Frame_Fulfilling
                    break;
                }

                if (NOT(Specialized_Arg(f->arg))) {

    //=//// UNSPECIALIZED REFINEMENT SLOT (no consumption) ////////////////=//

                    if (f->dsp_orig == DSP) { // no refinements left on stack
                        SET_FALSE(f->arg);
                        f->refine = BLANK_VALUE; // "don't consume args, ever"
                        goto continue_arg_loop;
                    }

                    f->refine = DS_TOP;

                    if (
                        IS_WORD(f->refine) && // canonized when pushed, #2258
                        VAL_WORD_CASED(f->refine) == VAL_PARAM_CANON(f->param)
                    ) {
                        DS_DROP; // we're lucky: this was next refinement used

                        SET_TRUE(f->arg); // marks refinement used
                        f->refine = f->arg; // "consume args (can be revoked)"
                        goto continue_arg_loop;
                    }

                    --f->refine; // not lucky: if in use, this is out of order

                    for (; f->refine > DS_AT(f->dsp_orig); --f->refine) {
                        if (IS_VARARGS(f->refine)) continue; // a pickup
                        if (
                            VAL_WORD_CASED(f->refine) // canonized when pushed
                            == VAL_PARAM_CANON(f->param) // #2258
                        ) {
                            // The call uses this refinement but we'll have to
                            // come back to it when the expression index to
                            // consume lines up.  Make a note of the param
                            // and arg and poke them into the stack value.
                            //
                            VAL_RESET_HEADER(f->refine, REB_VARARGS);
                            f->refine->payload.varargs.param
                                = const_KNOWN(f->param);
                            f->refine->payload.varargs.arg = f->arg;

                            SET_TRUE(f->arg); // marks refinement used
                            // "consume args later" (promise not to change)
                            f->refine = m_cast(REBVAL*, VOID_CELL);
                            goto continue_arg_loop;
                        }
                    }

                    // Wasn't in the path and not specialized, so not present
                    //
                    SET_FALSE(f->arg);
                    f->refine = BLANK_VALUE; // "don't consume args, ever"
                    goto continue_arg_loop;
                }

    //=//// SPECIALIZED REFINEMENT SLOT (no consumption) //////////////////=//

                if (args_evaluate && IS_QUOTABLY_SOFT(f->arg)) {
                    //
                    // Needed for `(copy [1 2 3])`, active specializations

                    if (EVAL_VALUE_THROWS(SINK(&f->cell.eval), f->arg)) {
                        *f->out = *KNOWN(&f->cell.eval);
                        Abort_Function_Args_For_Frame(f);
                        goto finished;
                    }

                    *f->arg = *KNOWN(&f->cell.eval);
                }

                if (IS_VOID(f->arg)) {
                    SET_FALSE(f->arg);
                    f->refine = BLANK_VALUE; // handled same as false
                    goto continue_arg_loop;
                }

                if (!IS_LOGIC(f->arg))
                    fail (Error_Non_Logic_Refinement(f));

                if (IS_CONDITIONAL_TRUE(f->arg))
                    f->refine = f->arg; // remember so we can revoke!
                else
                    f->refine = BLANK_VALUE; // (read-only)

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
                goto continue_arg_loop;

            case PARAM_CLASS_RETURN:
                assert(VAL_PARAM_SYM(f->param) == SYM_RETURN);

                if (!GET_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_RETURN)) {
                    SET_VOID(f->arg);
                    goto continue_arg_loop;
                }

                *f->arg = *NAT_VALUE(return);

                if (f->varlist) // !!! in specific binding, always for Plain
                    f->arg->extra.binding = f->varlist;
                else
                    f->arg->extra.binding = FUNC_PARAMLIST(f->func);
                goto continue_arg_loop;

            case PARAM_CLASS_LEAVE:
                assert(VAL_PARAM_SYM(f->param) == SYM_LEAVE);

                if (!GET_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_LEAVE)) {
                    SET_VOID(f->arg);
                    goto continue_arg_loop;
                }

                *f->arg = *NAT_VALUE(leave);

                if (f->varlist) // !!! in specific binding, always for Plain
                    f->arg->extra.binding = f->varlist;
                else
                    f->arg->extra.binding = FUNC_PARAMLIST(f->func);
                goto continue_arg_loop;
            }

    //=//// IF COMING BACK TO REFINEMENT ARGS LATER, MOVE ON FOR NOW //////=//

            if (IS_VOID(f->refine)) goto continue_arg_loop;

    //=//// SPECIALIZED ARG (already filled, so does not consume) /////////=//

            if (Specialized_Arg(f->arg)) {

                // The arg came preloaded with a value to use.  Handle soft
                // quoting first, in case arg needs evaluation.

                if (args_evaluate && IS_QUOTABLY_SOFT(f->arg)) {

                    if (EVAL_VALUE_THROWS(SINK(&f->cell.eval), f->arg)) {
                        *f->out = *KNOWN(&f->cell.eval);
                        Abort_Function_Args_For_Frame(f);
                        goto finished;
                    }

                    *f->arg = *KNOWN(&f->cell.eval);
                }

                // Varargs are special, because the type checking doesn't
                // actually check the type of the parameter--it's always
                // a VARARGS!.  Also since the "types accepted" are a lie
                // (an [integer! <...>] takes VARARGS!, not INTEGER!) then
                // an "honest" parameter has to be made to give the error.
                //
                if (
                    IS_CONDITIONAL_TRUE(f->refine) // not unused or revoking
                    && GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)
                ) {
                    if (!IS_VARARGS(f->arg)) {
                        REBVAL honest_param;
                        Val_Init_Typeset(
                            &honest_param,
                            FLAGIT_KIND(REB_VARARGS), // *actually* expected...
                            VAL_PARAM_CASED(f->param)
                        );

                        fail (Error_Arg_Type(
                            FRM_LABEL(f), &honest_param, VAL_TYPE(f->arg))
                        );
                    }

                    // !!! Passing the varargs through directly does not
                    // preserve the type checking or symbol.  This suggests
                    // that even array-based varargs frames should have
                    // an optional frame and parameter.  Consider specializing
                    // variadics to be TBD until the type checking issue
                    // is sorted out.
                    //
                    assert(FALSE);

                    goto continue_arg_loop;
                }

                goto check_arg; // normal checking, handles errors also
            }

    //=//// IF UNSPECIALIZED ARG IS INACTIVE, SET VOID AND MOVE ON ////////=//

            // Unspecialized arguments that do not consume do not need any
            // further processing or checking.  void will always be fine.
            //
            if (IS_BLANK(f->refine)) { // FALSE if revoked, and still evaluates
                assert(NOT(Specialized_Arg(f->arg)));
                SET_VOID(f->arg);
                goto continue_arg_loop;
            }

    //=//// VARIADIC ARG (doesn't consume anything *yet*) /////////////////=//

            // Evaluation argument "hook" parameters (marked in MAKE FUNCTION!
            // by a `[[]]` in the spec, and in FUNC by `<...>`).  They point
            // back to this call through a reified FRAME!, and are able to
            // consume additional arguments during the function run.
            //
            if (GET_VAL_FLAG(f->param, TYPESET_FLAG_VARIADIC)) {
                //
                // !!! Can EVAL/ONLY be supported by variadics?  What would
                // it mean?  It generally means that argument fulfillment will
                // ignore the quoting settings, if that's all it is then
                // the varargs needs to have this flag communicated...but
                // then should it function variadically anyway?
                //
                assert(args_evaluate);

                VAL_RESET_HEADER(f->arg, REB_VARARGS);

                // Note that this varlist is to a context that is not ready
                // to be shared with the GC yet (bad cells in any unfilled
                // arg slots).  To help cue that it's not necessarily a
                // completed context yet, we store it as an array type.
                //
                Context_For_Frame_May_Reify_Core(f);
                f->arg->extra.binding = f->varlist;

                f->arg->payload.varargs.param = const_KNOWN(f->param); // check
                f->arg->payload.varargs.arg = f->arg; // linkback, might change
                goto continue_arg_loop;
            }

    //=//// AFTER THIS, PARAMS CONSUME FROM CALLSITE IF NOT APPLY ////////=//

            assert(NOT(Specialized_Arg(f->arg)));

    //=//// ERROR ON END MARKER, BAR! IF APPLICABLE //////////////////////=//

            if (IS_END(f->value)) {
                if (!GET_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                    fail (Error_No_Arg(FRM_LABEL(f), f->param));

                SET_VOID(f->arg);
                goto continue_arg_loop;
            }

            // Literal expression barriers cannot be consumed in normal
            // evaluation, even if the argument takes a BAR!.  It must come
            // through non-literal means(e.g. `quote '|` or `first [|]`)
            //
            if (args_evaluate && IS_BAR(f->value)) {
                if (!GET_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                    fail (Error(RE_EXPRESSION_BARRIER));

                SET_VOID(f->arg);
                goto continue_arg_loop;
            }

    //=//// REGULAR ARG-OR-REFINEMENT-ARG (consumes a DO/NEXT's worth) ////=//

            if (pclass == PARAM_CLASS_NORMAL) {
                if (f->eval_type == ET_LOOKBACK) {
                    f->eval_type = ET_FUNCTION;

                    if (IS_END(f->out)) {
                        if (!GET_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                            fail (Error_No_Arg(FRM_LABEL(f), f->param));

                        SET_VOID(f->out);
                        goto continue_arg_loop;
                    }

                    *f->arg = *f->out;
                    SET_END(f->out);
                }
                else if (args_evaluate) {
                    DO_NEXT_REFETCH_MAY_THROW(f->arg, f, lookahead_flags);

                    if (THROWN(f->arg)) {
                        *f->out = *f->arg;
                        Abort_Function_Args_For_Frame(f);
                        goto finished;
                    }
                }
                else
                    QUOTE_NEXT_REFETCH(f->arg, f); // no VALUE_FLAG_EVALUATED

                goto check_arg;
            }

    //=//// HARD QUOTED ARG-OR-REFINEMENT-ARG /////////////////////////////=//

            if (pclass == PARAM_CLASS_HARD_QUOTE) {
                if (f->eval_type == ET_LOOKBACK) {
                    f->eval_type = ET_FUNCTION;

                    if (IS_END(f->out)) {
                        if (!GET_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                            fail (Error_No_Arg(FRM_LABEL(f), f->param));

                        SET_VOID(f->out);
                        goto continue_arg_loop;
                    }

                    if (GET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED))
                        fail (Error_Lookback_Quote_Too_Late(f));

                    *f->arg = *f->out;
                    SET_END(f->out);
                }
                else
                    QUOTE_NEXT_REFETCH(f->arg, f); // non-VALUE_FLAG_EVALUATED

                goto check_arg;
            }

    //=//// SOFT QUOTED ARG-OR-REFINEMENT-ARG  ////////////////////////////=//

            assert(pclass == PARAM_CLASS_SOFT_QUOTE);

            if (f->eval_type == ET_LOOKBACK) {
                f->eval_type = ET_LOOKBACK;

                if (IS_END(f->out)) {
                    if (!GET_VAL_FLAG(f->param, TYPESET_FLAG_ENDABLE))
                        fail (Error_No_Arg(FRM_LABEL(f), f->param));

                    SET_VOID(f->out);
                    goto continue_arg_loop;
                }

                if (GET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED))
                    fail (Error_Lookback_Quote_Too_Late(f));

                if (IS_SET_WORD(f->out) || IS_SET_PATH(f->out))
                    fail (Error_Lookback_Quote_Set_Soft(f));

                *f->arg = *f->out;
                SET_END(f->out);
            }
            else if (args_evaluate && IS_QUOTABLY_SOFT(f->value)) {
                if (EVAL_VALUE_CORE_THROWS(f->arg, f->value, f->specifier)) {
                    *f->out = *f->arg;
                    Abort_Function_Args_For_Frame(f);
                    goto finished;
                }
                FETCH_NEXT_ONLY_MAYBE_END(f);
            }
            else
                QUOTE_NEXT_REFETCH(f->arg, f); // non-VALUE_FLAG_EVALUATED

    //=//// TYPE CHECKING FOR (MOST) ARGS AT END OF ARG LOOP //////////////=//

        check_arg:

            // Some arguments can be fulfilled and skip type checking or
            // take care of it themselves.  But normal args pass through
            // this code which checks the typeset and also handles it when
            // a void arg signals the revocation of a refinement usage.

            ASSERT_VALUE_MANAGED(f->arg);
            assert(pclass != PARAM_CLASS_REFINEMENT);
            assert(pclass != PARAM_CLASS_LOCAL);

            // See notes on `Reb_Frame.refine` in %sys-do.h for more info.
            //
            assert(
                IS_BLANK(f->refine) || // f->arg is arg to never-used refinment
                IS_LOGIC(f->refine) || // F = revoked, T = used refinement slot
                IS_BAR(f->refine) // f->arg is ordinary function argument
            );

            if (IS_VOID(f->arg)) {
                if (IS_BAR(f->refine)) {
                    //
                    // fall through to check ordinary arg for if <opt> is ok
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
                    assert(IS_LOGIC(f->refine));

                    // We can only revoke the refinement if this is the 1st
                    // refinement arg.  If it's a later arg, then the first
                    // didn't trigger revocation, or refine wouldn't be WORD!
                    //
                    if (f->refine + 1 != f->arg)
                        fail (Error_Bad_Refine_Revoke(f));

                    SET_FALSE(f->refine);
                    // won't be modified
                    f->refine = m_cast(REBVAL*, FALSE_VALUE);
                    goto continue_arg_loop; // don't type check for optionality
                }
            }
            else {
                // If the argument is set, then the refinement shouldn't be
                // in a revoked or unused state.
                //
                if (IS_CONDITIONAL_FALSE(f->refine))
                    fail (Error_Bad_Refine_Revoke(f));
            }

            Type_Check_Arg_For_Param_May_Fail(f);

        continue_arg_loop: // `continue` might bind to the wrong scope
            NOOP;
        }

        // There may have been refinements that were skipped because the
        // order of definition did not match the order of usage.  They were
        // left on the stack with a pointer to the `param` and `arg` after
        // them for later fulfillment.
        //
        if (DSP != f->dsp_orig) {
            if (!IS_VARARGS(DS_TOP)) {
                //
                // The walk through the arguments didn't fill in any
                // information for this word, so it was either a duplicate of
                // one that was fulfilled or not a refinement the function
                // has at all.
                //
                assert(IS_WORD(DS_TOP));
                fail (Error(RE_BAD_REFINE, DS_TOP));
            }
            f->param = DS_TOP->payload.varargs.param;
            f->refine = f->arg = DS_TOP->payload.varargs.arg;
            assert(IS_LOGIC(f->refine) && VAL_LOGIC(f->refine));
            DS_DROP;
            doing_pickups = TRUE;
            goto continue_arg_loop; // leaves refine, but bumps param+arg
        }

    #if !defined(NDEBUG)
        if (GET_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_LEGACY))
            Legacy_Convert_Function_Args(f); // BLANK!+NONE! vs. FALSE+UNSET!
    #endif

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! ARGUMENTS NOW GATHERED, DISPATCH CALL
    //
    //==////////////////////////////////////////////////////////////////==//

        assert(DSP == f->dsp_orig);

        // Now we reset arg to the head of the argument list.  This provides
        // fast access for the callees, so they don't have to go through an
        // indirection further than just f->arg to get it.
        //
        // !!! When hybrid frames are introduced, review the question of
        // which pointer "wins".  Might more than one be used?
        //
        if (f->varlist) {
            //
            // Technically speaking we would only be *required* at this point
            // to manage the varlist array if we've poked it into a vararg
            // as a context.  But specific binding will always require a
            // context available, so no point in optimizing here.
            //
            Context_For_Frame_May_Reify_Managed(f);

            f->arg = CTX_VARS_HEAD(AS_CONTEXT(f->varlist));
        }
        else {
            // We cache the stackvars data pointer in the stack allocated
            // case.  Note that even if the frame becomes "reified" as a
            // context, the data pointer will be the same over the stack
            // level lifetime.
            //
            f->arg = &f->stackvars[0];
            assert(CHUNK_FROM_VALUES(f->arg) == TG_Top_Chunk);
        }

        // The garbage collector may run when we call out to functions, so
        // we have to be sure that the frame fields are something valid.
        // f->param cannot be a typeset while the function is running, because
        // typesets are used as a signal to Is_Function_Frame_Fulfilling.
        //
        f->cell.subfeed = NULL;

    execute_func:
        assert(IS_END(f->param));
        // refine can be anything.
        assert(
            IS_END(f->value)
            || (f->flags & DO_FLAG_VA_LIST)
            || IS_VALUE_IN_ARRAY(f->source.array, f->value)
        );

        if (Trace_Flags) Trace_Func(FRM_LABEL(f), FUNC_VALUE(f->func));

        // The out slot needs initialization for GC safety during the function
        // run.  Choosing an END marker should be legal because places that
        // you can use as output targets can't be visible to the GC (that
        // includes argument arrays being fulfilled).  This offers extra
        // perks, because it means a recycle/torture will catch you if you
        // try to Do_Core into movable memory...*and* a native can tell if it
        // has written the output slot yet or not (e.g. WHILE's /? refinement).
        //
        assert(IS_END(f->out));

        // Any of the below may return f->out as THROWN().  (Note: this used
        // to do `Eval_Natives++` in the native dispatcher, which now fades
        // into the background.)  The dispatcher may also push functions to
        // the data stack which will be used to process the return result.
        //
        REBNAT dispatcher; // goto would cross initialization
        dispatcher = FUNC_DISPATCHER(f->func);
        switch (dispatcher(f)) {
        case R_OUT: // put sequentially in switch() for jump-table optimization
            break;

        case R_OUT_IS_THROWN:
            assert(THROWN(f->out));
            break;

        case R_OUT_TRUE_IF_WRITTEN:
            if (IS_END(f->out))
                SET_FALSE(f->out);
            else
                SET_TRUE(f->out);
            break;

        case R_OUT_VOID_IF_UNWRITTEN:
            if (IS_END(f->out))
                SET_VOID(f->out);
            break;

        case R_BLANK:
            SET_BLANK(f->out);
            break;

        case R_VOID:
            SET_VOID(f->out);
            break;

        case R_TRUE:
            SET_TRUE(f->out);
            break;

        case R_FALSE:
            SET_FALSE(f->out);
            break;

        case R_REDO:
            //
            // This instruction represents the idea that it is desired to
            // run the f->func again.  The dispatcher may have changed the
            // value of what f->func is, for instance.
            //
            SET_END(f->out);
            goto execute_func;

        default:
            assert(FALSE);
        }

        assert(f->eval_type == ET_FUNCTION); // shouldn't have changed
        assert(NOT_END(f->out)); // should have overwritten

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! CATCHING OF EXITs (includes catching RETURN + LEAVE)
    //
    //==////////////////////////////////////////////////////////////////==//

        if (THROWN(f->out)) {
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
        }

    //==////////////////////////////////////////////////////////////////==//
    //
    // FUNCTION! CALL COMPLETION (Type Check Result, Throw If Needed)
    //
    //==////////////////////////////////////////////////////////////////==//

        Drop_Function_Args_For_Frame(f);

        // Here we know the function finished and did not throw or exit.  If
        // it has a definitional return we need to type check it--and if it
        // has punctuates we have to squash whatever the last evaluative
        // result was and return no value

        if (GET_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_PUNCTUATES)) {
            SET_VOID(f->out);
        }
        else if (GET_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_RETURN)) {
            f->param = FUNC_PARAM(f->func, FUNC_NUM_PARAMS(f->func));
            assert(VAL_PARAM_SYM(f->param) == SYM_RETURN);

            // The type bits of the definitional return are not applicable
            // to the `return` word being associated with a FUNCTION!
            // vs. an INTEGER! (for instance).  It is where the type
            // information for the non-existent return function specific
            // to this call is hidden.
            //
            if (!TYPE_CHECK(f->param, VAL_TYPE(f->out)))
                fail (Error_Arg_Type(
                    VAL_PARAM_CASED(f->param), f->param, VAL_TYPE(f->out))
                );
        }

        // Calling a function counts as an evaluation *unless* it's quote or
        // semiquote (the generic means for fooling the semiquote? test)
        //
        if (f->func == NAT_FUNC(semiquote) || f->func == NAT_FUNC(quote))
            CLEAR_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);
        else
            SET_VAL_FLAG(f->out, VALUE_FLAG_EVALUATED);

        // If we have functions pending to run on the outputs, then do so.
        //
        while (DSP != f->dsp_orig) {
            assert(IS_FUNCTION(DS_TOP));

            f->eval_type = ET_INERT; // function is over, so don't involve GC

            REBVAL temp = *f->out; // better safe than sorry, for now?
            if (Apply_Only_Throws(
                f->out, DS_TOP, &temp, END_CELL
            )) {
                goto finished;
            }

            DS_DROP;
        }

        assert(DSP == f->dsp_orig);

        if (Trace_Flags)
            Trace_Return(FRM_LABEL(f), f->out);

        CLEAR_FRAME_LABEL(f);
        break;

//==//////////////////////////////////////////////////////////////////////==//
//
// [ ??? ] => panic
//
// All types must match a case in the switch.  This shouldn't happen.
//
//==//////////////////////////////////////////////////////////////////////==//

    default:
        panic (Error(RE_MISC));
    }

    //==////////////////////////////////////////////////////////////////==//
    //
    // END MAIN SWITCH STATEMENT
    //
    //==////////////////////////////////////////////////////////////////==//

    assert(!THROWN(f->out)); // should have jumped to exit sooner

    if (IS_END(f->value))
        goto finished;

    assert(!IS_END(f->value));

    REBUPT eval_type_last;
    eval_type_last = f->eval_type;

    f->eval_type = Eval_Table[VAL_TYPE(f->value)];

    if (f->flags & DO_FLAG_NO_LOOKAHEAD) {
        //
        // Don't do infix lookahead if asked *not* to look.  It's not typical
        // to be requested by callers (there is already no infix lookahead
        // by using DO_FLAG_EVAL_ONLY, so those cases don't need to ask.)
        //
        // However, recursive cases of DO disable infix dispatch if they are
        // currently processing an infix operation.  The currently processing
        // operation is thus given "higher precedence" by this disablement.
    }
    else if (f->eval_type == ET_WORD) {

        // Don't overwrite f->value (if this just a DO/NEXT and it's not
        // infix, we might need to hold it at its position.)
        //
        f->gotten = Get_Var_Core(
            &f->eval_type, // gets set to ET_LOOKBACK (or ET_FUNCTION if not)
            f->value,
            f->specifier,
            GETVAR_READ_ONLY
        );

    //=//// DO/NEXT WON'T RUN MORE CODE UNLESS IT'S AN INFIX FUNCTION /////=//

        if (f->eval_type != ET_LOOKBACK && NOT(f->flags & DO_FLAG_TO_END)) {
            f->eval_type = ET_WORD; // restore the ET_WORD, needs to be right
            goto finished;
        }

    //=//// IT'S INFIX OR WE'RE DOING TO THE END...DISPATCH LIKE WORD /////=//

        START_NEW_EXPRESSION(f);

        if (!IS_FUNCTION(f->gotten)) // <-- DO_COUNT_BREAKPOINT landing spot
            goto do_word_in_value_with_gotten;

        SET_FRAME_LABEL(f, VAL_WORD_CASED(f->value));

        // If a previous "infix" call had 0 arguments and didn't consume
        // the value before it, assume that means it's a 0-arg barrier
        // that does not want to be the left hand side of another infix.
        //
        if (f->eval_type == ET_LOOKBACK) {
            if (eval_type_last == ET_LOOKBACK)
                fail (Error_Infix_Left_Arg_Prohibited(f));
        }
        else
            SET_END(f->out);

        goto do_function_in_gotten;
    }

    // Continue evaluating rest of block if not just a DO/NEXT
    //
    if (f->flags & DO_FLAG_TO_END)
        goto do_next;

finished:

#if !defined(NDEBUG)
    Do_Core_Exit_Checks_Debug(f); // will get called unless a fail() longjmps
#endif

    // Restore the top of stack (if there is a fail() and associated longjmp,
    // this restoration will be done by the Drop_Trap helper.)
    //
    DROP_CALL(f);

    // All callers must inspect for THROWN(f->out), and most should also
    // inspect for IS_END(f->value)
}
