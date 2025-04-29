//
//  file: %exec-action.h
//  summary: "Flags and Level State for Action_Executor()"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// The executor state has to be defined in order to be used (easily) in the
// union of the Level.
//

#define EXECUTOR_ACTION &Action_Executor   // shorthand in Xxx_Executor_Flag()


//=//// ACTION_EXECUTOR_FLAG_DOING_PICKUPS ////////////////////////////////=//
//
// If actions are invoked via path and use refinements in a different order
// from how they appear in the frame's parameter definition, then the arguments
// at the callsite can't be gathered in sequence.  Revisiting will be
// necessary.  This flag is set while they are revisited, which is important
// for Action_Executor() to know -and- the GC...since it means it must protect
// *all* of the arguments--not just up thru `key`.
//
// Note: It was tried to do this with ST_ACTION_DOING_PICKUPS as a state byte,
// which are not as scarce as executor flags.  But that overwrote the case
// of ST_ACTION_FULFILLING_INFIX_FROM_OUT, and sometimes the infix argument
// is actually a pickup (e.g. a refinement specialized to be the first
// ordinary argument).  There's a good reason for INFIX_FROM_OUT to be a state
// byte, so this moved to being a flag.
//
// Note: This flag only applies when not IN_DISPATCH, so could have a distinct
// meaning during dispatch if desired (e.g. DELEGATE_CONTROL)
//
#define ACTION_EXECUTOR_FLAG_DOING_PICKUPS \
    LEVEL_FLAG_5   // !!! temporary, was LEVEL_FLAG_25


//=//// ACTION_EXECUTOR_FLAG_ERROR_ON_DEFERRED_INFIX //////////////////////=//
//
// !!! TEMPORARILY DISABLED (defined to 0) - SHORT ON FLAGS AND NEED FOR
// ANOTHER MORE IMPORTANT PURPOSE - KEPT AS DOCUMENTATION !!!
//
// There are advanced features that "abuse" the evaluator, e.g. by making it
// create a specialization exemplar by example from a stream of code.  These
// cases are designed to operate in isolation, and are incompatible with the
// idea of infix operations that stay pending in the evaluation queue, e.g.
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
#define ACTION_EXECUTOR_FLAG_ERROR_ON_DEFERRED_INFIX \
    0  // !!! DISABLED FOR NOW, BUT CALLSITES CAN STILL REFERENCE


//=//// ACTION_EXECUTOR_FLAG_FULFILL_ONLY /////////////////////////////////=//
//
// In some scenarios, the desire is to fill up the frame but not actually run
// an action.  At one point this was done with a special "dummy" action to
// dodge having to check the flag on every dispatch.  But in the scheme of
// things, checking the flag is negligible...and it's better to do it with
// a flag so that one does not lose the paramlist information one was working
// with (overwriting with a dummy action on Level_Phase() led to an inconsistent
// case that had to be accounted for, since the dummy's arguments did not
// line up with the frame being filled).
//
#define ACTION_EXECUTOR_FLAG_FULFILL_ONLY \
    LEVEL_FLAG_24


//=//// ACTION_EXECUTOR_FLAG_TYPECHECK_ONLY ///////////////////////////////=//
//
// This is used by <maybe> to indicate that once the frame is fulfilled, the
// only thing that should be done is typechecking...don't run the action.
//
#define ACTION_EXECUTOR_FLAG_TYPECHECK_ONLY \
    LEVEL_FLAG_25


//=//// ACTION_EXECUTOR_FLAG_IN_DISPATCH //////////////////////////////////=//
//
// When the action dispatcher is fulfilling arguments, it needs frame state
// for tracking the current key + argument + parameter.  During that time it
// can also use the STATE byte in the frame for arbitrary purposes.  But once
// it starts running dispatch it has to leave the byte to the dispatcher.
//
#define ACTION_EXECUTOR_FLAG_IN_DISPATCH \
    LEVEL_FLAG_26

#define Is_Level_Fulfilling(L) \
    Not_Executor_Flag(ACTION, L, IN_DISPATCH)

#define Is_Level_Dispatching(L) \
    Get_Executor_Flag(ACTION, L, IN_DISPATCH)


//=//// ACTION_EXECUTOR_FLAG_DELEGATE_CONTROL /////////////////////////////=//
//
// Action dispatchers don't really want to delegate control with R_DELEGATE,
// because the action wants to appear to be on the stack.  For some it's even
// more technically important--because the varlist must stay alive to be a
// binding, so you can't Drop_Action() etc.  Something like a FUNC or LAMBDA
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
    LEVEL_FLAG_27


//=//// ACTION_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH ////////////////////////=//
//
// See EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH for an explanation.
//
// !!! Does this need both an ACTION and EVAL executor flag?
//
#define ACTION_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH \
    LEVEL_FLAG_28

STATIC_ASSERT(
    ACTION_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH
    == EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH
);


