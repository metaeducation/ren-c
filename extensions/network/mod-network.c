//
//  file: %mod-network.c
//  summary: "network port interface"
//  section: ports
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=/////////////////////////////////////////////////////////////////////////=//
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
//=/////////////////////////////////////////////////////////////////////////=//
//
// This is a TCP networking interface, evolved from the R3-Alpha PORT! code.
// It has been rewritten in terms of libuv as a platform abstraction layer.
// Although libuv has the implementation capability to do non-blocking and
// parallel network operations, the goal in Ren-C is to push the language
// toward allowing people to express their code in a synchronous fashion and
// have the asynchronous behavior accomplished by more "modern" means...taking
// inspiration from Go's goroutines and async/await:
//
//   https://forum.rebol.info/t/1733
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * Although some libuv APIs (such as the filesystem) allow you to pass in
//   nullptr for the callback and get synchronous behavior, the network APIs
//   don't seem to do that.  Hence the synchronous behavior is achieved here
//   by having operations like READ or WRITE call the event loop until they
//   notice they have completed.
//

// libuv's %include/uv/win.h has an "inconsistent annotation" that MSVC's
// static analyzer complains about.
//
#if defined(_MSC_VER) && defined(_PREFAST_)  // _PREFAST_ if MSVC /analyze
    #pragma warning(disable : 28251)  // suppress "inconsistent annotation"
#endif

#if defined(_MSC_VER)
    #pragma warning(disable : 4668)  // allow #if of undefined things
#endif
#include "uv.h"  // includes windows.h, with bad #ifs in <winioctl.h>
#if defined(_MSC_VER)
    #pragma warning(error : 4668)   // disallow #if of undefined things
#endif

#ifdef TO_WINDOWS
    #undef OUT  // %minwindef.h defines this, we have a better use for it
    #undef VOID  // %winnt.h defines this, we have a better use for it
#endif


#include "sys-core.h"
#include "tmp-mod-network.h"

#include "tmp-paramlists.h"  // !!! for INCLUDE_PARAMS_OF_OPEN, etc.

#include "reb-net.h"
extern Value* rebError_UV(int err);


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


uv_timer_t wait_timer;

void wait_timer_callback(uv_timer_t* handle) {
    assert(handle->data != nullptr);
    handle->data = nullptr;
}


uv_timer_t halt_poll_timer;

void halt_poll_timer_callback(uv_timer_t* handle) {
    //
    // Doesn't actually do anything, just breaks the UV_RUN_ONCE loop
    // every half a second.  Theoretically we could do this with uv_signal_t
    // for SIGINT and a callback like:
    //
    //     void signal_callback(uv_signal_t* handle, int signum) {
    //         Set_Trampoline_Flag(HALT);
    //     }
    //
    // But that seems to only work on Linux and not Windows.
    //
    UNUSED(handle);
}


//
//  Trap_Open_Socket: C
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
Value* Trap_Open_Socket(const Value* port)
{
    SOCKREQ *sock = Sock_Of_Port(port);
    assert(sock->stream == nullptr);

    sock->modes = 0;  // clear all flags

    assert(sock->transport == TRANSPORT_TCP);  // different UDP libuv functions

    int r = uv_tcp_init_ex(uv_default_loop(), &sock->tcp, AF_INET);
    if (r < 0)
        return rebError_UV(r);

    sock->stream = cast(uv_stream_t*, &sock->tcp);  // signal tcp is set

    return nullptr;
}


void on_close(uv_handle_t *handle) {
    bool *finished = cast(bool*, handle->data);
    *finished = true;
}

static void Close_Sock_If_Needed(SOCKREQ* sock) {
    if (sock->stream) {
        bool finished;
        sock->tcp.data = &finished;
        uv_close(cast(uv_handle_t*, &sock->tcp), on_close);

        do {
            uv_run(uv_default_loop(), UV_RUN_ONCE);
        } while (not finished);

        sock->stream = nullptr;
        sock->modes = 0;
    }
}

static void Sockreq_Handle_Cleaner(void* p, size_t length) {
    SOCKREQ* sock = cast(SOCKREQ*, p);
    UNUSED(length);

    Close_Sock_If_Needed(sock);
    Free_Memory(SOCKREQ, sock);
}


//
//  Close_Socket: C
//
Value* Close_Socket(const Value* port)
{
    SOCKREQ* sock = Sock_Of_Port(port);

    Value* error = nullptr;

    // Note: R3-Alpha allowed closing closed sockets
    Close_Sock_If_Needed(sock);

    return error;
}


// This libuv callback is triggered when a Trap_Connect_Socket_Else_Close()
// connection has been made...or a panic happens.
//
// The callback will only be invoked when the libuv event loop is being run.
//
static void on_connect(uv_connect_t *req, int status) {
    Reb_Connect_Request *rebreq = cast(Reb_Connect_Request*, req);
    const Value* port = Varlist_Archetype(rebreq->port_ctx);
    SOCKREQ *sock = Sock_Of_Port(port);

    if (status < 0) {
        rebreq->result = rebError_UV(status);
    }
    else {
        sock->stream = req->handle;

        Get_Local_IP(sock);
        rebreq->result = rebSpace();
    }
}


