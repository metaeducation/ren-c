//
//  File: %sys-bounce.h
//  Summary: "BOUNCE_CONTINUE, BOUNCE_DELEGATE, etc. and Helpers"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//


// 1. Generally speaking, generics (and most functions in the system) do
//    not work on antiforms, quasiforms, or quoted datatypes.
//
//    For one thing, this would introduce uncomfortable questions, like:
//    should the NEXT of ''[a b c] be [b c] or ''[b c] ?  This would take the
//    already staggering combinatorics of the system up a notch by forcing
//    "quote propagation" policies to be injected everywhere.
//
//    Yet there's another danger: if quoted/quasi items wind up giving an
//    answer instead of an error for lots of functions, this will lead to
//    carelessness in propagation of the marks...not stripping them off when
//    they aren't needed.  This would lead to an undisciplined hodgepodge of
//    marks that are effectively meaningless.  In addition to being ugly, that
//    limits the potential for using the marks intentionally in a dialect
//    later, if you're beholden to treating leaky quotes and quasis as if
//    they were not there.
//
INLINE Bounce Run_Generic_Dispatch(
    const Element* cue,
    Level* L,
    const Symbol* verb
){
    Heart heart = Cell_Heart_Ensure_Noquote(cue);  // no quoted/quasi/anti [1]

    GenericHook* hook = Generic_Hook_For_Heart(heart);
    return hook(L, verb);
}


// If Eval_Core gets back an REB_R_REDO from a dispatcher, it will re-execute
// the L->phase in the frame.  This function may be changed by the dispatcher
// from what was originally called.
//
// Note it is not safe to let arbitrary user code change values in a
// frame from expected types, and then let those reach an underlying native
// who thought the types had been checked.
//
#define C_REDO_UNCHECKED 'r'
#define BOUNCE_REDO_UNCHECKED \
    cast(Bounce, &PG_Bounce_Redo_Unchecked)

#define C_REDO_CHECKED 'R'
#define BOUNCE_REDO_CHECKED \
    cast(Bounce, &PG_Bounce_Redo_Checked)

#define C_DOWNSHIFTED 'd'
#define BOUNCE_DOWNSHIFTED \
    cast(Bounce, &PG_Bounce_Downshifted)


// Continuations are used to mitigate the problems that occur when the C stack
// contains a mirror of frames corresponding to the frames for each stack
// level.  Avoiding this means that routines that would be conceived as doing
// a recursion instead return to the evaluator with a new request.  This helps
// avoid crashes from C stack overflows and has many other advantages.  For a
// similar approach and explanation, see:
//
// https://en.wikipedia.org/wiki/Stackless_Python
//
// What happens is that when a BOUNCE_CONTINUE comes back via the C `return`
// for a native, that native's C stack variables are all gone.  But the heap
// allocated Level stays intact and in the Rebol stack trace.  The native's C
// function will be called back again when the continuation finishes.
//
#define C_CONTINUATION 'C'
#define BOUNCE_CONTINUE \
    cast(Bounce, &PG_Bounce_Continuation)


// A dispatcher may want to run a "continuation" but not be called back.
// This is referred to as delegation.
//
#define C_DELEGATION 'D'
#define BOUNCE_DELEGATE \
    cast(Bounce, &PG_Bounce_Delegation)


// For starters, a simple signal for suspending stacks in order to be able to
// try not using Asyncify (or at least not relying on it so heavily)
//
#define C_SUSPEND 'S'
#define BOUNCE_SUSPEND \
    cast(Bounce, &PG_Bounce_Suspend)


// Intrinsic typecheckers want to be able to run in the same Level as an
// action, but not overwrite the ->out cell of the level.  They motivate
// a special state for OKAY so that the L->out can be left as-is.
//
#define C_OKAY 'O'
#define BOUNCE_OKAY \
    cast(Bounce, &PG_Bounce_Okay)


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
    BOUNCE_CONTINUE)  /* ^-- don't heed result: want callback, push or not */

#define CONTINUE_CORE_4(...) ( \
    Pushed_Continuation(__VA_ARGS__, nullptr), \
    BOUNCE_CONTINUE)  /* ^-- don't heed result: want callback, push or not */

#define CONTINUE_CORE(...) \
    PP_CONCAT(CONTINUE_CORE_, PP_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define CONTINUE(out,...) \
    CONTINUE_CORE((out), LEVEL_MASK_NONE, SPECIFIED, __VA_ARGS__)

#define CONTINUE_BRANCH(out,...) \
    CONTINUE_CORE((out), LEVEL_FLAG_BRANCH, SPECIFIED, __VA_ARGS__)

INLINE Bounce Continue_Sublevel_Helper(Level* L, Level* sub) {
    assert(sub == TOP_LEVEL);  // currently sub must be pushed & top level
    UNUSED(sub);
    UNUSED(L);
    return BOUNCE_CONTINUE;
}

#define CONTINUE_SUBLEVEL(sub) \
    Continue_Sublevel_Helper(level_, (sub))


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
        (sub_flags) | (level_->flags.bits & LEVEL_FLAG_RAISED_RESULT_OK), \
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
    DELEGATE_CORE((out), LEVEL_FLAG_BRANCH, SPECIFIED, __VA_ARGS__)

#define DELEGATE_SUBLEVEL(sub) \
    (assert(Not_Executor_Flag(ACTION, level_, DISPATCHER_CATCHES)), \
        Continue_Sublevel_Helper(level_, (sub)), \
        BOUNCE_DELEGATE)
