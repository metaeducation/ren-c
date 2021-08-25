//
//  File: %host-device.c
//  Summary: "Device management and command dispatch"
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

#include <string.h>
#include <stdlib.h>

#include "sys-core.h"


static int Poll_Default(REBDEV *dev)
{
    // The default polling function for devices.
    // Retries pending requests. Return TRUE if status changed.

    bool change = false;

    REBNOD **prior = &dev->pending;
    REBREQ *req = BIN(m_cast(REBNOD*, *prior));
    for (; req != nullptr; req = BIN(m_cast(REBNOD*, *prior))) {
        assert(Req(req)->command < RDC_MAX);

        // Call command again:

        Req(req)->flags &= ~RRF_ACTIVE;
        int result = dev->commands[Req(req)->command](req);

        if (result == DR_DONE) { // if done, remove from pending list
            *prior = LINK(ReqNext, req);
            mutable_LINK(ReqNext, req) = nullptr;
            Req(req)->flags &= ~RRF_PENDING;
            change = true;
        }
        else {
            assert(result == DR_PEND);

            prior = &node_LINK(ReqNext, req);
            if (Req(req)->flags & RRF_ACTIVE)
                change = true;
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
void Attach_Request(REBNOD **node, REBREQ *req)
{
    REBNOD *r;

    // See if its there, and get last req:
    for (r = *node; r; r = *node) {
        if (r == req) return; // already in list
        node = &node_LINK(ReqNext, BIN(r));
    }

    // Link the new request to end:
    *node = req;
    Force_Req_Managed(req);
    mutable_LINK(ReqNext, req) = nullptr;
    Req(req)->flags |= RRF_PENDING;
}


//
//  Detach_Request: C
//
// Detach a request to a device's pending or accept list.
// If it is not in list, then no harm done.
//
void Detach_Request(REBNOD **node, REBREQ *req)
{
    REBNOD *r;

    for (r = *node; r; r = *node) {
        if (r == req) {
            *node = LINK(ReqNext, req);
            mutable_LINK(ReqNext, req) = nullptr;
            Req(req)->flags |= RRF_PENDING;
            return;
        }
        node = &node_LINK(ReqNext, BIN(r));
    }
}


// For use with rebRescue(), to intercept failures in order to do some
// processing if necessary before passing the failure up the stack.  The
// rescue will return this function's result (an INTEGER!) if no error is
// raised during the device code.
//
static REBVAL *Dangerous_Command(REBREQ *req) {
    REBDEV *dev = Req(req)->device;

    int result = (dev->commands[Req(req)->command])(req);
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
REBVAL *OS_Do_Device(REBREQ *req)
{
    REBDEV *dev = Req(req)->device;
    if (dev == NULL)
        rebJumps("fail {Rebol Device Not Found}");

    if (dev->commands[Req(req)->command] == NULL)
        rebJumps("fail {Invalid Command for Rebol Device}");

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

    if (rebDid("error?", error_or_int)) {
        if (dev->pending)
            Detach_Request(&dev->pending, req); // "often a no-op", it said

        return error_or_int;

        // !!! Should an auto-fail variation be offered, for callers who
        // do not want to get involved?
    }

    int result = rebUnboxInteger(rebR(error_or_int));

    // If request is pending, attach it to device for polling:
    //
    if (result == DR_PEND) {
        Attach_Request(&dev->pending, req);
        return NULL;
    }

    assert(result == DR_DONE);
    if (dev->pending)
        Detach_Request(&dev->pending, req); // often a no-op

    return rebLogic(true);
}


//
//  OS_Make_Devreq: C
//
REBREQ *OS_Make_Devreq(REBDEV *dev)
{
    REBREQ *req = Make_Binary_Core(
        dev->req_size,
        SERIES_FLAG_LINK_NODE_NEEDS_MARK | SERIES_FLAG_MISC_NODE_NEEDS_MARK
    );
    memset(BIN_HEAD(req), 0, dev->req_size);
    TERM_BIN_LEN(req, dev->req_size);

    mutable_LINK(ReqNext, req) = nullptr;
    mutable_MISC(ReqPortCtx, req) = nullptr;

    Req(req)->device = dev;

    return req;
}


//
//  OS_Abort_Device: C
//
// Ask device to abort prior request.
//
int OS_Abort_Device(REBREQ *req)
{
    REBDEV *dev = Req(req)->device;
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
    int num_changed = 0;

    REBDEV *dev = PG_Device_List;
    for (; dev != nullptr; dev = dev->next) {
        if (Poll_Default(dev))
            ++num_changed;
    }

    return num_changed;
}


//
//  OS_Register_Device: C
//
// !!! This follows the R3-Alpha model that a device is expected to be a
// global static variable, that is registered until the program finishes.  A
// more dynamic solution would be needed for DLLs that unload and reload...
// because the memory for the device would "go missing"--hence it would need
// some mechanism of unregistering.
//
void OS_Register_Device(REBDEV *dev) {
    dev->next = PG_Device_List;
    PG_Device_List = dev;
}

//
//  OS_Unregister_Device: C
//
void OS_Unregister_Device(REBDEV *dev) {
    //
    // !!! There was nothing to clear out pending events in R3-Alpha
    // if the device itself didn't free them.  "OS Events" for instance.
    // In order to be able to shut down and start up again safely if
    // we want to, they have to be freed.
    //
    while (dev->pending)
        Detach_Request(&dev->pending, BIN(m_cast(REBNOD*, dev->pending)));

    if (PG_Device_List == dev)
        PG_Device_List = dev->next;
    else {
        REBDEV *temp = PG_Device_List;
        while (temp->next != dev)
            temp = temp->next;
        temp->next = dev->next;
    }

    TRASH_POINTER_IF_DEBUG(dev->next);
}
