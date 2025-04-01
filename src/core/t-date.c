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
// Date and time are stored in UTC format with an optional timezone.
// The zone must be added when a date is exported or imported, but not
// when date computations are performed.
//
#include "sys-core.h"


//
//  CT_Date: C
//
REBINT CT_Date(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode == 1) {
        if (Get_Cell_Flag(a, DATE_HAS_ZONE)) {
            if (Not_Cell_Flag(b, DATE_HAS_ZONE))
                return 0; // can't be equal

            if (VAL_DATE(a).bits != VAL_DATE(b).bits)
                return 0; // both have zones, all bits must be equal
        }
        else {
            if (Get_Cell_Flag(b, DATE_HAS_ZONE))
                return 0; // a doesn't have, b does, can't be equal

            REBDAT dat_a = a->extra.date;
            REBDAT dat_b = b->extra.date;
            dat_a.date.zone = 0;
            dat_b.date.zone = 0;
            if (dat_a.bits != dat_b.bits)
                return 0; // canonized to 0 zone not equal
        }

        if (Get_Cell_Flag(a, DATE_HAS_TIME)) {
            if (Not_Cell_Flag(b, DATE_HAS_TIME))
                return 0; // can't be equal;

            if (VAL_NANO(a) != VAL_NANO(b))
                return 0; // both have times, all bits must be equal
        }
        else {
            if (Get_Cell_Flag(b, DATE_HAS_TIME))
                return 0; // a doesn't have, b, does, can't be equal

            // neither have times so equal
        }
        return 1;
    }

    REBINT num = Cmp_Date(a, b);
    if (mode >= 0)  return (num == 0);
    if (mode == -1) return (num >= 0);
    return (num > 0);
}


