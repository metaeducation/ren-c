//
//  File: %mod-zeromq.c
//  Summary: "Interface from REBOL3 to ZeroMQ"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2011 Andreas Bolka <a AT bolka DOT at>
// Copyright 2018 Rebol Open Source Contributors
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
// The ZeroMQ REBOL 3 extension uses the ØMQ library, the use of which is
// granted under the terms of the GNU Lesser General Public License (LGPL),
// Version 3.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This extension was written in 2011 for R3-Alpha, against the "COMMAND!"
// interface.  It is low-level in nature, and does not use the higher level
// C interface to ZeroMQ, which is C code layered above <zmq.h>:
//
// http://czmq.zeromq.org/
//
// (Whether it would be better to use czmq depends on whether one wants one's
// "middleware" to have a lot of Rebol machinery in it or not.)
//
// The 2011 code was built against 0MQ version 2.  For 2018 and beyond, it is
// currently assumed that clients will not be interested in less than v4, so
// it has been updated for those options and APIs.
//

// !!! Rebol wants to not need printf/fprintf/putc, so by default the release
// builds will "corrupt" them so they can't be used.  But ZeroMQ includes
// <stdio.h> directly, so that flag must be overridden for this extension.
// Also, it includes winsock2.h on Windows, which means including windows.h,
// that overrides Rebol's IS_ERROR().  (This won't be an issue when the
// extension is changed to use libRebol() only, and not the internal API.)
//
#define DEBUG_STDIO_OK
#include <zmq.h>
#ifdef TO_WINDOWS
    #undef IS_ERROR
#endif

#define REBOL_IMPLICIT_END

#include "sys-core.h"
#include "sys-ext.h"

#include "tmp-mod-zeromq-first.h"



// The standard pattern for ZeroMQ to fail is to return a result code that is
// nonzero and then set zmq_errno().  In the long-term strategy of Rebol
// errors, this should be giving them IDs/URLs, but just report strings ATM.
//
ATTRIBUTE_NO_RETURN static void fail_ZeroMQ(void) {
    int errnum = zmq_errno();
    const char *errmsg = zmq_strerror(errnum);
    rebJumps("FAIL", rebT(errmsg));
}


//
//  zmq-init: native/export [ ;; >= 0MQ 2.0.7
//
//  {Initialise 0MQ context}
//
//      return: [handle!]
//      io-threads [integer!]
//  ]
//
REBNATIVE(zmq_init) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_INIT;

    int io_threads = rebUnboxInteger(ARG(io_threads));
    void *ctx = zmq_init(io_threads);
    if (not ctx)
        fail_ZeroMQ();

    return rebHandle(ctx, 0, nullptr); // !!! add cleanup;
}


//
//  zmq-term: native/export [
//
//  {Terminate 0MQ context}
//
//      return: <void>
//      ctx [handle!]
//  ]
//
REBNATIVE(zmq_term) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_TERM;

    void *ctx = VAL_HANDLE_VOID_POINTER(ARG(ctx));

    int rc = zmq_term(ctx);
    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


//
//  zmq-msg-alloc: native/export [
//
//  {Allocate memory for a 0MQ message object}
//
//      return: [handle!]
//  ]
//
REBNATIVE(zmq_msg_alloc) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_MSG_ALLOC;

    // !!! Currently can't use rebAlloc() since this has indefinite lifetime
    //
    zmq_msg_t *msg = cast(zmq_msg_t*, malloc(sizeof(zmq_msg_t)));
    if (not msg)
        rebJumps("FAIL {Insufficient memory for zmq_msg_t}");

    return rebHandle(msg, 0, nullptr); // !!! add cleanup;
}


//
//  zmq-msg-free: native/export [
//
//  {Free the memory previously allocated for a 0MQ message object}
//
//      return: <void>
//      msg [handle!]
//  ]
//
REBNATIVE(zmq_msg_free) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_MSG_FREE;

    zmq_msg_t *msg = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg));
    free(msg);
    return rebVoid();
}


