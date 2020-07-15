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
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
    UNUSED(ARG(valid));

    if (Do_Branch_Throws(D_OUT, ARG(code)))
        return VOID_VALUE;

    return nullptr;
}


//
//  trap: native [
//
//  {Tries to DO a block, trapping raised errors}
//
//      return: "ERROR! if raised, else null (ANY-VALUE! for ENCLOSE & CHAIN)"
//          [<opt> error! any-value!]  ; see note about why ANY-VALUE!
//      code "Code to execute and monitor"
//          [block! action!]
//      /result "Optional output result of the evaluation if not an error"
//      valid [word! path!]
//  ]
//
REBNATIVE(trap)
//
// !!! R3C lacks multiple return value handling, but this gives parity with
// a /RESULT refinement for getting the mechanical result in case of no error.
// In mainline you could write:
//
//     [error valid]: trap [...]
//
// But R3C will have to do this as:
//
//     error: trap/result [...] 'valid
//
// !!! When the value being set is VOID!, SET/ANY must be used at this time.
// This is a bit more inefficient in the API since it requires scanning.
// Non-void cases are done with directly referencing the SET native.
//
// !!! Type widening of datatypes returned from derived functions is an
// imperfect science at time of writing, and much of the work is post-R3C.
// So the result is set to ANY-VALUE! so that ENCLOSE or ADAPT can make
// wrappers more compatible with historical Rebol TRY's behavior.
{
    INCLUDE_PARAMS_OF_TRAP;

    REBVAL *error = rebRescue(cast(REBDNG*, &Trap_Dangerous), frame_);
    UNUSED(ARG(code));  // gets used by the above call, via the frame_ pointer

    if (not error) {  // code didn't fail() or throw
        if (REF(result))
            rebElide(rebEval(NAT_VALUE(set)), ARG(valid), D_OUT, rebEND);

        return nullptr;
    }

    if (IS_VOID(error))  // signal used to indicate a throw
        return R_THROWN;

    assert(IS_ERROR(error));

    if (REF(result))  // error case voids result to minimize likely use
        rebElide(NAT_VALUE(set), ARG(valid), VOID_VALUE, rebEND);

    return error;
}


static REBVAL *Entrap_Dangerous(REBFRM *frame_) {
    INCLUDE_PARAMS_OF_ENTRAP;

    if (Do_Branch_Throws(D_OUT, ARG(code))) {
        Init_Error(D_OUT, Error_No_Catch_For_Throw(D_OUT));
        return nullptr;
    }

    if (IS_NULLED(D_OUT))
        return nullptr; // don't box it up

    REBARR *a = Alloc_Singular(ARRAY_FLAG_FILE_LINE | NODE_FLAG_MANAGED);
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
