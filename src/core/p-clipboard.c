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
static REB_R Clipboard_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    REBINT result;
    REBVAL *arg;
    REBINT len;
    REBSER *ser;

    arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBREQ *req = Ensure_Port_State(port, RDI_CLIPBOARD);

    switch (action) {
    case SYM_UPDATE:
        // Update the port object after a READ or WRITE operation.
        // This is normally called by the WAKE-UP function.
        arg = CTX_VAR(port, STD_PORT_DATA);
        if (req->command == RDC_READ) {
            // this could be executed twice:
            // once for an event READ, once for the CLOSE following the READ
            if (!req->common.data) return R_BLANK;
            len = req->actual;
            if (GET_FLAG(req->flags, RRF_WIDE)) {
                // convert to UTF8, so that it can be converted back to string!
                Init_Binary(arg, Make_UTF8_Binary(
                    req->common.data,
                    len / sizeof(REBUNI),
                    0,
                    OPT_ENC_UNISRC
                ));
            }
            else {
                REBSER *ser = Make_Binary(len);
                memcpy(BIN_HEAD(ser), req->common.data, len);
                SET_SERIES_LEN(ser, len);
                Init_Binary(arg, ser);
            }
            OS_FREE(req->common.data); // release the copy buffer
            req->common.data = 0;
        }
        else if (req->command == RDC_WRITE) {
            SET_BLANK(arg);  // Write is done.
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
        if (!IS_OPEN(req)) {
            if (OS_DO_DEVICE(req, RDC_OPEN))
                fail (Error_On_Port(RE_CANNOT_OPEN, port, req->error));
        }
        // Issue the read request:
        CLR_FLAG(req->flags, RRF_WIDE); // allow byte or wide chars
        result = OS_DO_DEVICE(req, RDC_READ);
        if (result < 0) fail (Error_On_Port(RE_READ_ERROR, port, req->error));
        if (result > 0) return R_BLANK; /* pending */

        // Copy and set the string result:
        arg = CTX_VAR(port, STD_PORT_DATA);

        len = req->actual;
        if (GET_FLAG(req->flags, RRF_WIDE)) {
            // convert to UTF8, so that it can be converted back to string!
            Init_Binary(arg, Make_UTF8_Binary(
                req->common.data,
                len / sizeof(REBUNI),
                0,
                OPT_ENC_UNISRC
            ));
        }
        else {
            REBSER *ser = Make_Binary(len);
            memcpy(BIN_HEAD(ser), req->common.data, len);
            SET_SERIES_LEN(ser, len);
            Init_Binary(arg, ser);
        }

        Move_Value(D_OUT, arg);
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

        if (!IS_STRING(arg) && !IS_BINARY(arg))
            fail (Error_Invalid_Port_Arg_Raw(arg));

        // This device is opened on the WRITE:
        if (!IS_OPEN(req)) {
            if (OS_DO_DEVICE(req, RDC_OPEN))
                fail (Error_On_Port(RE_CANNOT_OPEN, port, req->error));
        }

        // Handle /part refinement:
        len = VAL_LEN_AT(arg);
        if (REF(part) && VAL_INT32(ARG(limit)) < len)
            len = VAL_INT32(ARG(limit));

        // If bytes, see if we can fit it:
        if (SER_WIDE(VAL_SERIES(arg)) == 1) {
#ifdef ARG_STRINGS_ALLOWED
            if (!All_Bytes_ASCII(VAL_BIN_AT(arg), len)) {
                REBSER *copy = Copy_Bytes_To_Unicode(VAL_BIN_AT(arg), len);
                Init_String(arg, copy);
            } else
                req->common.data = VAL_BIN_AT(arg);
#endif

            // Temp conversion:!!!
            ser = Make_Unicode(len);
            len = Decode_UTF8_Negative_If_Latin1(
                UNI_HEAD(ser), VAL_BIN_AT(arg), len, FALSE
            );
            len = abs(len);
            TERM_UNI_LEN(ser, len);
            Init_String(arg, ser);
            req->common.data = cast(REBYTE*, UNI_HEAD(ser));
            SET_FLAG(req->flags, RRF_WIDE);
        }
        else
        // If unicode (may be from above conversion), handle it:
        if (SER_WIDE(VAL_SERIES(arg)) == sizeof(REBUNI)) {
            req->common.data = cast(REBYTE *, VAL_UNI_AT(arg));
            SET_FLAG(req->flags, RRF_WIDE);
        }

        // Temp!!!
        req->length = len * sizeof(REBUNI);

        // Setup the write:
        Move_Value(CTX_VAR(port, STD_PORT_DATA), arg); // keep it GC safe
        req->actual = 0;

        result = OS_DO_DEVICE(req, RDC_WRITE);
        SET_BLANK(CTX_VAR(port, STD_PORT_DATA)); // GC can collect it

        if (result < 0) fail (Error_On_Port(RE_WRITE_ERROR, port, req->error));
        //if (result == DR_DONE) SET_BLANK(CTX_VAR(port, STD_PORT_DATA));
        break; }

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

        if (OS_DO_DEVICE(req, RDC_OPEN))
            fail (Error_On_Port(RE_CANNOT_OPEN, port, req->error));
        break; }

    case SYM_CLOSE:
        OS_DO_DEVICE(req, RDC_CLOSE);
        break;

    case SYM_OPEN_Q:
        if (IS_OPEN(req)) return R_TRUE;
        return R_FALSE;

    default:
        fail (Error_Illegal_Action(REB_PORT, action));
    }

    Move_Value(D_OUT, D_ARG(1)); // port
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
