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
#include "reb-evtypes.h"

#define NET_BUF_SIZE 32*1024

enum Transport_Types {
    TRANSPORT_TCP,
    TRANSPORT_UDP
};

//
//  Ret_Query_Net: C
//
static void Ret_Query_Net(REBCTX *port, struct devreq_net *sock, REBVAL *out)
{
    REBVAL *std_info = In_Object(port, STD_PORT_SCHEME, STD_SCHEME_INFO, 0);
    REBCTX *info;

    if (!std_info || !IS_OBJECT(std_info))
        fail (Error_On_Port(RE_INVALID_SPEC, port, -10));

    info = Copy_Context_Shallow(VAL_CONTEXT(std_info));

    Set_Tuple(
        CTX_VAR(info, STD_NET_INFO_LOCAL_IP),
        cast(REBYTE*, &sock->local_ip),
        4
    );
    Init_Integer(
        CTX_VAR(info, STD_NET_INFO_LOCAL_PORT),
        sock->local_port
    );

    Set_Tuple(
        CTX_VAR(info, STD_NET_INFO_REMOTE_IP),
        cast(REBYTE*, &sock->remote_ip),
        4
    );
    Init_Integer(
        CTX_VAR(info, STD_NET_INFO_REMOTE_PORT),
        sock->remote_port
    );

    Init_Object(out, info);
}


//
//  Accept_New_Port: C
//
// Clone a listening port as a new accept port.
//
static void Accept_New_Port(REBVAL *out, REBCTX *port, struct devreq_net *sock)
{
    struct devreq_net *nsock;
    REBREQ *req = AS_REBREQ(sock);

    // Get temp sock struct created by the device:
    nsock = cast(struct devreq_net*, req->common.sock);
    if (!nsock) return;  // false alarm
    req->common.sock = AS_REBREQ(nsock)->next;
    REBREQ *nreq = AS_REBREQ(nsock);
    nreq->common.data = 0;
    nreq->next = 0;

    // Create a new port using ACCEPT request passed by sock->common.sock:
    port = Copy_Context_Shallow(port);
    Init_Port(out, port); // Also for GC protect

    Init_Blank(CTX_VAR(port, STD_PORT_DATA)); // just to be sure.
    Init_Blank(CTX_VAR(port, STD_PORT_STATE)); // just to be sure.

    // Copy over the new sock data:
    sock = cast(struct devreq_net*, Ensure_Port_State(port, RDI_NET));
    *sock = *nsock;
    AS_REBREQ(sock)->port = port;
    OS_FREE(nsock); // allocated by dev_net.c (MT issues?)
}

