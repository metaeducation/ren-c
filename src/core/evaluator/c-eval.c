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
// The Evaluator_Executor() simply calls Meta_Stepper_Executor() consecutively,
// and if the output is invisible (e.g. the result of a COMMENT, ELIDE, etc.)
// it won't overwrite the previous output.
//
// This facilitate features like:
//
//    >> eval [1 + 2 comment "hi"]
//    == 3
//
// The 1 + 2 evaluated to 3.  If we merely called the Meta_Stepper_Executor()
// again on the same output cell, the comment would evaluate to an antiform
// comma (e.g. a GHOST, ~,~ antiform).  That would overwrite the 3.  So the
// Evaluator_Executor() has a holding cell for the last result that it does
// not overwrite when invisible content comes along as the next value.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * The reason this isn't done with something like a DO_TO_END flag that
//   controls a mode of the Meta_Stepper_Executor() is so that the stepper can
//   have its own Level* in a debugger.  If it didn't have its own level,
//   then in order to keep alive it could not return a result to the
//   Trampoline until it had reached the end.  Thus, a generalized debugger
//   watching for finalized outputs would only see one final output--instead
//   of watching one be synthesized for each step.
//
// * ...BUT a performance trick in Evaluator_Executor() is that if you're NOT
//   debugging, it can actually avoid making its own Level* structure.  It's
//   able to use a cell in the Level structure for its holding of the last
//   result, and can actually just pass through to the Meta_Stepper_Executor().
//
// * !!! A debugger that was giving insights into the steps would show the
//   steps being meta-values, and giving a "trash" to indicate the end of
//   the stepping.  As a UI concern for the debugger, this might be a little
//   bit confusing for people to wonder why the steps are always ^META.
//   The debugger might want to customize the display based on the executor
//   to give a more

#include "sys-core.h"


#define PRIMED  cast(Atom*, &L->u.eval.primed)


//
//  Evaluator_Executor: C
//
// 1. *Before* a level is created for Evaluator_Executor(), the creator should
//    set the "primed" value for what they want as a result if there
//    are no non-invisible evaluations.  Right now the only two things
//    requested are nihil and ghost, so we can test for those.
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
        assert(Is_Nihil(PRIMED) or Is_Ghost(PRIMED));  // primed [1]
        goto initial_entry;

      default:
      #if RUNTIME_CHECKS
        if (L != TOP_LEVEL) {  // we're using the trampoline and pushed level
            assert(STATE == ST_EVALUATOR_STEPPING);
            goto step_done_with_meta_or_end_in_out;
        }
      #endif
        goto call_stepper_executor_directly;  // callback on behalf of stepper
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. As mentioned in the notes at the top of this file, the main reason
    //    Evaluator_Executor() is separate from Meta_Stepper_Executor() is
    //    to facilitate a general debugging loop hooked into the Trampoline
    //    that can see results generated at the granularity of a step.
    //
    //    But that generalized debugger doesn't exist yet.  So to make sure
    //    that both the "fast" no-Level mode and the "slow" added Level mode
    //    would be able to toggle based on whether such a hypothetical
    //    debugger were turned on, it sporadically spawns levels in the
    //    RUNTIME_CHECKS builds.

    if (SPORADICALLY(64)) {  // 1 out of 64 times, use sublevel if debug [1]
        Level* sub = Make_Level(
            &Meta_Stepper_Executor,
            L->feed,
            LEVEL_FLAG_RAISED_RESULT_OK
                | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
        );
        Push_Level_Erase_Out_If_State_0(OUT, sub);
        STATE = ST_EVALUATOR_STEPPING;
        return CONTINUE_SUBLEVEL(sub);  // executors *must* catch
    }

    goto call_stepper_executor_directly;

} new_step: {  ///////////////////////////////////////////////////////////////

    // 1. If we're not looking to see a debug step that goes over [] so we
    //    know the empty block is there, then we can skip empty blocks.
    //    In any case, we still sporadically test the code path if we don't
    //    do this probably-superfluous optimization.

  #if RUNTIME_CHECKS
    if (L != TOP_LEVEL) {  // detect if a sublevel was used
        Reset_Evaluator_Erase_Out(SUBLEVEL);
        return BOUNCE_CONTINUE;
    }
  #endif

    if (not SPORADICALLY(32)) {
        if (Is_Feed_At_End(L->feed))  // harmless if not debugging...? [1]
            goto finished;
    }

    Reset_Evaluator_Erase_Out(L);
    goto call_stepper_executor_directly;

} call_stepper_executor_directly: {  /////////////////////////////////////////

    assert(L == TOP_LEVEL);  // only do this when there's no sublevel

    Bounce bounce = Meta_Stepper_Executor(L);

    if (bounce == OUT)  // completed step, possibly finished
        goto step_done_with_meta_or_end_in_out;

    return bounce;  // requesting a continuation on behalf of the stepper

} step_done_with_meta_or_end_in_out: {  //////////////////////////////////////

    // 1. An idea was tried once where the error was not raised until a step
    //    was shown to be non-invisible.  This would allow invisible
    //    evaluations to come after an error and still fall out:
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

    if (Is_Endlike_Trash(OUT))  // the "official" way to reach the end
        goto finished;

    if (Is_Meta_Of_Ghost(OUT))  // something like an ELIDE or COMMENT
        goto new_step;  // leave previous result as-is in PRIMED

    Move_Atom(PRIMED, OUT);  // make current result the preserved one
    Meta_Unquotify_Undecayed(PRIMED);

    if (Is_Raised(PRIMED)) {  // raise synchronous error if not at end [1]
        if (not Is_Feed_At_End(L->feed))
            return FAIL(Cell_Error(PRIMED));  // (raise "foo",) still errors
        goto finished;
    }

    goto new_step;

} finished: {  ///////////////////////////////////////////////////////////////

  #if RUNTIME_CHECKS
    if (L != TOP_LEVEL)  // detect if a sublevel was used [2]
        Drop_Level(SUBLEVEL);
  #endif

    Copy_Cell(OUT, PRIMED);
    return OUT;
}}
