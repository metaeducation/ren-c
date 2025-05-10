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
//  Scan_Time: C
//
// Scan string and convert to time.  Return zero if error.
//
const Byte *Scan_Time(Value* out, const Byte *cp, REBLEN len)
{
    assert(Is_Cell_Erased(out));
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
        return nullptr; // small hole: --1:23

    // Can be:
    //    HH:MM       as part1:part2
    //    HH:MM:SS    as part1:part2:part3
    //    HH:MM:SS.DD as part1:part2:part3.part4
    //    MM:SS.DD    as part1:part2.part4

    REBINT part1 = -1;
    cp = Grab_Int(cp, &part1);
    if (part1 > MAX_HOUR)
        return nullptr;

    if (*cp++ != ':')
        return nullptr;

    const Byte *sp;

    REBINT part2 = -1;
    sp = Grab_Int(cp, &part2);
    if (part2 < 0 || sp == cp)
        return nullptr;

    cp = sp;

    REBINT part3 = -1;
    if (*cp == ':') {   // optional seconds
        sp = cp + 1;
        cp = Grab_Int(sp, &part3);
        if (part3 < 0 || cp == sp)
            return nullptr;
    }

    REBINT part4 = -1;
    if (*cp == '.' || *cp == ',') {
        sp = ++cp;
        cp = Grab_Int_Scale(sp, &part4, 9);
        if (part4 == 0)
            part4 = -1;
    }

    Byte merid;
    if (
        (UP_CASE(*cp) == 'A' || UP_CASE(*cp) == 'P')
        && (UP_CASE(cp[1]) == 'M')
    ){
        merid = cast(Byte, UP_CASE(*cp));
        cp += 2;
    }
    else
        merid = '\0';

    RESET_CELL(out, TYPE_TIME);

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

        VAL_NANO(out) = HOUR_TIME(part1) + MIN_TIME(part2) + SEC_TIME(part3);
    }
    else {
        // MM:SS mode

        if (merid != '\0')
            return nullptr;  // no AM/PM for minutes

        VAL_NANO(out) = MIN_TIME(part1) + SEC_TIME(part2);
    }

    if (part4 > 0)
        VAL_NANO(out) += part4;

    if (neg)
        VAL_NANO(out) = -VAL_NANO(out);

    return cp;
}


//
//  MF_Time: C
//
void MF_Time(Molder* mo, const Cell* v, bool form)
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
        Append_Codepoint(mo->utf8flex, '-');

    Emit(mo, fmt, tf.h, tf.m, tf.s, 0);

    if (tf.n > 0)
        Emit(mo, ".i", tf.n);
}


//
//  CT_Time: C
//
REBINT CT_Time(const Cell* a, const Cell* b, REBINT mode)
{
    REBINT num = Cmp_Time(a, b);
    if (mode >= 0)  return (num == 0);
    if (mode == -1) return (num >= 0);
    return (num > 0);
}


//
//  MAKE_Time: C
//
Bounce MAKE_Time(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == TYPE_TIME);
    UNUSED(kind);

    switch (Type_Of(arg)) {
    case TYPE_TIME: // just copy it (?)
        return Copy_Cell(out, arg);

    case TYPE_TEXT: { // scan using same decoding as LOAD would
        Size size;
        Byte *bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_TIME);

        Erase_Cell(out);
        if (Scan_Time(out, bp, size) == nullptr)
            goto no_time;

        return out; }

    case TYPE_INTEGER: // interpret as seconds
        if (VAL_INT64(arg) < -MAX_SECONDS || VAL_INT64(arg) > MAX_SECONDS)
            panic (Error_Out_Of_Range(arg));

        return Init_Time_Nanoseconds(out, VAL_INT64(arg) * SEC_SEC);

    case TYPE_DECIMAL:
        if (
            VAL_DECIMAL(arg) < cast(REBDEC, -MAX_SECONDS)
            || VAL_DECIMAL(arg) > cast(REBDEC, MAX_SECONDS)
        ){
            panic (Error_Out_Of_Range(arg));
        }
        return Init_Time_Nanoseconds(out, DEC_TO_SECS(VAL_DECIMAL(arg)));

    case TYPE_BLOCK: { // [hh mm ss]
        if (VAL_ARRAY_LEN_AT(arg) > 3)
            goto no_time;

        Cell* item = Cell_List_At(arg);
        if (not Is_Integer(item))
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
            if (not Is_Integer(item))
                goto no_time;

            if ((i = Int32(item)) < 0)
                goto no_time;

            secs += i * 60;
            if (secs > MAX_SECONDS)
                goto no_time;

            if (NOT_END(++item)) {
                if (Is_Integer(item)) {
                    if ((i = Int32(item)) < 0)
                        goto no_time;

                    secs += i;
                    if (secs > MAX_SECONDS)
                        goto no_time;
                }
                else if (Is_Decimal(item)) {
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
        if (Is_Decimal(item))
            nano += DEC_TO_SECS(VAL_DECIMAL(item));

        if (neg)
            nano = -nano;

        return Init_Time_Nanoseconds(out, nano); }

      default:
        goto no_time;
    }

  no_time:
    panic (Error_Bad_Make(TYPE_TIME, arg));
}


