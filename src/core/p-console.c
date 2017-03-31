//
//  File: %p-console.c
//  Summary: "console port interface"
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


#define OUT_BUF_SIZE 32*1024

// Does OS use wide chars or byte chars (UTF-8):
#ifdef OS_WIDE_CHAR
#define MAKE_OS_BUFFER Make_Unicode
#else
#define MAKE_OS_BUFFER Make_Binary
#endif

//
//  Console_Actor: C
//
static REB_R Console_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    REBINT result;
    REBVAL *arg;
    REBSER *ser;

    arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    Move_Value(D_OUT, D_ARG(1));

    REBREQ *req = Ensure_Port_State(port, RDI_STDIO);

    switch (action) {

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));

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

        // If not open, open it:
        if (!IS_OPEN(req)) {
            if (OS_DO_DEVICE(req, RDC_OPEN))
                fail (Error_On_Port(RE_CANNOT_OPEN, port, req->error));
        }

        // If no buffer, create a buffer:
        arg = CTX_VAR(port, STD_PORT_DATA);
        if (!IS_STRING(arg) && !IS_BINARY(arg)) {
            Init_Binary(arg, Make_Binary(OUT_BUF_SIZE));
        }
        ser = VAL_SERIES(arg);
        SET_SERIES_LEN(ser, 0);
        TERM_SERIES(ser);

        // !!! May be a 2-byte wide series on Windows for wide chars, in
        // which case the length is not bytes??  (Can't use BIN_DATA here
        // because that asserts width is 1...)
        //
        req->common.data = SER_DATA_RAW(ser);
        req->length = SER_AVAIL(ser);

        result = OS_DO_DEVICE(req, RDC_READ);
        if (result < 0) fail (Error_On_Port(RE_READ_ERROR, port, req->error));

        // !!! Among many confusions in this file, it said "Another copy???"
        //Init_String(D_OUT, Copy_OS_Str(ser->data, result));
        Init_Binary(D_OUT, Copy_Bytes(req->common.data, req->actual));
        break; }

    case SYM_OPEN: {
        SET_OPEN(req);
        break; }

    case SYM_CLOSE:
        SET_CLOSED(req);
        //OS_DO_DEVICE(req, RDC_CLOSE);
        break;

    case SYM_OPEN_Q:
        if (IS_OPEN(req)) return R_TRUE;
        return R_FALSE;

    default:
        fail (Error_Illegal_Action(REB_PORT, action));
    }

    return R_OUT;
}


//
//  get-console-actor-handle: native [
//
//  {Retrieve handle to the native actor for console}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_console_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Console_Actor);
    return R_OUT;
}
