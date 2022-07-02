//
//  File: %exec-action.h
//  Summary: {Flags and Frame State for Action_Executor()}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
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
// The frame state has to be defined in order to be used (easily) in the
// union of the Reb_Frame.
//

#define EXECUTOR_ACTION &Action_Executor   // shorthand in Xxx_Executor_Flag()


//=//// ACTION_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH ////////////////////////=//
//
// See EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH for an explanation.
//
// !!! Does this need both an ACTION and EVAL executor flag?
//
#define ACTION_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH \
    FRAME_FLAG_24

STATIC_ASSERT(
    ACTION_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH
    == EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH
);


//=//// ACTION_EXECUTOR_FLAG_FULFILLING_ARG ///////////////////////////////=//
//
// Lined up with EVAL_EXECUTOR_FLAG_FULFILLING_ARG so they can inherit via
// a simple bitmask.
//
#define ACTION_EXECUTOR_FLAG_FULFILLING_ARG \
    FRAME_FLAG_25

STATIC_ASSERT(
    EVAL_EXECUTOR_FLAG_FULFILLING_ARG == ACTION_EXECUTOR_FLAG_FULFILLING_ARG
);


//=//// ACTION_EXECUTOR_FLAG_ERROR_ON_DEFERRED_ENFIX //////////////////////=//
//
// There are advanced features that "abuse" the evaluator, e.g. by making it
// create a specialization exemplar by example from a stream of code.  These
// cases are designed to operate in isolation, and are incompatible with the
// idea of enfix operations that stay pending in the evaluation queue, e.g.
//
//     match+ parse "aab" [some "a"] else [print "what should this do?"]
//
// MATCH+ is variadic, and in one step asks to make a frame from the right
// hand side.  But it's 99% likely intent of this was to attach the ELSE to
// the MATCH and not the PARSE.  That looks inconsistent, since the user
// imagines it's the evaluator running PARSE as a parameter to MATCH (vs.
// MATCH becoming the evaluator and running it).
//
// It would be technically possible to allow ELSE to bind to the MATCH in
// this case.  It might even be technically possible to give MATCH back a
// frame for a CHAIN of actions that starts with PARSE but includes the ELSE
// (which sounds interesting but crazy, considering that's not what people
// would want here, but maybe sometimes they would).
//
// The best answer for right now is just to raise an error.
//
#define ACTION_EXECUTOR_FLAG_ERROR_ON_DEFERRED_ENFIX \
    FRAME_FLAG_26


//=//// ACTION_EXECUTOR_FLAG_DELEGATE_CONTROL /////////////////////////////=//
//
// Action dispatchers don't really want to delegate control with R_DELEGATE,
// because the action wants to appear to be on the stack.  For some it's even
// more technically important--because the varlist must stay alive to be a
// specifier, so you can't Drop_Action() etc.  Something like a FUNC or LAMBDA
// cannot delegate to the body block if there is a variadic, because it will
// look like the function isn't running.
//
// So when a dipatcher tells Action_Executor() it wants BOUNCE_DELEGATE, it does
// not propagate that to the trampoline...it just sets this flag and returns
// a continuation.  (Note however, that using delegation has an optimization
// that does not return BOUNCE_DELEGATE, if something like a branch can be
// evaluated to a constant value!  This won't leave the frame on the stack).
//
#define ACTION_EXECUTOR_FLAG_DELEGATE_CONTROL \
    FRAME_FLAG_27


//=//// ACTION_EXECUTOR_FLAG_RUNNING_ENFIX ////////////////////////////////=//
//
// Due to the unusual influences of partial refinement specialization, a frame
// may wind up with its enfix parameter as being something like the last cell
// in the argument list...when it has to then go back and fill earlier args
// as normal.  There's no good place to hold the memory that one is doing an
// enfix fulfillment besides a bit on the frame itself.
//
// It is also used to indicate to a ST_EVALUATOR_REEVALUATING frame whether
// to run an ACTION! cell as enfix or not.  The reason this may be overridden
// on what's in the action can be seen in the DECLARE_NATIVE(shove) code.
//
#define ACTION_EXECUTOR_FLAG_RUNNING_ENFIX \
    FRAME_FLAG_28


//=//// ACTION_EXECUTOR_FLAG_DISPATCHER_CATCHES ///////////////////////////=//
//
// Every Executor() gets called with the chance to cleanup in the THROWING
// state.  But in the specific case of the Action_Executor(), it uses this
// flag to keep track of whether the dispatcher it is calling (a kind of
// "sub-executor") wants to be told about the thrown state.  This would be
// something like a WHILE loop wanting to catch a BREAK.
//
#define ACTION_EXECUTOR_FLAG_DISPATCHER_CATCHES \
    FRAME_FLAG_29


//=//// ACTION_EXECUTOR_FLAG_FULFILL_ONLY /////////////////////////////////=//
//
// In some scenarios, the desire is to fill up the frame but not actually run
// an action.  At one point this was done with a special "dummy" action to
// dodge having to check the flag on every dispatch.  But in the scheme of
// things, checking the flag is negligible...and it's better to do it with
// a flag so that one does not lose the paramlist information one was working
// with (overwriting with a dummy action on FRM_PHASE() led to an inconsistent
// case that had to be accounted for, since the dummy's arguments did not
// line up with the frame being filled).
//
#define ACTION_EXECUTOR_FLAG_FULFILL_ONLY \
    FRAME_FLAG_30


//=//// ACTION_EXECUTOR_FLAG_TYPECHECK_ONLY ///////////////////////////////=//
//
// This is used by <blank> to indicate that once the frame is fulfilled, the
// only thing that should be done is typechecking...don't run the action.
//
#define ACTION_EXECUTOR_FLAG_TYPECHECK_ONLY \
    FRAME_FLAG_31


struct Reb_Action_Executor_State {
    //
    // If a function call is currently in effect, FRM_PHASE() is how you get
    // at the current function being run.  This is the action that started
    // the process.
    //
    // Compositions of functions (adaptations, specializations, hijacks, etc)
    // update the FRAME!'s payload in the f->varlist archetype to say what
    // the current "phase" is.  The reason it is updated there instead of
    // as a frame field is because specifiers use it.  Similarly, that is
    // where the binding is stored.
    //
    Action(*) original;

    // When enumerating across the key/arg/param trios in unison, the length
    // of the keylist is used to dictate how far to go.
    //
    // If key and key_tail are equal, then that means the frame has been
    // fulfilled and its arguments are valid for use.
    //
    const REBKEY *key;
    const REBKEY *key_tail;

    // `arg is the "actual argument"...which holds the pointer to the
    // REBVAL slot in the `arglist` for that corresponding `param`.  These
    // are moved in sync.  This movement can be done for typechecking or
    // fulfillment, see In_Typecheck_Mode()
    //
    // If arguments are actually being fulfilled into the slots, those
    // slots start out as trash.  Yet the GC has access to the frame list,
    // so it can examine `arg` and avoid trying to protect the random
    // bits that haven't been fulfilled yet.
    //
    REBVAL *arg;

    // The param can either be a definition of a parameter and its types to
    // be fulfilled, or if it has been specialized with a value already then
    // that space is used to hold the specialized value cell.
    //
    const REBPAR *param;
};
