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

#include "sys-core.h"
#include "reb-net.h"


//
//  DNS_Actor: C
//
static REB_R DNS_Actor(struct Reb_Frame *frame_, REBCTX *port, REBSYM action)
{
    REBVAL *spec;
    REBREQ *sock;
    REBINT result;
    REBVAL *arg;
    REBCNT len;
    REBOOL sync = FALSE; // act synchronously

    REBVAL tmp;

    Validate_Port(port, action);

    arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    *D_OUT = *D_ARG(1);

    sock = cast(REBREQ*, Use_Port_State(port, RDI_DNS, sizeof(*sock)));
    spec = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec)) fail (Error(RE_INVALID_PORT));

    sock->timeout = 4000; // where does this go? !!!

    switch (action) {

    case SYM_READ:
        if (!IS_OPEN(sock)) {
            if (OS_DO_DEVICE(sock, RDC_OPEN))
                fail (Error_On_Port(RE_CANNOT_OPEN, port, sock->error));
            sync = TRUE;
        }

        arg = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);

        if (IS_TUPLE(arg) && Scan_Tuple(VAL_BIN(arg), LEN_BYTES(VAL_BIN(arg)), &tmp)) {
            SET_FLAG(sock->modes, RST_REVERSE);
            memcpy(&sock->special.net.remote_ip, VAL_TUPLE(&tmp), 4);
        }
        else if (IS_STRING(arg)) {
            sock->common.data = VAL_BIN(arg);
        }
        else
            fail (Error_On_Port(RE_INVALID_SPEC, port, -10));

        result = OS_DO_DEVICE(sock, RDC_READ);
        if (result < 0)
            fail (Error_On_Port(RE_READ_ERROR, port, sock->error));

        // Wait for it...
        if (sync && result == DR_PEND) {
            for (len = 0; GET_FLAG(sock->flags, RRF_PENDING) && len < 10; len++) {
                OS_WAIT(2000, 0);
            }
            len = 1;
            goto pick;
        }
        if (result == DR_DONE) {
            len = 1;
            goto pick;
        }
        break;

    case SYM_PICK:  // FIRST - return result
        if (!IS_OPEN(sock))
            fail (Error_On_Port(RE_NOT_OPEN, port, -12));

        len = Get_Num_From_Arg(arg); // Position
pick:
        if (len == 1) {
            if (
                !sock->special.net.host_info
                || !GET_FLAG(sock->flags, RRF_DONE
            )) {
                return R_VOID;
            }
            if (sock->error) {
                OS_DO_DEVICE(sock, RDC_CLOSE);
                fail (Error_On_Port(RE_READ_ERROR, port, sock->error));
            }
            if (GET_FLAG(sock->modes, RST_REVERSE)) {
                Val_Init_String(D_OUT, Copy_Bytes(sock->common.data, LEN_BYTES(sock->common.data)));
            } else {
                Set_Tuple(D_OUT, cast(REBYTE*, &sock->special.net.remote_ip), 4);
            }
            OS_DO_DEVICE(sock, RDC_CLOSE);
        }
        else
            fail (Error_Out_Of_Range(arg));
        break;

    case SYM_OPEN:
        if (OS_DO_DEVICE(sock, RDC_OPEN))
            fail (Error_On_Port(RE_CANNOT_OPEN, port, -12));
        break;

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
//  Init_DNS_Scheme: C
//
void Init_DNS_Scheme(void)
{
    Register_Scheme(Canon(SYM_DNS), 0, DNS_Actor);
}