//
//  TO_Time: C
//
Bounce TO_Time(Value* out, enum Reb_Kind kind, const Value* arg)
{
    return MAKE_Time(out, kind, arg);
}


//
//  Cmp_Time: C
//
// Given two TIME!s (or DATE!s with a time componet), compare them.
//
REBINT Cmp_Time(const Cell* v1, const Cell* v2)
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
void Pick_Time(Value* out, const Value* value, const Value* picker)
{
    REBINT i;
    if (Is_Word(picker)) {
        switch (Cell_Word_Id(picker)) {
        case SYM_HOUR:   i = 0; break;
        case SYM_MINUTE: i = 1; break;
        case SYM_SECOND: i = 2; break;
        default:
            panic (Error_Invalid(picker));
        }
    }
    else if (Is_Integer(picker))
        i = VAL_INT32(picker) - 1;
    else
        panic (Error_Invalid(picker));

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
    Value* value,
    const Value* picker,
    const Value* poke
) {
    REBINT i;
    if (Is_Word(picker)) {
        switch (Cell_Word_Id(picker)) {
        case SYM_HOUR:   i = 0; break;
        case SYM_MINUTE: i = 1; break;
        case SYM_SECOND: i = 2; break;
        default:
            panic (Error_Invalid(picker));
        }
    }
    else if (Is_Integer(picker))
        i = VAL_INT32(picker) - 1;
    else
        panic (Error_Invalid(picker));

    REB_TIMEF tf;
    Split_Time(VAL_NANO(value), &tf); // loses sign

    REBINT n;
    if (Is_Integer(poke) || Is_Decimal(poke))
        n = Int32s(poke, 0);
    else if (Is_Blank(poke))
        n = 0;
    else
        panic (Error_Invalid(poke));

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

            tf.s = cast(REBINT, f);
            tf.n = cast(REBINT, (f - tf.s) * SEC_SEC);
        }
        else {
            tf.s = n;
            tf.n = 0;
        }
        break;

    default:
        panic (Error_Invalid(picker));
    }

    VAL_NANO(value) = Join_Time(&tf, false);
}


//
//  PD_Time: C
//
Bounce PD_Time(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    if (opt_setval) {
        //
        // Returning BOUNCE_IMMEDIATE means that we aren't actually changing a
        // variable directly, and it will be up to the caller to decide if
        // they can meaningfully determine what variable to copy the update
        // we're making to.
        //
        Poke_Time_Immediate(pvs->out, picker, opt_setval);
        return BOUNCE_IMMEDIATE;
    }

    Pick_Time(pvs->out, pvs->out, picker);
    return pvs->out;
}


