//
//  File: %t-date.c
//  Summary: "date datatype"
//  Section: datatypes
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
// Date and time are stored in UTC format with an optional timezone.
// The zone must be added when a date is exported or imported, but not
// when date computations are performed.
//

#include "sys-core.h"


static REBCTX *Error_Bad_Date_Compare(noquote(const Cell*) a, noquote(const Cell*) b)
{
    return Error_Invalid_Compare_Raw(
        cast(const REBVAL*, a),
        cast(const REBVAL*, b)
    );
}


//
//  CT_Date: C
//
REBINT CT_Date(noquote(const Cell*) a, noquote(const Cell*) b, bool strict)
{
    // Dates which lack times or time zones cannot be compared directly with
    // dates that do have times or time zones.  Error on those.
    //
    if (
        Does_Date_Have_Time(a) != Does_Date_Have_Time(b)
        or Does_Date_Have_Zone(a) != Does_Date_Have_Zone(b)
    ){
        fail (Error_Bad_Date_Compare(a, b));
    }

    DECLARE_LOCAL (adjusted_a);
    DECLARE_LOCAL (adjusted_b);

    REBINT tiebreaker = 0;

    if (Does_Date_Have_Zone(a)) {
        assert(Does_Date_Have_Zone(b));  // we checked for matching above

        // If the dates are in different time zones, they have to be canonized
        // to compare them.  But if we're doing strict equality then we can't
        // consider actually equal unless zones the same.

        if (VAL_DATE(a).zone != VAL_DATE(b).zone)
            tiebreaker = VAL_DATE(a).zone > VAL_DATE(b).zone ? 1 : -1;

        Dequotify(Copy_Cell(adjusted_a, SPECIFIC(CELL_TO_VAL(a))));
        Dequotify(Copy_Cell(adjusted_b, SPECIFIC(CELL_TO_VAL(b))));

        Adjust_Date_UTC(adjusted_a);
        Adjust_Date_UTC(adjusted_b);

        a = adjusted_a;
        b = adjusted_b;
    }

    // !!! This comparison doesn't know if it's being asked on behalf of
    // equality or not; and `strict` is passed in as true for plain > and <.
    // In those cases, strictness needs to be accurate for inequality but
    // never side for exact equality unless they really are equal (time zones
    // and all).  This is suboptimal, a redesign is needed:
    //
    // https://forum.rebol.info/t/comparison-semantics/1318
    //

    REBINT days_diff = Days_Between_Dates(  // compare date component first
        cast(const REBVAL*, a),
        cast(const REBVAL*, b)
    );
    if (days_diff != 0)
        return days_diff > 0 ? 1 : -1;

    if (Does_Date_Have_Time(a)) {
        assert(Does_Date_Have_Time(b));  // we checked for matching above

        REBINT time_ct = CT_Time(a, b, strict);  // guaranteed [-1 0 1]
        if (time_ct != 0)
            return time_ct;
    }

    if (strict)
        return tiebreaker;  // don't allow equal unless time zones equal

    return 0;
}


//
//  MF_Date: C
//
void MF_Date(REB_MOLD *mo, noquote(const Cell*) v_orig, bool form)
{
    // We can't/shouldn't modify the incoming date value we are molding, so we
    // make a copy that we can tweak during the emit process

    DECLARE_LOCAL (v);
    Copy_Cell(v, SPECIFIC(CELL_TO_VAL(v_orig)));
    Dequotify(v);  // accessors expect it to not be quoted

    if (
        VAL_MONTH(v) == 0
        or VAL_MONTH(v) > 12
        or VAL_DAY(v) == 0
        or VAL_DAY(v) > 31
    ) {
        Append_Ascii(mo->series, "?date?");
        return;
    }

    // Date bits are stored in canon UTC form.  But for rendering, the year
    // and month and day and time want to integrate the time zone.
    //
    int zone = Does_Date_Have_Zone(v) ? VAL_ZONE(v) : NO_DATE_ZONE;  // capture
    Fold_Zone_Into_Date(v);
    assert(not Does_Date_Have_Zone(v));

    REBYTE dash = GET_MOLD_FLAG(mo, MOLD_FLAG_SLASH_DATE) ? '/' : '-';

    REBYTE buf[64];
    REBYTE *bp = &buf[0];

    bp = Form_Int(bp, cast(REBINT, VAL_DAY(v)));
    *bp++ = dash;
    memcpy(bp, Month_Names[VAL_MONTH(v) - 1], 3);
    bp += 3;
    *bp++ = dash;
    bp = Form_Int_Pad(bp, cast(REBINT, VAL_YEAR(v)), 6, -4, '0');
    *bp = '\0';

    Append_Ascii(mo->series, s_cast(buf));

    if (Does_Date_Have_Time(v)) {
        Append_Codepoint(mo->series, '/');
        MF_Time(mo, v, form);

        if (zone != NO_DATE_ZONE) {
            bp = &buf[0];

            if (zone < 0) {
                *bp++ = '-';
                zone = -zone;
            }
            else
                *bp++ = '+';

            bp = Form_Int(bp, zone / 4);
            *bp++ = ':';
            bp = Form_Int_Pad(bp, (zone & 3) * 15, 2, 2, '0');
            *bp = 0;

            Append_Ascii(mo->series, s_cast(buf));
        }
    }
}


