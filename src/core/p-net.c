//
//  file: %p-net.c
//  summary: "network port interface"
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

#include "reb-net.h"

#define NET_BUF_SIZE 32*1024

enum Transport_Types {
    TRANSPORT_TCP,
    TRANSPORT_UDP
};

//
//  Query_Net: C
//
static void Query_Net(Value* out, Value* port, struct devreq_net *sock)
{
    Value* info = rebValue(
        "copy ensure object! (", port , ")/scheme/info"
    ); // shallow copy

    VarList* ctx = Cell_Varlist(info);

    Set_Tuple(
        Varlist_Slot(ctx, STD_NET_INFO_LOCAL_IP),
        cast(Byte*, &sock->local_ip),
        4
    );
    Init_Integer(
        Varlist_Slot(ctx, STD_NET_INFO_LOCAL_PORT),
        sock->local_port
    );

    Set_Tuple(
        Varlist_Slot(ctx, STD_NET_INFO_REMOTE_IP),
        cast(Byte*, &sock->remote_ip),
        4
    );
    Init_Integer(
        Varlist_Slot(ctx, STD_NET_INFO_REMOTE_PORT),
        sock->remote_port
    );

    Copy_Cell(out, info);
    rebRelease(info);
}


//
//  Transport_Actor: C
//
static Bounce Transport_Actor(
    Level* level_,
    Value* port,
    Value* verb,
    enum Transport_Types proto
){
    // Initialize the IO request
    //
    REBREQ *sock = Ensure_Port_State(port, RDI_NET);
    if (proto == TRANSPORT_UDP)
        sock->modes |= RST_UDP;

    VarList* ctx = Cell_Varlist(port);
    Value* spec = Varlist_Slot(ctx, STD_PORT_SPEC);

    // sock->timeout = 4000; // where does this go? !!!

    // !!! Comment said "HOW TO PREVENT OVERWRITE DURING BUSY OPERATION!!!
    // Should it just ignore it or cause an error?"

    // Actions for an unopened socket:

    if (not (sock->flags & RRF_OPEN)) {

        switch (Cell_Word_Id(verb)) { // Ordered by frequency

        case SYM_REFLECT: {
            INCLUDE_PARAMS_OF_REFLECT;

            UNUSED(ARG(VALUE)); // covered by `port`
            Option(SymId) property = Cell_Word_Id(ARG(PROPERTY));
            assert(property != SYM_0);

            switch (property) {
            case SYM_OPEN_Q:
                return LOGIC(false);

            default:
                break;
            }

            panic (Error_On_Port(SYM_NOT_OPEN, port, -12)); }

        case SYM_OPEN: {
            Value* arg = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);
            Value* port_id = Obj_Value(spec, STD_PORT_SPEC_NET_PORT_ID);

            // OPEN needs to know to bind() the socket to a local port before
            // the first sendto() is called, if the user is particular about
            // what the port ID of originating messages is.  So local_port
            // must be set before the OS_DO_DEVICE() call.
            //
            Value* local_id = Obj_Value(spec, STD_PORT_SPEC_NET_LOCAL_ID);
            if (Is_Nulled(local_id))
                DEVREQ_NET(sock)->local_port = 0; // let the system pick
            else if (Is_Integer(local_id))
                DEVREQ_NET(sock)->local_port = VAL_INT32(local_id);
            else
                panic ("local-id field of PORT! spec must be BLANK!/INTEGER!");

            OS_DO_DEVICE_SYNC(sock, RDC_OPEN);

            sock->flags |= RRF_OPEN;

            // Lookup host name (an extra TCP device step):
            if (Is_Text(arg)) {
                Size offset;
                Size size;
                Binary* temp = Temp_UTF8_At_Managed(
                    &offset, &size, arg, Series_Len_At(arg)
                );
                Push_GC_Guard(temp);

                sock->common.data = Binary_At(temp, offset);
                DEVREQ_NET(sock)->remote_port =
                    Is_Integer(port_id) ? VAL_INT32(port_id) : 80;

                // Note: sets remote_ip field
                //
                Value* l_result = OS_DO_DEVICE(sock, RDC_LOOKUP);
                Drop_GC_Guard(temp);

                assert(l_result != nullptr);
                if (rebDid("error?", rebQ(l_result)))
                    rebJumps("panic", l_result);
                rebRelease(l_result); // ignore result

                RETURN (port);
            }
            else if (Is_Tuple(arg)) { // Host IP specified:
                DEVREQ_NET(sock)->remote_port =
                    Is_Integer(port_id) ? VAL_INT32(port_id) : 80;
                memcpy(&(DEVREQ_NET(sock)->remote_ip), VAL_TUPLE(arg), 4);
                goto open_socket_actions;
            }
            else if (Is_Blank(arg)) { // No host, must be a LISTEN socket:
                sock->modes |= RST_LISTEN;
                DEVREQ_NET(sock)->local_port =
                    Is_Integer(port_id) ? VAL_INT32(port_id) : 8000;

                // When a client connection gets accepted, a port gets added
                // to a BLOCK! of connections.
                //
                Init_Block(
                    Varlist_Slot(ctx, STD_PORT_CONNECTIONS),
                    Make_Array(2)
                );
                goto open_socket_actions;
            }
            else
                panic (Error_On_Port(SYM_INVALID_SPEC, port, -10));
            break; }

        case SYM_CLOSE:
            RETURN (port);

        case SYM_ON_WAKE_UP:  // allowed after a close
            break;

        default:
            panic (Error_On_Port(SYM_NOT_OPEN, port, -12));
        }
    }

  open_socket_actions:;

    switch (Cell_Word_Id(verb)) { // Ordered by frequency

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(VALUE)); // covered by `port`
        Option(SymId) property = Cell_Word_Id(ARG(PROPERTY));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH: {
            Value* port_data = Varlist_Slot(ctx, STD_PORT_DATA);
            return Init_Integer(
                OUT,
                Any_Series(port_data) ? VAL_LEN_HEAD(port_data) : 0
            ); }

        case SYM_OPEN_Q:
            //
            // Connect for clients, bind for servers:
            //
            return Init_Logic(
                OUT,
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
        Value* port_data = Varlist_Slot(ctx, STD_PORT_DATA);
        if (sock->command == RDC_READ) {
            if (Is_Binary(port_data) or Any_String(port_data)) {
                Set_Flex_Len(
                    Cell_Flex(port_data),
                    VAL_LEN_HEAD(port_data) + sock->actual
                );
            }
        }
        else if (sock->command == RDC_WRITE) {
            Init_Nulled(port_data); // Write is done.
        }
        return Init_Trash(OUT); }

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

        // Read data into a buffer, expanding the buffer if needed.
        // If no length is given, program must stop it at some point.
        if (
            not (sock->modes & RST_UDP)
            and not (sock->state & RSM_CONNECT)
        ){
            panic (Error_On_Port(SYM_NOT_CONNECTED, port, -15));
        }

        // Setup the read buffer (allocate a buffer if needed):
        //
        Value* port_data = Varlist_Slot(ctx, STD_PORT_DATA);
        Binary* buffer;
        if (not Is_Text(port_data) and not Is_Binary(port_data)) {
            buffer = Make_Binary(NET_BUF_SIZE);
            Init_Blob(port_data, buffer);
        }
        else {
            buffer = Cell_Binary(port_data);

            if (Flex_Available_Space(buffer) < NET_BUF_SIZE/2)
                Extend_Flex(buffer, NET_BUF_SIZE);
        }

        sock->length = Flex_Available_Space(buffer);
        sock->common.data = Binary_Tail(buffer); // write at tail
        sock->actual = 0; // actual for THIS read (not for total)

        Value* result = OS_DO_DEVICE(sock, RDC_READ);
        if (result == nullptr) {
            //
            // Request pending
        }
        else {
            if (rebDid("error?", rebQ(result)))
                rebJumps("panic", result);

            // a note said "recv CAN happen immediately"
            //
            rebRelease(result); // ignore result
        }

        // !!! Post-processing enforces READ as returning OUT at the moment;
        // so you can't just `return port`.
        //
        Copy_Cell(OUT, port);
        return OUT; }

    case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PARAM(DESTINATION));

        if (Bool_ARG(SEEK)) {
            UNUSED(ARG(INDEX));
            panic (Error_Bad_Refines_Raw());
        }
        if (Bool_ARG(APPEND))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(ALLOW)) {
            UNUSED(ARG(ACCESS));
            panic (Error_Bad_Refines_Raw());
        }
        if (Bool_ARG(LINES))
            panic (Error_Bad_Refines_Raw());

        // Write the entire argument string to the network.
        // The lower level write code continues until done.

        if (
            not (sock->modes & RST_UDP)
            and not (sock->state & RSM_CONNECT)
        ){
            panic (Error_On_Port(SYM_NOT_CONNECTED, port, -15));
        }

        // Determine length. Clip /PART to size of string if needed.
        Value* data = ARG(DATA);

        REBLEN len = Series_Len_At(data);
        if (Bool_ARG(PART)) {
            REBLEN n = Int32s(ARG(LIMIT), 0);
            if (n <= len)
                len = n;
        }

        // Setup the write:

        Binary* temp;
        if (Is_Binary(data)) {
            temp = nullptr;
            sock->common.data = Blob_At(data);
            sock->length = len;

            Copy_Cell(Varlist_Slot(ctx, STD_PORT_DATA), data); // keep it GC safe
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
            Size offset;
            Size size;
            temp = Temp_UTF8_At_Managed(
                &offset,
                &size,
                data,
                len
            );
            sock->common.data = Binary_At(temp, offset);
            sock->length = size;

            Push_GC_Guard(temp);
        }

        sock->actual = 0;

        Value* result = OS_DO_DEVICE(sock, RDC_WRITE);

        if (temp != nullptr)
            Drop_GC_Guard(temp);

        if (result == nullptr) {
            //
            // Write pending !!! old comment said "do we get here?"
        }
        else {
            if (rebDid("error?", rebQ(result)))
                rebJumps("panic", result);

            // Note here said "send CAN happen immediately"
            //
            rebRelease(result); // ignore result
        }

        Init_Blank(Varlist_Slot(ctx, STD_PORT_DATA));
        RETURN (port); }

    case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;
        UNUSED(PARAM(SERIES));

        if (not (sock->modes & RST_LISTEN) or (sock->modes & RST_UDP))
            panic ("TAKE is only available on TCP LISTEN ports");

        UNUSED(Bool_ARG(PART)); // non-null limit accounts for

        return rebValue(
            "take/part/(", ARG(DEEP), ")/(", ARG(LAST), ")",
                Varlist_Slot(ctx, STD_PORT_CONNECTIONS),
                ARG(LIMIT)
        ); }

    case SYM_PICK: {
        panic (
            "Listening network PORT!s no longer support FIRST (or PICK) to"
            " extract the connection PORT! in an accept event.  It was"
            " actually TAKE-ing the port, since it couldn't be done again."
            " Use TAKE for now--PICK may be brought back eventually as a"
            " read-only way of looking at the accept list."
        ); }

    case SYM_QUERY: {
        //
        // Get specific information - the scheme's info object.
        // Special notation allows just getting part of the info.
        //
        Query_Net(OUT, port, DEVREQ_NET(sock));
        return OUT; }

    case SYM_CLOSE: {
        if (sock->flags & RRF_OPEN) {
            OS_DO_DEVICE_SYNC(sock, RDC_CLOSE);

            sock->flags &= ~RRF_OPEN;
        }
        RETURN (port); }

    case SYM_OPEN: {
        Value* result = OS_DO_DEVICE(sock, RDC_CONNECT);
        if (result == nullptr) {
            //
            // Asynchronous connect, this happens in TCP_Actor
        }
        else {
            if (rebDid("error?", rebQ(result)))
                rebJumps("fail", result);

            // This can happen with UDP, which is connectionless so it
            // returns DR_DONE.
            //
            // !!! Also can happen if it's already open (it checks for the
            // connected flag).  R3-Alpha could OPEN OPEN a port.  :-/
            //
            rebRelease(result); // ignore result
        }
        RETURN (port); }

    default:
        break;
    }

    panic (Error_Illegal_Action(TYPE_PORT, verb));
}


