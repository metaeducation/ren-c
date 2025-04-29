//
//  file: %host-time.c
//  summary: "POSIX Host Time Functions"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// Provide platform support for times and timing information.
//
// UNIX/POSIX time functions are a bit of a catastrophe,  For a good
// overview, see this article:
//
// http://www.catb.org/esr/time-programming/
//
// The methods used here are from R3-Alpha.  To see how the GNU
// `date` program gets its information, see:
//
// http://git.savannah.gnu.org/cgit/coreutils.git/tree/src/date.c
//

#ifndef __cplusplus
    // See feature_test_macros(7)
    // This definition is redundant under C++
    #define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <poll.h>
#include <fcntl.h>              /* Obtain O_* constant definitions */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <time.h>
#ifndef timeval
    #include <sys/time.h>  // for older systems
#endif

#include "rebol.h"
typedef RebolValue Value;

#include "c-enhanced.h"



//
//  Get_Timezone: C
//
// Get the time zone in minutes from GMT.
// NOT consistently supported in Posix OSes!
// We have to use a few different methods.
//
// !!! "local_tm->tm_gmtoff / 60 would make the most sense,
// but is no longer used" (said a comment)
//
// !!! This code is currently repeated in the filesystem extension, until a
// better way of sharing it is accomplished.
//
static int Get_Timezone(struct tm *utc_tm_unused)
{
    time_t now_secs;
    time(&now_secs); // UNIX seconds (since "epoch")
    struct tm local_tm = *localtime(&now_secs);

  #if !defined(HAS_SMART_TIMEZONE)
    //
    // !!! The R3-Alpha host code would always give back times in UTC plus a
    // timezone.  Then, functions like NOW would have ways of adjusting for
    // the timezone (unless you asked to do something like NOW/UTC), but
    // without taking daylight savings time into account.
    //
    // We don't want to return a fake UTC time to the caller for the sake of
    // keeping the time zone constant.  So this should return e.g. GMT-7
    // during pacific daylight time, and GMT-8 during pacific standard time.
    // Get that effect by erasing the is_dst flag out of the local time.
    //
    local_tm.tm_isdst = 0;
  #endif

    // mktime() function inverts localtime()... there is no equivalent for
    // gmtime().  However, we feed it a gmtime() as if it were the localtime.
    // Then the time zone can be calculated by diffing it from a mktime()
    // inversion of a suitable local time.
    //
    // !!! For some reason, R3-Alpha expected the caller to pass in a utc tm
    // structure pointer but then didn't use it, choosing to make another call
    // to gmtime().  Review.
    //
    UNUSED(utc_tm_unused);
    time_t now_secs_gm = mktime(gmtime(&now_secs));

    double diff = difftime(mktime(&local_tm), now_secs_gm);
    return cast(int, diff / 60);
}


//
//  Get_Current_Datetime_Value: C
//
// Get the current system date/time in UTC plus zone offset (mins).
//
Value* Get_Current_Datetime_Value(void)
{
    struct timeval tv;
    struct timezone * const tz_ptr = NULL; // obsolete
    if (gettimeofday(&tv, tz_ptr) != 0)
        rebJumps("fail -{gettimeofday() returned 0}-");

    // tv.tv_sec is the time in seconds 1 January 1970, 00:00:00 UTC
    // (epoch-1970).  It does not account for the time zone.  In POSIX, these
    // values are generally passed around as `time_t`...e.g. functions for
    // converting to local time expect that.
    //
    time_t stime = tv.tv_sec;

    // gmtime() means the same as utc time.  Note we have to be careful as
    // it returns a system static buffer, so we have to copy the result
    // via dereference to avoid calls to localtime() during Get_Timezone()
    // from corrupting the buffer before it gets used.
    //
    // !!! Consider usage of the thread-safe variants, though they are not
    // available on all older systems.
    //
    struct tm utc_tm = *gmtime(&stime);

    int zone = Get_Timezone(&utc_tm);

    return rebValue("ensure date! (make-date-ymdsnz",
        rebI(utc_tm.tm_year + 1900),  // year
        rebI(utc_tm.tm_mon + 1),  // month
        rebI(utc_tm.tm_mday),  // day
        rebI(
            utc_tm.tm_hour * 3600
            + utc_tm.tm_min * 60
            + utc_tm.tm_sec
        ),  // secs
        rebI(tv.tv_usec * 1000),  // nano
        rebI(zone),  // zone
    ")");
}
