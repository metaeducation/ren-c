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
    #undef OUT  // %minwindef.h defines this, we have a better use for it
    #undef VOID  // %winnt.h defines this, we have a better use for it

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


#if !(TO_WINDOWS)
//
//  Get_Local_Ip_Via_Google_DNS_May_Fail: C
//
// Passing null to gethostbyname() works on Windows, but does not seem to fly
// on Linux.  Using method described as "most elegant" from this article:
//
//   https://jhshi.me/2013/11/02/how-to-get-hosts-ip-address/index.html
//
// Had to make some fixes:
//
// * Needed const on the char* for C string literals
// * Needed to call freeaddrinfo() on all paths
// * Needed to close the socket on the success path
// * Called gethostname() for no obvious reason
//
static void Get_Local_Ip_Via_Google_DNS_May_Fail(Sink(Value(*)) out)
{
    const char* target_name = "8.8.8.8";  // Google's DNS server IP
    const char* target_port = "53";  // DNS port

    struct addrinfo* info = nullptr;
    const char* error = nullptr;
    int sock = 0;

    struct addrinfo hints;  // get peer server
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int ret = getaddrinfo(target_name, target_port, &hints, &info);
    if (ret != 0) {
        error = gai_strerror(ret);
        goto cleanup_and_fail_if_error;
    }

    if (info->ai_family == AF_INET6) {
        error = "dns:// doesn't support IPv6 yet";
        goto cleanup_and_fail_if_error;
    }

    // create socket
    sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (sock <= 0) {
        error = "Socket creation error to 8.8.8.8 for dns://";
        goto cleanup_and_fail_if_error;
    }

    // connect to server
    if (connect(sock, info->ai_addr, info->ai_addrlen) < 0) {
        error = "Connection error to 8.8.8.8 for dns://";
        goto cleanup_and_fail_if_error;
    }

    // get local socket info
  blockscope {
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sock, cast(struct sockaddr*, &local_addr), &addr_len) < 0) {
        error = "getsockname() error for local socket to 8.8.8.8 for dns://";
        goto cleanup_and_fail_if_error;
    }

    Init_Tuple_Bytes(out, cast(Byte*, &local_addr.sin_addr.s_addr), 4);
  }

  cleanup_and_fail_if_error:

    if (sock > 0)
        close(sock);
    if (info)
        freeaddrinfo(info);
    if (error)
        fail (error);
}
#endif


//
//  DNS_Actor: C
//
static Bounce DNS_Actor(Level* level_, REBVAL *port, const Symbol* verb)
{
    Context* ctx = VAL_CONTEXT(port);
    REBVAL *spec = CTX_VAR(ctx, STD_PORT_SPEC);

    switch (ID_OF_SYMBOL(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));  // covered by `port`

        Option(SymId) property = VAL_WORD_ID(ARG(property));
        switch (property) {
          case SYM_OPEN_Q:
            fail ("DNS 'ports' do not currently support OPEN?, only READ");

          default:
            break;
        }

        break; }

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;
        UNUSED(PARAM(source));  // covered by `port`

        if (REF(part) or REF(seek))
            fail (Error_Bad_Refines_Raw());

        UNUSED(PARAM(string)); // handled in dispatcher
        UNUSED(PARAM(lines)); // handled in dispatcher

        REBVAL *host = Obj_Value(spec, STD_PORT_SPEC_NET_HOST);

        if (Is_Nulled(host)) {
            //
            // Semantics of `read dns://` are open-ended.  Rebol2 gives back
            // the machine name.  Passing empty string or null to Windows's
            // gethostbyname() appears to give back the local machine's
            // hostent, but Linux gives back null.
            //
          #if TO_WINDOWS
            HOSTENT *he = gethostbyname(nullptr);  // 1 HOSTENT per thread
            if (he != nullptr)
                return Init_Tuple_Bytes(OUT, cast(Byte*, *he->h_addr_list), 4);
          #else
            Get_Local_Ip_Via_Google_DNS_May_Fail(OUT);
            return OUT;
          #endif
        }
        else if (Is_Tuple(host)) {
            //
            // DNS read e.g. of `read dns://66.249.66.140` should do a reverse
            // lookup.  Scheme handler may pass in either a TUPLE! or a string
            // that scans to a tuple, at this time (currently uses a string)
            //
          reverse_lookup:
            if (VAL_SEQUENCE_LEN(host) != 4)
                fail ("Reverse DNS lookup requires length 4 TUPLE!");

            // 93.184.216.34 => example.com
            char buf[MAX_TUPLE];
            Get_Tuple_Bytes(buf, host, 4);
            HOSTENT *he = gethostbyaddr(buf, 4, AF_INET);
            if (he != nullptr)
                return Init_Text(OUT, Make_String_UTF8(he->h_name));

            // ...else fall through to error handling...
        }
        else if (Is_Text(host)) {
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
            HOSTENT *he = gethostbyname(name);  // 1 HOSTENT per thread

            rebFree(name);
            if (he != nullptr)
                return Init_Tuple_Bytes(OUT, cast(Byte*, *he->h_addr_list), 4);

            // ...else fall through to error handling...
        }
        else
            fail (Error_On_Port(SYM_INVALID_SPEC, port, -10));

        switch (h_errno) {
          case HOST_NOT_FOUND:  // The specified host is unknown
          case NO_ADDRESS:  // (or NO_DATA) name is valid but has no IP
            return Init_Nulled(OUT);  // "expected" failures, signal w/null

          case NO_RECOVERY:
            rebJumps("fail {A nonrecoverable name server error occurred}");

          case TRY_AGAIN:
            rebJumps("fail {Temporary error on authoritative name server}");

          default:
            rebJumps("fail {Unknown host error}");
        } }

      case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        UNUSED(PARAM(spec));

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

    fail (UNHANDLED);
}


//
//  export get-dns-actor-handle: native [
//
//  {Retrieve handle to the native actor for DNS}
//
//      return: [handle!]
//  ]
//
DECLARE_NATIVE(get_dns_actor_handle)
{
    Make_Port_Actor_Handle(OUT, &DNS_Actor);
    return OUT;
}
