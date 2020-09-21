//
//  File: %mod-network.c
//  Summary: "network port interface"
//  Section: ports
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-net.h"

#undef IS_ERROR

#include "sys-core.h"

#include "reb-net.h"

#include "tmp-mod-network.h"

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

    Init_Tuple_Bytes(
        CTX_VAR(ctx, STD_NET_INFO_LOCAL_IP),
        cast(REBYTE*, &sock->local_ip),
        4
    );
    Init_Integer(
        CTX_VAR(ctx, STD_NET_INFO_LOCAL_PORT),
        sock->local_port
    );

    Init_Tuple_Bytes(
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
    REBREQ *sock = Force_Get_Port_State(port, &Dev_Net);
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
    assert(IS_BINARY(port_data) or IS_BLANK(port_data));

    // sock->timeout = 4000; // where does this go? !!!

    // !!! Comment said "HOW TO PREVENT OVERWRITE DURING BUSY OPERATION!!!
    // Should it just ignore it or cause an error?"

    if (not (req->flags & RRF_OPEN)) {
        //
        // Actions for an unopened socket
        //
        switch (VAL_WORD_SYM(verb)) {
          case SYM_REFLECT: {
            INCLUDE_PARAMS_OF_REFLECT;

            UNUSED(ARG(value));  // covered by `port`
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

                assert(l_result != nullptr);
                if (rebDid("error?", l_result, rebEND))
                    rebJumps("FAIL", l_result, rebEND);
                rebRelease(l_result); // ignore result

                RETURN (port);
            }
            else if (IS_TUPLE(arg)) { // Host IP specified:
                ReqNet(sock)->remote_port =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 80;
                Get_Tuple_Bytes(&(ReqNet(sock)->remote_ip), arg, 4);
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

  open_socket_actions:

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
            //
            // This WAKE-UP apparently does not always mean that the operation
            // has completed (that was previously assumed...)
            //
            if (req->actual == req->length)  // completion trashes
                assert(IS_POINTER_TRASH_DEBUG(req->common.binary));
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

        if (REF(seek))
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

        REBSIZ bufsize;

        if (REF(part)) {
            if (not IS_INTEGER(ARG(part)))
                fail (ARG(part));

            bufsize = req->length = VAL_INT32(ARG(part));
        }
        else {
            // !!! R3-Alpha didn't have a working READ/PART for networking; it
            // would just accrue data as each chunk came in.  The inability
            // to limit the read length meant it was difficult to implement
            // network protocols.  Ren-C has R3-Alpha's behavior if no /PART
            // is specified.
            //
            req->length = UINT32_MAX;  // signal "read as much as you can"
            bufsize = NET_BUF_SIZE;
        }

        // Setup the read buffer (allocate a buffer if needed)
        //
        REBBIN *buffer;
        if (IS_BLANK(port_data)) {
            buffer = Make_Binary(bufsize);
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
            buffer = VAL_BINARY_KNOWN_MUTABLE(port_data);

            // !!! Port code doesn't skip the index, but what if user does?
            //
            assert(VAL_INDEX(port_data) == 0);

            if (SER_AVAIL(buffer) < bufsize)
                Extend_Series(buffer, bufsize - SER_AVAIL(buffer));
        }

        TRASH_POINTER_IF_DEBUG(req->common.data);
        req->common.binary = port_data; // write at tail
        req->actual = 0; // actual for THIS read (not for total)

        REBVAL *result = OS_DO_DEVICE(sock, RDC_READ);
        if (result == nullptr) {
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


        // !!! R3-Alpha did not lay out the invariants of the port model,
        // or what datatypes it would accept at what levels.  TEXT! could be
        // sent here--and it once could be wide characters or Latin1 without
        // the user having knowledge of which.  UTF-8 everywhere has resolved
        // that point (always UTF-8 bytes)...but the port model needs a top
        // to bottom review of what types are accepted where and why.
        //
        REBVAL *data = ARG(data);

        // Setup the write.  We copy the data into the request, so that you
        // can say things like:
        //
        //     data: {abc}
        //     write port data
        //     reverse data
        //     write port data
        //
        // We also want to make sure the /PART is handled correctly, so by
        // delegating to COPY/PART we get that for free.
        //
        TRASH_POINTER_IF_DEBUG(req->common.data);
        req->common.binary = rebValue(
            "as binary! copy/part", data, rebQ(REF(part)),
        rebEND);

        // Because requests can be handled asynchronously, we won't
        // necessarily free the handle before WRITE ends.  Unmanage it.
        //
        rebUnmanage(req->common.binary);

        req->length = VAL_LEN_AT(req->common.binary);
        req->actual = 0;

        REBVAL *result = OS_DO_DEVICE(sock, RDC_WRITE);

        if (result == nullptr) {
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

      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;
        UNUSED(PAR(series));

        if (not (req->modes & RST_LISTEN) or (req->modes & RST_UDP))
            fail ("TAKE is only available on TCP LISTEN ports");

        return rebValueQ(
            "take/part/(", REF(deep), ")/(", REF(last), ")",
                CTX_VAR(ctx, STD_PORT_CONNECTIONS),
                REF(part),
        rebEND); }

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
        if (result == nullptr) {
            //
            // Asynchronous connect, this happens in TCP_Actor
        }
        else {
            if (rebDid("error?", result, rebEND))
                rebJumps("lib/FAIL", result, rebEND);

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
//  export register-network-device: native [
//
//  {Add entity to act as hub for dispatching asynchronous network requests}
//
//  ]
//
REBNATIVE(register_network_device)
{
    NETWORK_INCLUDE_PARAMS_OF_REGISTER_NETWORK_DEVICE;

    OS_Register_Device(&Dev_Net);
    return Init_Void(D_OUT);
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
//  export get-tcp-actor-handle: native [
//
//  {Retrieve handle to the native actor for TCP}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_tcp_actor_handle)
{
    NETWORK_INCLUDE_PARAMS_OF_GET_TCP_ACTOR_HANDLE;

    Make_Port_Actor_Handle(D_OUT, &TCP_Actor);
    return D_OUT;
}


//
//  export get-udp-actor-handle: native [
//
//  {Retrieve handle to the native actor for UDP}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_udp_actor_handle)
{
    NETWORK_INCLUDE_PARAMS_OF_GET_UDP_ACTOR_HANDLE;

    Make_Port_Actor_Handle(D_OUT, &UDP_Actor);
    return D_OUT;
}


//
//  export set-udp-multicast: native [
//
//  {Join (or leave) an IPv4 multicast group}
//
//      return: [<void>]
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
// !!! This was originally the kind of thing that SET-MODES though of using
// RDC_MODIFY for.  But that was never standardized or implemented for
// R3-Alpha (nor was RDC_MODIFY written.  With the networking broken out to
// an extension, it is less of a concern to be including platform-specific
// network calls here (though sockets are abstracted across Windows and POSIX,
// one still doesn't want it in the interpreter core...e.g. when the WASM
// build doesn't use it at all.)
{
    NETWORK_INCLUDE_PARAMS_OF_SET_UDP_MULTICAST;

    REBREQ *sock = Force_Get_Port_State(ARG(port), &Dev_Net);

    struct rebol_devreq *req = Req(sock);
    if (not (req->modes & RST_UDP)) // !!! other checks?
        rebJumps("FAIL {SET-UDP-MULTICAST used on non-UDP port}", rebEND);

    struct ip_mreq mreq;
    Get_Tuple_Bytes(&mreq.imr_multiaddr.s_addr, ARG(group), 4);
    Get_Tuple_Bytes(&mreq.imr_interface.s_addr, ARG(member), 4);

    int result = setsockopt(
        req->requestee.socket,
        IPPROTO_IP,
        REF(drop) ? IP_DROP_MEMBERSHIP : IP_ADD_MEMBERSHIP,
        cast(char*, &mreq),
        sizeof(mreq)
    );

    if (result < 0)
        rebFail_OS (result);

    return nullptr;
}


//
//  export set-udp-ttl: native [
//
//  {Set the TTL of a UDP port}
//
//      return: [<void>]
//      port [port!]
//          {An open UDP port}
//      ttl [integer!]
//          {0 = local machine only, 1 = subnet (default), or up to 255}
//  ]
//
REBNATIVE(set_udp_ttl)
//
// !!! See notes on SET_UDP_MULTICAST
{
    NETWORK_INCLUDE_PARAMS_OF_SET_UDP_TTL;

    REBREQ *sock = Force_Get_Port_State(ARG(port), &Dev_Net);
    struct rebol_devreq *req = Req(sock);

    if (not (req->modes & RST_UDP)) // !!! other checks?
        rebJumps("FAIL {SET-UDP-TTL used on non-UDP port}", rebEND);

    int ttl = VAL_INT32(ARG(ttl));
    int result = setsockopt(
        req->requestee.socket,
        IPPROTO_IP,
        IP_TTL,
        cast(char*, &ttl),
        sizeof(ttl)
    );

    if (result < 0)
        rebFail_OS (result);

    return nullptr;
}
