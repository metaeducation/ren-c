//
//  File: %sys-time.h
//  Summary: {Definitions for the TIME! and DATE! Datatypes}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The same payload is used for TIME! and DATE!.  The extra bits needed by
// DATE! (as REBYMD) fit into 32 bits, so can live in the ->extra field,
// which is the size of a platform pointer.
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  DATE!
//
//=////////////////////////////////////////////////////////////////////////=//

#if (! CPLUSPLUS_11)
    #define VAL_DATE(v) \
        EXTRA(Date, (v)).ymdz
#else
    // C++ has reference types--use them and add extra assert it's a date

    inline static const REBYMD & VAL_DATE(REBCEL(const*) v) {
        assert(CELL_KIND(v) == REB_DATE);
        return EXTRA(Date, v).ymdz;
    }

    inline static REBYMD & VAL_DATE(RELVAL *v) {
        assert(VAL_TYPE(v) == REB_DATE);
        return EXTRA(Date, v).ymdz; // const reference
    }
#endif

#define MAX_YEAR 0x3fff

#define VAL_YEAR(v) \
    VAL_DATE(v).year

#define VAL_MONTH(v) \
    VAL_DATE(v).month

#define VAL_DAY(v) \
    VAL_DATE(v).day

#define ZONE_MINS 15

#define ZONE_SECS \
    (ZONE_MINS * 60)

#define MAX_ZONE \
    (15 * (60 / ZONE_MINS))

// All dates have REBYMD information in their ->extra field, but not all
// of them also have associated time information.  This value for the nano
// means there is no time.
//
#define NO_DATE_TIME INT64_MIN

// There is a difference between a time zone of 0 (explicitly GMT) and
// choosing to be an agnostic local time.  This bad value means no time zone.
//
#define NO_DATE_ZONE -64

inline static bool Does_Date_Have_Time(REBCEL(const*) v)
{
    assert(CELL_KIND(v) == REB_DATE);
    if (PAYLOAD(Time, v).nanoseconds == NO_DATE_TIME) {
        assert(VAL_DATE(v).zone == NO_DATE_ZONE);
        return false;
    }
    return true;
}

inline static bool Does_Date_Have_Zone(REBCEL(const*) v)
{
    assert(CELL_KIND(v) == REB_DATE);
    if (VAL_DATE(v).zone == NO_DATE_ZONE)  // out of band of 7-bit field
        return false;
    assert(PAYLOAD(Time, v).nanoseconds != NO_DATE_TIME);
    return true;
}

inline static int VAL_ZONE(REBCEL(const*) v) {
    assert(Does_Date_Have_Zone(v));
    return VAL_DATE(v).zone;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  TIME! (and time component of DATE!s that have times)
//
//=////////////////////////////////////////////////////////////////////////=//

inline static REBI64 VAL_NANO(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_TIME or Does_Date_Have_Time(v));
    return PAYLOAD(Time, v).nanoseconds;
}

#define SECS_TO_NANO(seconds) \
    (cast(REBI64, seconds) * 1000000000L)

#define MAX_SECONDS \
    ((cast(REBI64, 1) << 31) - 1)

#define MAX_HOUR \
    (MAX_SECONDS / 3600)

#define MAX_TIME \
    (cast(REBI64, MAX_HOUR) * HR_SEC)

#define NANO 1.0e-9

#define SEC_SEC \
    cast(REBI64, 1000000000L)

#define MIN_SEC \
    (60 * SEC_SEC)

#define HR_SEC \
    (60 * 60 * SEC_SEC)

#define SEC_TIME(n) \
    ((n) * SEC_SEC)

#define MIN_TIME(n) \
    ((n) * MIN_SEC)

#define HOUR_TIME(n) \
    ((n) * HR_SEC)

#define SECS_FROM_NANO(n) \
    ((n) / SEC_SEC)

#define VAL_SECS(n) \
    (VAL_NANO(n) / SEC_SEC)

#define DEC_TO_SECS(n) \
    cast(REBI64, ((n) + 5.0e-10) * SEC_SEC)

#define SECS_IN_DAY 86400

#define TIME_IN_DAY \
    SEC_TIME(cast(REBI64, SECS_IN_DAY))

inline static REBVAL *Init_Time_Nanoseconds(
    RELVAL *v,
    REBI64 nanoseconds
){
    INIT_VAL_HEADER(v, REB_TIME, CELL_MASK_NONE);
    PAYLOAD(Time, v).nanoseconds = nanoseconds;
    return cast(REBVAL*, v);
}
