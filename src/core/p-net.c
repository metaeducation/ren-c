//
//  File: %p-net.c
//  Summary: "network port interface"
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
#include "reb-evtypes.h"

#define NET_BUF_SIZE 32*1024

enum Transport_Types {
    TRANSPORT_TCP,
    TRANSPORT_UDP
};

//
//  Ret_Query_Net: C
//
static void Ret_Query_Net(REBCTX *port, REBREQ *sock, REBVAL *out)
{
    REBVAL *std_info = In_Object(port, STD_PORT_SCHEME, STD_SCHEME_INFO, 0);
    REBCTX *info;

    if (!std_info || !IS_OBJECT(std_info))
        fail (Error_On_Port(RE_INVALID_SPEC, port, -10));

    info = Copy_Context_Shallow(VAL_CONTEXT(std_info));

    Set_Tuple(
        CTX_VAR(info, STD_NET_INFO_LOCAL_IP),
        cast(REBYTE*, &sock->special.net.local_ip),
        4
    );
    SET_INTEGER(
        CTX_VAR(info, STD_NET_INFO_LOCAL_PORT),
        sock->special.net.local_port
    );

    Set_Tuple(
        CTX_VAR(info, STD_NET_INFO_REMOTE_IP),
        cast(REBYTE*, &sock->special.net.remote_ip),
        4
    );
    SET_INTEGER(
        CTX_VAR(info, STD_NET_INFO_REMOTE_PORT),
        sock->special.net.remote_port
    );

    Val_Init_Object(out, info);
}


//
//  Accept_New_Port: C
// 
// Clone a listening port as a new accept port.
//
static void Accept_New_Port(REBVAL *out, REBCTX *port, REBREQ *sock)
{
    REBREQ *nsock;

    // Get temp sock struct created by the device:
    nsock = sock->common.sock;
    if (!nsock) return;  // false alarm
    sock->common.sock = nsock->next;
    nsock->common.data = 0;
    nsock->next = 0;

    // Create a new port using ACCEPT request passed by sock->common.sock:
    port = Copy_Context_Shallow(port);
    Val_Init_Port(out, port); // Also for GC protect

    SET_BLANK(CTX_VAR(port, STD_PORT_DATA)); // just to be sure.
    SET_BLANK(CTX_VAR(port, STD_PORT_STATE)); // just to be sure.

    // Copy over the new sock data:
    sock = cast(REBREQ*, Use_Port_State(port, RDI_NET, sizeof(*sock)));
    *sock = *nsock;
    sock->clen = sizeof(*sock);
    sock->port = port;
    OS_FREE(nsock); // allocated by dev_net.c (MT issues?)
}

