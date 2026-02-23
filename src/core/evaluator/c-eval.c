//
//  file: %c-eval.c
//  summary: "Evaluator Executor for Stepping Array Thru to the End"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022-2026 Ren-C Open Source Contributors
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
// The Evaluator_Executor() just calls Stepper_Executor() consecutively, and
// if the output is invisible (e.g. the result of a COMMENT, ELIDE, ()) it
// won't overwrite the previous output.
//
// This facilitates features like:
//
//    >> eval [1 + 2 comment "hi"]
//    == 3
//
// The 1 + 2 evaluated to 3.  If we called the Stepper_Executor() directly
// again on the same output cell, the comment would write a VOID! cell there.
// That would overwrite the 3.  So the Evaluator_Executor() has a holding cell
// ("PRIMED") for the last result that it does not overwrite when invisible
// content comes along as the next value.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. The reason Evaluator_Executor() exists and isn't just a modality of the
//    Stepper_Executor() (e.g. something like a DO_TO_END flag) is so there
//    can be a mode where the stepper has its own Level in a debugger.  If it
//    couldn't do that, then in order to keep alive it could not return a
//    result to the Trampoline until it reached the end.  Thus, a generalized
//    debugger watching for finalized outputs would only see one final output,
//    vs. watching one be synthesized for each step.
//
//    ...BUT a performance trick in Evaluator_Executor() is that if you're NOT
//    debugging, it can actually avoid a separate Level for the stepper.  It's
//    able to use a Cell in the Level structure for its holding of the last
//    result, and call the Stepper_Executor() on the same Level* (`L`).
//

#include "sys-core.h"


#define PRIMED  (&L->u.eval.primed)


// The Stepper keeps track of whether its output Cell is discardable or not.
// We have to do the next eval to know if it's going to be invisible (and
// if it is, then the previous result may not count as discarded):
//
//   >> if 1 = 1 [print "then"] [print "else"] elide print "Vanish!"
//   then
//   Vanish!
//   == [print "else"]  ; would have been an error if PRINT not ELIDE'd
//
// So we need to save the DISCARDABLE flag somewhere.  The PRIMED cell is a
// convenient place, but maybe there's a cheaper way to do it?
//
// Note: Bit chosen to match EVAL_EXECUTOR_FLAG_OUT_IS_DISCARDABLE
//
#define CELL_FLAG_PRIMED_NOTE_DISCARDABLE  CELL_FLAG_NOTE