//
//  Transport_Actor: C
//
static REB_R Transport_Actor(
    REBFRM *frame_,
    REBCTX *port,
    REBSYM action,
    enum Transport_Types proto
) {
    // Initialize the IO request
    //
    REBREQ *sock = Ensure_Port_State(port, RDI_NET);
    if (proto == TRANSPORT_UDP)
        SET_FLAG(sock->modes, RST_UDP);

    REBVAL *spec = CTX_VAR(port, STD_PORT_SPEC);
    if (!IS_OBJECT(spec))
        fail (Error_Invalid_Port_Raw());

    // sock->timeout = 4000; // where does this go? !!!

    // !!! Comment said "HOW TO PREVENT OVERWRITE DURING BUSY OPERATION!!!
    // Should it just ignore it or cause an error?"

    // Actions for an unopened socket:

    if (!IS_OPEN(sock)) {

        switch (action) { // Ordered by frequency

        case SYM_OPEN: {
            REBVAL *arg = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);
            REBVAL *val = Obj_Value(spec, STD_PORT_SPEC_NET_PORT_ID);

            if (OS_DO_DEVICE(sock, RDC_OPEN))
                fail (Error_On_Port(RE_CANNOT_OPEN, port, -12));
            SET_OPEN(sock);

            // Lookup host name (an extra TCP device step):
            if (IS_STRING(arg)) {
                sock->common.data = VAL_BIN(arg);
                DEVREQ_NET(sock)->remote_port =
                    IS_INTEGER(val) ? VAL_INT32(val) : 80;

                // Note: sets remote_ip field
                //
                REBINT result = OS_DO_DEVICE(sock, RDC_LOOKUP);
                if (result < 0)
                    fail (Error_On_Port(RE_NO_CONNECT, port, sock->error));

                Move_Value(D_OUT, CTX_VALUE(port));
                return R_OUT;
            }
            else if (IS_TUPLE(arg)) { // Host IP specified:
                DEVREQ_NET(sock)->remote_port =
                    IS_INTEGER(val) ? VAL_INT32(val) : 80;
                memcpy(&(DEVREQ_NET(sock)->remote_ip), VAL_TUPLE(arg), 4);
                break;
            }
            else if (IS_BLANK(arg)) { // No host, must be a LISTEN socket:
                SET_FLAG(sock->modes, RST_LISTEN);
                sock->common.sock = 0; // where ACCEPT requests are queued
                DEVREQ_NET(sock)->local_port =
                    IS_INTEGER(val) ? VAL_INT32(val) : 8000;
                break;
            }
            else
                fail (Error_On_Port(RE_INVALID_SPEC, port, -10));
            break; }

        case SYM_CLOSE:
            Move_Value(D_OUT, CTX_VALUE(port));
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

    switch (action) { // Ordered by frequency

    case SYM_UPDATE: {
        //
        // Update the port object after a READ or WRITE operation.
        // This is normally called by the WAKE-UP function.
        //
        REBVAL *port_data = CTX_VAR(port, STD_PORT_DATA);
        if (sock->command == RDC_READ) {
            if (ANY_BINSTR(port_data)) {
                SET_SERIES_LEN(
                    VAL_SERIES(port_data),
                    VAL_LEN_HEAD(port_data) + sock->actual
                );
            }
        }
        else if (sock->command == RDC_WRITE) {
            Init_Blank(port_data); // Write is done.
        }
        return R_BLANK; }

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

        // Read data into a buffer, expanding the buffer if needed.
        // If no length is given, program must stop it at some point.
        if (
            !GET_FLAG(sock->modes, RST_UDP)
            && !GET_FLAG(sock->state, RSM_CONNECT)
        ) {
            fail (Error_On_Port(RE_NOT_CONNECTED, port, -15));
        }

        // Setup the read buffer (allocate a buffer if needed):
        //
        REBVAL *port_data = CTX_VAR(port, STD_PORT_DATA);
        REBSER *buffer;
        if (!IS_STRING(port_data) && !IS_BINARY(port_data)) {
            buffer = Make_Binary(NET_BUF_SIZE);
            Init_Binary(port_data, buffer);
        }
        else {
            buffer = VAL_SERIES(port_data);
            assert(BYTE_SIZE(buffer));

            if (SER_AVAIL(buffer) < NET_BUF_SIZE/2)
                Extend_Series(buffer, NET_BUF_SIZE);
        }

        sock->length = SER_AVAIL(buffer);
        sock->common.data = BIN_TAIL(buffer); // write at tail
        sock->actual = 0; // actual for THIS read (not for total)

        // Note: recv can happen immediately
        //
        REBINT result = OS_DO_DEVICE(sock, RDC_READ);
        if (result < 0)
            fail (Error_On_Port(RE_READ_ERROR, port, sock->error));

        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PAR(destination));

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

        // Write the entire argument string to the network.
        // The lower level write code continues until done.

        if (
            !GET_FLAG(sock->modes, RST_UDP)
            && !GET_FLAG(sock->state, RSM_CONNECT)
        ){
            fail (Error_On_Port(RE_NOT_CONNECTED, port, -15));
        }

        // Determine length. Clip /PART to size of string if needed.
        REBVAL *data = ARG(data);

        REBCNT len = VAL_LEN_AT(data);
        if (REF(part)) {
            REBCNT n = Int32s(ARG(limit), 0);
            if (n <= len)
                len = n;
        }

        // Setup the write:

        Move_Value(CTX_VAR(port, STD_PORT_DATA), data); // keep it GC safe
        sock->length = len;
        sock->common.data = VAL_BIN_AT(data);
        sock->actual = 0;

        // Note: send can happen immediately
        //
        REBINT result = OS_DO_DEVICE(sock, RDC_WRITE);
        if (result < 0)
            fail (Error_On_Port(RE_WRITE_ERROR, port, sock->error));

        if (result == DR_DONE)
            Init_Blank(CTX_VAR(port, STD_PORT_DATA));

        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(PAR(aggregate));

        // FIRST server-port returns new port connection.
        //
        REBCNT len = Get_Num_From_Arg(ARG(index));
        if (len == 1 && GET_FLAG(sock->modes, RST_LISTEN) && sock->common.data)
            Accept_New_Port(SINK(D_OUT), port, DEVREQ_NET(sock));
        else
            fail (Error_Out_Of_Range(ARG(index)));
        return R_OUT; }

    case SYM_QUERY: {
        //
        // Get specific information - the scheme's info object.
        // Special notation allows just getting part of the info.
        //
        Ret_Query_Net(port, DEVREQ_NET(sock), D_OUT);
        return R_OUT; }

    case SYM_OPEN_Q:
        //
        // Connect for clients, bind for servers:
        //
        return R_FROM_BOOL (
            LOGICAL(sock->state & ((1 << RSM_CONNECT) | (1 << RSM_BIND)))
        );

    case SYM_CLOSE: {
        if (IS_OPEN(sock)) {
            OS_DO_DEVICE(sock, RDC_CLOSE);
            SET_CLOSED(sock);
        }
        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    case SYM_LENGTH: {
        REBVAL *port_data = CTX_VAR(port, STD_PORT_DATA);
        Init_Integer(
            D_OUT,
            ANY_SERIES(port_data) ? VAL_LEN_HEAD(port_data) : 0
        );
        return R_OUT; }

    case SYM_OPEN: {
        REBINT result = OS_DO_DEVICE(sock, RDC_CONNECT);
        if (result < 0)
            fail (Error_On_Port(RE_NO_CONNECT, port, sock->error));
        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    case SYM_DELETE: {
        //
        // !!! Comment said "Temporary to TEST error handler!"
        //
        REBVAL *event = Append_Event(); // sets signal
        Reset_Val_Header(event, REB_EVENT); // has more space, if needed
        VAL_EVENT_TYPE(event) = EVT_ERROR;
        VAL_EVENT_DATA(event) = 101;
        VAL_EVENT_REQ(event) = sock;
        Move_Value(D_OUT, CTX_VALUE(port));
        return R_OUT; }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, action));
}


