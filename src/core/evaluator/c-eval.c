//
//  File: %c-eval.c
//  Summary: "Evaluator Executor for Stepping Array Thru to the End"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022-2024 Ren-C Open Source Contributors
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
// The Evaluator_Executor() simply calls Stepper_Executor() consecutively, and
// if the output is invisible (e.g. the result of a COMMENT, ELIDE, or a
// COMMA!) then it does not overwrite the previous output.  This is to
// facilitate features like this:
//
//    >> eval [1 + 2 comment "hi"]
//    == 3
//
// The 1 + 2 evaluated to 3.  If we merely called the Stepper_Executor()
// again on the same output cell, the comment would evaluate to an antiform
// empty pack (e.g. a NIHIL, ~[]~ antiform).  That would overwrite the 3.
// So the Evaluator_Executor() has a holding cell for the last result that it
// does not overwrite when invisible content comes along as the next value.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * The reason this isn't done with something like a DO_TO_END flag that
//   controls a mode of the Stepper_Executor() is so that the stepper can
//   have its own Level* in a debugger.  If it didn't have its own level,
//   then in order to keep alive it could not return a result to the
//   trampoline until it had reached the end.  Thus, a generalized debugger
//   watching for finalized outputs would only see one final output--instead
//   of watching one be synthesized for each step.
//
// * ...BUT a performance trick in Evaluator_Executor() is that if you're NOT
//   debugging, it can actually avoid making its own Level* structure.  It's
//   able to use the data stack for its holding cell of the last result, and
//   can actually just pass through to the Stepper_Executor().
//

#include "sys-core.h"


//
//  Evaluator_Executor: C
//
// 1. *Before* a level is created for Evaluator_Executor(), the creator should
//    push the "primed" value for what they want as a result if there
//    are no non-invisible evaluations.  It's important to do it before, so
//    that the pushed cell is not part of the level's baseline.  Right now the
//    only two things requested are nihil and void, so we can test for those.
//
// 2. As mentioned in the notes at the top of this file, the primary reason
//    for Evaluator_Executor() to be separate from Stepper_Executor() is to
//    facilitate a general debugging loop hooked into the Trampoline that
//    can see results generated at the granularity of a step.  But that
//    generalized debugger doesn't exist yet.  So to make sure that both the
//    "fast" no-Level mode and the "slow" added Level mode would be able to
//    toggle based on whether such a hypothetical debugger were turned on,
//    it sporadically spawns levels in the RUNTIME_CHECKS builds.
//
// 3. An idea was tried once where the error was not raised until a step was
//    shown to be non-invisible.  This would allow invisible evaluations to
//    come after an error and still fall out:
//
//        >> raised? (raise "some error" comment "invisible")
//        == ~true~  ; anti
//
//    However, this means you have to wait until you know if the next
//    evaluation is invisible to raise the error.  This means things don't
//    stop running soon enough:
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
//    invisible material after the raised error has to be sacrificed.
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
        assert(Is_Nihil(TOP_ATOM) or Is_Void(TOP));  // primed [1]
        goto initial_entry;

      default:
      #if RUNTIME_CHECKS
        if (L != TOP_LEVEL) {
            assert(STATE == ST_EVALUATOR_STEPPING);
            goto step_result_in_out;
        }
      #endif
        goto call_stepper_executor;  // callback on behalf of stepper
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Feed_At_End(L->feed)) {
        Copy_Cell(OUT, TOP_ATOM);
        return OUT;
    }

    if (SPORADICALLY(64)) {  // 1 out of 64 times, use sublevel if debug [2]
        Level* sub = Make_Level(
            &Stepper_Executor,
            L->feed,
            LEVEL_FLAG_RAISED_RESULT_OK
                | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
        );
        Push_Level_Freshen_Out_If_State_0(OUT, sub);
        STATE = ST_EVALUATOR_STEPPING;
        return CONTINUE_SUBLEVEL(sub);  // executors *must* catch
    }

    goto call_stepper_executor;

} new_step: {  ///////////////////////////////////////////////////////////////

  #if RUNTIME_CHECKS
    if (L != TOP_LEVEL) {  // detect if a sublevel was used [2]
        Reset_Evaluator_Freshen_Out(SUBLEVEL);
        return BOUNCE_CONTINUE;
    }
  #endif

    Reset_Evaluator_Freshen_Out(L);
    goto call_stepper_executor;

} call_stepper_executor: {  ////////////////////////////////////////////////

    assert(L == TOP_LEVEL);  // only do this when there's no sublevel

    Bounce bounce = Stepper_Executor(L);

    if (bounce == OUT)
        goto step_result_in_out;

    return bounce;

} step_result_in_out: {  /////////////////////////////////////////////////////

    if (Is_Elision(OUT))  {  // was something like an ELIDE, COMMENT, COMMA!
        if (Not_Feed_At_End(L->feed))
            goto new_step;  // leave previous result as-is on stack

        Move_Cell(OUT, TOP_ATOM);  // finished, so extract result from stack
        goto finished;
    }

    if (Is_Feed_At_End(L->feed))
        goto finished;  // OUT is not invisible, so it's the final result

    if (Is_Raised(OUT))   // raise errors synchronously if not at end [3]
        return FAIL(Cell_Error(OUT));

    Move_Cell(TOP_ATOM, OUT);  // make current result the preserved one
    goto new_step;

} finished: {  ///////////////////////////////////////////////////////////////

  #if RUNTIME_CHECKS
    if (L != TOP_LEVEL)  // detect if a sublevel was used [2]
        Drop_Level(SUBLEVEL);
  #endif

    return OUT;
}}
