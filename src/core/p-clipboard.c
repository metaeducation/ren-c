//
//  File: %p-clipboard.c
//  Summary: "clipboard port interface"
//  Section: ports
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

#include "sys-core.h"


//
//  Clipboard_Actor: C
//
static REB_R Clipboard_Actor(REBFRM *frame_, REBCTX *port, REBSYM verb)
{
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBREQ *req = Ensure_Port_State(port, RDI_CLIPBOARD);

    switch (verb) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implied by `port`
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != 0);

        switch (property) {
        case SYM_OPEN_Q:
            return R_FROM_BOOL(did (req->flags & RRF_OPEN));

        default:
            break;
        }

        break; }

    case SYM_ON_WAKE_UP:
        // Update the port object after a READ or WRITE operation.
        // This is normally called by the WAKE-UP function.
        arg = CTX_VAR(port, STD_PORT_DATA);
        if (req->command == RDC_READ) {
            // this could be executed twice:
            // once for an event READ, once for the CLOSE following the READ
            if (req->common.data == NULL)
                return R_BLANK;

            REBVAL *data = cast(REBVAL*, req->common.data); // Hack!
            Move_Value(arg, data);
            rebRelease(data);

            req->common.data = NULL;
        }
        else if (req->command == RDC_WRITE) {
            Init_Blank(arg);  // Write is done.
        }
        return R_BLANK;

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source)); // already accounted for
        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(seek)) {
            UNUSED(ARG(index));
            fail (Error_Bad_Refines_Raw());
        }
        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        // This device is opened on the READ:
        if (not (req->flags & RRF_OPEN)) {
            REBVAL *o_result = OS_DO_DEVICE(req, RDC_OPEN);
            assert(o_result != NULL);
            if (rebDid("lib/error?", o_result, END))
                rebFail (o_result, END);
            rebRelease(o_result); // ignore result
        }

        REBVAL *r_result = OS_DO_DEVICE(req, RDC_READ);
        assert(r_result != NULL);
        if (rebDid("lib/error?", r_result, END))
            rebFail (r_result, END);
        rebRelease(r_result); // ignore result

        // Copy and set the string result:
        arg = CTX_VAR(port, STD_PORT_DATA);

        REBVAL *data = cast(REBVAL*, req->common.data); // !!! Hack
        assert(req->actual == 0); // !!! Unused

        if (IS_BLANK(data)) {
            //
            // What means will READ have for differentiating "no data" from
            // "empty"?  BLANK is one way...
            //
            Move_Value(D_OUT, data);
        }
        else {
            assert(IS_BINARY(data));
            Move_Value(D_OUT, data);
        }

        rebRelease(data);
        return R_OUT; }

    case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PAR(destination));
        UNUSED(PAR(data)); // used as arg

        if (REF(seek)) {
            UNUSED(ARG(index));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(append))
            fail (Error_Bad_Refines_Raw());
        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(lines))
            fail (Error_Bad_Refines_Raw());

        if (not IS_STRING(arg) and not IS_BINARY(arg))
            fail (Error_Invalid_Port_Arg_Raw(arg));

        // This device is opened on the WRITE:
        if (not (req->flags & RRF_OPEN)) {
            REBVAL *o_result = OS_DO_DEVICE(req, RDC_OPEN);
            assert(o_result != NULL);
            if (rebDid("lib/error?", o_result, END))
                rebFail (o_result, END);
        }

        // Handle /part refinement:
        REBINT len = VAL_LEN_AT(arg);
        if (REF(part) and VAL_INT32(ARG(limit)) < len)
            len = VAL_INT32(ARG(limit));

        req->common.data = cast(REBYTE*, arg);
        req->length = len;

        // Setup the write:
        Move_Value(CTX_VAR(port, STD_PORT_DATA), arg); // keep it GC safe
        req->actual = 0;

        REBVAL *w_result = OS_DO_DEVICE(req, RDC_WRITE);
        assert(w_result != NULL);
        if (rebDid("lib/error?", w_result, END))
            rebFail (w_result, END);

        Init_Blank(CTX_VAR(port, STD_PORT_DATA)); // GC can collect it

        goto return_port; }

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));
        if (REF(new))
            fail (Error_Bad_Refines_Raw());
        if (REF(read))
            fail (Error_Bad_Refines_Raw());
        if (REF(write))
            fail (Error_Bad_Refines_Raw());
        if (REF(seek))
            fail (Error_Bad_Refines_Raw());
        if (REF(allow)) {
            UNUSED(ARG(access));
            fail (Error_Bad_Refines_Raw());
        }

        REBVAL *result = OS_DO_DEVICE(req, RDC_OPEN);
        assert(result != NULL);
        if (rebDid("lib/error?", result, END))
            rebFail (result, END);
        rebRelease(result); // ignore result
        goto return_port; }

    case SYM_CLOSE: {
        REBVAL *result = OS_DO_DEVICE(req, RDC_CLOSE);
        assert(result != NULL);
        if (rebDid("lib/error?", result, END))
            rebFail (result, END);
        rebRelease(result); // ignore result
        goto return_port; }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));

return_port:
    Move_Value(D_OUT, D_ARG(1));
    return R_OUT;
}


//
//  get-clipboard-actor-handle: native [
//
//  {Retrieve handle to the native actor for clipboard}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_clipboard_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Clipboard_Actor);
    return R_OUT;
}
