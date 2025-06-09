//
//  file: %t-time.c
//  summary: "time datatype"
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

#include "sys-core.h"

//
//  Split_Time: C
//
void Split_Time(REBI64 t, REB_TIMEF *tf)
{
    // note: negative sign will be lost.
    REBI64 h, m, s, n, i;

    if (t < 0) t = -t;

    h = t / HR_SEC;
    i = t - (h * HR_SEC);
    m = i / MIN_SEC;
    i = i - (m * MIN_SEC);
    s = i / SEC_SEC;
    n = i - (s * SEC_SEC);

    tf->h = (REBLEN)h;
    tf->m = (REBLEN)m;
    tf->s = (REBLEN)s;
    tf->n = (REBLEN)n;
}

//
//  Join_Time: C
//
// !! A REB_TIMEF has lost the sign bit available on the REBI64
// used for times.  If you want to make it negative, you need
// pass in a flag here.  (Flag added to help document the
// issue, as previous code falsely tried to judge the sign
// of tf->h, which is always positive.)
//
REBI64 Join_Time(REB_TIMEF *tf, bool neg)
{
    REBI64 t;

    t = (tf->h * HR_SEC) + (tf->m * MIN_SEC) + (tf->s * SEC_SEC) + tf->n;
    return neg ? -t : t;
}

//
//  Try_Scan_Time_To_Stack: C
//
// Scan string and convert to time.  Return zero if error.
//
Option(const Byte*) Try_Scan_Time_To_Stack(
    const Byte* cp,
    Option(Length) len  // !!! Does not requre a length... should it?
){
    UNUSED(len);

    bool neg;
    if (*cp == '-') {
        ++cp;
        neg = true;
    }
    else if (*cp == '+') {
        ++cp;
        neg = false;
    }
    else
        neg = false;

    if (*cp == '-' || *cp == '+')
        return nullptr;  // small hole: --1:23

    // Can be:
    //    HH:MM       as part1:part2
    //    HH:MM:SS    as part1:part2:part3
    //    HH:MM:SS.DD as part1:part2:part3.part4
    //    MM:SS.DD    as part1:part2.part4

    REBINT part1;
    if (not (cp = maybe Try_Grab_Int(&part1, cp)))
        return nullptr;
    if (part1 < 0 or part1 > MAX_HOUR)
        return nullptr;

    if (*cp++ != ':')
        return nullptr;

    REBINT part2;
    if (not (cp = maybe Try_Grab_Int(&part2, cp)))
        return nullptr;
    if (part2 < 0)
        return nullptr;

    REBINT part3 = -1;
    if (*cp == ':') {  // optional seconds
        ++cp;
        if (not (cp = maybe Try_Grab_Int(&part3, cp)))
            return nullptr;
        if (part3 < 0)
            return nullptr;
    }

    const Byte* sp;

    REBINT part4 = -1;
    if (*cp == '.' || *cp == ',') {
        sp = ++cp;
        cp = Grab_Int_Scale_Zero_Default(&part4, sp, 9);
        if (part4 == 0)
            part4 = -1;
    }

    Byte merid;
    if (
        *cp != '\0'
        && (UP_CASE(*cp) == 'A' || UP_CASE(*cp) == 'P')
        && (cp[1] != '\0' and UP_CASE(cp[1]) == 'M')
    ){
        merid = cast(Byte, UP_CASE(*cp));
        cp += 2;
    }
    else
        merid = '\0';

    REBI64 nanoseconds;
    if (part3 >= 0 || part4 < 0) { // HH:MM mode
        if (merid != '\0') {
            if (part1 > 12)
                return nullptr;

            if (part1 == 12)
                part1 = 0;

            if (merid == 'P')
                part1 += 12;
        }

        if (part3 < 0)
            part3 = 0;

        nanoseconds =  HOUR_TIME(part1) + MIN_TIME(part2) + SEC_TIME(part3);
    }
    else { // MM:SS mode
        if (merid != '\0')
            return nullptr; // no AM/PM for minutes

        nanoseconds = MIN_TIME(part1) + SEC_TIME(part2);
    }

    if (part4 > 0)
        nanoseconds += part4;

    if (neg)
        nanoseconds = -nanoseconds;

    Init_Time_Nanoseconds(PUSH(), nanoseconds);
    return cp;
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Time)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form);  // no difference between MOLD and FORM at this time

    if (VAL_NANO(v) < cast(REBI64, 0))  // account for the sign if present
        Append_Codepoint(mo->string, '-');

    REB_TIMEF tf;
    Split_Time(VAL_NANO(v), &tf);  // loses sign

    // "H:MM" (pad minutes to two digits, but not the hour)
    //
    Append_Int(mo->string, tf.h);
    Append_Codepoint(mo->string, ':');
    Append_Int_Pad(mo->string, tf.m, 2);

    // If seconds or nanoseconds nonzero, pad seconds to ":SS", else omit
    //
    if (tf.s != 0 or tf.n != 0) {
        Append_Codepoint(mo->string, ':');
        Append_Int_Pad(mo->string, tf.s, 2);
    }

    // If nanosecond component is present, present as a fractional amount...
    // trimming any trailing zeros.
    //
    if (tf.n > 0) {
        Append_Codepoint(mo->string, '.');
        Append_Int_Pad(mo->string, tf.n, -9);
        Trim_Tail(mo, '0');
    }

    return TRIPWIRE;
}