//
//  zmq-msg-init: native/export [
//
//  {Initialise empty 0MQ message}
//
//      return: <void>
//      msg [handle!]
//  ]
//
REBNATIVE(zmq_msg_init) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_MSG_INIT;

    zmq_msg_t *msg = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg));

    int rc = zmq_msg_init(msg);
    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


//
//  zmq-msg-init-size: native/export [
//
//  {Initialise 0MQ message of a specified size}
//
//      return: <void>
//      msg [handle!]
//      size [integer!]
//  ]
//
REBNATIVE(zmq_msg_init_size) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_MSG_INIT_SIZE;

    zmq_msg_t *msg = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg));
    size_t msg_size = rebUnboxInteger(ARG(size));

    int rc = zmq_msg_init_size(msg, msg_size);
    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


void free_msg_data(void *data, void *hint) {
    UNUSED(hint);
    free(data);
}

//
//  zmq-msg-init-data: native/export [
//
//  {Initialise 0MQ message with (a copy of) supplied data}
//
//      return: <void>
//      msg [handle!]
//      data [binary!]
//  ]
//
REBNATIVE(zmq_msg_init_data) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_MSG_INIT_DATA;

    zmq_msg_t *msg = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg));

    size_t msg_size = rebBytesInto(nullptr, 0, ARG(data)); // query size
    REBYTE *msg_data = cast(REBYTE*, malloc(msg_size + 1));
    if (not msg_data)
        rebJumps("FAIL {Insufficient memory for msg_data}");

    size_t check_size = rebBytesInto(msg_data, msg_size, ARG(data));
    assert(check_size == msg_size);
    UNUSED(check_size);

    int rc = zmq_msg_init_data(
        msg,
        msg_data,
        msg_size,
        &free_msg_data, // callback to free the message
        nullptr // "hint" passed to freeing function
    );
    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


//
//  zmq-msg-close: native/export [
//
//  {Release 0MQ message}
//
//      return: <void>
//      msg [handle!]
//  ]
//
REBNATIVE(zmq_msg_close) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_MSG_CLOSE;

    zmq_msg_t *msg = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg));

    int rc = zmq_msg_close(msg);
    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


//
//  zmq-msg-data: native/export [
//
//  {Retrieve a copy of a message's content as a BINARY!}
//
//      return: [binary!]
//      msg [handle!]
//  ]
//
REBNATIVE(zmq_msg_data) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_MSG_DATA;

    zmq_msg_t *msg = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg));

    size_t msg_size = zmq_msg_size(msg);
    void *msg_data = zmq_msg_data(msg);

    return rebBinary(msg_data, msg_size);
}


//
//  zmq-msg-size: native/export [
//
//  {Retrieve message content size in bytes}
//
//      return: [integer!]
//      msg [handle!]
//  ]
//
REBNATIVE(zmq_msg_size) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_MSG_SIZE;

    zmq_msg_t *msg = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg));

    size_t msg_size = zmq_msg_size(msg); // "no errors are defined"

    return rebInteger(msg_size);
}


//
//  zmq-msg-copy: native/export [
//
//  {Copy content of a message to another message}
//
//      return: <void>
//      msg-dest [handle!]
//      msg-src [handle!]
//  ]
//
REBNATIVE(zmq_msg_copy) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_MSG_COPY;

    zmq_msg_t *msg_dest = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg_dest));
    zmq_msg_t *msg_src = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg_src));

    int rc = zmq_msg_copy(msg_dest, msg_src);
    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


//
//  zmq-msg-move: native/export [
//
//  {Move content of a message to another message}
//
//      return: <void>
//      msg-dest [handle!]
//      msg-src [handle!]
//  ]
//
REBNATIVE(zmq_msg_move) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_MSG_MOVE;

    zmq_msg_t *msg_dest = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg_dest));
    zmq_msg_t *msg_src = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg_src));

    int rc = zmq_msg_move(msg_dest, msg_src);
    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


