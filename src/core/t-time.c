//
//  File: %t-time.c
//  Summary: "time datatype"
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

    tf->h = (REBCNT)h;
    tf->m = (REBCNT)m;
    tf->s = (REBCNT)s;
    tf->n = (REBCNT)n;
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
//  Scan_Time: C
//
// Scan string and convert to time.  Return zero if error.
//
const REBYTE *Scan_Time(RELVAL *out, const REBYTE *cp, REBCNT len)
{
    TRASH_CELL_IF_DEBUG(out);
    cast(void, len); // !!! should len be paid attention to?

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
        return NULL; // small hole: --1:23

    // Can be:
    //    HH:MM       as part1:part2
    //    HH:MM:SS    as part1:part2:part3
    //    HH:MM:SS.DD as part1:part2:part3.part4
    //    MM:SS.DD    as part1:part2.part4

    REBINT part1 = -1;
    cp = Grab_Int(cp, &part1);
    if (part1 > MAX_HOUR)
        return NULL;

    if (*cp++ != ':')
        return NULL;

    const REBYTE *sp;

    REBINT part2 = -1;
    sp = Grab_Int(cp, &part2);
    if (part2 < 0 || sp == cp)
        return NULL;

    cp = sp;

    REBINT part3 = -1;
    if (*cp == ':') {   // optional seconds
        sp = cp + 1;
        cp = Grab_Int(sp, &part3);
        if (part3 < 0 || cp == sp)
            return NULL;
    }

    REBINT part4 = -1;
    if (*cp == '.' || *cp == ',') {
        sp = ++cp;
        cp = Grab_Int_Scale(sp, &part4, 9);
        if (part4 == 0)
            part4 = -1;
    }

    REBYTE merid;
    if (
        (UP_CASE(*cp) == 'A' || UP_CASE(*cp) == 'P')
        && (UP_CASE(cp[1]) == 'M')
    ){
        merid = cast(REBYTE, UP_CASE(*cp));
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

    Init_Time_Nanoseconds(out, nanoseconds);
    return cp;
}


//
//  MF_Time: C
//
void MF_Time(REB_MOLD *mo, const REBCEL *v, bool form)
{
    UNUSED(form); // no difference between MOLD and FORM at this time

    REB_TIMEF tf;
    Split_Time(VAL_NANO(v), &tf); // loses sign

    const char *fmt;
    if (tf.s == 0 && tf.n == 0)
        fmt = "I:2";
    else
        fmt = "I:2:2";

    if (VAL_NANO(v) < cast(REBI64, 0))
        Append_Codepoint(mo->series, '-');

    Emit(mo, fmt, tf.h, tf.m, tf.s, 0);

    if (tf.n > 0)
        Emit(mo, ".i", tf.n);
}


//
//  CT_Time: C
//
REBINT CT_Time(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    REBINT num = Cmp_Time(a, b);
    if (mode >= 0)  return (num == 0);
    if (mode == -1) return (num >= 0);
    return (num > 0);
}


//
//  MAKE_Time: C
//
REB_R MAKE_Time(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_TIME);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    switch (VAL_TYPE(arg)) {
    case REB_TIME: // just copy it (?)
        return Move_Value(out, arg);

    case REB_TEXT: { // scan using same decoding as LOAD would
        REBSIZ size;
        const REBYTE *bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_TIME);

        if (Scan_Time(out, bp, size) == NULL)
            goto no_time;

        return out; }

    case REB_INTEGER: // interpret as seconds
        if (VAL_INT64(arg) < -MAX_SECONDS || VAL_INT64(arg) > MAX_SECONDS)
            fail (Error_Out_Of_Range(arg));

        return Init_Time_Nanoseconds(out, VAL_INT64(arg) * SEC_SEC);

    case REB_DECIMAL:
        if (
            VAL_DECIMAL(arg) < cast(REBDEC, -MAX_SECONDS)
            || VAL_DECIMAL(arg) > cast(REBDEC, MAX_SECONDS)
        ){
            fail (Error_Out_Of_Range(arg));
        }
        return Init_Time_Nanoseconds(out, DEC_TO_SECS(VAL_DECIMAL(arg)));

    case REB_BLOCK: { // [hh mm ss]
        if (VAL_ARRAY_LEN_AT(arg) > 3)
            goto no_time;

        RELVAL *item = VAL_ARRAY_AT(arg);
        if (not IS_INTEGER(item))
            goto no_time;

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
            goto no_time;

        if (NOT_END(++item)) {
            if (not IS_INTEGER(item))
                goto no_time;

            if ((i = Int32(item)) < 0)
                goto no_time;

            secs += i * 60;
            if (secs > MAX_SECONDS)
                goto no_time;

            if (NOT_END(++item)) {
                if (IS_INTEGER(item)) {
                    if ((i = Int32(item)) < 0)
                        goto no_time;

                    secs += i;
                    if (secs > MAX_SECONDS)
                        goto no_time;
                }
                else if (IS_DECIMAL(item)) {
                    if (
                        secs + cast(REBI64, VAL_DECIMAL(item)) + 1
                        > MAX_SECONDS
                    ){
                        goto no_time;
                    }

                    // added in below
                }
                else
                    goto no_time;
            }
        }

        REBI64 nano = secs * SEC_SEC;
        if (IS_DECIMAL(item))
            nano += DEC_TO_SECS(VAL_DECIMAL(item));

        if (neg)
            nano = -nano;

        return Init_Time_Nanoseconds(out, nano); }

      default:
        goto no_time;
    }

  no_time:
    fail (Error_Bad_Make(REB_TIME, arg));
}