//=//// ACTION_EXECUTOR_FLAG_DISPATCHER_CATCHES ///////////////////////////=//
//
// Every Executor() gets called with the chance to cleanup in the THROWING
// state.  But in the specific case of the Action_Executor(), it uses this
// flag to keep track of whether the dispatcher it is calling (a kind of
// "sub-executor") wants to be told about the thrown state.
//
// This would be for something like a WHILE loop wanting to catch a BREAK,
// or something like FOR-EACH wanting to get notified if a fail() happens
// so it can clean up its iteration state.  (These failures could be emitted
// from the dispatcher itself, so it could `return FAIL()` and then the
// trampoline turns right around and calls the dispatcher that just returned
// with the thrown state.  This helps make it so you only have to write one
// bit of cleanup code that works for both longjmp()/C++-throw based failures
// as well as `return FAIL()` cases.
//
#define ACTION_EXECUTOR_FLAG_DISPATCHER_CATCHES \
    LEVEL_FLAG_29


//=//// ACTION_EXECUTOR_FLAG_INFIX_A //////////////////////////////////////=//
//
// Due to the unusual influences of partial refinement specialization, a frame
// may wind up with its infix parameter as being something like the last cell
// in the argument list...when it has to then go back and fill earlier args
// as normal.  There's no good place to hold the memory that one is doing an
// infix fulfillment besides a bit on the frame itself.
//
// It is also used to indicate to a ST_STEPPER_REEVALUATING frame whether
// to run an ACTION! cell as infix or not.  The reason this may be overridden
// on what's in the action can be seen in the DECLARE_NATIVE(SHOVE) code.
//
#define ACTION_EXECUTOR_FLAG_INFIX_A \
    LEVEL_FLAG_30


//=//// ACTION_EXECUTOR_FLAG_INFIX_B //////////////////////////////////////=//
//
#define ACTION_EXECUTOR_FLAG_INFIX_B \
    LEVEL_FLAG_31


typedef struct {
    //
    // If a function call is currently in effect, Level_Phase() is how you get
    // at the current function being run.  This is the action that started
    // the process.
    //
    // Compositions of functions (adaptations, specializations, hijacks, etc)
    // update the FRAME!'s payload in the L->varlist archetype to say what
    // the current "phase" is.  The reason it is updated there instead of
    // as a frame field is because bindings use it.  Similarly, that is
    // where the binding is stored.
    //
    Phase* original;

    // Functions don't have "names", though they can be assigned to words.
    // However, not all function invocations are through words or paths, so
    // the label may not be known.  Mechanics with labeling try to make sure
    // that *some* name is known, but a few cases can't be, e.g.:
    //
    //     run func [x] [print "This function never got a label"]
    //
    Option(const Symbol*) label;

    // When enumerating across the key/arg/param trios in unison, the length
    // of the keylist is used to dictate how far to go.
    //
    // If key and key_tail are equal, then that means the frame has been
    // fulfilled and its arguments are valid for use.
    //
    const Key* key;
    const Key* key_tail;

    // `arg is the "actual argument"...which holds the pointer to the
    // cell slot in the `arglist` for that corresponding `param`.  These
    // are moved in sync.  This movement can be done for typechecking or
    // fulfillment, see In_Typecheck_Mode()
    //
    // While ultimately the arguments will be Value* and not able to hold
    // unstable isotopes arguments, the process of argument fulfillment will
    // hold unstable isotopes temporarily.
    //
    Atom* arg;

    // The param can either be a definition of a parameter and its types to
    // be fulfilled, or if it has been specialized with a value already then
    // that space is used to hold the specialized value cell.
    //
    const Param* param;
} ActionExecutorState;


enum {
    ST_ACTION_INITIAL_ENTRY = STATE_0,

    ST_ACTION_FULFILLING_ARGS = 100,  // weird number if dispatcher gets it

    // Using the state byte to convey the next argument should come from OUT
    // serves an additional purpose, because STATE_0 would mean that OUT has
    // to be stale.  This allows the caller to subvert that rule as well as
    // have the infix-from-out signal without needing a separate flag.
    //
    ST_ACTION_INITIAL_ENTRY_INFIX,
    ST_ACTION_FULFILLING_INFIX_FROM_OUT,

    // While some special-purpose functions intentionally receive barrier, most
    // don't want to...so we can treat it as an expression barrier--whether
    // it's produced by a COMMA! evaluating, or otherwise.
    //
    //    foo: func [x [integer! <end>]] [...]
    //
    //    (foo,)  ; sees an end
    //    (foo anti ',)  ; also sees an end
    //
    //    bar: func [^y [barrier! integer!]] [...]
    //
    //    (bar,)  ; sees an barrier antiform ~,~
    //    (bar anti ',)  ; same
    //
    ST_ACTION_BARRIER_HIT,

    ST_ACTION_TYPECHECKING

    // Note: There is no ST_ACTION_DISPATCHING, because if an action is
    // dispatching, the STATE byte belongs to the dispatcher.  Detecting the
    // state of being in dispatch is (`key` == `key_tail`), which tells you
    // that argument enumeration is finished.
};