//
//  Trap_Connect_Socket_Else_Close: C
//
// Helper function that attempts to connect a socket to a specific IP address
// with an optional timeout.
//
// Closing the socket seems to be the only *effective* way to synchronously
// cancel the connection attempt if it is still pending.  If we tried to use
// uv_cancel() and then wait, it will wait too long.  If we don't wait, then
// that leaves pending on_connect() callbacks hanging...which creates issues
// in the current design.
//
// Returns:
//   nullptr on success
//   error Value* on failure
//   timeout Value* if timeout_ms > 0 and connection times out
//
static Value* Trap_Connect_Socket_Else_Close(
    const Value* port,
    Option(uint32_t) timeout_ms  // 0 = no timeout
){
    SOCKREQ *sock = Sock_Of_Port(port);
    assert(not (sock->modes & RST_LISTEN));

    struct sockaddr_in sa;
    Set_Addr(&sa, sock->remote_ip, sock->remote_port_number);

    // !!! For some reason the on_connect() callback cannot be passed as
    // nullptr to get a synchronous connection.
    //
    Reb_Connect_Request *rebreq = rebAlloc(Reb_Connect_Request);
    rebreq->port_ctx = Cell_Varlist(port);
    rebreq->result = nullptr;

    int r = uv_tcp_connect(
        &rebreq->req, &sock->tcp, cast(struct sockaddr *, &sa), on_connect
    );

    if (r < 0) {  // the *request* failed (didn't even try to connect)
        rebFree(rebreq);
        Close_Sock_If_Needed(sock);
        return rebError_UV(r);
    }

    // Set up timeout if requested
    uv_timer_t connect_timer;
    uv_timer_t* timer_ptr = nullptr;
    if (timeout_ms) {
        timer_ptr = &connect_timer;
        uv_timer_init(uv_default_loop(), timer_ptr);
        timer_ptr->data = timer_ptr;  // will be nulled on timeout
        uv_timer_start(timer_ptr, wait_timer_callback, opt timeout_ms, 0);
    }

    // Wait for either connection or timeout
    while (rebreq->result == nullptr &&
           (timer_ptr == nullptr || timer_ptr->data != nullptr)) {
        uv_run(uv_default_loop(), UV_RUN_ONCE);
    }

    // Clean up timer if used
    if (timer_ptr != nullptr) {
        uv_timer_stop(timer_ptr);
        uv_close(cast(uv_handle_t*, timer_ptr), nullptr);
        uv_run(uv_default_loop(), UV_RUN_NOWAIT);  // cleanup timer
    }

    Value* result;
    if (rebreq->result != nullptr) {  // on_connect() ran (success or failure)
        result = Is_Space(rebreq->result) ? nullptr : rebreq->result;
        if (result == nullptr)
            rebRelease(rebreq->result);  // success case
        else {
            possibly(sock->stream != nullptr);  // !!! is this true?
            Close_Sock_If_Needed(sock);  // connection failed
        }
    }
    else {  // timeout occurred - close socket to force callback completion
        uv_close(cast(uv_handle_t*, &sock->tcp), nullptr);

        while (rebreq->result == nullptr)  // must wait for on_connect()
            uv_run(uv_default_loop(), UV_RUN_NOWAIT);

        if (Is_Space(rebreq->result)) {  // connect succeded before close
            rebRelease(rebreq->result);
            // Don't call Close_Sock_If_Needed() - uv_close() already in flight
            // !!! without a uv_run() this may leave dangling socket, how can
            // we fix that?
        }
        else {
            // Connection failed (expected case when close beats connect)
            assert(Is_Warning(rebreq->result));
            rebRelease(rebreq->result);  // ignore the error, we're timing out
        }

        sock->stream = nullptr;
        sock->modes = 0;

        result = rebValue("make warning! -[Connection timeout]-");
    }

    rebFree(rebreq);
    return result;
}


