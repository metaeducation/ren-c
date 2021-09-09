//
//  File: %dev-net.c
//  Summary: "Device: TCP/IP network access"
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
// Supports TCP and UDP (but not raw socket modes.)
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include <stdlib.h>
#include <string.h>

#include "sys-net.h"

#ifdef IS_ERROR
    #undef IS_ERROR  // winerror.h defines, so undef it to avoid the warning
#endif
#include "sys-core.h"

#include "tmp-mod-network.h"

#include "reb-net.h"

REBVAL *Start_Listening_On_Socket(const REBVAL *port);


// Prevent sendmsg/write raising SIGPIPE the TCP socket is closed:
// https://stackoverflow.com/q/108183/
// Linux does not support SO_NOSIGPIPE
//
#ifndef MSG_NOSIGNAL
    #define MSG_NOSIGNAL 0
#endif

/***********************************************************************
**
**  Local Functions
**
***********************************************************************/

static void Set_Addr(struct sockaddr_in *sa, long ip, int port)
{
    // Set the IP address and port number in a socket_addr struct.

    memset(sa, 0, sizeof(*sa));
    sa->sin_family = AF_INET;

    // htonl(ip); NOTE: REBOL stays in network byte order
    //
    sa->sin_addr.s_addr = ip;
    sa->sin_port = htons((unsigned short)port);
}

static void Get_Local_IP(SOCKREQ *sock)
{
    // Get the local IP address and port number.
    // This code should be fast and never fail.

    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);

    getsockname(sock->socket, cast(struct sockaddr *, &sa), &len);

    // htonl(ip); NOTE: REBOL stays in network byte order
    //
    sock->local_ip = sa.sin_addr.s_addr;
    sock->local_port_number = ntohs(sa.sin_port);
}

static bool Try_Set_Sock_Options(SOCKET sock)
{
  #if defined(SO_NOSIGPIPE)
    //
    // Prevent sendmsg/write raising SIGPIPE if the TCP socket is closed:
    // https://stackoverflow.com/q/108183/
    //
    int on = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) < 0) {
        return false;
    }
  #endif

    // Set non-blocking mode. Return TRUE if no error.
  #ifdef FIONBIO
    unsigned long mode = 1;
    return IOCTL(sock, FIONBIO, &mode) == 0;
  #else
    int flags;
    flags = fcntl(sock, F_GETFL, 0);
    flags |= O_NONBLOCK;
    return fcntl(sock, F_SETFL, flags) >= 0;
  #endif
}


//
//  Startup_Networking: C
//
// Intialize networking libraries and related interfaces.
// This function will be called prior to any socket functions.
//
extern void Startup_Networking(void);
void Startup_Networking(void)
{
  #if TO_WINDOWS
    //
    // Initialize Windows Socket API with given VERSION.
    // It is ok to call twice, as long as WSACleanup twice.
    //
    WSADATA wsaData;
    if (WSAStartup(0x0101, &wsaData))
        rebFail_OS (GET_ERROR);
  #endif
}


//
//  Shutdown_Networking: C
//
// Close and cleanup networking libraries and related interfaces.
//
extern void Shutdown_Networking(void);
void Shutdown_Networking(void)
{
  #if TO_WINDOWS
    WSACleanup();
  #endif
}


//
//  Open_Socket: C
//
// Setup a socket with the specified protocol and bind it to the related
// transport service.
//
// Note: No actual connection is made by calling this routine.  The IP address
// and port number are not needed, only the type of service required.
//
// After usage:
//     Close_Socket() - to free OS allocations
//
REBVAL *Open_Socket(const REBVAL *port)
{
    SOCKREQ *sock = Sock_Of_Port(port);

    assert(sock->fd == SOCKET_NONE);
    assert(sock->socket == SOCKET_NONE);

    sock->modes = 0;  // clear all flags

    int type;
    int protocol;
    if (sock->transport == TRANSPORT_UDP) {
        type = SOCK_DGRAM;
        protocol = IPPROTO_UDP;
    }
    else {  // TCP is default
        type = SOCK_STREAM;
        protocol = IPPROTO_TCP;
    }

    // Bind to the transport service, return socket handle or error:

    long result = cast(int, socket(AF_INET, type, protocol));

    if (result == -1)
        return rebError_OS(GET_ERROR);

    sock->fd = result;

    // Set socket to non-blocking async mode:
    if (not Try_Set_Sock_Options(sock->fd))
        return rebError_OS(GET_ERROR);

    if (sock->local_port_number != 0) {
        //
        // !!! This modification was made to support a UDP application which
        // wanted to listen on a UDP port, as well as make packets appear to
        // come from the same port it was listening on when writing to another
        // UDP port.  But the only way to make packets appear to originate
        // from a specific port is using bind:
        //
        // https://stackoverflow.com/q/9873061
        //
        // So a second socket can't use bind() to listen on that same port.
        // Hence, a single socket has to be used for both writing and for
        // listening.  This tries to accomplish that for UDP by going ahead
        // and making a port that can both listen and send.  That processing
        // is done during CONNECT.
        //
        sock->modes |= RST_LISTEN;
    }

    return nullptr;
}


