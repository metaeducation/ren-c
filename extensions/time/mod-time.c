//
//  file: %mod-time.c
//  summary: "Time Extension"
//  section: ports
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


#include "sys-core.h"
#include "tmp-mod-time.h"


extern Value* Get_Current_Datetime_Value(void);

//
//  export now: native [
//
//  "Returns current date and time with timezone adjustment"
//
//      return: [date! time! integer!]
//      :year "Returns year only"
//      :month "Returns month only"
//      :day "Returns day of the month only"
//      :time "Returns time only"
//      :zone "Returns time zone offset from UCT (GMT) only"
//      :date "Returns date only"
//      :weekday "Returns day of the week as integer (Monday is day 1)"
//      :yearday "Returns day of the year (Julian)"
//      :precise "High precision time"
//      :utc "Universal time (zone +0:00)"
//      :local "Give time in current zone without including the time zone"
//  ]
//
DECLARE_NATIVE(NOW)
{
    INCLUDE_PARAMS_OF_NOW;

    RebolValue* timestamp = Get_Current_Datetime_Value();

    // However OS-level date and time is plugged into the system, it needs to
    // have enough granularity to give back date, time, and time zone.
    //
    assert(Is_Date(timestamp));
    assert(Does_Date_Have_Time(timestamp));
    assert(Does_Date_Have_Zone(timestamp));

    Value* out = Copy_Cell(OUT, timestamp);
    rebRelease(timestamp);

    if (not Bool_ARG(PRECISE)) {
        //
        // The "time" field is measured in nanoseconds, and the historical
        // meaning of not using precise measurement was to use only the
        // seconds portion (with the nanoseconds set to 0).  This achieves
        // that by extracting the seconds and then multiplying by nanoseconds.
        //
        Tweak_Cell_Nanoseconds(out, SECS_TO_NANO(VAL_SECS(out)));
    }

    if (Bool_ARG(UTC)) {
        //
        // Say it has a time zone component, but it's 0:00 (as opposed
        // to saying it has no time zone component at all?)
        //
        VAL_ZONE(out) = 0;
    }
    else if (Bool_ARG(LOCAL)) {
        //
        // Clear out the time zone flag
        //
        VAL_ZONE(out) = NO_DATE_ZONE;
    }
    else {
        if (
            Bool_ARG(YEAR)
            or Bool_ARG(MONTH)
            or Bool_ARG(DAY)
            or Bool_ARG(TIME)
            or Bool_ARG(DATE)
            or Bool_ARG(WEEKDAY)
            or Bool_ARG(YEARDAY)
        ){
            Fold_Zone_Into_Date(out);
        }
    }

    REBINT n = -1;

    if (Bool_ARG(DATE)) {
        Tweak_Cell_Nanoseconds(out, NO_DATE_TIME);
        VAL_ZONE(out) = NO_DATE_ZONE;
    }
    else if (Bool_ARG(TIME)) {
        KIND_BYTE(out) = TYPE_TIME;
    }
    else if (Bool_ARG(ZONE)) {
        Tweak_Cell_Nanoseconds(out, VAL_ZONE(out) * ZONE_MINS * MIN_SEC);
        KIND_BYTE(out) = TYPE_TIME;
    }
    else if (Bool_ARG(WEEKDAY))
        n = Week_Day(out);
    else if (Bool_ARG(YEARDAY))
        n = Julian_Date(out);
    else if (Bool_ARG(YEAR))
        n = VAL_YEAR(out);
    else if (Bool_ARG(MONTH))
        n = VAL_MONTH(out);
    else if (Bool_ARG(DAY))
        n = VAL_DAY(out);

    if (n > 0)
        Init_Integer(out, n);

    return OUT;
}