//
//  Trap_Open_And_Connect_Socket_For_Hostname: C
//
// Combines DNS lookup with connection attempts, trying multiple IP addresses
// with timeouts.
//
// For each IP address returned by DNS:
//
//  * Create a fresh socket
//  * Attempt connection with 2.5 second timeout
//  * If it fails, try the next IP address
//  * If all IPs fail, return error
//
// This consolidated function replaces the old three-step process (lookup,
// open, connect).  It has to be done in a unified manner, because it's simply
// essential in modern networking to try multiple addresses, or the code will
// fail to connect to many services that have multiple IPs.  At one point
// example.com transitioned to being one such service, causing the old code to
// fail to connect about half the time to the arbitrary address chosen during
// a separate open().  Hence it had to be changed to this approach folding
// together lookup, open, and connect.
//
// 1. R3-Alpha would use asynchronous DNS API on Windows.  But that API
//    was not supported by IPv6, and developers are encouraged to use normal
//    socket APIs with their own threads.  Now that we use libuv, there is
//    again the ability to specify a callback and do asynchronous lookup.  But
//    this would have to be fit in with a client understanding for how to
//    request a LOOKUP event, and when it had to be waited on.  For now it's
//    synchronous.
//
Value* Trap_Open_And_Connect_Socket_For_Hostname(  // synchronous [1]
    const Value* port,
    const Value* hostname
){
    SOCKREQ *sock = Sock_Of_Port(port);

    assert(Is_Text(hostname));
    const char *hostname_utf8 = s_cast(Cell_Utf8_At(hostname));
    char *port_number_utf8 = rebSpell(
        CANON(TO), CANON(TEXT_X), rebI(sock->remote_port_number)
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
    Mem_Fill(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;  // IPv4 addresses only for now
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    // This is a replacement for:
    //
    //     HOSTENT *host = gethostbyname(s_cast(hostname_utf8));
    //
    // gethostbyname() is long since deprecated: it's IPv4 only, assumes DNS
    // resolves to one name you can trust to answer you instead of trying
    // multiple, and isn't thread safe either.
    //
    uv_getaddrinfo_t req;
    int r = uv_getaddrinfo(
        uv_default_loop(),
        &req,
        nullptr,  // callback
        hostname_utf8,  // called "node" in libuv, but "hostname" in POSIX
        port_number_utf8,  // "service" string or port (e.g. "echo", "80")
        &hints  // "hints" (which is a const struct addrinfo*)
    );

    rebFree(port_number_utf8);

    if (r != 0)
        return rebError_UV(r);

    struct addrinfo *addr_info = req.addrinfo;  // DNS may give many addresses

    // This assert used to check ai_addrlen for an IPv4 address was 16, but on
    // HaikuOS it was 32.  Changed assert, still works.  :-/
    //
    // https://stackoverflow.com/q/31343855/
    //
    assert(req.addrinfo->ai_addrlen >= 16);

    for (struct addrinfo *ai = addr_info; ai != nullptr; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET)
            continue;  // Skip non-IPv4 addresses

        struct sockaddr_in *sa = cast(struct sockaddr_in*, ai->ai_addr);

        memcpy(&sock->remote_ip, &sa->sin_addr, 4);  // store for QUERY

        Value* open_error = Trap_Open_Socket(port);  // sock for this attempt
        if (open_error) {
            rebRelease(open_error);
            continue;  // try next IP
        }

        Option(uint32_t) timeout_ms = 2500;  // 2.5 seconds
        Value* connect_error = Trap_Connect_Socket_Else_Close(port, timeout_ms);
        if (connect_error == nullptr) {  // success!
            uv_freeaddrinfo(addr_info);
            return nullptr;
        }

        rebRelease(connect_error);  // connection failed for this IP, try next
    }

    uv_freeaddrinfo(addr_info);

    // All IPs failed, return a generic connection failure message
    return rebValue("make warning! -[Connection failed to all IP addresses]-");
}


//
// Accept an inbound connection on a TCP listen socket.
//
void on_new_connection(uv_stream_t *server, int status) {
    VarList* listener_port_ctx = cast(VarList*, server->data);
    const Value* listening_port = Varlist_Archetype(listener_port_ctx);
    SOCKREQ *listening_sock = Sock_Of_Port(listening_port);
    UNUSED(listening_sock);

    // !!! This connection can happen at any time the libUV event loop runs.
    // So this error has a chance of being raised during unrelated READ or
    // WRITE calls.  How should such errors be delivered?
    //
    if (status < 0)
        panic (rebError_UV(status));

    VarList* client = Copy_Varlist_Shallow_Managed(listener_port_ctx);
    Push_Lifeguard(client);

    Init_Nulled(
        Slot_Init_Hack(Varlist_Slot(client, STD_PORT_DATA))  // "be sure" (?)
    );

    Value* c_state = Slot_Hack(Varlist_Slot(client, STD_PORT_STATE));
    require (
      SOCKREQ* sock = Alloc_On_Heap(SOCKREQ)
    );
    memset(sock, 0, sizeof(SOCKREQ));

    Init_Handle_Cdata_Managed(
        c_state, sock, sizeof(SOCKREQ), &Sockreq_Handle_Cleaner
    );

    SOCKREQ *sock_new = Sock_Of_Port(Varlist_Archetype(client));

    // Create a new port using ACCEPT

    uv_tcp_init(uv_default_loop(), &sock_new->tcp);
    sock_new->stream = cast(uv_stream_t*, &sock_new->tcp);

    int r = uv_accept(server, sock_new->stream);
    if (r < 0)
        panic (rebError_UV(r));  // !!! See note on FAIL above about errors here

    // NOTE: REBOL stays in network byte order, no htonl(ip) needed
    //
    struct sockaddr_in sa;
    int len = sizeof(sa);
    uv_tcp_getpeername(&sock_new->tcp, cast(struct sockaddr *, &sa), &len);
    assert(len == sizeof(sa));
    sock_new->remote_ip = sa.sin_addr.s_addr;
    sock_new->remote_port_number = ntohs(sa.sin_port);

    Get_Local_IP(sock_new);

    Drop_Lifeguard(client);

    rebElide("(", listening_port, ").spec/accept", Varlist_Archetype(client));
}


//
//  Start_Listening_On_Socket: C
//
// Setup a listening TCP socket.
//
// Before usage:
//     Trap_Open_Socket();
//     Set local_port to desired port number.
//
// Use this instead of Connect_Socket().
//
// !!! Historically this was common for TCP and UDP.  libuv separates the
// bind() command to operate on different types, there is a tcp_t vs. udp_t
// for the socket itself.
//
Value* Start_Listening_On_Socket(const Value* port)
{
    SOCKREQ *sock = Sock_Of_Port(port);
    sock->modes |= RST_LISTEN;

    assert(sock->stream != nullptr);  // must be open

  setup_address_range_and_port: {

    struct sockaddr_in sa;
    Set_Addr(&sa, INADDR_ANY, sock->local_port_number);

  bind_socket: {

    int r = uv_tcp_bind(&sock->tcp, cast(struct sockaddr*, &sa), 0);
    if (r < 0)
        return rebError_UV(r);

} listen_on_socket: {

    sock->tcp.data = Cell_Varlist(port);
    int r = uv_listen(
        cast(uv_stream_t*, &sock->tcp),
        DEFAULT_BACKLOG,
        on_new_connection
    );
    if (r < 0)
        return rebError_UV(r);

    sock->modes |= RSM_BIND;

    Get_Local_IP(sock);

    return nullptr;
}}}


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
//     until [find [ready close] state.state] [
//         if not port? wait [state.connection port.spec.timeout] [
//             panic make-http-error "Timeout"
//         ]
//         if state.state = 'reading-data [
//             read state.connection
//         ]
//     ]
//
// So for transitional compatibility with R3-Alpha ports, data is accrued in
// the `data` field of the port as a BLOB!.  This adds up over successive
// reads until the port clears it.
//
void on_read_alloc(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf)
{
    UNUSED(suggested_size);

    Reb_Read_Request *rebreq = cast(Reb_Read_Request*, handle->data);

    VarList* port_ctx = rebreq->port_ctx;
    Value* port_data = Slot_Hack(Varlist_Slot(port_ctx, STD_PORT_DATA));

    Size bufsize;
    if (rebreq->length == UNLIMITED)  // read maximum amount possible
        bufsize = NET_BUF_SIZE;  // !!! use libuv's (large) suggestion instead?
    else
        bufsize = *(unwrap rebreq->length) - rebreq->actual;  // suggestion?

    Binary* bin;
    if (Is_Nulled(port_data)) {
        bin = Make_Binary(bufsize);
        Init_Blob(port_data, bin);
    }
    else {
        bin = Cell_Binary_Known_Mutable(port_data);

        // !!! Port code doesn't skip the index, but what if user does?
        //
        assert(Series_Index(port_data) == 0);

        // !!! Binaries need +1 space for the terminator, but that is handled
        // internally to Extend_Flex.  Review wasted space in array case.
        //
        require (
          Extend_Flex_If_Necessary_But_Dont_Change_Used(bin, bufsize)
        );
    }

    buf->base = s_cast(Binary_Tail(bin));
    buf->len = bufsize;

    // We are handing out a buffer of size buf->len
}


// on_read(): stream-oriented libuv callback for reading.
//
// Note that "each buffer is used only once", e.g. there is a call to
// on_read_alloc() for every read.
//
// 1. An error like "connection reset by peer" can occur before a call to
//    on_read_alloc() is made, so the buffer might be null in that case.
//    For safety's sake, assume this could also happen for 0 reads.
//
// 2. Asking to do a `uv_read_stop()` when on an error or EOF asserts:
//
//      https://github.com/joyent/libuv/issues/1534
//
// 3. Binary Blobs must be kept with proper termination in case the GC sees
//    them.  This rule is maintained in case Blobs alias UTF-8 Strings, which
//    are stored terminated with 0.
//
// 4. The model of libuv's streaming is such that you cannot make another
//    uv_read_start() request without calling uv_read_stop().  For now we stop
//    and start, but the right answer is to expose an interface more attuned
//    to how streaming actually works.
//
//    RE: uv_read_stop() "This function will always succeed; hence, checking
//    its return value is unnecessary. A non-zero return indicates that
//    finishing releasing resources may be pending on the next input event on
//    that TTY on Windows, and does not indicate failure."
//
void on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
{
    Reb_Read_Request* rebreq = cast(Reb_Read_Request*, stream->data);
    VarList* port_ctx = rebreq->port_ctx;

    Value* port_data = Slot_Hack(Varlist_Slot(port_ctx, STD_PORT_DATA));

    if (Is_Nulled(port_data))
        assert(nread <= 0);  // can happen, e.g. "connection reset by peer" [1]
    else
        assert(Is_Blob(port_data));

    if (nread == 0) {
        //
        // Note: "nread might be 0, which does not indicate an error or EOF.
        // This is equivalent to EAGAIN or EWOULDBLOCK under read(2)."
        //
        // It seems like this means that the buffer you allocated would just
        // be tossed; but we're "allocating" buffers sequentially out of the
        // port's binary at the moment.  Do nothing?
    }
    else if (nread < 0) {  // Error while reading
        //
        // !!! How to handle corrupted data?  Clear the whole buffer?  Leave
        // it at the termination before the READ?  Clear it for now just to
        // catch errors where partial data would be used as if it were okay,

        Init_Nulled(port_data);  // already null if no on_read_alloc() [1]

        /* uv_read_stop(stream); */  // asserts when error or EOF [2]
        stream->data = nullptr;
        rebreq->result = rebError_UV(nread);
    }
    else if (nread == UV_EOF) {
        /* uv_read_stop(stream); */  // asserts when error or EOF [2]

        if (rebreq->length == UNLIMITED)  // "read as much as you can" hit eof
            goto post_read_finished_event;

        // If we had a :PART setting on the READ, follow the Rebol convention
        // of allowing less to be accepted, which FILE! does as well:
        //
        //     rebol2>> write %test.dat #{01}
        //
        //     rebol2>> read:part %test.dat 100000
        //     == #{01}
        //
        // Under this rule, it is the caller's responsibility to check how much
        // data they actually got with a READ:PART call.  But this is where you
        // could handle that situation differently.

        assert(rebreq->actual < *(unwrap rebreq->length));
        goto post_read_finished_event;
    }
    else {
        Binary* bin = Cell_Binary_Known_Mutable(port_data);
        assert(buf->base == s_cast(Binary_Tail(bin)));
        UNUSED(buf);

        rebreq->actual += nread;
        Term_Binary_Len(bin, Binary_Len(bin) + nread);  // GC needs term [3]

        if (rebreq->length == UNLIMITED)
            goto post_read_finished_event;  // unlimited, so just keep going

        assert(*(unwrap rebreq->length) >= 0);

        if (
            rebreq->actual
            == *(unwrap rebreq->length)  // we read as much as we wanted to
        ){
            goto post_read_finished_event;
        }

        // Less than the total was reached while reading a limited amount.
        // Don't stop the stream or send an event, keep accruing data.
    }

    return;

  post_read_finished_event: { ////////////////////////////////////////////////

    uv_read_stop(stream);  // stop on each read, no result to check [4]
    stream->data = nullptr;

    rebreq->result = rebSpace();
}}


// libuv callback when a write is finished.
//
void on_write_finished(uv_write_t *req, int status)
{
    Reb_Write_Request *rebreq = cast(Reb_Write_Request*, req);

    if (status < 0) {
        rebreq->result = rebError_UV(status);
    }
    else {
        rebreq->result = rebSpace();
    }

    // !!! We could more proactively free memory early for the GC here if
    // we wanted to, presuming we weren't reusing locked data.
    //
    rebRelease(rebreq->binary);
}


// Shared code between UDP and TCP handling
//
static Bounce Transport_Actor(Level* level_, enum Transport_Type transport) {
    Value* port = ARG_N(1);
    const Symbol* verb = Level_Verb(LEVEL);

    if (transport == TRANSPORT_UDP)  // disabled for now
        panic (
            "https://forum.rebol.info/t/fringe-udp-support-archiving/1730"
        );

    VarList* ctx = Cell_Varlist(port);
    Value* spec = Slot_Hack(Varlist_Slot(ctx, STD_PORT_SPEC));

    // If a transfer is in progress, the port_data is a BLOB!.  Its index
    // represents how much of the transfer has finished.  The data starts
    // as NULL (from `make-port*`) and R3-Alpha would reset it after a
    // transfer was finished.  For writes, R3-Alpha held a copy of the value
    // being written...and text was allowed (even though it might be wide
    // characters, a likely oversight from the addition of unicode).
    //
    Value* port_data = Slot_Hack(Varlist_Slot(ctx, STD_PORT_DATA));
    assert(Is_Blob(port_data) or Is_Nulled(port_data));

    SOCKREQ *sock;
    Value* state = Slot_Hack(Varlist_Slot(ctx, STD_PORT_STATE));
    if (Is_Handle(state)) {
        sock = Sock_Of_Port(port);
        assert(sock->transport == transport);
    }
    else {
        // !!! The Make_Devreq() code would zero out the struct, so to keep
        // things compatible while ripping out the devreq code this must too.
        //
        assert(Is_Nulled(state));
        require (
          sock = Alloc_On_Heap(SOCKREQ)
        );
        memset(sock, 0, sizeof(SOCKREQ));
        Init_Handle_Cdata_Managed(
            state,
            sock,
            sizeof(SOCKREQ),
            &Sockreq_Handle_Cleaner
        );
        sock->transport = transport;
        sock->stream = nullptr;

        // !!! There is no way to customize the timeout.  Where should this
        // setting be configured?
    }

    if (sock->stream == nullptr) {  // Actions for an unopened socket
        switch (opt Symbol_Id(verb)) {
          case SYM_OPEN_Q:
            return Init_False(OUT);

          case SYM_OPEN: {
            Value* arg = Slot_Hack(Obj_Slot(spec, STD_PORT_SPEC_NET_HOST));
            Value* port_id = Slot_Hack(
                Obj_Slot(spec, STD_PORT_SPEC_NET_PORT_ID)
            );

            // OPEN needs to know to bind() the socket to a local port before
            // the first sendto() is called, if the user is particular about
            // what the port ID of originating messages is.  So local_port
            // must be set before the OS_Do_Device() call.
            //
            Value* local_id = Slot_Hack(
                Obj_Slot(spec, STD_PORT_SPEC_NET_LOCAL_ID)
            );
            if (Is_Nulled(local_id))
                sock->local_port_number = 0;  // let the system pick
            else if (Is_Integer(local_id))
                sock->local_port_number = VAL_INT32(local_id);
            else
                panic (
                    "local-id field of PORT! spec must be NULL or INTEGER!"
                );


            if (Is_Nulled(arg)) {  // No host, must be a LISTEN socket:
                sock->local_port_number =
                    Is_Integer(port_id) ? VAL_INT32(port_id) : 8000;

                Value* open_error = Trap_Open_Socket(port);
                if (open_error)
                    panic (open_error);

                Value* listen_error = Start_Listening_On_Socket(port);
                if (listen_error)
                    panic (listen_error);

                return COPY(port);
            }

            // !!! R3-Alpha would open the socket using `socket()` call, and
            // then do a DNS lookup afterward if necessary.  But the right
            // way to do it is to look up the DNS first and find out what kind
            // of socket to create (e.g. IPv4 vs IPv6, for instance).

            if (Is_Text(arg)) {
                sock->remote_port_number =
                    Is_Integer(port_id) ? VAL_INT32(port_id) : 80;

                // Combined DNS lookup + connect with retry across multiple IPs
                Value* connect_error = Trap_Open_And_Connect_Socket_For_Hostname(
                    port, arg
                );
                if (connect_error)
                    panic (connect_error);
            }
            else if (Is_Tuple(arg)) {  // Host IP specified:
                sock->remote_port_number =
                    Is_Integer(port_id) ? VAL_INT32(port_id) : 80;

                Get_Tuple_Bytes(&sock->remote_ip, arg, 4);

                // For direct IP, still need to open socket and connect
                Value* open_error = Trap_Open_Socket(port);
                if (open_error)
                    panic (open_error);

                // UDP is connectionless so it will not add to the connectors.
                if (sock->transport != TRANSPORT_UDP) {
                    Option(uint32_t) timeout_ms = 0;  // no timeout
                    Value* errval = Trap_Connect_Socket_Else_Close(port, timeout_ms);
                    if (errval != nullptr)
                        panic (errval);
                }
            }
            else
                panic (Error_On_Port(SYM_INVALID_SPEC, port, -10));

            return COPY(port); }

          case SYM_CLOSE:
            return COPY(port);

          default:
            panic (Error_On_Port(SYM_NOT_OPEN, port, -12));
        }
    }

  //=//// ACTIONS ON "OPEN" SOCKETS ////////////////////////////////////////=//

    switch (opt Symbol_Id(verb)) { // Ordered by frequency
      case SYM_LENGTH_OF: {
        return Init_Integer(
            OUT,
            Is_Blob(port_data) ? Series_Len_Head(port_data) : 0
        ); }

      case SYM_OPEN_Q:
        //
        // Connect for clients, bind for servers:
        //
        return Init_Logic(
            OUT,
            (sock->modes & RSM_BIND) or (sock->stream != nullptr)
        );

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PARAM(SOURCE));

        if (Bool_ARG(SEEK))
            panic (Error_Bad_Refines_Raw());

        UNUSED(PARAM(STRING)); // handled in dispatcher
        UNUSED(PARAM(LINES)); // handled in dispatcher

        if (sock->stream == nullptr and sock->transport != TRANSPORT_UDP)
            panic (Error_On_Port(SYM_NOT_CONNECTED, port, -15));

        Reb_Read_Request *rebreq = rebAlloc(Reb_Read_Request);
        rebreq->port_ctx = Cell_Varlist(port);
        rebreq->actual = 0;
        rebreq->result = nullptr;

        if (Bool_ARG(PART)) {
            if (not Is_Integer(ARG(PART)))
                panic (PARAM(PART));

            rebreq->length_store = VAL_INT32(ARG(PART));
            rebreq->length = &rebreq->length_store;
        }
        else {
            // !!! R3-Alpha didn't have a working READ:PART for networking; it
            // would just accrue data as each chunk came in.  The inability
            // to limit the read length meant it was difficult to implement
            // network protocols.  Ren-C has R3-Alpha's behavior if no :PART
            // is specified.
            //
            rebreq->length = UNLIMITED;  // -1, e.g. "read as much as you can"
        }

        // handle_t* passed to the on_read_alloc callback is the TCP handle
        //
        sock->tcp.data = rebreq;

        int r = uv_read_start(sock->stream, on_read_alloc, on_read);
        if (r < 0)
            return fail (rebError_UV(r));  // e.g. "broken pipe" ?

        do {
            uv_run(uv_default_loop(), UV_RUN_ONCE);
        } while (rebreq->result == nullptr);

        if (not Is_Space(rebreq->result))
            return fail (rebreq->result);  // e.g. "broken pipe" ?
        rebRelease(rebreq->result);

        rebFree(rebreq);

        return COPY(port); }

      case SYM_WRITE: {
        INCLUDE_PARAMS_OF_WRITE;

        UNUSED(PARAM(DESTINATION));

        if (Bool_ARG(SEEK) or Bool_ARG(APPEND) or Bool_ARG(LINES))
            panic (Error_Bad_Refines_Raw());

        if (sock->stream == nullptr and sock->transport != TRANSPORT_UDP)
            panic (Error_On_Port(SYM_NOT_CONNECTED, port, -15));

        // !!! R3-Alpha did not lay out the invariants of the port model,
        // or what datatypes it would accept at what levels.  TEXT! could be
        // sent here--and it once could be wide characters or Latin1 without
        // the user having knowledge of which.  UTF-8 everywhere has resolved
        // that point (always UTF-8 bytes)...but the port model needs a top
        // to bottom review of what types are accepted where and why.
        //
        Value* data = ARG(DATA);

        // When we get the callback we'll get the libuv req pointer, which is
        // the same pointer as the rebreq (first struct member).
        //
        Reb_Write_Request *rebreq = rebAlloc(Reb_Write_Request);
        rebreq->port_ctx = Cell_Varlist(port);  // API handle for GC safety?
        rebreq->result = nullptr;

        // Make a copy of the BLOB! to put in the request, so that you can
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
        // We also want to make sure the :PART is handled correctly, so by
        // delegating to COPY:PART we get that for free.
        //
        // !!! If you FREEZE the data then a copy is not necessary, review
        // this as an angle on efficiency.
        //
        rebreq->binary = rebValue(
            "as blob! copy:part", data, rebQ(ARG(PART))
        );
        rebUnmanage(rebreq->binary);  // otherwise would be seen as a leak

        uv_buf_t buf;
        buf.base = s_cast(Blob_At_Ensure_Mutable(rebreq->binary));
        buf.len = Series_Len_At(rebreq->binary);
        int r = uv_write(&rebreq->req, sock->stream, &buf, 1, on_write_finished);
        if (r < 0)
            return fail (rebError_UV(r));  // e.g. "broken pipe" ?

        do {
            uv_run(uv_default_loop(), UV_RUN_ONCE);
        } while (rebreq->result == nullptr);

        if (not Is_Space(rebreq->result))
            return fail (rebreq->result);  // e.g. "broken pipe" ?
        rebRelease(rebreq->result);

        rebFree(rebreq);

        return COPY(port); }

      case SYM_QUERY: {
        //
        // !!! There are bigger plans for a QUERY dialect (like PARSE).  This
        // old behavior of getting the IP addresses is for legacy only.

        Value* result = rebValue(
            "copy ensure object! (@", port, ").scheme.info"
        );  // shallow copy

        VarList* info = Cell_Varlist(result);

        require (
          Init_Tuple_Bytes(
            Slot_Init_Hack(Varlist_Slot(info, STD_NET_INFO_LOCAL_IP)),
            cast(Byte*, &sock->local_ip),
            4
        ));
        Init_Integer(
            Slot_Init_Hack(Varlist_Slot(info, STD_NET_INFO_LOCAL_PORT)),
            sock->local_port_number
        );

        require (
          Init_Tuple_Bytes(
            Slot_Init_Hack(Varlist_Slot(info, STD_NET_INFO_REMOTE_IP)),
            cast(Byte*, &sock->remote_ip),
            4
        ));
        Init_Integer(
            Slot_Init_Hack(Varlist_Slot(info, STD_NET_INFO_REMOTE_PORT)),
            sock->remote_port_number
        );

        return result; }

      case SYM_CLOSE: {
        if (sock->stream) {  // allows close of closed socket (?)
            Value* errval = Close_Socket(port);
            if (errval)
                panic (errval);
        }
        return COPY(port); }

      default:
        break;
    }

    panic (UNHANDLED);
}


