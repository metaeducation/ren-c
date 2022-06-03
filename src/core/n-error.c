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


// This is the code which is protected by the exception mechanism.  See the
// rebRescue() API for more information.
//
static const REBVAL *Trap_Dangerous(REBFRM *frame_) {
    INCLUDE_PARAMS_OF_TRAP;
    UNUSED(ARG(result));

    if (Do_Branch_Throws(OUT, ARG(code)))
        return Lib(BLANK);  // signal thrown without corrupting OUT

    if (IS_VOID(OUT))
        Init_None(OUT);

    return nullptr;
}


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
{
    INCLUDE_PARAMS_OF_TRAP;

    REBVAL *error = rebRescue(cast(REBDNG*, &Trap_Dangerous), frame_);
    UNUSED(ARG(code)); // gets used by the above call, via the frame_ pointer
    if (not error) {
        if (REF(result))
            rebElide(Lib(SET), rebQ(REF(result)), rebQ(OUT));
        return nullptr; // code didn't fail() or throw
    }

    if (IS_BLANK(error)) // signal used to indicate a throw
        return_thrown (OUT);

    assert(IS_ERROR(error));
    return error;
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
