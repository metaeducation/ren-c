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
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// Note that the mechanism by which errors are raised is based on longjmp(),
// and thus can interrupt stacks in progress.  Trapping errors is only done
// by those levels of the stack that have done a PUSH_TRAP (as opposed to
// detecting thrown values, that is "cooperative" and "bubbles" up through
// every stack level in its return slot, with no longjmp()).
//

#include "sys-core.h"


//
//  trap: native [
//
//  {Tries to DO a block, trapping raised errors}
//
//      return: "ERROR! if raised, else null"
//          [<opt> error!]
//      result: "<output> The optional result of the evaluation"
//          [<opt> any-value!]
//
//      code "Code to execute and monitor"
//          [block! action!]
//  ]
//
REBNATIVE(trap)
//
// !!! For stackless, the implementation of TRAP is actually moved into the
// trampoline.  A generic mechanism that allows dispatchers to register
// interest in errors is likely needed to parallel such a mechanism for throws.
{
    INCLUDE_PARAMS_OF_TRAP;

    REBVAL *code = ARG(code);

    enum {
        ST_TRAP_INITIAL_ENTRY = 0,
        ST_TRAP_EVALUATING
    };

    switch (STATE) {
      case ST_TRAP_INITIAL_ENTRY: goto initial_entry;
      case ST_TRAP_EVALUATING: goto evaluation_finished;
      default: assert(false);
    }

  initial_entry: {
    if (NOT_CELL_FLAG(code, CONST))
        SET_CELL_FLAG(code, EXPLICITLY_MUTABLE);  // see REBNATIVE(do) for why

    STATE = ST_TRAP_EVALUATING;
    continue_catchable (OUT, code, END);
  }

  evaluation_finished: {
    if (not THROWING) {
        if (REF(result)) {
            Reify_Eval_Out_Plain(OUT);
            rebElide(Lib(SET), rebQ(REF(result)), rebQ(OUT));
        }
        return nullptr;
    }

    if (not IS_ERROR(VAL_THROWN_LABEL(FRAME)))  // CATCH for non-ERROR! throws
        return R_THROWN;

    Copy_Cell(OUT, VAL_THROWN_LABEL(FRAME));
    CATCH_THROWN(SPARE, FRAME);
    assert(IS_NULLED(SPARE));  // all error throws are null-valued

    return_branched (OUT);
  }
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
REBNATIVE(set_location_of_error)
{
    INCLUDE_PARAMS_OF_SET_LOCATION_OF_ERROR;

    REBVAL *location = ARG(location);

    REBCTX *context;
    if (IS_WORD(location)) {
        if (not IS_WORD_BOUND(location))
            fail ("SET-LOCATION-OF-ERROR requires bound WORD!");
        context = VAL_WORD_CONTEXT(location);
    }
    else {
        assert(IS_FRAME(location));
        context = VAL_CONTEXT(location);
    }

    REBFRM *where = CTX_FRAME_MAY_FAIL(context);

    REBCTX *error = VAL_CONTEXT(ARG(error));
    Set_Location_Of_Error(error, where);

    return nullptr;
}