//
//  Month_Length: C
//
// Given a year, determine the number of days in the month.
// Handles all leap year calculations.
//
static REBLEN Month_Length(REBLEN month, REBLEN year)
{
    if (month != 1)
        return Month_Max_Days[month];

    return (
        ((year % 4) == 0) and  // divisible by four is a leap year
        (
            ((year % 100) != 0) or // except when divisible by 100
            ((year % 400) == 0)  // but not when divisible by 400
        )
    ) ? 29 : 28;
}


//
//  Julian_Date: C
//
// Given a year, month and day, return the number of days since the
// beginning of that year.
//
REBLEN Julian_Date(REBYMD date)
{
    REBLEN days;
    REBLEN i;

    days = 0;

    for (i = 0; i < cast(REBLEN, date.month - 1); i++)
        days += Month_Length(i, date.year);

    return date.day + days;
}


//
//  Days_Between_Dates: C
//
// Calculate the (signed) difference in days between two dates.
//
REBINT Days_Between_Dates(const REBVAL *a, const REBVAL *b)
{
    if (Does_Date_Have_Time(a) != Does_Date_Have_Time(b))
        fail (Error_Invalid_Compare_Raw(a, b));

    DECLARE_LOCAL (utc_a);
    DECLARE_LOCAL (utc_b);

    if (Does_Date_Have_Zone(a) != Does_Date_Have_Zone(b))
        fail (Error_Invalid_Compare_Raw(a, b));

    if (Does_Date_Have_Zone(a)) {
        Copy_Cell(utc_a, a);
        Copy_Cell(utc_b, b);

        Adjust_Date_UTC(utc_a);
        Adjust_Date_UTC(utc_b);

        a = utc_a;
        b = utc_b;
    }

    REBYMD d1 = VAL_DATE(a);
    REBYMD d2 = VAL_DATE(b);

    REBINT sign = 1;

    if (d1.year < d2.year)
        sign = -1;
    else if (d1.year == d2.year) {
        if (d1.month < d2.month)
            sign = -1;
        else if (d1.month == d2.month) {
            if (d1.day < d2.day)
                sign = -1;
            else if (d1.day == d2.day)
                return 0;
        }
    }

    if (sign == -1) {
        REBYMD tmp = d1;
        d1 = d2;
        d2 = tmp;
    }

    // if not same year, calculate days to end of month, year and
    // days in between years plus days in end year
    //
    if (d1.year > d2.year) {
        REBLEN days = Month_Length(d2.month-1, d2.year) - d2.day;

        REBLEN m;
        for (m = d2.month; m < 12; m++)
            days += Month_Length(m, d2.year);

        REBLEN y;
        for (y = d2.year + 1; y < d1.year; y++) {
            days += (((y % 4) == 0) and  // divisible by four is a leap year
                (((y % 100) != 0) or  // except when divisible by 100
                ((y % 400) == 0)))  // but not when divisible by 400
                ? 366u : 365u;
        }
        return sign * (REBINT)(days + Julian_Date(d1));
    }

    return sign * (REBINT)(Julian_Date(d1) - Julian_Date(d2));
}


//
//  Week_Day: C
//
// Return the day of the week for a specific date.
//
REBLEN Week_Day(const REBVAL *date)
{
    DECLARE_LOCAL (year1);
    Copy_Cell(year1, date);
    VAL_DATE(year1).year = 0;
    VAL_DATE(year1).month = 1;
    VAL_DATE(year1).day = 1;

    return ((Days_Between_Dates(date, year1) + 5) % 7) + 1;
}


//
//  Normalize_Time: C
//
// Adjust *dp by number of days and set secs to less than a day.
//
void Normalize_Time(REBI64 *sp, REBLEN *dp)
{
    REBI64 secs = *sp;
    assert(secs != NO_DATE_TIME);

    // how many days worth of seconds do we have ?
    //
    REBINT day = cast(REBINT, secs / TIME_IN_DAY);
    secs %= TIME_IN_DAY;

    if (secs < 0L) {
        day--;
        secs += TIME_IN_DAY;
    }

    *dp += day;
    *sp = secs;
}


