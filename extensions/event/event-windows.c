//
//  File: %dev-event.c
//  Summary: "Device: Event handler for Win32"
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
// Processes events to pass to REBOL. Note that events are
// used for more than just windowing.
//

#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>
#undef IS_ERROR

#include "sys-core.h"

extern void Done_Device(uintptr_t handle, int error);

static int Timer_Id = 0;  // The timer we are using


//
//  Delta_Time: C
//
// Return time difference in microseconds. If base = 0, then
// return the counter. If base != 0, compute the time difference.
//
// Note: Requires high performance timer.
//      Q: If not found, use timeGetTime() instead ?!
//
int64_t Delta_Time(int64_t base)
{
    LARGE_INTEGER time;
    if (not QueryPerformanceCounter(&time))
        rebJumps("panic {Missing high performance timer}");

    if (base == 0) return time.QuadPart; // counter (may not be time)

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    return ((time.QuadPart - base) * 1000) / (freq.QuadPart / 1000);
}


//
//  Startup_Events: C
//
// Initialize the event device.
//
// Create a hidden window to handle special events,
// such as timers and async DNS.
//
extern void Startup_Events(void);
void Startup_Events(void)
{
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
    // Set timer (we assume this is very fast):
    Timer_Id = SetTimer(0, Timer_Id, Req(req)->length, 0);

    // Wait for message or the timer:
    //
    MSG msg;
    if (GetMessage(&msg, NULL, 0, 0))
        DispatchMessage(&msg);

    // Quickly check for other events:
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        // !!! A flag was set here to return DR_PEND, when this was
        // Poll_Events...which seemingly only affected the GUI.
        //
        if (msg.message == WM_TIMER)
            break;
        DispatchMessage(&msg);
    }


    //if (Timer_Id) KillTimer(0, Timer_Id);
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

static DEVICE_CMD_CFUNC Dev_Cmds[RDC_MAX] = {
    0,  // RDC_OPEN,        // open device unit (port)
    0,  // RDC_CLOSE,       // close device unit
    0,  // RDC_READ,        // read from unit
    0,  // RDC_WRITE,       // write to unit
    Connect_Events,
    Query_Events,
};

EXTERN_C REBDEV Dev_Event;
DEFINE_DEV(Dev_Event, "OS Events", 1, Dev_Cmds, RDC_MAX, sizeof(struct rebol_devreq));
