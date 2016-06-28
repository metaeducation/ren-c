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
// Copyright 2012-2016 Rebol Open Source Contributors
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
// Date and time are stored in UTC format with an optional timezone.
// The zone must be added when a date is exported or imported, but not
// when date computations are performed.
//
#include "sys-core.h"


//
//  Set_Date_UTC: C
// 
// Convert date/time/zone to UTC with zone.
//
void Set_Date_UTC(REBVAL *val, REBINT y, REBINT m, REBINT d, REBI64 t, REBINT z)
{
    // Adjust for zone....
    VAL_YEAR(val)  = y;
    VAL_MONTH(val) = m;
    VAL_DAY(val)   = d;
    VAL_TIME(val)  = t;
    VAL_ZONE(val)  = z;
    VAL_RESET_HEADER(val, REB_DATE);
    if (z) Adjust_Date_Zone(val, TRUE);
}


//
//  Set_Date: C
// 
// Convert OS date struct to REBOL value struct.
// NOTE: Input zone is in minutes.
//
void Set_Date(REBVAL *val, REBOL_DAT *dat)
{
    VAL_YEAR(val)  = dat->year;
    VAL_MONTH(val) = dat->month;
    VAL_DAY(val)   = dat->day;
    VAL_ZONE(val)  = dat->zone / ZONE_MINS;
    VAL_TIME(val)  = TIME_SEC(dat->time) + dat->nano;
    VAL_RESET_HEADER(val, REB_DATE);
}


//
//  CT_Date: C
//
REBINT CT_Date(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    REBINT num = Cmp_Date(a, b);
    if (mode == 1)
        return VAL_DATE(a).bits == VAL_DATE(b).bits && VAL_TIME(a) == VAL_TIME(b);
    if (mode >= 0)  return (num == 0);
    if (mode == -1) return (num >= 0);
    return (num > 0);
}


//
//  Emit_Date: C
//
void Emit_Date(REB_MOLD *mold, const REBVAL *value_orig)
{
    REBYTE buf[64];
    REBYTE *bp = &buf[0];
    REBINT tz;
    REBYTE dash = GET_MOPT(mold, MOPT_SLASH_DATE) ? '/' : '-';

    // We don't want to modify the incoming date value we are molding,
    // so we make a copy that we can tweak during the emit process

    REBVAL value_buffer = *value_orig;
    REBVAL *value = &value_buffer;

    if (
        VAL_MONTH(value) == 0
        || VAL_MONTH(value) > 12
        || VAL_DAY(value) == 0
        || VAL_DAY(value) > 31
    ) {
        Append_Unencoded(mold->series, "?date?");
        return;
    }

    if (VAL_TIME(value) != NO_TIME) Adjust_Date_Zone(value, FALSE);

//  Punctuation[GET_MOPT(mold, MOPT_COMMA_PT) ? PUNCT_COMMA : PUNCT_DOT]

    bp = Form_Int(bp, (REBINT)VAL_DAY(value));
    *bp++ = dash;
    memcpy(bp, Month_Names[VAL_MONTH(value)-1], 3);
    bp += 3;
    *bp++ = dash;
    bp = Form_Int_Pad(bp, (REBINT)VAL_YEAR(value), 6, -4, '0');
    *bp = 0;

    Append_Unencoded(mold->series, s_cast(buf));

    if (VAL_TIME(value) != NO_TIME) {

        Append_Codepoint_Raw(mold->series, '/');
        Emit_Time(mold, value);

        if (VAL_ZONE(value) != 0) {

            bp = &buf[0];
            tz = VAL_ZONE(value);
            if (tz < 0) {
                *bp++ = '-';
                tz = -tz;
            }
            else
                *bp++ = '+';

            bp = Form_Int(bp, tz/4);
            *bp++ = ':';
            bp = Form_Int_Pad(bp, (tz&3) * 15, 2, 2, '0');
            *bp = 0;

            Append_Unencoded(mold->series, s_cast(buf));
        }
    }
}


//
//  Month_Length: C
// 
// Given a year, determine the number of days in the month.
// Handles all leap year calculations.
//
static REBCNT Month_Length(REBCNT month, REBCNT year)
{
    if (month != 1)
        return Month_Max_Days[month];

    return (
        ((year % 4) == 0) &&        // divisible by four is a leap year
        (
            ((year % 100) != 0) ||  // except when divisible by 100
            ((year % 400) == 0)     // but not when divisible by 400
        )
    ) ? 29 : 28;
}


