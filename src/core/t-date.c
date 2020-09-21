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


//
//  CT_Date: C
//
REBINT CT_Date(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    REBYMD dat_a = VAL_DATE(a);
    REBYMD dat_b = VAL_DATE(b);

    if (strict) {
        if (Does_Date_Have_Zone(a)) {
            if (not Does_Date_Have_Zone(b))
                return 1;  // can't be equal

            if (
                dat_a.year != dat_b.year
                or dat_a.month != dat_b.month
                or dat_a.day != dat_b.year
                or dat_a.zone != dat_b.zone
            ){
                return 1;  // both have zones, all bits must be equal
            }
        }
        else {
            if (Does_Date_Have_Zone(b))
                return 1;  // a doesn't have, b does, can't be equal

            if (
                dat_a.year != dat_b.year
                or dat_a.month != dat_b.month
                or dat_a.day != dat_b.day
                // old code here ignored .zone
            ){
                return 1;  // canonized to 0 zone not equal
            }
        }

        if (Does_Date_Have_Time(a)) {
            if (not Does_Date_Have_Time(b))
                return 1;  // can't be equal;

            if (VAL_NANO(a) != VAL_NANO(b))
                return 1;  // both have times, all bits must be equal
        }
        else {
            if (Does_Date_Have_Time(b))
                return 1; // a doesn't have, b, does, can't be equal

            // neither have times so equal
        }
        return 0;
    }

    REBINT diff = Diff_Date(VAL_DATE(a), VAL_DATE(b));
    if (diff != 0)
        return diff;

    if (not Does_Date_Have_Time(a)) {
        if (not Does_Date_Have_Time(b))
            return 0;  // equal if no diff and neither has a time

        return -1;  // b is bigger if no time on a
    }

    if (not Does_Date_Have_Time(b))
        return 1;  // a is bigger if no time on b

    return CT_Time(a, b, strict);
}


