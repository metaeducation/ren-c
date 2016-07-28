//
//  File: %d-eval.c
//  Summary: "Debug-Build Checks for the Evaluator"
//  Section: debug
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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
// Due to the length of Do_Core() and how many debug checks it already has,
// three debug-only routines are separated out:
//
// * Do_Core_Entry_Checks_Debug() runs once at the beginning of a Do_Core()
//   call.  It verifies that the fields of the frame the caller has to
//   provide have been pre-filled correctly, and snapshots bits of the
//   interpreter state that are supposed to "balance back to zero" by the
//   end of a run (assuming it completes, and doesn't longjmp from fail()ing)
//
// * Do_Core_Expression_Checks_Debug() runs before each full "expression"
//   is evaluated, e.g. before each DO/NEXT step.  It makes sure the state
//   balanced completely--so no DS_PUSH that wasn't balanced by a DS_POP
//   or DS_DROP (for example).  It also trashes variables in the frame which
//   might accidentally carry over from one step to another, so that there
//   will be a crash instead of a casual reuse.
//
// * Do_Core_Exit_Checks_Debug() runs if the Do_Core() call makes it to the
//   end without a fail() longjmping out from under it.  It also checks to
//   make sure the state has balanced, and that the return result is
//   consistent with the state being returned.
//
// Because none of these routines are in the release build, they cannot have
// any side-effects that affect the interpreter's ordinary operation.
//

#include "sys-core.h"

#if !defined(NDEBUG)


//
//  Dump_Frame_Location: C
//
void Dump_Frame_Location(REBFRM *f)
{
    REBVAL dump;
    COPY_VALUE(&dump, f->value, f->specifier);

    PROBE_MSG(&dump, "Dump_Frame_Location() value");

    if (f->flags.bits & DO_FLAG_VA_LIST) {
        //
        // NOTE: This reifies the va_list in the frame, and hence has
        // side effects.  It may need to be commented out if the
        // problem you are trapping with DO_COUNT_BREAKPOINT was
        // specifically with va_list frame processing.
        //
        const REBOOL truncated = TRUE;
        Reify_Va_To_Array_In_Frame(f, truncated);
    }

    if (f->pending && NOT_END(f->pending)) {
        assert(IS_SPECIFIC(f->pending));
        PROBE_MSG(
            const_KNOWN(f->pending),
            "EVAL in progress, so next will be..."
        );
    }

    if (IS_END(f->value)) {
        Debug_Fmt("...then Dump_Frame_Location() at end of array");
    }
    else {
        REBVAL dump;
        Val_Init_Series_Index_Core(
            &dump,
            REB_BLOCK,
            ARR_SERIES(f->source.array),
            cast(REBCNT, f->index),
            f->specifier
        );

        PROBE_MSG(&dump, "Dump_Frame_Location() next input");
    }
}


//
//  Do_Core_Entry_Checks_Debug: C
//
void Do_Core_Entry_Checks_Debug(REBFRM *f)
{
    // Though we can protect the value written into the target pointer 'out'
    // from GC during the course of evaluation, we can't protect the
    // underlying value from relocation.  Technically this would be a problem
    // for any series which might be modified while this call is running, but
    // most notably it applies to the data stack--where output used to always
    // be returned.
    //
    // !!! A non-contiguous data stack which is not a series is a possibility.
    //
#ifdef STRESS_CHECK_DO_OUT_POINTER
    REBSER *containing = Try_Find_Containing_Series_Debug(f->out);

    if (containing) {
        if (GET_SER_FLAG(series, SERIES_FLAG_FIXED_SIZE)) {
            //
            // Currently it's considered OK to be writing into a fixed size
            // series, for instance the durable portion of a function's
            // arg storage.  It's assumed that the memory will not move
            // during the course of the argument evaluation.
            //
        }
        else {
            Debug_Fmt("Request for ->out location in movable series memory");
            assert(FALSE);
        }
    }
#else
    assert(!IN_DATA_STACK_DEBUG(f->out));
#endif

    // The caller must preload ->value with the first value to process.  It
    // may be resident in the array passed that will be used to fetch further
    // values, or it may not.
    //
    assert(f->value);

    f->label = NULL;
    f->label_debug = NULL;

    // All callers should ensure that the type isn't an END marker before
    // bothering to invoke Do_Core().
    //
    assert(NOT_END(f->value));

    // The DO_FLAGs were decided to come in pairs for clarity, to make sure
    // that each callsite of the core routines was clear on what it was
    // asking for.  This may or may not be overkill long term, but helps now.
    //
    assert(
        LOGICAL(f->flags.bits & DO_FLAG_NEXT)
        != LOGICAL(f->flags.bits & DO_FLAG_TO_END)
    );
    assert(
        LOGICAL(f->flags.bits & DO_FLAG_LOOKAHEAD)
        != LOGICAL(f->flags.bits & DO_FLAG_NO_LOOKAHEAD)
    );
    assert(
        LOGICAL(f->flags.bits & DO_FLAG_ARGS_EVALUATE)
        != LOGICAL(f->flags.bits & DO_FLAG_NO_ARGS_EVALUATE)
    );
}


