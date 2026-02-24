//
//  file: %exec-eval.h
//  summary: "Flags and State for Evaluator_Executor() and Stepper_Executor()"
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

#define EXECUTOR_EVAL &Stepper_Executor  // shorthand in Xxx_Executor_Flag()


//=//// EVAL_EXECUTOR_FLAG_22 /////////////////////////////////////////////=//
//
#define EVAL_EXECUTOR_FLAG_22 \
    LEVEL_EXECUTOR_FLAG_22


//=//// EVAL_EXECUTOR_FLAG_23 /////////////////////////////////////////////=//
//
#define EVAL_EXECUTOR_FLAG_23 \
    LEVEL_EXECUTOR_FLAG_23


// There is no LEVEL_EXECUTOR_FLAG_24


//=//// EVAL_EXECUTOR_FLAG_FULFILLING_ARG /////////////////////////////////=//
//
// Deferred lookback operations need to know when they are dealing with an
// argument fulfillment for a function, e.g. `summation 1 2 3 |> 100` should
// be `(summation 1 2 3) |> 100` and not `summation 1 2 (3 |> 100)`.  This
// also means that `add 1 <| 2` will act as an error.
//
// Note: There is ACTION_EXECUTOR_FLAG_FULFILLING_ARG which matches this.
//
#define EVAL_EXECUTOR_FLAG_FULFILLING_ARG \
    LEVEL_EXECUTOR_FLAG_25


//=//// EVAL_EXECUTOR_FLAG_26 /////////////////////////////////////////////=//
//
#define EVAL_EXECUTOR_FLAG_26 \
    LEVEL_EXECUTOR_FLAG_26


//=//// EVAL_EXECUTOR_FLAG_27 /////////////////////////////////////////////=//
//
#define EVAL_EXECUTOR_FLAG_27 \
    LEVEL_EXECUTOR_FLAG_27


//=//// EVAL_EXECUTOR_FLAG_OUT_IS_DISCARDABLE /////////////////////////////=//
//
// We want to know when evaluations without side-effects are discarded, as
// that is indicative of an error.  Like this IF missing its ELSE:
//
//     >> if 1 = 1 [print "then"] [print "else"] print "Woo!"
//     then
//     Woo!
//     ** PANIC: Pure evaluation product discarded: [print "else"]
//
// So discardability is implemented as a flag on evaluator Levels.  But since
// it's not traced on a per-Cell basis (like a generic CELL_FLAG_DISCARDABLE)
// this means a function's internal mechanics are irrelevant...values that
// are synthesized by a function are considered discardable:
//
//     >> if 1 = 1 [if 2 = 2 [print "then"] [print "else"]] print "Boo!"
//     then
//     Boo!
//
// The can of worms opened up by a generic CELL_FLAG_DISCARDABLE would be big.
// Forwarding and triaging a flag that by-definition vanishes on assignments
// is essentially a problem already addressed by unstable antiforms (like
// FAILURE!).  It's too much for average non-error-handling code to have to
// be concerned with.  So this is just a low-impact "light" and helpful
// feature that doesn't leak its concerns beyond individual EVAL:STEPs.
//
// Note: Bit chosen to match CELL_FLAG_PRIMED_NOTE_DISCARDABLE
//
#define EVAL_EXECUTOR_FLAG_OUT_IS_DISCARDABLE \
    LEVEL_EXECUTOR_FLAG_28_ALSO_CELL_FLAG_NOTE


//=//// EVAL_EXECUTOR_FLAG_NO_RESIDUE /////////////////////////////////////=//
//
// Sometimes a single step evaluation is done in which it would be considered
// an error if all of the arguments are not used.  This requests an error if
// the feed does not reach the end.
//
// !!! Interactions with ELIDE won't currently work with this, so evaluation
// would have to take this into account to greedily run ELIDEs if the flag
// is set.  However, it's only used in variadic apply at the moment with
// calls from the system that do not use ELIDE.  These calls may someday
// turn into rebValue(), in which case the mechanism would need rethinking.
//
// !!! A userspace tool for doing this was once conceived as `||`, which
// was variadic and would only allow one evaluation step after it, after
// which it would need to reach either an END or another `||`.
//
#define EVAL_EXECUTOR_FLAG_NO_RESIDUE \
    LEVEL_EXECUTOR_FLAG_29


