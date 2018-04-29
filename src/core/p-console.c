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

//
//  Console_Actor: C
//
static REB_R Console_Actor(REBFRM *frame_, REBCTX *port, REBSYM verb)
{
    REBREQ *req = Ensure_Port_State(port, RDI_STDIO);

    switch (verb) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implied by `port`
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_OPEN_Q:
            return R_FROM_BOOL(did (req->flags & RRF_OPEN));

        default:
            break;
        }

        break; }

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
        if (not (req->flags & RRF_OPEN)) {
            REBVAL *o_result = OS_DO_DEVICE(req, RDC_OPEN);
            assert(o_result != NULL);
            if (rebDid("lib/error?", o_result, END))
                rebFail (o_result, END);
            rebRelease(o_result); // ignore result
        }

        // If no buffer, create a buffer:
        //
        REBVAL *data = CTX_VAR(port, STD_PORT_DATA);
        if (not IS_BINARY(data))
            Init_Binary(data, Make_Binary(OUT_BUF_SIZE));

        REBSER *ser = VAL_SERIES(data);
        SET_SERIES_LEN(ser, 0);
        TERM_SERIES(ser);

        req->common.data = BIN_HEAD(ser);
        req->length = SER_AVAIL(ser);

        REBVAL *r_result = OS_DO_DEVICE(req, RDC_READ);
        assert(r_result != NULL);
        if (rebDid("lib/error?", r_result, END))
            rebFail (r_result, END);
        rebRelease(r_result); // ignore result

        // !!! Among many confusions in this file, it said "Another copy???"
        //
        Init_Binary(D_OUT, Copy_Bytes(req->common.data, req->actual));
        return R_OUT; }

    case SYM_OPEN: {
        req->flags |= RRF_OPEN;
        goto return_port; }

    case SYM_CLOSE:
        req->flags &= ~RRF_OPEN;
        //OS_DO_DEVICE(req, RDC_CLOSE);
        goto return_port;

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));

return_port:
    Move_Value(D_OUT, D_ARG(1));
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