//
//  MF_Date: C
//
void MF_Date(REB_MOLD *mo, REBCEL(const*) v_orig, bool form)
{
    // We can't/shouldn't modify the incoming date value we are molding, so we
    // make a copy that we can tweak during the emit process

    DECLARE_LOCAL (v);
    Move_Value(v, SPECIFIC(CELL_TO_VAL(v_orig)));

    if (
        VAL_MONTH(v) == 0
        or VAL_MONTH(v) > 12
        or VAL_DAY(v) == 0
        or VAL_DAY(v) > 31
    ) {
        Append_Ascii(mo->series, "?date?");
        return;
    }

    if (Does_Date_Have_Zone(v)) {
        const bool to_utc = false;
        Adjust_Date_Zone(v, to_utc);
    }

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

        if (Does_Date_Have_Zone(v)) {
            bp = &buf[0];

            REBINT tz = VAL_ZONE(v);
            if (tz < 0) {
                *bp++ = '-';
                tz = -tz;
            }
            else
                *bp++ = '+';

            bp = Form_Int(bp, tz / 4);
            *bp++ = ':';
            bp = Form_Int_Pad(bp, (tz & 3) * 15, 2, 2, '0');
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
//  Diff_Date: C
//
// Calculate the difference in days between two dates.
//
REBINT Diff_Date(REBYMD d1, REBYMD d2)
{
    // !!! Time zones (and times) throw a wrench into this calculation.
    // This just keeps R3-Alpha behavior going as flaky as it was,
    // and doesn't heed the time zones.

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
REBLEN Week_Day(REBYMD date)
{
    REBYMD year1;
    CLEARS(&year1);
    year1.day = 1;
    year1.month = 1;

    return ((Diff_Date(date, year1) + 5) % 7) + 1;
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
//  Adjust_Date_Zone: C
//
// Adjust date and time for the timezone.
// The result should be used for output, not stored.
//
void Adjust_Date_Zone(RELVAL *d, bool to_utc)
{
    if (not Does_Date_Have_Zone(d))
        return;

    if (not Does_Date_Have_Time(d)) {
        VAL_DATE(d).zone = NO_DATE_ZONE; // !!! Is this necessary?
        return;
    }

    // (compiler should fold the constant)

    REBI64 secs =
        cast(int64_t, VAL_ZONE(d)) * (cast(int64_t, ZONE_SECS) * SEC_SEC);
    if (to_utc)
        secs = -secs;
    secs += VAL_NANO(d);

    PAYLOAD(Time, d).nanoseconds = (secs + TIME_IN_DAY) % TIME_IN_DAY;

    REBLEN n = VAL_DAY(d) - 1;

    if (secs < 0)
        --n;
    else if (secs >= TIME_IN_DAY)
        ++n;
    else
        return;

    VAL_DATE(d) = Normalize_Date(
        n, VAL_MONTH(d) - 1, VAL_YEAR(d), VAL_ZONE(d)
    );
}


//
//  Subtract_Date: C
//
// Called by DIFFERENCE function.
//
void Subtract_Date(REBVAL *d1, REBVAL *d2, REBVAL *result)
{
    REBINT diff = Diff_Date(VAL_DATE(d1), VAL_DATE(d2));

    // Note: abs() takes `int`, but there is a labs(), and C99 has llabs()
    //
    if (cast(REBLEN, abs(cast(int, diff))) > (((1U << 31) - 1) / SECS_IN_DAY))
        fail (Error_Overflow_Raw());

    REBI64 t1;
    if (Does_Date_Have_Time(d1))
        t1 = VAL_NANO(d1);
    else
        t1 = 0L;

    REBI64 t2;
    if (Does_Date_Have_Time(d2))
        t2 = VAL_NANO(d2);
    else
        t2 = 0L;

    Init_Time_Nanoseconds(
        result,
        (t1 - t2) + (cast(REBI64, diff) * TIME_IN_DAY)
    );
}


//
//  MAKE_Date: C
//
REB_R MAKE_Date(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_DATE);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (IS_DATE(arg))
        return Move_Value(out, arg);

    if (IS_TEXT(arg)) {
        REBSIZ size;
        const REBYTE *bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_DATE);
        if (NULL == Scan_Date(out, bp, size))
            goto bad_make;
        return out;
    }

    if (ANY_ARRAY(arg) and VAL_ARRAY_LEN_AT(arg) >= 3) {
        const RELVAL *item = VAL_ARRAY_AT(arg);
        if (not IS_INTEGER(item))
            goto bad_make;

        REBLEN day = Int32s(item, 1);

        ++item;
        if (not IS_INTEGER(item))
            goto bad_make;

        REBLEN month = Int32s(item, 1);

        ++item;
        if (not IS_INTEGER(item))
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
        if (IS_END(item)) {
            secs = NO_DATE_TIME;
            tz = NO_DATE_ZONE;
        }
        else {
            if (not IS_TIME(item))
                goto bad_make;

            secs = VAL_NANO(item);
            ++item;

            if (IS_END(item))
                tz = NO_DATE_ZONE;
            else {
                if (not IS_TIME(item))
                    goto bad_make;

                tz = cast(REBINT, VAL_NANO(item) / (ZONE_MINS * MIN_SEC));
                if (tz < -MAX_ZONE or tz > MAX_ZONE)
                    fail (Error_Out_Of_Range(SPECIFIC(item)));
                ++item;
            }
        }

        if (NOT_END(item))
            goto bad_make;

        if (secs != NO_DATE_TIME)
            Normalize_Time(&secs, &day);

        RESET_CELL(out, REB_DATE, CELL_MASK_NONE);
        VAL_DATE(out) = Normalize_Date(day, month, year, tz);
        PAYLOAD(Time, out).nanoseconds = secs;

        const bool to_utc = true;
        Adjust_Date_Zone(out, to_utc);
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


static REBINT Int_From_Date_Arg(const REBVAL *opt_poke) {
    if (IS_INTEGER(opt_poke) or IS_DECIMAL(opt_poke))
        return Int32s(opt_poke, 0);

    if (IS_BLANK(opt_poke))
        return 0;

    fail (opt_poke);
}


//
//  Pick_Or_Poke_Date: C
//
void Pick_Or_Poke_Date(
    REBVAL *opt_out,
    REBVAL *v,
    const RELVAL *picker,
    const REBVAL *opt_poke
){
    REBSYM sym;
    if (IS_WORD(picker)) {
        sym = VAL_WORD_SYM(picker); // error later if SYM_0 or not a match
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
            fail (SPECIFIC(picker));
        }
    }
    else
        fail (rebUnrelativize(picker));

    if (opt_poke == NULL) {
        assert(opt_out != NULL);
        TRASH_CELL_IF_DEBUG(opt_out);

        switch (sym) {
        case SYM_YEAR:
            Init_Integer(opt_out, VAL_YEAR(v));
            break;

        case SYM_MONTH:
            Init_Integer(opt_out, VAL_MONTH(v));
            break;

        case SYM_DAY:
            Init_Integer(opt_out, VAL_DAY(v));
            break;

        case SYM_TIME:
            if (not Does_Date_Have_Time(v))
                Init_Nulled(opt_out);
            else {
                Move_Value(opt_out, v); // want v's adjusted VAL_NANO()
                Adjust_Date_Zone(opt_out, false);
                RESET_VAL_HEADER(opt_out, REB_TIME, CELL_MASK_NONE);
            }
            break;

        case SYM_ZONE:
            if (not Does_Date_Have_Zone(v)) {
                Init_Nulled(opt_out);
            }
            else {
                assert(Does_Date_Have_Time(v));

                Init_Time_Nanoseconds(
                    opt_out,
                    cast(int64_t, VAL_ZONE(v)) * ZONE_MINS * MIN_SEC
                );
            }
            break;

        case SYM_DATE: {
            Move_Value(opt_out, v);

            const bool to_utc = false;
            Adjust_Date_Zone(opt_out, to_utc); // !!! necessary?

            PAYLOAD(Time, opt_out).nanoseconds = NO_DATE_TIME;
            VAL_DATE(opt_out).zone = NO_DATE_ZONE;
            break; }

        case SYM_WEEKDAY:
            Init_Integer(opt_out, Week_Day(VAL_DATE(v)));
            break;

        case SYM_JULIAN:
        case SYM_YEARDAY:
            Init_Integer(opt_out, cast(REBINT, Julian_Date(VAL_DATE(v))));
            break;

        case SYM_UTC: {
            Move_Value(opt_out, v);
            VAL_DATE(opt_out).zone = 0;
            const bool to_utc = true;
            Adjust_Date_Zone(opt_out, to_utc);
            break; }

        case SYM_HOUR:
            if (not Does_Date_Have_Time(v))
                Init_Nulled(opt_out);
            else {
                REB_TIMEF time;
                Split_Time(VAL_NANO(v), &time);
                Init_Integer(opt_out, time.h);
            }
            break;

        case SYM_MINUTE:
            if (not Does_Date_Have_Time(v))
                Init_Nulled(opt_out);
            else {
                REB_TIMEF time;
                Split_Time(VAL_NANO(v), &time);
                Init_Integer(opt_out, time.m);
            }
            break;

        case SYM_SECOND:
            if (not Does_Date_Have_Time(v))
                Init_Nulled(opt_out);
            else {
                REB_TIMEF time;
                Split_Time(VAL_NANO(v), &time);
                if (time.n == 0)
                    Init_Integer(opt_out, time.s);
                else
                    Init_Decimal(
                        opt_out,
                        cast(REBDEC, time.s) + (time.n * NANO)
                    );
            }
            break;

        default:
            Init_Nulled(opt_out); // "out of range" PICK semantics
        }
    }
    else {
        assert(opt_out == NULL);

        // Here the desire is to modify the incoming date directly.  This is
        // done by changing the components that need to change which were
        // extracted, and building a new date out of the parts.

        REBLEN day = VAL_DAY(v) - 1;
        REBLEN month = VAL_MONTH(v) - 1;
        REBLEN year = VAL_YEAR(v);

        // Not all dates have times or time zones.  But track whether or not
        // the extracted "secs" or "tz" fields are valid by virtue of updating
        // the flags in the value itself.
        //
        REBI64 secs = Does_Date_Have_Time(v) ? VAL_NANO(v) : NO_DATE_TIME;
        REBINT tz = Does_Date_Have_Zone(v) ? VAL_ZONE(v) : NO_DATE_ZONE;

        switch (sym) {
        case SYM_YEAR:
            year = Int_From_Date_Arg(opt_poke);
            break;

        case SYM_MONTH:
            month = Int_From_Date_Arg(opt_poke) - 1;
            break;

        case SYM_DAY:
            day = Int_From_Date_Arg(opt_poke) - 1;
            break;

        case SYM_TIME:
            if (IS_NULLED(opt_poke)) { // clear out the time component
                PAYLOAD(Time, v).nanoseconds = NO_DATE_TIME;
                VAL_DATE(v).zone = NO_DATE_ZONE;
                return;
            }

            if (IS_TIME(opt_poke) or IS_DATE(opt_poke))
                secs = VAL_NANO(opt_poke);
            else if (IS_INTEGER(opt_poke))
                secs = Int_From_Date_Arg(opt_poke) * SEC_SEC;
            else if (IS_DECIMAL(opt_poke))
                secs = DEC_TO_SECS(VAL_DECIMAL(opt_poke));
            else
                fail (opt_poke);
            break;

        case SYM_ZONE:
            if (IS_NULLED(opt_poke)) { // clear out the zone component
                VAL_DATE(v).zone = NO_DATE_ZONE;
                return;
            }

            if (not Does_Date_Have_Time(v))
                fail ("Can't set /ZONE in a DATE! with no time component");

            if (IS_TIME(opt_poke))
                tz = cast(REBINT, VAL_NANO(opt_poke) / (ZONE_MINS * MIN_SEC));
            else if (IS_DATE(opt_poke))
                tz = VAL_ZONE(opt_poke);
            else
                tz = Int_From_Date_Arg(opt_poke) * (60 / ZONE_MINS);
            if (tz > MAX_ZONE or tz < -MAX_ZONE)
                fail (Error_Out_Of_Range(opt_poke));
            break;

        case SYM_JULIAN:
        case SYM_WEEKDAY:
        case SYM_UTC:
            fail (rebUnrelativize(picker));

        case SYM_DATE:
            if (!IS_DATE(opt_poke))
                fail (opt_poke);
            VAL_DATE(v) = VAL_DATE(opt_poke);

            assert(Does_Date_Have_Zone(opt_poke) == Does_Date_Have_Zone(v));
            return;

        case SYM_HOUR: {
            if (not Does_Date_Have_Time(v))
                secs = 0; // secs is applicable

            REB_TIMEF time;
            Split_Time(secs, &time);
            time.h = Int_From_Date_Arg(opt_poke);
            secs = Join_Time(&time, false);
            break; }

        case SYM_MINUTE: {
            if (not Does_Date_Have_Time(v))
                secs = 0; // secs is applicable

            REB_TIMEF time;
            Split_Time(secs, &time);
            time.m = Int_From_Date_Arg(opt_poke);
            secs = Join_Time(&time, false);
            break; }

        case SYM_SECOND: {
            if (not Does_Date_Have_Time(v))
                secs = 0; // secs is applicable

            REB_TIMEF time;
            Split_Time(secs, &time);
            if (IS_INTEGER(opt_poke)) {
                time.s = Int_From_Date_Arg(opt_poke);
                time.n = 0;
            }
            else {
                //if (f < 0.0) fail (Error_Out_Of_Range(setval));
                time.s = cast(REBINT, VAL_DECIMAL(opt_poke));
                time.n = cast(REBINT,
                    (VAL_DECIMAL(opt_poke) - time.s) * SEC_SEC);
            }
            secs = Join_Time(&time, false);
            break; }

        default:
            fail (rebUnrelativize(picker));
        }

        // !!! We've gone through and updated the date or time, but we could
        // have made something nonsensical...dates or times that do not
        // exist.  Rebol historically allows it, but just goes through a
        // shady process of "normalization".  So if you have February 29 in
        // a non-leap year, it would adjust that to be March 1st, or something
        // along these lines.  Review.
        //
        if (Does_Date_Have_Time(v))
            Normalize_Time(&secs, &day);

        // No time zone component flag set shouldn't matter for date
        // normalization, it just passes it through
        //
        VAL_DATE(v) = Normalize_Date(day, month, year, tz);
        PAYLOAD(Time, v).nanoseconds = secs;  // may be NO_DATE_TIME

        const bool to_utc = true;
        Adjust_Date_Zone(v, to_utc);
    }
}


//
//  PD_Date: C
//
REB_R PD_Date(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    if (opt_setval != NULL) {
        //
        // Updates pvs->out; R_IMMEDIATE means path dispatch will write it
        // back to whatever the originating variable location was, or error
        // if it didn't come from a variable.
        //
        Pick_Or_Poke_Date(NULL, pvs->out, picker, opt_setval);
        return R_IMMEDIATE;
    }

    // !!! The date picking as written can't both read and write the out cell.
    //
    DECLARE_LOCAL (temp);
    Move_Value(temp, pvs->out);
    Pick_Or_Poke_Date(pvs->out, temp, picker, NULL);
    return pvs->out;
}


//
//  REBTYPE: C
//
REBTYPE(Date)
{
    REBVAL *v = D_ARG(1);
    assert(IS_DATE(v));

    REBSYM sym = VAL_WORD_SYM(verb);

    REBYMD date = VAL_DATE(v);
    REBLEN day = VAL_DAY(v) - 1;
    REBLEN month = VAL_MONTH(v) - 1;
    REBLEN year = VAL_YEAR(v);
    REBI64 secs = Does_Date_Have_Time(v) ? VAL_NANO(v) : NO_DATE_TIME;

    if (sym == SYM_SUBTRACT or sym == SYM_ADD) {
        REBVAL *arg = D_ARG(2);
        REBINT type = VAL_TYPE(arg);

        if (type == REB_DATE) {
            if (sym == SYM_SUBTRACT)
                return Init_Integer(D_OUT, Diff_Date(date, VAL_DATE(arg)));
        }
        else if (type == REB_TIME) {
            if (sym == SYM_ADD) {
                if (secs == NO_DATE_TIME)
                    secs = 0;
                secs += VAL_NANO(arg);
                goto fix_time;
            }
            if (sym == SYM_SUBTRACT) {
                if (secs == NO_DATE_TIME)
                    secs = 0;
                secs -= VAL_NANO(arg);
                goto fix_time;
            }
        }
        else if (type == REB_INTEGER) {
            REBINT num = Int32(arg);
            if (sym == SYM_ADD) {
                day += num;
                goto fix_date;
            }
            if (sym == SYM_SUBTRACT) {
                day -= num;
                goto fix_date;
            }
        }
        else if (type == REB_DECIMAL) {
            REBDEC dec = Dec64(arg);
            if (sym == SYM_ADD) {
                if (secs == NO_DATE_TIME)
                    secs = 0;
                secs += cast(REBI64, dec * TIME_IN_DAY);
                goto fix_time;
            }
            if (sym == SYM_SUBTRACT) {
                if (secs == NO_DATE_TIME)
                    secs = 0;
                secs -= cast(REBI64, dec * TIME_IN_DAY);
                goto fix_time;
            }
        }
    }
    else {
        switch (sym) {
          case SYM_COPY:
            RETURN (v);  // immediate type, just copy bits

          case SYM_EVEN_Q:
            return Init_Logic(D_OUT, ((~day) & 1) == 0);

          case SYM_ODD_Q:
            return Init_Logic(D_OUT, (day & 1) == 0);

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

            Subtract_Date(val1, val2, D_OUT);
            return D_OUT; }

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
    RESET_CELL(D_OUT, REB_DATE, CELL_MASK_NONE);
    VAL_DATE(D_OUT) = date;
    PAYLOAD(Time, D_OUT).nanoseconds = secs; // may be NO_DATE_TIME
    if (secs == NO_DATE_TIME)
        VAL_DATE(D_OUT).zone = NO_DATE_ZONE;
    return D_OUT;
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

    RESET_CELL(D_OUT, REB_DATE, CELL_MASK_NONE);
    VAL_YEAR(D_OUT) = VAL_INT32(ARG(year));
    VAL_MONTH(D_OUT) = VAL_INT32(ARG(month));
    VAL_DAY(D_OUT) = VAL_INT32(ARG(day));

    if (IS_BLANK(ARG(zone)))
        VAL_DATE(D_OUT).zone = NO_DATE_ZONE;
    else
        VAL_DATE(D_OUT).zone = VAL_INT32(ARG(zone)) / ZONE_MINS;

    REBI64 nano = IS_BLANK(ARG(nano)) ? 0 : VAL_INT64(ARG(nano));
    PAYLOAD(Time, D_OUT).nanoseconds
        = SECS_TO_NANO(VAL_INT64(ARG(seconds))) + nano;

    assert(Does_Date_Have_Time(D_OUT));
    return D_OUT;
}
