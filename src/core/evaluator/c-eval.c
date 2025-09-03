//
//  file: %c-eval.c
//  summary: "Evaluator Executor for Stepping Array Thru to the End"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022-2025 Ren-C Open Source Contributors
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
// The Evaluator_Executor() just calls Stepper_Executor() consecutively,
// and if the output is invisible (e.g. the result of a COMMENT, ELIDE, etc.)
// it won't overwrite the previous output.
//
// This facilitates features like:
//
//    >> eval [1 + 2 comment "hi"]
//    == 3
//
// The 1 + 2 evaluated to 3.  If we merely called the Stepper_Executor()
// again on the same output cell, the comment would evaluate to an antiform
// comma (e.g. a GHOST, ~,~ antiform).  That would overwrite the 3.  So the
// Evaluator_Executor() has a holding cell for the last result that it does
// not overwrite when invisible content comes along as the next value.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. The reason this isn't done with something like a DO_TO_END flag that
//    controls a mode of the Stepper_Executor() is so the stepper can
//    have its own Level in a debugger.  If it didn't have its own Level,
//    then in order to keep alive it could not return a result to the
//    Trampoline until it had reached the end.  Thus, a generalized debugger
//    watching for finalized outputs would only see one final output--instead
//    of watching one be synthesized for each step.
//
//    ...BUT a performance trick in Evaluator_Executor() is that if you're NOT
//    debugging, it can actually avoid making its own Level structure.  It's
//    able to use a Cell in the Level structure for its holding of the last
//    result, and actually just passes through to the Stepper_Executor().
//
// B. !!! A debugger that was giving insights into the steps would show the
//    steps being meta-values, and giving a "trash" to indicate the end of
//    the stepping.  As a UI concern for the debugger, this might be a little
//    bit confusing for people to wonder why the steps are always ^META.
//    The debugger might want to customize the display based on the executor.
//

#include "sys-core.h"


#define PRIMED  cast(Atom*, &L->u.eval.primed)


static bool Using_Sublevel_For_Stepping(Level* L) {  // see [A]
    if (L == TOP_LEVEL)
        return false;
    assert(TOP_LEVEL->prior == L);
    assert(TOP_LEVEL->executor == &Stepper_Executor);
    return true;
}


//
//  Evaluator_Executor: C
//
// 1. *Before* a level is created for Evaluator_Executor(), the creator should
//    set the "primed" value for what they want as a result if there
//    are no non-invisible evaluations.  Right now the only two things
//    requested are VOID and GHOST, so we can test for those.
//
Bounce Evaluator_Executor(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    enum {
        ST_EVALUATOR_INITIAL_ENTRY = STATE_0,
        ST_EVALUATOR_STEPPING
    };

    if (THROWING)
        return THROWN;  // no state to clean up

    switch (STATE) {
      case ST_EVALUATOR_INITIAL_ENTRY:
        assert(Not_Level_Flag(L, TRAMPOLINE_KEEPALIVE));
        assert(Is_Void(PRIMED) or Is_Ghost(PRIMED));  // primed [1]
        goto initial_entry;

      default:
        if (Using_Sublevel_For_Stepping(L)) {
            assert(STATE == ST_EVALUATOR_STEPPING);
            goto step_done_with_dual_in_out;
        }
        goto call_stepper_executor_directly;  // callback on behalf of stepper
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (In_Debug_Mode(64)) {
        require (
          Level* sub = Make_Level(  // sublevel to hook steps, see [A]
            &Stepper_Executor,
            L->feed,
            LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
        ));
        Push_Level_Erase_Out_If_State_0(OUT, sub);
        STATE = ST_EVALUATOR_STEPPING;
        return CONTINUE_SUBLEVEL(sub);  // executors *must* catch
    }

    goto call_stepper_executor_directly;

} start_new_step: {  /////////////////////////////////////////////////////////

    if (Try_Is_Level_At_End_Optimization(L))
        goto finished;

    if (Using_Sublevel_For_Stepping(L)) {
        Reset_Evaluator_Erase_Out(SUBLEVEL);
        return BOUNCE_CONTINUE;
    }

    Reset_Evaluator_Erase_Out(L);
    goto call_stepper_executor_directly;

} call_stepper_executor_directly: {  /////////////////////////////////////////

    assert(not Using_Sublevel_For_Stepping(L));

    Bounce bounce = Stepper_Executor(L);

    if (bounce == OUT)  // completed step, possibly reached end of feed
        goto step_done_with_dual_in_out;

    return bounce;  // requesting a continuation on behalf of the stepper

} step_done_with_dual_in_out: {  /////////////////////////////////////////////

    // 1. See CELL_FLAG_OUT_HINT_UNSURPRISING for a full explanation of this.
    //    But what's going on is that if a function doesn't return GHOST!
    //    all the time, it gets flagged as "surprising"... you need to use
    //    the `^` to get the GHOST! out of it.
    //
    // 2. An idea was tried once where the error wasn't panicked until a step
    //    was shown to be non-invisible.  This would allow invisible
    //    evaluations to come after an error and still fall out:
    //
    //        >> error? (fail "some error" comment "invisible")
    //        == ~true~  ; anti
    //
    //    However, this means you have to wait until you know if the next
    //    evaluation is invisible to panic.  This means things don't stop
    //    running soon enough:
    //
    //        >> data: []
    //
    //        >> take [] append data 'a
    //        ** Error: Can't take from empty block
    //
    //        >> data
    //        == [a]
    //
    //    That's a bad enough outcome that the feature of being able to put
    //    invisible material after the error has to be sacrificed.

    if (Is_Endlike_Unset(OUT))  // the "official" way to detect reaching end
        goto finished;

    if (Is_Ghost(OUT)) { // something like an ELIDE or COMMENT
        if (Get_Cell_Flag(OUT, OUT_HINT_UNSURPRISING))
            goto start_new_step;  // leave previous result as-is in PRIMED

        Init_Surprising_Ghost(PRIMED);  // (use ^ to be "UNAFRAID") [1]
        goto start_new_step;
    }

    Move_Atom(PRIMED, OUT);  // make current result the preserved one

    dont(Try_Is_Level_At_End_Optimization(L));  // (fail x,) must error
    if (Is_Feed_At_End(L->feed))
        goto finished;

    require (  // panic if error seen before last step [2]
      Elide_Unless_Error_Including_In_Packs(PRIMED)
    );

    goto start_new_step;

} finished: {  ///////////////////////////////////////////////////////////////

    if (Using_Sublevel_For_Stepping(L))
        Drop_Level(SUBLEVEL);

    Move_Atom(OUT, PRIMED);
    return OUT;
}}


//
//  Startup_Evaluator: C
//
// The evaluator allows you to override some aspects of its functionality,
// such as what happens to evaluate FENCE!.  This is a novel new concept.
//
void Startup_Evaluator(void)
{
    Copy_Cell(Sink_Lib_Var(SYM_FENCE_X_EVAL), LIB(CONSTRUCT));
}