//
//  TCP_Actor: C
//
static REB_R TCP_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    return Transport_Actor(frame_, port, action, TRANSPORT_TCP);
}


//
//  UDP_Actor: C
//
static REB_R UDP_Actor(REBFRM *frame_, REBCTX *port, REBSYM action)
{
    return Transport_Actor(frame_, port, action, TRANSPORT_UDP);
}


//
//  get-tcp-actor-handle: native [
//
//  {Retrieve handle to the native actor for TCP}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_tcp_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &TCP_Actor);
    return R_OUT;
}


//
//  get-udp-actor-handle: native [
//
//  {Retrieve handle to the native actor for UDP}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_udp_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &UDP_Actor);
    return R_OUT;
}


//
//  set-udp-multicast: native [
//
//  {Join (or leave) an IPv4 multicast group}
//
//      return: [<opt>]
//      port [port!]
//          {An open UDP port}
//      group [tuple!]
//          {Multicast group to join (224.0.0.0 to 239.255.255.255)}
//      member [tuple!]
//          {Member to add to multicast group (use 0.0.0.0 for INADDR_ANY)}
//      /drop
//          {Leave the group (default is to add)}
//  ]
//
REBNATIVE(set_udp_multicast)
//
// !!! SET-MODES was never standardized or implemented for R3-Alpha, so there
// was no RDC_MODIFY written.  While it is tempting to just go ahead and
// start writing `setsockopt` calls right here in this file, that would mean
// adding platform-sensitive network includes into the core.
//
// Ultimately, the desire is that ports would be modules--consisting of some
// Rebol code, and some C code (possibly with platform-conditional libs).
// This is the direction for the extension model, where the artificial limit
// of having "native port actors" that can't just do the OS calls they want
// will disappear.
//
// Until that happens, we want to pass this through to the Reb_Device layer
// somehow.  It's not easy to see how to modify this "REBREQ" which is
// actually *the port's state* to pass it the necessary information for this
// request.  Hence the cheat is just to pass it the frame, and then let
// Reb_Device implementations go ahead and use the extension API to pick
// that frame apart.
{
    INCLUDE_PARAMS_OF_SET_UDP_MULTICAST;

    REBCTX *port = VAL_CONTEXT(ARG(port));
    REBREQ *sock = Ensure_Port_State(port, RDI_NET);

    sock->common.data = cast(REBYTE*, frame_);

    // sock->command is going to just be RDC_MODIFY, so all there is to go
    // by is the data and flags.  Since RFC3171 specifies IPv4 multicast
    // address space...how about that?
    //
    sock->flags = 3171;

    UNUSED(ARG(group));
    UNUSED(ARG(member));
    UNUSED(REF(drop));

    REBINT result = OS_DO_DEVICE(sock, RDC_MODIFY);
    if (result < 0)
        fail ("SET-UDP-MULTICAST failure"); // can device layer just fail()?

    return R_VOID;
}


//
//  set-udp-ttl: native [
//
//  {Set the TTL of a UDP port}
//
//      return: [<opt>]
//      port [port!]
//          {An open UDP port}
//      ttl [integer!]
//          {0 = local machine only, 1 = subnet (default), or up to 255}
//  ]
//
REBNATIVE(set_udp_ttl)
{
    INCLUDE_PARAMS_OF_SET_UDP_TTL;

    REBCTX *port = VAL_CONTEXT(ARG(port));
    REBREQ *sock = Ensure_Port_State(port, RDI_NET);

    sock->common.data = cast(REBYTE*, frame_);

    // sock->command is going to just be RDC_MODIFY, so all there is to go
    // by is the data and flags.  Since RFC2365 specifies IPv4 multicast
    // administrative boundaries...how about that?
    //
    sock->flags = 2365;

    UNUSED(ARG(ttl));

    REBINT result = OS_DO_DEVICE(sock, RDC_MODIFY);
    if (result < 0)
        fail ("SET-UDP-TTL failure"); // can device layer just fail()?

    return R_VOID;
}
