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
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// !!! Rebol's historical port model was not well suited to streaming.  It
// could only process one chunk of data being read or written at a time, and
// wasn't able to do both reading and writing at once.  It was also very dated
// in not supporting non-blocking APIs.  It is being incrementally migrated
// to use libuv, as part of a broader effort to ultimately make a more
// suitable streaming design.
//

#include "uv.h"  // includes windows.h
#ifdef TO_WINDOWS
    #undef IS_ERROR  // windows.h defines, contentious with IS_ERROR in Ren-C
#endif

#include "sys-core.h"

#include "reb-net.h"
extern REBVAL *rebError_UV(int err);

#include "tmp-mod-network.h"


REBDEV *Dev_Net;

#define NET_BUF_SIZE 32*1024


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
    int len = sizeof(sa);

    uv_tcp_getsockname(&sock->tcp, cast(struct sockaddr *, &sa), &len);
    assert(len == sizeof(sa));

    // htonl(ip); NOTE: REBOL stays in network byte order
    //
    sock->local_ip = sa.sin_addr.s_addr;
    sock->local_port_number = ntohs(sa.sin_port);
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

    sock->modes = 0;  // clear all flags

    assert(sock->transport == TRANSPORT_TCP);  // different UDP libuv functions

    int result = uv_tcp_init_ex(uv_default_loop(), &sock->tcp, AF_INET);
    if (result < 0)
        return rebError_UV(result);

    sock->fd = 0;  // signal tcp is set

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
        assert(sock->stream == nullptr);  // shouldn't be connected
    else {
        uv_close(cast(uv_handle_t*, &sock->tcp), nullptr);

        sock->stream = nullptr;
        sock->fd = SOCKET_NONE;
        sock->modes = 0;
    }

    return error;
}


//
//  Lookup_Socket_Synchronously: C
//
// !!! R3-Alpha would use asynchronous DNS API on Windows, but that API
// was not supported by IPv6, and developers are encouraged to use normal
// socket APIs with their own threads.  Now that we use libuv, there is again
// the ability to specify a callback and do asynchronous lookup.  But this
// would have to be fit in with a client understanding for how to request a
// LOOKUP event, and when it had to be waited on.  For now it's synchronous.
//
REBVAL *Lookup_Socket_Synchronously(
    const REBVAL *port,
    const REBVAL *hostname
){
    SOCKREQ *sock = Sock_Of_Port(port);

    assert(IS_TEXT(hostname));
    const char *hostname_utf8 = cs_cast(VAL_UTF8_AT(hostname));
    char *port_number_utf8 = rebSpell(
        Lib(FORM), rebI(sock->remote_port_number)
    );

    // !!! You can leave the "hints" argument as nullptr.  But this is what
    // Julia said for hints, which didn't prescribe an ai_family of PF_INET,
    // and it also used memset() to 0...so it got hints.ai_protocol as
    // IPPROTO_IP which is called a "dummy for IP":
    //
    //     struct addrinfo hints;
    //     memset(&hints, 0, sizeof(hints));
    //     hints.ai_family = PF_UNSPEC;
    //     hints.ai_socktype = SOCK_STREAM;
    //     hints.ai_flags |= AI_CANONNAME;
    //
    // The example in libuv's documentation was more specific and did not
    // bother with the memset...but it set the ai_protocol.  For starters we
    // use the simpler-seeming libuv case.
    //
    struct addrinfo hints;
    hints.ai_family = PF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    // This is a replacement for:
    //
    //     HOSTENT *host = gethostbyname(cs_cast(hostname_utf8));
    //
    uv_getaddrinfo_t req;
    int result = uv_getaddrinfo(
        uv_default_loop(),
        &req,
        nullptr,  // callback
        hostname_utf8,  // called "node" in libuv, but "hostname" in POSIX
        port_number_utf8,  // "service" string or port (e.g. "echo", "80")
        &hints  // "hints" (which is a const struct addrinfo*)
    );

    rebFree(port_number_utf8);

    if (result != 0)
        return rebError_UV(result);

    // Synchronously fill in the port's remote_ip with the answer to looking
    // up the hostname.
    //
    // This is a replacement for:
    //
    //      memcpy(&sock->remote_ip, *host->h_addr_list, 4);
    //
    struct sockaddr_in *sa = cast(struct sockaddr_in*, req.addrinfo->ai_addr);

    // https://stackoverflow.com/q/31343855/
    //
    assert(req.addrinfo->ai_addrlen == 16);
    assert(sizeof(sa->sin_addr) == 4);
    memcpy(&sock->remote_ip, &sa->sin_addr, 4);

    uv_freeaddrinfo(req.addrinfo);  // have to free it

    // !!! Theoretically this is where we'd know whether it's an IPv6 address
    // or an IPv4 address.  This is still transitional IPv4 code, though.

    return nullptr;
}


