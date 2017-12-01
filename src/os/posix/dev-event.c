//
//  File: %dev-event.c
//  Summary: "Device: Event handler for Posix"
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
// Processes events to pass to REBOL. Note that events are
// used for more than just windowing.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

#include "reb-host.h"

extern void Done_Device(REBUPT handle, int error);

//
//  Init_Events: C
//
// Initialize the event device.
//
// Create a hidden window to handle special events,
// such as timers and async DNS.
//
DEVICE_CMD Init_Events(REBREQ *dr)
{
    REBDEV *dev = (REBDEV*)dr; // just to keep compiler happy
    dev->flags |= RDF_INIT;
    return DR_DONE;
}


//
//  Poll_Events: C
//
// Poll for events and process them.
// Returns 1 if event found, else 0.
//
DEVICE_CMD Poll_Events(REBREQ *req)
{
    UNUSED(req);

    int flag = DR_DONE;
    return flag;    // different meaning compared to most commands
}


//
//  Query_Events: C
//
// Wait for an event, or a timeout (in milliseconds) specified by
// req->length. The latter is used by WAIT as the main timing
// method.
//
DEVICE_CMD Query_Events(REBREQ *req)
{
    struct timeval tv;
    int result;

    tv.tv_sec = 0;
    tv.tv_usec = req->length * 1000;
    //printf("usec %d\n", tv.tv_usec);

    result = select(0, 0, 0, 0, &tv);
    if (result < 0) {
        //
        // !!! In R3-Alpha this had a TBD that said "set error code" and had a
        // printf that said "ERROR!!!!".  However this can happen when a
        // Ctrl-C interrupts a timer on a WAIT.  As a patch this is tolerant
        // of EINTR, but still returns the error code.  :-/
        //
        if (errno == EINTR)
            return DR_DONE;

        rebFail_OS (errno);
    }

    return DR_DONE;
}


//
//  Connect_Events: C
//
// Simply keeps the request pending for polling purposes.
// Use Abort_Device to remove it.
//
DEVICE_CMD Connect_Events(REBREQ *req)
{
    UNUSED(req);
    return DR_PEND; // keep pending
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_FUNC Dev_Cmds[RDC_MAX] = {
    Init_Events,            // init device driver resources
    0,  // RDC_QUIT,        // cleanup device driver resources
    0,  // RDC_OPEN,        // open device unit (port)
    0,  // RDC_CLOSE,       // close device unit
    0,  // RDC_READ,        // read from unit
    0,  // RDC_WRITE,       // write to unit
    Poll_Events,
    Connect_Events,
    Query_Events,
};

DEFINE_DEV(Dev_Event, "OS Events", 1, Dev_Cmds, RDC_MAX, sizeof(REBREQ));