//
//  Normalize_Date: C
//
// Given a year, month and day, normalize and combine to give a new
// date value.
//
static REBYMD Normalize_Date(REBINT day, REBINT month, REBINT year, REBINT tz)
{
    // First we normalize the month to get the right year

    if (month < 0) {
        year -= (-month + 11) / 12;
        month= 11 - ((-month + 11) % 12);
    }
    if (month >= 12) {
        year += month / 12;
        month %= 12;
    }

    // Now adjust the days by stepping through each month

    REBINT d;
    while (day >= (d = cast(REBINT, Month_Length(month, year)))) {
        day -= d;
        if (++month >= 12) {
            month = 0;
            year++;
        }
    }
    while (day < 0) {
        if (month == 0) {
            month = 11;
            year--;
        }
        else
            month--;
        day += cast(REBINT, Month_Length(month, year));
    }

    if (year < 0 or year > MAX_YEAR)
        fail (Error_Type_Limit_Raw(Datatype_From_Kind(REB_DATE)));

    REBYMD dr;
    dr.year = year;
    dr.month = month + 1;
    dr.day = day + 1;
    dr.zone = tz;
    return dr;
}


//
//  Adjust_Date_Zone_Core: C
//
// If the date and time bits would show a given rendered output for what the
// values would be for the current time zone, then adjust those bits for if
// the given zone were stored in the date.
//
void Adjust_Date_Zone_Core(Cell *d, int zone)
{
    assert(not Does_Date_Have_Zone(d));

    if (zone == NO_DATE_ZONE)
        return;

    REBI64 nano =
        cast(int64_t, - zone)  // !!! this negation of the zone seems necessary
        * (cast(int64_t, ZONE_SECS) * SEC_SEC);

    nano += VAL_NANO(d);

    PAYLOAD(Time, d).nanoseconds = (nano + TIME_IN_DAY) % TIME_IN_DAY;

    REBLEN n = VAL_DAY(d) - 1;

    if (nano < 0)
        --n;
    else if (nano >= TIME_IN_DAY)
        ++n;
    else {
        VAL_DATE(d).zone = zone;  // usually done by Normalize
        return;
    }

    VAL_DATE(d) = Normalize_Date(
        n, VAL_MONTH(d) - 1, VAL_YEAR(d), zone
    );
}


//
//  Fold_Zone_Into_Date: C
//
// Adjust day, month, year and time fields to match the reported timezone.
// The result should be used for output, not stored.
//
// For clarity, the resulting date reports it has *no* time zone information,
// e.g. it considers itself a "local" time to whatever the time zone had been.
// The zone should be captured if it was needed.
//
void Fold_Zone_Into_Date(Cell *d)
{
    if (Does_Date_Have_Zone(d)) {
        int zone = VAL_ZONE(d);
        VAL_DATE(d).zone = NO_DATE_ZONE;
        if (zone != 0)
            Adjust_Date_Zone_Core(d, - zone);
        VAL_DATE(d).zone = NO_DATE_ZONE;
    }
}


//
//  Adjust_Date_UTC: C
//
// Regardless of what time zone a date is in, transform to UTC time (0:00 zone)
//
// !!! It's almost certainly a bad idea to allow dates with no times or time
// zones to be transformed to UTC by assuming they are equivalent to UTC.  If
// anything they should be interpreted as "local" times with the local zone,
// but that seems like something that is better specified explicitly by the
// caller and not assumed by the system.  Review this as it is a new concept
// enabled by differentiating the 0:00 UTC status from "no time zone".
//
void Adjust_Date_UTC(Cell *d)
{
    if (not Does_Date_Have_Time(d)) {
        PAYLOAD(Time, d).nanoseconds = 0;
        VAL_DATE(d).zone = 0;
    }
    else if (not Does_Date_Have_Zone(d)) {
        VAL_DATE(d).zone = 0;
    }
    else {
        int zone = VAL_ZONE(d);
        if (zone != 0) {
            VAL_DATE(d).zone = NO_DATE_ZONE;
            Adjust_Date_Zone_Core(d, - zone);
            VAL_DATE(d).zone = 0;
        }
    }
}