//
//  CT_Time: C
//
REBINT CT_Time(const Element* a, const Element* b, bool strict)
{
    UNUSED(strict);

    REBI64 t1 = VAL_NANO(a);
    REBI64 t2 = VAL_NANO(b);

    if (t2 == t1)
        return 0;
    if (t1 > t2)
        return 1;
    return -1;
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Time)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not Bool_ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Time(v1, v2, strict) == 0);
}


IMPLEMENT_GENERIC(LESSER_Q, Is_Time)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Time(v1, v2, true) == -1);
}


IMPLEMENT_GENERIC(ZEROIFY, Is_Time)
{
    INCLUDE_PARAMS_OF_ZEROIFY;
    UNUSED(ARG(EXAMPLE));  // always gives 0:00

    return Init_Time_Nanoseconds(OUT, 0);
}


IMPLEMENT_GENERIC(MAKE, Is_Time)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Cell_Datatype_Builtin_Heart(ARG(TYPE)) == TYPE_TIME);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    switch (Type_Of(arg)) {
      case TYPE_INTEGER:  // interpret as seconds
        if (VAL_INT64(arg) < -MAX_SECONDS or VAL_INT64(arg) > MAX_SECONDS)
            return PANIC(Error_Out_Of_Range(arg));

        return Init_Time_Nanoseconds(OUT, VAL_INT64(arg) * SEC_SEC);

      case TYPE_DECIMAL:
        if (
            VAL_DECIMAL(arg) < cast(REBDEC, -MAX_SECONDS)
            or VAL_DECIMAL(arg) > cast(REBDEC, MAX_SECONDS)
        ){
            return PANIC(Error_Out_Of_Range(arg));
        }
        return Init_Time_Nanoseconds(OUT, DEC_TO_SECS(VAL_DECIMAL(arg)));

      case TYPE_BLOCK: { // [hh mm ss]
        const Element* tail;
        const Element* item = Cell_List_At(&tail, arg);

        if (item == tail)
            goto bad_make;  // must have at least hours

        if (not Is_Integer(item))
            goto bad_make;  // hours must be integer

        bool neg;
        REBI64 i = Int32(item);
        if (i < 0) {
            i = -i;
            neg = true;
        }
        else
            neg = false;

        REBI64 secs = i * 3600;
        if (secs > MAX_SECONDS)
            goto bad_make;

        if (tail != ++item) {  // minutes
            if (not Is_Integer(item))
                goto bad_make;

            if ((i = Int32(item)) < 0)
                goto bad_make;

            secs += i * 60;
            if (secs > MAX_SECONDS)
                goto bad_make;
        }

        if (item != tail and tail != ++item) {  // seconds
            if (Is_Integer(item)) {
                if ((i = Int32(item)) < 0)
                    goto bad_make;

                secs += i;
                if (secs > MAX_SECONDS)
                    goto bad_make;
            }
            else if (Is_Decimal(item)) {
                if (
                    secs + cast(REBI64, VAL_DECIMAL(item)) + 1
                    > MAX_SECONDS
                ){
                    goto bad_make;
                }

                // added in below
            }
            else
                goto bad_make;
        }

        REBI64 nano = secs * SEC_SEC;

        if (item != tail and tail != ++item) {
            if (not Is_Decimal(item))
                goto bad_make;

            nano += DEC_TO_SECS(VAL_DECIMAL(item));
        }

        if (item != tail and ++item != tail)
            goto bad_make;  // more than 4 items of initialization

        if (neg)
            nano = -nano;

        return Init_Time_Nanoseconds(OUT, nano); }

      default:
        goto bad_make;
    }

  bad_make:

    return FAIL(Error_Bad_Make(TYPE_TIME, arg));
}