//
//  Close_Socket: C
//
REBVAL *Close_Socket(const REBVAL *port)
{
    SOCKREQ *sock = Sock_Of_Port(port);

    REBVAL *error = nullptr;

    if (sock->fd == SOCKET_NONE)  // R3-Alpha allowed closing closed sockets
        assert(sock->socket == SOCKET_NONE);  // shouldn't be connected
    else {
        // If it was trying to connect, then drop it from the connection list.

        struct Reb_Sock_Connector *connector = Net_Connectors;
        struct Reb_Sock_Connector **update = &Net_Connectors;
        while (connector != nullptr) {
            if (connector->port_ctx == VAL_CONTEXT(port)) {
                assert(sock->modes & RSM_ATTEMPT);
                assert(sock->socket == SOCKET_NONE);
                *update = connector->next;
                FREE(struct Reb_Sock_Connector, connector);
                connector = *update;
            }
            else {
                update = &connector->next;
                connector = connector->next;
            }
        }

        if (CLOSE_SOCKET(sock->fd) != 0)  // platform independent close() macro
            error = rebError_OS(GET_ERROR);

        sock->socket = sock->fd = SOCKET_NONE;
        sock->modes = 0;
    }

    return error;
}


//
//  Lookup_Socket: C
//
// !!! R3-Alpha would use asynchronous DNS API on Windows, but that API
// was not supported by IPv6, and developers are encouraged to use normal
// socket APIs with their own threads.  Because the Reb_Device model is slated
// for replacement it is not worth investing in aynchronous behavior here.
//
REBVAL *Lookup_Socket(const REBVAL *port, const REBVAL *hostname)
{
    SOCKREQ *sock = Sock_Of_Port(port);

    assert(IS_TEXT(hostname));
    const REBYTE *hostname_utf8 = VAL_UTF8_AT(hostname);

    HOSTENT *host = gethostbyname(cs_cast(hostname_utf8));
    if (host == nullptr)
        return rebError_OS(GET_ERROR);

    // Synchronously fill in the port's remote_ip with the answer to looking
    // up the hostname.
    //
    memcpy(&sock->remote_ip, *host->h_addr_list, 4);

    return nullptr;
}


//
//  Connect_Socket_Maybe_Queued: C
//
// Connect a socket to a service.
// Only required for connection-based protocols (e.g. not UDP).
// The IP address must already be resolved before calling.
//
// This function is asynchronous. It will return immediately.
// You can call this function again to check the pending connection.
//
// The function will return:
//     =0: connection succeeded (or already is connected)
//     >0: in-progress, still trying
//     <0: error occurred, no longer trying
//
// Before usage:
//     Open_Socket() -- to allocate the socket
//
REBVAL *Connect_Socket_Maybe_Queued(const REBVAL *port)
{
    SOCKREQ *sock = Sock_Of_Port(port);

    int result;
    struct sockaddr_in sa;

    assert(sock->fd != SOCKET_NONE);  // must be open

    if (sock->socket != SOCKET_NONE)
        return nullptr;  // !!! R3-Alpha tolerated connected, should we?

    if (sock->transport == TRANSPORT_UDP) {
        sock->modes &= ~RSM_ATTEMPT;
        sock->socket = sock->fd;

        rebElide(
            "insert system/ports/system make event! [",
                "type: 'connect",
                "port:", port,
            "]"
        );

        if (sock->modes & RST_LISTEN)
            return Start_Listening_On_Socket(port);

        Get_Local_IP(sock); // would overwrite local_port for listen
        return nullptr;
    }

    if (sock->modes & RST_LISTEN)
        return Start_Listening_On_Socket(port);

    Set_Addr(&sa, sock->remote_ip, sock->remote_port_number);
    result = connect(
        sock->fd, cast(struct sockaddr *, &sa), sizeof(sa)
    );

    if (result != 0)
        result = GET_ERROR;

    switch (result) {
      case 0:  // no error
      case NE_ISCONN:
        break;  // connected, set state

    #if TO_WINDOWS
      case NE_INVALID:  // Comment said "Corrects for Microsoft bug"
    #endif
      case NE_WOULDBLOCK:
      case NE_INPROGRESS:
      case NE_ALREADY: {
        sock->modes |= RSM_ATTEMPT;

        // Put it into the queue of sockets that are awaiting connection.
        // (The current model is that will just keep calling Connect_Socket()
        // over and over again, but before it does it removes the socket from
        // the list, so we re-add it each time we can't connect.)
        //
        struct Reb_Sock_Connector *connector
            = TRY_ALLOC(struct Reb_Sock_Connector);

        connector->port_ctx = VAL_CONTEXT(port);
        connector->next = Net_Connectors;
        Net_Connectors = connector;
        Init_True(RESET(CTX_VAR(VAL_CONTEXT(port), STD_PORT_PENDING)));

        return nullptr; }

      default:
        sock->modes &= ~RSM_ATTEMPT;

        // !!! Review policy on asynchronous error delivery.
        //
        // https://github.com/metaeducation/ren-c/issues/1048
        //
        Close_Socket(port);
        return rebError_OS(result);
    }

    sock->modes &= ~RSM_ATTEMPT;
    sock->socket = sock->fd;  // indicates connected
    Get_Local_IP(sock);

    rebElide(
        "insert system/ports/system make event! [",
            "type: 'connect",
            "port:", port,
        "]"
    );

    return nullptr;
}


