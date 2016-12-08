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
// Copyright 2012-2016 Rebol Open Source Contributors
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
                if (DO_VAL_ARRAY_AT_THROWS(D_OUT, ARG(handler)))
                    return R_OUT_IS_THROWN;

                if (REF(q)) return R_TRUE;

                return R_OUT;
            }
            else if (IS_FUNCTION(handler)) {
                REBVAL arg;
                Val_Init_Error(&arg, error);

                // Try passing the handler the ERROR! we trapped.  Passing
                // FALSE for `fully` means it will not raise an error if
                // the handler happens to be arity 0.
                //
                if (Apply_Only_Throws(D_OUT, FALSE, handler, &arg, END_CELL))
                    return R_OUT_IS_THROWN;

                if (REF(q)) return R_TRUE;

                return R_OUT;
            }

            panic (Error(RE_MISC)); // should not be possible (type-checking)
        }

        if (REF(q)) return R_TRUE;

        Val_Init_Error(D_OUT, error);
        return R_OUT;
    }

    if (DO_VAL_ARRAY_AT_THROWS(D_OUT, ARG(block))) {
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
//  fail: native [
//
//  {Interrupts execution by reporting an error (a TRAP can intercept it).}
//
//      reason [error! string! block!]
//          "ERROR! value, message string, or failure spec"
//      /where
//          "Specify an originating location other than the FAIL itself"
//      location [frame! any-word!]
//          "Frame or parameter at which to indicate the error originated"
//  ]
//
REBNATIVE(fail)
{
    INCLUDE_PARAMS_OF_FAIL;

    REBVAL *reason = ARG(reason);

    if (IS_ERROR(reason))
        fail (VAL_CONTEXT(reason)); // if argument is an error, trigger as-is

    if (IS_BLOCK(reason)) {
        //
        // Ultimately we'd like FAIL to use some clever error-creating
        // dialect when passed a block, maybe something like:
        //
        //     fail [<invalid-key> {The key} key-name: key {is invalid}]
        //
        // That could provide an error ID, the format message, and the
        // values to plug into the slots to make the message...which could
        // be extracted from the error if captured (e.g. error/id and
        // `error/key-name`.  Another option would be something like:
        //
        //     fail/with [{The key} :key-name {is invalid}] [key-name: key]

        RELVAL *item = VAL_ARRAY_AT(reason);

        REBVAL pending_delimiter;
        SET_END(&pending_delimiter);

        REB_MOLD mo;
        CLEARS(&mo);

        // Check to make sure we're only drawing from the limited types
        // we accept (reserving room for future dialect expansion)
        //
        for (; NOT_END(item); item++) {
            if (IS_STRING(item) || ANY_SCALAR(item))
                continue;

            // Leave the group in and let the reduce take care of it
            //
            if (IS_GROUP(item))
                continue;

            // Literal blocks in the spec given to Format used by PRINT
            // has special meaning for BLOCK! (and BAR! when not used
            // in the middle of an expression)
            //
            if (IS_BLOCK(item) || IS_BAR(item))
                continue;

            // Leave words in to be handled by the reduce step as long
            // as they don't look up to functions.
            //
            // !!! This keeps the option open of being able to know that
            // strings that appear in the block appear in the error
            // message so it can be templated.
            //
            if (IS_WORD(item) || IS_GET_WORD(item)) {
                const REBVAL *var
                    = TRY_GET_OPT_VAR(item, VAL_SPECIFIER(reason));

                if (!var || !IS_FUNCTION(var))
                    continue;
            }

            // The only way to tell if a path resolves to a function
            // or not is to actually evaluate it, and we are delegating
            // to Reduce_Block ATM.  For now we force you to use a GROUP!
            //
            //     fail [{Erroring on} (the/safe/side) {for now.}]
            //
            fail (Error(RE_LIMITED_FAIL_INPUT));
        }

        // Use the same logic that PRINT does, which will create newline
        // at expression barriers and form literal blocks with no spaces

        Push_Mold(&mo);
        if (Form_Value_Throws(
            D_OUT,
            &mo,
            &pending_delimiter, // variable shared by recursions
            reason,
            FORM_FLAG_REDUCE
                | FORM_FLAG_NEWLINE_SEQUENTIAL_STRINGS, // no newline at end
            SPACE_VALUE, // delimiter same as PRINT (customizable?)
            0 // depth
        )) {
            return R_OUT_IS_THROWN;
        }

        Val_Init_String(reason, Pop_Molded_String(&mo));
    }

    assert(IS_STRING(reason));

    REBFRM *where = NULL;
    if (REF(where)) {
        REBCTX *context;
        if (IS_WORD(ARG(location)))
            context = VAL_WORD_CONTEXT(ARG(location));
        else
            context = VAL_CONTEXT(ARG(location));
        where = CTX_FRAME(context);

        // !!! If where comes back NULL, what to do?  Probably bad if someone
        // is trying to decipher an error to trigger another error.  Maybe
        // the meta info on the error could be annotated with "tried a
        // where that was for an expired stack frame" or similar...
    }

    if (Make_Error_Object_Throws(D_OUT, reason, where)) {
        // Throw name is in D_OUT, thrown value is held task local
        return R_OUT_IS_THROWN;
    }

    fail (VAL_CONTEXT(D_OUT));
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

    if (DO_VAL_ARRAY_AT_THROWS(D_OUT, block)) {
        DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

        // Throw name is in D_OUT, thrown value is held task local
        return R_OUT_IS_THROWN;
    }

    DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&state);

    return R_OUT;
}
