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

    REBOOL change = FALSE;

    REBREQ **prior = &dev->pending;
    REBREQ *req;
    for (req = *prior; req; req = *prior) {
        assert(req->command < RDC_MAX);

        // Call command again:

        req->flags &= ~RRF_ACTIVE;
        int result = dev->commands[req->command](req);

        if (result == DR_DONE) { // if done, remove from pending list
            *prior = req->next;
            req->next = 0;
            req->flags &= ~RRF_PENDING;
            change = TRUE;
        }
        else {
            assert(result == DR_PEND);

            prior = &req->next;
            if (req->flags & RRF_ACTIVE)
                change = TRUE;
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

    for (r = *node; r; r = *node) {
        if (r == req) {
            *node = req->next;
            req->next = 0;
            req->flags |= RRF_PENDING;
            return;
        }
        node = &r->next;
    }
}


//
//  OS_Signal_Device: C
//
// Generate a device event to awake a port on REBOL.
//
// !!! R3-Alpha had this explicitly imported in some files by having an extern
// definition, but just put it in as another OS_XXX function for now.
//
void OS_Signal_Device(REBREQ *req, REBYTE type)
{
    REBEVT evt;

    CLEARS(&evt);

    evt.type = type;
    evt.model = EVM_DEVICE;
    evt.eventee.req = req;

    rebEvent(&evt); // (returns 0 if queue is full, ignored)
}


// For use with rebRescue(), to intercept failures in order to do some
// processing if necessary before passing the failure up the stack.  The
// rescue will return this function's result (an INTEGER!) if no error is
// raised during the device code.
//
static REBVAL *Dangerous_Command(REBREQ *req) {
    REBDEV *dev = Devices[req->device];

    int result = (dev->commands[req->command])(req);
    return rebInteger(result);
}


//
//  OS_Do_Device: C
//
// Tell a device to perform a command.  Non-blocking in many cases and will
// attach the request for polling.
//
// !!! R3-Alpha returned 0 for success (DR_DONE), 1 for command still pending
// (DR_PEND) and negative numbers for errors.  As the device model is revamped
// the concept is to return the actual result, NULL if pending, or an ERROR!.
//
REBVAL *OS_Do_Device(REBREQ *req, REBCNT command)
{
    req->command = command;

    if (req->device >= RDI_MAX)
        rebFail ("{Rebol Device Number Too Large}", rebEnd());

    REBDEV *dev = Devices[req->device];
    if (dev == NULL)
        rebFail ("{Rebol Device Not Found}", rebEnd());

    if (NOT(dev->flags & RDF_INIT)) {
        if (dev->flags & RDO_MUST_INIT)
            rebFail ("{Rebol Device Uninitialized}", rebEnd());

        if (
            !dev->commands[RDC_INIT]
            || !dev->commands[RDC_INIT](cast(REBREQ*, dev))
        ){
            dev->flags |= RDF_INIT;
        }
    }

    if (
        req->command > dev->max_command
        || dev->commands[req->command] == NULL
    ){
        rebFail ("{Invalid Command for Rebol Device}", rebEnd());
    }

    // !!! Currently the StdIO port is initialized before Rebol's startup
    // code ever runs.  This is to allow debug messages to be printed during
    // boot.  That means it's too early to be pushing traps, having errors,
    // or really using any REBVALs at all.  Review the dependency, but in
    // the meantime just don't try and push trapping of errors if there's
    // not at least one Rebol state pushed.
    //
    if (req->device == RDI_STDIO && req->command == RDC_OPEN) {
        int result = (dev->commands[req->command])(req);
        assert(result == DR_DONE);
        UNUSED(result);
        return NULL;
    }

    // !!! R3-Alpha had it so when an error was raised from a "device request"
    // it would give back DR_ERROR and the caller would have to interpret an
    // integer error code that was filled into the request.  Sometimes these
    // were OS-specific, and hence not readable to most people...and sometimes
    // they were just plain made up (e.g. search for `req->error = -18` in the
    // R3-Alpha sources.)
    //
    // The plan here is to use the fail() mechanic to let literate error
    // messages be produced.  However, there was code here that would react
    // to DR_ERROR in order to allow for cleanup in the case that a request
    // was flagged with RRF_ALLOC.  New lifetime management strategies that
    // attach storage to stack frames should make that aspect obsolete.
    //
    // There was one other aspect of presumed pending removal, however.  For
    // now, preserve that behavior by always running the device code with
    // a trap in effect.

    REBVAL *error_or_int = rebRescue(cast(REBDNG*, &Dangerous_Command), req);

    if (rebTypeOf(error_or_int) == RXT_ERROR) {
        if (dev->pending)
            Detach_Request(&dev->pending, req); // "often a no-op", it said

        return error_or_int;

        // !!! Should an auto-fail variation be offered, for callers who
        // do not want to get involved?
        /* rebFail (error_or_int, rebEnd()); // propagate error up the stack*/
    }

    assert(rebTypeOf(error_or_int) == RXT_INTEGER);

    int result = rebUnboxInteger(error_or_int);
    rebRelease(error_or_int);

    // If request is pending, attach it to device for polling:
    //
    if (result == DR_PEND) {
        Attach_Request(&dev->pending, req);
        return NULL;
    }

    assert(result == DR_DONE);
    if (dev->pending)
        Detach_Request(&dev->pending, req); // often a no-op

    return rebLogic(TRUE);
}


//
//  OS_Make_Devreq: C
//
REBREQ *OS_Make_Devreq(int device)
{
    assert(device < RDI_MAX);

    REBDEV *dev = Devices[device];
    assert(dev != NULL);

    REBREQ *req = cast(REBREQ *, OS_ALLOC_MEM(dev->req_size));
    memset(req, 0, dev->req_size);
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
    REBDEV *dev = Devices[req->device];
    assert(dev != NULL);

    Detach_Request(&dev->pending, req);
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
            && (dev->pending || DID(dev->flags & RDO_AUTO_POLL))
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
            && DID(dev->flags & RDF_INIT)
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
    // printf("OS_Wait %d\n", millisec);

    int64_t base = OS_DELTA_TIME(0); // start timing

    // Comment said "Setup for timing: OK: QUERY below does not store it"
    //
    REBREQ req;
    CLEARS(&req);
    req.device = RDI_EVENT;

    OS_REAP_PROCESS(-1, NULL, 0);

    // Let any pending device I/O have a chance to run:
    //
    if (OS_Poll_Devices())
        return -1;

    // Nothing, so wait for period of time

    REBCNT delta = cast(REBCNT, OS_DELTA_TIME(base)) / 1000 + res;
    if (delta >= millisec)
        return 0;

    millisec -= delta; // account for time lost above
    req.length = millisec;

    // printf("Wait: %d ms\n", millisec);

    // Comment said "wait for timer or other event"
    //
    REBVAL *result = OS_DO_DEVICE(&req, RDC_QUERY);
    assert(result != NULL); // should be synchronous
    if (rebTypeOf(result) == RXT_ERROR)
        rebFail (result, rebEnd());
    rebRelease(result); // ignore result

    return 1;  // layer above should check delta again
}
