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
// Due to the length of Eval_Core_Throws() and debug checks it already has,
// some debug-only routines are separated out here.  (Note that these are in
// addition to the checks already done by Push_Level() and Drop_Level() time)
//
// * Eval_Core_Expression_Checks_Debug() runs before each full "expression"
//   is evaluated, e.g. before each EVALUATE step.  It makes sure the state
//   balanced completely--so no PUSH() that wasn't balanced by a DROP().  It
//   also trashes variables in the frame which might accidentally carry over
//   from one step to another, so that there will be a crash instead of a
//   casual reuse.
//
// * Eval_Core_Exit_Checks_Debug() runs if the Eval_Core_Throws() call makes
//   it to the end without a fail() longjmping out from under it.  It also
//   checks to make sure the state has balanced, and that the return result is
//   consistent with the state being returned.
//
// Because none of these routines are in the release build, they cannot have
// any side-effects that affect the interpreter's ordinary operation.
//

#include "sys-core.h"

#if defined(DEBUG_COUNT_TICKS) && defined(DEBUG_HAS_PROBE)

//
//  Dump_Level_Location: C
//
void Dump_Level_Location(const Cell* current, Level* L)
{
    DECLARE_VALUE (dump);

    if (current) {
        Derelativize(dump, current, L->specifier);
        printf("Dump_Level_Location() current\n");
        PROBE(dump);
    }

    if (IS_END(L->value)) {
        printf("...then Dump_Level_Location() is at end of array\n");
        if (not current and not L->value) { // well, that wasn't informative
            if (not L->prior)
                printf("...and no parent frame, so you're out of luck\n");
            else {
                printf("...dumping parent in case that's more useful?\n");
                Dump_Level_Location(nullptr, L->prior);
            }
        }
    }
    else {
        Derelativize(dump, L->value, L->specifier);
        printf("Dump_Level_Location() next\n");
        PROBE(dump);

        printf("Dump_Level_Location() rest\n");

        if (LVL_IS_VALIST(L)) {
            //
            // NOTE: This reifies the va_list in the frame, and hence has side
            // effects.  It may need to be commented out if the problem you
            // are trapping with TICK_BREAKPOINT or C-DEBUG-BREAK was
            // specifically related to va_list frame processing.
            //
            const bool truncated = true;
            Reify_Va_To_Array_In_Level(L, truncated);
        }

        Init_Any_Series_At_Core(
            dump,
            REB_BLOCK,
            SER(L->source->array),
            cast(REBLEN, L->source->index),
            L->specifier
        );
        PROBE(dump);
    }
}

#endif


#if !defined(NDEBUG)

// These are checks common to Expression and Exit checks (hence also common
// to the "end of Start" checks, since that runs on the first expression)
//
static void Eval_Core_Shared_Checks_Debug(Level* L) {
    //
    // The state isn't actually guaranteed to balance overall until a frame
    // is completely dropped.  This is because a frame may be reused over
    // multiple calls by something like REDUCE or FORM, accumulating items
    // on the data stack or mold stack/etc.  See Drop_Level() for the actual
    // balance check.

    assert(L == TOP_LEVEL);
    assert(TOP_INDEX == L->stack_base);

    assert(not (L->flags.bits & DO_FLAG_FINAL_DEBUG));

    if (L->source->array) {
        assert(not IS_POINTER_TRASH_DEBUG(L->source->array));
        assert(
            L->source->index != TRASHED_INDEX
            and L->source->index != END_FLAG_PRIVATE // ...special case use!
            and L->source->index != THROWN_FLAG_PRIVATE // ...don't use these
            and L->source->index != VA_LIST_FLAG_PRIVATE // ...usually...
        ); // END, THROWN, VA_LIST only used by wrappers
    }
    else
        assert(L->source->index == TRASHED_INDEX);

    // If this fires, it means that Flip_Series_To_White was not called an
    // equal number of times after Flip_Series_To_Black, which means that
    // the custom marker on series accumulated.
    //
    assert(TG_Num_Black_Series == 0);

    // We only have a label if we are in the middle of running a function,
    // and if we're not running a function then L->original should be null.
    //
    assert(not L->original);
    assert(IS_POINTER_TRASH_DEBUG(L->opt_label));

    if (L->varlist) {
        assert(NOT_SER_FLAG(L->varlist, NODE_FLAG_MANAGED));
        assert(NOT_SER_INFO(L->varlist, SERIES_INFO_INACCESSIBLE));
    }

    //=//// ^-- ABOVE CHECKS *ALWAYS* APPLY ///////////////////////////////=//

    if (IS_END(L->value))
        return;

    if (NOT_END(L->out) and THROWN(L->out))
        return;

    //=//// v-- BELOW CHECKS ONLY APPLY IN EXITS CASE WITH MORE CODE //////=//

    assert(NOT_END(L->value));
    assert(not THROWN(L->value));
    assert(L->value != L->out);

    //=//// ^-- ADD CHECKS EARLIER THAN HERE IF THEY SHOULD ALWAYS RUN ////=//
}