IMPLEMENT_GENERIC(OLDGENERIC, Is_Time)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* time = cast(Element*, ARG_N(1));
    REBI64 secs = VAL_NANO(time);

    if (
        id == SYM_ADD
        or id == SYM_SUBTRACT
        or id == SYM_DIVIDE
        or id == SYM_REMAINDER
    ){
        INCLUDE_PARAMS_OF_ADD;
        USED(ARG(VALUE1));  // is time
        Element* arg = Element_ARG(VALUE2);
        Heart heart = Heart_Of_Builtin_Fundamental(arg);

        if (heart == TYPE_TIME) {     // handle TIME - TIME cases
            REBI64 secs2 = VAL_NANO(arg);

            switch (id) {
              case SYM_ADD:
                secs = Add_Max(TYPE_TIME, secs, secs2, MAX_TIME);
                return Init_Time_Nanoseconds(OUT, secs);

              case SYM_SUBTRACT:
                secs = Add_Max(TYPE_TIME, secs, -secs2, MAX_TIME);
                return Init_Time_Nanoseconds(OUT, secs);

              case SYM_DIVIDE:
                if (secs2 == 0)
                    return PANIC(Error_Zero_Divide_Raw());
                return Init_Decimal(
                    OUT,
                    cast(REBDEC, secs) / cast(REBDEC, secs2)
                );

              case SYM_REMAINDER:
                if (secs2 == 0)
                    return PANIC(Error_Zero_Divide_Raw());
                secs %= secs2;
                return Init_Time_Nanoseconds(OUT, secs);

              default:
                return PANIC(Error_Math_Args(TYPE_TIME, verb));
            }
        }
        else if (heart == TYPE_INTEGER) {     // handle TIME - INTEGER cases
            REBI64 num = VAL_INT64(arg);

            switch (id) {
              case SYM_ADD:
                secs = Add_Max(TYPE_TIME, secs, num * SEC_SEC, MAX_TIME);
                return Init_Time_Nanoseconds(OUT, secs);

              case SYM_SUBTRACT:
                secs = Add_Max(TYPE_TIME, secs, num * -SEC_SEC, MAX_TIME);
                return Init_Time_Nanoseconds(OUT, secs);

              case SYM_DIVIDE:
                if (num == 0)
                    return PANIC(Error_Zero_Divide_Raw());
                secs /= num;
                Init_Integer(OUT, secs);
                return Init_Time_Nanoseconds(OUT, secs);

              case SYM_REMAINDER:
                if (num == 0)
                    return PANIC(Error_Zero_Divide_Raw());
                secs %= num;
                return Init_Time_Nanoseconds(OUT, secs);

              default:
                return PANIC(Error_Math_Args(TYPE_TIME, verb));
            }
        }
        else if (heart == TYPE_DECIMAL) {     // handle TIME - DECIMAL cases
            REBDEC dec = VAL_DECIMAL(arg);

            switch (id) {
              case SYM_ADD:
                secs = Add_Max(
                    TYPE_TIME,
                    secs,
                    cast(int64_t, dec * SEC_SEC),
                    MAX_TIME
                );
                return Init_Time_Nanoseconds(OUT, secs);

              case SYM_SUBTRACT:
                secs = Add_Max(
                    TYPE_TIME,
                    secs,
                    cast(int64_t, dec * -SEC_SEC),
                    MAX_TIME
                );
                return Init_Time_Nanoseconds(OUT, secs);

              case SYM_DIVIDE:
                if (dec == 0.0)
                    return PANIC(Error_Zero_Divide_Raw());
                secs = cast(int64_t, secs / dec);
                return Init_Time_Nanoseconds(OUT, secs);

              /*  // !!! Was commented out, why?
             case SYM_REMAINDER:
               ld = fmod(ld, VAL_DECIMAL(arg));
               goto decTime; */

              default:
                return PANIC(Error_Math_Args(TYPE_TIME, verb));
            }
        }
        else if (heart == TYPE_DATE and id == SYM_ADD) {
            //
            // We're adding a time and a date, code for which exists in the
            // date dispatcher already.  Instead of repeating the code here in
            // the time dispatcher, swap the arguments and call DATE's version.
            //
            Element* spare = Move_Cell(SPARE, time);
            Move_Cell(time, arg);
            Move_Cell(arg, spare);
            return GENERIC_CFUNC(OLDGENERIC, Is_Date)(level_);
        }
        return PANIC(Error_Math_Args(TYPE_TIME, verb));
    }
    else {
        // unary actions
        switch (id) {
          case SYM_ODD_Q:
            return Init_Logic(OUT, (SECS_FROM_NANO(secs) & 1) != 0);

          case SYM_EVEN_Q:
            return Init_Logic(OUT, (SECS_FROM_NANO(secs) & 1) == 0);

          case SYM_NEGATE:
            secs = -secs;
            return Init_Time_Nanoseconds(OUT, secs);

          case SYM_ABSOLUTE:
            if (secs < 0) secs = -secs;
            return Init_Time_Nanoseconds(OUT, secs);

          default:
            break;
        }
    }

    return UNHANDLED;
}