//
//  zmq-socket: native/export [
//
//  {Create 0MQ socket}
//
//      return: [handle!] {0MQ Socket}
//      ctx [handle!]
//      type [integer! word!]
//          "REQ, REP, DEALER, ROUTER, PUB, SUB, PUSH, PULL, PAIR"
//  ]
//
REBNATIVE(zmq_socket) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_SOCKET;

    void *ctx = VAL_HANDLE_VOID_POINTER(ARG(ctx));

    int type = rebUnbox( // !!! GROUP! needed for MATCH quirk
        "(match integer!", rebUneval(ARG(type)), ") or [select make map! ["
            "REQ", rebI(ZMQ_REQ),
            "REP", rebI(ZMQ_REP),
            "DEALER", rebI(ZMQ_DEALER), // >= 0MQ 2.1, was XREQ prior to that
            "ROUTER", rebI(ZMQ_ROUTER), // >= 0MQ 2.1, was XREP prior to that
            "PUB", rebI(ZMQ_PUB),
            "SUB", rebI(ZMQ_SUB),
            "PUSH", rebI(ZMQ_PUSH),
            "PULL", rebI(ZMQ_PULL),
            "PAIR", rebI(ZMQ_PAIR),
        "]", rebUneval(ARG(type)), "] or [",
            "fail [{Unknown zmq_socket() type:}", rebUneval(ARG(type)), "]",
        "]");

    void *socket = zmq_socket(ctx, type);
    if (not socket)
        fail_ZeroMQ();

    return rebHandle(socket, 0, nullptr); // !!! add cleanup
}


//
//  zmq-close: native/export [
//
//  {Close 0MQ socket}
//
//      return: <void>
//      socket [handle!]
//  ]
//
REBNATIVE(zmq_close) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_CLOSE;

    void *socket = VAL_HANDLE_VOID_POINTER(ARG(socket));

    int rc = zmq_close(socket);
    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


