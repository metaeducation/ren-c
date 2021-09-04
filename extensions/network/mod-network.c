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


struct Reb_Sock_Transfer *Net_Transfers;
struct Reb_Sock_Listener *Net_Listeners;
struct Reb_Sock_Connector *Net_Connectors;

extern REBVAL *Open_Socket(const REBVAL *port);
extern REBVAL *Lookup_Socket(const REBVAL *port, const REBVAL *hostname);
extern REBVAL *Close_Socket(const REBVAL *port);

extern bool Transfer_Socket_Finishing(struct Reb_Sock_Transfer *transfer);
extern bool Accept_Socket_Finishing(struct Reb_Sock_Listener *listener);
extern REBVAL *Connect_Socket_Maybe_Queued(const REBVAL *port);


extern void Startup_Networking(void);
extern void Shutdown_Networking(void);

REBDEV *Dev_Net;


#define NET_BUF_SIZE 32*1024

//
//  Query_Net: C
//
static void Query_Net(REBVAL *out, REBVAL *port, SOCKREQ *sock)
{
    REBVAL *info = rebValue(
        "copy ensure object! (@", port, ")/scheme/info"
    );  // shallow copy

    REBCTX *ctx = VAL_CONTEXT(info);

    Init_Tuple_Bytes(
        CTX_VAR(ctx, STD_NET_INFO_LOCAL_IP),
        cast(REBYTE*, &sock->local_ip),
        4
    );
    Init_Integer(
        CTX_VAR(ctx, STD_NET_INFO_LOCAL_PORT),
        sock->local_port_number
    );

    Init_Tuple_Bytes(
        CTX_VAR(ctx, STD_NET_INFO_REMOTE_IP),
        cast(REBYTE*, &sock->remote_ip),
        4
    );
    Init_Integer(
        CTX_VAR(ctx, STD_NET_INFO_REMOTE_PORT),
        sock->remote_port_number
    );

    Copy_Cell(out, info);
    rebRelease(info);
}