//
//  Julian_Date: C
// 
// Given a year, month and day, return the number of days since the
// beginning of that year.
//
REBCNT Julian_Date(REBDAT date)
{
    REBCNT days;
    REBCNT i;

    days = 0;

    for (i = 0; i < cast(REBCNT, date.date.month - 1); i++)
        days += Month_Length(i, date.date.year);

    return date.date.day + days;
}


//
//  Diff_Date: C
// 
// Calculate the difference in days between two dates.
//
REBINT Diff_Date(REBDAT d1, REBDAT d2)
{
    REBCNT days;
    REBINT sign;
    REBCNT m, y;
    REBDAT tmp;

    if (d1.bits == d2.bits) return 0;

    if (d1.bits < d2.bits) {
        sign = -1;
        tmp = d1;
        d1 = d2;
        d2 = tmp;
    }
    else
        sign = 1;

    // if not same year, calculate days to end of month, year and
    // days in between years plus days in end year
    if (d1.date.year > d2.date.year) {
        days = Month_Length(d2.date.month-1, d2.date.year) - d2.date.day;

        for (m = d2.date.month; m < 12; m++)
            days += Month_Length(m, d2.date.year);

        for (y = d2.date.year + 1; y < d1.date.year; y++) {
            days += (((y % 4) == 0) &&  // divisible by four is a leap year
                (((y % 100) != 0) ||    // except when divisible by 100
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
REBCNT Week_Day(REBDAT date)
{
    REBDAT year1;
    CLEARS(&year1);
    year1.date.day = 1;
    year1.date.month = 1;

    return ((Diff_Date(date, year1) + 5) % 7) + 1;
}


//
//  Normalize_Time: C
// 
// Adjust *dp by number of days and set secs to less than a day.
//
void Normalize_Time(REBI64 *sp, REBCNT *dp)
{
    REBI64 secs = *sp;
    REBINT day;

    if (secs == NO_TIME) return;

    // how many days worth of seconds do we have
    day = (REBINT)(secs / TIME_IN_DAY);
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
static REBDAT Normalize_Date(REBINT day, REBINT month, REBINT year, REBINT tz)
{
    REBINT d;
    REBDAT dr;

    // First we normalize the month to get the right year
    if (month<0) {
        year-=(-month+11)/12;
        month=11-((-month+11)%12);
    }
    if (month >= 12) {
        year += month / 12;
        month %= 12;
    }

    // Now adjust the days by stepping through each month
    while (day >= (d = (REBINT)Month_Length(month, year))) {
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
        day += (REBINT)Month_Length(month, year);
    }

    if (year < 0 || year > MAX_YEAR)
        fail (Error(RE_TYPE_LIMIT, Get_Type(REB_DATE)));

    dr.date.year = year;
    dr.date.month = month+1;
    dr.date.day = day+1;
    dr.date.zone = tz;

    return dr;
}


//
//  Adjust_Date_Zone: C
// 
// Adjust date and time for the timezone.
// The result should be used for output, not stored.
//
void Adjust_Date_Zone(REBVAL *d, REBOOL to_utc)
{
    REBI64 secs;
    REBCNT n;

    if (VAL_ZONE(d) == 0) return;

    if (VAL_TIME(d) == NO_TIME) {
        VAL_TIME(d) = VAL_ZONE(d) = 0;
        return;
    }

    // (compiler should fold the constant)
    secs = ((i64)VAL_ZONE(d) * ((i64)ZONE_SECS * SEC_SEC));
    if (to_utc) secs = -secs;
    secs += VAL_TIME(d);

    VAL_TIME(d) = (secs + TIME_IN_DAY) % TIME_IN_DAY;

    n = VAL_DAY(d) - 1;

    if (secs < 0) n--;
    else if (secs >= TIME_IN_DAY) n++;
    else return;

    VAL_DATE(d) = Normalize_Date(n, VAL_MONTH(d)-1, VAL_YEAR(d), VAL_ZONE(d));
}


//
//  Subtract_Date: C
// 
// Called by DIFFERENCE function.
//
void Subtract_Date(REBVAL *d1, REBVAL *d2, REBVAL *result)
{
    REBINT diff;
    REBI64 t1;
    REBI64 t2;

    diff  = Diff_Date(VAL_DATE(d1), VAL_DATE(d2));
    if (cast(REBCNT, abs(diff)) > (((1U << 31) - 1) / SECS_IN_DAY))
        fail (Error(RE_OVERFLOW));

    t1 = VAL_TIME(d1);
    if (t1 == NO_TIME) t1 = 0L;
    t2 = VAL_TIME(d2);
    if (t2 == NO_TIME) t2 = 0L;

    VAL_RESET_HEADER(result, REB_TIME);
    VAL_TIME(result) = (t1 - t2) + ((REBI64)diff * TIME_IN_DAY);
}


//
//  Cmp_Date: C
//
REBINT Cmp_Date(const RELVAL *d1, const RELVAL *d2)
{
    REBINT diff;

    diff  = Diff_Date(VAL_DATE(d1), VAL_DATE(d2));
    if (diff == 0) diff = Cmp_Time(d1, d2);

    return diff;
}


//
//  MAKE_Date: C
//
void MAKE_Date(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    if (IS_DATE(arg)) {
        *out = *arg;
        return;
    }

    if (IS_STRING(arg)) {
        REBCNT len;
        REBYTE *bp = Temp_Byte_Chars_May_Fail(arg, MAX_SCAN_DATE, &len, FALSE);
        if (!Scan_Date(bp, len, out))
            goto bad_make;
        return;
    }

    if (ANY_ARRAY(arg) && VAL_ARRAY_LEN_AT(arg) >= 3) {
        REBI64 secs = NO_TIME;
        REBINT tz = 0;
        REBDAT date;
        REBCNT year, month, day;

        const RELVAL *item = VAL_ARRAY_AT(arg);
        if (!IS_INTEGER(item))
            goto bad_make;
        day = Int32s(item, 1);

        ++item;
        if (!IS_INTEGER(item))
            goto bad_make;
        month = Int32s(item, 1);

        ++item;
        if (!IS_INTEGER(item))
            goto bad_make;

        if (day > 99) {
            year = day;
            day = Int32s(item, 1);
            ++item;
        }
        else {
            year = Int32s(item, 0);
            ++item;
        }

        if (month < 1 || month > 12)
            goto bad_make;

        if (year > MAX_YEAR || day < 1 || day > Month_Max_Days[month-1])
            goto bad_make;

        // Check February for leap year or century:
        if (month == 2 && day == 29) {
            if (((year % 4) != 0) ||        // not leap year
                ((year % 100) == 0 &&       // century?
                (year % 400) != 0)) goto bad_make; // not leap century
        }

        day--;
        month--;

        if (IS_TIME(item)) {
            secs = VAL_TIME(item);
            ++item;
        }

        if (IS_TIME(item)) {
            tz = cast(REBINT, VAL_TIME(item) / (ZONE_MINS * MIN_SEC));
            if (tz < -MAX_ZONE || tz > MAX_ZONE)
                fail (Error_Out_Of_Range(const_KNOWN(item)));
            ++item;
        }

        if (NOT_END(item)) goto bad_make;

        Normalize_Time(&secs, &day);
        date = Normalize_Date(day, month, year, tz);

        VAL_RESET_HEADER(out, REB_DATE);
        VAL_DATE(out) = date;
        VAL_TIME(out) = secs;
        Adjust_Date_Zone(out, TRUE);
        return;
    }

bad_make:
    fail (Error_Bad_Make(REB_DATE, arg));
}


//
//  TO_Date: C
//
void TO_Date(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    MAKE_Date(out, kind, arg);
}


//
//  PD_Date: C
//
REBINT PD_Date(REBPVS *pvs)
{
    REBVAL *value = KNOWN(pvs->value);
    assert(IS_DATE(value));

    // Extract the components of the input
    //
    REBDAT date = VAL_DATE(value);
    REBCNT day = VAL_DAY(value) - 1;
    REBCNT month = VAL_MONTH(value) - 1;
    REBCNT year = VAL_YEAR(value);

    REBI64 secs = VAL_TIME(value);
    REBINT tz = VAL_ZONE(value);

    REBINT i;

    const REBVAL *sel = pvs->selector;
    if (IS_WORD(sel)) {
        //
        // !!! Wouldn't it be clearer if this turned indices into symbols?
        //
        switch (VAL_WORD_SYM(sel)) {
        case SYM_YEAR:  i = 0; break;
        case SYM_MONTH: i = 1; break;
        case SYM_DAY:   i = 2; break;
        case SYM_TIME:  i = 3; break;
        case SYM_ZONE:  i = 4; break;
        case SYM_DATE:  i = 5; break;
        case SYM_WEEKDAY: i = 6; break;
        case SYM_JULIAN:
        case SYM_YEARDAY: i = 7; break;
        case SYM_UTC:    i = 8; break;
        case SYM_HOUR:   i = 9; break;
        case SYM_MINUTE: i = 10; break;
        case SYM_SECOND: i = 11; break;
        default:
            fail (Error_Bad_Path_Select(pvs));
        }
    }
    else if (IS_INTEGER(sel)) {
        i = Int32(sel) - 1;
        if (i < 0 || i > 8)
            fail (Error_Bad_Path_Select(pvs));
    }
    else fail (Error_Bad_Path_Select(pvs));

    REB_TIMEF time;
    if (i > 8) Split_Time(secs, &time);

    const REBVAL *setval = pvs->opt_setval;
    if (setval == NULL) {
        //
        // Adapt the date value that came in to get the return result.  Put
        // the existing value in the store, and adjust its time zone if
        // not aleady adusted.
        //
        REBVAL *store = pvs->store;
        if (value != store)
            *store = *value;

        if (i != 8) Adjust_Date_Zone(store, FALSE);

        REBINT num;
        switch(i) {
        case 0:
            num = year;
            break;
        case 1:
            num = month + 1;
            break;
        case 2:
            num = day + 1;
            break;
        case 3:
            if (secs == NO_TIME) return PE_NONE;
            VAL_RESET_HEADER(store, REB_TIME);
            return PE_USE_STORE;
        case 4:
            if (secs == NO_TIME) return PE_NONE;
            VAL_TIME(store) = cast(i64, tz) * ZONE_MINS * MIN_SEC;
            VAL_RESET_HEADER(store, REB_TIME);
            return PE_USE_STORE;
        case 5:
            // date
            VAL_TIME(store) = NO_TIME;
            VAL_ZONE(store) = 0;
            return PE_USE_STORE;
        case 6:
            // weekday
            num = Week_Day(date);
            break;
        case 7:
            // yearday
            num = cast(REBINT, Julian_Date(date));
            break;
        case 8:
            // utc
            VAL_ZONE(store) = 0;
            return PE_USE_STORE;
        case 9:
            num = time.h;
            break;
        case 10:
            num = time.m;
            break;
        case 11:
            if (time.n == 0) num = time.s;
            else {
                SET_DECIMAL(store, cast(REBDEC, time.s) + (time.n * NANO));
                return PE_USE_STORE;
            }
            break;

        default:
            return PE_NONE;
        }

        SET_INTEGER(store, num);
        return PE_USE_STORE;
    }
    else {
        // Here the desire is to modify the incoming date directly.  This is
        // done by changing the components that need to change which were
        // extracted, and building a new date out of the parts.

        REBINT n;

        if (IS_INTEGER(setval) || IS_DECIMAL(setval))
            n = Int32s(setval, 0);
        else if (IS_BLANK(setval))
            n = 0;
        else if (IS_TIME(setval) && (i == 3 || i == 4))
            NOOP;
        else if (IS_DATE(setval) && (i == 3 || i == 5))
            NOOP;
        else fail (Error_Bad_Path_Field_Set(pvs));

        switch(i) {
        case 0:
            year = n;
            break;
        case 1:
            month = n - 1;
            break;
        case 2:
            day = n - 1;
            break;
        case 3:
            // time
            if (IS_BLANK(setval)) {
                secs = NO_TIME;
                tz = 0;
                break;
            }
            else if (IS_TIME(setval) || IS_DATE(setval))
                secs = VAL_TIME(setval);
            else if (IS_INTEGER(setval))
                secs = n * SEC_SEC;
            else if (IS_DECIMAL(setval))
                secs = DEC_TO_SECS(VAL_DECIMAL(setval));
            else
                fail (Error_Bad_Path_Field_Set(pvs));
            break;
        case 4:
            // zone
            if (IS_TIME(setval))
                tz = (REBINT)(VAL_TIME(setval) / (ZONE_MINS * MIN_SEC));
            else if (IS_DATE(setval))
                tz = VAL_ZONE(setval);
            else tz = n * (60 / ZONE_MINS);
            if (tz > MAX_ZONE || tz < -MAX_ZONE)
                fail (Error_Bad_Path_Range(pvs));
            break;
        case 5:
            // date
            if (!IS_DATE(setval))
                fail (Error_Bad_Path_Field_Set(pvs));
            date = VAL_DATE(setval);
            goto set_date;
        case 9:
            time.h = n;
            secs = Join_Time(&time, FALSE);
            break;
        case 10:
            time.m = n;
            secs = Join_Time(&time, FALSE);
            break;
        case 11:
            if (IS_INTEGER(setval)) {
                time.s = n;
                time.n = 0;
            }
            else {
                //if (f < 0.0) fail (Error_Out_Of_Range(setval));
                time.s = cast(REBINT, VAL_DECIMAL(setval));
                time.n = cast(REBINT,
                    (VAL_DECIMAL(setval) - time.s) * SEC_SEC);
            }
            secs = Join_Time(&time, FALSE);
            break;

        default:
            fail (Error_Bad_Path_Set(pvs));
        }

        Normalize_Time(&secs, &day);
        date = Normalize_Date(day, month, year, tz);

    set_date:
        VAL_RESET_HEADER(pvs->value, REB_DATE);
        VAL_DATE(pvs->value) = date;
        VAL_TIME(pvs->value) = secs;
        Adjust_Date_Zone(KNOWN(pvs->value), TRUE);

        return PE_OK;
    }
}


//
//  REBTYPE: C
//
REBTYPE(Date)
{
    REBI64  secs;
    REBINT  tz;
    REBDAT  date;
    REBCNT  day, month, year;
    REBVAL  *val;
    REBVAL  *arg = NULL;
    REBINT  num;

    val = D_ARG(1);
    assert(IS_DATE(val));
    date  = VAL_DATE(val);
    day   = VAL_DAY(val) - 1;
    month = VAL_MONTH(val) - 1;
    year  = VAL_YEAR(val);
    tz    = VAL_ZONE(val);
    secs  = VAL_TIME(val);

    if (D_ARGC > 1) arg = D_ARG(2);

    if (action == SYM_SUBTRACT || action == SYM_ADD) {
        REBINT  type = VAL_TYPE(arg);

        if (type == REB_DATE) {
            if (action == SYM_SUBTRACT) {
                num = Diff_Date(date, VAL_DATE(arg));
                goto ret_int;
            }
        }
        else if (type == REB_TIME) {
            if (secs == NO_TIME) secs = 0;
            if (action == SYM_ADD) {
                secs += VAL_TIME(arg);
                goto fixTime;
            }
            if (action == SYM_SUBTRACT) {
                secs -= VAL_TIME(arg);
                goto fixTime;
            }
        }
        else if (type == REB_INTEGER) {
            num = Int32(arg);
            if (action == SYM_ADD) {
                day += num;
                goto fixDate;
            }
            if (action == SYM_SUBTRACT) {
                day -= num;
                goto fixDate;
            }
        }
        else if (type == REB_DECIMAL) {
            REBDEC dec = Dec64(arg);
            if (secs == NO_TIME) secs = 0;
            if (action == SYM_ADD) {
                secs += (REBI64)(dec * TIME_IN_DAY);
                goto fixTime;
            }
            if (action == SYM_SUBTRACT) {
                secs -= (REBI64)(dec * TIME_IN_DAY);
                goto fixTime;
            }
        }
    }
    else {
        switch(action) {
        case SYM_EVEN_Q:
            return ((~day) & 1) == 0 ? R_TRUE : R_FALSE;

        case SYM_ODD_Q:
            return (day & 1) == 0 ? R_TRUE : R_FALSE;

        case SYM_PICK:
            assert(D_ARGC > 1);
            Pick_Path(D_OUT, val, arg, 0);
            return R_OUT;

///     case SYM_POKE:
///         Pick_Path(D_OUT, val, arg, D_ARG(3));
///         *D_OUT = *D_ARG(3);
///         return R_OUT;

        case SYM_RANDOM: {
            REFINE(2, seed);
            REFINE(3, secure);
            // !!! "needs further definition ?  random/zero" <-- ?

            const REBOOL secure = REF(secure);

            if (REF(seed)) {
                //
                // Note that nsecs not set often for dates (requires /precise)
                //
                Set_Random(
                    (cast(REBI64, year) << 48)
                    + (cast(REBI64, Julian_Date(date)) << 32)
                    + secs
                );
                return R_VOID;
            }

            if (year == 0) break;

            year = cast(REBCNT, Random_Range(year, secure));
            month = cast(REBCNT, Random_Range(12, secure));
            day = cast(REBCNT, Random_Range(31, secure));

            if (secs != NO_TIME)
                secs = Random_Range(TIME_IN_DAY, secure);

            goto fixDate;
        }

        case SYM_ABSOLUTE:
            goto setDate;
        }
    }
    fail (Error_Illegal_Action(REB_DATE, action));

fixTime:
    Normalize_Time(&secs, &day);

fixDate:
    date = Normalize_Date(day, month, year, tz);

setDate:
    VAL_RESET_HEADER(D_OUT, REB_DATE);
    VAL_DATE(D_OUT) = date;
    VAL_TIME(D_OUT) = secs;
    return R_OUT;

ret_int:
    SET_INTEGER(D_OUT, num);
    return R_OUT;
}
