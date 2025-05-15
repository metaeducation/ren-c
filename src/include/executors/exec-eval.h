//
//  file: %exec-eval.h
//  summary: "Flags and State for Evaluator_Executor() and Meta_Stepper_Executor()"
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

#define EXECUTOR_EVAL &Meta_Stepper_Executor  // shorthand in Xxx_Executor_Flag()


//=//// EVAL_EXECUTOR_FLAG_24 /////////////////////////////////////////////=//
//
#define EVAL_EXECUTOR_FLAG_24 \
    LEVEL_FLAG_24


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
    LEVEL_FLAG_25


//=//// EVAL_EXECUTOR_FLAG_26 /////////////////////////////////////////////=//
//
#define EVAL_EXECUTOR_FLAG_26 \
    LEVEL_FLAG_26


//=//// EVAL_EXECUTOR_FLAG_27 /////////////////////////////////////////////=//
//
#define EVAL_EXECUTOR_FLAG_27 \
    LEVEL_FLAG_27


//=//// EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH //////////////////////////=//
//
// There is a contention between operators that want to quote their left hand
// side and ones that want to quote their right hand side.  The left hand side
// wins in order for things like `help default` to work.  But deciding on
// whether the left hand side should win or not if it's a PATH! is a tricky
// case, as one must evaluate the path to know if it winds up producing a
// right quoting action or not.
//
// So paths win automatically unless a special (rare) override is used.  But
// if that path doesn't end up being a right quoting operator, it's less
// confusing to give an error message informing the user to use >- vs. just
// make it appear there was no left hand side.
//
// There is a parallel flag in ACTION_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH
//
#define EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_PATH \
    LEVEL_FLAG_28


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
    LEVEL_FLAG_29


//=//// EVAL_EXECUTOR_FLAG_30 /////////////////////////////////////////////=//
//
#define EVAL_EXECUTOR_FLAG_30 \
    LEVEL_FLAG_30


//=//// EVAL_EXECUTOR_FLAG_INERT_OPTIMIZATION /////////////////////////////=//
//
// If ST_STEPPER_LOOKING_AHEAD is being used due to an inert optimization,
// this flag is set, so that the quoting machinery can realize the lookback
// quote is not actually too late.
//
#define EVAL_EXECUTOR_FLAG_INERT_OPTIMIZATION \
    LEVEL_FLAG_31


typedef struct {
    //
    // Invisibility is a critical feature in Ren-C:
    //
    //     >> 1 + 2 elide print "Invisibility is central to many things"
    //     Invisibility is central to many things
    //     == 3
    //
    // It was once accomplished with a BOUNCE_INVISIBILE that didn't actually
    // overwrite the previous output, but set a flag on the cell that could
    // be un-set to recover the value.  But this approach predated the
    // semantics of empty antiform blocks (VOID), and could no longer work.
    //
    // So unfortunately, the evaluator really does need to save the prior
    // value when doing multiple steps.  If not performing multiple steps,
    // then it can be used...though likely by the parent (e.g. an Action
    // Level that knows it's only requesting a single step could write
    // some value there if it needed to.)
    //
    Cell primed;

    Option(const Value*) current_gotten;

    // The error reporting machinery doesn't want where `index` is right now,
    // but where it was at the beginning of a single EVALUATE step.
    //
    // !!! With the conversion to using feeds, it doesn't seem anything is
    // using this field at time of writing.  It's not displaying the start
    // of the expression, just where it is--which is poor for debugging.
    // That should be fixed, along with general debugging design.
    //
    uintptr_t expr_index;

    Option(StackIndex) stackindex_circled;  // used only by multi-return
} EvaluatorExecutorState;


// The stepper publishes its internal states in this header file, so that
// a level can be made with e.g. `FLAG_STATE_BYTE(ST_STEPPER_REEVALUATING)`
// to start in various points of the evaluation process.  When doing so, be
// sure the expected level variables for that state are initialized.
//
typedef enum {
    ST_STEPPER_INITIAL_ENTRY = STATE_0,


  //=//// STEPPER STATES BELOW MAX_TYPE RESERVED FOR DATATYPE //////////////=//

    // The stepper uses TYPE_XXX types of the current cell being processed
    // for the STATE byte in those cases.  This is helpful for knowing what
    // the mode of an evaluator level is, and makes the value on hand for
    // easy use in the "hot" level header location.
    //
    // Since we can only evaluate Element, we start the stepper custom states
    // after MAX_TYPE_ELEMENT (inclusive, e.g. TYPE_QUOTED = MAX_TYPE_ELEMENT)

    ST_STEPPER_MAX_TYPE = MAX_TYPE_BYTE_ELEMENT,


  //=//// STEPPER STATES ABOVE MAX_TYPE ////////////////////////////////////=//

    ST_STEPPER_LOOKING_AHEAD,
    ST_STEPPER_REEVALUATING,
    ST_STEPPER_CALCULATING_INTRINSIC_ARG,

    ST_STEPPER_TIE_EVALUATING_RIGHT_SIDE,  // $ ("tie") sigil
    ST_STEPPER_LIFT_EVALUATING_RIGHT_SIDE,  // ^ ("lift") sigil

    ST_STEPPER_GET_WORD,
    ST_STEPPER_GET_TUPLE,
    ST_STEPPER_GENERIC_SET,
    ST_STEPPER_SET_BLOCK,
    ST_STEPPER_SET_GROUP,
} StepperState;

// There's a rule that the Level's OUT has to be fresh if it's in the
// initial state.  So if an evaluator Level gets reused, it needs to
// set the state back to zero each time.
//
#if RUNTIME_CHECKS
    #define ST_STEPPER_FINISHED_DEBUG  255
#endif