// These are checks common to Expression and Exit checks (hence also common
// to the "end of Start" checks, since that runs on the first expression)
//
static void Do_Core_Shared_Checks_Debug(REBFRM *f) {
    //
    // There shouldn't have been any "accumulated state", in the sense that
    // we should be back where we started in terms of the data stack, the
    // mold buffer position, the outstanding manual series allocations, etc.
    //
    // Because this check is a bit expensive it is lightened up and used in
    // the exit case only.  But re-enable it to help narrowing down an
    // imbalanced state discovered on an exit.

    /* ASSERT_STATE_BALANCED(&f->state);*/
    assert(f == FS_TOP);
    assert(f->state.top_chunk == TG_Top_Chunk);
    /* assert(DSP == f->dsp_orig); */ // !!! not true now with push SET-WORD!

    if (f->flags.bits & DO_FLAG_VA_LIST)
        assert(f->index == TRASHED_INDEX);
    else {
        assert(
            f->index != TRASHED_INDEX
            && f->index != END_FLAG
            && f->index != THROWN_FLAG
            && f->index != VA_LIST_FLAG
        ); // END, THROWN, VA_LIST only used by wrappers
    }

    if (IS_END(f->value) || THROWN(f->out))
        return;

    assert(f->value_type == VAL_TYPE(f->value));

    //=//// BELOW CHECKS ONLY APPLY IN THE EXIT CASE WITH MORE DATA ///////=//

    // The eval_type is expected to be calculated already.  Should match
    // f->value, with special exemption for optimized lookback calls
    // coming from DO_NEXT_REFETCH_MAY_THROW()
    //
    assert(
        (
            (f->eval_type == REB_0_LOOKBACK  || f->eval_type == REB_FUNCTION)
            && (IS_WORD(f->value) || IS_FUNCTION(f->value))
        )
        || f->eval_type == VAL_TYPE(f->value)
    );

    assert(f->value);
    assert(NOT_END(f->value));
    assert(NOT(THROWN(f->value)));
    ASSERT_VALUE_MANAGED(f->value);
    assert(f->value != f->out);

    if (f->gotten != NULL) { // See notes on `f->gotten`
        if (f->eval_type == REB_WORD) {
            enum Reb_Kind eval_type;
            REBVAL *test_gotten = Get_Var_Core(
                &eval_type,
                f->value,
                f->specifier,
                GETVAR_READ_ONLY
            ); // Expensive check, but a fairly important one.  Review.

            // Successive Do_Core calls are not robust to changes in system
            // state besides those made by Do_Core.  If one of these fire,
            // you probably should be using the INDEXOR-based API.
            //
            assert(test_gotten == f->gotten);
            if (eval_type == REB_0_LOOKBACK)
                assert(f->eval_type == REB_0_LOOKBACK);
        }
      /*  else
            assert(IS_FUNCTION(f->value));*/
    }
}