//
//  Time_Between_Dates: C
//
// Called by DIFFERENCE function.
//
REBVAL *Time_Between_Dates(REBVAL *out, const REBVAL *d1, const REBVAL *d2)
{
    // DIFFERENCE is supposed to calculate a time difference, and dates without
    // time components will lead to misleading answers for that.  The user is
    // expected to explicitly ensure that if a 0:00 time is intended as
    // equivalent, that they default to that:
    //
    //     >> t: 3-Jul-2021
    //
    //     >> t.zone: default [0:00]
    //     == 0:00
    //
    //     >> t
    //     == 3-Jul-2021/0:00+0:00
    //
    if (not Does_Date_Have_Time(d1) or not Does_Date_Have_Time(d2))
        fail (Error_Invalid_Compare_Raw(d1, d2));

    REBI64 t1 = VAL_NANO(d1);
    REBI64 t2 = VAL_NANO(d2);

    REBINT diff = Days_Between_Dates(d1, d2);

    // Note: abs() takes `int`, but there is a labs(), and C99 has llabs()
    //
    if (cast(REBLEN, abs(cast(int, diff))) > (((1U << 31) - 1) / SECS_IN_DAY))
        fail (Error_Overflow_Raw());


    return Init_Time_Nanoseconds(
        out,
        (t1 - t2) + (cast(REBI64, diff) * TIME_IN_DAY)
    );
}


//
//  MAKE_Date: C
//
REB_R MAKE_Date(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_DATE);
    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    if (IS_DATE(arg))
        return Copy_Cell(out, arg);

    if (IS_TEXT(arg)) {
        REBSIZ size;
        const REBYTE *bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_DATE);
        if (NULL == Scan_Date(out, bp, size))
            goto bad_make;
        return out;
    }

    if (not ANY_ARRAY(arg))
        goto bad_make;

  blockscope {
    const Cell *tail;
    const Cell *item = VAL_ARRAY_AT(&tail, arg);

    if (item == tail or not IS_INTEGER(item))
        goto bad_make;

    REBLEN day = Int32s(item, 1);

    ++item;
    if (item == tail or not IS_INTEGER(item))
        goto bad_make;

    REBLEN month = Int32s(item, 1);

    ++item;
    if (item == tail or not IS_INTEGER(item))
        goto bad_make;

    REBLEN year;
    if (day > 99) {
        year = day;
        day = Int32s(item, 1);
        ++item;
    }
    else {
        year = Int32s(item, 0);
        ++item;
    }

    if (month < 1 or month > 12)
        goto bad_make;

    if (year > MAX_YEAR or day < 1 or day > Month_Max_Days[month-1])
        goto bad_make;

    // Check February for leap year or century:
    if (month == 2 and day == 29) {
        if (((year % 4) != 0) or        // not leap year
            ((year % 100) == 0 and       // century?
            (year % 400) != 0)) goto bad_make; // not leap century
    }

    day--;
    month--;

    REBI64 secs;
    REBINT tz;
    if (item == tail) {
        secs = NO_DATE_TIME;
        tz = NO_DATE_ZONE;
    }
    else {
        if (not IS_TIME(item))
            goto bad_make;

        secs = VAL_NANO(item);
        ++item;

        if (item == tail)
            tz = NO_DATE_ZONE;
        else {
            if (not IS_TIME(item))
                goto bad_make;

            tz = cast(REBINT, VAL_NANO(item) / (ZONE_MINS * MIN_SEC));
            if (tz < -MAX_ZONE or tz > MAX_ZONE)
                fail (Error_Out_Of_Range(item));
            ++item;

            if (item != tail)
                goto bad_make;
        }
    }

    if (secs != NO_DATE_TIME)
        Normalize_Time(&secs, &day);

    Reset_Cell_Header_Untracked(TRACK(out), REB_DATE, CELL_MASK_NONE);
    VAL_DATE(out) = Normalize_Date(day, month, year, tz);
    PAYLOAD(Time, out).nanoseconds = secs;

    Adjust_Date_UTC(out);
    return out;
  }

  bad_make:
    fail (Error_Bad_Make(REB_DATE, arg));
}


//
//  TO_Date: C
//
REB_R TO_Date(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    return MAKE_Date(out, kind, nullptr, arg);
}


static REBINT Int_From_Date_Arg(const REBVAL *poke) {
    if (IS_INTEGER(poke) or IS_DECIMAL(poke))
        return Int32s(poke, 0);

    if (IS_BLANK(poke))
        return 0;

    fail (poke);
}


