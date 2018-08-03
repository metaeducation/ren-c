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
// Copyright 2012-2017 Rebol Open Source Contributors
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
// some debug-only routines are separated out here.  (Note that these are in
// addition to the checks already done by Push_Frame() and Drop_Frame() time)
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

#if !defined(NDEBUG) || defined(DEBUG_COUNT_TICKS)

//
//  Dump_Frame_Location: C
//
void Dump_Frame_Location(const RELVAL *current, REBFRM *f)
{
    DECLARE_LOCAL (dump);

    if (current) {
        Derelativize(dump, current, f->specifier);
        printf("Dump_Frame_Location() current\n");
        PROBE(dump);
    }

    if (f->value) {
        Derelativize(dump, f->value, f->specifier);
        printf("Dump_Frame_Location() next\n");
        PROBE(dump);
    }

    if (FRM_AT_END(f)) {
        printf("...then Dump_Frame_Location() is at end of array\n");
        if (not current and not f->value) { // well, that wasn't informative
            if (not f->prior)
                printf("...and no parent frame, so you're out of luck\n");
            else {
                printf("...dumping parent in case that's more useful?\n");
                Dump_Frame_Location(NULL, f->prior);
            }
        }
    }
    else {
        printf("Dump_Frame_Location() rest\n");

        if (FRM_IS_VALIST(f)) {
            //
            // NOTE: This reifies the va_list in the frame, and hence has side
            // effects.  It may need to be commented out if the problem you
            // are trapping with TICK_BREAKPOINT or C-DEBUG-BREAK was
            // specifically related to va_list frame processing.
            //
            const REBOOL truncated = TRUE;
            Reify_Va_To_Array_In_Frame(f, truncated);
        }

        Init_Any_Series_At_Core(
            dump,
            REB_BLOCK,
            SER(f->source.array),
            cast(REBCNT, f->source.index),
            f->specifier
        );
        PROBE(dump);
    }
}

#endif


#if !defined(NDEBUG)

