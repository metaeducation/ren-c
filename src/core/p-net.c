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
static void Query_Net(REBVAL *out, REBVAL *port, struct devreq_net *sock)
{
    REBVAL *info = rebValueQ(
        "copy ensure object! (", port, ")/scheme/info", rebEND
    ); // shallow copy

    REBCTX *ctx = VAL_CONTEXT(info);

    Init_Tuple(
        CTX_VAR(ctx, STD_NET_INFO_LOCAL_IP),
        cast(REBYTE*, &sock->local_ip),
        4
    );
    Init_Integer(
        CTX_VAR(ctx, STD_NET_INFO_LOCAL_PORT),
        sock->local_port
    );

    Init_Tuple(
        CTX_VAR(ctx, STD_NET_INFO_REMOTE_IP),
        cast(REBYTE*, &sock->remote_ip),
        4
    );
    Init_Integer(
        CTX_VAR(ctx, STD_NET_INFO_REMOTE_PORT),
        sock->remote_port
    );

    Move_Value(out, info);
    rebRelease(info);
}


//
//  Transport_Actor: C
//
static REB_R Transport_Actor(
    REBFRM *frame_,
    REBVAL *port,
    const REBVAL *verb,
    enum Transport_Types proto
){
    // Initialize the IO request
    //
    REBREQ *sock = Ensure_Port_State(port, RDI_NET);
    struct rebol_devreq *req = Req(sock);

    if (proto == TRANSPORT_UDP)
        req->modes |= RST_UDP;

    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);

    // If a transfer is in progress, the port_data is a BINARY!.  Its index
    // represents how much of the transfer has finished.  The data starts
    // as blank (from `make-port*`) and R3-Alpha would blank it after a
    // transfer was finished.  For writes, R3-Alpha held a copy of the value
    // being written...and text was allowed (even though it might be wide
    // characters, a likely oversight from the addition of unicode).
    //
    REBVAL *port_data = CTX_VAR(ctx, STD_PORT_DATA);
    assert(IS_BINARY(port_data) or IS_TEXT(port_data) or IS_BLANK(port_data));

    // sock->timeout = 4000; // where does this go? !!!

    // !!! Comment said "HOW TO PREVENT OVERWRITE DURING BUSY OPERATION!!!
    // Should it just ignore it or cause an error?"

    // Actions for an unopened socket:

    if (not (req->flags & RRF_OPEN)) {

        switch (VAL_WORD_SYM(verb)) { // Ordered by frequency

        case SYM_REFLECT: {
            INCLUDE_PARAMS_OF_REFLECT;

            UNUSED(ARG(value)); // covered by `port`
            REBSYM property = VAL_WORD_SYM(ARG(property));
            assert(property != SYM_0);

            switch (property) {
            case SYM_OPEN_Q:
                return Init_False(D_OUT);

            default:
                break;
            }

            fail (Error_On_Port(SYM_NOT_OPEN, port, -12)); }

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
                ReqNet(sock)->local_port = 0; // let the system pick
            else if (IS_INTEGER(local_id))
                ReqNet(sock)->local_port = VAL_INT32(local_id);
            else
                fail ("local-id field of PORT! spec must be BLANK!/INTEGER!");

            OS_DO_DEVICE_SYNC(sock, RDC_OPEN);

            req->flags |= RRF_OPEN;

            // Lookup host name (an extra TCP device step):
            if (IS_TEXT(arg)) {
                //
                // !!! This is storing a direct pointer into the given string
                // data in the socket.  A better system is needed which would
                // either pass the value itself with a temporary hold against
                // mutation, or take ownership of a copy.
                //
                // !!! Should not modify!
                //
                req->common.data = m_cast(REBYTE*, VAL_UTF8_AT(NULL, arg));

                ReqNet(sock)->remote_port =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 80;

                // Note: sets remote_ip field
                //
                REBVAL *l_result = OS_DO_DEVICE(sock, RDC_LOOKUP);

                assert(l_result != NULL);
                if (rebDid("error?", l_result, rebEND))
                    rebJumps("FAIL", l_result, rebEND);
                rebRelease(l_result); // ignore result

                RETURN (port);
            }
            else if (IS_TUPLE(arg)) { // Host IP specified:
                ReqNet(sock)->remote_port =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 80;
                memcpy(&(ReqNet(sock)->remote_ip), VAL_TUPLE(arg), 4);
                goto open_socket_actions;
            }
            else if (IS_BLANK(arg)) { // No host, must be a LISTEN socket:
                req->modes |= RST_LISTEN;
                ReqNet(sock)->local_port =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 8000;

                // When a client connection gets accepted, a port gets added
                // to a BLOCK! of connections.
                //
                Init_Block(
                    CTX_VAR(ctx, STD_PORT_CONNECTIONS),
                    Make_Array(2)
                );
                goto open_socket_actions;
            }
            else
                fail (Error_On_Port(SYM_INVALID_SPEC, port, -10));
            break; }

        case SYM_CLOSE:
            RETURN (port);

        case SYM_ON_WAKE_UP:  // allowed after a close
            break;

        default:
            fail (Error_On_Port(SYM_NOT_OPEN, port, -12));
        }
    }

  open_socket_actions:;

    switch (VAL_WORD_SYM(verb)) { // Ordered by frequency

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // covered by `port`
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH: {
            return Init_Integer(
                D_OUT,
                IS_BINARY(port_data) ? VAL_LEN_HEAD(port_data) : 0
            ); }

        case SYM_OPEN_Q:
            //
            // Connect for clients, bind for servers:
            //
            return Init_Logic(
                D_OUT,
                (req->state & (RSM_CONNECT | RSM_BIND)) != 0
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
        if (req->command == RDC_READ) {
            assert(IS_BINARY(port_data));  // transfer in progress
            assert(req->common.binary == port_data);

            // !!! R3-Alpha would take req->actual and advance the tail of
            // the actual input binary here (the req only had byte access,
            // and could not keep the BINARY! up to date).  Ren-C tries to
            // operate with the binary in a valid state after every change.
            //
            ASSERT_SERIES_TERM(VAL_BINARY(port_data));
        }
        else if (req->command == RDC_WRITE) {
            enum Reb_Kind kind = VAL_TYPE(port_data);
            assert(kind == REB_BINARY or kind == REB_TEXT);
            UNUSED(kind);

            // !!! Still uses the convention of passing a byte pointer to
            // the device layer, vs. a BINARY!.  Pointer is advanced on each
            // section of write.  WROTE event happens only when all the data
            // has been written.
            //
            REBSIZ size;
            assert(
                req->common.data ==
                    VAL_BYTES_AT(&size, port_data) + req->length
            );
            UNUSED(size);

            // !!! R3-Alpha said "write is done" here, and threw away the
            // port data by blanking it.  But was it done?
            //
            Init_Blank(port_data);
        }
        else
            assert(
                req->command == RDC_LOOKUP
                or req->command == RDC_CONNECT
                or req->command == RDC_CREATE
                or req->command == RDC_CLOSE
            );

        return Init_Void(D_OUT); }

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));

        if (REF(part) or REF(seek))
            fail (Error_Bad_Refines_Raw());

        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        // Read data into a buffer, expanding the buffer if needed.
        // If no length is given, program must stop it at some point.
        if (
            not (req->modes & RST_UDP)
            and not (req->state & RSM_CONNECT)
        ){
            fail (Error_On_Port(SYM_NOT_CONNECTED, port, -15));
        }

        // Setup the read buffer (allocate a buffer if needed)
        //
        REBBIN *buffer;
        if (IS_BLANK(port_data)) {
            buffer = Make_Binary(NET_BUF_SIZE);
            Init_Binary(port_data, buffer);
        }
        else {
            // In R3-Alpha, the client could leave data in the buffer of the
            // port and just accumulate it, as in SYNC-OP from %prot-http.r:
            //
            //     while [not find [ready close] state/state] [
            //         if not port? wait [state/connection port/spec/timeout] [
            //             fail make-http-error "Timeout"
            //         ]
            //         if state/state = 'reading-data [
            //             read state/connection
            //         ]
            //     ]
            //
            buffer = VAL_BINARY(port_data);

            // !!! Port code doesn't skip the index, but what if user does?
            //
            assert(VAL_INDEX(port_data) == 0);

            if (SER_AVAIL(buffer) < NET_BUF_SIZE / 2)
                Extend_Series(buffer, NET_BUF_SIZE);
        }

        req->length = SER_AVAIL(buffer);
        TRASH_POINTER_IF_DEBUG(req->common.data);
        req->common.binary = port_data; // write at tail
        req->actual = 0; // actual for THIS read (not for total)

        REBVAL *result = OS_DO_DEVICE(sock, RDC_READ);
        if (result == NULL) {
            //
            // Request pending
        }
        else {
            if (rebDid("error?", result, rebEND))
                rebJumps("FAIL", result, rebEND);

            // a note said "recv CAN happen immediately"
            //
            rebRelease(result); // ignore result
        }

        RETURN (port); }

    case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PAR(destination));

        if (REF(seek) or REF(append) or REF(allow) or REF(lines))
            fail (Error_Bad_Refines_Raw());

        // Write the entire argument string to the network.
        // The lower level write code continues until done.

        if (
            not (req->modes & RST_UDP)
            and not (req->state & RSM_CONNECT)
        ){
            fail (Error_On_Port(SYM_NOT_CONNECTED, port, -15));
        }

        // Determine length. Clip /PART to size of string if needed.
        REBVAL *data = ARG(data);

        REBCNT len = VAL_LEN_AT(data);
        if (REF(part)) {
            REBCNT n = Int32s(ARG(part), 0);
            if (n <= len)
                len = n;
        }

        // Setup the write:

        // !!! R3-Alpha did not lay out the invariants of the port model,
        // or what datatypes it would accept at what levels.  TEXT! could be
        // sent here--and it once could be wide characters or Latin1 without
        // the user having knowledge of which.  UTF-8 everywhere has resolved
        // that point (always UTF-8 bytes)...but the port model needs a top
        // to bottom review of what types are accepted where and why.
        //
        // !!! Uses m_cast, but should not modify!

        assert(IS_BINARY(data) or IS_TEXT(data));
        Move_Value(port_data, data);  // GC-safety (blanked out on UPDATE)

        REBSIZ size;
        req->common.data = m_cast(REBYTE*, VAL_BYTES_AT(&size, data));
        assert(len == size);
        UNUSED(size);
        req->length = len;

        req->actual = 0;

        REBVAL *result = OS_DO_DEVICE(sock, RDC_WRITE);

        if (result == NULL) {
            //
            // Write pending !!! old comment said "do we get here?"
        }
        else {
            if (rebDid("error?", result, rebEND))
                rebJumps("FAIL", result, rebEND);

            // Note here said "send CAN happen immediately"
            //
            rebRelease(result); // ignore result
        }

        RETURN (port); }

    case SYM_TAKE_P: {
        INCLUDE_PARAMS_OF_TAKE_P;
        UNUSED(PAR(series));

        if (not (req->modes & RST_LISTEN) or (req->modes & RST_UDP))
            fail ("TAKE is only available on TCP LISTEN ports");

        return rebValueQ(
            "take*/part/(", ARG(deep), ")/(", ARG(last), ")",
                CTX_VAR(ctx, STD_PORT_CONNECTIONS),
                ARG(part),
                rebEND
        ); }

    case SYM_PICK: {
        fail (
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
        Query_Net(D_OUT, port, ReqNet(sock));
        return D_OUT; }

    case SYM_CLOSE: {
        if (req->flags & RRF_OPEN) {
            OS_DO_DEVICE_SYNC(sock, RDC_CLOSE);

            req->flags &= ~RRF_OPEN;
        }
        RETURN (port); }

    case SYM_OPEN: {
        REBVAL *result = OS_DO_DEVICE(sock, RDC_CONNECT);
        if (result == NULL) {
            //
            // Asynchronous connect, this happens in TCP_Actor
        }
        else {
            if (rebDid("error?", result, rebEND))
                rebJumps("libFAIL", result, rebEND);

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

    return R_UNHANDLED;
}


//
//  TCP_Actor: C
//
static REB_R TCP_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb)
{
    return Transport_Actor(frame_, port, verb, TRANSPORT_TCP);
}


//
//  UDP_Actor: C
//
static REB_R UDP_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb)
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
    return D_OUT;
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
    return D_OUT;
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

    REBREQ *sock = Ensure_Port_State(ARG(port), RDI_NET);

    struct rebol_devreq *req = Req(sock);

    req->common.data = cast(REBYTE*, frame_);

    // req->command is going to just be RDC_MODIFY, so all there is to go
    // by is the data and flags.  Since RFC3171 specifies IPv4 multicast
    // address space...how about that?
    //
    req->flags = 3171;

    UNUSED(ARG(group));
    UNUSED(ARG(member));
    UNUSED(REF(drop));

    OS_DO_DEVICE_SYNC(sock, RDC_MODIFY);
    return nullptr;
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

    REBREQ *sock = Ensure_Port_State(ARG(port), RDI_NET);
    struct rebol_devreq *req = Req(sock);

    req->common.data = cast(REBYTE*, frame_);

    // req->command is going to just be RDC_MODIFY, so all there is to go
    // by is the data and flags.  Since RFC2365 specifies IPv4 multicast
    // administrative boundaries...how about that?
    //
    req->flags = 2365;

    UNUSED(ARG(ttl));

    OS_DO_DEVICE_SYNC(sock, RDC_MODIFY);
    return nullptr;
}
