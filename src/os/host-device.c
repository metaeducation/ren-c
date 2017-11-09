//
//  File: %host-device.c
//  Summary: "Device management and command dispatch"
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
// OS independent
//
// This module is parsed for function declarations used to
// build prototypes, tables, and other definitions. To change
// function arguments requires a rebuild of the REBOL library.
//
// This module implements a device management system for
// REBOL devices and tracking their I/O requests.
// It is intentionally kept very simple (makes debugging easy!)
//
// 1. Not a lot of devices are needed (dozens, not hundreds).
// 2. Devices are referenced by integer (index into device table).
// 3. A single device can support multiple requests.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include <stdio.h>
#include <string.h>

#include "reb-host.h"


/***********************************************************************
**
**  REBOL Device Table
**
**      The table most be in same order as the RDI_ enums.
**      Table is in polling priority order.
**
***********************************************************************/

EXTERN_C REBDEV Dev_StdIO;
EXTERN_C REBDEV Dev_File;
EXTERN_C REBDEV Dev_Event;
EXTERN_C REBDEV Dev_Net;
EXTERN_C REBDEV Dev_DNS;

#ifdef TO_WINDOWS
EXTERN_C REBDEV Dev_Clipboard;
#endif

// There should be a better decoupling of these devices so the core
// does not need to know about them...
#if defined(TO_WINDOWS) || defined(TO_LINUX)
EXTERN_C REBDEV Dev_Serial;
#endif

#ifdef HAS_POSIX_SIGNAL
EXTERN_C REBDEV Dev_Signal;
#endif

REBDEV *Devices[RDI_LIMIT] =
{
    0,
    &Dev_StdIO,
    0,
    &Dev_File,
    &Dev_Event,
    &Dev_Net,
    &Dev_DNS,
#ifdef TO_WINDOWS
    &Dev_Clipboard,
#else
    0,
#endif

#if defined(TO_WINDOWS) || defined(TO_LINUX)
    &Dev_Serial,
#else
    NULL,
#endif

#ifdef HAS_POSIX_SIGNAL
    &Dev_Signal,
#endif
    0,
};


static int Poll_Default(REBDEV *dev)
{
    // The default polling function for devices.
    // Retries pending requests. Return TRUE if status changed.
    REBREQ **prior = &dev->pending;
    REBREQ *req;
    REBOOL change = FALSE;
    int result;

    for (req = *prior; req; req = *prior) {

        // Call command again:
        if (req->command < RDC_MAX) {
            req->flags &= ~RRF_ACTIVE;
            result = dev->commands[req->command](req);
        } else {
            result = -1;    // invalid command, remove it
            req->error = ((REBCNT)-1);
        }

        // If done or error, remove command from list:
        if (result <= 0) {
            *prior = req->next;
            req->next = 0;
            req->flags &= ~RRF_PENDING;
            change = TRUE;
        } else {
            prior = &req->next;
            if (req->flags & RRF_ACTIVE) {
                change = TRUE;
            }
        }
    }

    return change ? 1 : 0;
}


//
//  Attach_Request: C
//
// Attach a request to a device's pending or accept list.
// Node is a pointer to the head pointer of the req list.
//
void Attach_Request(REBREQ **node, REBREQ *req)
{
    REBREQ *r;

#ifdef special_debug
    if (req->device == 5) {
        printf("Attach: %x %x %x %x\n",
            req, req->device, req->port, req->next
        );
        fflush(stdout);
    }
#endif

    // See if its there, and get last req:
    for (r = *node; r; r = *node) {
        if (r == req) return; // already in list
        node = &r->next;
    }

    // Link the new request to end:
    *node = req;
    req->next = 0;
    req->flags |= RRF_PENDING;
}