IMPLEMENT_GENERIC(TWEAK_P, Is_Time)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    Element* time = Element_ARG(LOCATION);
    const Element* picker = Element_ARG(PICKER);

    REBINT i;
    if (Is_Word(picker)) {
        switch (Cell_Word_Id(picker)) {
        case SYM_HOUR:   i = 0; break;
        case SYM_MINUTE: i = 1; break;
        case SYM_SECOND: i = 2; break;
        default:
            panic (picker);
        }
    }
    else if (Is_Integer(picker))
        i = VAL_INT32(picker) - 1;
    else
        panic (picker);

    REB_TIMEF tf;
    Split_Time(VAL_NANO(time), &tf); // loses sign

    Value* dual = ARG(DUAL);
    if (Not_Lifted(dual)) {
        if (Is_Dual_Nulled_Pick_Signal(dual))
            goto handle_pick;

        return PANIC(Error_Bad_Poke_Dual_Raw(dual));
    }

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    switch(i) {
      case 0: // hours
        Init_Integer(OUT, tf.h);
        break;

      case 1: // minutes
        Init_Integer(OUT, tf.m);
        break;

      case 2: // seconds
        if (tf.n == 0)
            Init_Integer(OUT, tf.s);
        else
            Init_Decimal(OUT, cast(REBDEC, tf.s) + (tf.n * NANO));
        break;

      default:
        return DUAL_SIGNAL_NULL_ABSENT;
    }

    return DUAL_LIFTED(OUT);

} handle_poke: { /////////////////////////////////////////////////////////////

    Unliftify_Known_Stable(dual);

    if (Is_Antiform(dual))
        return PANIC(Error_Bad_Antiform(dual));

    Element* poke = Known_Element(dual);

    REBINT n;
    if (Is_Integer(poke) || Is_Decimal(poke))
        n = Int32s(poke, 0);
    else if (Is_Space(poke))
        n = 0;
    else
        return PANIC(PARAM(DUAL));

    switch(i) {
      case 0:
        tf.h = n;
        break;

      case 1:
        tf.m = n;
        break;

      case 2:
        if (Is_Decimal(poke)) {
            REBDEC f = VAL_DECIMAL(poke);
            if (f < 0.0)
                panic (Error_Out_Of_Range(poke));

            tf.s = f;
            tf.n = (f - tf.s) * SEC_SEC;
        }
        else {
            tf.s = n;
            tf.n = 0;
        }
        break;

      default:
        return PANIC(PARAM(PICKER));
    }

    Tweak_Cell_Nanoseconds(time, Join_Time(&tf, false));

    return WRITEBACK(COPY(time));  // caller needs to update their time bits
}}


