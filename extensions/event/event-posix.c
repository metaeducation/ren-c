//
//  File: %dev-event.c
//  Summary: "Device: Event handler for Posix"
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
//=////////////////////////////////////////////////////////////////////////=//
//

#if !defined( __cplusplus) && defined(TO_LINUX)
    // See feature_test_macros(7)
    // This definition is redundant under C++
    #define _GNU_SOURCE  // Needed for pipe2 on Linux
#endif

#include <assert.h>

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <errno.h>

#include "sys-core.h"

//
//  Delta_Time: C
//
// Return time difference in microseconds. If base = 0, then
// return the counter. If base != 0, compute the time difference.
//
// NOTE: This needs to be precise, but many OSes do not
// provide a precise time sampling method. So, if the target
// posix OS does, add the ifdef code in here.
//
int64_t Delta_Time(int64_t base)
{
    struct timeval tv;
    gettimeofday(&tv,0);

    int64_t time = cast(int64_t, tv.tv_sec * 1000000) + tv.tv_usec;
    if (base == 0)
        return time;

    return time - base;
}


//
//  Reap_Process: C
//
// pid:
//     > 0, a single process
//     -1, any child process
//
// flags:
//     0: return immediately
//
// Return -1 on error
//
int Reap_Process(int pid, int *status, int flags)
{
    return waitpid(pid, status, flags == 0? WNOHANG : 0);
}


extern void Done_Device(uintptr_t handle, int error);

//
//  Startup_Events: C
//
// Initialize the event device.
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
    struct timeval tv;
    int result;

    tv.tv_sec = 0;
    tv.tv_usec = Req(req)->length * 1000;
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