//
//  Eval_Core_Expression_Checks_Debug: C
//
// The iteration preamble takes care of clearing out variables and preparing
// the state for a new "/NEXT" evaluation.  It's a way of ensuring in the
// debug build that one evaluation does not leak data into the next, and
// making the code shareable allows code paths that jump to later spots
// in the switch (vs. starting at the top) to reuse the work.
//
void Eval_Core_Expression_Checks_Debug(Level* L) {

    assert(L == TOP_LEVEL);  // should be topmost frame, still

    Eval_Core_Shared_Checks_Debug(L);

    // The previous frame doesn't know *what* code is going to be running,
    // and it can shake up data pointers arbitrarily.  Any cache of a fetched
    // word must be dropped if it calls a sub-evaluator (signified by END).
    // Exception is subframes, which proxy the gotten into the child and
    // then copy the updated gotten back...signify this interim state in
    // the debug build with trash pointer.
    //
    assert(
        IS_POINTER_TRASH_DEBUG(L->prior->gotten)
        or not L->prior->gotten
    );

    if (L->gotten) {
        if (not Is_Level_Gotten_Shoved(L)) {
            assert(IS_WORD(L->value));
            assert(Try_Get_Opt_Var(L->value, L->specifier) == L->gotten);
        }
    }

  #if defined(DEBUG_UNREADABLE_BLANKS)
    assert(Is_Unreadable_Debug(&TG_Thrown_Arg)); // no evals between throws
  #endif

    // Trash fields that GC won't be seeing unless Is_Action_Level()
    //
    TRASH_POINTER_IF_DEBUG(L->param);
    TRASH_POINTER_IF_DEBUG(L->arg);
    TRASH_POINTER_IF_DEBUG(L->special);
    TRASH_POINTER_IF_DEBUG(L->refine);

    assert(
        not L->varlist
        or NOT_SER_INFO(L->varlist, SERIES_INFO_INACCESSIBLE)
    );

    // Mutate va_list sources into arrays at fairly random moments in the
    // debug build.  It should be able to handle it at any time.
    //
    if (LVL_IS_VALIST(L) and SPORADICALLY(50)) {
        const bool truncated = true;
        Reify_Va_To_Array_In_Level(L, truncated);
    }
}


//
//  Do_Process_Action_Checks_Debug: C
//
void Do_Process_Action_Checks_Debug(Level* L) {

    assert(IS_FRAME(L->rootvar));
    assert(L->arg == L->rootvar + 1);

    // See Level_Phase() for why it's not allowed when dummy is the dispatcher
    //
    REBACT *phase = L->rootvar->payload.any_context.phase;
    if (phase == PG_Dummy_Action)
        return;

    //=//// v-- BELOW CHECKS ONLY APPLY WHEN Level_Phase() is VALID ////////=//

    assert(GET_SER_FLAG(phase, ARRAY_FLAG_PARAMLIST));
    if (L->param != ACT_PARAMS_HEAD(phase)) {
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
            Level_Phase(L->prior) == NAT_ACTION(do)
            or Level_Phase(L->prior) == NAT_ACTION(applique)
        );
    }

    if (L->refine == ORDINARY_ARG) {
        if (not (L->out->header.bits & OUT_MARKED_STALE))
            assert(GET_ACT_FLAG(phase, ACTION_FLAG_INVISIBLE));
    }
    else
        assert(L->refine == LOOKBACK_ARG);
}


//
//  Do_After_Action_Checks_Debug: C
//
void Do_After_Action_Checks_Debug(Level* L) {
    assert(NOT_END(L->out));
    assert(not THROWN(L->out));

    if (GET_SER_INFO(L->varlist, SERIES_INFO_INACCESSIBLE)) // e.g. ENCLOSE
        return;

    // See Level_Phase() for why it's not allowed when DEFER-0 is the dispatcher
    //
    REBACT *phase = LVL_PHASE_OR_DUMMY(L);
    if (phase == PG_Dummy_Action)
        return;

    //=//// v-- BELOW CHECKS ONLY APPLY WHEN Level_Phase() is VALID ////////=//

    // Usermode functions check the return type via Returner_Dispatcher(),
    // with everything else assumed to return the correct type.  But this
    // double checks any function marked with RETURN in the debug build,
    // so native return types are checked instead of just trusting the C.
    //
    if (GET_ACT_FLAG(phase, ACTION_FLAG_RETURN)) {
        Value* typeset = ACT_PARAM(phase, ACT_NUM_PARAMS(phase));
        assert(Cell_Parameter_Id(typeset) == SYM_RETURN);
        if (
            not TYPE_CHECK(typeset, VAL_TYPE(L->out))
            and not (
                GET_ACT_FLAG(phase, ACTION_FLAG_INVISIBLE)
                and IS_NULLED(L->out) // this happens with `do [return]`
            )
        ){
            printf("Native code violated return type contract!\n");
            panic (Error_Bad_Return_Type(L, VAL_TYPE(L->out)));
        }
    }
}


//
//  Eval_Core_Exit_Checks_Debug: C
//
void Eval_Core_Exit_Checks_Debug(Level* L) {
    Eval_Core_Shared_Checks_Debug(L);

    if (L->gotten) {
        if (L->gotten == Level_Shove(L->prior))
            assert(GET_VAL_FLAG(Level_Shove(L->prior), VALUE_FLAG_ENFIXED));
        else {
            assert(IS_WORD(L->value));
            assert(Try_Get_Opt_Var(L->value, L->specifier) == L->gotten);
        }
    }

    if (NOT_END(L->value) and not LVL_IS_VALIST(L)) {
        if (L->source->index > ARR_LEN(L->source->array)) {
            assert(
                (L->source->pending != nullptr and IS_END(L->source->pending))
                or THROWN(L->out)
            );
            assert(L->source->index == ARR_LEN(L->source->array) + 1);
        }
    }

    if (L->flags.bits & DO_FLAG_TO_END)
        assert(THROWN(L->out) or IS_END(L->value));

    // We'd like `do [1 + comment "foo"]` to act identically to `do [1 +]`
    // (as opposed to `do [1 + ()]`).  Eval_Core_Throws() thus distinguishes
    // an END for a fully "invisible" evaluation, as opposed to void.  This
    // distinction is only offered internally, at the moment.
    //
    if (NOT_END(L->out))
        assert(VAL_TYPE(L->out) <= REB_MAX_NULLED);

    L->flags.bits |= DO_FLAG_FINAL_DEBUG;
}

#endif
