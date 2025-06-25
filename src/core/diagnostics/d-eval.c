//
//  file: %d-eval.c
//  summary: "Debug-Build Checks for the Evaluator"
//  section: debug
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Due to the length of %c-eval.c and debug checks it already has, some
// debug-only routines are separated out here.  (Note that these are in
// addition to the checks already done by Push_Level() and Drop_Level() time)
//
// * Evaluator_Expression_Checks_Debug() runs before each full "expression"
//   is evaluated, e.g. before each EVALUATE step.  It makes sure the state
//   balanced completely--so no PUSH() that wasn't balanced by a DROP()
//   (for example).  It also corrupts variables in the level which might
//   accidentally carry over from one step to another, so that there will be
//   a crash instead of a casual reuse.
//
// * Evaluator_Exit_Checks_Debug() runs only if Stepper_Executor() makes
//   it to the end without a panic() longjmping out from under it.  It also
//   checks to make sure the state has balanced, and that the return result is
//   consistent with the state being returned.
//
// Because none of these routines are in the release build, they cannot have
// any side-effects that affect the interpreter's ordinary operation.
//

#include "sys-core.h"

#undef At_Level

#define L_next          At_Feed(L->feed)

#define L_next_gotten_raw  (&L->feed->gotten)
#define L_next_gotten  (not Is_Gotten_Invalid(L_next_gotten_raw))

#define L_binding     Level_Binding(L)

#if DEBUG_HAS_PROBE

//
//  Dump_Level_Location: C
//
void Dump_Level_Location(Level* L)
{
    DECLARE_ATOM (dump);

    if (
        L->executor == &Stepper_Executor  // looks ahead by one
        and LEVEL_STATE_BYTE(L) != ST_STEPPER_INITIAL_ENTRY  // L->u corrupt
    ){
        printf("Dump_Level_Location() current\n");
        PROBE(Evaluator_Level_Current(L));
    }

    if (Get_Feed_Flag(L->feed, NEEDS_SYNC)) {
        printf("...feed is at a non-synchronized point (is it scanning?)\n");
    }
    else if (Is_Feed_At_End(L->feed)) {
        printf("...then Dump_Level_Location() is at end of array\n");
        if (L->prior == BOTTOM_LEVEL)
            printf("...and no parent frame, so you're out of luck\n");
        else {
            printf("...dumping parent in case that's more useful?\n");
            Dump_Level_Location(L->prior);
        }
    }
    else {
        Derelativize(dump, L_next, L_binding);
        printf("Dump_Level_Location() next\n");
        PROBE(dump);

        printf("Dump_Level_Location() rest\n");

        if (Level_Is_Variadic(L)) {
            //
            // NOTE: This reifies the va_list in the frame, which should not
            // affect procssing.  But it is a side-effect and may need to be
            // avoided if the problem you are debugging was specifically
            // related to va_list frame processing.
            //
            const bool truncated = true;
            Reify_Variadic_Feed_As_Array_Feed(L->feed, truncated);
        }

        Init_Any_List_At_Core(
            dump,
            TYPE_BLOCK,
            Level_Array(L),
            Level_Array_Index(L),
            L_binding
        );
        PROBE(dump);
    }
}


//
//  Where_Core_Debug: C
//
// !!! This should be merged with Dump_Level_Location()
//
void Where_Core_Debug(Level* L) {
    if (FEED_IS_VARIADIC(L->feed))
        Reify_Variadic_Feed_As_Array_Feed(L->feed, false);

    REBLEN index = FEED_INDEX(L->feed);

    if (index > 0) {
        DECLARE_MOLDER (mo);
        SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
        mo->limit = 40 * 20;  // 20 lines of length 40, or so?

        REBLEN before_index = index > 3 ? index - 3 : 0;
        Push_Mold(mo);
        Mold_Array_At(mo, Feed_Array(L->feed), before_index, "[]");
        Throttle_Mold(mo);
        printf("Where(Before):\n");
        printf("%s\n\n", Binary_At(mo->string, mo->base.size));
        Drop_Mold(mo);
    }

    DECLARE_MOLDER (mo);
    SET_MOLD_FLAG(mo, MOLD_FLAG_LIMIT);
    mo->limit = 40 * 20;  // 20 lines of length 40, or so?
    Push_Mold(mo);
    Mold_Array_At(mo, Feed_Array(L->feed), index, "[]");
    Throttle_Mold(mo);
    printf("Where(At):\n");
    printf("%s\n\n", Binary_At(mo->string, mo->base.size));
    Drop_Mold(mo);
}


