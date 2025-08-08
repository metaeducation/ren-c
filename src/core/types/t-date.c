//
//  file: %t-date.c
//  summary: "date datatype"
//  section: datatypes
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
// Date and time are stored in UTC format with an optional timezone.
// The zone must be added when a date is exported or imported, but not
// when date computations are performed.
//

#include "sys-core.h"


//
//  CT_Date: C
//
// Comparing Rebol DATE! is fraught with ambiguities, because a date can have
// various levels of specificity (having a time, or lacking a time, etc.)
//
// It was tried to say that dates without time or zones lacked specificity to
// participate in comparisons with dates that had them.  This turned out to be
// quite unpleasant in practice, so we instead use more pragmatic methods
// where you can say things like (26-Jul-2021/7:41:45.314 > 26-Jul-2021) is
// false, because it still lies on the same span of a day.
//
// Note that throwing in ideas like assuming 26-Jul-2021 is in the "current
// time zone" would result in determinism problems for this comparison, so
// date value literals on different machines would compare differently.
//
REBINT CT_Date(const Element* a_in, const Cell* b_in, bool strict)
//
// 1. This comparison doesn't know if it's being asked on behalf of equality or
//    not.  This is suboptimal, a redesign is needed:
//
//      https://forum.rebol.info/t/comparison-semantics/1318
//
// 2. Plain > and < sometimes pass in strict.  We don't want that for dates,
//    because we want (26-Jul-2021/7:41:45.314 > 26-Jul-2021) to be false.
//    See GREATER? and LESSER? for the nuance of the relevant hackery.
{
    bool a_had_zone = Does_Date_Have_Zone(a_in);
    bool b_had_zone = Does_Date_Have_Zone(b_in);

    bool a_had_time = Does_Date_Have_Time(a_in);
    bool b_had_time = Does_Date_Have_Time(b_in);

    DECLARE_ELEMENT (a);
    DECLARE_ELEMENT (b);
    Copy_Dequoted_Cell(a, a_in);
    Copy_Dequoted_Cell(b, b_in);

    Adjust_Date_UTC(a);  // gets 00:00:00+0:00 filled in if no time info
    Adjust_Date_UTC(b);

    REBINT days_diff = Days_Between_Dates(a, b);
    if (days_diff != 0)  // all comparison modes consider this unequal
        return days_diff > 0 ? 1 : -1;

    if (not strict and (not a_had_time or not b_had_time))  // [2]
        return 0;  // non strict says (26-Jul-2021/7:41:45.314 = 26-Jul-2021)

    if (strict) {
        if (not a_had_time and not b_had_time)  // AND, not OR, for strict
            return 0;

        if (a_had_time != b_had_time)  // 26-Jul-2021/0:00 strict > 26-Jul-2021
            return b_had_time ? 1 : -1;
    }

    assert(a_had_time and b_had_time);

    REBINT time_ct = CT_Time(a, b, strict);  // guaranteed [-1 0 1]
    if (time_ct != 0)
        return time_ct;

    if (strict and (a_had_zone != b_had_zone))
        return b_had_zone ? 1 : -1;

    return 0;
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Date)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not Bool_ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Date(v1, v2, strict) == 0);
}