//
//  TCP_Actor: C
//
static Bounce TCP_Actor(Level* level_, Value* port, Value* verb)
{
    return Transport_Actor(level_, port, verb, TRANSPORT_TCP);
}


//
//  UDP_Actor: C
//
static Bounce UDP_Actor(Level* level_, Value* port, Value* verb)
{
    return Transport_Actor(level_, port, verb, TRANSPORT_UDP);
}


//
//  get-tcp-actor-handle: native [
//
//  {Retrieve handle to the native actor for TCP}
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(GET_TCP_ACTOR_HANDLE)
{
    Make_Port_Actor_Handle(OUT, &TCP_Actor);
    return OUT;
}


//
//  get-udp-actor-handle: native [
//
//  {Retrieve handle to the native actor for UDP}
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(GET_UDP_ACTOR_HANDLE)
{
    Make_Port_Actor_Handle(OUT, &UDP_Actor);
    return OUT;
}


//
//  set-udp-multicast: native [
//
//  {Join (or leave) an IPv4 multicast group}
//
//      return: [~null~]
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
DECLARE_NATIVE(SET_UDP_MULTICAST)
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

    REBREQ *sock = Ensure_Port_State(ARG(PORT), RDI_NET);

    sock->common.data = cast(Byte*, level_);

    // sock->command is going to just be RDC_MODIFY, so all there is to go
    // by is the data and flags.  Since RFC3171 specifies IPv4 multicast
    // address space...how about that?
    //
    sock->flags = 3171;

    UNUSED(ARG(GROUP));
    UNUSED(ARG(MEMBER));
    UNUSED(Bool_ARG(DROP));

    OS_DO_DEVICE_SYNC(sock, RDC_MODIFY);
    return nullptr;
}


//
//  set-udp-ttl: native [
//
//  {Set the TTL of a UDP port}
//
//      return: [~null~]
//      port [port!]
//          {An open UDP port}
//      ttl [integer!]
//          {0 = local machine only, 1 = subnet (default), or up to 255}
//  ]
//
DECLARE_NATIVE(SET_UDP_TTL)
{
    INCLUDE_PARAMS_OF_SET_UDP_TTL;

    REBREQ *sock = Ensure_Port_State(ARG(PORT), RDI_NET);

    sock->common.data = cast(Byte*, level_);

    // sock->command is going to just be RDC_MODIFY, so all there is to go
    // by is the data and flags.  Since RFC2365 specifies IPv4 multicast
    // administrative boundaries...how about that?
    //
    sock->flags = 2365;

    UNUSED(ARG(TTL));

    OS_DO_DEVICE_SYNC(sock, RDC_MODIFY);
    return nullptr;
}