//
//  Pick_Or_Poke_Date: C
//
void Pick_Or_Poke_Date(
    option(REBVAL*) opt_out,
    REBVAL *v,
    const Cell *picker,
    option(const REBVAL*) opt_poke
){
    SYMID sym;
    if (IS_WORD(picker)) {
        sym = VAL_WORD_ID(picker); // error later if SYM_0 or not a match
    }
    else if (IS_INTEGER(picker)) {
        switch (Int32(picker)) {
        case 1: sym = SYM_YEAR; break;
        case 2: sym = SYM_MONTH; break;
        case 3: sym = SYM_DAY; break;
        case 4: sym = SYM_TIME; break;
        case 5: sym = SYM_ZONE; break;
        case 6: sym = SYM_DATE; break;
        case 7: sym = SYM_WEEKDAY; break;
        case 8: sym = SYM_JULIAN; break; // a.k.a. SYM_YEARDAY
        case 9: sym = SYM_UTC; break;
        case 10: sym = SYM_HOUR; break;
        case 11: sym = SYM_MINUTE; break;
        case 12: sym = SYM_SECOND; break;
        default:
            fail (picker);
        }
    }
    else
        fail (picker);

    // When a date has a time zone on it, then this can distort the integer
    // value of the month/day/year that is seen in rendering from what is
    // stored.  (So you might see the day as the 2nd, when VAL_DAY() is
    // actually 3.)  We extract the original values so we have them if we
    // need them (e.g if asked for the UTC or zone) and adjust.
    //
    DECLARE_LOCAL (adjusted);
    Copy_Cell(adjusted, v);
    Fold_Zone_Into_Date(adjusted);
    assert(not Does_Date_Have_Zone(adjusted));

    REBINT day = VAL_DAY(adjusted);
    REBINT month = VAL_MONTH(adjusted);
    REBINT year = VAL_YEAR(adjusted);
    REBI64 nano = Does_Date_Have_Time(adjusted)
        ? VAL_NANO(adjusted)
        : NO_DATE_TIME;
    REBINT zone = Does_Date_Have_Zone(v)  // original...can be changed by poke
        ? VAL_ZONE(v)
        : NO_DATE_ZONE;

    if (not opt_poke) {
        assert(opt_out);
        REBVAL *out = unwrap(opt_out);

        switch (sym) {
          case SYM_YEAR:
            Init_Integer(out, year);  // tz adjusted year
            break;

          case SYM_MONTH:
            Init_Integer(out, month);  // tz adjusted month
            break;

          case SYM_DAY:
            Init_Integer(out, day);  // tz adjusted day
            break;

          case SYM_TIME:
            if (not Does_Date_Have_Time(v))
                Init_Nulled(out);
            else
                Init_Time_Nanoseconds(out, nano);  // tz adjusted nano
            break;

          case SYM_ZONE:
            if (not Does_Date_Have_Zone(v))  // un-adjusted zone (obviously!)
                Init_Nulled(out);
            else
                Init_Time_Nanoseconds(
                    out,
                    cast(int64_t, VAL_ZONE(v)) * ZONE_MINS * MIN_SEC
                );
            break;

          case SYM_DATE: {
            Copy_Cell(out, adjusted);  // want the adjusted date
            PAYLOAD(Time, out).nanoseconds = NO_DATE_TIME;  // with no time
            assert(VAL_DATE(out).zone == NO_DATE_ZONE);  // time zone removed
            break; }

          case SYM_WEEKDAY:
            Init_Integer(out, Week_Day(adjusted));  // adjusted date
            break;

          case SYM_JULIAN:
          case SYM_YEARDAY:
            Init_Integer(out, cast(REBINT, Julian_Date(VAL_DATE(adjusted))));
            break;

          case SYM_UTC: {
            if (not Does_Date_Have_Time(v) or not Does_Date_Have_Zone(v))
                fail ("DATE! must have /TIME and /ZONE components to get UTC");

            // We really just want the original un-adjusted stored time but
            // with the time zone component set to 0:00
            //
            Move_Cell(out, v);
            VAL_DATE(out).zone = 0;  // GMT
            break; }

          case SYM_HOUR:
            if (not Does_Date_Have_Time(v))
                Init_Nulled(out);
            else {
                REB_TIMEF time;
                Split_Time(nano, &time);  // tz adjusted time
                Init_Integer(out, time.h);
            }
            break;

          case SYM_MINUTE:
            if (not Does_Date_Have_Time(v))
                Init_Nulled(out);
            else {
                REB_TIMEF time;
                Split_Time(nano, &time);  // tz adjusted time
                Init_Integer(out, time.m);
            }
            break;

          case SYM_SECOND:
            if (not Does_Date_Have_Time(v))
                Init_Nulled(out);
            else {
                REB_TIMEF time;
                Split_Time(nano, &time);  // tz adjusted time
                if (time.n == 0)
                    Init_Integer(out, time.s);
                else
                    Init_Decimal(
                        out,
                        cast(REBDEC, time.s) + (time.n * NANO)
                    );
            }
            break;

          default:
            Init_Nulled(out);  // "out of range" PICK semantics
        }
    }
    else {
        assert(not opt_out);
        const REBVAL *poke = unwrap(opt_poke);

        // Here the desire is to modify the incoming date directly.  This is
        // done by changing the components that need to change which were
        // extracted, and building a new date out of the parts.
        //
        // The modifications are done to the time zone adjusted fields, and
        // then the time is fixed back.

        switch (sym) {
          case SYM_YEAR:
            year = Int_From_Date_Arg(poke);
            break;

          case SYM_MONTH:
            month = Int_From_Date_Arg(poke);
            if (month < 1 or month > 12)
                fail (Error_Out_Of_Range(poke));
            break;

          case SYM_DAY:
            day = Int_From_Date_Arg(poke);
            if (
                day < 1
                or day > cast(REBINT, Month_Length(VAL_MONTH(v), VAL_YEAR(v)))
            ){
                fail (Error_Out_Of_Range(poke));
            }
            break;

          case SYM_TIME:
            if (IS_NULLED(poke)) {  // clear out the time component
                nano = NO_DATE_TIME;
                zone = NO_DATE_ZONE;
            }
            else if (IS_TIME(poke) or IS_DATE(poke))
                nano = VAL_NANO(poke);
            else if (IS_INTEGER(poke))
                nano = Int_From_Date_Arg(poke) * SEC_SEC;
            else if (IS_DECIMAL(poke))
                nano = DEC_TO_SECS(VAL_DECIMAL(poke));
            else
                fail (poke);

            PAYLOAD(Time, v).nanoseconds = nano;

          check_nanoseconds:
            if (
                nano != NO_DATE_TIME
                and (nano < 0 or nano >= SECS_TO_NANO(24 * 60 * 60))
            ){
                fail (Error_Out_Of_Range(poke));
            }
            break;

          case SYM_ZONE:
            if (IS_NULLED(poke)) {  // clear out the zone component
                zone = NO_DATE_ZONE;
            }
            else {
                // Make it easier to turn a time into one that math can be
                // done on by letting you set the time zone even if it does
                // not have a time component.  Will become 00:00:00
                //
                if (not Does_Date_Have_Time(v))
                    nano = 0;

                if (IS_TIME(poke))
                    zone = cast(REBINT, VAL_NANO(poke) / (ZONE_MINS * MIN_SEC));
                else if (IS_DATE(poke))
                    zone = VAL_ZONE(poke);
                else
                    zone = Int_From_Date_Arg(poke) * (60 / ZONE_MINS);
                if (zone > MAX_ZONE or zone < -MAX_ZONE)
                    fail (Error_Out_Of_Range(poke));
            }
            break;

          case SYM_JULIAN:
          case SYM_WEEKDAY:
          case SYM_UTC:
            fail (picker);

          case SYM_DATE: {
            if (not IS_DATE(poke))
                fail (poke);

            // We want to adjust the date being poked, so the year/month/day
            // that the user sees is the one reflected.  Safest is to work in
            // UTC instead of mixing and matching :-/ but if you're going to
            // mix then visual consistency gives the most coherent experience.
            //
            // (It could also be an error if the time zones don't line up)

            DECLARE_LOCAL (poke_adjusted);
            Copy_Cell(poke_adjusted, poke);
            Fold_Zone_Into_Date(poke_adjusted);
            assert(not Does_Date_Have_Zone(poke_adjusted));

            year = VAL_YEAR(poke_adjusted);
            month = VAL_MONTH(poke_adjusted);
            day = VAL_DAY(poke_adjusted);
            break; }

          case SYM_HOUR: {
            if (not Does_Date_Have_Time(v))
                nano = 0;  // allow assignment if no prior time component

            REB_TIMEF time;
            Split_Time(nano, &time);
            time.h = Int_From_Date_Arg(poke);
            nano = Join_Time(&time, false);
            goto check_nanoseconds; }

          case SYM_MINUTE: {
            if (not Does_Date_Have_Time(v))
                nano = 0;  // allow assignment if no prior time component

            REB_TIMEF time;
            Split_Time(nano, &time);
            time.m = Int_From_Date_Arg(poke);
            nano = Join_Time(&time, false);
            goto check_nanoseconds; }

          case SYM_SECOND: {
            if (not Does_Date_Have_Time(v))
                nano = 0;  // allow assignment if no prior time component

            REB_TIMEF time;
            Split_Time(nano, &time);
            if (IS_INTEGER(poke)) {
                time.s = Int_From_Date_Arg(poke);
                time.n = 0;
            }
            else {
                time.s = cast(REBINT, VAL_DECIMAL(poke));
                time.n = cast(REBINT,
                    (VAL_DECIMAL(poke) - time.s) * SEC_SEC);
            }
            nano = Join_Time(&time, false);
            goto check_nanoseconds; }

          default:
            fail (picker);
        }

        // R3-Alpha went through a shady process of "normalization" if you
        // created an invalid date/time combination.  So if you have February
        // 29 in a non-leap year, it would adjust that to be March 1st.  That
        // code was basically reusing the code from date math on fieldwise
        // assignment.  Consensus was to error on invalid dates instead:
        //
        // https://forum.rebol.info/t/240/
        //
        VAL_DATE(v).year = year;
        VAL_DATE(v).month = month;
        VAL_DATE(v).day = day;
        VAL_DATE(v).zone = NO_DATE_ZONE;  // to be adjusted
        PAYLOAD(Time, v).nanoseconds = nano;  // may be NO_DATE_TIME

        // This is not a canon stored date, so we have to take into account
        // the separated zone variable (which may have been changed or cleared).

        if (zone != NO_DATE_ZONE)
            Adjust_Date_Zone_Core(v, zone);
    }
}