//
//  Do_Core_Expression_Checks_Debug: C
//
// The iteration preamble takes care of clearing out variables and preparing
// the state for a new "/NEXT" evaluation.  It's a way of ensuring in the
// debug build that one evaluation does not leak data into the next, and
// making the code shareable allows code paths that jump to later spots
// in the switch (vs. starting at the top) to reuse the work.
//
REBUPT Do_Core_Expression_Checks_Debug(REBFRM *f) {

    Do_Core_Shared_Checks_Debug(f);

    // Once a throw is started, no new expressions may be evaluated until
    // that throw gets handled.
    //
    assert(IS_TRASH_DEBUG(&TG_Thrown_Arg));

    assert(f->label == NULL && f->label_debug == NULL);

    // Make sure `eval` is trash in debug build if not doing a `reevaluate`.
    // It does not have to be GC safe (for reasons explained below).  We
    // also need to reset evaluation to normal vs. a kind of "inline quoting"
    // in case EVAL/ONLY had enabled that.
    //
    // Note that since the cell lives in a union, it cannot have a constructor
    // so the automatic mark of writable that most REBVALs get could not
    // be used.  Since it's a raw RELVAL, we have to explicitly mark writable.
    //
    // Also, the eval's cell bits live in a union that can wind up getting used
    // for other purposes.  Hence the writability must be re-indicated here
    // before the slot is used each time.
    //
    if (f->value != &f->cell)
        INIT_CELL_IF_DEBUG(&f->cell);

    // Trash call variables in debug build to make sure they're not reused.
    // Note that this call frame will *not* be seen by the GC unless it gets
    // chained in via a function execution, so it's okay to put "non-GC safe"
    // trash in at this point...though by the time of that call, they must
    // hold valid values.
    
    TRASH_POINTER_IF_DEBUG(f->param);
    TRASH_POINTER_IF_DEBUG(f->arg);
    TRASH_POINTER_IF_DEBUG(f->refine);

    TRASH_POINTER_IF_DEBUG(f->args_head);
    TRASH_POINTER_IF_DEBUG(f->varlist);
  
    TRASH_POINTER_IF_DEBUG(f->func);
    TRASH_POINTER_IF_DEBUG(f->binding);
    TRASH_POINTER_IF_DEBUG(f->underlying);

    // Mutate va_list sources into arrays at fairly random moments in the
    // debug build.  It should be able to handle it at any time.
    //
    if ((f->flags.bits & DO_FLAG_VA_LIST) && SPORADICALLY(50)) {
        const REBOOL truncated = TRUE;
        Reify_Va_To_Array_In_Frame(f, truncated);
    }

    // We bound the count at the max unsigned 32-bit, since otherwise it would
    // roll over to zero and print a message that wasn't asked for, which
    // is annoying even in a debug build.  (It's actually a REBUPT, so this
    // wastes possible bits in the 64-bit build, but there's no MAX_REBUPT.)
    //
    if (TG_Do_Count < MAX_U32)
        f->do_count = ++TG_Do_Count;

    return f->do_count;
}


//
//  Do_Core_Exit_Checks_Debug: C
//
void Do_Core_Exit_Checks_Debug(REBFRM *f) {
    //
    // To keep from slowing down the debug build too much, this is not put in
    // the shared checks.  But if it fires and it's hard to figure out which
    // exact cycle caused the problem, re-add it in the shared checks.
    //
    ASSERT_STATE_BALANCED(&f->state);

    Do_Core_Shared_Checks_Debug(f);

    if (NOT_END(f->value) && NOT(f->flags.bits & DO_FLAG_VA_LIST)) {
        assert(
            (f->index <= ARR_LEN(f->source.array))
            || (
                (
                    (f->pending && IS_END(f->pending))
                    || THROWN(f->out)
                )
                && f->index == ARR_LEN(f->source.array) + 1
            )
        );
    }

    if (f->flags.bits & DO_FLAG_TO_END)
        assert(THROWN(f->out) || IS_END(f->value));

    // Function execution should have written *some* actual output value.
    //
    assert(NOT_END(f->out)); // series END marker shouldn't leak out
    assert(!IS_TRASH_DEBUG(f->out));
    assert(VAL_TYPE(f->out) <= REB_MAX_VOID); // cheap check

    if (NOT(THROWN(f->out))) {
        assert(f->label == NULL);
        ASSERT_VALUE_MANAGED(f->out);
    }
}

#endif