//
//  Transfer_Socket_Finishing: C
//
// Write or read a socket for connection-based protocols.  Which direction
// is indicated by transfer->direction (TRANSFER_SEND or TRANSFER_RECEIVE).
//
// A READ or a WRITE action on a TCP port will put a transfer structure in a
// linked list that causes the network extension's polling hook to call
// this function.  It will be called repeatedly until this function indicates
// the transfer is complete or has errored.
//
bool Transfer_Socket_Finishing(struct Reb_Sock_Transfer *transfer)
{
    const REBVAL *port = CTX_ARCHETYPE(transfer->port_ctx);

    REBVAL *state = CTX_VAR(transfer->port_ctx, STD_PORT_STATE);
    SOCKREQ *sock = cast(SOCKREQ*, VAL_BINARY_AT_ENSURE_MUTABLE(state));

    if (
        sock->socket == SOCKET_NONE  // not connected
        and sock->transport != TRANSPORT_UDP
    ){
        rebJumps(
            "fail {Socket must be connected in Transfer_Socket() unless UDP}"
        );
    }

    struct sockaddr_in remote_addr;
    socklen_t addr_len = sizeof(remote_addr);

    int result;

    // We should not still be getting called in the Net_Transfers list unless
    // there is more left to transfer.
    //
    assert(transfer->actual < transfer->length);

    if (transfer->direction == TRANSFER_SEND) {
        size_t len = transfer->length - transfer->actual;  // total to try

        const REBBIN *bin = VAL_BINARY(transfer->binary);
        ASSERT_SERIES_TERM_IF_NEEDED(bin);

        const char *data = cs_cast(BIN_AT(bin, transfer->actual));

        // If host is no longer connected:
        //
        Set_Addr(&remote_addr, sock->remote_ip, sock->remote_port_number);
        result = sendto(
            sock->socket,
            data, len,
            MSG_NOSIGNAL, // Flags
            cast(struct sockaddr*, &remote_addr), addr_len
        );

      /*#if !defined(NDEBUG)
        if (SPORADICALLY(10))
            goto fake_send_error;
      #endif*/

        if (result < 0) {
            result = GET_ERROR;
            if (result == NE_WOULDBLOCK)
                return false;  // don't consider blocking an actual "error"

          /*fake_send_error:*/
            rebRelease(transfer->binary);
            goto handle_error;
        }

        transfer->actual += result;

        assert(transfer->actual <= transfer->length);
        if (transfer->actual == transfer->length) {
            rebRelease(transfer->binary);
            TRASH_POINTER_IF_DEBUG(transfer->binary);

            rebElide(
                "insert system/ports/system make event! [",
                    "type: 'wrote",
                    "port:", port,
                "]"
            );

            Init_False(RESET(CTX_VAR(VAL_CONTEXT(port), STD_PORT_PENDING)));

            return true;  // finishing
        }

        return false;  // still more to go
    }
    else {
        assert(transfer->direction == TRANSFER_RECEIVE);

        // The buffer should be big enough to hold the request size (or some
        // implementation-defined size) if transfer->length is MAX_UINT32.
        //
        REBVAL *port_data = CTX_VAR(transfer->port_ctx, STD_PORT_DATA);
        REBBIN *bin = VAL_BINARY_KNOWN_MUTABLE(port_data);
        ASSERT_SERIES_TERM_IF_NEEDED(bin);

        size_t len;
        if (transfer->length == UINT32_MAX)
            len = SER_AVAIL(bin);
        else {
            len = transfer->length - transfer->actual;
            assert(SER_AVAIL(bin) >= len);
        }

        assert(VAL_INDEX(port_data) == 0);

        REBLEN old_len = BIN_LEN(bin);

        result = recvfrom(
            sock->socket,
            s_cast(BIN_AT(bin, old_len)), len,
            0, // Flags
            cast(struct sockaddr*, &remote_addr), &addr_len
        );

      /*#if !defined(NDEBUG)
        if (SPORADICALLY(10))
            goto fake_receive_error;  // fake error after termination
      #endif*/

        if (result < 0) {
            result = GET_ERROR;
            if (result == NE_WOULDBLOCK)
                return false;  // don't consider blocking an actual "error"

          /*fake_receive_error:*/
            TERM_BIN_LEN(bin, 0);  // in case it was partly corrupted
            goto handle_error;
        }

        TERM_BIN_LEN(bin, old_len + result);
        transfer->actual += result;

        if (sock->transport == TRANSPORT_UDP) {
            sock->remote_ip = remote_addr.sin_addr.s_addr;
            sock->remote_port_number = ntohs(remote_addr.sin_port);
        }

        bool finished;
        if (
            transfer->length == transfer->actual  // read an exact amount
            or (
                transfer->length == UINT32_MAX  // want to read as much you can
                and result != 0  // ...and it wasn't a clean socket close
            ) or (
                transfer->length != UINT32_MAX  // we wanted to read exactly...
                and result == 0  // ...but the socket closed cleanly
                and transfer->actual > 0  // ...and there's some data in the buffer
            )
        ){
            // If we had a /PART setting on the READ, we follow the Rebol
            // convention of allowing less than that to be accepted, which
            // FILE! does as well:
            //
            //     >> write %test.dat #{01}
            //
            //     >> read/part %test.dat 100000
            //     == #{01}
            //
            // Hence it is the caller's responsibility to check how much
            // data they actually got with a READ/PART call.
            //
            rebElide(
                "insert system/ports/system make event! [",
                    "type: 'read",
                    "port:", port,
                "]"
            );

            Init_False(RESET(CTX_VAR(VAL_CONTEXT(port), STD_PORT_PENDING)));

            finished = true;  // don't return yet, if closing... need event
        }
        else
            finished = false;

        // 0 in a TCP connection means "The socket gracefully closed".  But
        // for UDP, apparently reading 0 can mean a send of size 0.
        //
        if (result == 0 and sock->transport == TRANSPORT_TCP) {
            rebElide(
                "insert system/ports/system make event! [",
                    "type: 'close",
                    "port:", port,
                "]"
            );

            // !!! This used to call close socket.  But if the socket has
            // "gracefully closed" that just opens up to raising an error,
            // and error reporting isn't good here.  Is this better?
            //
            sock->socket = sock->fd = SOCKET_NONE;
            sock->modes = 0;
            return true;
        }

        return finished;  // Not done (and we didn't send a READ EVENT! yet)
    }

  handle_error: {
    REBVAL *error = rebError_OS(result);

    // Don't want to raise errors synchronously because we may be in the
    // event loop, e.g. `trap [write ...]` can't work if the writing
    // winds up happening outside the TRAP.  Try poking an error into
    // the state.
    //
    // The default awake handlers will just FAIL on the error, but this
    // can be overridden.
    //
    rebElide(
        "(", port, ")/error:", rebR(error),

        "insert system/ports/system make event! [",
            "type: 'error",
            "port:", port,
        "]"
    );

    // We are killing the request that has the network error (it cannot be
    // continued).  Returning finished will detach it.
    //
    return true;
  }
}