//
//  export tcp-actor: native [
//
//  "Handler for OLDGENERIC dispatch on TCP PORT!s"
//
//      return: [any-stable?]
//  ]
//
DECLARE_NATIVE(TCP_ACTOR)
{
    return Transport_Actor(level_, TRANSPORT_TCP);
}


//
//  export udp-actor: native [
//
//  "Handler for OLDGENERIC dispatch on UDP PORT!s"
//
//      return: [any-stable?]
//  ]
//
DECLARE_NATIVE(UDP_ACTOR)
{
    return Transport_Actor(level_, TRANSPORT_UDP);
}


//
//  startup*: native [
//
//  "Initialize Network Extension (e.g. call WSAStartup() on Windows)"
//
//      return: []
//  ]
//
DECLARE_NATIVE(STARTUP_P)
//
// Intialize networking libraries and related interfaces.  This needs to be
// called prior to any socket functions.
//
// !!! Note the DNS extension currently relies on this startup being called
// instead of doing its own.
{
    INCLUDE_PARAMS_OF_STARTUP_P;

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
            rebPanic_OS (WSAGetLastError());
        first_startup = false;
    }
  #endif

    uv_timer_init(uv_default_loop(), &wait_timer);
    uv_timer_init(uv_default_loop(), &halt_poll_timer);

    return "~";
}


