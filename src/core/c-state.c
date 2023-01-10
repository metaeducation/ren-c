//
//  File: %c-state.c
//  Summary: "Memoization of Status of Global Interpreter State"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2022 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See remarks in %sys-state.h
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * WORK IN PROGRESS: "stackless" features are adding demands to tighten up
//   the adjustment and rollback of global state.
//


#include "sys-core.h"


//
//  Snap_State_Core: C
//
// **Note:** Modifying this routine likely means a necessary modification to
// `Assert_State_Balanced_Debug()`.
//
void Snap_State_Core(struct Reb_State *s)
{
    s->stack_base = TOP_INDEX;

    s->guarded_len = SER_USED(GC_Guarded);

    s->manuals_len = SER_USED(GC_Manuals);
    s->mold_buf_len = STR_LEN(STR(MOLD_BUF));
    s->mold_buf_size = STR_SIZE(STR(MOLD_BUF));
    s->mold_loop_tail = SER_USED(TG_Mold_Stack);

    s->saved_sigmask = Eval_Sigmask;
}


//
//  Rollback_Globals_To_State: C
//
// This routine is used by things like Drop_Frame() when a fail occurs, to
// automatically restore the state of globals to how they were at the time
// the passed-in state was SNAP_STATE()'d.
//
void Rollback_Globals_To_State(struct Reb_State *s)
{
    Drop_Data_Stack_To(s->stack_base);

    // Free any manual series that were extant (e.g. Make_Series() nodes
    // which weren't created with NODE_FLAG_MANAGED and were not transitioned
    // into the managed state).  This will include the series used as backing
    // store for rebMalloc() calls.
    //
    assert(SER_USED(GC_Manuals) >= s->manuals_len);
    while (SER_USED(GC_Manuals) != s->manuals_len) {
        Free_Unmanaged_Series(
            *SER_AT(REBSER*, GC_Manuals, SER_USED(GC_Manuals) - 1)
        );  // ^-- Free_Unmanaged_Series will decrement SER_USED(GC_Manuals)
    }

    SET_SERIES_LEN(GC_Guarded, s->guarded_len);

    TERM_STR_LEN_SIZE(STR(MOLD_BUF), s->mold_buf_len, s->mold_buf_size);

  #if !defined(NDEBUG)
    //
    // Because reporting errors in the actual Push_Mold process leads to
    // recursion, this debug flag helps make it clearer what happens if
    // that does happen...and can land on the right comment.  If there's
    // a fail of some kind, the flag for the warning needs to be cleared.
    //
    TG_Pushing_Mold = false;
  #endif

    SET_SERIES_LEN(TG_Mold_Stack, s->mold_loop_tail);

    Eval_Sigmask = s->saved_sigmask;
}


#if !defined(NDEBUG)

//
//  Assert_State_Balanced_Debug: C
//
// Check that all variables in `state` have returned to what they were at
// the time of snapshot.
//
void Assert_State_Balanced_Debug(
    struct Reb_State *s,
    const char *file,
    int line
){
    if (s->stack_base != TOP_INDEX) {
        printf(
            "PUSH()x%d without DROP()\n",
            cast(int, TOP_INDEX - s->stack_base)
        );
        panic_at (nullptr, file, line);
    }

    if (s->guarded_len != SER_USED(GC_Guarded)) {
        printf(
            "PUSH_GC_GUARD()x%d without DROP_GC_GUARD()\n",
            cast(int, SER_USED(GC_Guarded) - s->guarded_len)
        );
        Node* guarded = *SER_AT(
            Node*,
            GC_Guarded,
            SER_USED(GC_Guarded) - 1
        );
        panic_at (guarded, file, line);
    }

    // !!! Note that this inherits a test that uses GC_Manuals->content.xxx
    // instead of SER_USED().  The idea being that although some series
    // are able to fit in the series node, the GC_Manuals wouldn't ever
    // pay for that check because it would always be known not to.  Review
    // this in general for things that may not need "series" overhead,
    // e.g. a contiguous pointer stack.
    //
    if (s->manuals_len > SER_USED(GC_Manuals)) {
        //
        // Note: Should this ever actually happen, panic() on the series won't
        // do any real good in helping debug it.  You'll probably need
        // additional checks in Manage_Series() and Free_Unmanaged_Series()
        // that check against the caller's manuals_len.
        //
        panic_at ("manual series freed outside checkpoint", file, line);
    }
    else if (s->manuals_len < SER_USED(GC_Manuals)) {
        printf(
            "Make_Series()x%d w/o Free_Unmanaged_Series or Manage_Series\n",
            cast(int, SER_USED(GC_Manuals) - s->manuals_len)
        );
        REBSER *manual = *(SER_AT(
            REBSER*,
            GC_Manuals,
            SER_USED(GC_Manuals) - 1
        ));
        panic_at (manual, file, line);
    }

    assert(s->mold_buf_len == STR_LEN(STR(MOLD_BUF)));
    assert(s->mold_buf_size == STR_SIZE(STR(MOLD_BUF)));
    assert(s->mold_loop_tail == SER_USED(TG_Mold_Stack));

/*    assert(s->saved_sigmask == Eval_Sigmask);  // !!! is this always true? */
}

#endif