//
//  Transport_Actor: C
//
static REB_R Transport_Actor(
    REBFRM *frame_,
    REBVAL *port,
    const REBSYM *verb,
    enum Transport_Type transport
){
    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);

    // If a transfer is in progress, the port_data is a BINARY!.  Its index
    // represents how much of the transfer has finished.  The data starts
    // as NULL (from `make-port*`) and R3-Alpha would reset it after a
    // transfer was finished.  For writes, R3-Alpha held a copy of the value
    // being written...and text was allowed (even though it might be wide
    // characters, a likely oversight from the addition of unicode).
    //
    REBVAL *port_data = CTX_VAR(ctx, STD_PORT_DATA);
    assert(IS_BINARY(port_data) or IS_NULLED(port_data));

    SOCKREQ *sock;
    REBVAL *state = CTX_VAR(ctx, STD_PORT_STATE);
    if (IS_BINARY(state)) {
        sock = Sock_Of_Port(port);
        assert(sock->transport == transport);
    }
    else {
        // !!! The Make_Devreq() code would zero out the struct, so to keep
        // things compatible while ripping out the devreq code this must too.
        //
        assert(IS_NULLED(state));
        REBBIN *bin = Make_Binary(sizeof(SOCKREQ));
        Init_Binary(state, bin);
        memset(BIN_HEAD(bin), 0, sizeof(SOCKREQ));
        TERM_BIN_LEN(bin, sizeof(SOCKREQ));

        sock = Sock_Of_Port(port);
        sock->transport = transport;
        sock->fd = SOCKET_NONE;
        sock->socket = SOCKET_NONE;

        // !!! There is no way to customize the timeout.  Where should this
        // setting be configured?
    }

    if (sock->fd == SOCKET_NONE) {
        //
        // Actions for an unopened socket
        //
        switch (ID_OF_SYMBOL(verb)) {
          case SYM_REFLECT: {
            INCLUDE_PARAMS_OF_REFLECT;

            UNUSED(ARG(value));  // covered by `port`
            SYMID property = VAL_WORD_ID(ARG(property));
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
            // must be set before the OS_Do_Device() call.
            //
            REBVAL *local_id = Obj_Value(spec, STD_PORT_SPEC_NET_LOCAL_ID);
            if (IS_BLANK(local_id))
                sock->local_port_number = 0;  // let the system pick
            else if (IS_INTEGER(local_id))
                sock->local_port_number = VAL_INT32(local_id);
            else
                fail ("local-id field of PORT! spec must be BLANK!/INTEGER!");

            REBVAL *error = Open_Socket(port);
            if (error)
                fail (error);

            // Lookup host name (an extra TCP device step)
            //
            if (IS_TEXT(arg)) {
                sock->remote_port_number =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 80;

                // Note: sets remote_ip field
                //
                REBVAL *lookup_error = Lookup_Socket(port, arg);
                if (lookup_error)
                    fail (lookup_error);
            }
            else if (IS_TUPLE(arg)) {  // Host IP specified:
                sock->remote_port_number =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 80;

                Get_Tuple_Bytes(&sock->remote_ip, arg, 4);
            }
            else if (IS_BLANK(arg)) {  // No host, must be a LISTEN socket:
                sock->modes |= RST_LISTEN;
                sock->local_port_number =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 8000;

                // When a client connection gets accepted, a port gets added
                // to a BLOCK! of connections.
                //
                Init_Block(
                    CTX_VAR(ctx, STD_PORT_CONNECTIONS),
                    Make_Array(2)
                );
            }
            else
                fail (Error_On_Port(SYM_INVALID_SPEC, port, -10));

            REBVAL *connect_error = Connect_Socket_Maybe_Queued(port);
            if (connect_error != nullptr)
                fail (connect_error);

            RETURN (port); }

          case SYM_CLOSE:
            RETURN (port);

          default:
            fail (Error_On_Port(SYM_NOT_OPEN, port, -12));
        }
    }

    switch (ID_OF_SYMBOL(verb)) { // Ordered by frequency
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // covered by `port`
        SYMID property = VAL_WORD_ID(ARG(property));
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
                (sock->modes & RSM_BIND) or (sock->socket != SOCKET_NONE)
            );

          default:
            break;
        }

        break; }

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        // Make sure no existing transfer is already READ-ing this port.
        //
        // !!! This limitation is an artifact carried over from R3-Alpha; a
        // better callback-based design should replace this.
        //
      blockscope {
        struct Reb_Sock_Transfer *temp = Net_Transfers;
        for (; temp != nullptr; temp = temp->next) {
            if (temp->direction == TRANSFER_RECEIVE) {
                if (temp->port_ctx == VAL_CONTEXT(port))
                    fail ("READ on PORT! with a READ already in flight");
            }
        }
      }

        UNUSED(PAR(source));

        if (REF(seek))
            fail (Error_Bad_Refines_Raw());

        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        // Read data into a buffer, expanding the buffer if needed.
        // If no length is given, program must stop it at some point.
        //
        if (sock->socket == SOCKET_NONE and sock->transport != TRANSPORT_UDP)
            fail (Error_On_Port(SYM_NOT_CONNECTED, port, -15));

        struct Reb_Sock_Transfer *transfer = TRY_ALLOC(struct Reb_Sock_Transfer);
        transfer->port_ctx = VAL_CONTEXT(port);
        transfer->direction = TRANSFER_RECEIVE;
        transfer->actual = 0;  // actual for THIS read (not for total)

        REBSIZ bufsize;

        if (REF(part)) {
            if (not IS_INTEGER(ARG(part)))
                fail (ARG(part));

            bufsize = transfer->length = VAL_INT32(ARG(part));
        }
        else {
            // !!! R3-Alpha didn't have a working READ/PART for networking; it
            // would just accrue data as each chunk came in.  The inability
            // to limit the read length meant it was difficult to implement
            // network protocols.  Ren-C has R3-Alpha's behavior if no /PART
            // is specified.
            //
            transfer->length = UINT32_MAX;  // signal "read as much as you can"
            bufsize = NET_BUF_SIZE;
        }

        // Setup the read buffer (allocate a buffer if needed)
        //
        REBBIN *buffer;
        if (IS_NULLED(port_data)) {
            buffer = Make_Binary(bufsize);
            Init_Binary(port_data, buffer);
            TERM_BIN_LEN(buffer, 0);
        }
        else {
            // In R3-Alpha, the client could leave data in the buffer of the
            // port and just accumulate it, as in SYNC-OP from %prot-http.r:
            //
            //     loop [not find [ready close] state/state] [
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

            if (SER_AVAIL(buffer) < bufsize) {
                Extend_Series(buffer, bufsize - SER_AVAIL(buffer));
                TERM_BIN(buffer);
            }
        }

        // We always queue the transfer.
        //
        transfer->next = Net_Transfers;
        Net_Transfers = transfer;
        Init_True(CTX_VAR(VAL_CONTEXT(port), STD_PORT_PENDING));

        RETURN (port); }

      case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PAR(destination));

        if (REF(seek) or REF(append) or REF(lines))
            fail (Error_Bad_Refines_Raw());

        if (sock->socket == SOCKET_NONE and sock->transport != TRANSPORT_UDP)
            fail (Error_On_Port(SYM_NOT_CONNECTED, port, -15));

        // !!! R3-Alpha did not lay out the invariants of the port model,
        // or what datatypes it would accept at what levels.  TEXT! could be
        // sent here--and it once could be wide characters or Latin1 without
        // the user having knowledge of which.  UTF-8 everywhere has resolved
        // that point (always UTF-8 bytes)...but the port model needs a top
        // to bottom review of what types are accepted where and why.
        //
        REBVAL *data = ARG(data);

        // If there is a transfer in flight already, we want to find it and
        // add more data to it.
        //
        struct Reb_Sock_Transfer *transfer = Net_Transfers;
        for (; transfer != nullptr; transfer = transfer->next) {
            if (transfer->direction != TRANSFER_SEND)
                continue;
            if (transfer->port_ctx != VAL_CONTEXT(port))
                continue;

            break;
        }

        if (not transfer) {  // there was no in-flight SEND, make new one
            transfer = TRY_ALLOC(struct Reb_Sock_Transfer);
            transfer->port_ctx = VAL_CONTEXT(port);
            transfer->direction = TRANSFER_SEND;

            // Copy the data into the request, so that you can say things like:
            //
            //     data: {abc}
            //     write port data
            //     reverse data
            //     write port data
            //
            // We also want to make sure the /PART is handled correctly, so by
            // delegating to COPY/PART we get that for free.
            //
            // !!! If you FREEZE the data then a copy is not necessary, review
            // this as an angle on efficiency.
            //
            transfer->binary = rebValue(
                "as binary! copy/part", data, rebQ(REF(part))
            );
            transfer->length = VAL_LEN_AT(transfer->binary);
            transfer->actual = 0;  // actual for THIS read (not for total)

            // Because requests can be handled asynchronously, we won't
            // necessarily free the handle before WRITE ends.  Unmanage it.
            //
            rebUnmanage(transfer->binary);

            // We always queue the transfer.
            //
            transfer->next = Net_Transfers;
            Net_Transfers = transfer;
            Init_True(CTX_VAR(VAL_CONTEXT(port), STD_PORT_PENDING));
        }
        else {
            // We just append the data to the binary that's already there.
            //
            // !!! This could be done better with a block of binary fragments,
            // where each fragment could have been FREEZE'd by the user so that
            // the transfer could take ownership of it and avoid copying.  But
            // as an initial version to break free of the REBREQ this just
            // does an append.
            //
            rebElide(
                "append/part", transfer->binary, data, rebQ(REF(part))
            );

            // The length is bumped by the length of the binary.
            //
            transfer->length = VAL_LEN_AT(transfer->binary);

            // The ->actual for how much has been transferred is left as is.
        }

        RETURN (port); }

      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;
        UNUSED(PAR(series));

        if (not (sock->modes & RST_LISTEN) or sock->transport == TRANSPORT_UDP)
            fail ("TAKE is only available on TCP LISTEN ports");

        return rebValue(
            "take/part/(@", REF(deep), ")/(@", REF(last), ")",
                CTX_VAR(ctx, STD_PORT_CONNECTIONS),
                rebQ(REF(part))
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
        Query_Net(D_OUT, port, sock);
        return D_OUT; }

      case SYM_CLOSE: {
        if (sock->fd != SOCKET_NONE) {  // allows close of closed socket (?)
            REBVAL *error = Close_Socket(port);
            if (error)
                fail (error);
        }
        RETURN (port); }

      case SYM_CONNECT: {
        //
        // CONNECT may happen synchronously, or asynchronously...so this may
        // add to Net_Connectors.
        //
        // UDP is connectionless so it will not add to the connectors.
        //
        REBVAL *error = Connect_Socket_Maybe_Queued(port);
        if (error != nullptr)
            fail (error);

        RETURN (port); }

      default:
        break;
    }

    return R_UNHANDLED;
}