// !!! R3-Alpha and Red both behave thusly:
//
//     >> -4.94065645841247E-324 < 0.0
//     == true
//
//     >> -4.94065645841247E-324 = 0.0
//     == true
//
// This is to say that the `=` is operating under non-strict rules, while
// the `<` is still strict to see the difference.  Kept this way for
// compatibility for now.
//
// BUT one exception is made for dates, so that they will compare
// (26-Jul-2021/7:41:45.314 > 26-Jul-2021) to be false.  This requires
// being willing to consider them equal, hence non-strict.
//
IMPLEMENT_GENERIC(LESSER_Q, Is_Date)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Date(v1, v2, false) == -1);
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Date)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);  // calls MOLDIFY on the time component, may heed

    UNUSED(form);

    if (
        VAL_MONTH(v) == 0
        or VAL_MONTH(v) > 12
        or VAL_DAY(v) == 0
        or VAL_DAY(v) > 31
    ) {
        require (
          Append_Ascii(mo->strand, "?date?")
        );
        return TRIPWIRE;
    }

    // Date bits are stored in canon UTC form.  But for rendering, the year
    // and month and day and time want to integrate the time zone.
    //
    int zone = Does_Date_Have_Zone(v) ? VAL_ZONE(v) : NO_DATE_ZONE;  // capture
    Fold_Zone_Into_Date(v);
    assert(not Does_Date_Have_Zone(v));

    Byte dash = GET_MOLD_FLAG(mo, MOLD_FLAG_SLASH_DATE) ? '/' : '-';

    Byte buf[64];
    Byte* bp = &buf[0];

    bp = Form_Int(bp, VAL_DAY(v));
    *bp++ = dash;
    memcpy(bp, g_month_names[VAL_MONTH(v) - 1], 3);
    bp += 3;
    *bp++ = dash;
    bp = Form_Int_Pad(bp, VAL_YEAR(v), 6, -4, '0');
    *bp = '\0';

    require (
      Append_Ascii(mo->strand, s_cast(buf))
    );

    if (Does_Date_Have_Time(v)) {
        Append_Codepoint(mo->strand, '/');
        Bounce bounce = GENERIC_CFUNC(MOLDIFY, Is_Time)(LEVEL);  // Bool_ARG(FORM)?
        assert(bounce == TRIPWIRE);  // !!! generically might BOUNCE_CONTINUE
        UNUSED(bounce);

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

            require (
              Append_Ascii(mo->strand, s_cast(buf))
            );
        }
    }

    return TRIPWIRE;
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
        return g_month_max_days[month];

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
REBLEN Julian_Date(const Cell* date)
{
    REBLEN days = 0;

    REBLEN i;
    for (i = 0; i < VAL_MONTH(date) - 1; i++)
        days += Month_Length(i, VAL_YEAR(date));

    return VAL_DAY(date) + days;
}


//
//  Days_Between_Dates: C
//
// Calculate the (signed) difference in days between two dates.
//
REBINT Days_Between_Dates(const Value* a_in, const Value* b_in)
{
    if (Does_Date_Have_Time(a_in) != Does_Date_Have_Time(b_in))
        panic (Error_Invalid_Compare_Raw(a_in, b_in));

    if (Does_Date_Have_Zone(a_in) != Does_Date_Have_Zone(b_in))
        panic (Error_Invalid_Compare_Raw(a_in, b_in));

    DECLARE_VALUE (a);
    DECLARE_VALUE (b);
    Copy_Cell(a, a_in);
    Copy_Cell(b, b_in);

    if (Does_Date_Have_Zone(a)) {
        Adjust_Date_UTC(a);
        Adjust_Date_UTC(b);
    }

    REBINT sign = 1;

    if (VAL_YEAR(a) < VAL_YEAR(b))
        sign = -1;
    else if (VAL_YEAR(a) == VAL_YEAR(b)) {
        if (VAL_MONTH(a) < VAL_MONTH(b))
            sign = -1;
        else if (VAL_MONTH(a) == VAL_MONTH(b)) {
            if (VAL_DAY(a) < VAL_DAY(b))
                sign = -1;
            else if (VAL_DAY(a) == VAL_DAY(b))
                return 0;
        }
    }

    if (sign == -1) {
        DECLARE_VALUE (tmp);
        Copy_Cell(tmp, a);
        Copy_Cell(a, b);
        Copy_Cell(b, tmp);
    }

    // if not same year, calculate days to end of month, year and
    // days in between years plus days in end year
    //
    if (VAL_YEAR(a) > VAL_YEAR(b)) {
        REBLEN days = Month_Length(VAL_MONTH(b) - 1, VAL_YEAR(b)) - VAL_DAY(b);

        REBLEN m;
        for (m = VAL_MONTH(b); m < 12; m++)
            days += Month_Length(m, VAL_YEAR(b));

        REBLEN y;
        for (y = VAL_YEAR(b) + 1; y < VAL_YEAR(a); y++) {
            days += (((y % 4) == 0) and  // divisible by four is a leap year
                (((y % 100) != 0) or  // except when divisible by 100
                ((y % 400) == 0)))  // but not when divisible by 400
                ? 366u : 365u;
        }
        return sign * (days + Julian_Date(a));
    }

    return sign * (Julian_Date(a) - Julian_Date(b));
}


