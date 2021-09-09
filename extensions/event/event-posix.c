//
//  File: %event-posix.c
//  Summary: "Device: Event handler for Posix"
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

#if !defined( __cplusplus) && TO_LINUX
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
//  Startup_Events: C
//
// Currently there is no special startup event code for POSIX.
//
void Startup_Events(void)
{
}


//
//  Wait_Milliseconds_Interrupted: C
//
// !!! This said "Wait for an event, or a timeout (in milliseconds)".  This
// makes it sound like the select() statement could be interrupted by something
// other than a timeout, even though it's passing in all 0s for the file
// descriptors to wait on...is that just Ctrl-C?
//
bool Wait_Milliseconds_Interrupted(
    unsigned int millisec  // the MAX_WAIT_MS is 64 in WAIT, between polls
){
    struct timeval tv;

    tv.tv_sec = 0;
    tv.tv_usec = millisec * 1000;

    int result = select(0, 0, 0, 0, &tv);
    if (result < 0) {
        if (errno == EINTR)  // e.g. Ctrl-C interrupting timer on WAIT
            return true;

        rebFail_OS (errno);  // some other error
    }

    return false;
}