//
//  TCP_Actor: C
//
static REB_R TCP_Actor(REBFRM *frame_, REBVAL *port, const REBSYM *verb)
{
    return Transport_Actor(frame_, port, verb, TRANSPORT_TCP);
}


//
//  UDP_Actor: C
//
static REB_R UDP_Actor(REBFRM *frame_, REBVAL *port, const REBSYM *verb)
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
//      return: <none>
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

    SOCKREQ *sock = Sock_Of_Port(ARG(port));

    if (not (sock->transport == TRANSPORT_UDP))  // !!! other checks?
        rebJumps("fail {SET-UDP-MULTICAST used on non-UDP port}");

    struct ip_mreq mreq;
    Get_Tuple_Bytes(&mreq.imr_multiaddr.s_addr, ARG(group), 4);
    Get_Tuple_Bytes(&mreq.imr_interface.s_addr, ARG(member), 4);

    int result = setsockopt(
        sock->fd,
        IPPROTO_IP,
        REF(drop) ? IP_DROP_MEMBERSHIP : IP_ADD_MEMBERSHIP,
        cast(char*, &mreq),
        sizeof(mreq)
    );

    if (result < 0)
        rebFail_OS (result);

    return rebNone();
}


//
//  export set-udp-ttl: native [
//
//  {Set the TTL of a UDP port}
//
//      return: <none>
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

    SOCKREQ *sock = Sock_Of_Port(ARG(port));

    if (not (sock->transport == TRANSPORT_UDP))  // !!! other checks?
        rebJumps("fail {SET-UDP-TTL used on non-UDP port}");

    int ttl = VAL_INT32(ARG(ttl));
    int result = setsockopt(
        sock->fd,
        IPPROTO_IP,
        IP_TTL,
        cast(char*, &ttl),
        sizeof(ttl)
    );

    if (result < 0)
        rebFail_OS (result);

    return rebNone();
}