//
//  shutdown*: native [
//
//  "Shutdown Network Extension"
//
//      return: []
//  ]
//
DECLARE_NATIVE(SHUTDOWN_P)
//
// 1. uv_close() on a timer is just a request, you have to actually run the
//    event loop to have it get freed and finalized.  If you don't run the
//    loop and try to do uv_loop_close() you'll get a UV_EBUSY and the loop
//    will not be freed.
//
// 2. uv_default_loop() allocates the default loop on its first usage.  It
//    must be freed like anything else, or you get a memory leak.
//
//      https://github.com/libuv/libuv/issues/140
{
    INCLUDE_PARAMS_OF_SHUTDOWN_P;

  #if TO_WINDOWS
    WSACleanup();  // have to call as libuv does not
  #endif

    // Stop timers before closing them to avoid "resource busy" errors
    uv_timer_stop(&wait_timer);
    uv_timer_stop(&halt_poll_timer);

    uv_close(cast(uv_handle_t*, &wait_timer), nullptr);  // no callback
    uv_close(cast(uv_handle_t*, &halt_poll_timer), nullptr);

    uv_run(uv_default_loop(), UV_RUN_DEFAULT);  // wait for timers to close [1]

    int result = uv_loop_close(uv_default_loop());  // else valgrind leak [2]
    if (result != 0)
        panic (rebError_UV(result));

    return "~";
}


