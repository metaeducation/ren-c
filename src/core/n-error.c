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

    if (Do_Branch_Throws(D_OUT, D_SPARE, ARG(code)))
        return VOID_VALUE;

    return nullptr;
}


//
//  trap: native [
//
//  {Tries to DO a block, trapping raised errors}
//
//      return: "ERROR! if raised, else null"
//          [<opt> error!]
//      code "Code to execute and monitor"
//          [block! action!]
//      /result "The optional output result of the evaluation"
//          [<output> <opt> any-value!]
//  ]
//
REBNATIVE(trap)
{
    INCLUDE_PARAMS_OF_TRAP;

    REBVAL *error = rebRescue(cast(REBDNG*, &Trap_Dangerous), frame_);
    UNUSED(ARG(code)); // gets used by the above call, via the frame_ pointer
    if (not error) {
        if (REF(result))
            rebElide(NATIVE_VAL(set), rebQ(REF(result)), rebQ(D_OUT), rebEND);
        return nullptr; // code didn't fail() or throw
    }

    if (IS_VOID(error)) // signal used to indicate a throw
        return R_THROWN;

    assert(IS_ERROR(error));
    return error;
}


static REBVAL *Entrap_Dangerous(REBFRM *frame_) {
    INCLUDE_PARAMS_OF_ENTRAP;

    if (Do_Branch_Throws(D_OUT, D_SPARE, ARG(code))) {
        Init_Error(D_OUT, Error_No_Catch_For_Throw(D_OUT));
        return nullptr;
    }

    if (IS_NULLED(D_OUT))
        return nullptr; // don't box it up

    REBARR *a = Alloc_Singular(
        ARRAY_MASK_HAS_FILE_LINE | NODE_FLAG_MANAGED
    );
    Move_Value(ARR_SINGLE(a), D_OUT);
    Init_Block(D_OUT, a);
    return nullptr;
}


//
//  entrap: native [
//
//  {DO a block and put result in a 1-item BLOCK!, unless error is raised}
//
//      return: "ERROR! if raised, null if null, or result in a BLOCK!"
//          [<opt> block! error!]
//      code "Code to execute and monitor"
//          [block! action!]
//  ]
//
REBNATIVE(entrap)
{
    INCLUDE_PARAMS_OF_ENTRAP;

    if (IS_BLOCK(ARG(code)))
        Symify(ARG(code));  // request that branch is not voidified

    REB_R error = rebRescue(cast(REBDNG*, &Entrap_Dangerous), frame_);
    UNUSED(ARG(code)); // gets used by the above call, via the frame_ pointer

    if (error)
        return error;

    return D_OUT;
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