void Where(Level* L)
  { Where_Core_Debug(L); }  // Dump_Level_Location(L) ???

#endif  // DEBUG_HAS_PROBE


#if RUNTIME_CHECKS

// These are checks common to Expression and Exit checks (hence also common
// to the "end of Start" checks, since that runs on the first expression)
//
static void Evaluator_Shared_Checks_Debug(Level* L)
{
    // The state isn't actually guaranteed to balance overall until a level
    // is completely dropped.  This is because a level may be reused over
    // multiple calls by something like REDUCE or FORM, accumulating items
    // on the data stack or mold stack/etc.  See Drop_Level() for the actual
    // balance check.
    //
    Assert_No_DataStack_Pointers_Extant();

    // See notes on L->feed->gotten about the coherence issues in the face
    // of arbitrary function execution.
    //
    if (L_next_gotten and not Is_Frame(L_next)) {
        assert(Any_Word(L_next));

        // !!! With ACCESSORs this may be incoherent.  We need to track if
        // the value came from an accessor or not, and if it does, we should
        // not bother checking it.
        //
        // !!! This is totally dicey, and likely to break.

        DECLARE_VALUE (check);
        Option(Error*) e = Trap_Get_Word(check, L_next, L_binding);
        assert(not e);
        UNUSED(e);
        assert(
            memcmp(check, L_next_gotten_raw, 4 * sizeof(uintptr_t)) == 0
        );
    }

    assert(L == TOP_LEVEL);

    // If this fires, it means that Flip_Stub_To_White was not called an
    // equal number of times after Flip_Stub_To_Black, which means that
    // the custom marker on Stubs accumulated.
    //
    assert(g_mem.num_black_stubs == 0);

    if (L->varlist)
        assert(Not_Base_Managed(L->varlist));

    //=//// ^-- ABOVE CHECKS *ALWAYS* APPLY ///////////////////////////////=//

    if (Is_Feed_At_End(L->feed))
        return;

    if (Is_Throwing(L))
        return;

    //=//// v-- BELOW CHECKS ONLY APPLY IN EXITS CASE WITH MORE CODE //////=//

    assert(L_next != L->out);

    //=//// ^-- ADD CHECKS EARLIER THAN HERE IF THEY SHOULD ALWAYS RUN ////=//
}


//
//  Evaluator_Expression_Checks_Debug: C
//
// These fields are required upon initialization:
//
//     L->out
//     Atom pointer to which the evaluation's result should be written.
//     Should be to writable memory in a cell that lives above this call to
//     the evalutor in stable memory (not user-visible, e.g. DECLARE_ATOM
//     or the parent's L->spare).  This can't point into an array whose memory
//     may move during arbitrary evaluation, and that includes cells on the
//     expandable data stack.  It also usually can't write a function argument
//     cell, because that could expose an unfinished calculation during this
//     Action_Executor() through its FRAME!...though an Action_Executor(L) must
//     write L's *own* arg slots to fulfill them.
//
//     L->feed
//     Contains the Array* or C va_list of subsequent values to fetch...as
//     well as the binding.  The current value, its cached "gotten" value if
//     it is a WORD!, and other information is stored here through a level of
//     indirection so it may be shared and updated between recursions.
//
// This routine attempts to "corrupt" a lot of level state variables to help
// make sure one evaluation does not leak data into the next.
//
void Evaluator_Expression_Checks_Debug(Level* L)
{
    assert(L == TOP_LEVEL); // should be topmost level, still

    assert(Not_Executor_Flag(EVAL, L, DIDNT_LEFT_QUOTE_PATH));
    if (Not_Executor_Flag(EVAL, L, FULFILLING_ARG))
        assert(Not_Feed_Flag(L->feed, NO_LOOKAHEAD));
    assert(Not_Feed_Flag(L->feed, DEFERRING_INFIX));

    Evaluator_Shared_Checks_Debug(L);

    assert(not Is_Throwing(L)); // no evals between throws

    // Corrupt fields that GC won't be seeing unless Is_Action_Level()
    //
    Corrupt_Pointer_If_Debug(L->u.action.key);
    Corrupt_Pointer_If_Debug(L->u.action.arg);
    Corrupt_Pointer_If_Debug(L->u.action.param);

    // Mutate va_list sources into arrays at fairly random moments in the
    // checked build.  It should be able to handle it at any time.
    //
    if (Level_Is_Variadic(L) and SPORADICALLY(50)) {
        const bool truncated = true;
        Reify_Variadic_Feed_As_Array_Feed(L->feed, truncated);
    }
}