//
//  REBTYPE: C
//
REBTYPE(Date)
{
    REBVAL *v = D_ARG(1);
    assert(IS_DATE(v));

    SYMID id = ID_OF_SYMBOL(verb);

    REBYMD date = VAL_DATE(v);
    REBLEN day = VAL_DAY(v) - 1;
    REBLEN month = VAL_MONTH(v) - 1;
    REBLEN year = VAL_YEAR(v);
    REBI64 secs = Does_Date_Have_Time(v) ? VAL_NANO(v) : NO_DATE_TIME;

    if (id == SYM_PICK_P) {

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Cell *picker = ARG(picker);

        Pick_Or_Poke_Date(OUT, v, picker, nullptr);
        return OUT;
    }
    else if (id == SYM_POKE_P) {

    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        const Cell *picker = ARG(picker);

        REBVAL *setval = Meta_Unquotify(ARG(value));

        Pick_Or_Poke_Date(nullptr, v, picker, setval);

        // This is a case where the bits are stored in the cell, so
        // whoever owns this cell has to write it back.
        //
        return_value (v);
    }

    if (id == SYM_SUBTRACT or id == SYM_ADD) {
        REBVAL *arg = D_ARG(2);
        REBINT type = VAL_TYPE(arg);

        if (type == REB_DATE) {
            if (id == SYM_SUBTRACT)
                return Init_Integer(OUT, Days_Between_Dates(v, arg));
        }
        else if (type == REB_TIME) {
            if (id == SYM_ADD) {
                if (secs == NO_DATE_TIME)
                    secs = 0;
                secs += VAL_NANO(arg);
                goto fix_time;
            }
            if (id == SYM_SUBTRACT) {
                if (secs == NO_DATE_TIME)
                    secs = 0;
                secs -= VAL_NANO(arg);
                goto fix_time;
            }
        }
        else if (type == REB_INTEGER) {
            REBINT num = Int32(arg);
            if (id == SYM_ADD) {
                day += num;
                goto fix_date;
            }
            if (id == SYM_SUBTRACT) {
                day -= num;
                goto fix_date;
            }
        }
        else if (type == REB_DECIMAL) {
            REBDEC dec = Dec64(arg);
            if (id == SYM_ADD) {
                if (secs == NO_DATE_TIME)
                    secs = 0;
                secs += cast(REBI64, dec * TIME_IN_DAY);
                goto fix_time;
            }
            if (id == SYM_SUBTRACT) {
                if (secs == NO_DATE_TIME)
                    secs = 0;
                secs -= cast(REBI64, dec * TIME_IN_DAY);
                goto fix_time;
            }
        }
    }
    else {
        switch (id) {
          case SYM_COPY:
            return_value (v);  // immediate type, just copy bits

          case SYM_EVEN_Q:
            return Init_Logic(OUT, ((~day) & 1) == 0);

          case SYM_ODD_Q:
            return Init_Logic(OUT, (day & 1) == 0);

          case SYM_RANDOM: {
            INCLUDE_PARAMS_OF_RANDOM;
            UNUSED(PAR(value));

            if (REF(only))
                fail (Error_Bad_Refines_Raw());

            const bool secure = did REF(secure);

            if (REF(seed)) {
                //
                // Note that nsecs not set often for dates (requires /precise)
                //
                Set_Random(
                    (cast(REBI64, year) << 48)
                    + (cast(REBI64, Julian_Date(date)) << 32)
                    + secs
                );
                return nullptr;
            }

            if (year == 0) break;

            year = cast(REBLEN, Random_Range(year, secure));
            month = cast(REBLEN, Random_Range(12, secure));
            day = cast(REBLEN, Random_Range(31, secure));

            if (secs != NO_DATE_TIME)
                secs = Random_Range(TIME_IN_DAY, secure);

            goto fix_date; }

          case SYM_ABSOLUTE:
            goto set_date;

          case SYM_DIFFERENCE: {
            INCLUDE_PARAMS_OF_DIFFERENCE;

            REBVAL *val1 = ARG(value1);
            REBVAL *val2 = ARG(value2);

            if (REF(case))
                fail (Error_Bad_Refines_Raw());

            if (REF(skip))
                fail (Error_Bad_Refines_Raw());

            // !!! Plain SUBTRACT on dates has historically given INTEGER! of
            // days, while DIFFERENCE has given back a TIME!.  This is not
            // consistent with the "symmetric difference" that all other
            // applications of difference are for.  Review.
            //
            // https://forum.rebol.info/t/486
            //
            if (not IS_DATE(val2))
                fail (Error_Unexpected_Type(VAL_TYPE(val1), VAL_TYPE(val2)));

            return Time_Between_Dates(OUT, val1, val2); }

          default:
            break;
        }
    }

    return R_UNHANDLED;

  fix_time:
    Normalize_Time(&secs, &day);

  fix_date:
    date = Normalize_Date(
        day,
        month,
        year,
        Does_Date_Have_Zone(v) ? VAL_ZONE(v) : 0
    );

  set_date:
    Reset_Cell_Header_Untracked(TRACK(OUT), REB_DATE, CELL_MASK_NONE);
    VAL_DATE(OUT) = date;
    PAYLOAD(Time, OUT).nanoseconds = secs; // may be NO_DATE_TIME
    if (secs == NO_DATE_TIME)
        VAL_DATE(OUT).zone = NO_DATE_ZONE;
    return OUT;
}


