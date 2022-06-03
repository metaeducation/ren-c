//
//  File: %sys-state.h
//  Summary: "Interpreter State"
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
// One historical idea from Rebol is to make use of a number of "hot" global
// buffers.  This is useful when the produced data is only temporary, or when
// the precise size of an output is not known in advance.  (For instance: a
// REDUCE can't accurately predict the number of elements for the result block
// based on the number in the input block, so pushing to a cached memory
// location first to get an accurate count can avoid wasteful reallocations
// or unused memory in the result.)
//
// Some buffers cannot be used across recursions, and must be cleared out
// before requesting an arbitrary evaluation.  Others can "stack", so they
// require each evaluator recursion wishing to use them must mark the limit
// used at the beginning, complete their work, and then restore the buffer's
// position to where it was.
//
// The handling of stackable buffers became more complicated with the addition
// of features like YIELD.  That means evaluator recursions can be suspended
// and resumed at will.  So there has to be enough smarts in the code for
// hibernating a portion of the buffer (in a GC-safe location), and restoring
// it to the right "baseline" for the stack level that is being unwound, e.g.:
//
//     g: generator [yield reduce [yield "a" yield "b"]]
//
//     >> g  ; imagine DSP is 0 here
//     == "a"
//
//     >> reduce [g g]  ; REDUCE changes the DSP as it accrues values
//     == ["b" ["a" "b"]]
//
// The REDUCE inside the generator and outside the generator both need to
// have a concept of baseline, but they're also pushing values to the data
// stack.  This means that baseline must be adjusted for each call to the
// generator based on the delta in stack position between each call.  Similar
// principles apply to adjusting markers for the mold buffer and other
// nestable global state.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * WORK IN PROGRESS: "stackless" features are adding demands to tighten up
//   the adjustment and rollback of global state.
//
// * Each evaluator stack frame currently stores a Reb_State structure in its
//   `REBFRM->baseline` field.  There are likely ways to compact and conserve
//   this information, e.g. by using a small fixed size structure that can
//   "pop out" into a dynamic structure if need be.  But, correctness first!
//

struct Reb_State {
    REBDSP dsp;
    REBLEN guarded_len;

    REBLEN manuals_len; // Where GC_Manuals was when state started
    REBLEN mold_buf_len;
    REBSIZ mold_buf_size;
    REBLEN mold_loop_tail;

    // Some operations disable the ability to halt, e.g. remove SIG_HALT
    // from Eval_Sigmask...and then restore it when they are done.  If one of
    // these operations is running and then there is a longjmp past the place
    // where the restore is going to happen, they'd have to pay the cost of
    // a PUSH_TRAP to put it back.  We save effort for that case by saving
    // the signal mask and restoring it at the trap states.
    //
    REBFLGS saved_sigmask;
};


// Capture a measure of the current global state.
//
// !!! This is a macro because it may be that since snapping the state is
// done on every frame push, that code should be in the header so it could
// get inlined.  However, header dependencies currently put definitions like
// DSP and MOLD_BUF later.  Review if it's worth it to break this out
// in a different way.
//
#define SNAP_STATE(s) \
    Snap_State_Core(s)


// Check that the current global state lines up with the passed-in state.
//
#ifdef NDEBUG
    #define ASSERT_STATE_BALANCED(s) NOOP
#else
    #define ASSERT_STATE_BALANCED(s) \
        Assert_State_Balanced_Debug((s), __FILE__, __LINE__)
#endif
