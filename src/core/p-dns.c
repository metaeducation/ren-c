//
//  File: %p-dns.c
//  Summary: "DNS port interface"
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
#include "reb-net.h"


//
//  DNS_Actor: C
//
static Bounce DNS_Actor(Level* level_, Value* port, Value* verb)
{
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    REBREQ *sock = Ensure_Port_State(port, RDI_DNS);
    sock->timeout = 4000; // where does this go? !!!

    VarList* ctx = Cell_Varlist(port);
    Value* spec = Varlist_Slot(ctx, STD_PORT_SPEC);

    REBLEN len;

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(VALUE));
        Option(SymId) property = Cell_Word_Id(ARG(PROPERTY));
        assert(property != SYM_0);

        switch (property) {
        case SYM_OPEN_Q:
            return Init_Logic(OUT, did (sock->flags & RRF_OPEN));

        default:
            break;
        }

        break; }

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PARAM(SOURCE));
        if (Bool_ARG(PART)) {
            UNUSED(ARG(LIMIT));
            fail (Error_Bad_Refines_Raw());
        }

        if (Bool_ARG(SEEK)) {
            UNUSED(ARG(INDEX));
            fail (Error_Bad_Refines_Raw());
        }

        UNUSED(PARAM(STRING)); // handled in dispatcher
        UNUSED(PARAM(LINES)); // handled in dispatcher

        if (not (sock->flags & RRF_OPEN))
            OS_DO_DEVICE_SYNC(sock, RDC_OPEN);

        arg = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);

        // A DNS read e.g. of `read dns://66.249.66.140` should do a reverse
        // lookup.  The scheme handler may pass in either a TUPLE! or a string
        // that scans to a tuple, at this time (currently uses a string)
        //
        if (Is_Tuple(arg)) {
            sock->modes |= RST_REVERSE;
            memcpy(&(DEVREQ_NET(sock)->remote_ip), VAL_TUPLE(arg), 4);
        }
        else if (Is_Text(arg)) {
            Size offset;
            Size size;
            Binary* temp = Temp_UTF8_At_Managed(
                &offset, &size, arg, Cell_Series_Len_At(arg)
            );

            DECLARE_VALUE (tmp);
            if (Scan_Tuple(tmp, Binary_At(temp, offset), size) != nullptr) {
                sock->modes |= RST_REVERSE;
                memcpy(&(DEVREQ_NET(sock)->remote_ip), VAL_TUPLE(tmp), 4);
            }
            else // lookup string's IP address
                sock->common.data = Cell_Blob_Head(arg);
        }
        else
            fail (Error_On_Port(SYM_INVALID_SPEC, port, -10));

        OS_DO_DEVICE_SYNC(sock, RDC_READ);

        len = 1;
        goto pick; }

    case SYM_PICK: { // FIRST - return result
        if (not (sock->flags & RRF_OPEN))
            fail (Error_On_Port(SYM_NOT_OPEN, port, -12));

     pick:
        len = Get_Num_From_Arg(arg); // Position
        if (len != 1)
            fail (Error_Out_Of_Range(arg));

        assert(sock->flags & RRF_DONE); // R3-Alpha async DNS removed

        if (DEVREQ_NET(sock)->host_info == nullptr) {
            Init_Blank(OUT); // HOST_NOT_FOUND or NO_ADDRESS blank vs. error
            return OUT; // READ action currently required to use OUT
        }

        if (sock->modes & RST_REVERSE) {
            Init_Text(
                OUT,
                Copy_Bytes(sock->common.data, LEN_BYTES(sock->common.data))
            );
        }
        else {
            Set_Tuple(OUT, cast(Byte*, &DEVREQ_NET(sock)->remote_ip), 4);
        }

        OS_DO_DEVICE_SYNC(sock, RDC_CLOSE);
        return OUT; }

    case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PARAM(SPEC));
        if (Bool_ARG(NEW))
            fail (Error_Bad_Refines_Raw());
        if (Bool_ARG(READ))
            fail (Error_Bad_Refines_Raw());
        if (Bool_ARG(WRITE))
            fail (Error_Bad_Refines_Raw());
        if (Bool_ARG(SEEK))
            fail (Error_Bad_Refines_Raw());
        if (Bool_ARG(ALLOW)) {
            UNUSED(ARG(ACCESS));
            fail (Error_Bad_Refines_Raw());
        }

        OS_DO_DEVICE_SYNC(sock, RDC_OPEN);
        RETURN (port); }

    case SYM_CLOSE: {
        OS_DO_DEVICE_SYNC(sock, RDC_CLOSE);
        RETURN (port); }

    case SYM_ON_WAKE_UP:
        return Init_Nothing(OUT);

    default:
        break;
    }

    fail (Error_Illegal_Action(TYPE_PORT, verb));
}


//
//  get-dns-actor-handle: native [
//
//  {Retrieve handle to the native actor for DNS}
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(GET_DNS_ACTOR_HANDLE)
{
    Make_Port_Actor_Handle(OUT, &DNS_Actor);
    return OUT;
}
