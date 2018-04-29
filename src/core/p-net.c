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
    REBSYM verb,
    enum Transport_Types proto
){
    FAIL_IF_BAD_PORT(port);

    // Initialize the IO request
    //
    REBREQ *sock = Ensure_Port_State(port, RDI_NET);
    if (proto == TRANSPORT_UDP)
        sock->modes |= RST_UDP;

    REBVAL *spec = CTX_VAR(port, STD_PORT_SPEC);

    // sock->timeout = 4000; // where does this go? !!!

    // !!! Comment said "HOW TO PREVENT OVERWRITE DURING BUSY OPERATION!!!
    // Should it just ignore it or cause an error?"

    // Actions for an unopened socket:

    if (not (sock->flags & RRF_OPEN)) {

        switch (verb) { // Ordered by frequency

        case SYM_REFLECT: {
            INCLUDE_PARAMS_OF_REFLECT;

            UNUSED(ARG(value)); // covered by `port`
            REBSYM property = VAL_WORD_SYM(ARG(property));
            assert(property != SYM_0);

            switch (property) {
            case SYM_OPEN_Q:
                return R_FALSE;

            default:
                break;
            }

            fail (Error_On_Port(RE_NOT_OPEN, port, -12)); }

        case SYM_OPEN: {
            REBVAL *arg = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);
            REBVAL *port_id = Obj_Value(spec, STD_PORT_SPEC_NET_PORT_ID);

            // OPEN needs to know to bind() the socket to a local port before
            // the first sendto() is called, if the user is particular about
            // what the port ID of originating messages is.  So local_port
            // must be set before the OS_DO_DEVICE() call.
            //
            REBVAL *local_id = Obj_Value(spec, STD_PORT_SPEC_NET_LOCAL_ID);
            if (IS_BLANK(local_id))
                DEVREQ_NET(sock)->local_port = 0; // let the system pick
            else if (IS_INTEGER(local_id))
                DEVREQ_NET(sock)->local_port = VAL_INT32(local_id);
            else
                fail ("local-id field of PORT! spec must be BLANK!/INTEGER!");

            REBVAL *o_result = OS_DO_DEVICE(sock, RDC_OPEN);
            assert(o_result != NULL);
            if (rebDid("lib/error?", o_result, END))
                rebFail (o_result, END);
            rebRelease(o_result); // ignore result

            sock->flags |= RRF_OPEN;

            // Lookup host name (an extra TCP device step):
            if (IS_STRING(arg)) {
                REBSIZ offset;
                REBSIZ size;
                REBSER *temp = Temp_UTF8_At_Managed(
                    &offset, &size, arg, VAL_LEN_AT(arg)
                );
                PUSH_GUARD_SERIES(temp);

                sock->common.data = BIN_AT(temp, offset);
                DEVREQ_NET(sock)->remote_port =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 80;

                // Note: sets remote_ip field
                //
                REBVAL *l_result = OS_DO_DEVICE(sock, RDC_LOOKUP);
                DROP_GUARD_SERIES(temp);

                assert(l_result != NULL);
                if (rebDid("lib/error?", l_result, END))
                    rebFail (l_result, END);
                rebRelease(l_result); // ignore result

                goto return_port;
            }
            else if (IS_TUPLE(arg)) { // Host IP specified:
                DEVREQ_NET(sock)->remote_port =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 80;
                memcpy(&(DEVREQ_NET(sock)->remote_ip), VAL_TUPLE(arg), 4);
                break; // fall through to open case SYM_OPEN/CONNECT (?)
            }
            else if (IS_BLANK(arg)) { // No host, must be a LISTEN socket:
                sock->modes |= RST_LISTEN;
                sock->common.sock = 0; // where ACCEPT requests are queued
                DEVREQ_NET(sock)->local_port =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 8000;
                break; // fall through to open case SYM_OPEN/CONNECT (?)
            }
            else
                fail (Error_On_Port(RE_INVALID_SPEC, port, -10));
            break; }

        case SYM_CLOSE:
            Move_Value(D_OUT, CTX_ARCHETYPE(port));
            return R_OUT;

        case SYM_ON_WAKE_UP:  // allowed after a close
            break;

        default:
            fail (Error_On_Port(RE_NOT_OPEN, port, -12));
        }
    }

    // Actions for an open socket:

    switch (verb) { // Ordered by frequency

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // covered by `port`
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH: {
            REBVAL *port_data = CTX_VAR(port, STD_PORT_DATA);
            Init_Integer(
                D_OUT,
                ANY_SERIES(port_data) ? VAL_LEN_HEAD(port_data) : 0
            );
            return R_OUT; }

        case SYM_OPEN_Q:
            //
            // Connect for clients, bind for servers:
            //
            return R_FROM_BOOL (
                (sock->state & (RSM_CONNECT | RSM_BIND)) != 0
            );

        default:
            break;
        }

        break; }

    case SYM_ON_WAKE_UP: {
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
            not (sock->modes & RST_UDP)
            and not (sock->state & RSM_CONNECT)
        ){
            fail (Error_On_Port(RE_NOT_CONNECTED, port, -15));
        }

        // Setup the read buffer (allocate a buffer if needed):
        //
        REBVAL *port_data = CTX_VAR(port, STD_PORT_DATA);
        REBSER *buffer;
        if (not IS_STRING(port_data) and not IS_BINARY(port_data)) {
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

        REBVAL *result = OS_DO_DEVICE(sock, RDC_READ);
        if (result == NULL) {
            //
            // Request pending
        }
        else {
            if (rebDid("lib/error?", result, END))
                rebFail (result, END);

            // a note said "recv CAN happen immediately"
            //
            rebRelease(result); // ignore result
        }

        goto return_port; }

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
            not (sock->modes & RST_UDP)
            and not (sock->state & RSM_CONNECT)
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

        REBSER *temp;
        if (IS_BINARY(data)) {
            temp = NULL;
            sock->common.data = VAL_BIN_AT(data);
            sock->length = len;

            Move_Value(CTX_VAR(port, STD_PORT_DATA), data); // keep it GC safe
        }
        else {
            // !!! R3-Alpha did not lay out the invariants of the port model,
            // or what datatypes it would accept at what levels.  STRING!
            // could be sent here--and it could be wide characters or Latin1
            // without the user having knowledge of which.  Yet it would write
            // the string bytes raw either way, giving effectively random
            // behavior.  Convert to UTF-8...but the port model needs a top
            // to bottom review of what types are accepted where and why.
            //
            REBSIZ offset;
            REBSIZ size;
            temp = Temp_UTF8_At_Managed(
                &offset,
                &size,
                data,
                len
            );
            sock->common.data = BIN_AT(temp, offset);
            sock->length = size;

            PUSH_GUARD_SERIES(temp);
        }

        sock->actual = 0;

        REBVAL *result = OS_DO_DEVICE(sock, RDC_WRITE);

        if (temp != NULL)
            DROP_GUARD_SERIES(temp);

        if (result == NULL) {
            //
            // Write pending !!! old comment said "do we get here?"
        }
        else {
            if (rebDid("lib/error?", result, END))
                rebFail (result, END);

            // Note here said "send CAN happen immediately"
            //
            rebRelease(result); // ignore result
        }

        Init_Blank(CTX_VAR(port, STD_PORT_DATA));
        goto return_port; }

    case SYM_PICK: {
        INCLUDE_PARAMS_OF_PICK;
        UNUSED(PAR(location));

        // FIRST server-port returns new port connection.
        //
        REBCNT len = Get_Num_From_Arg(ARG(picker));
        if (
            len == 1
            and not (sock->modes & RST_UDP)
            and (sock->modes & RST_LISTEN)
            and sock->common.data != NULL
        ){
            Accept_New_Port(SINK(D_OUT), port, DEVREQ_NET(sock));
        }
        else
            fail (Error_Out_Of_Range(ARG(picker)));
        return R_OUT; }

    case SYM_QUERY: {
        //
        // Get specific information - the scheme's info object.
        // Special notation allows just getting part of the info.
        //
        Ret_Query_Net(port, DEVREQ_NET(sock), D_OUT);
        return R_OUT; }

    case SYM_CLOSE: {
        if (sock->flags & RRF_OPEN) {
            REBVAL *result = OS_DO_DEVICE(sock, RDC_CLOSE);
            assert(result != NULL); // should be synchronous
            if (rebDid("lib/error?", result, END))
                rebFail (result, END);
            rebRelease(result); // ignore result

            sock->flags &= ~RRF_OPEN;
        }
        goto return_port; }

    case SYM_OPEN: {
        REBVAL *result = OS_DO_DEVICE(sock, RDC_CONNECT);
        if (result == NULL) {
            //
            // Asynchronous connect, this happens in TCP_Actor
        }
        else {
            if (rebDid("lib/error?", result, END))
                rebFail (result, END);
            else {
                // This can happen with UDP, which is connectionless so it
                // returns DR_DONE.
                //
                // !!! Also can happen if it's already open (it checks for the
                // connected flag).  R3-Alpha could OPEN OPEN a port.  :-/
                //
                rebRelease(result); // ignore result
            }
        }
        goto return_port; }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));

return_port:
    Move_Value(D_OUT, CTX_ARCHETYPE(port));
    return R_OUT;
}


//
//  TCP_Actor: C
//
static REB_R TCP_Actor(REBFRM *frame_, REBCTX *port, REBSYM verb)
{
    return Transport_Actor(frame_, port, verb, TRANSPORT_TCP);
}


//
//  UDP_Actor: C
//
static REB_R UDP_Actor(REBFRM *frame_, REBCTX *port, REBSYM verb)
{
    return Transport_Actor(frame_, port, verb, TRANSPORT_UDP);
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

    REBVAL *result = OS_DO_DEVICE(sock, RDC_MODIFY);
    assert(result != NULL); // should be synchronous
    if (rebDid("lib/error?", result, END))
        rebFail (result, END); // !!! chain as "SET-UDP-MULTICAST failure"?

    rebRelease(result); // ignore result
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

    REBVAL *result = OS_DO_DEVICE(sock, RDC_MODIFY);
    assert(result != NULL);
    if (rebDid("lib/error?", result, END))
        rebFail (result, END); // !!! should chain as "SET-UDP-TTL failure"?

    rebRelease(result); // ignore result
    return R_VOID;
}
