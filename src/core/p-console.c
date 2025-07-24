//
//  file: %p-console.c
//  summary: "console port interface"
//  section: ports
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
static Bounce Console_Actor(Level* level_, Value* port, Value* verb)
{
    VarList* ctx = Cell_Varlist(port);
    REBREQ *req = Ensure_Port_State(port, RDI_STDIO);

    switch (opt Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(VALUE)); // implied by `port`
        Option(SymId) property = Word_Id(ARG(PROPERTY));

        switch (opt property) {
        case SYM_OPEN_Q:
            return Init_Logic(OUT, did (req->flags & RRF_OPEN));

        default:
            break;
        }

        break; }

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PARAM(SOURCE));

        if (Bool_ARG(PART)) {
            UNUSED(ARG(LIMIT));
            panic (Error_Bad_Refines_Raw());
        }
        if (Bool_ARG(SEEK)) {
            UNUSED(ARG(INDEX));
            panic (Error_Bad_Refines_Raw());
        }
        UNUSED(PARAM(STRING)); // handled in dispatcher
        UNUSED(PARAM(LINES)); // handled in dispatcher

        // If not open, open it:
        if (not (req->flags & RRF_OPEN))
            OS_DO_DEVICE_SYNC(req, RDC_OPEN);

        // If no buffer, create a buffer:
        //
        Value* data = Varlist_Slot(ctx, STD_PORT_DATA);
        if (not Is_Blob(data))
            Init_Blob(data, Make_Binary(OUT_BUF_SIZE));

        Binary* flex = Cell_Binary(data);
        Set_Flex_Len(flex, 0);
        Term_Flex(flex);

        req->common.data = Binary_Head(flex);
        req->length = Flex_Available_Space(flex);

        OS_DO_DEVICE_SYNC(req, RDC_READ);

        // !!! Among many confusions in this file, it said "Another copy???"
        //
        Init_Blob(OUT, Copy_Bytes(req->common.data, req->actual));
        return OUT; }

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

    panic (Error_Illegal_Action(TYPE_PORT, verb));
}


//
//  get-console-actor-handle: native [
//
//  {Retrieve handle to the native actor for console}
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(GET_CONSOLE_ACTOR_HANDLE)
{
    Make_Port_Actor_Handle(OUT, &Console_Actor);
    return OUT;
}
