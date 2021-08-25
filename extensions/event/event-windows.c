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

#ifndef HWND_MESSAGE
#define HWND_MESSAGE (HWND)-3
#endif

extern void Done_Device(uintptr_t handle, int error);

// Move or remove globals? !?
HWND Event_Handle = 0;          // Used for async DNS
static int Timer_Id = 0;        // The timer we are using


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
//  Reap_Process: C
//
// pid:
//      > 0, a single process
//      -1, any child process
// flags:
//      0: return immediately
//
//      Return -1 on error
//
int Reap_Process(int pid, int *status, int flags)
{
    UNUSED(pid);
    UNUSED(status);
    UNUSED(flags);

    // !!! It seems that processes don't need to be "reaped" on Windows (?)
    return 0;
}


//
//  REBOL_Event_Proc: C
//
// The minimal default event handler.
//
LRESULT CALLBACK REBOL_Event_Proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch(msg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            // Default processing that we do not care about:
            return DefWindowProc(hwnd, msg, wparam, lparam);
    }
    return 0;
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
    // !!! The Windows build of R3-Alpha used a hidden window for message
    // processing.  The only use case was asynchronous DNS, which was a
    // deprecated feature (not being carried forward to IPv6):
    //
    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms741522(v=vs.85).aspx
    //
    // One aspect of making this window is that it requires the HINSTANCE of
    // the application.  That was being passed via a global App_Instance
    // variable:
    //
    // EXTERN_C HINSTANCE App_Instance;  // From Main module.
    //
    // This complicated linking of libRebol, and since the event strategy is
    // being rethought this is #ifdef'd out for now.
    //
    // Long-term, the better way to tunnel such parameters from the host to
    // extensions would likely be to put a HANDLE! in the environment, and
    // then those extensions that require the Windows HINSTANCE could
    // complain if it wasn't there...vs. creating a linker dependency for
    // all clients.
    //
  #if 0
    WNDCLASSEX wc;

    memset(&wc, '\0', sizeof(wc));

    // Register event object class:
    wc.cbSize        = sizeof(wc);
    wc.lpszClassName = L"REBOL-Events";
    wc.hInstance     = App_Instance;
    wc.lpfnWndProc   = REBOL_Event_Proc;

    ATOM atom = RegisterClassEx(&wc);
    if (atom == 0)
        rebFail_OS (GetLastError());

    Event_Handle = CreateWindowEx(
        0,
        wc.lpszClassName,
        wc.lpszClassName,
        0,0,0,0,0,
        HWND_MESSAGE, // used for message-only windows
        NULL, App_Instance, NULL
    );
    if (Event_Handle == NULL)
        rebFail_OS (GetLastError());
  #else
    Event_Handle = NULL;
  #endif
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