IMPLEMENT_GENERIC(RANDOMIZE, Is_Time)
{
    INCLUDE_PARAMS_OF_RANDOMIZE;

    const Element* time = Element_ARG(SEED);
    REBI64 secs = VAL_NANO(time);

    Set_Random(secs);
    return TRIPWIRE;
}


IMPLEMENT_GENERIC(RANDOM, Is_Time)
{
    INCLUDE_PARAMS_OF_RANDOM;

    Element* time = Element_ARG(MAX);
    REBI64 secs = VAL_NANO(time);

    REBI64 rand_secs = Random_Range(secs / SEC_SEC, Bool_ARG(SECURE)) * SEC_SEC;
    return Init_Time_Nanoseconds(OUT, rand_secs);
}


IMPLEMENT_GENERIC(MULTIPLY, Is_Time)
{
    INCLUDE_PARAMS_OF_MULTIPLY;

    REBI64 secs = VAL_NANO(ARG(VALUE1));  // guaranteed to be a time
    Value* v2 = ARG(VALUE2);

    if (Is_Integer(v2)) {
        secs *= VAL_INT64(v2);
        if (secs < -(MAX_TIME) or secs > MAX_TIME)
            return PANIC(
                Error_Type_Limit_Raw(Datatype_From_Type(TYPE_TIME))
            );
    }
    else if (Is_Decimal(v2))
        secs = cast(int64_t, secs * VAL_DECIMAL(v2));
    else
        return PANIC(PARAM(VALUE2));

    return Init_Time_Nanoseconds(OUT, secs);
}


IMPLEMENT_GENERIC(ROUND, Is_Time)
{
    INCLUDE_PARAMS_OF_ROUND;

    REBI64 secs = VAL_NANO(ARG(VALUE));  // guaranteed to be a time

    USED(ARG(EVEN)); USED(ARG(DOWN)); USED(ARG(HALF_DOWN));
    USED(ARG(FLOOR)); USED(ARG(CEILING)); USED(ARG(HALF_CEILING));

    if (not Bool_ARG(TO)) {
        Init_True(ARG(TO));  // by default make it /TO seconds
        secs = Round_Int(secs, level_, SEC_SEC);
        return Init_Time_Nanoseconds(OUT, secs);
    }

    Value* to = ARG(TO);
    if (Is_Time(to)) {
        secs = Round_Int(secs, level_, VAL_NANO(to));
        return Init_Time_Nanoseconds(OUT, secs);
    }
    else if (Is_Decimal(to)) {
        VAL_DECIMAL(to) = Round_Dec(
            cast(REBDEC, secs),
            level_,
            Dec64(to) * SEC_SEC
        );
        VAL_DECIMAL(to) /= SEC_SEC;
        return COPY(to);
    }
    else if (Is_Integer(to)) {
        mutable_VAL_INT64(to)
            = Round_Int(secs, level_, Int32(to) * SEC_SEC) / SEC_SEC;
        return COPY(to);
    }

    return PANIC(PARAM(TO));
}
