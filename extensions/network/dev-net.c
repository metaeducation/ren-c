//
//  File: %dev-net.c
//  Summary: "Device: TCP/IP network access"
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
// Supports TCP and UDP (but not raw socket modes.)
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include <stdlib.h>
#include <string.h>

#include "sys-net.h"

#ifdef IS_ERROR
#undef IS_ERROR //winerror.h defines this, so undef it to avoid the warning
#endif
#include "sys-core.h"

#include "tmp-mod-network.h"

#include "reb-net.h"

#if (0)
    #define WATCH1(s,a) printf(s, a)
    #define WATCH2(s,a,b) printf(s, a, b)
    #define WATCH4(s,a,b,c,d) printf(s, a, b, c, d)
#else
    #define WATCH1(s,a)
    #define WATCH2(s,a,b)
    #define WATCH4(s,a,b,c,d)
#endif

DEVICE_CMD Listen_Socket(REBREQ *sock);

#ifdef TO_WINDOWS
    extern HWND Event_Handle; // For WSAAsync API
#endif

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
    sa->sin_addr.s_addr = ip;  //htonl(ip); NOTE: REBOL stays in network byte order
    sa->sin_port = htons((unsigned short)port);
}

static void Get_Local_IP(REBREQ *sock)
{
    // Get the local IP address and port number.
    // This code should be fast and never fail.
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);

    getsockname(Req(sock)->requestee.socket, cast(struct sockaddr *, &sa), &len);
    ReqNet(sock)->local_ip = sa.sin_addr.s_addr; //htonl(ip); NOTE: REBOL stays in network byte order
    ReqNet(sock)->local_port = ntohs(sa.sin_port);
}

static bool Set_Sock_Options(SOCKET sock)
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
//  Init_Net: C
//
// Intialize networking libraries and related interfaces.
// This function will be called prior to any socket functions.
//
DEVICE_CMD Init_Net(REBREQ *dr)
{
    REBDEV *dev = cast(REBDEV*, dr);

#ifdef TO_WINDOWS
    //
    // Initialize Windows Socket API with given VERSION.
    // It is ok to call twice, as long as WSACleanup twice.
    //
    WSADATA wsaData;
    if (WSAStartup(0x0101, &wsaData))
        rebFail_OS (GET_ERROR);
#endif

    dev->flags |= RDF_INIT;
    return DR_DONE;
}


//
//  Quit_Net: C
//
// Close and cleanup networking libraries and related interfaces.
//
DEVICE_CMD Quit_Net(REBREQ *dr)
{
    UNUSED(dr);

  #ifdef TO_WINDOWS
    if (Dev_Net.flags & RDF_INIT)
        WSACleanup();
  #endif

    Dev_Net.flags &= ~RDF_INIT;
    return DR_DONE;
}



