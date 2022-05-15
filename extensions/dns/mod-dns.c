//
//  File: %p-dns.c
//  Summary: "DNS port interface"
//  Section: ports
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=/////////////////////////////////////////////////////////////////////////=//
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
//=/////////////////////////////////////////////////////////////////////////=//
//
// Only READ is supported on DNS ports at this time:
//
//     >> read dns://rebol.com
//     == 162.216.18.225
//
//     >> read dns://162.216.18.225
//     == "rebol.com"
//
// See %extensions/dns/README.md regarding why asynchronous DNS was removed.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * This extension expects to be loaded alongside the networking extension,
//   as it does not call WSAStartup() itself to start up sockets on Windows.
//

#include "reb-config.h"

#if TO_WINDOWS
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

//
//  DNS_Actor: C
//
static REB_R DNS_Actor(REBFRM *frame_, REBVAL *port, const REBSYM *verb)
{
    REBCTX *ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);

    switch (ID_OF_SYMBOL(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // covered by `port`

        SYMID property = VAL_WORD_ID(ARG(property));
        assert(property != SYM_0);

        switch (property) {
          case SYM_OPEN_Q:
            fail ("DNS 'ports' do not currently support OPEN?, only READ");

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

        REBVAL *host = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);

        // A DNS read e.g. of `read dns://66.249.66.140` should do a reverse
        // lookup.  The scheme handler may pass in either a TUPLE! or a string
        // that scans to a tuple, at this time (currently uses a string)
        //
        if (IS_TUPLE(host)) {
          reverse_lookup:
            if (VAL_SEQUENCE_LEN(host) != 4)
                fail ("Reverse DNS lookup requires length 4 TUPLE!");

            // 93.184.216.34 => example.com
            char buf[MAX_TUPLE];
            Get_Tuple_Bytes(buf, host, 4);
            HOSTENT *he = gethostbyaddr(buf, 4, AF_INET);
            if (he != nullptr)
                return Init_Text(D_OUT, Make_String_UTF8(he->h_name));

            // ...else fall through to error handling...
        }
        else if (IS_TEXT(host)) {
            REBVAL *tuple = rebValue(
                "match tuple! first transcode", host
            );  // W3C says non-IP hosts can't end with number in tuple
            if (tuple) {
                if (rebUnboxLogic("integer? last @", tuple)) {
                    Copy_Cell(host, tuple);
                    rebRelease(tuple);
                    goto reverse_lookup;
                }
                rebRelease(tuple);
            }

            char *name = rebSpell(host);

            // example.com => 93.184.216.34
            HOSTENT *he = gethostbyname(name);

            rebFree(name);
            if (he != nullptr)
                return Init_Tuple_Bytes(D_OUT, cast(REBYTE*, *he->h_addr_list), 4);

            // ...else fall through to error handling...
        }
        else
            fail (Error_On_Port(SYM_INVALID_SPEC, port, -10));

        switch (h_errno) {
          case HOST_NOT_FOUND:  // The specified host is unknown
          case NO_ADDRESS:  // (or NO_DATA) name is valid but has no IP
            return Init_Nulled(D_OUT);  // "expected" failures, signal w/null

          case NO_RECOVERY:
            rebJumps("fail {A nonrecoverable name server error occurred}");

          case TRY_AGAIN:
            rebJumps("fail {Temporary error on authoritative name server}");

          default:
            rebJumps("fail {Unknown host error}");
        } }

      case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PAR(spec));

        if (REF(new) or REF(read) or REF(write))
            fail (Error_Bad_Refines_Raw());

        // !!! All the information the DNS needs is at the moment in the
        // port spec, so there's nothing that has to be done in the OPEN.
        // Though at one time, this took advantage of "lazy initialization"
        // of WSAStartup(), piggy-backing on the network layer.
        //
        // So for the moment we error if you try to open a DNS port.

        fail ("DNS 'ports' do not currently support OPEN, only READ"); }

      case SYM_CLOSE: {
        fail ("DNS 'ports' do not currently support CLOSE, only READ"); }

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
