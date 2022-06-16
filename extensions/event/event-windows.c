//
//  File: %dev-event.c
//  Summary: "Device: Event handler for Win32"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// This implements what's needed by WAIT in order to yield to the OS event
// loop for a certain period of time, with the ability to be interrupted.
//

#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>
#undef IS_ERROR
#undef OUT  // %minwindef.h defines this, we have a better use for it
#undef VOID  // %winnt.h defines this, we have a better use for it


#include "sys-core.h"


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
// !!! This once created a hidden window to handle special events, such as
// timers and async DNS.  That is not being done at this time (async DNS
// was deprecated by Microsoft in favor of using synchronous DNS on one's
// own threads--it's not supported in IPv6).
//
void Startup_Events(void)
{
}


//
//  Wait_Milliseconds_Interrupted: C
//
// This is what is called by WAIT in order to yield to the event loop.  It
// was once doing so for GUI messages to be processed (so the UI would not
// freeze up while waiting on network events or on a timer).  At the moment
// that does not apply, so it's just being a good citizen by yielding the
// CPU rather than keeping it in a busy wait during WAIT.
//
bool Wait_Milliseconds_Interrupted(
    unsigned int millisec  // the MAX_WAIT_MS is 64 in WAIT, between polls
){
    // Set timer (we assume this is very fast)
    //
    // !!! This uses the form that needs processing by sending a WM_TIMER
    // message.  This is presumably because when there was a GUI, it wanted
    // to have a way to keep from locking up the interface.
    //
    HWND hwnd = 0;
    TIMERPROC timer_func = nullptr;
    UINT_PTR timer_id = SetTimer(hwnd, 0, millisec, timer_func);
    if (timer_id == 0)
        rebFail_OS (GetLastError());

    // Wait for any message, which could be a timer.
    //
    // Note: The documentation says that GetMessage returns a "BOOL" but then
    // says it can return -1 on error.  :-(
    //
    MSG msg;
    int result = GetMessage(&msg, NULL, 0, 0);
    if (result == -1) {
        KillTimer(hwnd, timer_id);
        rebFail_OS (GetLastError());
    }
    if (result == 0) {  // WM_QUIT
        //
        // !!! We don't currently take in a means to throw a quit signal.
        // Is this necessary?
        //
        fail ("QUIT message received in Wait_Milliseconds_Interrupted()");
    }

    TranslateMessage(&msg);
    DispatchMessage(&msg);

    // If the message we got was the timer we set, then that means we waited
    // for the specified amount of time.
    //
    if (msg.message == WM_TIMER) {
        assert(timer_id == msg.wParam);
        KillTimer(hwnd, timer_id);
        return false;  // not interrupted, waited the full time
    }

    // R3-Alpha did a trick here and did a peek to see if the timer message
    // happens to be the *next* message.  If it was, then it still counted the
    // wait as being complete.
    //
    // !!! Was this a good idea?
    //
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_TIMER) {
            assert(timer_id == msg.wParam);
            KillTimer(hwnd, timer_id);
            return false;
        }
    }

    // If anything else came into the message pump, there was something to
    // do...so assume it means we want to run the polling loop.
    //
    KillTimer(hwnd, timer_id);
    return true;  // interrupted by some GUI event or otherwise
}
