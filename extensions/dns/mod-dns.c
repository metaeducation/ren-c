//
//  File: %p-dns.c
//  Summary: "DNS port interface"
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
// !!! R3-Alpha used WSAAsyncGetHostByName and WSAAsyncGetHostByName to do
// non-blocking DNS lookup on Windows.  These functions are deprecated, since
// they do not have IPv6 equivalents...so applications that want asynchronous
// lookup are expected to use their own threads and call getnameinfo().
//


#ifdef TO_WINDOWS
    #include <winsock2.h>
    #undef IS_ERROR  // Windows defines this, so does %sys-core.h
#else
    #include <errno.h>
    #include <fcntl.h>
    #include <netdb.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>

    #ifndef HOSTENT
        typedef struct hostent HOSTENT;
    #endif
#endif

#include "sys-core.h"

#include "tmp-mod-dns.h"

EXTERN_C REBDEV Dev_Net;

//
//  DNS_Actor: C
//
static REB_R DNS_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb)
{
    // !!! The DNS shares "lazy initialization" code with the network code.
    // This is because before you can call any network operations on Windows,
    // you need to call WSAStartup, but you don't necessarily want to pay
    // for that cost if your script doesn't do any network operations.
    // Hence the port state of being open or closed relates to that.
    //
    REBREQ *req = Force_Get_Port_State(port, &Dev_Net);
    struct rebol_devreq *sock = Req(req);

    sock->timeout = 4000;  // where does this go? !!!

    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);

    switch (VAL_WORD_SYM(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // covered by `port`

        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
          case SYM_OPEN_Q:
            return Init_Logic(D_OUT, did (sock->flags & RRF_OPEN));

          default:
            break;
        }

        break; }

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;
        UNUSED(PAR(source));  // covered by `port`

        if (REF(part) or REF(seek))
            fail (Error_Bad_Refines_Raw());

        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        if (not (sock->flags & RRF_OPEN))
            OS_DO_DEVICE_SYNC(req, RDC_OPEN);  // e.g. to call WSAStartup()

        REBVAL *host = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);

        // A DNS read e.g. of `read dns://66.249.66.140` should do a reverse
        // lookup.  The scheme handler may pass in either a TUPLE! or a string
        // that scans to a tuple, at this time (currently uses a string)
        //
        if (IS_TUPLE(host)) {
          reverse_lookup:
            if (VAL_TUPLE_LEN(host) != 4)
                fail ("Reverse DNS lookup requires length 4 TUPLE!");

            // 93.184.216.34 => example.com
            HOSTENT *he = gethostbyaddr(
                cast(char*, VAL_TUPLE(host)),
                4,
                AF_INET
            );
            if (he != nullptr)
                return Init_Text(D_OUT, Make_String_UTF8(he->h_name));

            // ...else fall through to error handling...
        }
        else if (IS_TEXT(host)) {
            REBVAL *tuple = rebValue(
                "match tuple! [' ' ']: transcode", host,
            rebEND);
            if (tuple) {
                Move_Value(host, tuple);
                rebRelease(tuple);
                goto reverse_lookup;
            }

            char *name = rebSpell(host, rebEND);

            // example.com => 93.184.216.34
            HOSTENT *he = gethostbyname(name);

            rebFree(name);
            if (he != nullptr)
                return Init_Tuple(D_OUT, cast(REBYTE*, *he->h_addr_list), 4);

            // ...else fall through to error handling...
        }
        else
            fail (Error_On_Port(SYM_INVALID_SPEC, port, -10));

        switch (h_errno) {
          case HOST_NOT_FOUND:  // The specified host is unknown
          case NO_ADDRESS:  // (or NO_DATA) name is valid but has no IP
            return Init_Nulled(D_OUT);  // "expected" failures, signal w/null

          case NO_RECOVERY:
            rebJumps(
                "FAIL {A nonrecoverable name server error occurred}",
                rebEND
            );

          case TRY_AGAIN:
            rebJumps(
                "FAIL {Temporary error on authoritative name server}",
                rebEND
            );

          default:
            rebJumps("FAIL {Unknown host error}", rebEND);
        } }

      case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));

        if (REF(new) or REF(read) or REF(write) or REF(seek) or REF(allow))
            fail (Error_Bad_Refines_Raw());

        OS_DO_DEVICE_SYNC(req, RDC_OPEN);
        RETURN (port); }

      case SYM_CLOSE: {
        OS_DO_DEVICE_SYNC(req, RDC_CLOSE);  // e.g. WSACleanup()
        RETURN (port); }

      case SYM_ON_WAKE_UP:
        return Init_Void(D_OUT);

      default:
        break;
    }

    return R_UNHANDLED;
}


//
//  export get-dns-actor-handle: native [
//
//  {Retrieve handle to the native actor for DNS}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_dns_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &DNS_Actor);
    return D_OUT;
}
