//
//  File: %cell-time.h
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
// DATE! fit into 32 bits, so can live in the ->extra field, which is the
// size of a platform pointer.
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  DATE!
//
//=////////////////////////////////////////////////////////////////////////=//

#if (! DEBUG_USE_CELL_SUBCLASSES)
    #define Ensure_Date(v) \
        (v)
#else
    INLINE const Cell* Ensure_Date(const Cell* v) {
        assert(Cell_Heart(v) == REB_DATE);
        return v;
    }

    INLINE Element* Ensure_Date(Atom* v) {
        assert(VAL_TYPE(v) == REB_DATE);
        return cast(Element*, v);
    }
#endif

#define MAX_YEAR 0x3fff

#define VAL_YEAR(v) \
    EXTRA(Date, Ensure_Date(v)).year

#define VAL_MONTH(v) \
    EXTRA(Date, Ensure_Date(v)).month

#define VAL_DAY(v) \
    EXTRA(Date, Ensure_Date(v)).day

#define ZONE_MINS 15

#define ZONE_SECS \
    (ZONE_MINS * 60)

#define MAX_ZONE \
    (15 * (60 / ZONE_MINS))

// All dates have year/month/day information in their ->extra field, but not
// all of them also have associated time information.  This value for the nano
// means there is no time.
//
#define NO_DATE_TIME INT64_MIN

// There is a difference between a time zone of 0 (explicitly GMT) and
// choosing to be an agnostic local time.  This bad value means no time zone.
//
#define NO_DATE_ZONE -64

INLINE bool Does_Date_Have_Time(const Cell* v)
{
    assert(Cell_Heart(v) == REB_DATE);
    if (PAYLOAD(Time, v).nanoseconds == NO_DATE_TIME) {
        assert(EXTRA(Date, v).zone == NO_DATE_ZONE);
        return false;
    }
    return true;
}

INLINE bool Does_Date_Have_Zone(const Cell* v)
{
    assert(Cell_Heart(v) == REB_DATE);
    if (EXTRA(Date, v).zone == NO_DATE_ZONE)  // out of band of 7-bit field
        return false;
    assert(PAYLOAD(Time, v).nanoseconds != NO_DATE_TIME);
    return true;
}

#if (! CPLUSPLUS_11) || (! DEBUG)
    #define VAL_ZONE(v) \
        EXTRA(Date, Ensure_Date(v)).zone
#else
    template<typename TP>
    struct ZoneHolder {
        typedef typename std::remove_pointer<TP>::type T;
        T* cell;

        ZoneHolder(T* cell) : cell (cell)
          { assert(Cell_Heart(cell) == REB_DATE); }

        operator int () {  // stop accidental reads of NO_DATE_ZONE
            assert(EXTRA(Date, cell).zone != NO_DATE_ZONE);
            return EXTRA(Date, cell).zone;
        }

        template<
            typename U = T,
            typename std::enable_if<not std::is_const<U>::value, int>::type = 0
        >
        void operator=(int zone) {  // zone writes allow NO_DATE_ZONE
            EXTRA(Date, cell).zone = zone;
        }

    };
    #define VAL_ZONE(v) ZoneHolder<decltype(v)>{v}
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  TIME! (and time component of DATE!s that have times)
//
//=////////////////////////////////////////////////////////////////////////=//

INLINE REBI64 VAL_NANO(const Cell* v) {
    assert(Cell_Heart(v) == REB_TIME or Does_Date_Have_Time(v));
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

INLINE REBVAL *Init_Time_Nanoseconds(
    Cell* v,
    REBI64 nanoseconds
){
    Reset_Unquoted_Header_Untracked(v, CELL_MASK_TIME);
    PAYLOAD(Time, v).nanoseconds = nanoseconds;
    return cast(REBVAL*, v);
}
