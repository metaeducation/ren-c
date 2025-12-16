//
//  file: %p-dns.c
//  summary: "DNS port interface"
//  section: ports
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
  #if defined(_MSC_VER)
    #pragma warning(disable : 4668)  // allow #if of undefined things
  #endif
    #include <winsock2.h>  // has bad #ifdefs in <winioctl.h>
  #if defined(_MSC_VER)
    #pragma warning(error : 4668)   // disallow #if of undefined things
  #endif

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

#include "tmp-paramlists.h"  // !!! for INCLUDE_PARAMS_OF_OPEN, etc.

#if !(TO_WINDOWS)
//
//  Get_Local_Ip_Via_Google_DNS_May_Panic: C
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
static void Get_Local_Ip_Via_Google_DNS_May_Panic(Sink(Stable) out)
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
        goto cleanup_and_panic_if_error;
    }

    if (info->ai_family == AF_INET6) {
        error = "dns:// doesn't support IPv6 yet";
        goto cleanup_and_panic_if_error;
    }

  create_socket: { ///////////////////////////////////////////////////////////

    sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    if (sock <= 0) {
        error = "Socket creation error to 8.8.8.8 for dns://";
        goto cleanup_and_panic_if_error;
    }

} connect_to_server: { ///////////////////////////////////////////////////////

    if (connect(sock, info->ai_addr, info->ai_addrlen) < 0) {
        error = "Connection error to 8.8.8.8 for dns://";
        goto cleanup_and_panic_if_error;
    }

} get_local_socket_info: { ///////////////////////////////////////////////////

    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(sock, cast(struct sockaddr*, &local_addr), &addr_len) < 0) {
        error = "getsockname() error for local socket to 8.8.8.8 for dns://";
        goto cleanup_and_panic_if_error;
    }

    require (
      Init_Tuple_Bytes(out, cast(Byte*, &local_addr.sin_addr.s_addr), 4)
    );

} cleanup_and_panic_if_error: { //////////////////////////////////////////////

    if (sock > 0)
        close(sock);
    if (info)
        freeaddrinfo(info);
    if (error)
        panic (error);
}}
#endif



//
//  export dns-actor: native [
//
//  "Handler for OLDGENERIC dispatch on DNS PORT!s"
//
//      return: [any-stable?]
//  ]
//
DECLARE_NATIVE(DNS_ACTOR)
{
    Stable* port = ARG_N(1);
    const Symbol* verb = Level_Verb(LEVEL);

    VarList* ctx = Cell_Varlist(port);
    Stable* spec = Slot_Hack(Varlist_Slot(ctx, STD_PORT_SPEC));

    switch (opt Symbol_Id(verb)) {
      case SYM_OPEN_Q:
        return "panic -[DNS 'ports' don't support OPEN?, only READ]-";

      case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        if (ARG(PART) or ARG(SEEK))
            panic (Error_Bad_Refines_Raw());

        UNUSED(PARAM(STRING)); // handled in dispatcher
        UNUSED(PARAM(LINES)); // handled in dispatcher

        Stable* host = Slot_Hack(Obj_Slot(spec, STD_PORT_SPEC_NET_HOST));

        if (Is_Nulled(host)) {
            //
            // Semantics of `read dns://` are open-ended.  Rebol2 gives back
            // the machine name.  Passing empty string to Windows's
            // gethostbyname() appears to give back the local machine's
            // hostent, but Linux gives back null.  (The Windows documentation
            // says gethostbyname(nullptr) is the same as an empty string,
            // but MSVC's /analyze checker says that's not legal.)
            //
          #if TO_WINDOWS
            HOSTENT *he = gethostbyname("");  // 1 HOSTENT per thread
            if (he != nullptr) {
                require (
                    Init_Tuple_Bytes(OUT, cast(Byte*, *he->h_addr_list), 4)
                );
                return OUT;
            }
          #else
            Get_Local_Ip_Via_Google_DNS_May_Panic(OUT);
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
            if (Sequence_Len(host) != 4)
                return "panic -[Reverse DNS lookup requires length 4 TUPLE!]-";

            // 93.184.216.34 => example.com
            char buf[MAX_TUPLE];
            Get_Tuple_Bytes(buf, host, 4);
            HOSTENT *he = gethostbyaddr(buf, 4, AF_INET);
            if (he != nullptr)
                return Init_Text(OUT, Make_Strand_UTF8(he->h_name));

            // ...else fall through to error handling...
        }
        else if (Is_Text(host)) {
            Api(Stable*) tuple = rebStable(
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
            if (he != nullptr) {
                require (
                  Init_Tuple_Bytes(OUT, cast(Byte*, *he->h_addr_list), 4)
                );
                return OUT;
            }

            // ...else fall through to error handling...
        }
        else
            panic (Error_On_Port(SYM_INVALID_SPEC, port, -10));

        switch (h_errno) {
          case HOST_NOT_FOUND:  // The specified host is unknown
          case NO_ADDRESS:  // (or NO_DATA) name is valid but has no IP
            return Init_Nulled(OUT);  // "expected" failures, signal w/null

          case NO_RECOVERY:
            return "panic -[A nonrecoverable name server error occurred]-";

          case TRY_AGAIN:
            return "panic -[Temporary error on authoritative name server]-";

          default:
            return "panic -[Unknown host error]-";
        } }

      case SYM_OPEN: {
        INCLUDE_PARAMS_OF_OPEN;

        if (ARG(NEW) or ARG(READ) or ARG(WRITE))
            panic (Error_Bad_Refines_Raw());

        // !!! All the information the DNS needs is at the moment in the
        // port spec, so there's nothing that has to be done in the OPEN.
        // Though at one time, this took advantage of "lazy initialization"
        // of WSAStartup(), piggy-backing on the network layer.
        //
        // So for the moment we error if you try to open a DNS port.

        goto open_or_close_panic; }

      open_or_close_panic:
      case SYM_CLOSE:
        return "panic -[DNS 'ports' don't OPEN/CLOSE, only READ]-";

      default:
        break;
    }

    panic (UNHANDLED);
}
