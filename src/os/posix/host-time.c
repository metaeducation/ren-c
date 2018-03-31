//
//  File: %host-time.c
//  Summary: "POSIX Host Time Functions"
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
#include <stdio.h>
#include <poll.h>
#include <fcntl.h>              /* Obtain O_* constant definitions */
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "reb-host.h"


#ifndef timeval // for older systems
#include <sys/time.h>
#endif

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
//  Convert_Date: C
//
// Convert local format of system time into standard date
// and time structure (for date/time and file timestamps).
//
REBVAL *Convert_Date(time_t *stime, long usec)
{
    // gmtime() is badly named.  It's utc time.  Note we have to be careful as
    // it returns a system static buffer, so we have to copy the result
    // via dereference to avoid calls to localtime() inside Get_Timezone
    // from corrupting the buffer before it gets used.
    //
    // !!! Consider usage of the thread-safe variants, though they are not
    // available on all older systems.
    //
    struct tm utc_tm = *gmtime(stime);

    int zone = Get_Timezone(&utc_tm);

    return rebInitDate(
        utc_tm.tm_year + 1900, // year
        utc_tm.tm_mon + 1, // month
        utc_tm.tm_mday, // day
        utc_tm.tm_hour * 3600
            + utc_tm.tm_min * 60
            + utc_tm.tm_sec, // secs
        usec * 1000, // nano
        zone // zone
    );
}


//
//  OS_Get_Time: C
//
// Get the current system date/time in UTC plus zone offset (mins).
//
REBVAL *OS_Get_Time(void)
{
    struct timeval tv;
    struct timezone * const tz_ptr = NULL; // obsolete
    if (gettimeofday(&tv, tz_ptr) != 0)
        assert(FALSE); // should fail(), but not currently included

    // tv.tv_sec is the time in seconds 1 January 1970, 00:00:00 UTC
    // (epoch-1970).  It does not account for the time zone.  In POSIX, these
    // values are generally passed around as `time_t`...e.g. functions for
    // converting to local time expect that.
    //
    time_t stime = tv.tv_sec;

    return Convert_Date(&stime, tv.tv_usec);
}


//
//  OS_Delta_Time: C
//
// Return time difference in microseconds. If base = 0, then
// return the counter. If base != 0, compute the time difference.
//
// NOTE: This needs to be precise, but many OSes do not
// provide a precise time sampling method. So, if the target
// posix OS does, add the ifdef code in here.
//
int64_t OS_Delta_Time(int64_t base)
{
    struct timeval tv;
    gettimeofday(&tv,0);

    int64_t time = cast(int64_t, tv.tv_sec * 1000000) + tv.tv_usec;
    if (base == 0)
        return time;

    return time - base;
}


//
//  OS_File_Time: C
//
// Convert file.time to REBOL date/time format.
// Time zone is UTC.
//
REBVAL *OS_File_Time(struct devreq_file *file)
{
    if (sizeof(time_t) > sizeof(file->time.l)) {
        REBI64 t = file->time.l;
        t |= cast(REBI64, file->time.h) << 32;
        return Convert_Date(cast(time_t*, &t), 0);
    }

    return Convert_Date(cast(time_t *, &file->time.l), 0);
}