//
//  Detach_Request: C
//
// Detach a request to a device's pending or accept list.
// If it is not in list, then no harm done.
//
void Detach_Request(REBREQ **node, REBREQ *req)
{
    REBREQ *r;

#ifdef special_debug
    if (req->device == 5) {
        printf("Detach= n: %x r: %x p: %x %x\n",
            *node, req, req->port, &req->next);
        fflush(stdout);
    }
#endif

    // See if its there, and get last req:
    for (r = *node; r; r = *node) {
#ifdef special_debug
    if (req->device == 5) {
        printf("Detach: r: %x n: %x\n", r, r->next);
        fflush(stdout);
    }
#endif
        if (r == req) {
            *node = req->next;
            req->next = 0;
            req->flags |= RRF_PENDING;
            return;
        }
        node = &r->next;
    }
}


extern void Done_Device(REBUPT handle, int error);

//
//  Done_Device: C
//
// Given a handle mark the related request as done.
// (Used by DNS device).
//
void Done_Device(REBUPT handle, int error)
{
    REBINT d;
    for (d = RDI_NET; d <= RDI_DNS; d++) {
        REBDEV *dev = Devices[d];
        REBREQ **prior = &dev->pending;

        // Scan the pending requests, mark the one we got:

        REBREQ *req;
        for (req = *prior; req; req = *prior) {
            if (cast(REBUPT, req->requestee.handle) == handle) {
                req->error = error; // zero when no error
                req->flags |= RRF_DONE;
                return;
            }
            prior = &req->next;
        }
    }
}


//
//  Signal_Device: C
//
// Generate a device event to awake a port on REBOL.
//
void Signal_Device(REBREQ *req, REBINT type)
{
    REBEVT evt;

    CLEARS(&evt);

    evt.type = (REBYTE)type;
    evt.model = EVM_DEVICE;
    evt.eventee.req = req;
    if (type == EVT_ERROR) evt.data = req->error;

    rebEvent(&evt); // (returns 0 if queue is full, ignored)
}


//
//  OS_Call_Device: C
//
// Shortcut for non-request calls to device.
//
// Init - Initialize any device-related resources (e.g. libs).
// Quit - Cleanup any device-related resources.
// Make - Create and initialize a request for a device.
// Free - Free a device request structure.
// Poll - Poll device for activity.
//
int OS_Call_Device(REBINT device, REBCNT command)
{
    REBDEV *dev;
    REBREQ req;

    // Validate device:
    if (device >= RDI_MAX || !(dev = Devices[device]))
        return -1;

    // Validate command:
    if (command > dev->max_command || dev->commands[command] == 0)
        return -2;

    // Do command, return result:
    /* fake a request, not all fields are set */
    req.device = device;
    req.command = command;
    return dev->commands[command](&req);
}


//
//  OS_Do_Device: C
//
// Tell a device to perform a command. Non-blocking in many
// cases and will attach the request for polling.
//
// Returns:
//     =0: for command success
//     >0: for command still pending
//     <0: for command error
//
int OS_Do_Device(REBREQ *req, REBCNT command)
{
    REBDEV *dev;
    REBINT result;

    req->error = 0; // A94 - be sure its cleared

    // Validate device:
    if (req->device >= RDI_MAX || !(dev = Devices[req->device])) {
        req->error = RDE_NO_DEVICE;
        return -1;
    }

    // Confirm device is initialized. If not, return an error or init
    // it if auto init option is set.
    if (NOT(dev->flags & RDF_INIT)) {
        if (dev->flags & RDO_MUST_INIT) {
            req->error = RDE_NO_INIT;
            return -1;
        }
        if (
            !dev->commands[RDC_INIT]
            || !dev->commands[RDC_INIT]((REBREQ*)dev)
        ){
            dev->flags |= RDF_INIT;
        }
    }

    // Validate command:
    if (command > dev->max_command || dev->commands[command] == 0) {
        req->error = RDE_NO_COMMAND;
        return -1;
    }

    // Do the command:
    req->command = command;
    result = dev->commands[command](req);

    // If request is pending, attach it to device for polling:
    if (result > 0) Attach_Request(&dev->pending, req);
    else if (dev->pending) {
        Detach_Request(&dev->pending, req); // often a no-op
        if (result == DR_ERROR && LOGICAL(req->flags & RRF_ALLOC)) {
            // not on stack
            Signal_Device(req, EVT_ERROR);
        }
    }

    return result;
}