//
//  TO_Time: C
//
REB_R TO_Time(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Time(out, kind, nullptr, arg);
}


//
//  Cmp_Time: C
//
// Given two TIME!s (or DATE!s with a time componet), compare them.
//
REBINT Cmp_Time(const REBCEL *v1, const REBCEL *v2)
{
    REBI64 t1 = VAL_NANO(v1);
    REBI64 t2 = VAL_NANO(v2);

    if (t2 == t1)
        return 0;
    if (t1 > t2)
        return 1;
    return -1;
}


//
//  Pick_Time: C
//
void Pick_Time(REBVAL *out, const REBVAL *value, const REBVAL *picker)
{
    REBINT i;
    if (IS_WORD(picker)) {
        switch (VAL_WORD_SYM(picker)) {
        case SYM_HOUR:   i = 0; break;
        case SYM_MINUTE: i = 1; break;
        case SYM_SECOND: i = 2; break;
        default:
            fail (picker);
        }
    }
    else if (IS_INTEGER(picker))
        i = VAL_INT32(picker) - 1;
    else
        fail (picker);

    REB_TIMEF tf;
    Split_Time(VAL_NANO(value), &tf); // loses sign

    switch(i) {
    case 0: // hours
        Init_Integer(out, tf.h);
        break;
    case 1: // minutes
        Init_Integer(out, tf.m);
        break;
    case 2: // seconds
        if (tf.n == 0)
            Init_Integer(out, tf.s);
        else
            Init_Decimal(out, cast(REBDEC, tf.s) + (tf.n * NANO));
        break;
    default:
        Init_Nulled(out); // "out of range" behavior for pick
    }
}


//
//  Poke_Time_Immediate: C
//
void Poke_Time_Immediate(
    REBVAL *value,
    const REBVAL *picker,
    const REBVAL *poke
) {
    REBINT i;
    if (IS_WORD(picker)) {
        switch (VAL_WORD_SYM(picker)) {
        case SYM_HOUR:   i = 0; break;
        case SYM_MINUTE: i = 1; break;
        case SYM_SECOND: i = 2; break;
        default:
            fail (picker);
        }
    }
    else if (IS_INTEGER(picker))
        i = VAL_INT32(picker) - 1;
    else
        fail (picker);

    REB_TIMEF tf;
    Split_Time(VAL_NANO(value), &tf); // loses sign

    REBINT n;
    if (IS_INTEGER(poke) || IS_DECIMAL(poke))
        n = Int32s(poke, 0);
    else if (IS_BLANK(poke))
        n = 0;
    else
        fail (poke);

    switch(i) {
    case 0:
        tf.h = n;
        break;
    case 1:
        tf.m = n;
        break;
    case 2:
        if (IS_DECIMAL(poke)) {
            REBDEC f = VAL_DECIMAL(poke);
            if (f < 0.0)
                fail (Error_Out_Of_Range(poke));

            tf.s = cast(REBINT, f);
            tf.n = cast(REBINT, (f - tf.s) * SEC_SEC);
        }
        else {
            tf.s = n;
            tf.n = 0;
        }
        break;

    default:
        fail (picker);
    }

    PAYLOAD(Time, value).nanoseconds = Join_Time(&tf, false);
}