//
//  Week_Day: C
//
// Return the day of the week for a specific date.
//
REBLEN Week_Day(const Value* date)
{
    DECLARE_VALUE (year1);
    Copy_Cell(year1, date);
    VAL_YEAR(year1) = 0;
    VAL_MONTH(year1) = 1;
    VAL_DAY(year1) = 1;

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
//  Init_Normalized_Date: C
//
// Given a year, month and day, normalize and combine to give a new
// date value.
//
static Element* Init_Normalized_Date(
    Init(Element) out,
    REBINT day,
    REBINT month,
    REBINT year,
    REBINT tz
){
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
    while (day >= (d = Month_Length(month, year))) {
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
        day += Month_Length(month, year);
    }

    if (year < 0 or year > MAX_YEAR)
        panic (Error_Type_Limit_Raw(Datatype_From_Type(TYPE_DATE)));

    Reset_Cell_Header_Noquote(out, CELL_MASK_DATE);
    CELL_DATE_YMDZ(out).year = year;
    CELL_DATE_YMDZ(out).month = month + 1;
    CELL_DATE_YMDZ(out).day = day + 1;
    CELL_DATE_YMDZ(out).zone = tz;
    Tweak_Cell_Nanoseconds(out, NO_DATE_TIME);

    return out;
}


//
//  Adjust_Date_Zone_Core: C
//
// If the date and time bits would show a given rendered output for what the
// values would be for the current time zone, then adjust those bits for if
// the given zone were stored in the date.
//
void Adjust_Date_Zone_Core(Cell* d, int zone)
{
    assert(not Does_Date_Have_Zone(d));

    if (zone == NO_DATE_ZONE)
        return;

    REBI64 nano =
        cast(int64_t, - zone)  // !!! this negation of the zone seems necessary
        * (cast(int64_t, ZONE_SECS) * SEC_SEC);

    nano += VAL_NANO(d);

    REBLEN n = VAL_DAY(d) - 1;

    if (nano < 0)
        --n;
    else if (nano >= TIME_IN_DAY)
        ++n;
    else {
        VAL_ZONE(d) = zone;  // usually done by Normalize
        Tweak_Cell_Nanoseconds(d, (nano + TIME_IN_DAY) % TIME_IN_DAY);
        return;
    }

    Init_Normalized_Date(d, n, VAL_MONTH(d) - 1, VAL_YEAR(d), zone);
    Tweak_Cell_Nanoseconds(d, (nano + TIME_IN_DAY) % TIME_IN_DAY);
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
void Fold_Zone_Into_Date(Cell* d)
{
    if (Does_Date_Have_Zone(d)) {
        int zone = VAL_ZONE(d);
        VAL_ZONE(d) = NO_DATE_ZONE;
        if (zone != 0)
            Adjust_Date_Zone_Core(d, - zone);
        VAL_ZONE(d) = NO_DATE_ZONE;
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
void Adjust_Date_UTC(Cell* d)
{
    if (not Does_Date_Have_Time(d)) {
        Tweak_Cell_Nanoseconds(d, 0);
        VAL_ZONE(d) = 0;
    }
    else if (not Does_Date_Have_Zone(d)) {
        VAL_ZONE(d) = 0;
    }
    else {
        int zone = VAL_ZONE(d);
        if (zone != 0) {
            VAL_ZONE(d) = NO_DATE_ZONE;
            Adjust_Date_Zone_Core(d, - zone);
            VAL_ZONE(d) = 0;
        }
    }
}


//
//  Time_Between_Dates: C
//
// Called by DIFFERENCE function.
//
Value* Time_Between_Dates(
    Sink(Value) out,
    const Value* d1,
    const Value* d2
){
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
        panic (Error_Invalid_Compare_Raw(d1, d2));

    REBI64 t1 = VAL_NANO(d1);
    REBI64 t2 = VAL_NANO(d2);

    REBINT diff = Days_Between_Dates(d1, d2);

    // Note: abs() takes `int`, but there is a labs(), and C99 has llabs()
    //
    if (cast(unsigned, abs(cast(int, diff))) > (((1U << 31) - 1) / SECS_IN_DAY))
        panic (Error_Overflow_Raw());


    return Init_Time_Nanoseconds(
        out,
        (t1 - t2) + (cast(REBI64, diff) * TIME_IN_DAY)
    );
}


IMPLEMENT_GENERIC(MAKE, Is_Date)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Datatype_Builtin_Heart(ARG(TYPE)) == TYPE_DATE);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    if (Any_List(arg))
        goto make_from_array;

    if (Is_Text(arg)) {
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Size_At(&size, arg);
        if (
            cast(const Byte*, utf8) + size
            != Try_Scan_Date_To_Stack(utf8, size)
        ){
            goto bad_make;
        }
        Move_Cell(OUT, TOP_ELEMENT);
        DROP();
        return OUT;
    }

    goto bad_make;

  make_from_array: {  ////////////////////////////////////////////////////////

    const Element* tail;
    const Element* item = List_At(&tail, arg);

    if (item == tail or not Is_Integer(item))
        goto bad_make;

    REBLEN day = Int32s(item, 1);

    ++item;
    if (item == tail or not Is_Integer(item))
        goto bad_make;

    REBLEN month = Int32s(item, 1);

    ++item;
    if (item == tail or not Is_Integer(item))
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

    if (year > MAX_YEAR or day < 1 or day > g_month_max_days[month-1])
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
        if (not Is_Time(item))
            goto bad_make;

        secs = VAL_NANO(item);
        ++item;

        if (item == tail)
            tz = NO_DATE_ZONE;
        else {
            if (not Is_Time(item))
                goto bad_make;

            tz = cast(REBINT, VAL_NANO(item) / (ZONE_MINS * MIN_SEC));
            if (tz < -MAX_ZONE or tz > MAX_ZONE)
                return fail (Error_Out_Of_Range(item));
            ++item;

            if (item != tail)
                goto bad_make;
        }
    }

    if (secs != NO_DATE_TIME)
        Normalize_Time(&secs, &day);

    Init_Normalized_Date(OUT, day, month, year, tz);
    Tweak_Cell_Nanoseconds(OUT, secs);

    Adjust_Date_UTC(OUT);
    return OUT;

} bad_make: {  ///////////////////////////////////////////////////////////////

    return fail (Error_Bad_Make(TYPE_DATE, arg));
}}


static REBINT Int_From_Date_Arg(const Value* poke) {
    if (Is_Integer(poke) or Is_Decimal(poke))
        return Int32s(poke, 0);

    if (Is_Space(poke))
        return 0;

    panic (poke);
}


IMPLEMENT_GENERIC(OLDGENERIC, Is_Date)
{
    Option(SymId) id = Symbol_Id(Level_Verb(LEVEL));

    Element* v = cast(Element*, ARG_N(1));
    assert(Is_Date(v));

    REBLEN day = VAL_DAY(v) - 1;
    REBLEN month = VAL_MONTH(v) - 1;
    REBLEN year = VAL_YEAR(v);
    REBI64 secs = Does_Date_Have_Time(v) ? VAL_NANO(v) : NO_DATE_TIME;

    if (id == SYM_SUBTRACT or id == SYM_ADD) {
        INCLUDE_PARAMS_OF_ADD;  // must have same layout as SUBTRACT
        USED(ARG(VALUE1));  // is v
        Element* arg = Element_ARG(VALUE2);
        Heart heart = Heart_Of_Builtin_Fundamental(arg);

        if (heart == TYPE_DATE) {
            if (id == SYM_SUBTRACT)
                return Init_Integer(OUT, Days_Between_Dates(v, arg));
        }
        else if (heart == TYPE_TIME) {
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
        else if (heart == TYPE_INTEGER) {
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
        else if (heart == TYPE_DECIMAL) {
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

    panic (UNHANDLED);

  fix_time:
    Normalize_Time(&secs, &day);

  fix_date:
    Init_Normalized_Date(
        OUT,
        day,
        month,
        year,
        Does_Date_Have_Zone(v) ? cast(int, VAL_ZONE(v)) : 0
    );

    Tweak_Cell_Nanoseconds(OUT, secs); // may be NO_DATE_TIME
    if (secs == NO_DATE_TIME)
        VAL_ZONE(OUT) = NO_DATE_ZONE;
    return OUT;
}


IMPLEMENT_GENERIC(TWEAK_P, Is_Date)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    Element* date = Element_ARG(LOCATION);
    const Value* picker = Element_ARG(PICKER);

    Option(SymId) sym;
    if (Is_Word(picker)) {
        sym = Word_Id(picker); // error later if SYM_0 or not a match
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
            panic (PARAM(PICKER));
        }
    }
    else
        panic (PARAM(PICKER));

    // When a date has a time zone on it, then this can distort the integer
    // value of the month/day/year that is seen in rendering from what is
    // stored.  (So you might see the day as the 2nd, when VAL_DAY() is
    // actually 3.)  We extract the original values so we have them if we
    // need them (e.g if asked for the UTC or zone) and adjust.
    //
    DECLARE_ELEMENT (adjusted);
    Copy_Cell(adjusted, date);
    Fold_Zone_Into_Date(adjusted);
    assert(not Does_Date_Have_Zone(adjusted));

    REBINT day = VAL_DAY(adjusted);
    REBINT month = VAL_MONTH(adjusted);
    REBINT year = VAL_YEAR(adjusted);
    REBI64 nano = Does_Date_Have_Time(adjusted)
        ? VAL_NANO(adjusted)
        : NO_DATE_TIME;
    REBINT zone = Does_Date_Have_Zone(date)  // original can be changed by poke
        ? VAL_ZONE(date)
        : NO_DATE_ZONE;

    Value* dual = ARG(DUAL);

 dispatch_pick_or_poke: {

    if (Not_Lifted(dual)) {
        if (Is_Dual_Nulled_Pick_Signal(dual))
            goto handle_pick;

        panic (Error_Bad_Poke_Dual_Raw(dual));
    }

    goto handle_poke;

} handle_pick: { /////////////////////////////////////////////////////////////

    switch (opt sym) {
      case SYM_YEAR:
        Init_Integer(OUT, year);  // tz adjusted year
        break;

      case SYM_MONTH:
        Init_Integer(OUT, month);  // tz adjusted month
        break;

      case SYM_DAY:
        Init_Integer(OUT, day);  // tz adjusted day
        break;

      case SYM_TIME:
        if (not Does_Date_Have_Time(date))
            Init_Nulled(OUT);
        else
            Init_Time_Nanoseconds(OUT, nano);  // tz adjusted nano
        break;

      case SYM_ZONE:
        if (not Does_Date_Have_Zone(date))  // un-adjusted zone (obviously!)
            Init_Nulled(OUT);
        else
            Init_Time_Nanoseconds(
                OUT,
                cast(int64_t, VAL_ZONE(date)) * ZONE_MINS * MIN_SEC
            );
        break;

      case SYM_DATE: {
        Element* out = Copy_Cell(OUT, adjusted);  // want the adjusted date
        Tweak_Cell_Nanoseconds(out, NO_DATE_TIME);  // with no time
        assert(not Does_Date_Have_Zone(out));  // time zone removed
        break; }

      case SYM_WEEKDAY:
        Init_Integer(OUT, Week_Day(adjusted));  // adjusted date
        break;

      case SYM_JULIAN:
      case SYM_YEARDAY:
        Init_Integer(OUT, Julian_Date(adjusted));
        break;

      case SYM_UTC: {
        if (not Does_Date_Have_Time(date) or not Does_Date_Have_Zone(date))
            panic (
                "DATE! must have :TIME and :ZONE components to get UTC"
            );

        // We really just want the original un-adjusted stored time but
        // with the time zone component set to 0:00
        //
        Element* out = Move_Cell(OUT, date);
        VAL_ZONE(out) = 0;  // GMT
        break; }

      case SYM_HOUR:
        if (not Does_Date_Have_Time(date))
            Init_Nulled(OUT);
        else {
            REB_TIMEF time;
            Split_Time(nano, &time);  // tz adjusted time
            Init_Integer(OUT, time.h);
        }
        break;

      case SYM_MINUTE:
        if (not Does_Date_Have_Time(date))
            Init_Nulled(OUT);
        else {
            REB_TIMEF time;
            Split_Time(nano, &time);  // tz adjusted time
            Init_Integer(OUT, time.m);
        }
        break;

      case SYM_SECOND:
        if (not Does_Date_Have_Time(date))
            Init_Nulled(OUT);
        else {
            REB_TIMEF time;
            Split_Time(nano, &time);  // tz adjusted time
            if (time.n == 0)
                Init_Integer(OUT, time.s);
            else
                Init_Decimal(
                    OUT,
                    cast(REBDEC, time.s) + (time.n * NANO)
                );
        }
        break;

      default:
        return DUAL_SIGNAL_NULL_ABSENT;
    }

    return DUAL_LIFTED(OUT);

} handle_poke: { /////////////////////////////////////////////////////////////

    // Here the desire is to modify the incoming date directly.  This is
    // done by changing the components that need to change which were
    // extracted, and building a new date out of the parts.
    //
    // The modifications are done to the time zone adjusted fields, and
    // then the time is fixed back.

    Value* poke = Unliftify_Known_Stable(dual);

    switch (opt sym) {
      case SYM_YEAR:
        year = Int_From_Date_Arg(poke);
        break;

      case SYM_MONTH:
        month = Int_From_Date_Arg(poke);
        if (month < 1 or month > 12)
            panic (Error_Out_Of_Range(poke));
        break;

      case SYM_DAY:
        day = Int_From_Date_Arg(poke);
        if (
            day < 1
            or day > Month_Length(VAL_MONTH(date), VAL_YEAR(date))
        ){
            panic (Error_Out_Of_Range(poke));
        }
        break;

      case SYM_TIME:
        if (Is_Nulled(poke)) {  // clear out the time component
            nano = NO_DATE_TIME;
            zone = NO_DATE_ZONE;
        }
        else if (Is_Time(poke) or Is_Date(poke))
            nano = VAL_NANO(poke);
        else if (Is_Integer(poke))
            nano = Int_From_Date_Arg(poke) * SEC_SEC;
        else if (Is_Decimal(poke))
            nano = DEC_TO_SECS(VAL_DECIMAL(poke));
        else
            panic (poke);

        Tweak_Cell_Nanoseconds(date, nano);
        goto check_nanoseconds;

      case SYM_ZONE:
        if (Is_Nulled(poke)) {  // clear out the zone component
            zone = NO_DATE_ZONE;
        }
        else {
            // Make it easier to turn a time into one that math can be
            // done on by letting you set the time zone even if it does
            // not have a time component.  Will become 00:00:00
            //
            if (not Does_Date_Have_Time(date))
                nano = 0;

            if (Is_Time(poke))
                zone = cast(REBINT, VAL_NANO(poke) / (ZONE_MINS * MIN_SEC));
            else if (Is_Date(poke))
                zone = VAL_ZONE(poke);
            else
                zone = Int_From_Date_Arg(poke) * (60 / ZONE_MINS);
            if (zone > MAX_ZONE or zone < -MAX_ZONE)
                panic (Error_Out_Of_Range(poke));
        }
        break;

      case SYM_JULIAN:
      case SYM_WEEKDAY:
      case SYM_UTC:
        panic (PARAM(PICKER));

      case SYM_DATE: {
        if (not Is_Date(poke))
            panic (poke);

        // We want to adjust the date being poked, so the year/month/day
        // that the user sees is the one reflected.  Safest is to work in
        // UTC instead of mixing and matching :-/ but if you're going to
        // mix then visual consistency gives the most coherent experience.
        //
        // (It could also be an error if the time zones don't line up)

        DECLARE_ATOM (poke_adjusted);
        Copy_Cell(poke_adjusted, poke);
        Fold_Zone_Into_Date(poke_adjusted);
        assert(not Does_Date_Have_Zone(poke_adjusted));

        year = VAL_YEAR(poke_adjusted);
        month = VAL_MONTH(poke_adjusted);
        day = VAL_DAY(poke_adjusted);
        break; }

      case SYM_HOUR: {
        if (not Does_Date_Have_Time(date))
            nano = 0;  // allow assignment if no prior time component

        REB_TIMEF time;
        Split_Time(nano, &time);
        time.h = Int_From_Date_Arg(poke);
        nano = Join_Time(&time, false);
        goto check_nanoseconds; }

      case SYM_MINUTE: {
        if (not Does_Date_Have_Time(date))
            nano = 0;  // allow assignment if no prior time component

        REB_TIMEF time;
        Split_Time(nano, &time);
        time.m = Int_From_Date_Arg(poke);
        nano = Join_Time(&time, false);
        goto check_nanoseconds; }

      case SYM_SECOND: {
        if (not Does_Date_Have_Time(date))
            nano = 0;  // allow assignment if no prior time component

        REB_TIMEF time;
        Split_Time(nano, &time);
        if (Is_Integer(poke)) {
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
        panic (picker);
    }

    goto finalize;

  check_nanoseconds: { ///////////////////////////////////////////////////////

    if (
        nano != NO_DATE_TIME
        and (nano < 0 or nano >= SECS_TO_NANO(24 * 60 * 60))
    ){
        panic (Error_Out_Of_Range(poke));
    }

    goto finalize;

}} finalize: { ////////////////////////////////////////////////////////////////

    // R3-Alpha went through a shady process of "normalization" if you
    // created an invalid date/time combination.  So if you have February
    // 29 in a non-leap year, it would adjust that to be March 1st.  That
    // code was basically reusing the code from date math on fieldwise
    // assignment.  Consensus was to error on invalid dates instead:
    //
    // https://forum.rebol.info/t/240/

    VAL_YEAR(date) = year;
    VAL_MONTH(date) = month;
    VAL_DAY(date) = day;
    VAL_ZONE(date) = NO_DATE_ZONE;  // to be adjusted
    Tweak_Cell_Nanoseconds(date, nano);  // may be NO_DATE_TIME

    // This is not a canon stored date, so we have to take into account
    // the separated zone variable (which may have been changed or cleared).

    if (zone != NO_DATE_ZONE)
        Adjust_Date_Zone_Core(date, zone);

    return WRITEBACK(Copy_Cell(OUT, date));  // all bits must writeback
}}


IMPLEMENT_GENERIC(RANDOMIZE, Is_Date)
{
    INCLUDE_PARAMS_OF_RANDOMIZE;

    Element* date = Element_ARG(SEED);

    REBLEN year = VAL_YEAR(date);  // unhandled if 0?
    REBI64 nano = Does_Date_Have_Time(date) ? VAL_NANO(date) : 0;

    Set_Random(  // Note that nano not set often for dates (requires :PRECISE)
        (cast(REBI64, year) << 48)
        + (cast(REBI64, Julian_Date(date)) << 32)
        + nano
    );
    return TRIPWIRE;
}


IMPLEMENT_GENERIC(RANDOM, Is_Date)
{
    INCLUDE_PARAMS_OF_RANDOM;

    Element* date = Element_ARG(MAX);

    REBLEN year = VAL_YEAR(date);
    if (year == 0)
        panic (UNHANDLED);

    const bool secure = Bool_ARG(SECURE);

    REBLEN rand_year = Random_Range(year, secure);
    REBLEN rand_month = Random_Range(12, secure);
    REBLEN rand_day = Random_Range(31, secure);

    REBI64 rand_nano;
    if (Does_Date_Have_Time(date))
        rand_nano = Random_Range(TIME_IN_DAY, secure);
    else
        rand_nano = NO_DATE_TIME;

    Init_Normalized_Date(
        OUT,
        rand_day,
        rand_month,
        rand_year,
        Does_Date_Have_Zone(date) ? cast(int, VAL_ZONE(date)) : 0
    );

    Tweak_Cell_Nanoseconds(OUT, rand_nano);  // may be NO_DATE_TIME
    if (rand_nano == NO_DATE_TIME)
        VAL_ZONE(OUT) = NO_DATE_ZONE;
    return OUT;
}


// !!! Plain SUBTRACT on dates has historically given INTEGER! of days, while
// DIFFERENCE has given back a TIME!.  This is not consistent with the
// "symmetric difference" that all other applications of difference are for.
// Review.
//
// https://forum.rebol.info/t/486
//
IMPLEMENT_GENERIC(DIFFERENCE, Is_Date)
{
    INCLUDE_PARAMS_OF_DIFFERENCE;

    Value* val1 = ARG(VALUE1);
    Value* val2 = ARG(VALUE2);

    if (Bool_ARG(CASE))
        panic (Error_Bad_Refines_Raw());

    if (Bool_ARG(SKIP))
        panic (Error_Bad_Refines_Raw());

    if (not Is_Date(val2))
        panic (
            Error_Unexpected_Type(TYPE_DATE, Datatype_Of(val2))
        );

    return Time_Between_Dates(OUT, val1, val2);
}


//
//  make-date-ymdsnz: native [
//
//  "Make a date from Year, Month, Day, Seconds, Nanoseconds, time Zone"
//
//      return: [date!]
//      year "full integer, e.g. 1975"
//          [integer!]
//      month "1 is January, 12 is December"
//          [integer!]
//      day "1 to 31"
//          [integer!]
//      seconds "3600 for each hour, 60 for each minute"
//          [integer!]
//      nano [<opt> integer!]
//      zone [<opt> integer!]
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

    Reset_Cell_Header_Noquote(TRACK(OUT), CELL_MASK_DATE);
    VAL_YEAR(OUT) = VAL_INT32(ARG(YEAR));
    VAL_MONTH(OUT) = VAL_INT32(ARG(MONTH));
    VAL_DAY(OUT) = VAL_INT32(ARG(DAY));

    if (Is_Nulled(ARG(ZONE)))
        VAL_ZONE(OUT) = NO_DATE_ZONE;
    else
        VAL_ZONE(OUT) = VAL_INT32(ARG(ZONE)) / ZONE_MINS;

    REBI64 nano = Is_Nulled(ARG(NANO)) ? 0 : VAL_INT64(ARG(NANO));
    Tweak_Cell_Nanoseconds(OUT, SECS_TO_NANO(VAL_INT64(ARG(SECONDS))) + nano);

    assert(Does_Date_Have_Time(OUT));
    return OUT;
}


//
//  make-time-sn: native [
//
//  "Make a TIME! from Seconds and Nanoseconds"
//
//      return: [time!]
//      seconds "3600 for each hour, 60 for each minute"
//          [integer!]
//      nano "Nanoseconds"
//          [<opt> integer!]
//  ]
//
DECLARE_NATIVE(MAKE_TIME_SN)
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

    Reset_Cell_Header_Noquote(TRACK(OUT), CELL_MASK_TIME);

    REBI64 nano = Is_Nulled(ARG(NANO)) ? 0 : VAL_INT64(ARG(NANO));
    Tweak_Cell_Nanoseconds(OUT, SECS_TO_NANO(VAL_INT64(ARG(SECONDS))) + nano);

    return OUT;
}