//
//  OS_Make_Devreq: C
//
REBREQ *OS_Make_Devreq(int device)
{
    REBDEV *dev;

    // Validate device:
    if (device >= RDI_MAX || !(dev = Devices[device]))
        return 0;

    REBREQ *req = cast (REBREQ *, OS_ALLOC_MEM(dev->req_size));
    memset(req, 0, dev->req_size);
    req->flags |= RRF_ALLOC;
    req->device = device;

    return req;
}


//
//  OS_Abort_Device: C
//
// Ask device to abort prior request.
//
int OS_Abort_Device(REBREQ *req)
{
    REBDEV *dev;

    if ((dev = Devices[req->device]) != 0) Detach_Request(&dev->pending, req);
    return 0;
}


//
//  OS_Poll_Devices: C
//
// Poll devices for activity.
//
// Returns count of devices that changed status.
//
// Devices with pending lists will be called to see if
// there is a change in status of those requests. If so,
// those devices are allowed to change the state of those
// requests or call-back into special REBOL functions
// (e.g. Add_Event for GUI) to invoke special actions.
//
int OS_Poll_Devices(void)
{
    int d;
    int cnt = 0;
    REBDEV *dev;
    //int cc = 0;

    //printf("Polling Devices\n");

    // Check each device:
    for (d = 0; d < RDI_MAX; d++) {
        dev = Devices[d];
        if (
            dev != NULL
            && (dev->pending || LOGICAL(dev->flags & RDO_AUTO_POLL))
        ){
            // If there is a custom polling function, use it:
            if (dev->commands[RDC_POLL]) {
                if (dev->commands[RDC_POLL]((REBREQ*)dev)) cnt++;
            }
            else {
                if (Poll_Default(dev)) cnt++;
            }
        }
        //if (cc != cnt) {printf("dev=%s ", dev->title); cc = cnt;}
    }

    return cnt;
}


//
//  OS_Quit_Devices: C
//
// Terminate all devices in preparation to quit.
//
// Allows devices to perform cleanup and resource freeing.
//
// Set flags to zero for now. (May later be used to indicate
// a device query check or a brute force quit.)
//
// Returns: 0 for now.
//
int OS_Quit_Devices(int flags)
{
    UNUSED(flags);

    int d;
    for (d = RDI_MAX - 1; d >= 0; d--) {
        REBDEV *dev = Devices[d];
        if (
            dev != NULL
            && LOGICAL(dev->flags & RDF_INIT)
            && dev->commands[RDC_QUIT] != NULL
        ){
            dev->commands[RDC_QUIT](cast(REBREQ*, dev));
        }
    }

    return 0;
}


//
//  OS_Wait: C
//
// Check if devices need attention, and if not, then wait.
// The wait can be interrupted by a GUI event, otherwise
// the timeout will wake it.
//
// Res specifies resolution. (No wait if less than this.)
//
// Returns:
//     -1: Devices have changed state.
//      0: past given millsecs
//      1: wait in timer
//
// The time it takes for the devices to be scanned is
// subtracted from the timer value.
//
REBINT OS_Wait(REBCNT millisec, REBCNT res)
{
    REBREQ req;     // OK: QUERY below does not store it
    REBCNT delta;
    i64 base;

    // printf("OS_Wait %d\n", millisec);

    base = OS_Delta_Time(0); // start timing

    // Setup for timing:
    CLEARS(&req);
    req.device = RDI_EVENT;

    OS_Reap_Process(-1, NULL, 0);

    // Let any pending device I/O have a chance to run:
    if (OS_Poll_Devices()) return -1;

    // Nothing, so wait for period of time
    delta = cast(REBCNT, OS_Delta_Time(base)) / 1000 + res;
    if (delta >= millisec) return 0;
    millisec -= delta;  // account for time lost above
    req.length = millisec;

    // printf("Wait: %d ms\n", millisec);
    OS_Do_Device(&req, RDC_QUERY); // wait for timer or other event

    return 1;  // layer above should check delta again
}
