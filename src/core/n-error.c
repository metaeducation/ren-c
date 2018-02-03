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


//
//  trap: native [
//
//  {Tries to DO a block, trapping error as return value (if one is raised).}
//
//      return: [<opt> any-value!]
//      block [block!]
//      /with
//          "Handle error case with code"
//      handler [block! function!]
//          "If FUNCTION!, spec allows [error [error!]]"
//      /?
//         "Instead of result or error, return LOGIC! of if a trap occurred"
//  ]
//
REBNATIVE(trap)
{
    INCLUDE_PARAMS_OF_TRAP; // ? is renamed as "q"

    struct Reb_State state;
    REBCTX *error;

    PUSH_TRAP(&error, &state);

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) {
        if (REF(with)) {
            REBVAL *handler = ARG(handler);

            if (IS_BLOCK(handler)) {
                // There's no way to pass 'error' to a block (so just DO it)
                if (Do_Any_Array_At_Throws(D_OUT, ARG(handler)))
                    return R_OUT_IS_THROWN;

                if (REF(q))
                    return R_TRUE;

                return R_OUT;
            }
            else {
                assert (IS_FUNCTION(handler));

                DECLARE_LOCAL (arg);
                Init_Error(arg, error);

                // Try passing the handler the ERROR! we trapped.  Passing
                // FALSE for `fully` means it will not raise an error if
                // the handler happens to be arity 0.
                //
                if (Apply_Only_Throws(D_OUT, FALSE, handler, arg, END))
                    return R_OUT_IS_THROWN;

                if (REF(q))
                    return R_TRUE;

                return R_OUT;
            }
        }

        if (REF(q)) return R_TRUE;

        Init_Error(D_OUT, error);
        return R_OUT;
    }

    if (Do_Any_Array_At_Throws(D_OUT, ARG(block))) {
        // Note that we are interested in when errors are raised, which
        // causes a tricky C longjmp() to the code above.  Yet a THROW
        // is different from that, and offers an opportunity to each
        // DO'ing stack level along the way to CATCH the thrown value
        // (with no need for something like the PUSH_TRAP above).
        //
        // We're being given that opportunity here, but doing nothing
        // and just returning the THROWN thing for other stack levels
        // to look at.  For the construct which does let you catch a
        // throw, see REBNATIVE(catch), which has code for this case.

        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);
        return R_OUT_IS_THROWN;
    }

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    if (REF(q)) return R_FALSE;

    return R_OUT;
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

    REBCTX *context;
    if (IS_WORD(ARG(location)))
        context = VAL_WORD_CONTEXT(ARG(location));
    else
        context = VAL_CONTEXT(ARG(location));

    REBFRM *where = CTX_FRAME_MAY_FAIL(context);

    REBCTX *error = VAL_CONTEXT(ARG(error));
    Set_Location_Of_Error(error, where);

    return R_VOID;
}


//
//  attempt: native [
//
//  {Tries to evaluate a block and returns result or NONE on error.}
//
//      return: [<opt> any-value!]
//      block [block!]
//  ]
//
REBNATIVE(attempt)
{
    INCLUDE_PARAMS_OF_ATTEMPT;

    REBVAL *block = ARG(block);

    struct Reb_State state;
    REBCTX *error;

    PUSH_TRAP(&error, &state);

    // The first time through the following code 'error' will be NULL, but...
    // `fail` can longjmp here, so 'error' won't be NULL *if* that happens!

    if (error) return R_BLANK;

    if (Do_Any_Array_At_Throws(D_OUT, block)) {
        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

        // Throw name is in D_OUT, thrown value is held task local
        return R_OUT_IS_THROWN;
    }

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    return R_OUT;
}