//
//  Dev_Net_Poll: c
//
bool Dev_Net_Poll(void)
{
    bool changed = false;

    // Process the singly linked list of in-progress transfers, removing any
    // transfers that complete.
    //
  blockscope {
    struct Reb_Sock_Transfer *transfer = Net_Transfers;
    struct Reb_Sock_Transfer **update = &Net_Transfers;

    while (transfer != nullptr) {
        changed = true;  // !!! Do we want notices on *any* work done?

        if (Transfer_Socket_Finishing(transfer)) {
            *update = transfer->next;
            FREE(struct Reb_Sock_Transfer, transfer);
            transfer = *update;
        }
        else {
            update = &transfer->next;
            transfer = transfer->next;
        }
    }
  }

    // Process the singly linked list of sockets that are listening for
    // connections.  The socket should not report that it is "finished"
    // while it is checking for accept unless there is some kind of error.
    //
  blockscope {
    struct Reb_Sock_Listener *listener = Net_Listeners;
    struct Reb_Sock_Listener **update = &Net_Listeners;
    while (listener != nullptr) {
        changed = true;  // !!! Do we want notices on *any* work done?

        if (Accept_Socket_Finishing(listener)) {
            *update = listener->next;
            FREE(struct Reb_Sock_Listener, listener);
            listener = *update;
        }
        else {
            update = &listener->next;
            listener = listener->next;
        }
    }
  }

    // Process the singly linked list of sockets that are listening for
    // connections.  The code seemed to be stylized to just keep calling
    // connect, I think (?) so do that.
    //
  blockscope {
    struct Reb_Sock_Connector *connector = Net_Connectors;
    struct Reb_Sock_Connector **update = &Net_Connectors;
    while (connector != nullptr) {
        changed = true;  // !!! Do we want notices on *any* work done?

        const REBVAL *port = CTX_ARCHETYPE(connector->port_ctx);
        Init_False(CTX_VAR(VAL_CONTEXT(port), STD_PORT_PENDING));

        *update = connector->next;
        FREE(struct Reb_Sock_Connector, connector);
        connector = *update;

        REBVAL *error = Connect_Socket_Maybe_Queued(port);
        if (error)
            fail (error);  // !!! Probably bad to fail here
    }
  }

    return changed;
}


