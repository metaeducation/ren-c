//
//  File: %dev-dns.c
//  Summary: "Device: DNS access"
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Calls local DNS services for domain name lookup.
//
// See MS WSAAsyncGetHost* details regarding multiple requests.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "reb-host.h"
#include "sys-net.h"
#include "reb-net.h"

extern DEVICE_CMD Init_Net(REBREQ *); // Share same init
extern DEVICE_CMD Quit_Net(REBREQ *);


//
//  Open_DNS: C
//
DEVICE_CMD Open_DNS(REBREQ *sock)
{
    sock->flags |= RRF_OPEN;
    return DR_DONE;
}


//
//  Close_DNS: C
//
// Note: valid even if not open.
//
DEVICE_CMD Close_DNS(REBREQ *req)
{
    // Terminate a pending request:
    struct devreq_net *sock = DEVREQ_NET(req);

    if (sock->host_info) OS_FREE(sock->host_info);
    sock->host_info = 0;
    req->requestee.handle = 0;
    req->flags &= ~RRF_OPEN;
    return DR_DONE; // Removes it from device's pending list (if needed)
}


//
//  Read_DNS: C
//
// Initiate the GetHost request and return immediately.
// Note the temporary results buffer (must be freed later by the caller).
//
// !!! R3-Alpha used WSAAsyncGetHostByName and WSAAsyncGetHostByName to do
// non-blocking DNS lookup on Windows.  These functions are deprecated, since
// they do not have IPv6 equivalents...so applications that want asynchronous
// lookup are expected to use their own threads and call getnameinfo().
//
// !!! R3-Alpha was written to use the old non-reentrant form in POSIX, but
// glibc2 implements _r versions.
//
DEVICE_CMD Read_DNS(REBREQ *req)
{
    struct devreq_net *sock = DEVREQ_NET(req);
    char *host = OS_ALLOC_N(char, MAXGETHOSTSTRUCT);

    HOSTENT *he;
    if (req->modes & RST_REVERSE) {
        // 93.184.216.34 => example.com
        he = gethostbyaddr(
            cast(char*, &sock->remote_ip), 4, AF_INET
        );
        if (he != NULL) {
            sock->host_info = host; //???
            req->common.data = b_cast(he->h_name);
            req->flags |= RRF_DONE;
            return DR_DONE;
        }
    }
    else {
        // example.com => 93.184.216.34
        he = gethostbyname(s_cast(req->common.data));
        if (he != NULL) {
            sock->host_info = host; // ?? who deallocs?
            memcpy(&sock->remote_ip, *he->h_addr_list, 4); //he->h_length);
            req->flags |= RRF_DONE;
            return DR_DONE;
        }
    }

    OS_FREE(host);
    sock->host_info = NULL;

    switch (h_errno) {
    case HOST_NOT_FOUND: // The specified host is unknown
    case NO_ADDRESS: // (or NO_DATA) name is valid but has no IP
        //
        // The READ should return a blank in these cases, vs. raise an
        // error, for convenience in handling.
        //
        req->flags |= RRF_DONE;
        return DR_DONE;

    case NO_RECOVERY:
        rebFail ("A nonrecoverable name server error occurred");
        DEAD_END;

    case TRY_AGAIN:
        rebFail ("Temporary error on authoritative name server");
        DEAD_END;

    default:
        break;
    }

    rebFail (NULL);
    DEAD_END;
}


//
//  Poll_DNS: C
//
// Check for completed DNS requests. These are marked with
// RRF_DONE by the windows message event handler (dev-event.c).
// Completed requests are removed from the pending queue and
// event is signalled (for awake dispatch).
//
DEVICE_CMD Poll_DNS(REBREQ *dr)
{
    REBDEV *dev = (REBDEV*)dr;  // to keep compiler happy
    REBREQ **prior = &dev->pending;
    REBREQ *req;
    REBOOL change = FALSE;
    HOSTENT *host;

    // Scan the pending request list:
    for (req = *prior; req; req = *prior) {

        // If done or error, remove command from list:
        if (req->flags & RRF_DONE) {
            *prior = req->next;
            req->next = 0;
            req->flags &= ~RRF_PENDING;

            host = cast(HOSTENT*, DEVREQ_NET(req)->host_info);
            if (req->modes & RST_REVERSE)
                req->common.data = b_cast(host->h_name);
            else
                memcpy(&(DEVREQ_NET(req)->remote_ip), *host->h_addr_list, 4); //he->h_length);
            OS_SIGNAL_DEVICE(req, EVT_READ);
            change = TRUE;
        }
        else prior = &req->next;
    }

    return change ? 1 : 0; // DEVICE_CMD implicitly returns i32
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] =
{
    Init_Net,   // Shared init - called only once
    Quit_Net,   // Shared
    Open_DNS,
    Close_DNS,
    Read_DNS,
    0,  // write
    Poll_DNS
};

DEFINE_DEV(Dev_DNS, "DNS", 1, Dev_Cmds, RDC_MAX, sizeof(struct devreq_net));
