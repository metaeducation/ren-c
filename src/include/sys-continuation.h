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

#define CONTINUE_CORE_5(...) ( \
    Pushed_Continuation(__VA_ARGS__), \
        BOUNCE_CONTINUE)  /* ^-- don't heed result: always want callback */

#define CONTINUE_CORE_4(...) ( \
    Pushed_Continuation(__VA_ARGS__, nullptr), \
        BOUNCE_CONTINUE)  /* ^-- don't heed result: always want callback */

#define CONTINUE_CORE(...) \
    PP_CONCAT(CONTINUE_CORE_, PP_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define CONTINUE(out,...) \
    CONTINUE_CORE((out), LEVEL_FLAG_SUPPRESS_VOIDS, SPECIFIED, __VA_ARGS__)

#define CONTINUE(out,...) \
    CONTINUE_CORE((out), LEVEL_FLAG_SUPPRESS_VOIDS, SPECIFIED, __VA_ARGS__)

#define CONTINUE_BRANCH(out,...) \
    CONTINUE_CORE((out), \
        LEVEL_FLAG_FORCE_HEAVY_NULLS | LEVEL_FLAG_SUPPRESS_VOIDS, \
        SPECIFIED, __VA_ARGS__)

INLINE void Continue_Sublevel_Helper(Level* L, Level* sub) {
    assert(sub == TOP_LEVEL);  // currently sub must be pushed & top level
    UNUSED(sub);
    UNUSED(L);
}

#define CONTINUE_SUBLEVEL(sub) \
    (Continue_Sublevel_Helper(level_, (sub)), \
        /* <- */ BOUNCE_CONTINUE)


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

#define DELEGATE_CORE_3(o,sub_flags,...) ( \
    assert(Not_Executor_Flag(ACTION, level_, DISPATCHER_CATCHES)), \
    assert((o) == level_->out), \
    Pushed_Continuation( \
        level_->out, \
        (sub_flags), \
        __VA_ARGS__  /* binding, branch, and "with" argument */ \
    ) ? BOUNCE_DELEGATE \
        : level_->out)  // no need to give callback to delegator

#define DELEGATE_CORE_2(out,sub_flags,...) \
    DELEGATE_CORE_3((out), (sub_flags), __VA_ARGS__, nullptr)

#define DELEGATE_CORE(out,sub_flags,...) \
    PP_CONCAT(DELEGATE_CORE_, PP_NARGS(__VA_ARGS__))( \
        (out), (sub_flags), __VA_ARGS__)

#define DELEGATE(out,...) \
    DELEGATE_CORE((out), LEVEL_MASK_NONE, SPECIFIED, __VA_ARGS__)

#define DELEGATE_BRANCH(out,...) \
    DELEGATE_CORE((out), \
        LEVEL_FLAG_FORCE_HEAVY_NULLS, \
        SPECIFIED, __VA_ARGS__)

#define DELEGATE_SUBLEVEL(sub) \
    (assert(Not_Executor_Flag(ACTION, level_, DISPATCHER_CATCHES)), \
        Continue_Sublevel_Helper(level_, (sub)), \
        /* <- */ BOUNCE_DELEGATE)