//
//  PD_Time: C
//
REB_R PD_Time(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    if (opt_setval) {
        //
        // Returning R_IMMEDIATE means that we aren't actually changing a
        // variable directly, and it will be up to the caller to decide if
        // they can meaningfully determine what variable to copy the update
        // we're making to.
        //
        Poke_Time_Immediate(pvs->out, picker, opt_setval);
        return R_IMMEDIATE;
    }

    Pick_Time(pvs->out, pvs->out, picker);
    return pvs->out;
}


//
//  REBTYPE: C
//
REBTYPE(Time)
{
    REBVAL *val = D_ARG(1);

    REBI64 secs = VAL_NANO(val);

    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBSYM sym = VAL_WORD_SYM(verb);

    // !!! This used to use IS_BINARY_ACT(), which is not available under
    // the symbol-based dispatch.  Consider doing another way.
    //
    if (
        sym == SYM_ADD
        or sym == SYM_SUBTRACT
        or sym == SYM_MULTIPLY
        or sym == SYM_DIVIDE
        or sym == SYM_REMAINDER
    ){
        REBINT  type = VAL_TYPE(arg);

        assert(arg);

        if (type == REB_TIME) {     // handle TIME - TIME cases
            REBI64 secs2 = VAL_NANO(arg);

            switch (sym) {
            case SYM_ADD:
                secs = Add_Max(REB_TIME, secs, secs2, MAX_TIME);
                return Init_Time_Nanoseconds(D_OUT, secs);

            case SYM_SUBTRACT:
                secs = Add_Max(REB_TIME, secs, -secs2, MAX_TIME);
                return Init_Time_Nanoseconds(D_OUT, secs);

            case SYM_DIVIDE:
                if (secs2 == 0)
                    fail (Error_Zero_Divide_Raw());
                return Init_Decimal(
                    D_OUT,
                    cast(REBDEC, secs) / cast(REBDEC, secs2)
                );

            case SYM_REMAINDER:
                if (secs2 == 0)
                    fail (Error_Zero_Divide_Raw());
                secs %= secs2;
                return Init_Time_Nanoseconds(D_OUT, secs);

            default:
                fail (Error_Math_Args(REB_TIME, verb));
            }
        }
        else if (type == REB_INTEGER) {     // handle TIME - INTEGER cases
            REBI64 num = VAL_INT64(arg);

            switch (VAL_WORD_SYM(verb)) {
            case SYM_ADD:
                secs = Add_Max(REB_TIME, secs, num * SEC_SEC, MAX_TIME);
                return Init_Time_Nanoseconds(D_OUT, secs);

            case SYM_SUBTRACT:
                secs = Add_Max(REB_TIME, secs, num * -SEC_SEC, MAX_TIME);
                return Init_Time_Nanoseconds(D_OUT, secs);

            case SYM_MULTIPLY:
                secs *= num;
                if (secs < -MAX_TIME || secs > MAX_TIME)
                    fail (Error_Type_Limit_Raw(Datatype_From_Kind(REB_TIME)));
                return Init_Time_Nanoseconds(D_OUT, secs);

            case SYM_DIVIDE:
                if (num == 0)
                    fail (Error_Zero_Divide_Raw());
                secs /= num;
                Init_Integer(D_OUT, secs);
                return Init_Time_Nanoseconds(D_OUT, secs);

            case SYM_REMAINDER:
                if (num == 0)
                    fail (Error_Zero_Divide_Raw());
                secs %= num;
                return Init_Time_Nanoseconds(D_OUT, secs);

            default:
                fail (Error_Math_Args(REB_TIME, verb));
            }
        }
        else if (type == REB_DECIMAL) {     // handle TIME - DECIMAL cases
            REBDEC dec = VAL_DECIMAL(arg);

            switch (VAL_WORD_SYM(verb)) {
            case SYM_ADD:
                secs = Add_Max(
                    REB_TIME,
                    secs,
                    cast(int64_t, dec * SEC_SEC),
                    MAX_TIME
                );
                return Init_Time_Nanoseconds(D_OUT, secs);

            case SYM_SUBTRACT:
                secs = Add_Max(
                    REB_TIME,
                    secs,
                    cast(int64_t, dec * -SEC_SEC),
                    MAX_TIME
                );
                return Init_Time_Nanoseconds(D_OUT, secs);

            case SYM_MULTIPLY:
                secs = cast(int64_t, secs * dec);
                return Init_Time_Nanoseconds(D_OUT, secs);

            case SYM_DIVIDE:
                if (dec == 0.0)
                    fail (Error_Zero_Divide_Raw());
                secs = cast(int64_t, secs / dec);
                return Init_Time_Nanoseconds(D_OUT, secs);

//          case SYM_REMAINDER:
//              ld = fmod(ld, VAL_DECIMAL(arg));
//              goto decTime;

            default:
                fail (Error_Math_Args(REB_TIME, verb));
            }
        }
        else if (type == REB_DATE and sym == SYM_ADD) { // TIME + DATE case
            // Swap args and call DATE datatupe:
            Move_Value(D_ARG(3), val); // (temporary location for swap)
            Move_Value(D_ARG(1), arg);
            Move_Value(D_ARG(2), D_ARG(3));
            return T_Date(frame_, verb);
        }
        fail (Error_Math_Args(REB_TIME, verb));
    }
    else {
        // unary actions
        switch (sym) {
        case SYM_COPY:
            RETURN (val);  // immediate type, just copy bits

        case SYM_ODD_Q:
            return Init_Logic(D_OUT, (SECS_FROM_NANO(secs) & 1) != 0);

        case SYM_EVEN_Q:
            return Init_Logic(D_OUT, (SECS_FROM_NANO(secs) & 1) == 0);

        case SYM_NEGATE:
            secs = -secs;
            return Init_Time_Nanoseconds(D_OUT, secs);

        case SYM_ABSOLUTE:
            if (secs < 0) secs = -secs;
            return Init_Time_Nanoseconds(D_OUT, secs);

        case SYM_ROUND: {
            INCLUDE_PARAMS_OF_ROUND;

            UNUSED(PAR(value));

            REBFLGS flags = (
                (REF(to) ? RF_TO : 0)
                | (REF(even) ? RF_EVEN : 0)
                | (REF(down) ? RF_DOWN : 0)
                | (REF(half_down) ? RF_HALF_DOWN : 0)
                | (REF(floor) ? RF_FLOOR : 0)
                | (REF(ceiling) ? RF_CEILING : 0)
                | (REF(half_ceiling) ? RF_HALF_CEILING : 0)
            );

            if (not REF(to)) {
                secs = Round_Int(secs, flags | RF_TO, SEC_SEC);
                return Init_Time_Nanoseconds(D_OUT, secs);
            }

            REBVAL *to = ARG(to);
            if (IS_TIME(to)) {
                secs = Round_Int(secs, flags, VAL_NANO(to));
                return Init_Time_Nanoseconds(D_OUT, secs);
            }
            else if (IS_DECIMAL(to)) {
                VAL_DECIMAL(to) = Round_Dec(
                    cast(REBDEC, secs),
                    flags,
                    Dec64(to) * SEC_SEC
                );
                VAL_DECIMAL(to) /= SEC_SEC;
                RESET_VAL_HEADER(to, REB_DECIMAL, CELL_MASK_NONE);
                RETURN (to);
            }
            else if (IS_INTEGER(to)) {
                VAL_INT64(to) = Round_Int(secs, 1, Int32(to) * SEC_SEC) / SEC_SEC;
                RESET_VAL_HEADER(to, REB_INTEGER, CELL_MASK_NONE);
                RETURN (to);
            }
            
            fail (PAR(to)); }

        case SYM_RANDOM: {
            INCLUDE_PARAMS_OF_RANDOM;

            UNUSED(PAR(value));

            if (REF(only))
                fail (Error_Bad_Refines_Raw());

            if (REF(seed)) {
                Set_Random(secs);
                return nullptr;
            }
            secs = Random_Range(secs / SEC_SEC, REF(secure)) * SEC_SEC;
            return Init_Time_Nanoseconds(D_OUT, secs); }

        default:
            break;
        }
    }

    return R_UNHANDLED;
}
