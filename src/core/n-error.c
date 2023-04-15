//
//  File: %n-error.c
//  Summary: "native functions for raising and trapping errors"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2022 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//


#include "sys-core.h"


//
//  enrescue: native [
//
//  {Sandbox code to intercept failures at ANY depth (including typos)}
//
//      return: "ERROR! if raised, else ^META of the result"
//          [error! quoted! quasi! blank!]
//      code "Code to sandbox and monitor"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(enrescue)
//
// Note: During boot, this operation is removed from LIB and moved to the
// system utilities, so it is typically called as SYS.UTIL.ENRESCUE.  Reason
// is to help raise awareness of the risks involved with using this function,
// because it's dangerous to react to these errors (or suppress them) due to
// how little you know about what actually happened.
{
    INCLUDE_PARAMS_OF_ENRESCUE;

    REBVAL *code = ARG(code);

    enum {
        ST_ENRESCUE_INITIAL_ENTRY = STATE_0,
        ST_ENRESCUE_EVALUATING
    };

    switch (STATE) {
      case ST_ENRESCUE_INITIAL_ENTRY: goto initial_entry;
      case ST_ENRESCUE_EVALUATING: goto evaluation_finished;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Not_Cell_Flag(code, CONST))
        Set_Cell_Flag(code, EXPLICITLY_MUTABLE);  // see DECLARE_NATIVE(do) for why

    STATE = ST_ENRESCUE_EVALUATING;
    return CATCH_CONTINUE(OUT, code);

} evaluation_finished: {  ////////////////////////////////////////////////////

    if (not THROWING)  // successful result
        return Meta_Quotify(OUT);

    if (not IS_ERROR(VAL_THROWN_LABEL(FRAME)))  // non-ERROR! throws
        return BOUNCE_THROWN;

    Copy_Cell(OUT, VAL_THROWN_LABEL(FRAME));
    CATCH_THROWN(SPARE, FRAME);
    assert(Is_Nulled(SPARE));  // all error throws are null-valued

    return OUT;
  }
}


//
//  entrap: native [
//
//  {Tries to DO a block, trapping raised errors}
//
//      return: "ERROR! if raised, else the ^META of the result"
//          [error! quasi! quoted! blank!]
//      code "Code to execute and monitor"
//          [block! frame!]
//  ]
//
DECLARE_NATIVE(entrap)  // wrapped as multi-return versions TRAP and ATTEMPT
//
// Unlike SYS.UTIL.RESCUE, the ENTRAP function only reacts to errors from the
// functions it directly calls via FRAME_FLAG_FAILURE_RESULT_OK.  Hence it
// does not intercept "thrown" errors, making it much safer to react to the
// errors one gets back from it.
{
    INCLUDE_PARAMS_OF_ENTRAP;

    REBVAL *code = ARG(code);

    enum {
        ST_ENTRAP_INITIAL_ENTRY = STATE_0,
        ST_ENTRAP_EVALUATING
    };

    switch (STATE) {
      case ST_ENTRAP_INITIAL_ENTRY: goto initial_entry;
      case ST_ENTRAP_EVALUATING: goto eval_step_result_in_out;
      default: assert(false);
    }

  initial_entry: {  /////////////////////////////////////////////////////////

    Init_Void(OUT);  // default if all evaluations produce void

    Tweak_Non_Const_To_Explicitly_Mutable(code);  // see comments on function

    Flags flags =
        FRAME_FLAG_TRAMPOLINE_KEEPALIVE  // reused for each step
        | FRAME_FLAG_FAILURE_RESULT_OK;  // we're trapping it

    Frame(*) sub;
    if (IS_BLOCK(code)) {
        sub = Make_Frame_At(
            code,  // REB_BLOCK or REB_GROUP
            flags
                | FRAME_FLAG_ALLOCATED_FEED
        );
        Push_Frame(SPARE, sub);
    }
    else {
        bool pushed = Pushed_Continuation(
            SPARE,
            flags,
            SPECIFIED,
            code,
            nullptr
        );
        assert(pushed);
        UNUSED(pushed);
        sub = TOP_FRAME;
    }

    STATE = ST_ENTRAP_EVALUATING;
    return CONTINUE_SUBFRAME(sub);  // not a CATCH_CONTINUE; throws passthru

} eval_step_result_in_out: {  ////////////////////////////////////////////////

    if (Is_Raised(SPARE)) {
        Drop_Frame(SUBFRAME);
        Move_Cell(OUT, SPARE);
        mutable_QUOTE_BYTE(OUT) = UNQUOTED_1;  // change isotope error to plain
        return BRANCHED(OUT);
    }

    if (not Is_Void(SPARE))
        Move_Cell(OUT, SPARE);

    if (Is_Frame_At_End(SUBFRAME))
        goto finished;

    Restart_Evaluator_Frame(SUBFRAME);
    return CONTINUE_SUBFRAME(SUBFRAME);

} finished: {  ///////////////////////////////////////////////////////////////

    Drop_Frame(SUBFRAME);
    return Meta_Quotify(OUT);  // ^META result, may be initial void state
}}


//
//  except: enfix native [
//
//  {Analogue to something like a THEN which traps definitional errors}
//
//      return: "Non-failure input, or product of processing failure"
//          [<opt> <void> any-value!]
//      ^optional "<deferred argument> Run branch if this is definitional fail"
//          [<opt> <void> <fail> <pack> any-value!]
//      :branch "If arity-1 ACTION!, receives value that triggered branch"
//          [<unrun> any-branch!]
//  ]
//
DECLARE_NATIVE(except)
{
    INCLUDE_PARAMS_OF_EXCEPT;

    Value(*) v = ARG(optional);
    Value(*) branch = ARG(branch);
    Decay_If_Activation(ARG(branch));

    if (not Is_Meta_Of_Raised(v))
        return UNMETA(v);

    Unquasify(v);  // meta failures are ~QUASI-ERROR!~, branch wants ERROR!
    return DELEGATE_BRANCH(OUT, branch, v);
}


//
//  raised?: native [
//
//  "Tells you if argument is a failure, but does not raise it"
//
//      return: [logic!]
//      ^optional [<opt> <void> <fail> <pack> any-value!]
//  ]
//
DECLARE_NATIVE(raised_q)
{
    INCLUDE_PARAMS_OF_RAISED_Q;

    return Init_Logic(OUT, Is_Meta_Of_Raised(ARG(optional)));
}


//
//  set-location-of-error: native [
//
//  {Sets the WHERE, NEAR, FILE, and LINE fields of an error}
//
//      return: [<opt>]
//      error [error!]
//      location [frame! any-word!]
//  ]
//
DECLARE_NATIVE(set_location_of_error)
{
    INCLUDE_PARAMS_OF_SET_LOCATION_OF_ERROR;

    REBVAL *location = ARG(location);

    Context(*) context;
    if (IS_WORD(location)) {
        if (not IS_WORD_BOUND(location))
            fail ("SET-LOCATION-OF-ERROR requires bound WORD!");
        context = VAL_WORD_CONTEXT(location);
    }
    else {
        assert(IS_FRAME(location));
        context = VAL_CONTEXT(location);
    }

    Frame(*) where = CTX_FRAME_MAY_FAIL(context);

    Context(*) error = VAL_CONTEXT(ARG(error));
    Set_Location_Of_Error(error, where);

    return nullptr;
}