// !!! Could cache this at startup, and ideally the list would be available to
// give to the user somehow.  But putting them in a Rebol file would mean
// manually hardcoding the constants, vs getting them from the headers and
// exporting them during startup.
//
// Proper research could get this table right for arbitrary ZeroMQ versions
// via detecting the ZMQ_VERSION_MAJOR and ZMQ_VERSION_MINOR values.  As a
// first shot, this list is typed in from what was in the 4.1 list, minus
// deprecated options:
//
// http://api.zeromq.org/4-1:zmq-setsockopt
//
// Ones that were in the list but didn't have the ZMQ_XXX constant defined
// are commented out.  Someone sufficiently motivated can figure out every
// #ifdef (and even add support for versions older than 3) if they like.
//
static REBVAL *Make_Sockopts_Table(void) {
    //
    // !!! A block is used instead of a MAP! just to cut down on brackets.
    // REDUCE is used so the type words become datatypes.
    // ISSUE!s used instead of LIT-WORD!s for visibility.

    REBVAL *table = rebRun("reduce [",
        "#AFFINITY integer!", rebI(ZMQ_AFFINITY),
        "#BACKLOG integer!", rebI(ZMQ_BACKLOG),
      /*
        "CONNECT_RID binary!", rebI(ZMQ_CONNECT_RID), // ZMQ_CONNECT_ROUTING_ID
      */
      /*
        "#GSSAPI_PLAINTEXT logic!", rebI(ZMQ_GSSAPI_PLAINTEXT),
        "#GSSAPI_PRINCIPAL text!", rebI(ZMQ_GSSAPI_PRINCIPAL),
        "#GSSAPI_SERVER logic!", rebI(ZMQ_GSSAPI_SERVER),
        "#GSSAPI_SERVICE_PRINCIPAL logic!", rebI(ZMQ_GSSAPI_SERVICE_PRINCIPAL),
        "#HANDSHAKE_IVL integer!", rebI(ZMQ_HANDSHAKE_IVL), // msec
      */
        "#IDENTITY binary!", rebI(ZMQ_IDENTITY), // !!! ZMQ_ROUTING_ID
        "#LINGER integer!", rebI(ZMQ_LINGER), // msec
        "#MAXMSGSIZE integer!", rebI(ZMQ_MAXMSGSIZE), // bytes
        "#MULTICAST_HOPS integer!", rebI(ZMQ_MULTICAST_HOPS), // hops
        "#RATE integer!", rebI(ZMQ_RATE), // Kbits/sec
        "#RCVBUF integer!", rebI(ZMQ_RCVBUF), // bytes
        "#RCVHWM integer!", rebI(ZMQ_RCVHWM), // messages
        "#RCVTIMEO integer!", rebI(ZMQ_RCVTIMEO), // msec
        "#RECONNECT_IVL integer!", rebI(ZMQ_RECONNECT_IVL), // msec
        "#RECONNECT_IVL_MAX integer!", rebI(ZMQ_RECONNECT_IVL_MAX), // msec
        "#RECOVERY_IVL integer!", rebI(ZMQ_RECOVERY_IVL), // msec
      /*
        "#ROUTER_HANDOVER logic!", rebI(ZMQ_ROUTER_HANDOVER),
      */
        "#ROUTER_MANDATORY logic!", rebI(ZMQ_ROUTER_MANDATORY),
        "#SNDBUF integer!", rebI(ZMQ_SNDBUF), // bytes
        "#SNDHWM integer!", rebI(ZMQ_SNDHWM), // messages
        "#SNDTIMEO integer!", rebI(ZMQ_SNDTIMEO), // msec
        "#SUBSCRIBE binary!", rebI(ZMQ_SUBSCRIBE),
        "#TCP_KEEPALIVE integer!", rebI(ZMQ_TCP_KEEPALIVE), // -1, 0, 1
        "#TCP_KEEPALIVE_CNT integer!", rebI(ZMQ_TCP_KEEPALIVE_CNT), // -1,>0
      /*
        "#TCP_KEEPALIVE_IDLE integer!", rebI(TCP_KEEPALIVE_IDLE), // -1,>0
      */
        "#TCP_KEEPALIVE_INTVL integer!", rebI(ZMQ_TCP_KEEPALIVE_INTVL), // >-2
      /*
        "#TOS integer!", rebI(ZMQ_TOS), // >0
      */
        "#UNSUBSCRIBE binary!", rebI(ZMQ_UNSUBSCRIBE),
        "#XPUB_VERBOSE logic!", rebI(ZMQ_XPUB_VERBOSE),
    "]");

  #ifdef ZMQ_HAVE_CURVE // can't put #ifdef inside rebXXX() macros
    rebElide("append", table, "reduce [",
       "#CURVE_PUBLICKEY binary!", rebI(ZMQ_CURVE_PUBLICKEY),
       "#CURVE_SECRETKEY binary!", rebI(ZMQ_CURVE_SECRETKEY),
       "#CURVE_SERVERKEY binary!", rebI(ZMQ_CURVE_SERVERKEY),
       "#CURVE_SERVER logic!", rebI(ZMQ_CURVE_SERVER),
    "]");
  #endif

  #if ZMQ_VERSION_MAJOR >= 4 // can't put #ifdef inside rebXXX() macros
    rebElide("append", table, "reduce [",
        "#CONFLATE logic!", rebI(ZMQ_CONFLATE),
        "#IMMEDIATE logic!", rebI(ZMQ_IMMEDIATE),
        "#IPV6 logic!", rebI(ZMQ_IPV6),
        "#PLAIN_PASSWORD text!", rebI(ZMQ_PLAIN_PASSWORD),
        "#PLAIN_SERVER logic!", rebI(ZMQ_PLAIN_SERVER),
        "#PLAIN_USERNAME text!", rebI(ZMQ_PLAIN_USERNAME),
        "#PROBE_ROUTER logic!", rebI(ZMQ_PROBE_ROUTER),
        "#REQ_CORRELATE logic!", rebI(ZMQ_REQ_CORRELATE),
        "#REQ_RELAXED logic!", rebI(ZMQ_REQ_RELAXED),
        "#ROUTER_RAW logic!", rebI(ZMQ_ROUTER_RAW),
        "#ZAP_DOMAIN text!", rebI(ZMQ_ZAP_DOMAIN),
    "]");
  #endif

    return table;
}


