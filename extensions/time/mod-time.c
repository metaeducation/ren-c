//
//  File: %mod-time.c
//  Summary: "Time Extension"
//  Section: ports
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


#include "sys-core.h"

#include "tmp-mod-time.h"


extern REBVAL *Get_Current_Datetime_Value(void);

//
//  export now: native [
//
//  "Returns current date and time with timezone adjustment."
//
//      return: [date! time! integer!]
//      /year "Returns year only"
//      /month "Returns month only"
//      /day "Returns day of the month only"
//      /time "Returns time only"
//      /zone "Returns time zone offset from UCT (GMT) only"
//      /date "Returns date only"
//      /weekday "Returns day of the week as integer (Monday is day 1)"
//      /yearday "Returns day of the year (Julian)"
//      /precise "High precision time"
//      /utc "Universal time (zone +0:00)"
//      /local "Give time in current zone without including the time zone"
//  ]
//
REBNATIVE(now)
{
    TIME_INCLUDE_PARAMS_OF_NOW;

    REBVAL *timestamp = Get_Current_Datetime_Value();

    // However OS-level date and time is plugged into the system, it needs to
    // have enough granularity to give back date, time, and time zone.
    //
    assert(IS_DATE(timestamp));
    assert(Does_Date_Have_Time(timestamp));
    assert(Does_Date_Have_Zone(timestamp));

    Copy_Cell(D_OUT, timestamp);
    rebRelease(timestamp);

    if (not REF(precise)) {
        //
        // The "time" field is measured in nanoseconds, and the historical
        // meaning of not using precise measurement was to use only the
        // seconds portion (with the nanoseconds set to 0).  This achieves
        // that by extracting the seconds and then multiplying by nanoseconds.
        //
        PAYLOAD(Time, D_OUT).nanoseconds = SECS_TO_NANO(VAL_SECS(D_OUT));
    }

    if (REF(utc)) {
        //
        // Say it has a time zone component, but it's 0:00 (as opposed
        // to saying it has no time zone component at all?)
        //
        VAL_DATE(D_OUT).zone = 0;
    }
    else if (REF(local)) {
        //
        // Clear out the time zone flag
        //
        VAL_DATE(D_OUT).zone = NO_DATE_ZONE;
    }
    else {
        if (
            REF(year)
            or REF(month)
            or REF(day)
            or REF(time)
            or REF(date)
            or REF(weekday)
            or REF(yearday)
        ){
            Fold_Zone_Into_Date(D_OUT);
        }
    }

    REBINT n = -1;

    if (REF(date)) {
        PAYLOAD(Time, D_OUT).nanoseconds = NO_DATE_TIME;
        VAL_DATE(D_OUT).zone = NO_DATE_ZONE;
    }
    else if (REF(time)) {
        RESET_VAL_HEADER(D_OUT, REB_TIME, CELL_MASK_NONE);
    }
    else if (REF(zone)) {
        PAYLOAD(Time, D_OUT).nanoseconds
            = VAL_ZONE(D_OUT) * ZONE_MINS * MIN_SEC;
        RESET_VAL_HEADER(D_OUT, REB_TIME, CELL_MASK_NONE);
    }
    else if (REF(weekday))
        n = Week_Day(D_OUT);
    else if (REF(yearday))
        n = Julian_Date(VAL_DATE(D_OUT));
    else if (REF(year))
        n = VAL_YEAR(D_OUT);
    else if (REF(month))
        n = VAL_MONTH(D_OUT);
    else if (REF(day))
        n = VAL_DAY(D_OUT);

    if (n > 0)
        Init_Integer(D_OUT, n);

    return D_OUT;
}