//
//  Transport_Actor: C
//
static REB_R Transport_Actor(
    struct Reb_Frame *frame_,
    REBCTX *port,
    REBSYM action,
    enum Transport_Types proto
) {
    REBREQ *sock;   // IO request
    REBVAL *spec;   // port spec
    REBVAL *arg;    // action argument value
    REBVAL *val;    // e.g. port number value
    REBINT result;  // IO result
    REBCNT refs;    // refinement argument flags
    REBCNT len;     // generic length
    REBSER *ser;    // simplifier

    Validate_Port(port, action);

    *D_OUT = *D_ARG(1);
    arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    sock = cast(REBREQ*, Use_Port_State(port, RDI_NET, sizeof(*sock)));
    if (proto == TRANSPORT_UDP) {
        SET_FLAG(sock->modes, RST_UDP);
    }
    //Debug_Fmt("Sock: %x", sock);
    spec = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec)) fail (Error(RE_INVALID_PORT));

    // sock->timeout = 4000; // where does this go? !!!

    // HOW TO PREVENT OVERWRITE DURING BUSY OPERATION!!!
    // Should it just ignore it or cause an error?

    // Actions for an unopened socket:
    if (!IS_OPEN(sock)) {

        switch (action) {   // Ordered by frequency

        case SYM_OPEN:

            arg = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);
            val = Obj_Value(spec, STD_PORT_SPEC_NET_PORT_ID);

            if (OS_DO_DEVICE(sock, RDC_OPEN))
                fail (Error_On_Port(RE_CANNOT_OPEN, port, -12));
            SET_OPEN(sock);

            // Lookup host name (an extra TCP device step):
            if (IS_STRING(arg)) {
                sock->common.data = VAL_BIN(arg);
                sock->special.net.remote_port = IS_INTEGER(val) ? VAL_INT32(val) : 80;
                result = OS_DO_DEVICE(sock, RDC_LOOKUP);  // sets remote_ip field
                if (result < 0)
                    fail (Error_On_Port(RE_NO_CONNECT, port, sock->error));
                return R_OUT;
            }

            // Host IP specified:
            else if (IS_TUPLE(arg)) {
                sock->special.net.remote_port = IS_INTEGER(val) ? VAL_INT32(val) : 80;
                memcpy(&sock->special.net.remote_ip, VAL_TUPLE(arg), 4);
                break;
            }

            // No host, must be a LISTEN socket:
            else if (IS_BLANK(arg)) {
                SET_FLAG(sock->modes, RST_LISTEN);
                sock->common.data = 0; // where ACCEPT requests are queued
                sock->special.net.local_port = IS_INTEGER(val) ? VAL_INT32(val) : 8000;
                break;
            }
            else
                fail (Error_On_Port(RE_INVALID_SPEC, port, -10));

        case SYM_CLOSE:
            return R_OUT;

        case SYM_OPEN_Q:
            return R_FALSE;

        case SYM_UPDATE:  // allowed after a close
            break;

        default:
            fail (Error_On_Port(RE_NOT_OPEN, port, -12));
        }
    }

    // Actions for an open socket:
    switch (action) {   // Ordered by frequency

    case SYM_UPDATE:
        // Update the port object after a READ or WRITE operation.
        // This is normally called by the WAKE-UP function.
        arg = CTX_VAR(port, STD_PORT_DATA);
        if (sock->command == RDC_READ) {
            if (ANY_BINSTR(arg)) {
                SET_SERIES_LEN(
                    VAL_SERIES(arg),
                    VAL_LEN_HEAD(arg) + sock->actual
                );
            }
        }
        else if (sock->command == RDC_WRITE) {
            SET_BLANK(arg);  // Write is done.
        }
        return R_BLANK;

    case SYM_READ:
        // Read data into a buffer, expanding the buffer if needed.
        // If no length is given, program must stop it at some point.
        refs = Find_Refines(frame_, ALL_READ_REFS);
        if (
            !GET_FLAG(sock->modes, RST_UDP)
            && !GET_FLAG(sock->state, RSM_CONNECT)
        ) {
            fail (Error_On_Port(RE_NOT_CONNECTED, port, -15));
        }

        // Setup the read buffer (allocate a buffer if needed):
        arg = CTX_VAR(port, STD_PORT_DATA);
        if (!IS_STRING(arg) && !IS_BINARY(arg)) {
            Val_Init_Binary(arg, Make_Binary(NET_BUF_SIZE));
        }
        ser = VAL_SERIES(arg);
        sock->length = SER_AVAIL(ser); // space available
        if (sock->length < NET_BUF_SIZE/2) Extend_Series(ser, NET_BUF_SIZE);
        sock->length = SER_AVAIL(ser);

        // This used STR_TAIL (obsolete, equivalent to BIN_TAIL) but was it
        // sure the series was byte sized?  Added in a check.
        assert(BYTE_SIZE(ser));
        sock->common.data = BIN_TAIL(ser); // write at tail

        //if (SER_LEN(ser) == 0)
        sock->actual = 0;  // Actual for THIS read, not for total.

        //Print("(max read length %d)", sock->length);
        result = OS_DO_DEVICE(sock, RDC_READ); // recv can happen immediately
        if (result < 0) fail (Error_On_Port(RE_READ_ERROR, port, sock->error));
        break;

    case SYM_WRITE:
        // Write the entire argument string to the network.
        // The lower level write code continues until done.

        refs = Find_Refines(frame_, ALL_WRITE_REFS);
        if (!GET_FLAG(sock->modes, RST_UDP)
            && !GET_FLAG(sock->state, RSM_CONNECT))
            fail (Error_On_Port(RE_NOT_CONNECTED, port, -15));

        // Determine length. Clip /PART to size of string if needed.
        spec = D_ARG(2);
        len = VAL_LEN_AT(spec);
        if (refs & AM_WRITE_PART) {
            REBCNT n = Int32s(D_ARG(ARG_WRITE_LIMIT), 0);
            if (n <= len) len = n;
        }

        // Setup the write:
        *CTX_VAR(port, STD_PORT_DATA) = *spec;  // keep it GC safe
        sock->length = len;
        sock->common.data = VAL_BIN_AT(spec);
        sock->actual = 0;

        //Print("(write length %d)", len);
        result = OS_DO_DEVICE(sock, RDC_WRITE); // send can happen immediately
        if (result < 0) fail (Error_On_Port(RE_WRITE_ERROR, port, sock->error));
        if (result == DR_DONE) SET_BLANK(CTX_VAR(port, STD_PORT_DATA));
        break;

    case SYM_PICK:
        // FIRST server-port returns new port connection.
        len = Get_Num_From_Arg(arg); // Position
        if (len == 1 && GET_FLAG(sock->modes, RST_LISTEN) && sock->common.data)
            Accept_New_Port(D_OUT, port, sock); // sets D_OUT
        else
            fail (Error_Out_Of_Range(arg));
        break;

    case SYM_QUERY:
        // Get specific information - the scheme's info object.
        // Special notation allows just getting part of the info.
        Ret_Query_Net(port, sock, D_OUT);
        break;

    case SYM_OPEN_Q:
        // Connect for clients, bind for servers:
        if (sock->state & ((1<<RSM_CONNECT) | (1<<RSM_BIND))) return R_TRUE;
        return R_FALSE;

    case SYM_CLOSE:
        if (IS_OPEN(sock)) {
            OS_DO_DEVICE(sock, RDC_CLOSE);
            SET_CLOSED(sock);
        }
        break;

    case SYM_LENGTH:
        arg = CTX_VAR(port, STD_PORT_DATA);
        len = ANY_SERIES(arg) ? VAL_LEN_HEAD(arg) : 0;
        SET_INTEGER(D_OUT, len);
        break;

    case SYM_OPEN:
        result = OS_DO_DEVICE(sock, RDC_CONNECT);
        if (result < 0)
            fail (Error_On_Port(RE_NO_CONNECT, port, sock->error));
        break;

    case SYM_DELETE: // Temporary to TEST error handler!
        {
            REBVAL *event = Append_Event();     // sets signal
            VAL_RESET_HEADER(event, REB_EVENT); // has more space, if needed
            VAL_EVENT_TYPE(event) = EVT_ERROR;
            VAL_EVENT_DATA(event) = 101;
            VAL_EVENT_REQ(event) = sock;
        }
        break;

    default:
        fail (Error_Illegal_Action(REB_PORT, action));
    }

    return R_OUT;
}

//
//  TCP_Actor: C
//
static REB_R TCP_Actor(struct Reb_Frame *frame_, REBCTX *port, REBSYM action)
{
    return Transport_Actor(frame_, port, action, TRANSPORT_TCP);
}

//
//  UDP_Actor: C
//
static REB_R UDP_Actor(struct Reb_Frame *frame_, REBCTX *port, REBSYM action)
{
    return Transport_Actor(frame_, port, action, TRANSPORT_UDP);
}

//
//  Init_TCP_Scheme: C
//
void Init_TCP_Scheme(void)
{
    Register_Scheme(Canon(SYM_TCP), 0, TCP_Actor);
}

//
//  Init_UDP_Scheme: C
//
void Init_UDP_Scheme(void)
{
    Register_Scheme(Canon(SYM_UDP), 0, UDP_Actor);
}