//
//  zmq-setsockopt: native/export [
//
//  {Set 0MQ socket options}
//
//      return: <void>
//      socket [handle!]
//      name [word! integer!]
//          "see http://api.zeromq.org/4-1:zmq-setsockopt"
//      value [binary! integer! text! logic!]
//          "if INTEGER!, option should be of type '[u]int64_t'"
//  ]
//
REBNATIVE(zmq_setsockopt) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_SETSOCKOPT;

    void *socket = VAL_HANDLE_VOID_POINTER(ARG(socket));

    int name;
    if (rebDid("integer?", ARG(name))) {
        name = rebUnboxInteger(ARG(name)); // take their word for it :-/
    }
    else {
        REBVAL *opts = Make_Sockopts_Table(); // !!! should cache on startup

        REBVAL *pos = rebRun(
            "find", opts, "as issue!", ARG(name), "or [",
                "fail [{Couldn't find option constant for}", ARG(name), "]",
            "]");

        // !!! Is it overzealous to disallow integer arguments that are 0 or 1
        // to a "boolean" parameter, forcing people to use LOGIC?
        //
        name = rebUnboxInteger(
            "if type of", ARG(value), "<> ensure datatype! second", pos, "[",
                "fail [", ARG(name), "{needs to be} an (second", pos, ")]",
            "]",
            "third", pos);

        rebRelease(pos);
        rebRelease(opts);
    }

    int rc;
    if (rebDid("match [binary! text!]", ARG(value))) {
        size_t value_size;
        REBYTE *value_data = rebBytes(&value_size, ARG(value));

        rc = zmq_setsockopt(socket, name, value_data, value_size);

        rebFree(value_data);
    }
    else {
        int64_t value = rebUnbox(
            "ensure [logic! integer!]", ARG(value), rebEND
        );

        rc = zmq_setsockopt(socket, name, &value, sizeof(value));
    }

    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


//
//  zmq-getsockopt: native/export [
//
//  {Get 0MQ socket options}
//
//      return: [logic! binary! text! integer!]
//      socket [handle!]
//      name "see http://api.zeromq.org/4-1:zmq-getsockopt"
//          [word! integer!]
//      /type "If name is an INTEGER!, specify the return type"
//      datatype [datatype!]
//  ]
//
REBNATIVE(zmq_getsockopt) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_GETSOCKOPT;

    void *socket = VAL_HANDLE_VOID_POINTER(ARG(socket));

    int name;
    REBVAL *datatype;
    if (rebDid("integer?", ARG(name))) {
        name = rebUnboxInteger(ARG(name)); // take their word for it :-/
        if (not REF(type))
            rebJumps("FAIL {INTEGER! name use requires /TYPE specification}");
        datatype = ARG(datatype);
    }
    else {
        if (REF(type))
            rebJumps("FAIL {Can't override /TYPE unless INTEGER! name used}");

        REBVAL *opts = Make_Sockopts_Table(); // !!! should cache on startup

        REBVAL *pos = rebRun(
            "find", opts, "as issue!", ARG(name), "or [",
                "fail [{Couldn't find option constant for}", ARG(name), "]",
            "]");

        datatype = rebRun("ensure datatype! second", pos);
        name = rebUnbox("ensure integer! third", pos);

        rebRelease(pos);
        rebRelease(opts);
    }

    REBVAL *result;
    if (rebDid("find reduce [logic! integer!]", datatype)) {
        int64_t value_data;
        size_t value_size = sizeof(value_data);
        int rc = zmq_getsockopt(socket, name, &value_data, &value_size);
        if (rc != 0)
            fail_ZeroMQ();

        if (rebDid(datatype, "== logic!")) {
            if (value_data != 0 and value_data != 1)
                rebJumps("FAIL {LOGIC! property didn't return a 1 or 0}");
            result = rebLogic(value_data);
        }
        else
            result = rebInteger(value_data);
    }
    else {
        // According to ZeroMQ developers, no option should be larger than
        // 256 bytes: https://github.com/zeromq/libzmq/issues/3160
        //
        char value_data[257];
        size_t value_size = 256;

        int rc = zmq_getsockopt(socket, name, value_data, &value_size);
        if (rc != 0)
            fail_ZeroMQ();

        value_data[value_size] = '\0';
        if (rebDid(datatype, "== text!"))
            result = rebText(value_data);
        else {
            rebElide("assert [", datatype, "== binary!]");
            result = rebBinary(value_data, value_size);
        }
    }

    if (not REF(type))
        rebRelease(datatype); // need to release if from a rebRun() call...

    return result;
}


