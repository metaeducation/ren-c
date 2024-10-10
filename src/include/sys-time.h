//
//  File: %sys-time.h
//  Summary: {Definitions for the TIME! and DATE! Datatypes}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
//

// `CELL_FLAG_DATE_HAS_TIME` answers whether a date's Reb_Time payload is valid.
// All dates have REBYMD information in their ->extra field, but not all
// of them also have associated time information.
//
#define CELL_FLAG_DATE_HAS_TIME \
    FLAG_TYPE_SPECIFIC_BIT(0)

// `CELL_FLAG_DATE_HAS_ZONE` tells whether a date's time zone bits are valid.
// There is a difference between a time zone of 0 (explicitly GMT) and
// choosing to be an agnostic local time.
//
#define CELL_FLAG_DATE_HAS_ZONE \
    FLAG_TYPE_SPECIFIC_BIT(1)


//=////////////////////////////////////////////////////////////////////////=//
//
//  TIME! (and time component of DATE!s that have times)
//
//=////////////////////////////////////////////////////////////////////////=//

#if defined(NDEBUG) || (! CPLUSPLUS_11)
    #define VAL_NANO(v) \
        ((v)->payload.time.nanoseconds)
#else
    INLINE REBI64 VAL_NANO(const Cell* v) {
        assert(
            Is_Time(v) or (Is_Date(v) and Get_Cell_Flag(v, DATE_HAS_TIME))
        );
        return v->payload.time.nanoseconds;
    }

    INLINE REBI64 &VAL_NANO(Cell* v) {
        assert(
            Is_Time(v) or (Is_Date(v) and Get_Cell_Flag(v, DATE_HAS_TIME))
        );
        return v->payload.time.nanoseconds;
    }
#endif

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

INLINE Value* Init_Time_Nanoseconds(Cell* v, REBI64 nanoseconds) {
    RESET_CELL(v, REB_TIME);
    VAL_NANO(v) = nanoseconds;
    return cast(Value*, v);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DATE!
//
//=////////////////////////////////////////////////////////////////////////=//

#define VAL_DATE(v) \
    ((v)->extra.date)

#define MAX_YEAR 0x3fff

#define VAL_YEAR(v) \
    ((v)->extra.date.date.year)

#define VAL_MONTH(v) \
    ((v)->extra.date.date.month)

#define VAL_DAY(v) \
    ((v)->extra.date.date.day)


// Note: can't use reference trick as with VAL_NANO() above to allow using
// VAL_ZONE() as an lvalue, because it is a bit field.
//
INLINE int VAL_ZONE(const Cell* v) {
    assert(Is_Date(v) && Get_Cell_Flag(v, DATE_HAS_ZONE));
    return v->extra.date.date.zone;
}

INLINE void INIT_VAL_ZONE(Cell* v, int zone) {
    assert(Is_Date(v) && Get_Cell_Flag(v, DATE_HAS_ZONE));
    v->extra.date.date.zone = zone;
}

#define ZONE_MINS 15

#define ZONE_SECS \
    (ZONE_MINS * 60)

#define MAX_ZONE \
    (15 * (60 / ZONE_MINS))
