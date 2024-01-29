//
//  File: %c-stepper.c
//  Summary: "Array Stepper Executor"
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
// The Stepper_Executor() simply calls Evaluator_Executor() consecutively, and
// if the output is invisible (e.g. the result of a COMMENT, ELIDE, or a
// COMMA!) then it does not overwrite the previous output.  This is to
// facilitate features like this:
//
//    >> do [1 + 2 comment "hi"]
//    == 3
//
// The 1 + 2 evaluated to 3.  If we merely called the evaluator executor
// again on the same output cell, the comment would evaluate to an isotopic
// empty pack (e.g. a NIHIL, ~[]~ antiform).  That would overwrite the 3.
// So the stepper executor has a holding cell for the last result that it does
// not overwrite when invisible content comes along as the next value.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * A performance trick for the stepper executor is that it doesn't actually
//   need its own Level structure.  It uses the data stack for its holding
//   cell of the last result, and can actually just pass through to the
//   Evaluator_Executor().
//

#include "sys-core.h"


//
//  Stepper_Executor: C
//
// 1. *Before* a level is created for an array stepper, the creator should
//    push the "primed" value for what they want as a result if there
//    are no non-invisible evaluations.  It's important to do it before, so
//    that the pushed cell is not part of the level's baseline.  Right now the
//    only two things requested are nihil and void, so we can test for those.
//
// 2. An idea was tried once where the error was not raised until a step was
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
Bounce Stepper_Executor(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    enum {
        ST_STEPPER_INITIAL_ENTRY = STATE_0,
        ST_STEPPER_STEPPING
    };

    if (THROWING)
        return THROWN;  // no state to clean up

    switch (STATE) {
      case ST_STEPPER_INITIAL_ENTRY:
        assert(Not_Level_Flag(L, TRAMPOLINE_KEEPALIVE));
        assert(Is_Void(TOP) or Is_Nihil(atom_TOP));  // primed [1]
        goto initial_entry;

      default:
      #if DEBUG
        if (L != TOP_LEVEL) {
            assert(STATE == ST_STEPPER_STEPPING);
            goto step_result_in_out;
        }
      #endif
        goto call_evaluator_executor;  // callback on behalf of evaluator
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Feed_At_End(L->feed)) {
        Copy_Cell(OUT, atom_TOP);
        return OUT;
    }

    if (SPORADICALLY(64)) {  // 1 out of every 64 times, use sublevel if debug
        Level* sub = Make_Level(
            L->feed,
            LEVEL_FLAG_RAISED_RESULT_OK
                | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
        );
        Push_Level(OUT, sub);
        STATE = ST_STEPPER_STEPPING;
        return CATCH_CONTINUE_SUBLEVEL(sub);  // executors *must* catch
    }

    goto call_evaluator_executor;

} new_step: {  ///////////////////////////////////////////////////////////////

  #if DEBUG
    if (L != TOP_LEVEL) {  // sporadic creation of sublevel
        Restart_Evaluator_Level(SUBLEVEL);
        return BOUNCE_CONTINUE;
    }
  #endif

    STATE = 0;
    goto call_evaluator_executor;

} call_evaluator_executor: {  ////////////////////////////////////////////////

    assert(L == TOP_LEVEL);  // only do this when there's no sublevel

    Bounce bounce = Evaluator_Executor(L);

    if (bounce == OUT)
        goto step_result_in_out;

    return bounce;

} step_result_in_out: {  /////////////////////////////////////////////////////

    if (Is_Elision(OUT))  {  // was something like an ELIDE, COMMENT, COMMA!
        if (Not_Feed_At_End(L->feed)) {
            Erase_Cell(OUT);
            goto new_step;  // leave previous result as-is on stack
        }
        Move_Cell(OUT, atom_TOP);  // finished, so extract result from stack
        goto finished;
    }

    if (Is_Feed_At_End(L->feed))
        goto finished;  // OUT is not invisible, so it's the final result

    if (Is_Raised(OUT))   // raise errors synchronously if not at end [2]
        fail (VAL_CONTEXT(OUT));

    Move_Cell(atom_TOP, OUT);  // make current result the preserved one
    goto new_step;

} finished: {  ///////////////////////////////////////////////////////////////

  #if DEBUG
    if (L != TOP_LEVEL)
        Drop_Level(SUBLEVEL);
  #endif

    return OUT;
}}