// This libuv callback is triggered when a Request_Connect_Socket()
// connection has been made...or an error is raised.  The port to send notice
// to about the connection is stored in the request data, and its AWAKE
// handler is invoked with a connect event.
//
// The callback should only be invoked during a WAIT, when the libuv event
// loop is being run.
//
static void on_connect(uv_connect_t *req, int status) {
    REBCTX *port_ctx = cast(REBCTX*, req->data);
    const REBVAL *port = CTX_ARCHETYPE(port_ctx);
    SOCKREQ *sock = Sock_Of_Port(port);

    REBVAL *awake = CTX_VAR(VAL_CONTEXT(port), STD_PORT_AWAKE);
    if (not IS_ACTION(awake)) {
        assert(IS_NULLED(awake));
        awake = nullptr;
    }

    REBVAL *event = nullptr;

    if (status < 0) {
        //
        // This gets called in the event loop, so if it wants to report an
        // error it has to do it somehow or another.  But given that a
        // coherent erroring strategy hasn't been articulated, raise it here.
        //
        // /*event = rebValue("make event! [type: 'error port:", port, "]");*/
        //
        fail (rebError_UV(status));
    }
    else {
        sock->stream = req->handle;

        Get_Local_IP(sock);

        event = rebValue("make event! [type: 'connect port:", port, "]");
    }

    if (awake) {
        if (rebDid(awake, rebR(event))) {
            rebElide(Lib(APPEND), "system.ports.system.data", port);
        }
    }

    FREE(uv_connect_t, req);
}


//
//  Request_Connect_Socket: C
//
// Connect a socket to a service.
// Only required for connection-based protocols (e.g. not UDP).
// The IP address must already be resolved before calling.
//
// This function is asynchronous. It will return immediately.
// You can call this function again to check the pending connection.
//
// Before usage:
//     Open_Socket() -- to allocate the socket
//
REBVAL *Request_Connect_Socket(const REBVAL *port)
{
    SOCKREQ *sock = Sock_Of_Port(port);
    assert(not (sock->modes & RST_LISTEN));

    struct sockaddr_in sa;

    assert(sock->fd != SOCKET_NONE);  // must be open

    if (sock->stream != nullptr)
        return nullptr;  // !!! R3-Alpha tolerated connected, should we?

    Set_Addr(&sa, sock->remote_ip, sock->remote_port_number);

    // This is an asynchronous request.  Because it is asynchronous, we
    // need to allocate memory for it.
    //
    uv_connect_t *req = TRY_ALLOC(uv_connect_t);
    req->data = VAL_CONTEXT(port);  // !!! keepalive as API handle?
    int result = uv_tcp_connect(
        req, &sock->tcp, cast(struct sockaddr *, &sa), on_connect
    );

    if (result < 0) {  // the *request* failed (didn't even try to connect)
        FREE(uv_connect_t, req);
        Close_Socket(port);
        return rebError_UV(result);
    }

    return nullptr;
}