// These are checks common to Expression and Exit checks (hence also common
// to the "end of Start" checks, since that runs on the first expression)
//
static void Do_Core_Shared_Checks_Debug(REBFRM *f) {
    //
    // The state isn't actually guaranteed to balance overall until a frame
    // is completely dropped.  This is because a frame may be reused over
    // multiple calls by something like REDUCE or FORM, accumulating items
    // on the data stack or mold stack/etc.  See Drop_Frame_Core() for the
    // actual balance check.

    assert(f == FS_TOP);
    assert(DSP == f->dsp_orig);

    assert(not (f->flags.bits & DO_FLAG_FINAL_DEBUG));

    if (f->source.array) {
        assert(not IS_POINTER_TRASH_DEBUG(f->source.array));
        assert(
            f->source.index != TRASHED_INDEX
            and f->source.index != END_FLAG_PRIVATE // ...special case use!
            and f->source.index != THROWN_FLAG_PRIVATE // ...don't use these
            and f->source.index != VA_LIST_FLAG_PRIVATE // ...usually...
        ); // END, THROWN, VA_LIST only used by wrappers
    }
    else
        assert(f->source.index == TRASHED_INDEX);

    // If this fires, it means that Flip_Series_To_White was not called an
    // equal number of times after Flip_Series_To_Black, which means that
    // the custom marker on series accumulated.
    //
    assert(TG_Num_Black_Series == 0);

    if (f->gotten) {
        assert(IS_WORD(f->value)); // may not match eval_type at this point
        assert(Get_Opt_Var_May_Fail(f->value, f->specifier) == f->gotten);
    }

    // We only have a label if we are in the middle of running a function,
    // and if we're not running a function then f->original should be null.
    //
    assert(not f->original);
    assert(IS_POINTER_TRASH_DEBUG(f->opt_label));

    if (f->varlist) {
        assert(NOT_SER_FLAG(f->varlist, NODE_FLAG_MANAGED));
        assert(NOT_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE));
    }

    //=//// ^-- ABOVE CHECKS *ALWAYS* APPLY ///////////////////////////////=//

    if (FRM_AT_END(f))
        return;

    if (NOT_END(f->out) and THROWN(f->out))
        return;

    //=//// v-- BELOW CHECKS ONLY APPLY IN EXITS CASE WITH MORE CODE //////=//

    // The eval_type is expected to be calculated already.  Should match
    // f->value, with special exemption for optimized lookback calls
    // coming from Do_Next_In_Subframe_Throws()
    //
    if (f->eval_type != VAL_TYPE(f->value))
        assert(
            f->eval_type == REB_ACTION
            and (IS_WORD(f->value) or IS_ACTION(f->value))
        );

    assert(f->value);
    assert(FRM_HAS_MORE(f));
    assert(not THROWN(f->value));
    assert(f->value != f->out);

    //=//// ^-- ADD CHECKS EARLIER THAN HERE IF THEY SHOULD ALWAYS RUN ////=//
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
void Do_Core_Expression_Checks_Debug(REBFRM *f) {

    assert(f == FS_TOP); // should be topmost frame, still

    Do_Core_Shared_Checks_Debug(f);

    // The previous frame doesn't know *what* code is going to be running,
    // and it can shake up data pointers arbitrarily.  Any cache of a fetched
    // word must be dropped if it calls a sub-evaluator (signified by END).
    // Exception is subframes, which proxy the gotten into the child and
    // then copy the updated gotten back...signify this interim state in
    // the debug build with trash pointer.
    //
    assert(
        IS_POINTER_TRASH_DEBUG(f->prior->gotten)
        or not f->prior->gotten
    );

  #if defined(DEBUG_UNREADABLE_BLANKS)
    //
    // The only thing the evaluator can take for granted between evaluations
    // about the output cell is that it's not trash.  In the debug build,
    // give this more teeth by explicitly setting it to an unreadable blank,
    // but only if it wasn't an END marker (that's how we can tell no
    // evaluations have been done yet, consider `(comment [...] + 2)`) or
    // have NODE_FLAG_MARKED by BAR! for (1 + 2 | comment "hi") to be 3.
    //
    if (not (f->out->header.bits & OUT_MARKED_STALE)) {
        Init_Unreadable_Blank(f->out);
        f->out->header.bits |= OUT_MARKED_STALE;
    }

    // Once a throw is started, no new expressions may be evaluated until
    // that throw gets handled.
    //
    assert(IS_UNREADABLE_DEBUG(&TG_Thrown_Arg));

    // Make sure `cell` is reset in debug build if not doing a `reevaluate`
    // (once this was used by EVAL the native, but now it's used by rebEval()
    // at the API level, which currently sets `f->value = &f->cell;`)
    //
    #if !defined(NDEBUG)
        if (f->value != &f->cell)
            Init_Unreadable_Blank(&f->cell);
    #endif
  #endif

    // Trash call variables in debug build to make sure they're not reused.
    // Note that this call frame will *not* be seen by the GC unless it gets
    // chained in via a function execution, so it's okay to put "non-GC safe"
    // trash in at this point...though by the time of that call, they must
    // hold valid values.

    TRASH_POINTER_IF_DEBUG(f->param);
    TRASH_POINTER_IF_DEBUG(f->arg);
    TRASH_POINTER_IF_DEBUG(f->special);
    TRASH_POINTER_IF_DEBUG(f->refine);

    assert(
        not f->varlist
        or NOT_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE)
    );

    // Mutate va_list sources into arrays at fairly random moments in the
    // debug build.  It should be able to handle it at any time.
    //
    if (FRM_IS_VALIST(f) and SPORADICALLY(50)) {
        const REBOOL truncated = TRUE;
        Reify_Va_To_Array_In_Frame(f, truncated);
    }
}