//
//  Start_Listening_On_Socket: C
//
// Setup a server (listening) socket (TCP or UDP).
//
// Before usage:
//     Open_Socket();
//     Set local_port to desired port number.
//
// Use this instead of Connect_Socket().
//
REBVAL *Start_Listening_On_Socket(const REBVAL *port)
{
    SOCKREQ *sock = Sock_Of_Port(port);

    assert(sock->fd != SOCKET_NONE);  // must be open
    assert(sock->socket == SOCKET_NONE);  // shouldn't be connected

    int result;

    // Setup socket address range and port:
    //
    struct sockaddr_in sa;
    Set_Addr(&sa, INADDR_ANY, sock->local_port_number);

    // Allow listen socket reuse:
    //
    int len = 1;
    result = setsockopt(
        sock->fd, SOL_SOCKET, SO_REUSEADDR,
        cast(char*, &len), sizeof(len)
    );
    if (result != 0)
        return rebError_OS(GET_ERROR);

    // Bind the socket to our local address:
    result = bind(
        sock->fd, cast(struct sockaddr *, &sa), sizeof(sa)
    );
    if (result != 0)
        return rebError_OS(GET_ERROR);

    sock->modes |= RSM_BIND;

    // For TCP connections, setup listen queue
    //
    if (not (sock->transport == TRANSPORT_UDP)) {
        result = listen(sock->fd, SOMAXCONN);
        if (result != 0)
            return rebError_OS (GET_ERROR);
        sock->modes |= RSM_LISTEN;
    }

    Get_Local_IP(sock);

    // Add to list of polled listeners, so that connections can be accepted
    // during WAIT
    //
    struct Reb_Sock_Listener *listener = TRY_ALLOC(struct Reb_Sock_Listener);
    listener->port_ctx = VAL_CONTEXT(port);
    listener->next = Net_Listeners;
    Net_Listeners = listener;
    Init_True(CTX_VAR(VAL_CONTEXT(port), STD_PORT_PENDING));

    return nullptr;
}