//
// Accept an inbound connection on a TCP listen socket.
//
void on_new_connection(uv_stream_t *server, int status) {
    REBCTX *listener_port_ctx = cast(REBCTX*, server->data);
    const REBVAL *listening_port = CTX_ARCHETYPE(listener_port_ctx);
    SOCKREQ *listening_sock = Sock_Of_Port(listening_port);
    UNUSED(listening_sock);

    if (status < 0) {
        fprintf(stderr, "New connection error %s\n", uv_strerror(status));
        // error!
        return;
    }

    REBCTX *connection = Copy_Context_Shallow_Managed(listener_port_ctx);
    PUSH_GC_GUARD(connection);

    Init_Nulled(CTX_VAR(connection, STD_PORT_DATA));  // just to be sure

    REBVAL *c_state = CTX_VAR(connection, STD_PORT_STATE);
    REBBIN *bin = Make_Binary(sizeof(SOCKREQ));
    Init_Binary(c_state, bin);
    memset(BIN_HEAD(bin), 0, sizeof(SOCKREQ));
    TERM_BIN_LEN(bin, sizeof(SOCKREQ));

    SOCKREQ *sock_new = Sock_Of_Port(CTX_ARCHETYPE(connection));

    // Create a new port using ACCEPT

    uv_tcp_init(uv_default_loop(), &sock_new->tcp);
    sock_new->stream = cast(uv_stream_t*, &sock_new->tcp);

    int r = uv_accept(server, sock_new->stream);
    if (r < 0)
        fail (rebError_UV(r));

    // NOTE: REBOL stays in network byte order, no htonl(ip) needed
    //
    struct sockaddr_in sa;
    int len = sizeof(sa);
    uv_tcp_getpeername(&sock_new->tcp, cast(struct sockaddr *, &sa), &len);
    assert(len == sizeof(sa));
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
}


//
//  Start_Listening_On_Socket: C
//
// Setup a listening TCP socket.
//
// Before usage:
//     Open_Socket();
//     Set local_port to desired port number.
//
// Use this instead of Connect_Socket().
//
// !!! Historically this was common for TCP and UDP.  libuv separates the
// bind() command to operate on different types, there is a tcp_t vs. udp_t
// for the socket itself.
//
REBVAL *Start_Listening_On_Socket(const REBVAL *port)
{
    SOCKREQ *sock = Sock_Of_Port(port);
    sock->modes |= RST_LISTEN;

    assert(sock->fd != SOCKET_NONE);  // must be open

    // Setup socket address range and port:
    //
    struct sockaddr_in sa;
    Set_Addr(&sa, INADDR_ANY, sock->local_port_number);

  blockscope {
    int r = uv_tcp_bind(&sock->tcp, cast(struct sockaddr*, &sa), 0);
    if (r < 0)
        return rebError_UV(r);
  }

  blockscope {
    sock->tcp.data = VAL_CONTEXT(port);
    int r = uv_listen(
        cast(uv_stream_t*, &sock->tcp),
        DEFAULT_BACKLOG,
        on_new_connection
    );
    if (r < 0)
        return rebError_UV(r);
  }

    sock->modes |= RSM_BIND;

    Get_Local_IP(sock);

    Set_Port_Pending(port);

    rebElide(
        "insert system/ports/system make event! [",
            "type: 'connect",
            "port:", port,
        "]"
    );

    return nullptr;
}