//
//  Open_Socket: C
//
// Setup a socket with the specified protocol and bind it to
// the related transport service.
//
// Returns 0 on success.
// On failure, error code is OS local.
//
// Note: This is an intialization procedure and no actual
// connection is made at this time. The IP address and port
// number are not needed, only the type of service required.
//
// After usage:
//     Close_Socket() - to free OS allocations
//
DEVICE_CMD Open_Socket(REBREQ *req)
{
    struct rebol_devreq *sock = Req(req);

    sock->state = 0;  // clear all flags

    int type;
    int protocol;
    if (sock->modes & RST_UDP) {
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
        rebFail_OS (GET_ERROR);

    sock->requestee.socket = result;
    sock->state |= RSM_OPEN;

    // Set socket to non-blocking async mode:
    if (!Set_Sock_Options(sock->requestee.socket))
        rebFail_OS (GET_ERROR);

    if (ReqNet(req)->local_port != 0) {
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

    return DR_DONE;
}


//
//  Close_Socket: C
//
// Close a socket.
//
// Returns 0 on success.
// On failure, error code is OS local.
//
DEVICE_CMD Close_Socket(REBREQ *sock)
{
    struct rebol_devreq *req = Req(sock);

    if (req->state & RSM_OPEN) {

        req->state = 0;  // clear: RSM_OPEN, RSM_CONNECT

        // If DNS pending, abort it:
        if (ReqNet(sock)->host_info) {  // indicates DNS phase active
            rebFree(ReqNet(sock)->host_info);
            req->requestee.socket = req->length; // Restore TCP socket (see Lookup)
        }

        if (CLOSE_SOCKET(req->requestee.socket) != 0)
            rebFail_OS (GET_ERROR);
    }

    return DR_DONE;
}


//
//  Lookup_Socket: C
//
// Initiate the GetHost request and return immediately.
// This is very similar to the DNS device.
// Note the temporary results buffer (must be freed later).
// Note we use the sock->requestee.handle for the DNS handle. During use,
// we store the TCP socket in the length field.
//
DEVICE_CMD Lookup_Socket(REBREQ *sock)
{
    struct rebol_devreq *req = Req(sock);

    ReqNet(sock)->host_info = NULL; // no allocated data

    // !!! R3-Alpha would use asynchronous DNS API on Windows, but that API
    // was not supported by IPv6, and developers are encouraged to use normal
    // socket APIs with their own threads.

    HOSTENT *host = gethostbyname(s_cast(req->common.data));
    if (host == NULL)
        rebFail_OS (GET_ERROR);

    memcpy(&ReqNet(sock)->remote_ip, *host->h_addr_list, 4); //he->h_length);
    req->flags &= ~RRF_DONE;

    rebElide(
        "insert/only system/ports/system make event! [",
            "type: 'lookup",
            "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(sock))),
        "]",
    rebEND);

    return DR_DONE;
}


//
//  Connect_Socket: C
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
DEVICE_CMD Connect_Socket(REBREQ *sock)
{
    int result;
    struct sockaddr_in sa;
    struct rebol_devreq *req = Req(sock);

    if (req->state & RSM_CONNECT)
        return DR_DONE; // already connected

    if (req->modes & RST_UDP) {
        req->state &= ~RSM_ATTEMPT;
        req->state |= RSM_CONNECT;

        rebElide(
            "insert/only system/ports/system make event! [",
                "type: 'connect",
                "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(sock))),
            "]",
        rebEND);

        if (req->modes & RST_LISTEN)
            return Listen_Socket(sock);

        Get_Local_IP(sock); // would overwrite local_port for listen
        return DR_DONE;
    }

    if (req->modes & RST_LISTEN)
        return Listen_Socket(sock);

    Set_Addr(&sa, ReqNet(sock)->remote_ip, ReqNet(sock)->remote_port);
    result = connect(
        req->requestee.socket, cast(struct sockaddr *, &sa), sizeof(sa)
    );

    if (result != 0) result = GET_ERROR;

    WATCH2("connect() error: %d - %s\n", result, strerror(result));

    switch (result) {

    case 0: // no error
    case NE_ISCONN:
        break; // connected, set state

#ifdef TO_WINDOWS
    case NE_INVALID: // Corrects for Microsoft bug
#endif
    case NE_WOULDBLOCK:
    case NE_INPROGRESS:
    case NE_ALREADY:
        // Still trying:
        req->state |= RSM_ATTEMPT;
        return DR_PEND;

    default:
        req->state &= ~RSM_ATTEMPT;
        rebFail_OS (result);
    }

    req->state &= ~RSM_ATTEMPT;
    req->state |= RSM_CONNECT;
    Get_Local_IP(sock);

    rebElide(
        "insert/only system/ports/system make event! [",
            "type: 'connect",
            "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(sock))),
        "]",
    rebEND);

    return DR_DONE;
}


//
//  Transfer_Socket: C
//
// Write or read a socket (for connection-based protocols).
//
// This function is asynchronous. It will return immediately.
// You can call this function again to check the pending connection.
//
// The mode is RSM_RECEIVE or RSM_SEND.
//
// The function will return:
//     =0: succeeded
//     >0: in-progress, still trying
//     <0: error occurred, no longer trying
//
// Before usage:
//     Open_Socket()
//     Connect_Socket()
//     Verify that RSM_CONNECT is true
//     Setup the sock->common.data and sock->length
//
// Note that the mode flag is cleared by the caller, not here.
//
DEVICE_CMD Transfer_Socket(REBREQ *sock)
{
    struct rebol_devreq *req = Req(sock);

    if (not (req->state & RSM_CONNECT) and not (req->modes & RST_UDP))
        rebJumps(
            "FAIL {RSM_CONNECT must be true in Transfer_Socket() unless UDP}",
            rebEND
        );

    struct sockaddr_in remote_addr;
    socklen_t addr_len = sizeof(remote_addr);

    int mode = (req->command == RDC_READ ? RSM_RECEIVE : RSM_SEND);
    req->state |= mode;

    // Limit size of transfer
    //
    size_t len = MIN(req->length - req->actual, MAX_TRANSFER);

    int result;

    if (mode == RSM_SEND) {
        // If host is no longer connected:
        Set_Addr(
            &remote_addr,
            ReqNet(sock)->remote_ip,
            ReqNet(sock)->remote_port
        );
        result = sendto(
            req->requestee.socket,
            s_cast(req->common.data), len,
            MSG_NOSIGNAL, // Flags
            cast(struct sockaddr*, &remote_addr), addr_len
        );
        WATCH2("send() len: %d actual: %d\n", len, result);

        if (result >= 0) {
            req->common.data += result;
            req->actual += result;
            if (req->actual >= req->length) {
                rebElide(
                    "insert/only system/ports/system make event! [",
                        "type: 'wrote",
                        "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(sock))),
                    "]",
                rebEND);

                return DR_DONE;
            }
            req->flags |= RRF_ACTIVE; // notify OS_WAIT of activity
            return DR_PEND;
        }
        // if (result < 0) ...
    }
    else {
        assert(VAL_INDEX(req->common.binary) == 0);

        REBBIN *bin = VAL_BINARY(req->common.binary);
        assert(SER_AVAIL(bin) >= len);
        REBLEN old_len = BIN_LEN(bin);

        result = recvfrom(
            req->requestee.socket,
            s_cast(BIN_AT(bin, old_len)), len,
            0, // Flags
            cast(struct sockaddr*, &remote_addr), &addr_len
        );
        WATCH2("recv() len: %d result: %d\n", len, result);

        if (result > 0) {
            if (req->modes & RST_UDP) {
                ReqNet(sock)->remote_ip = remote_addr.sin_addr.s_addr;
                ReqNet(sock)->remote_port = ntohs(remote_addr.sin_port);
            }
            TERM_BIN_LEN(bin, old_len + result);

            rebElide(
                "insert/only system/ports/system make event! [",
                    "type: 'read",
                    "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(sock))),
                "]",
            rebEND);

            return DR_DONE;
        }
        if (result == 0) {      // The socket gracefully closed.
            TERM_BIN_LEN(bin, old_len);
            req->state &= ~RSM_CONNECT; // But, keep RRF_OPEN true

            rebElide(
                "insert/only system/ports/system make event! [",
                    "type: 'close",
                    "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(sock))),
                "]",
            rebEND);

            return DR_DONE;
        }
        // if (result < 0) ...
    }

    result = GET_ERROR;

    if (result != NE_WOULDBLOCK)
        rebFail_OS (result);

    return DR_PEND; // still waiting
}


//
//  Listen_Socket: C
//
// Setup a server (listening) socket (TCP or UDP).
//
// Before usage:
//     Open_Socket();
//     Set local_port to desired port number.
//
// Use this instead of Connect_Socket().
//
DEVICE_CMD Listen_Socket(REBREQ *sock)
{
    struct rebol_devreq *req = Req(sock);

    int result;

    // Setup socket address range and port:
    //
    struct sockaddr_in sa;
    Set_Addr(&sa, INADDR_ANY, ReqNet(sock)->local_port);

    // Allow listen socket reuse:
    //
    int len = 1;
    result = setsockopt(
        req->requestee.socket, SOL_SOCKET, SO_REUSEADDR,
        cast(char*, &len), sizeof(len)
    );
    if (result != 0)
        rebFail_OS (GET_ERROR);

    // Bind the socket to our local address:
    result = bind(
        req->requestee.socket, cast(struct sockaddr *, &sa), sizeof(sa)
    );
    if (result != 0)
        rebFail_OS (GET_ERROR);

    req->state |= RSM_BIND;

    // For TCP connections, setup listen queue:
    if (not (req->modes & RST_UDP)) {
        result = listen(req->requestee.socket, SOMAXCONN);
        if (result != 0)
            rebFail_OS (GET_ERROR);
        req->state |= RSM_LISTEN;
    }

    Get_Local_IP(sock);
    req->command = RDC_CREATE; // the command done on wakeup

    return DR_PEND;
}



//
//  Modify_Socket: C
//
// !!! R3-Alpha had no RDC_MODIFY commands.  Some way was needed to get
// multicast setting through to the platform-specific port code, and this
// method was chosen.  Eventually, the ports *themselves* should be extension
// modules instead of in core, and then there won't be concern about the
// mixture of port dispatch code with platform code.
//
DEVICE_CMD Modify_Socket(REBREQ *sock)
{
    struct rebol_devreq *req = Req(sock);

    assert(req->command == RDC_MODIFY);

    REBFRM *frame_ = FRM(cast(void*, req->common.data));
    int result = 0;

    switch (req->flags) {
    case 3171: {
        NETWORK_INCLUDE_PARAMS_OF_SET_UDP_MULTICAST;

        UNUSED(ARG(port)); // implicit from sock, which caller extracted

        if (not (req->modes & RST_UDP)) // !!! other checks?
            rebJumps("FAIL {SET-UDP-MULTICAST used on non-UDP port}", rebEND);

        struct ip_mreq mreq;
        memcpy(&mreq.imr_multiaddr.s_addr, VAL_TUPLE(ARG(group)), 4);
        memcpy(&mreq.imr_interface.s_addr, VAL_TUPLE(ARG(member)), 4);

        result = setsockopt(
            req->requestee.socket,
            IPPROTO_IP,
            REF(drop) ? IP_DROP_MEMBERSHIP : IP_ADD_MEMBERSHIP,
            cast(char*, &mreq),
            sizeof(mreq)
        );

        break; }

    case 2365: {
        NETWORK_INCLUDE_PARAMS_OF_SET_UDP_TTL;

        UNUSED(ARG(port)); // implicit from sock, which caller extracted

        if (not (req->modes & RST_UDP)) // !!! other checks?
            rebJumps("FAIL {SET-UDP-TTL used on non-UDP port}", rebEND);

        int ttl = VAL_INT32(ARG(ttl));
        result = setsockopt(
            req->requestee.socket,
            IPPROTO_IP,
            IP_TTL,
            cast(char*, &ttl),
            sizeof(ttl)
        );

        break; }

    default:
        rebJumps("FAIL {Unknown socket MODIFY operation}", rebEND);
    }

    if (result < 0)
        rebFail_OS (result);

    return DR_DONE;
}


//
//  Accept_Socket: C
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
DEVICE_CMD Accept_Socket(REBREQ *sock)
{
    struct rebol_devreq *req = Req(sock);

    // !!! In order to make packets appear to originate from a specific UDP
    // point, a "two-ended" connection-like socket is created for UDP.  But
    // it cannot accept connections.  Without better knowledge of how to stay
    // pending for UDP purposes but not TCP purposes, just return for now.
    //
    // This happens because of RDC_CREATE being posted in Listen_Socket; so
    // it's not clear whether to not send that event or squash it here.  It
    // must be accepted, however, to recvfrom() data in the future.
    //
    if (req->modes & RST_UDP) {
            rebElide("insert/only system/ports/system make event! [",
                "type: 'accept",
                "port:", CTX_ARCHETYPE(CTX(ReqPortCtx(sock))),
            "]",
        rebEND);

        return DR_PEND;
    }

    // Accept a new socket, if there is one:

    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    int fd = accept(req->requestee.socket, cast(struct sockaddr *, &sa), &len);

    if (fd == -1) {
        int errnum = GET_ERROR;
        if (errnum == NE_WOULDBLOCK)
            return DR_PEND;

        rebFail_OS (errnum);
    }

    if (not Set_Sock_Options(fd))
        rebFail_OS (GET_ERROR);

    // Create a new port using ACCEPT

    REBCTX *listener = CTX(ReqPortCtx(sock));
    REBCTX *connection = Copy_Context_Shallow_Managed(listener);
    PUSH_GC_GUARD(connection);

    Init_Blank(CTX_VAR(connection, STD_PORT_DATA)); // just to be sure.
    Init_Blank(CTX_VAR(connection, STD_PORT_STATE)); // just to be sure.

    REBREQ *sock_new = Ensure_Port_State(CTX_ARCHETYPE(connection), &Dev_Net);

    struct rebol_devreq *req_new = Req(sock_new);

    memset(req_new, '\0', sizeof(struct devreq_net));  // !!! already zeroed?
    req_new->device = req->device;  // !!! already set?
    req_new->common.data = nullptr;

    req_new->flags |= RRF_OPEN;
    req_new->state |= (RSM_OPEN | RSM_CONNECT);

    // NOTE: REBOL stays in network byte order, no htonl(ip) needed
    //
    req_new->requestee.socket = fd;
    ReqNet(sock_new)->remote_ip = sa.sin_addr.s_addr;
    ReqNet(sock_new)->remote_port = ntohs(sa.sin_port);
    Get_Local_IP(sock_new);

    ReqPortCtx(sock_new) = connection;

    rebElide(
        "append ensure block!", CTX_VAR(listener, STD_PORT_CONNECTIONS),
        CTX_ARCHETYPE(connection), // will GC protect during run
        rebEND
    );

    DROP_GC_GUARD(connection);

    // We've added the new PORT! for the connection, but the client has to
    // find out about it and get an `accept` event.  Signal that.
    //
    rebElide(
        "insert/only system/ports/system make event! [",
            "type: 'accept",
            "port:", CTX_ARCHETYPE(listener),
        "]",
    rebEND);

    // Even though we signalled, we keep the listen pending to
    // accept additional connections.
    //
    return DR_PEND;
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_CFUNC Dev_Cmds[RDC_MAX] = {
    Init_Net,
    Quit_Net,
    Open_Socket,
    Close_Socket,
    Transfer_Socket,        // Read
    Transfer_Socket,        // Write
    Connect_Socket,
    0,  // query
    Modify_Socket,          // modify
    Accept_Socket,          // Create
    0,  // delete
    0,  // rename
    Lookup_Socket
};

DEFINE_DEV(
    Dev_Net,
    "TCP/IP Network", 1, Dev_Cmds, RDC_MAX, sizeof(struct devreq_net)
);