//
//  Accept_Socket_Finishing: C
//
// Accept an inbound connection on a TCP listen socket.
//
// The function will return:
//     =0: succeeded
//     >0: in-progress, still trying
//     <0: error occurred, no longer trying
//
// Before usage:
//     Open_Socket();
//     Set local_port to desired port number.
//     Listen_Socket();
//
bool Accept_Socket_Finishing(struct Reb_Sock_Listener *listener)
{
    const REBVAL *listening_port = CTX_ARCHETYPE(listener->port_ctx);
    SOCKREQ *listening_sock = Sock_Of_Port(listening_port);

    // !!! In order to make packets appear to originate from a specific UDP
    // point, a "two-ended" connection-like socket is created for UDP.  But
    // it cannot accept connections.  Without better knowledge of how to stay
    // pending for UDP purposes but not TCP purposes, just return for now.
    //
    // This happens because UDP still adds to the list in Listen_Socket; so
    // it's not clear whether to not send that event or squash it here.  It
    // must be accepted, however, to recvfrom() data in the future.
    //
    if (listening_sock->transport == TRANSPORT_UDP) {
        rebElide("insert system/ports/system make event! [",
            "type: 'accept",
            "port:", listening_port,
        "]");

        return false;  // keep listening
    }

    // Accept a new socket, if there is one:

    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    int new_fd = accept(listening_sock->fd, cast(struct sockaddr*, &sa), &len);

    if (new_fd == -1) {
        int errnum = GET_ERROR;
        if (errnum == NE_WOULDBLOCK)
            return false;

        rebFail_OS (errnum);
    }

    if (not Try_Set_Sock_Options(new_fd))
        rebFail_OS (GET_ERROR);

    // Create a new port using ACCEPT

    REBCTX *connection = Copy_Context_Shallow_Managed(listener->port_ctx);
    PUSH_GC_GUARD(connection);

    Init_Nulled(CTX_VAR(connection, STD_PORT_DATA));  // just to be sure.

    REBVAL *c_state = CTX_VAR(connection, STD_PORT_STATE);
    REBBIN *bin = Make_Binary(sizeof(SOCKREQ));
    Init_Binary(c_state, bin);
    memset(BIN_HEAD(bin), 0, sizeof(SOCKREQ));
    TERM_BIN_LEN(bin, sizeof(SOCKREQ));

    SOCKREQ *sock_new = Sock_Of_Port(CTX_ARCHETYPE(connection));

    // NOTE: REBOL stays in network byte order, no htonl(ip) needed
    //
    sock_new->fd = new_fd;  // treat as open
    sock_new->socket = new_fd;  // also treat as connected
    sock_new->remote_ip = sa.sin_addr.s_addr;
    sock_new->remote_port_number = ntohs(sa.sin_port);
    Get_Local_IP(sock_new);

    rebElide(
        "append ensure block!",
            CTX_VAR(VAL_CONTEXT(listening_port), STD_PORT_CONNECTIONS),
            CTX_ARCHETYPE(connection)  // will GC protect during run
    );

    DROP_GC_GUARD(connection);

    // We've added the new PORT! for the connection, but the client has to
    // find out about it and get an `accept` event.  Signal that.
    //
    rebElide(
        "insert system/ports/system make event! [",
            "type: 'accept",
            "port:", listening_port,
        "]"
    );

    return false;  // keep listening
}
