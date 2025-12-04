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
// B. There are effectively two different ways to model multi-step evaluation
//    in terms of how GHOST! (the ~,~ antiform) is treated.  One is a sort of
//    regimented approach where the idea is that you want the aggregate
//    evaluation of a BLOCK! to be something that is comprised of distinct
//    EVAL:STEP calls, which handle GHOST! in an identical way on each step.
//    The other modality is more based around the idea of things like an
//    inline GROUP!, which prioritizes the idea that `expr` and `(expr)` will
//    behave equivalently.
//
//    This manifests in terms of if you pass in LEVEL_FLAG_AFRAID_OF_GHOSTS
//    at the beginning of the evaluation.  If you don't, then the evaluator
//    only becomes "afraid" of ghosts (voidifying the non-GHOSTABLE cases)
//    once it sees a non-GHOST! result.
//
//        ghost? eval [comment "hi"]    ; => ~okay~
//        ghost? (eval [comment "hi"])  ; => should also be ~okay~
//
//    But if you start afraid, then all are identically afraid, e.g.:
//
//        eval:step [eval [comment "hi"]]    ; => void, not GHOST!
//        eval:step [^ eval [comment "hi"]]  ; => GHOST!
//
//    This divergence of evaluator styles is a natural outcome of the needs
//    of EVAL-the-function and GROUP!-the-syntax-tool.  They are different
//    by necessity.
//

#include "sys-core.h"


#define PRIMED  cast(Value*, &L->u.eval.primed)


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
//    are no non-invisible evaluations.  Theoretically any preloaded value is
//    possible (and we may want to expose that as a feature e.g. in EVAL).
//    But for now, GHOST! is the presumed initial value at all callsites.
//
//    (Note: The reason preloading was initially offered to clients was to
//    allow a choice of VOID vs. GHOST!, so that contexts where vaporization
//    would be "risky" could avoid ghosts.  A systemic and powerful way of
//    controlling vaporization arose from LEVEL_FLAG_AFRAID_OF_GHOSTS which
//    gives a best-of-both-worlds approach: allowing multi-step evaluation
//    contexts to convert ghosts to voids in steps for functions that are
//    not intrinically "GHOSTABLE", with an operator to override the ghost
//    suppression.  But preloading is kept as it may be useful later.)
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
        possibly(Get_Level_Flag(L, AFRAID_OF_GHOSTS));  // GROUP! unafraid [B]
        assert(Is_Ghost(PRIMED));  // all cases GHOST! at the moment [1]
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
                | (L->flags.bits & LEVEL_FLAG_AFRAID_OF_GHOSTS)  // see [B]
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

  // 1. Note that unless a function is declared as GHOSTABLE, any GHOST! it
  //    tries to return will be converted to a VOID for safety when in an
  //    evaluation step marked by LEVEL_FLAG_AFRAID_OF_GHOSTS.  You have to
  //    override this explicitly with the `^` operator when you actually want
  //    a ghost...whether it's from a non-ghostable function or just a
  //    quasiform or ^META variable fetch.
  //
  // 2. It may seem desirable to allow invisible evaluations to come after
  //    an ERROR!, permitting things like:
  //
  //        >> error? (fail "some error" comment "invisible")
  //        == \~okay~\  ; anti
  //
  //    But consider:
  //
  //        >> data: []
  //
  //        >> take [] append data 'a
  //        ** Error: Can't take from empty block
  //
  //        >> data
  //        == [a]
  //
  //    You can't wait until you know if the next evaluation is invisible to
  //    escalate ERROR! to panic.  Things don't stop running soon enough.

    if (Is_Endlike_Unset(OUT))  // the "official" way to detect reaching end
        goto finished;

    if (Is_Ghost(OUT)) // ELIDE, COMMENT, ~,~ or ^GHOST-VAR etc. [1]
        goto start_new_step;  // leave previous result as-is in PRIMED

    Move_Value(PRIMED, OUT);  // make current result the preserved one

    if (Using_Sublevel_For_Stepping(L)) {  // always unafraid now, see [B]
        possibly(Get_Level_Flag(SUBLEVEL, AFRAID_OF_GHOSTS));
        Set_Level_Flag(SUBLEVEL, AFRAID_OF_GHOSTS);
    } else{
        possibly(Get_Level_Flag(L, AFRAID_OF_GHOSTS));
        Set_Level_Flag(L, AFRAID_OF_GHOSTS);
    }

    dont(Try_Is_Level_At_End_Optimization(L));  // (fail x,) must error
    if (Is_Feed_At_End(L->feed))
        goto finished;

    require (  // panic if error seen before final step [2]
      Elide_Unless_Error_Including_In_Packs(PRIMED)
    );

    goto start_new_step;

} finished: {  ///////////////////////////////////////////////////////////////

    if (Using_Sublevel_For_Stepping(L))
        Drop_Level(SUBLEVEL);

    Move_Value(OUT, PRIMED);
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