//
//  startup*: native [  ; Note: DO NOT EXPORT!
//
//  {Initialize Network Extension (e.g. call WSAStartup() on Windows)}
//
//      return: <none>
//  ]
//
REBNATIVE(startup_p)
{
    NETWORK_INCLUDE_PARAMS_OF_STARTUP_P;

    // !!! This currently does synchronous initialization, e.g. WSAStartup()
    // on Windows.  This would be better to defer until first network use.
    //
    // !!! Note the DNS extension currently relies on this startup being
    // called instead of doing its own.
    //
    Startup_Networking();

    // We use linked lists here to manage the in progress transfers and
    // listeners.  (Making them BLOCK!s of user-exposed REBVAL* creates
    // inconvenience for unliking them when done, and also means reification
    // of implementation details).
    //
    assert(Net_Transfers == nullptr);
    assert(Net_Listeners == nullptr);
    assert(Net_Connectors == nullptr);

    // We register a "device" with the system so that when WAIT is being run
    // then our supplied polling function will be called.  That function
    // handles in-progress network transfers as well as accepting new
    // socket connections while listening on a port.
    //
    assert(Dev_Net == nullptr);
    Dev_Net = OS_Register_Device("TCP/IP Network", &Dev_Net_Poll);

    return rebNone();
}


//
//  shutdown*: native [  ; Note: DO NOT EXPORT!
//
//  {Shutdown Network Extension}
//
//      return: <none>
//  ]
//
REBNATIVE(shutdown_p)
{
    NETWORK_INCLUDE_PARAMS_OF_SHUTDOWN_P;

  blockscope {
    struct Reb_Sock_Transfer *transfer = Net_Transfers;
    while (transfer != nullptr) {
        struct Reb_Sock_Transfer *temp = transfer;
        transfer = transfer->next;
        FREE(struct Reb_Sock_Transfer, temp);
    }
    Net_Transfers = nullptr;
  }

  blockscope {
    struct Reb_Sock_Listener *listener = Net_Listeners;
    while (listener != nullptr) {
        struct Reb_Sock_Listener *temp = listener;
        listener = listener->next;
        FREE(struct Reb_Sock_Listener, temp);
    }
    Net_Listeners = nullptr;
  }

  blockscope {
    struct Reb_Sock_Connector *connector = Net_Connectors;
    while (connector != nullptr) {
        struct Reb_Sock_Connector *temp = connector;
        connector = connector->next;
        FREE(struct Reb_Sock_Connector, temp);
    }
    Net_Connectors = nullptr;
  }

    OS_Unregister_Device(Dev_Net);

    Shutdown_Networking();

    return rebNone();
}
