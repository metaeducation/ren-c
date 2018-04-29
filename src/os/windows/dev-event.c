//
//  File: %dev-event.c
//  Summary: "Device: Event handler for Win32"
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

#include <windows.h>
#include "reb-host.h"

#ifndef HWND_MESSAGE
#define HWND_MESSAGE (HWND)-3
#endif

extern void Done_Device(uintptr_t handle, int error);

// Move or remove globals? !?
HWND Event_Handle = 0;          // Used for async DNS
static int Timer_Id = 0;        // The timer we are using

EXTERN_C HINSTANCE App_Instance;  // From Main module.


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

    // Create the hidden window:
    Event_Handle = CreateWindowEx(
        0,
        wc.lpszClassName,
        wc.lpszClassName,
        0,0,0,0,0,
        HWND_MESSAGE,                   //used for message-only windows
        NULL, App_Instance, NULL
    );

    if (Event_Handle == NULL)
        rebFail_OS (GetLastError());

    dev->flags |= RDF_INIT;
    return DR_DONE;
}


//
//  Poll_Events: C
//
// Poll for events and process them.
// Returns 1 if event found, else 0.
//
// MS Notes:
//
// "The PeekMessage function normally does not remove WM_PAINT
// messages from the queue. WM_PAINT messages remain in the queue
// until they are processed."
//
DEVICE_CMD Poll_Events(REBREQ *req)
{
    UNUSED(req);

    MSG msg;
    int flag = DR_DONE;

    // Are there messages to process?
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        flag = DR_PEND;
        if (msg.message == WM_TIMER)
            break;
        DispatchMessage(&msg);
    }

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
    // Set timer (we assume this is very fast):
    Timer_Id = SetTimer(0, Timer_Id, req->length, 0);

    // Wait for message or the timer:
    //
    MSG msg;
    if (GetMessage(&msg, NULL, 0, 0))
        DispatchMessage(&msg);

    // Quickly check for other events:
    Poll_Events(0);

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