//
//  make-date-ymdsnz: native [
//
//  {Make a date from Year, Month, Day, Seconds, Nanoseconds, time Zone}
//
//      return: [date!]
//      year [integer!]
//          "full integer, e.g. 1975"
//      month [integer!]
//          "1 is January, 12 is December"
//      day [integer!]
//          "1 to 31"
//      seconds [integer!]
//          "3600 for each hour, 60 for each minute"
//      nano [blank! integer!]
//      zone [blank! integer!]
//  ]
//
REBNATIVE(make_date_ymdsnz)
//
// !!! This native exists to avoid adding specialized C routines to the API
// for the purposes of date creation in NOW.  Ideally there would be a nicer
// syntax via MAKE TIME!, which could use other enhancements:
//
// https://github.com/rebol/rebol-issues/issues/2313
//
{
    INCLUDE_PARAMS_OF_MAKE_DATE_YMDSNZ;

    Reset_Cell_Header_Untracked(OUT, REB_DATE, CELL_MASK_NONE);
    VAL_YEAR(OUT) = VAL_INT32(ARG(year));
    VAL_MONTH(OUT) = VAL_INT32(ARG(month));
    VAL_DAY(OUT) = VAL_INT32(ARG(day));

    if (IS_BLANK(ARG(zone)))
        VAL_DATE(OUT).zone = NO_DATE_ZONE;
    else
        VAL_DATE(OUT).zone = VAL_INT32(ARG(zone)) / ZONE_MINS;

    REBI64 nano = IS_BLANK(ARG(nano)) ? 0 : VAL_INT64(ARG(nano));
    PAYLOAD(Time, OUT).nanoseconds
        = SECS_TO_NANO(VAL_INT64(ARG(seconds))) + nano;

    assert(Does_Date_Have_Time(OUT));
    return OUT;
}


//
//  make-time-sn: native [
//
//  {Make a TIME! from Seconds and Nanoseconds}
//
//      return: [time!]
//      seconds "3600 for each hour, 60 for each minute"
//          [integer!]
//      nano "Nanoseconds"
//          [blank! integer!]
//  ]
//
REBNATIVE(make_time_sn)
//
// !!! The MAKE TIME! as defined by historical Rebol lacked granularity to
// to add fractions of seconds (it was `make time! [hour minutes seconds]`).
// This primitive is added to facilitate implementation of NOW/TIME/PRECISE
// in the near term without committing anything new about MAKE TIME! [].
//
// https://github.com/rebol/rebol-issues/issues/2313
//
// !!! Is there a reason why time zones can only be put on times when they
// are coupled with a DATE! ?
{
    INCLUDE_PARAMS_OF_MAKE_TIME_SN;

    Reset_Cell_Header_Untracked(TRACK(OUT), REB_TIME, CELL_MASK_NONE);

    REBI64 nano = IS_BLANK(ARG(nano)) ? 0 : VAL_INT64(ARG(nano));
    PAYLOAD(Time, OUT).nanoseconds
        = SECS_TO_NANO(VAL_INT64(ARG(seconds))) + nano;

    return OUT;
}