//
//  Do_Process_Action_Checks_Debug: C
//
void Do_Process_Action_Checks_Debug(REBFRM *f) {

    assert(IS_FRAME(f->rootvar));
    assert(f->arg == f->rootvar + 1);

    // ACTION!s with no args will have ends in the first position of their
    // paramlist, which may come from Init_Endlike_Header() and not be cells.
    //
    assert(IS_END(f->special) or (f->special->header.bits & NODE_FLAG_CELL));

    // DECLARE_FRAME() starts out f->cell as valid GC-visible bits, and as
    // it's used for various temporary purposes it should remain valid.  But
    // its contents could be anything, based on that temporary purpose.  Help
    // hint functions not to try to read from it before they overwrite it with
    // their own content.
    //
    // !!! Allowing the release build to "leak" temporary cell state to
    // natives may be bad, and there are advantages to being able to count on
    // this being an END.  However, unless one wants to get in the habit of
    // zeroing out all temporary state for "security" reasons then clients who
    // call Do_Next_In_Frame() would be able to see it anyway.  For now, do
    // the more performant thing and leak whatever is in f->cell to the
    // function in the release build, to avoid paying for the initialization.
    //
  #if !defined(NDEBUG)
    Init_Unreadable_Blank(&f->cell); // DECLARE_FRAME() requires GC safe
  #endif

    // See FRM_PHASE() for why it's not allowed when dummy is the dispatcher
    //
    REBACT *phase = f->rootvar->payload.any_context.phase;
    if (phase == PG_Dummy_Action)
        return;

    //=//// v-- BELOW CHECKS ONLY APPLY WHEN FRM_PHASE() is VALID ////////=//

    assert(GET_SER_FLAG(phase, ARRAY_FLAG_PARAMLIST));
    if (f->param != ACT_FACADE_HEAD(phase)) {
        //
        // !!! When you MAKE FRAME! 'APPEND/ONLY, it will create a frame
        // with a keylist that has /ONLY hidden.  But there's no new ACTION!
        // to tie it to, so the only phase it knows about is plain APPEND.
        // This means when it sees system internal signals like a REFINEMENT!
        // in a refinement slot--instead of TRUE or FALSE--it thinks it has
        // to type check it, as if the user said `apply 'append [only: /foo]`.
        // Using the keylist as the facade is taken care of in DO for FRAME!,
        // and this check is here pending a more elegant sorting of this.
        //
        assert(
            FRM_PHASE(f->prior) == NAT_ACTION(do)
            or FRM_PHASE(f->prior) == NAT_ACTION(apply)
        );
    }

    if (f->refine == ORDINARY_ARG) {
        if (not (f->out->header.bits & OUT_MARKED_STALE))
            assert(GET_ACT_FLAG(phase, ACTION_FLAG_INVISIBLE));
    }
    else
        assert(f->refine == LOOKBACK_ARG);
}


//
//  Do_After_Action_Checks_Debug: C
//
void Do_After_Action_Checks_Debug(REBFRM *f) {
    assert(f->eval_type == REB_ACTION);
    assert(NOT_END(f->out));
    assert(not THROWN(f->out));

    if (GET_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE)) // e.g. ENCLOSE
        return;

    // See FRM_PHASE() for why it's not allowed when DEFER-0 is the dispatcher
    //
    REBACT *phase = FRM_PHASE_OR_DUMMY(f);
    if (phase == PG_Dummy_Action)
        return;

    //=//// v-- BELOW CHECKS ONLY APPLY WHEN FRM_PHASE() is VALID ////////=//

    // Usermode functions check the return type via Returner_Dispatcher(),
    // with everything else assumed to return the correct type.  But this
    // double checks any function marked with RETURN in the debug build,
    // so native return types are checked instead of just trusting the C.
    //
    if (GET_ACT_FLAG(phase, ACTION_FLAG_RETURN)) {
        REBVAL *typeset = ACT_PARAM(phase, ACT_NUM_PARAMS(phase));
        assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);
        if (not TYPE_CHECK(typeset, VAL_TYPE(f->out))) {
            printf("Native code violated return type contract!\n");
            panic (Error_Bad_Return_Type(f, VAL_TYPE(f->out)));
        }
    }
}


//
//  Do_Core_Exit_Checks_Debug: C
//
void Do_Core_Exit_Checks_Debug(REBFRM *f) {
    Do_Core_Shared_Checks_Debug(f);

    if (not FRM_AT_END(f) and not FRM_IS_VALIST(f)) {
        if (f->source.index > ARR_LEN(f->source.array)) {
            assert(
                (f->source.pending != NULL and IS_END(f->source.pending))
                or THROWN(f->out)
            );
            assert(f->source.index == ARR_LEN(f->source.array) + 1);
        }
    }

    if (f->flags.bits & DO_FLAG_TO_END)
        assert(THROWN(f->out) or FRM_AT_END(f));

    // We'd like `do [1 + comment "foo"]` to act identically to `do [1 +]`
    // (as opposed to `do [1 + ()]`).  Hence Do_Core() offers the distinction
    // of END for a fully "invisible" evaluation, as opposed to void.  This
    // distinction is only offered internally, at the moment.
    //
    if (NOT_END(f->out))
        assert(VAL_TYPE(f->out) <= REB_MAX_NULLED);

    f->flags.bits |= DO_FLAG_FINAL_DEBUG;
}

#endif
