//
//  file: %sys-continuation.h
//  summary: "Continuation and Delegation Helpers"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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



//=//// CONTINUATION HELPER MACROS ////////////////////////////////////////=//
//
// Normal continuations come in catching and non-catching forms; they evaluate
// without tampering with the result.
//
// Branch continuations enforce the result not being pure null or void.
//
// Uses variadic method to allow you to supply an argument to be passed to
// a branch continuation if it is a function.
//

#define CONTINUE_CORE_4(...) ( \
    Pushed_Continuation(__VA_ARGS__), /* <-- don't heed result... */ \
        Bounce_Continue(TOP_LEVEL))  /* ...always want callback! */

#define CONTINUE_CORE_3(...) ( \
    Pushed_Continuation(__VA_ARGS__, nullptr), /* <-- don't heed result... */ \
        Bounce_Continue(TOP_LEVEL))  /* ...always want callback! */

#define CONTINUE_CORE(...) \
    PP_CONCAT(CONTINUE_CORE_, PP_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define CONTINUE(...) \
    CONTINUE_CORE( \
        LEVEL_FLAG_VANISHABLE_VOIDS_ONLY | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE, SPECIFIED, __VA_ARGS__)

#define CONTINUE(...) \
    CONTINUE_CORE( \
        LEVEL_FLAG_VANISHABLE_VOIDS_ONLY | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE, SPECIFIED, __VA_ARGS__)

#define CONTINUE_BRANCH(...) \
    CONTINUE_CORE( \
        LEVEL_FLAG_FORCE_HEAVY_BRANCH | LEVEL_FLAG_VANISHABLE_VOIDS_ONLY | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE, \
        SPECIFIED, __VA_ARGS__)


#define CONTINUE_SUBLEVEL \
    (assert(STATE != STATE_0), \
        /* <- */ Bounce_Continue(SUBLEVEL))

#define CONTINUE_SAMELEVEL \
    /* <- */ Bounce_Continue(LEVEL)  // STATE may be STATE_0


//=//// DELEGATION HELPER MACROS ///////////////////////////////////////////=//
//
// Delegation is when a level wants to hand over the work to do to another
// level, and not receive any further callbacks.  This gives the opportunity
// for an optimization to not go through with a continuation at all and just
// use the output if it is simple to do.
//
// !!! Delegation doesn't want to use the old level it had.  It leaves it
// on the stack for sanity of debug tracing, but it could be more optimal
// if the delegating level were freed before running what's underneath it...
// at least it could be collapsed into a more primordial state.  Review.

#define DELEGATE_CORE_3(sub_flags,...) \
    (assert(Not_Executor_Flag(ACTION, level_, DISPATCHER_CATCHES)), \
    Set_Executor_Flag(ACTION, level_, DELEGATE_CONTROL), \
    LEVEL_STATE_BYTE(level_) = 127, \
    Pushed_Continuation( \
        (sub_flags), \
        __VA_ARGS__  /* binding, branch, and "with" argument */ \
    ), TOP_LEVEL->target = Level_Out(level_), Bounce_Continue(TOP_LEVEL))

#define DELEGATE_CORE_2(sub_flags,...) \
    DELEGATE_CORE_3((sub_flags), __VA_ARGS__, nullptr)

#define DELEGATE_CORE(sub_flags,...) \
    PP_CONCAT(DELEGATE_CORE_, PP_NARGS(__VA_ARGS__))( \
        (sub_flags), __VA_ARGS__)

#define DELEGATE(...) \
    DELEGATE_CORE(LEVEL_MASK_NONE, SPECIFIED, __VA_ARGS__)

#define DELEGATE_BRANCH(...) \
    DELEGATE_CORE( \
        LEVEL_FLAG_FORCE_HEAVY_BRANCH, \
        SPECIFIED, __VA_ARGS__)

#define DELEGATE_SUBLEVEL \
    (assert(Not_Executor_Flag(ACTION, level_, DISPATCHER_CATCHES)), \
        assert(TOP_LEVEL->prior == level_), \
        Set_Executor_Flag(ACTION, level_, DELEGATE_CONTROL), \
        LEVEL_STATE_BYTE(level_) = 127, \
        /* <- */ Bounce_Continue(TOP_LEVEL))
