#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>
#undef IS_ERROR

#include <process.h>
#include <assert.h>

#include "sys-core.h"


//
//  Get_Current_Datetime_Value: C
//
// Get the current system date/time in UTC plus zone offset (mins).
//
REBVAL *Get_Current_Datetime_Value(void)
{
    // GetSystemTime() gets the UTC time.  (GetLocalTime() would get the
    // local time, but we instead get the time zone to get the whole picture.)
    //
    SYSTEMTIME stime;
    GetSystemTime(&stime);

    // Note about tzone.Bias:
    //
    //   The bias is the difference, in minutes, between Coordinated Universal
    //   Time (UTC) and local time. All translations between UTC and local time
    //   are based on the following formula:
    //
    //     UTC = local time + bias
    //
    // And about tzone.DaylightBias:
    //
    //   This value is added to the value of the Bias member to form the bias
    //   used during daylight saving time. In most time zones, the value of
    //   this member is –60.
    //
    // The concept in historical Rebol incorporates daylight savings directly
    // into the time zone component of a DATE!.  Hence your time zone appears
    // to change depending on whether it's daylight savings time or not.
    //
    TIME_ZONE_INFORMATION tzone;
    if (TIME_ZONE_ID_DAYLIGHT == GetTimeZoneInformation(&tzone))
        tzone.Bias += tzone.DaylightBias;

    return rebValue("ensure date! (make-date-ymdsnz",
        rebI(stime.wYear),  // year
        rebI(stime.wMonth),  // month
        rebI(stime.wDay),  // day
        rebI(
            stime.wHour * 3600 + stime.wMinute * 60 + stime.wSecond
        ),  // "secs"
        rebI(1000000 * stime.wMilliseconds), // nano
        rebI(-tzone.Bias),  // zone
    ")");
}