//
//  export wait*: native [
//
//  "Waits for a duration, port, or both"
//
//      return: "NULL if timeout, PORT! that awoke or BLOCK! of ports if /ALL"
//          [null? port! block!]
//      value [<opt> any-number? time! port! block!]
//  ]
//
DECLARE_NATIVE(WAIT_P)  // See wrapping function WAIT in usermode code
//
// WAIT* expects a BLOCK! argument to have been pre-reduced; this means it
// does not have to implement the reducing process "stacklessly" itself.  The
// stackless nature comes for free by virtue of REDUCE-ing in usermode.
{
    INCLUDE_PARAMS_OF_WAIT_P;

    REBLEN timeout = 0;  // in milliseconds
    Value* ports = nullptr;

    Option(const Element*) val;
    if (not Is_Block(ARG(VALUE)))
        val = Is_Nulled(ARG(VALUE)) ? nullptr : Element_ARG(VALUE);
    else {
        ports = ARG(VALUE);

        REBLEN num_pending = 0;
        const Element* tail;
        const Element* at = List_At(&tail, ports);
        for (; at != tail; ++at) {  // find timeout
            if (Is_Port(at))
                ++num_pending;

            if (Is_Integer(at) or Is_Decimal(at) or Is_Time(at))
                break;
        }
        if (at == tail) {
            if (num_pending == 0)
                return nullptr; // has no pending ports!
            val = nullptr;  // no timeout provided
        }
        else {
            val = at;
        }
    }

    if (not val) {
        timeout = ALL_BITS; // wait for all windows
    }
    else switch (opt Type_Of(unwrap val)) {
      case TYPE_INTEGER:
      case TYPE_DECIMAL:
      case TYPE_TIME:
        timeout = Milliseconds_From_Value(unwrap val);
        break;

      case TYPE_PORT: {
        Source* single = Make_Source(1);
        require (
          Sink(Element) cell = Alloc_Tail_Array(single)
        );
        Copy_Cell(cell, unwrap val);
        Init_Block(ARG(VALUE), single);
        ports = ARG(VALUE);

        timeout = ALL_BITS;
        break; }

      default:
        panic (Error_Bad_Value(unwrap val));
    }

    const uint64_t repeat_ms = 0;  // do not repeat the timer

    if (timeout != ALL_BITS) {
        wait_timer.data = &wait_timer;  // nulled by callback if timeout
        uv_timer_start(&wait_timer, &wait_timer_callback, timeout, repeat_ms);
    }

    // !!! See halt_poll_timer_callback() on why not uv_signal_t for SIGINT
    //
    uv_timer_start(&halt_poll_timer, &halt_poll_timer_callback, 500, 500);

    // Let any pending device I/O have a chance to run.  UV_RUN_ONCE means it
    // will block until *something* happens (could be the timer timing out,
    // or could be something like an incoming network connection being made).
    //
    while (
        (timeout == ALL_BITS or wait_timer.data != nullptr)
        and not Get_Trampoline_Flag(HALT)
    ){
        int callbacks_left = uv_run(uv_default_loop(), UV_RUN_ONCE);
        UNUSED(callbacks_left);
    }

    uv_timer_stop(&halt_poll_timer);

    if (timeout != ALL_BITS) {
        uv_timer_stop(&wait_timer);
    }

    if (Get_Trampoline_Flag(HALT)) {
        Clear_Trampoline_Flag(HALT);

        Init_Thrown_With_Label(LEVEL, LIB(NULL), LIB(HALT));
        return BOUNCE_THROWN;
    }

    if (Get_Trampoline_Flag(DEBUG_BREAK)) {
        Clear_Trampoline_Flag(DEBUG_BREAK);

        // !!! If implemented, this would allow triggering a breakpoint
        // with a keypress.  This needs to be thought out a bit more,
        // but may not involve much more than running `BREAKPOINT`.
        //
        panic (
            "BREAKPOINT from TRAMPOLINE_FLAG_DEBUG_BREAK unimplemented"
        );
    }

    return nullptr;
}