//
//  MF_Date: C
//
void MF_Date(Molder* mo, const Cell* v_orig, bool form)
{
    // We don't want to modify the incoming date value we are molding,
    // so we make a copy that we can tweak during the emit process

    DECLARE_VALUE (v);
    Copy_Cell(v, KNOWN(v_orig));

    if (
        VAL_MONTH(v) == 0
        || VAL_MONTH(v) > 12
        || VAL_DAY(v) == 0
        || VAL_DAY(v) > 31
    ) {
        Append_Unencoded(mo->utf8flex, "?date?");
        return;
    }

    if (Get_Cell_Flag(v, DATE_HAS_ZONE)) {
        const bool to_utc = false;
        Adjust_Date_Zone(v, to_utc);
    }

    Byte dash = GET_MOLD_FLAG(mo, MOLD_FLAG_SLASH_DATE) ? '/' : '-';

    Byte buf[64];
    Byte *bp = &buf[0];

    bp = Form_Int(bp, cast(REBINT, VAL_DAY(v)));
    *bp++ = dash;
    memcpy(bp, Month_Names[VAL_MONTH(v) - 1], 3);
    bp += 3;
    *bp++ = dash;
    bp = Form_Int_Pad(bp, cast(REBINT, VAL_YEAR(v)), 6, -4, '0');
    *bp = '\0';

    Append_Unencoded(mo->utf8flex, s_cast(buf));

    if (Get_Cell_Flag(v, DATE_HAS_TIME)) {
        Append_Codepoint(mo->utf8flex, '/');
        MF_Time(mo, v, form);

        if (Get_Cell_Flag(v, DATE_HAS_ZONE)) {
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

            Append_Unencoded(mo->utf8flex, s_cast(buf));
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
REBLEN Julian_Date(REBDAT date)
{
    REBLEN days;
    REBLEN i;

    days = 0;

    for (i = 0; i < cast(REBLEN, date.date.month - 1); i++)
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
    // !!! Time zones (and times) throw a wrench into this calculation.
    // This just keeps R3-Alpha behavior going as flaky as it was,
    // forcing zero into the time zone bits...to avoid using uninitialized
    // time zone bits.
    //
    d1.date.zone = 0;
    d2.date.zone = 0;

    if (d1.bits == d2.bits)
        return 0;

    REBINT sign;
    if (d1.bits < d2.bits) {
        REBDAT tmp = d1;
        d1 = d2;
        d2 = tmp;
        sign = -1;
    }
    else
        sign = 1;

    // if not same year, calculate days to end of month, year and
    // days in between years plus days in end year
    //
    if (d1.date.year > d2.date.year) {
        REBLEN days
            = Month_Length(d2.date.month-1, d2.date.year) - d2.date.day;

        REBLEN m;
        for (m = d2.date.month; m < 12; m++)
            days += Month_Length(m, d2.date.year);

        REBLEN y;
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
REBLEN Week_Day(REBDAT date)
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
void Normalize_Time(REBI64 *sp, REBLEN *dp)
{
    REBI64 secs = *sp;

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
        fail (Error_Type_Limit_Raw(Datatype_From_Kind(TYPE_DATE)));

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
void Adjust_Date_Zone(Value* d, bool to_utc)
{
    if (Not_Cell_Flag(d, DATE_HAS_ZONE))
        return;

    if (Not_Cell_Flag(d, DATE_HAS_TIME)) {
        Clear_Cell_Flag(d, DATE_HAS_ZONE); // !!! Is this necessary?
        return;
    }

    // (compiler should fold the constant)

    REBI64 secs =
        cast(int64_t, VAL_ZONE(d)) * (cast(int64_t, ZONE_SECS) * SEC_SEC);
    if (to_utc)
        secs = -secs;
    secs += VAL_NANO(d);

    VAL_NANO(d) = (secs + TIME_IN_DAY) % TIME_IN_DAY;

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
void Subtract_Date(Value* d1, Value* d2, Value* result)
{
    REBINT diff = Diff_Date(VAL_DATE(d1), VAL_DATE(d2));
    if (cast(REBLEN, abs(diff)) > (((1U << 31) - 1) / SECS_IN_DAY))
        fail (Error_Overflow_Raw());

    REBI64 t1;
    if (Get_Cell_Flag(d1, DATE_HAS_TIME))
        t1 = VAL_NANO(d1);
    else
        t1 = 0L;

    REBI64 t2;
    if (Get_Cell_Flag(d2, DATE_HAS_TIME))
        t2 = VAL_NANO(d2);
    else
        t2 = 0L;

    RESET_CELL(result, TYPE_TIME);
    VAL_NANO(result) = (t1 - t2) + (cast(REBI64, diff) * TIME_IN_DAY);
}


//
//  Cmp_Date: C
//
REBINT Cmp_Date(const Cell* d1, const Cell* d2)
{
    REBINT diff = Diff_Date(VAL_DATE(d1), VAL_DATE(d2));
    if (diff != 0)
        return diff;

    if (Not_Cell_Flag(d1, DATE_HAS_TIME)) {
        if (Not_Cell_Flag(d2, DATE_HAS_TIME))
            return 0; // equal if no diff and neither has a time

        return -1; // d2 is bigger if no time on d1
    }

    if (Not_Cell_Flag(d2, DATE_HAS_TIME))
        return 1; // d1 is bigger if no time on d2

    return Cmp_Time(d1, d2);
}


//
//  MAKE_Date: C
//
Bounce MAKE_Date(Value* out, enum Reb_Kind kind, const Value* arg) {
    assert(kind == TYPE_DATE);
    UNUSED(kind);

    if (Is_Date(arg))
        return Copy_Cell(out, arg);

    if (Is_Text(arg)) {
        Size size;
        Byte *bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_DATE);
        Erase_Cell(out);
        if (nullptr == Scan_Date(out, bp, size))
            goto bad_make;
        return out;
    }

    if (Any_List(arg) && VAL_ARRAY_LEN_AT(arg) >= 3) {
        const Cell* item = Cell_List_At(arg);
        if (not Is_Integer(item))
            goto bad_make;

        REBLEN day = Int32s(item, 1);

        ++item;
        if (not Is_Integer(item))
            goto bad_make;

        REBLEN month = Int32s(item, 1);

        ++item;
        if (not Is_Integer(item))
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

        REBI64 secs;
        REBINT tz;
        if (IS_END(item)) {
            secs = 0;
            tz = 0;
        }
        else {
            if (not Is_Time(item))
                goto bad_make;

            secs = VAL_NANO(item);
            ++item;

            if (IS_END(item))
                tz = 0;
            else {
                if (not Is_Time(item))
                    goto bad_make;

                tz = cast(REBINT, VAL_NANO(item) / (ZONE_MINS * MIN_SEC));
                if (tz < -MAX_ZONE || tz > MAX_ZONE)
                    fail (Error_Out_Of_Range(KNOWN(item)));
                ++item;
            }
        }

        if (NOT_END(item))
            goto bad_make;

        Normalize_Time(&secs, &day);

        Reset_Cell_Header(out, TYPE_DATE, CELL_FLAG_DATE_HAS_TIME);
        VAL_DATE(out) = Normalize_Date(day, month, year, tz);
        VAL_NANO(out) = secs;

        const bool to_utc = true;
        Adjust_Date_Zone(out, to_utc);
        return out;
    }

  bad_make:
    fail (Error_Bad_Make(TYPE_DATE, arg));
}


//
//  TO_Date: C
//
Bounce TO_Date(Value* out, enum Reb_Kind kind, const Value* arg) {
    return MAKE_Date(out, kind, arg);
}


static REBINT Int_From_Date_Arg(const Value* opt_poke) {
    if (Is_Integer(opt_poke) || Is_Decimal(opt_poke))
        return Int32s(opt_poke, 0);

    if (Is_Blank(opt_poke))
        return 0;

    fail (Error_Invalid(opt_poke));
}


//
//  Pick_Or_Poke_Date: C
//
void Pick_Or_Poke_Date(
    Value* opt_out,
    Value* v,
    const Value* picker,
    const Value* opt_poke
){
    Option(SymId) sym;
    if (Is_Word(picker)) {
        sym = Cell_Word_Id(picker); // error later if SYM_0 or not a match
    }
    else if (Is_Integer(picker)) {
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
            fail (Error_Invalid(picker));
        }
    }
    else
        fail (Error_Invalid(picker));

    if (opt_poke == nullptr) {
        assert(opt_out != nullptr);
        Erase_Cell(opt_out);

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
            if (Not_Cell_Flag(v, DATE_HAS_TIME))
                Init_Nulled(opt_out);
            else {
                Copy_Cell(opt_out, v); // want v's adjusted VAL_NANO()
                Adjust_Date_Zone(opt_out, false);
                RESET_CELL(opt_out, TYPE_TIME); // clears date flags
            }
            break;

        case SYM_ZONE:
            if (Not_Cell_Flag(v, DATE_HAS_ZONE)) {
                Init_Nulled(opt_out);
            }
            else {
                assert(Get_Cell_Flag(v, DATE_HAS_TIME));

                Init_Time_Nanoseconds(
                    opt_out,
                    cast(int64_t, VAL_ZONE(v)) * ZONE_MINS * MIN_SEC
                );
            }
            break;

        case SYM_DATE: {
            Copy_Cell(opt_out, v);

            const bool to_utc = false;
            Adjust_Date_Zone(opt_out, to_utc); // !!! necessary?

            Clear_Cell_Flag(opt_out, DATE_HAS_TIME);
            Clear_Cell_Flag(opt_out, DATE_HAS_ZONE);
            break; }

        case SYM_WEEKDAY:
            Init_Integer(opt_out, Week_Day(VAL_DATE(v)));
            break;

        case SYM_JULIAN:
        case SYM_YEARDAY:
            Init_Integer(opt_out, cast(REBINT, Julian_Date(VAL_DATE(v))));
            break;

        case SYM_UTC: {
            Copy_Cell(opt_out, v);
            Set_Cell_Flag(opt_out, DATE_HAS_ZONE);
            INIT_VAL_ZONE(opt_out, 0);
            const bool to_utc = true;
            Adjust_Date_Zone(opt_out, to_utc);
            break; }

        case SYM_HOUR:
            if (Not_Cell_Flag(v, DATE_HAS_TIME))
                Init_Nulled(opt_out);
            else {
                REB_TIMEF time;
                Split_Time(VAL_NANO(v), &time);
                Init_Integer(opt_out, time.h);
            }
            break;

        case SYM_MINUTE:
            if (Not_Cell_Flag(v, DATE_HAS_TIME))
                Init_Nulled(opt_out);
            else {
                REB_TIMEF time;
                Split_Time(VAL_NANO(v), &time);
                Init_Integer(opt_out, time.m);
            }
            break;

        case SYM_SECOND:
            if (Not_Cell_Flag(v, DATE_HAS_TIME))
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
        assert(opt_out == nullptr);

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
        REBI64 secs = Get_Cell_Flag(v, DATE_HAS_TIME) ? VAL_NANO(v) : 0;
        REBINT tz = Get_Cell_Flag(v, DATE_HAS_ZONE) ? VAL_ZONE(v) : 0;

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
            if (Is_Nulled(opt_poke)) { // clear out the time component
                Clear_Cell_Flag(v, DATE_HAS_TIME);
                Clear_Cell_Flag(v, DATE_HAS_ZONE);
                return;
            }

            Set_Cell_Flag(v, DATE_HAS_TIME); // hence secs is applicable
            if (Is_Time(opt_poke) || Is_Date(opt_poke))
                secs = VAL_NANO(opt_poke);
            else if (Is_Integer(opt_poke))
                secs = Int_From_Date_Arg(opt_poke) * SEC_SEC;
            else if (Is_Decimal(opt_poke))
                secs = DEC_TO_SECS(VAL_DECIMAL(opt_poke));
            else
                fail (Error_Invalid(opt_poke));
            break;

        case SYM_ZONE:
            if (Is_Nulled(opt_poke)) { // clear out the zone component
                Clear_Cell_Flag(v, DATE_HAS_ZONE);
                return;
            }

            if (Not_Cell_Flag(v, DATE_HAS_TIME))
                fail ("Can't set /ZONE in a DATE! with no time component");

            Set_Cell_Flag(v, DATE_HAS_ZONE); // hence tz is applicable
            if (Is_Time(opt_poke))
                tz = cast(REBINT, VAL_NANO(opt_poke) / (ZONE_MINS * MIN_SEC));
            else if (Is_Date(opt_poke))
                tz = VAL_ZONE(opt_poke);
            else tz = Int_From_Date_Arg(opt_poke) * (60 / ZONE_MINS);
            if (tz > MAX_ZONE || tz < -MAX_ZONE)
                fail (Error_Out_Of_Range(opt_poke));
            break;

        case SYM_JULIAN:
        case SYM_WEEKDAY:
        case SYM_UTC:
            fail (Error_Invalid(picker));

        case SYM_DATE:
            if (!Is_Date(opt_poke))
                fail (Error_Invalid(opt_poke));
            VAL_DATE(v) = VAL_DATE(opt_poke);

            // If the poked date's time zone bitfield is not in effect, that
            // needs to be copied to the date we're assigning.
            //
            if (Get_Cell_Flag(opt_poke, DATE_HAS_ZONE))
                Set_Cell_Flag(v, DATE_HAS_ZONE);
            else
                Clear_Cell_Flag(v, DATE_HAS_ZONE);
            return;

        case SYM_HOUR: {
            if (Not_Cell_Flag(v, DATE_HAS_TIME)) {
                secs = 0;
                Set_Cell_Flag(v, DATE_HAS_TIME); // secs is applicable
            }

            REB_TIMEF time;
            Split_Time(secs, &time);
            time.h = Int_From_Date_Arg(opt_poke);
            secs = Join_Time(&time, false);
            break; }

        case SYM_MINUTE: {
            if (Not_Cell_Flag(v, DATE_HAS_TIME)) {
                secs = 0;
                Set_Cell_Flag(v, DATE_HAS_TIME); // secs is applicable
            }

            REB_TIMEF time;
            Split_Time(secs, &time);
            time.m = Int_From_Date_Arg(opt_poke);
            secs = Join_Time(&time, false);
            break; }

        case SYM_SECOND: {
            if (Not_Cell_Flag(v, DATE_HAS_TIME)) {
                secs = 0;
                Set_Cell_Flag(v, DATE_HAS_TIME); // secs is applicable
            }

            REB_TIMEF time;
            Split_Time(secs, &time);
            if (Is_Integer(opt_poke)) {
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
            fail (Error_Invalid(picker));
        }

        // !!! We've gone through and updated the date or time, but we could
        // have made something nonsensical...dates or times that do not
        // exist.  Rebol historically allows it, but just goes through a
        // shady process of "normalization".  So if you have February 29 in
        // a non-leap year, it would adjust that to be March 1st, or something
        // along these lines.  Review.
        //
        Normalize_Time(&secs, &day); // note secs is 0 if no time component

        // Note that tz will be 0 if no zone component flag set; shouldn't
        // matter for date normalization, it just passes it through
        //
        VAL_DATE(v) = Normalize_Date(day, month, year, tz);
        if (secs != 0)
            VAL_NANO(v) = secs;

        const bool to_utc = true;
        Adjust_Date_Zone(v, to_utc);
    }
}


//
//  PD_Date: C
//
Bounce PD_Date(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    if (opt_setval != nullptr) {
        //
        // Updates pvs->out; BOUNCE_IMMEDIATE means path dispatch will write it
        // back to whatever the originating variable location was, or error
        // if it didn't come from a variable.
        //
        Pick_Or_Poke_Date(nullptr, pvs->out, picker, opt_setval);
        return BOUNCE_IMMEDIATE;
    }

    // !!! The date picking as written can't both read and write the out cell.
    //
    DECLARE_VALUE (temp);
    Copy_Cell(temp, pvs->out);
    Pick_Or_Poke_Date(pvs->out, temp, picker, nullptr);
    return pvs->out;
}


//
//  REBTYPE: C
//
REBTYPE(Date)
{
    Value* val = D_ARG(1);
    assert(Is_Date(val));

    Option(SymId) sym = Cell_Word_Id(verb);

    RESET_CELL(OUT, TYPE_DATE); // so we can set flags on it

    REBDAT date = VAL_DATE(val);
    REBLEN day = VAL_DAY(val) - 1;
    REBLEN month = VAL_MONTH(val) - 1;
    REBLEN year = VAL_YEAR(val);
    REBI64 secs = Get_Cell_Flag(val, DATE_HAS_TIME) ? VAL_NANO(val) : 0;

    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    if (sym == SYM_SUBTRACT || sym == SYM_ADD) {
        REBINT  type = Type_Of(arg);

        if (type == TYPE_DATE) {
            if (sym == SYM_SUBTRACT)
                return Init_Integer(OUT, Diff_Date(date, VAL_DATE(arg)));
        }
        else if (type == TYPE_TIME) {
            if (sym == SYM_ADD) {
                Set_Cell_Flag(OUT, DATE_HAS_TIME);
                secs += VAL_NANO(arg);
                goto fixTime;
            }
            if (sym == SYM_SUBTRACT) {
                Set_Cell_Flag(OUT, DATE_HAS_TIME);
                secs -= VAL_NANO(arg);
                goto fixTime;
            }
        }
        else if (type == TYPE_INTEGER) {
            REBINT num = Int32(arg);
            if (sym == SYM_ADD) {
                day += num;
                goto fixDate;
            }
            if (sym == SYM_SUBTRACT) {
                day -= num;
                goto fixDate;
            }
        }
        else if (type == TYPE_DECIMAL) {
            REBDEC dec = Dec64(arg);
            if (sym == SYM_ADD) {
                Set_Cell_Flag(OUT, DATE_HAS_TIME);
                secs += (REBI64)(dec * TIME_IN_DAY);
                goto fixTime;
            }
            if (sym == SYM_SUBTRACT) {
                Set_Cell_Flag(OUT, DATE_HAS_TIME);
                secs -= (REBI64)(dec * TIME_IN_DAY);
                goto fixTime;
            }
        }
    }
    else {
        switch (sym) {
        case SYM_EVEN_Q:
            return Init_Logic(OUT, ((~day) & 1) == 0);

        case SYM_ODD_Q:
            return Init_Logic(OUT, (day & 1) == 0);

        case SYM_RANDOM: {
            INCLUDE_PARAMS_OF_RANDOM;

            UNUSED(PARAM(VALUE));

            if (Bool_ARG(ONLY))
                fail (Error_Bad_Refines_Raw());

            const bool secure = Bool_ARG(SECURE);

            if (Bool_ARG(SEED)) {
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

            if (Get_Cell_Flag(val, DATE_HAS_TIME))
                secs = Random_Range(TIME_IN_DAY, secure);

            goto fixDate;
        }

        case SYM_ABSOLUTE:
            goto setDate;

        case SYM_DIFFERENCE: {
            INCLUDE_PARAMS_OF_DIFFERENCE;

            Value* val1 = ARG(VALUE1);
            Value* val2 = ARG(VALUE2);

            if (Bool_ARG(CASE))
                fail (Error_Bad_Refines_Raw());

            if (Bool_ARG(SKIP))
                fail (Error_Bad_Refines_Raw());
            UNUSED(PARAM(SIZE));

            // !!! Plain SUBTRACT on dates has historically given INTEGER! of
            // days, while DIFFERENCE has given back a TIME!.  This is not
            // consistent with the "symmetric difference" that all other
            // applications of difference are for.  Review.
            //
            // https://forum.rebol.info/t/486
            //
            if (not Is_Date(val2))
                fail (Error_Unexpected_Type(Type_Of(val1), Type_Of(val2)));

            Subtract_Date(val1, val2, OUT);
            return OUT; }

        default:
            fail (Error_Illegal_Action(TYPE_DATE, verb));
        }
    }
    fail (Error_Illegal_Action(TYPE_DATE, verb));

fixTime:
    Normalize_Time(&secs, &day);

fixDate:
    date = Normalize_Date(
        day,
        month,
        year,
        Get_Cell_Flag(val, DATE_HAS_ZONE) ? VAL_ZONE(val) : 0
    );

setDate:
    VAL_DATE(OUT) = date;
    if (Get_Cell_Flag(OUT, DATE_HAS_TIME))
        VAL_NANO(OUT) = secs;
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
//      nano [integer!]
//      zone [integer!]
//  ]
//
DECLARE_NATIVE(MAKE_DATE_YMDSNZ)
//
// !!! This native exists to avoid adding specialized C routines to the API
// for the purposes of date creation in NOW.  Ideally there would be a nicer
// syntax via MAKE TIME!, which could use other enhancements:
//
// https://github.com/rebol/rebol-issues/issues/2313
//
{
    INCLUDE_PARAMS_OF_MAKE_DATE_YMDSNZ;

    RESET_CELL(OUT, TYPE_DATE);
    VAL_YEAR(OUT) = VAL_INT32(ARG(YEAR));
    VAL_MONTH(OUT) = VAL_INT32(ARG(MONTH));
    VAL_DAY(OUT) = VAL_INT32(ARG(DAY));

    Set_Cell_Flag(OUT, DATE_HAS_ZONE);
    INIT_VAL_ZONE(OUT, VAL_INT32(ARG(ZONE)) / ZONE_MINS);

    Set_Cell_Flag(OUT, DATE_HAS_TIME);
    VAL_NANO(OUT)
        = SECS_TO_NANO(VAL_INT64(ARG(SECONDS))) + VAL_INT64(ARG(NANO));

    return OUT;
}