//
//  zmq-bind: native/export [
//
//  {Accept connections on a socket}
//
//      return: <void>
//      socket [handle!]
//      endpoint [text! url!]
//  ]
//
REBNATIVE(zmq_bind) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_BIND;

    void *socket = VAL_HANDLE_VOID_POINTER(ARG(socket));
    char *str = rebSpell(ARG(endpoint));

    int rc = zmq_bind(socket, str);
    rebFree(str);

    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


//
//  zmq-connect: native/export [
//
//  {Connect a socket}
//
//      return: <void>
//      socket [handle!]
//      endpoint [text! url!]
//  ]
//
REBNATIVE(zmq_connect) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_CONNECT;

    void *socket = VAL_HANDLE_VOID_POINTER(ARG(socket));
    char *str = rebSpell(ARG(endpoint));

    int rc = zmq_connect(socket, str);
    rebFree(str);

    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


//
//  zmq-send: native/export [
//
//  {Send a message on a socket}
//
//      return: [integer! word!]
//          "Number of bytes in the message or [EINTR EAGAIN]"
//      socket [handle!]
//      msg [handle!]
//      /dontwait "In DEALER and PUSH use non-blocking mode, may give EAGAIN"
//      /sndmore "Message is multi-part, and more sends will be coming"
//  ]
//
REBNATIVE(zmq_send) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_SEND;

    void *socket = VAL_HANDLE_VOID_POINTER(ARG(socket));
    zmq_msg_t *msg = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg));
    int flags = 0;
    if (REF(dontwait))
        flags |= ZMQ_NOBLOCK;
    if (REF(sndmore))
        flags |= ZMQ_SNDMORE;

    int rc = zmq_msg_send(msg, socket, flags);
    if (rc != -1)
        return rebInteger(rc); // number of bytes in the message

    int errnum = zmq_errno();
    if (errnum == EINTR)
        return rebRun("'EINTR");
    if (errnum == EAGAIN)
        return rebRun("'EAGAIN");

    fail_ZeroMQ();
}


//
//  zmq-recv: native/export [
//
//  {Receive a message from a socket}
//
//      return: [integer! word!]
//          "Number of bytes received, or [EINTR EAGAIN]"
//      socket [handle!]
//      msg [handle!]
//      /dontwait "Nonblocking mode, gives EAGAIN if no messages available"
//  ]
//
REBNATIVE(zmq_recv) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_RECV;

    void *socket = VAL_HANDLE_VOID_POINTER(ARG(socket));
    zmq_msg_t *msg = VAL_HANDLE_POINTER(zmq_msg_t, ARG(msg));
    int flags = 0;
    if (REF(dontwait))
        flags |= ZMQ_DONTWAIT;

    int rc = zmq_msg_recv(msg, socket, flags);
    if (rc != -1)
        return rebInteger(rc); // number of bytes received

    int errnum = zmq_errno();
    if (errnum == EINTR)
        return rebRun("'EINTR");
    if (errnum == EAGAIN)
        return rebRun("'EAGAIN");

    fail_ZeroMQ();
}


