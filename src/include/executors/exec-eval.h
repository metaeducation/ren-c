//
//  File: %exec-eval.h
//  Summary: {Flags and Level State for Evaluator_Executor()}
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
// The executor state has to be defined in order to be used (easily) in the
// union of the Level.
//

#define EXECUTOR_EVAL &Evaluator_Executor  // shorthand in Xxx_Executor_Flag()


//=//// EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_TUPLE //////////////////////////=//
//
// There is a contention between operators that want to quote their left hand
// side and ones that want to quote their right hand side.  The left hand side
// wins in order for things like `help default` to work.  But deciding on
// whether the left hand side should win or not if it's a TUPLE! is a tricky
// case, as one must evaluate the tuple to know if it winds up producing a
// right quoting action or not.
//
// So tuples win automatically unless a special (rare) override is used.  But
// if that path doesn't end up being a right quoting operator, it's less
// confusing to give an error message informing the user to use -> vs. just
// make it appear there was no left hand side.
//
// There is a parallel flag in ACTION_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_TUPLE
//
#define EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_TUPLE \
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


//=//// EVAL_EXECUTOR_FLAG_NO_EVALUATIONS /////////////////////////////////=//
//
// It might seem strange to have an evaluator mode in which no evaluations are
// performed.  However, this simplifies the implementation of operators such
// as ANY and ALL, which wish to run in an "inert" mode:
//
//     >> any [1 + 2]
//     == 3
//
//     >> any @[1 + 2]
//     == 1
//
// Inert operations wind up costing a bit more because they're pushing a frame
// when it seems "they don't need to"; but it provides flexibility for code
// that sometimes wants to evaluate and sometimes doesn't, and also the
// /PREDICATE can change what the rules are so that inert items don't all
// count as true.
//
#define EVAL_EXECUTOR_FLAG_NO_EVALUATIONS \
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
// If ST_EVALUATOR_LOOKING_AHEAD is being used due to an inert optimization,
// this flag is set, so that the quoting machinery can realize the lookback
// quote is not actually too late.
//
#define EVAL_EXECUTOR_FLAG_INERT_OPTIMIZATION \
    LEVEL_FLAG_31


struct EvaluatorExecutorStateStruct {
    //
    // Unlike actions (which have as many GC-protected locals as they want
    // to make in a frame), the evaluator has only the SPARE.  It makes things
    // much more convenient to use the space where the action executor would
    // be putting things like [arg param key key_tail] for an extra GC-safe
    // slot to be used.  The GC explicitly checks for Eval_Executor() to know
    // if this needs to be marked.
    //
    // The main use of this cell is to store a copy of the current cell
    // being evaluated.  That can't be the feed's current cell, because the
    // evaluator has to seek ahead one unit to find lookback quoters, such
    // as `x: default [...]`, where DEFAULT wants to quote the X: to its
    // left.  An attempt was made to optimize this by multiplexing the OUT
    // cell for this purpose (after all, inert items want to wind up in the
    // output cell anyway).  But besides obfuscating the code, it was slower,
    // since the output cell involves a level of indirection to address.
    //
    Cell current;  // raw vs. derived class due to union/destructor combo

    Option(const Value*) current_gotten;

    char enfix_reevaluate;  // either 'Y' or 'N' (catches bugs)

    StackIndex stackindex_circled;  // used only by multi-return
};


// The evaluator publishes its internal states in this header file, so that
// a level can be made with e.g. `FLAG_STATE_BYTE(ST_EVALUATOR_REEVALUATING)`
// to start in various points of the evaluation process.  When doing so, be
// sure the expected level variables for that state are initialized.
//
enum {
    ST_EVALUATOR_INITIAL_ENTRY = STATE_0,

    // The evaluator uses REB_XXX types of the current cell being processed
    // for the STATE byte in those cases.  This is helpful for knowing what
    // the mode of an evaluator level is, and makes the value on hand for
    // easy use in the "hot" level header location.

    ST_EVALUATOR_LOOKING_AHEAD = 100,
    ST_EVALUATOR_REEVALUATING,
    ST_EVALUATOR_CALCULATING_INTRINSIC_ARG
};