//=//// EVAL_EXECUTOR_FLAG_30 /////////////////////////////////////////////=//
//
#define EVAL_EXECUTOR_FLAG_30 \
    LEVEL_EXECUTOR_FLAG_30


//=//// EVAL_EXECUTOR_FLAG_31 /////////////////////////////////////////////=//
//
#define EVAL_EXECUTOR_FLAG_31 \
    LEVEL_EXECUTOR_FLAG_31


typedef struct EvaluatorExecutorStateStruct {

  // Invisibility is a critical feature in Ren-C:
  //
  //     >> 1 + 2 elide print "Invisibility is central to many things"
  //     Invisibility is central to many things
  //     == 3
  //
  // It was once accomplished with a BOUNCE_INVISIBILE that didn't actually
  // overwrite the OUT cell, but set the BASE_FLAG_UNREADABLE as an indicator
  // of voidness.  The bit could be un-set to recover the original data.  But
  // this was very circuitous.
  //
  // Hence the evaluator really does need to save the prior value when doing
  // multiple steps.  It uses the PRIMED cell for this.
  //
  // Note that if using Stepper_Executor() and not Evaluator_Executor() then
  // the primed cell is available for other usages.

    Value primed;

  // The error reporting machinery doesn't want where `index` is right now,
  // but where it was at the beginning of a single EVALUATE step.
  //
  // !!! With the conversion to using feeds, it doesn't seem anything is
  // using this field at time of writing.  It's not displaying the start
  // of the expression, just where it is--which is poor for debugging.
  // That should be fixed, along with general debugging design.

    uintptr_t expr_index;

  // used only by multi-return

    Option(StackIndex) stackindex_circled;

} EvaluatorExecutorState;


// The stepper publishes its internal states in this header file, so that
// a level can be made with e.g. `FLAG_STATE_BYTE(ST_STEPPER_REEVALUATING)`
// to start in various points of the evaluation process.  When doing so, be
// sure the expected level variables for that state are initialized.
//
typedef enum {
    ST_STEPPER_INITIAL_ENTRY = STATE_0,


  //=//// STEPPER STATES RESERVED FOR DATATYPE ////////////////////////////=//

    // The stepper uses TYPE_XXX types of the current cell being processed
    // for the STATE byte in those cases.  This is helpful for knowing what
    // the mode of an evaluator level is, and makes the value on hand for
    // easy use in the "hot" level header location.
    //
    // Since we only evaluate Element, start the stepper custom states *after*
    // MAX_TYPE_FUNDAMENTAL (quoted and quasiform types don't require
    // continuations so we don't need to worry about those states, either)

    ST_STEPPER_MAX_TYPE = i_cast(StateByte, MAX_TYPE_FUNDAMENTAL),


  //=//// STEPPER STATES ABOVE MAX_TYPE ////////////////////////////////////=//

    ST_STEPPER_LOOKING_AHEAD,
    ST_STEPPER_REEVALUATING,
    ST_STEPPER_FULFILLING_INTRINSIC_ARG,

    ST_STEPPER_BIND_OPERATOR,  // $ is the BIND operator
    ST_STEPPER_IDENTITY_OPERATOR,  // ^ is the IDENTITY operator

    ST_STEPPER_GET_WORD,
    ST_STEPPER_GET_TUPLE,
    ST_STEPPER_GENERIC_SET,
    ST_STEPPER_SET_BLOCK,
    ST_STEPPER_SET_GROUP,

    ST_STEPPER_NONZERO_STATE,  // sometimes just can't leave state at 0

    ST_STEPPER_RESERVED_FOR_EVALUATOR  // evaluator uses when has own Level
} StepperState;

// There's a rule that the Level's OUT has to be fresh if it's in the
// initial state.  So if an evaluator Level gets reused, it needs to
// set the state back to zero each time.
//
#if RUNTIME_CHECKS
    #define ST_STEPPER_FINISHED_DEBUG  255
#endif