// libuv actually enforces allocating a buffer on each read request, and it
// gives a suggested size which can be large (64k) in all cases, no matter
// how much you are asking to read.
//
// https://stackoverflow.com/questions/28511541/
//
// With memory pooling the cost of this can be basically nothing compared to
// the cost of network transfers, but beyond that point...it means that there
// is no interface for limiting the amount of data read besides limiting the
// size of the buffer.
//
// !!! In R3-Alpha, the client could leave data in the buffer of the port and
// just accumulate it, as in SYNC-OP from %prot-http.r:
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
// So for transitional compatibility with R3-Alpha ports, data is accrued in
// the `data` field of the port as a BINARY!.  This adds up over successive
// reads until the port clears it.
//
void on_read_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    UNUSED(suggested_size);

    Reb_Read_Request *rebreq = cast(Reb_Read_Request*, handle->data);

    REBCTX *port_ctx = rebreq->port_ctx;
    REBVAL *port_data = CTX_VAR(port_ctx, STD_PORT_DATA);

    size_t bufsize;
    if (rebreq->length == UNLIMITED)  // read maximum amount possible
        bufsize = NET_BUF_SIZE;  // !!! use libuv's (large) suggestion instead?
    else
        bufsize = rebreq->length - rebreq->actual;  // !!! use suggestion here?

    REBBIN *bin;
    if (IS_NULLED(port_data)) {
        bin = Make_Binary(bufsize);
        Init_Binary(port_data, bin);
    }
    else {
        bin = VAL_BINARY_KNOWN_MUTABLE(port_data);

        // !!! Port code doesn't skip the index, but what if user does?
        //
        assert(VAL_INDEX(port_data) == 0);

        // !!! Binaries need +1 space for the terminator, but that is handled
        // internally to Extend_Series.  Review wasted space in array case.
        //
        Extend_Series_If_Necessary(bin, bufsize);
    }

    buf->base = s_cast(BIN_TAIL(bin));
    buf->len = bufsize;

    // We are handing out a buffer of size buf->len
}


// stream-oriented libuv callback for reading.
//
// !!! The model of libuv's streaming is such that you cannot make another
// uv_read_start() request without calling uv_read_stop().  With the way
// R3-Alpha ports expected to work getting reads in chunks and then re-issuing
// READ requests if it wasn't enough, we either have to call stop on each
// AWAKE callback -or- make the definition of READ quite complex.  For now we
// stop and start, but the right answer is to expose an interface more attuned
// to how streaming actually works.
//
void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    Reb_Read_Request *rebreq = cast(Reb_Read_Request*, stream->data);
    REBCTX *port_ctx = rebreq->port_ctx;
    const REBVAL *port = CTX_ARCHETYPE(port_ctx);

    REBVAL *port_data = CTX_VAR(port_ctx, STD_PORT_DATA);

    REBBIN *bin;
    if (IS_NULLED(port_data)) {
        //
        // An error like "connection reset by peer" can occur before a call to
        // on_read_alloc() is made, so the buffer might be null in that case.
        // For safety's sake, assume this could also happen for 0 reads.
        //
        assert(nread <= 0);  // error or 0 read
        bin = nullptr;
    }
    else
        bin = VAL_BINARY_KNOWN_MUTABLE(port_data);

    REBVAL *awake = CTX_VAR(port_ctx, STD_PORT_AWAKE);
    if (not IS_ACTION(awake)) {
        assert(IS_NULLED(awake));
        awake = nullptr;
    }

    REBVAL *event = nullptr;

    if (nread == 0) {  // Zero bytes read
        //
        // Note: "nread might be 0, which does not indicate an error or EOF.
        // This is equivalent to EAGAIN or EWOULDBLOCK under read(2)."
        //
        // It seems like this means that the buffer you allocated would just
        // be tossed; but we're "allocating" buffers sequentially out of the
        // port's binary at the moment.  Do nothing?
    }
    else if (nread < 0) {  // Error while reading
        REBVAL *error = rebError_UV(nread);

        // !!! How to handle corrupted data?  Clear the whole buffer?  Leave
        // it at the termination before the READ?  Clear it for now just to
        // catch errors where partial data would be used as if it were okay,
        // but consider a defensive strategy that could use partial results.
        // Note that on_read_alloc() may not have been called at all, as in
        // the case of some "connection reset by peer" errors; so port_data
        // might be a binary or it might be nulled.
        //
        Init_Nulled(port_data);

        // Don't want to raise errors synchronously because we may be in the
        // event loop, e.g. `trap [write ...]` can't work if the writing
        // winds up happening outside the TRAP.  Let awake handler handle it.
        //
        rebElide("(", port, ").error:", rebR(error));

        if (awake)
            event = rebValue("make event! [type: 'error, port:", port, "]");

        // Asking to do a `uv_read_stop()` when an error happens asserts:
        // https://github.com/joyent/libuv/issues/1534

        FREE(Reb_Read_Request, rebreq);
        stream->data = nullptr;
    }
    else if (nread == UV_EOF) {
        //
        // Asking to do a `uv_read_stop()` when you've reached EOF asserts:
        // https://github.com/joyent/libuv/issues/1534

        if (rebreq->length == UNLIMITED) {
            //
            // -1 is the "read as much as you can" signal.  Reaching the end is
            // an acceptable outcome.
            //
            goto post_read_finished_event;
        }
        else {
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
            // data they actually got with a READ/PART call.  But this is
            // where you could handle that situation differently.

            // Note: cast of UNLIMITED to unsigned will be a very large value.
            //
            assert(rebreq->actual < cast(size_t, rebreq->length));

            goto post_read_finished_event;
        }
    }
    else {
        // Note that "each buffer is used only once", e.g. there is a call
        // to on_read_alloc() for every read.
        //
        assert(buf->base == s_cast(BIN_TAIL(bin)));

        rebreq->actual += nread;

        // Binaries must be kept with proper termination in case the GC sees
        // them.  This rule is maintained in case binaries alias UTF-8 strings,
        // which are stored terminated with 0.
        //
        TERM_BIN_LEN(bin, BIN_LEN(bin) + nread);

        if (rebreq->length == UNLIMITED) {
            //
            // Reading an unlimited amount of data, so keep going.
            //
            goto post_read_finished_event;
        }

        assert(rebreq->length >= 0);

        if (rebreq->actual == cast(size_t, rebreq->length)) {
            //
            // We've read as much as we wanted to, so ask to stop reading.
            //

          post_read_finished_event:

            // !!! See note at top of function on why we stop each AWAKE call.

            // RE: uv_read_stop() "This function will always succeed; hence,
            // checking its return value is unnecessary. A non-zero return
            // indicates that finishing releasing resources may be pending on
            // the next input event on that TTY on Windows, and does not
            // indicate failure."
            //
            uv_read_stop(stream);
            FREE(Reb_Read_Request, rebreq);
            stream->data = nullptr;

            if (awake)
                event = rebValue("make event! [type: 'read port:", port, "]");

            Clear_Port_Pending(port);
        }
        else {
            // Less than the total was reached while reading a limited amount.
            // Don't stop the stream or send an event, keep accruing data.
        }
    }

    if (event) {
        assert(awake);
        if (rebDid(awake, rebR(event)))
            rebElide(Lib(APPEND), "system.ports.system.data", port);
    }
}