//
//  REBTYPE: C
//
REBTYPE(Time)
{
    Value* val = D_ARG(1);

    REBI64 secs = VAL_NANO(val);

    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    Option(SymId) sym = Cell_Word_Id(verb);

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
        REBINT  type = Type_Of(arg);

        assert(arg);

        if (type == TYPE_TIME) {     // handle TIME - TIME cases
            REBI64 secs2 = VAL_NANO(arg);

            switch (sym) {
            case SYM_ADD:
                secs = Add_Max(TYPE_TIME, secs, secs2, MAX_TIME);
                goto fixTime;

            case SYM_SUBTRACT:
                secs = Add_Max(TYPE_TIME, secs, -secs2, MAX_TIME);
                goto fixTime;

            case SYM_DIVIDE:
                if (secs2 == 0)
                    panic (Error_Zero_Divide_Raw());
                //secs /= secs2;
                RESET_CELL(OUT, TYPE_DECIMAL);
                VAL_DECIMAL(OUT) = cast(REBDEC, secs) / cast(REBDEC, secs2);
                return OUT;

            case SYM_REMAINDER:
                if (secs2 == 0)
                    panic (Error_Zero_Divide_Raw());
                secs %= secs2;
                goto setTime;

            default:
                panic (Error_Math_Args(TYPE_TIME, verb));
            }
        }
        else if (type == TYPE_INTEGER) {     // handle TIME - INTEGER cases
            REBI64 num = VAL_INT64(arg);

            switch (Cell_Word_Id(verb)) {
            case SYM_ADD:
                secs = Add_Max(TYPE_TIME, secs, num * SEC_SEC, MAX_TIME);
                goto fixTime;

            case SYM_SUBTRACT:
                secs = Add_Max(TYPE_TIME, secs, num * -SEC_SEC, MAX_TIME);
                goto fixTime;

            case SYM_MULTIPLY:
                secs *= num;
                if (secs < -MAX_TIME || secs > MAX_TIME)
                    panic (Error_Type_Limit_Raw(Datatype_From_Kind(TYPE_TIME)));
                goto setTime;

            case SYM_DIVIDE:
                if (num == 0)
                    panic (Error_Zero_Divide_Raw());
                secs /= num;
                Init_Integer(OUT, secs);
                goto setTime;

            case SYM_REMAINDER:
                if (num == 0)
                    panic (Error_Zero_Divide_Raw());
                secs %= num;
                goto setTime;

            default:
                panic (Error_Math_Args(TYPE_TIME, verb));
            }
        }
        else if (type == TYPE_DECIMAL) {     // handle TIME - DECIMAL cases
            REBDEC dec = VAL_DECIMAL(arg);

            switch (Cell_Word_Id(verb)) {
            case SYM_ADD:
                secs = Add_Max(
                    TYPE_TIME,
                    secs,
                    cast(int64_t, dec * SEC_SEC),
                    MAX_TIME
                );
                goto fixTime;

            case SYM_SUBTRACT:
                secs = Add_Max(
                    TYPE_TIME,
                    secs,
                    cast(int64_t, dec * -SEC_SEC),
                    MAX_TIME
                );
                goto fixTime;

            case SYM_MULTIPLY:
                secs = cast(int64_t, cast(REBDEC, secs) * dec);
                goto setTime;

            case SYM_DIVIDE:
                if (dec == 0.0)
                    panic (Error_Zero_Divide_Raw());
                secs = cast(int64_t, cast(REBDEC, secs) / dec);
                goto setTime;

//          case SYM_REMAINDER:
//              ld = fmod(ld, VAL_DECIMAL(arg));
//              goto decTime;

            default:
                panic (Error_Math_Args(TYPE_TIME, verb));
            }
        }
        else if (type == TYPE_DATE and sym == SYM_ADD) { // TIME + DATE case
            // Swap args and call DATE datatupe:
            Copy_Cell(D_ARG(3), val); // (temporary location for swap)
            Copy_Cell(D_ARG(1), arg);
            Copy_Cell(D_ARG(2), D_ARG(3));
            return T_Date(level_, verb);
        }
        panic (Error_Math_Args(TYPE_TIME, verb));
    }
    else {
        // unary actions
        switch (sym) {

        case SYM_ODD_Q:
            return Init_Logic(OUT, (SECS_FROM_NANO(secs) & 1) != 0);

        case SYM_EVEN_Q:
            return Init_Logic(OUT, (SECS_FROM_NANO(secs) & 1) == 0);

        case SYM_NEGATE:
            secs = -secs;
            goto setTime;

        case SYM_ABSOLUTE:
            if (secs < 0) secs = -secs;
            goto setTime;

        case SYM_ROUND: {
            INCLUDE_PARAMS_OF_ROUND;

            UNUSED(PARAM(VALUE));

            Flags flags = (
                (Bool_ARG(TO) ? RF_TO : 0)
                | (Bool_ARG(EVEN) ? RF_EVEN : 0)
                | (Bool_ARG(DOWN) ? RF_DOWN : 0)
                | (Bool_ARG(HALF_DOWN) ? RF_HALF_DOWN : 0)
                | (Bool_ARG(FLOOR) ? RF_FLOOR : 0)
                | (Bool_ARG(CEILING) ? RF_CEILING : 0)
                | (Bool_ARG(HALF_CEILING) ? RF_HALF_CEILING : 0)
            );

            if (Bool_ARG(TO)) {
                arg = ARG(SCALE);
                if (Is_Time(arg)) {
                    secs = Round_Int(secs, flags, VAL_NANO(arg));
                }
                else if (Is_Decimal(arg)) {
                    VAL_DECIMAL(arg) = Round_Dec(
                        cast(REBDEC, secs),
                        flags,
                        Dec64(arg) * SEC_SEC
                    );
                    VAL_DECIMAL(arg) /= SEC_SEC;
                    RESET_CELL(arg, TYPE_DECIMAL);
                    Copy_Cell(OUT, ARG(SCALE));
                    return OUT;
                }
                else if (Is_Integer(arg)) {
                    VAL_INT64(arg) = Round_Int(secs, 1, Int32(arg) * SEC_SEC) / SEC_SEC;
                    RESET_CELL(arg, TYPE_INTEGER);
                    Copy_Cell(OUT, ARG(SCALE));
                    return OUT;
                }
                else
                    panic (Error_Invalid(arg));
            }
            else {
                secs = Round_Int(secs, flags | RF_TO, SEC_SEC);
            }
            goto fixTime; }

        case SYM_RANDOM: {
            INCLUDE_PARAMS_OF_RANDOM;

            UNUSED(PARAM(VALUE));

            if (Bool_ARG(ONLY))
                panic (Error_Bad_Refines_Raw());

            if (Bool_ARG(SEED)) {
                Set_Random(secs);
                return nullptr;
            }
            secs = Random_Range(secs / SEC_SEC, Bool_ARG(SECURE)) * SEC_SEC;
            goto fixTime; }

        default:
            break;
        }
    }
    panic (Error_Illegal_Action(TYPE_TIME, verb));

fixTime:
setTime:
    RESET_CELL(OUT, TYPE_TIME);
    VAL_NANO(OUT) = secs;
    return OUT;
}