//
//  Evaluator_Executor: C
//
Bounce Evaluator_Executor(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    if (THROWING)
        return THROWN;  // no state to clean up

    Level* stepper;  // Level w/Stepper_Executor() may be same as L [A]

    enum {
        ST_EVALUATOR_INITIAL_ENTRY = STATE_0,
        ST_EVALUATOR_STEPPING_IN_SUBLEVEL = ST_STEPPER_RESERVED_FOR_EVALUATOR
    };

  dispatch_on_state: {

  // When the Evaluator_Executor() and Stepper_Executor() are running in the
  // same Level, the STATE byte will be one of the ST_STEPPER_XXX states.
  // But one state is reserved by the Stepper to say the Evaluator_Executor()
  // has its own Level.
  //
  // 1. If Stepper_Executor() pushes levels with TRAMPOLINE_KEEPALIVE, then
  //    the `L` that Evaluator_Executor() gets might not be the prior of
  //    TOP_LEVEL.  Climbing the stack is very slightly "inefficient" but the
  //    optimized case of not having a sublevel doesn't pay that "cost".

    switch (STATE) {
      case ST_EVALUATOR_INITIAL_ENTRY:
        goto initial_entry;

      case ST_EVALUATOR_STEPPING_IN_SUBLEVEL: {
        possibly(TOP_LEVEL->prior != L);  // if stepper pushed a KEEPALIVE [1]
        stepper = TOP_LEVEL;
        while (stepper->prior != L)
            stepper = stepper->prior;
        goto step_done_with_result_in_out; }

      default:  // ST_STEPPER_XXX states (Stepper_Executor() using same Level)
        stepper = L;
        goto call_stepper_executor_directly;
    }

} initial_entry: {  //////////////////////////////////////////////////////////

    possibly(L != TOP_LEVEL);
    possibly(Get_Level_Flag(L, TRAMPOLINE_KEEPALIVE));

    possibly(Get_Level_Flag(L, VANISHABLE_VOIDS_ONLY));  // see flag definition

    unnecessary(Force_Erase_Cell(PRIMED));  // !!! Prep_Level() does this ATM
    Init_Blank_Untracked(
        PRIMED,
        FLAG_LIFT_BYTE(TYPE_VOID)
            | CELL_FLAG_PRIMED_NOTE_DISCARDABLE
    );
    assert(Is_Void(PRIMED));

    Sync_Feed_At_Cell_Or_End_May_Panic(L->feed);

    if (In_Debug_Mode(64)) {
        require (
          stepper = Make_Level(  // sublevel to hook steps, see [A]
            &Stepper_Executor,
            L->feed,
            LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
                | (L->flags.bits & LEVEL_FLAG_VANISHABLE_VOIDS_ONLY)  // [B]
        ));
        definitely(Is_Cell_Erased(OUT));  // we are in STATE_0
        Push_Level(OUT, stepper);

        STATE = ST_EVALUATOR_STEPPING_IN_SUBLEVEL;  // before `goto finished;`

        if (Is_Level_At_End(stepper)) {
            Init_Void(OUT);
            goto finished;
        }

        inapplicable(  // Executor, not a Dispatcher (always catches throws)
            Enable_Dispatcher_Catching_Of_Throws(stepper)
        );
        return CONTINUE_SUBLEVEL;
    }

    stepper = L;

    if (Is_Level_At_End(L)) {
        Init_Void(OUT);
        goto finished;
    }

    goto call_stepper_executor_directly;

} call_stepper_executor_directly: {  /////////////////////////////////////////

    assert(stepper == L);

    Bounce bounce = Stepper_Executor(L);

    if (bounce == OUT)  // completed step, possibly reached end of feed [1]
        goto step_done_with_result_in_out;

    return bounce;  // continuation/throw/etc. on behalf of the stepper

} step_done_with_result_in_out: {  ///////////////////////////////////////////

  // 1. Note that unless a function is declared as VANISHABLE, any VOID! it
  //    tries to return will be converted to empty PACK! for safety when in
  //    an evaluation step with LEVEL_FLAG_VANISHABLE_VOIDS_ONLY.  You have to
  //    override this explicitly with the `^` operator when you actually want
  //    a void...whether it's from a non-vanishable function or just a
  //    quasiform or ^META variable fetch.
  //
  // 2. It may seem desirable to allow invisible evaluations to come after
  //    a FAILURE!, permitting things like:
  //
  //        >> failure? (fail "some error" comment "invisible")
  //        == \~okay~\  ; anti
  //
  //    But consider things like:
  //
  //        call "shell command"  ; FAILURE! if nonzero exit code
  //        call "shell command critically dependent on prior's success"
  //
  //    You can't wait until you know if the next evaluation is invisible to
  //    escalate FAILURE! to panic.  Things don't stop running soon enough.

    if (Is_Void(OUT)) {  // ELIDE, COMMENT, (^ EVAL []) etc. [1]
        if (Is_Level_At_End(stepper)) {
            Move_Cell(OUT, PRIMED);
            goto finished;
        }
        goto start_new_step;  // leave previous result as-is in PRIMED
    }

    if (
        Not_Cell_Flag(PRIMED, PRIMED_NOTE_DISCARDABLE)  // see flag define
        and not Is_Trash(PRIMED)  // ~<trash>~ can be discarded, no error!
    ){
      #if RUNTIME_CHECKS
        Value* primed = PRIMED;  // easier to inspect in C watchlist
        USED(primed);
      #endif
        panic (Error_Discarded_Value_Raw(PRIMED));
    }

    possibly(Get_Level_Flag(stepper, VANISHABLE_VOIDS_ONLY));
    Set_Level_Flag(stepper, VANISHABLE_VOIDS_ONLY);  // always set now [B]

    if (Is_Level_At_End(stepper)) {
        possibly(Is_Failure(OUT));
        goto finished;
    }

    require (  // panic if failure seen before final step [2]
      Ensure_No_Failures_Including_In_Packs(OUT)
    );

    Move_Cell(PRIMED, OUT);  // make current result the preserved one

    STATIC_ASSERT(
        EVAL_EXECUTOR_FLAG_OUT_IS_DISCARDABLE
            == CELL_FLAG_PRIMED_NOTE_DISCARDABLE
    );
    PRIMED->header.bits |= (  // remember discardability as bit on PRIMED
        stepper->flags.bits & EVAL_EXECUTOR_FLAG_OUT_IS_DISCARDABLE
    );

    goto start_new_step;

} start_new_step: {  /////////////////////////////////////////////////////////

  // 1. The STATE byte has to be reset to zero on each evaluator step.  But
  //    if sharing a Level, you only want the Evaluator_Executor()'s code for
  //    `initial_entry:` to run once.  The trick is that when the stepper is
  //    reset, we immediately calls the stepper again, before returning to
  //    the trampoline.  So it never re-enters with zero.

    Reset_Stepper_Erase_Out(stepper);

    if (STATE == ST_EVALUATOR_STEPPING_IN_SUBLEVEL)
        return BOUNCE_CONTINUE;

    definitely(STATE == STATE_0);  // `initial_entry` won't see zero again [1]
    goto call_stepper_executor_directly;  // ...Stepper_Executor() changes it

} finished: {  ///////////////////////////////////////////////////////////////

  // 1. We want error parity in ((1) print "HI") with (1 print "Hi"), and this
  //    is accomplished by examining the discardability bit on the Level for
  //    the overall step.  But since this bit is tracked by the Stepper, if
  //    the Evaluator_Executor() has its own Level we have to copy the bit.

    if (STATE == ST_EVALUATOR_STEPPING_IN_SUBLEVEL) {
        L->flags.bits |= (  // proxy from stepper to evaluator [1]
            stepper->flags.bits & EVAL_EXECUTOR_FLAG_OUT_IS_DISCARDABLE
        );
        Drop_Level(stepper);
    }

    if (Get_Level_Flag(L, FORCE_HEAVY_BRANCH))
        Force_Cell_Heavy(L->out);

    return OUT;
}}


//
//  Startup_Evaluator: C
//
// The evaluator allows you to override some aspects of its functionality,
// such as what happens to evaluate FENCE!.  This is a novel new concept.
//
// But FENCE! typically runs WRAP.
//
void Startup_Evaluator(void)
{
    Copy_Cell(Sink_LIB(FENCE_X_EVAL), LIB(AS_BLOCK_WRAP));
}
