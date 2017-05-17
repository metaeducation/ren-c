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
static REB_R DNS_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    REBVAL *spec;
    REBINT result;
    REBVAL *arg;
    REBCNT len;
    REBOOL sync = FALSE; // act synchronously

    arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    Move_Value(D_OUT, D_ARG(1));

    REBREQ *sock = Ensure_Port_State(port, RDI_DNS);
    spec = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec)) fail (Error_Invalid_Port_Raw());

    sock->timeout = 4000; // where does this go? !!!

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

        if (!IS_OPEN(sock)) {
            if (OS_DO_DEVICE(sock, RDC_OPEN))
                fail (Error_On_Port(RE_CANNOT_OPEN, port, sock->error));
            sync = TRUE;
        }

        arg = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);

        // A DNS read e.g. of `read dns://66.249.66.140` should do a reverse
        // lookup.  The scheme handler may pass in either a TUPLE! or a string
        // that scans to a tuple, at this time (currently uses a string)
        //
        if (IS_TUPLE(arg)) {
            SET_FLAG(sock->modes, RST_REVERSE);
            memcpy(&(DEVREQ_NET(sock)->remote_ip), VAL_TUPLE(arg), 4);
        }
        else if (IS_STRING(arg)) {
            REBCNT index = VAL_INDEX(arg);
            REBCNT len = VAL_LEN_AT(arg);
            REBSER *utf8 = Temp_Bin_Str_Managed(arg, &index, &len);

            DECLARE_LOCAL (tmp);
            if (Scan_Tuple(tmp, BIN_AT(utf8, index), len) != NULL) {
                SET_FLAG(sock->modes, RST_REVERSE);
                memcpy(&(DEVREQ_NET(sock)->remote_ip), VAL_TUPLE(tmp), 4);
            }
            else
                sock->common.data = VAL_BIN(arg); // lookup string's IP address
        }
        else
            fail (Error_On_Port(RE_INVALID_SPEC, port, -10));

        result = OS_DO_DEVICE(sock, RDC_READ);
        if (result < 0)
            fail (Error_On_Port(RE_READ_ERROR, port, sock->error));

        if (sync && result == DR_PEND) {
            assert(FALSE); // asynchronous R3-Alpha DNS code removed
            len = 0;
            for (; GET_FLAG(sock->flags, RRF_PENDING) && len < 10; ++len) {
                OS_WAIT(2000, 0);
            }
            len = 1;
            goto pick;
        }
        if (result == DR_DONE) {
            len = 1;
            goto pick;
        }
        break; }

    case SYM_PICK_P:  // FIRST - return result
        if (!IS_OPEN(sock))
            fail (Error_On_Port(RE_NOT_OPEN, port, -12));

        len = Get_Num_From_Arg(arg); // Position
    pick:
        if (len != 1)
            fail (Error_Out_Of_Range(arg));

        assert(GET_FLAG(sock->flags, RRF_DONE)); // R3-Alpha async DNS removed

        if (sock->error) {
            OS_DO_DEVICE(sock, RDC_CLOSE);
            fail (Error_On_Port(RE_READ_ERROR, port, sock->error));
        }

        if (DEVREQ_NET(sock)->host_info == NULL) {
            Init_Blank(D_OUT); // HOST_NOT_FOUND or NO_ADDRESS blank vs. error
            return R_OUT; // READ action currently required to use R_OUTs
        }

        if (GET_FLAG(sock->modes, RST_REVERSE)) {
            Init_String(
                D_OUT,
                Copy_Bytes(sock->common.data, LEN_BYTES(sock->common.data))
            );
        }
        else {
            Set_Tuple(D_OUT, cast(REBYTE*, &DEVREQ_NET(sock)->remote_ip), 4);
        }
        OS_DO_DEVICE(sock, RDC_CLOSE);
        break;

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

        if (OS_DO_DEVICE(sock, RDC_OPEN))
            fail (Error_On_Port(RE_CANNOT_OPEN, port, -12));
        break; }

    case SYM_CLOSE:
        OS_DO_DEVICE(sock, RDC_CLOSE);
        break;

    case SYM_OPEN_Q:
        if (IS_OPEN(sock)) return R_TRUE;
        return R_FALSE;

    case SYM_UPDATE:
        return R_BLANK;

    default:
        fail (Error_Illegal_Action(REB_PORT, action));
    }

    return R_OUT;
}


//
//  get-dns-actor-handle: native [
//
//  {Retrieve handle to the native actor for DNS}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_dns_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &DNS_Actor);
    return R_OUT;
}