//
//  Do_After_Action_Checks_Debug: C
//
void Do_After_Action_Checks_Debug(Level* level_) {
    assert(not Is_Throwing(LEVEL));

    // Usermode functions check the return type via Func_Dispatcher(),
    // with everything else assumed to return the correct type.  But this
    // double checks any function marked with RETURN in the checked build,
    // so native return types are checked instead of just trusting the C.
    //
  #if CHECK_RAW_NATIVE_RETURNS
    Details* details = Ensure_Level_Details(LEVEL);
    if (Get_Details_Flag(details, RAW_NATIVE) and Is_Cell_Stable(OUT)) {
        const Param* param = cast(Param*,
            Details_At(details, IDX_RAW_NATIVE_RETURN)
        );
        assert(Is_Parameter(param));

        heeded(Corrupt_Cell_If_Debug(SPARE));
        heeded(Corrupt_Cell_If_Debug(SCRATCH));

        if (not Typecheck_Coerce_Return(LEVEL, param, OUT)) {
            assert(!"'Raw' native code violated return type contract!\n");
            crash (Error_Bad_Return_Type(LEVEL, OUT, param));
        }
    }
  #endif
}


//
//  Evaluator_Exit_Checks_Debug: C
//
void Evaluator_Exit_Checks_Debug(Level* L) {
    Evaluator_Shared_Checks_Debug(L);

    if (Not_Level_At_End(L) and not Level_Is_Variadic(L)) {
        if (Level_Array_Index(L) > Array_Len(Level_Array(L))) {
            assert(Is_Throwing(L));
            assert(Level_Array_Index(L) == Array_Len(Level_Array(L)) + 1);
        }
    }

  //=//// CHECK FOR STRAY FLAGS ///////////////////////////////////////////=//

    if (not Is_Throwing(L)) {
        Flags filtered = (L->flags.bits & ~FLAG_STATE_BYTE(255));
        filtered &= ~ (
            LEVEL_FLAG_0_IS_TRUE  // always true
            | LEVEL_FLAG_4_IS_TRUE  // always true
            | LEVEL_FLAG_ROOT_LEVEL
            | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
        );

        // These options to Stepper_Executor() should not change over the
        // course of the evaluation (could check this?)  But in any case they
        // are okay if they are set.
        //
        filtered &= ~ (
            LEVEL_FLAG_FORCE_HEAVY_NULLS
            | LEVEL_FLAG_FORCE_SURPRISING
            | LEVEL_FLAG_UNINTERRUPTIBLE
            | EVAL_EXECUTOR_FLAG_FULFILLING_ARG
            | EVAL_EXECUTOR_FLAG_NO_RESIDUE
        );

        if (filtered != 0) {
            int bit;
            for (bit = 0; bit < 32; ++bit)
                if (filtered & FLAG_LEFT_BIT(bit))
                    printf("BIT %d SET in EVAL_FLAGS\n", bit);

            assert(!"Unexpected stray flags found in evaluator finalization");
        }
    }
}

#endif