//
//  zmq-poll: native/export [
//
//  {Input/output multiplexing}
//
//      return: [block!] "Filtered poll-spec with ready events"
//      poll-spec "[socket1 events1 socket2 events2 ...]"
//          [block!]
//      timeout [integer!] "Timeout in microseconds"
//  ]
//
REBNATIVE(zmq_poll)
//
// !!! This is an attempted "libRebol"-style rewrite of the code from the
// original extension.  However, there were no examples of the code being
// used, so it hasn't been tested.  Exported constants said:
//
//      pollin 1
//      pollout 2
//      ;pollerr 4 ;; not for 0MQ sockets (& we can't use standard sockets)
{
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_POLL;

    REBVAL *spec = ARG(poll_spec);
    long timeout = rebUnboxInteger(ARG(timeout));

    int spec_length = rebUnbox("length of", spec);

    if (spec_length % 2 != 0)
        rebJumps("FAIL {Invalid poll-spec: length}");
    int nitems = spec_length / 2;

    // Prepare pollitem_t array by mapping a pair of REBOL handle!/integer!
    // values to one zmq_pollitem_t.  (rebAlloc automatically frees on fail)

    zmq_pollitem_t *pollitems = rebAllocN(zmq_pollitem_t, nitems);

    int i;
    for (i = 0; i < nitems; ++i) {
        REBVAL *socket = rebRun( // !!! GROUP! needed for MATCH quirk
            "(match handle! pick", spec, rebI(i * 2), ") else [",
                "fail {Expected HANDLE! in spec position}",
            "]");
        pollitems[i].socket = VAL_HANDLE_VOID_POINTER(socket);

        pollitems[i].events = rebUnbox( // !!! GROUP! needed for MATCH quirk
            "(match integer! pick", spec, rebI(i * 2 + 1), ") else [",
                "fail {Expected INTEGER! in spec position}",
            "]");
    }

    int nready = zmq_poll(pollitems, nitems, timeout);
    if (nready == -1)
        rebJumps("FAIL {zmq_poll() returned -1 (TBD: report errno)}");

    // Create results block of the same form as the items block, but filter
    // out all 0MQ socket handle!s (& their events integer!) for which no
    // event is ready.

    REBVAL *result = rebRun("make block!", rebI(nready * 2));

    int check_nready = 0;
    for (i = 0; i < nitems; ++i) {
        if (pollitems[i].revents == 0)
            continue;

        rebElide(
            "append", result, "pick", spec, rebI(i * 2),
            "append", result, "pick", spec, rebI(i * 2 + 1)
        );
        ++check_nready;
    }
    assert(nready == check_nready);
    UNUSED(check_nready);

    return result;
}


//
//  zmq-proxy: native/export [
//
//  {Start built-in 0MQ proxy in the current application thread}
//
//      return: <void>
//      frontend [handle!] {Socket handle}
//      backend [handle!] {Socket handle}
//      /capture
//      capturer [handle!] {Socket handle}
//  ]
//
REBNATIVE(zmq_proxy) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_PROXY;

    void *frontend_socket = VAL_HANDLE_VOID_POINTER(ARG(frontend));
    void *backend_socket = VAL_HANDLE_VOID_POINTER(ARG(backend));

    void *capture_socket;
    if (REF(capture))
        capture_socket = VAL_HANDLE_VOID_POINTER(ARG(capturer));
    else
        capture_socket = nullptr;

    int rc = zmq_proxy(frontend_socket, backend_socket, capture_socket);
    if (rc != 0)
        fail_ZeroMQ();

    return rebVoid();
}


//
//  zmq-version: native/export [
//
//  {Report 0MQ library version}
//
//      return: [tuple!]
//  ]
//
REBNATIVE(zmq_version) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_VERSION;

    int major;
    int minor;
    int patch;
    zmq_version(&major, &minor, &patch);

    return rebRun(
        "make tuple! [", rebI(major), rebI(minor), rebI(patch), "]"
    );;
}


//
//  zmq-equal?: native/export [
//
//  {Returns TRUE if two 0MQ handle! values are equal (Workaround Bug #1868)}
//
//      return: [logic!]
//      value1 [handle!]
//      value2 [handle!]
//  ]
//
REBNATIVE(zmq_equal_q) {
    ZEROMQ_INCLUDE_PARAMS_OF_ZMQ_EQUAL_Q;

    void *h1 = VAL_HANDLE_VOID_POINTER(ARG(value1));
    void *h2 = VAL_HANDLE_VOID_POINTER(ARG(value2));

    return rebLogic(h1 == h2);
}


#include "tmp-mod-zeromq-last.h"
