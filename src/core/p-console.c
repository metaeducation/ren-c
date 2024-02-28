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
static REB_R Console_Actor(REBFRM *frame_, Value* port, Value* verb)
{
    REBCTX *ctx = VAL_CONTEXT(port);
    REBREQ *req = Ensure_Port_State(port, RDI_STDIO);

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implied by `port`
        Option(SymId) property = Cell_Word_Id(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_OPEN_Q:
            return Init_Logic(D_OUT, did (req->flags & RRF_OPEN));

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
        if (not (req->flags & RRF_OPEN))
            OS_DO_DEVICE_SYNC(req, RDC_OPEN);

        // If no buffer, create a buffer:
        //
        Value* data = CTX_VAR(ctx, STD_PORT_DATA);
        if (not IS_BINARY(data))
            Init_Binary(data, Make_Binary(OUT_BUF_SIZE));

        REBSER *ser = VAL_SERIES(data);
        SET_SERIES_LEN(ser, 0);
        TERM_SERIES(ser);

        req->common.data = BIN_HEAD(ser);
        req->length = SER_AVAIL(ser);

        OS_DO_DEVICE_SYNC(req, RDC_READ);

        // !!! Among many confusions in this file, it said "Another copy???"
        //
        Init_Binary(D_OUT, Copy_Bytes(req->common.data, req->actual));
        return D_OUT; }

    case SYM_OPEN: {
        req->flags |= RRF_OPEN;
        RETURN (port); }

    case SYM_CLOSE:
        req->flags &= ~RRF_OPEN;
        //OS_DO_DEVICE(req, RDC_CLOSE);
        RETURN (port);

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));
}


//
//  get-console-actor-handle: native [
//
//  {Retrieve handle to the native actor for console}
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(get_console_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Console_Actor);
    return D_OUT;
}