// libuv callback when a write is finished.
//
void on_write_finished(uv_write_t *req, int status)
{
    Reb_Write_Request *rebreq = cast(Reb_Write_Request*, req);
    REBCTX *port_ctx = rebreq->port_ctx;
    const REBVAL *port = CTX_ARCHETYPE(port_ctx);

    REBVAL *awake = CTX_VAR(port_ctx, STD_PORT_AWAKE);
    if (not IS_ACTION(awake)) {
        assert(IS_NULLED(awake));
        awake = nullptr;
    }

    REBVAL *event = nullptr;

    if (status < 0) {
        REBVAL *error = rebError_UV(status);

        // Don't want to raise errors synchronously because we may be in the
        // event loop, e.g. `trap [write ...]` can't work if the writing
        // winds up happening outside the TRAP.  Let awake handler handle it.
        //
        rebElide("(", port, ").error:", rebR(error));

        if (awake)
            event = rebValue("make event! [type: 'error, port:", port, "]");
    }
    else {
        //
        // !!! We could more proactively free memory early for the GC here if
        // we wanted to, presuming we weren't reusing locked data.
        //
        rebRelease(rebreq->binary);

        if (awake)
            event = rebValue("make event! [type: 'wrote port:", port, "]");

        Clear_Port_Pending(CTX_ARCHETYPE(port_ctx));
    }

    if (event) {
        assert(awake);
        if (rebDid(awake, rebR(event))) {
            rebElide(Lib(APPEND), "system.ports.system.data", port);
        }
    }

    FREE(Reb_Write_Request, rebreq);
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
    if (transport == TRANSPORT_UDP)  // disabled for now
        fail ("https://forum.rebol.info/t/fringe-udp-support-archiving/1730");

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
        sock->stream = nullptr;

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

            // !!! R3-Alpha would open the socket using `socket()` call, and
            // then do a DNS lookup afterward if necessary.  But the right
            // way to do it is to look up the DNS first and find out what kind
            // of socket to create (e.g. IPv4 vs IPv6, for instance).

            bool listen;
            if (IS_TEXT(arg)) {
                listen = false;
                sock->remote_port_number =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 80;

                // Note: sets remote_ip field
                //
                REBVAL *lookup_error = Lookup_Socket_Synchronously(port, arg);
                if (lookup_error)
                    fail (lookup_error);
            }
            else if (IS_TUPLE(arg)) {  // Host IP specified:
                listen = false;
                sock->remote_port_number =
                    IS_INTEGER(port_id) ? VAL_INT32(port_id) : 80;

                Get_Tuple_Bytes(&sock->remote_ip, arg, 4);
            }
            else if (IS_BLANK(arg)) {  // No host, must be a LISTEN socket:
                listen = true;
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

            REBVAL *open_error = Open_Socket(port);
            if (open_error)
                fail (open_error);

            if (listen) {
                REBVAL *listen_error = Start_Listening_On_Socket(port);
                if (listen_error)
                    fail (listen_error);
            }

            RETURN (port); }

          case SYM_CLOSE:
            RETURN (port);

          default:
            fail (Error_On_Port(SYM_NOT_OPEN, port, -12));
        }
    }

  //=//// ACTIONS ON "OPEN" SOCKETS ////////////////////////////////////////=//

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
                (sock->modes & RSM_BIND) or (sock->stream != nullptr)
            );

          default:
            break;
        }

        break; }

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));

        if (REF(seek))
            fail (Error_Bad_Refines_Raw());

        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        if (sock->stream == nullptr and sock->transport != TRANSPORT_UDP)
            fail (Error_On_Port(SYM_NOT_CONNECTED, port, -15));

        Reb_Read_Request *rebreq = TRY_ALLOC(Reb_Read_Request);
        rebreq->port_ctx = VAL_CONTEXT(port);
        rebreq->actual = 0;

        if (REF(part)) {
            if (not IS_INTEGER(ARG(part)))
                fail (ARG(part));

            rebreq->length = VAL_INT32(ARG(part));
        }
        else {
            // !!! R3-Alpha didn't have a working READ/PART for networking; it
            // would just accrue data as each chunk came in.  The inability
            // to limit the read length meant it was difficult to implement
            // network protocols.  Ren-C has R3-Alpha's behavior if no /PART
            // is specified.
            //
            rebreq->length = UNLIMITED;  // -1, e.g. "read as much as you can"
        }

        // handle_t* passed to the on_read_alloc callback is the TCP handle
        //
        sock->tcp.data = rebreq;

        int result = uv_read_start(sock->stream, on_read_alloc, on_read);
        if (result < 0) {
            FREE(Reb_Read_Request, rebreq);
            fail (rebError_UV(result));
        }

        Set_Port_Pending(port);

        RETURN (port); }

      case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PAR(destination));

        if (REF(seek) or REF(append) or REF(lines))
            fail (Error_Bad_Refines_Raw());

        if (sock->stream == nullptr and sock->transport != TRANSPORT_UDP)
            fail (Error_On_Port(SYM_NOT_CONNECTED, port, -15));

        // !!! R3-Alpha did not lay out the invariants of the port model,
        // or what datatypes it would accept at what levels.  TEXT! could be
        // sent here--and it once could be wide characters or Latin1 without
        // the user having knowledge of which.  UTF-8 everywhere has resolved
        // that point (always UTF-8 bytes)...but the port model needs a top
        // to bottom review of what types are accepted where and why.
        //
        REBVAL *data = ARG(data);

        // When we get the callback we'll get the libuv req pointer, which is
        // the same pointer as the rebreq (first struct member).
        //
        Reb_Write_Request *rebreq = TRY_ALLOC(Reb_Write_Request);
        rebreq->port_ctx = VAL_CONTEXT(port);  // API handle for GC safety?

        // Make a copy of the BINARY! to put in the request, so that you can
        // say things like:
        //
        //     data: {abc}
        //     write port data
        //     reverse data
        //     write port data
        //
        // We don't want that to be nondeterministic and say {abccba} sometimes
        // and {cbacba} sometimes.  With multithreading it could be worse if
        // the reverse happened in mid-transfer.  :-/
        //
        // We also want to make sure the /PART is handled correctly, so by
        // delegating to COPY/PART we get that for free.
        //
        // !!! If you FREEZE the data then a copy is not necessary, review
        // this as an angle on efficiency.
        //
        rebreq->binary = rebValue(
            "as binary! copy/part", data, rebQ(REF(part))
        );
        rebUnmanage(rebreq->binary);  // otherwise would be seen as a leak

        uv_buf_t buf;
        buf.base = s_cast(m_cast(REBYTE*, VAL_BINARY_AT(rebreq->binary)));
        buf.len = VAL_LEN_AT(rebreq->binary);
        uv_write(&rebreq->req, sock->stream, &buf, 1, on_write_finished);

        Set_Port_Pending(port);

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
        // !!! There are bigger plans for a QUERY dialect (like PARSE).  This
        // old behavior of getting the IP addresses is for legacy only.

        REBVAL *result = rebValue(
            "copy ensure object! (@", port, ").scheme.info"
        );  // shallow copy

        REBCTX *info = VAL_CONTEXT(result);

        Init_Tuple_Bytes(
            CTX_VAR(info, STD_NET_INFO_LOCAL_IP),
            cast(REBYTE*, &sock->local_ip),
            4
        );
        Init_Integer(
            CTX_VAR(info, STD_NET_INFO_LOCAL_PORT),
            sock->local_port_number
        );

        Init_Tuple_Bytes(
            CTX_VAR(info, STD_NET_INFO_REMOTE_IP),
            cast(REBYTE*, &sock->remote_ip),
            4
        );
        Init_Integer(
            CTX_VAR(info, STD_NET_INFO_REMOTE_PORT),
            sock->remote_port_number
        );

        return result; }

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
        REBVAL *error = Request_Connect_Socket(port);
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
//  Dev_Net_Poll: c
//
bool Dev_Net_Poll(void)
{
    bool changed = false;  // we don't actually know from return result :-/

    // !!! If we use UV_RUN_ONCE here, it will block if there are no callbacks.
    // For now that's not a good idea because the timeout mechanism isn't
    // based on libuv timers.  But even if it were, the Ctrl-C hooks that
    // interrupt the evaluator don't interrupt this.  Use UV_RUN_NOWAIT.
    //
    int loop_result = uv_run(uv_default_loop(), UV_RUN_NOWAIT);
    UNUSED(loop_result);

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
//
// Intialize networking libraries and related interfaces.  This needs to be
// called prior to any socket functions.
//
// !!! Note the DNS extension currently relies on this startup being called
// instead of doing its own.
{
    NETWORK_INCLUDE_PARAMS_OF_STARTUP_P;

  #if TO_WINDOWS
    //
    // LibUV calls WSAStartup with MAKEWORD(2, 2) on demand.  Which means that
    // we don't have to on the first startup.  But it never calls WSACleanup(),
    // so we do in SHUTDOWN*.
    //
    // In order to get the number of WSAStartup and WSAShutdown calls to match,
    // we thus need to call startup every time but the first!
    //
    static bool first_startup = true;
    if (first_startup) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData))
            rebFail_OS (WSAGetLastError());
        first_startup = false;
    }
  #endif

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

    OS_Unregister_Device(Dev_Net);

  #if TO_WINDOWS
    WSACleanup();  // have to call as libuv does not
  #endif

    return rebNone();
}
