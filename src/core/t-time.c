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
REBI64 Join_Time(REB_TIMEF *tf, REBOOL neg)
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
const REBYTE *Scan_Time(const REBYTE *cp, REBCNT len, REBVAL *value)
{
    const REBYTE  *sp;
    REBYTE  merid = '\0';
    REBOOL  neg = FALSE;
    REBINT  part1, part2, part3 = -1;
    REBINT  part4 = -1;

    if (*cp == '-') cp++, neg = TRUE;
    else if (*cp == '+') cp++;

    if (*cp == '-' || *cp == '+') return 0; // small hole: --1:23

    // Can be:
    //    HH:MM       as part1:part2
    //    HH:MM:SS    as part1:part2:part3
    //    HH:MM:SS.DD as part1:part2:part3.part4
    //    MM:SS.DD    as part1:part2.part4
    cp = Grab_Int(cp, &part1);
    if (part1 > MAX_HOUR) return 0;
    if (*cp++ != ':') return 0;
    sp = Grab_Int(cp, &part2);
    if (part2 < 0 || sp == cp) return 0;
    cp = sp;
    if (*cp == ':') {   // optional seconds
        sp = cp + 1;
        cp = Grab_Int(sp, &part3);
        if (part3 < 0 || cp == sp) return 0;  //part3 = -1;
    }
    if (*cp == '.' || *cp == ',') {
        sp = ++cp;
        cp = Grab_Int_Scale(sp, &part4, 9);
        if (part4 == 0) part4 = -1;
    }
    if ((UP_CASE(*cp) == 'A' || UP_CASE(*cp) == 'P') && (UP_CASE(cp[1]) == 'M')) {
        merid = cast(REBYTE, UP_CASE(*cp));
        cp += 2;
    }

    if (part3 >= 0 || part4 < 0) {  // HH:MM mode
        if (merid != '\0') {
            if (part1 > 12) return 0;
            if (part1 == 12) part1 = 0;
            if (merid == 'P') part1 += 12;
        }
        if (part3 < 0) part3 = 0;
        VAL_TIME(value) = HOUR_TIME(part1) + MIN_TIME(part2) + SEC_TIME(part3);
    }
    else {
        // MM:SS mode

        if (merid != '\0')
            return NULL; // no AM/PM for minutes

        VAL_TIME(value) = MIN_TIME(part1) + SEC_TIME(part2);
    }

    if (part4 > 0) VAL_TIME(value) += part4;

    if (neg) VAL_TIME(value) = -VAL_TIME(value);
    VAL_RESET_HEADER(value, REB_TIME);

    return cp;
}


//
//  Emit_Time: C
//
void Emit_Time(REB_MOLD *mold, const REBVAL *value)
{
    REB_TIMEF tf;
    const char *fmt;

    Split_Time(VAL_TIME(value), &tf); // loses sign

    if (tf.s == 0 && tf.n == 0) fmt = "I:2";
    else fmt = "I:2:2";

    if (VAL_TIME(value) < cast(REBI64, 0))
        Append_Codepoint_Raw(mold->series, '-');

    Emit(mold, fmt, tf.h, tf.m, tf.s, 0);

    if (tf.n > 0) Emit(mold, ".i", tf.n);
}


//
//  CT_Time: C
//
REBINT CT_Time(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    REBINT num = Cmp_Time(a, b);
    if (mode >= 0)  return (num == 0);
    if (mode == -1) return (num >= 0);
    return (num > 0);
}


//
//  Make_Time: C
// 
// Returns NO_TIME if error.
//
REBI64 Make_Time(const REBVAL *val)
{
    REBI64 secs = 0;

    if (IS_TIME(val)) {
        secs = VAL_TIME(val);
    }
    else if (IS_STRING(val)) {
        REBYTE *bp;
        REBCNT len;
        bp = Temp_Byte_Chars_May_Fail(val, MAX_SCAN_TIME, &len, FALSE);
        REBVAL temp;
        if (!Scan_Time(bp, len, &temp)) goto no_time;
        secs = VAL_TIME(&temp);
    }
    else if (IS_INTEGER(val)) {
        if (VAL_INT64(val) < -MAX_SECONDS || VAL_INT64(val) > MAX_SECONDS)
            fail (Error_Out_Of_Range(val));
        secs = VAL_INT64(val) * SEC_SEC;
    }
    else if (IS_DECIMAL(val)) {
        if (VAL_DECIMAL(val) < (REBDEC)(-MAX_SECONDS) || VAL_DECIMAL(val) > (REBDEC)MAX_SECONDS)
            fail (Error_Out_Of_Range(val));
        secs = DEC_TO_SECS(VAL_DECIMAL(val));
    }
    else if (ANY_ARRAY(val) && VAL_ARRAY_LEN_AT(val) <= 3) {
        REBOOL neg = FALSE;
        REBI64 i;
        RELVAL *item;

        item = VAL_ARRAY_AT(val);
        if (!IS_INTEGER(val)) goto no_time;
        i = Int32(KNOWN(item));
        if (i < 0) i = -i, neg = TRUE;
        secs = i * 3600;
        if (secs > MAX_SECONDS) goto no_time;

        if (NOT_END(++item)) {
            if (!IS_INTEGER(item)) goto no_time;
            if ((i = Int32(KNOWN(item))) < 0) goto no_time;
            secs += i * 60;
            if (secs > MAX_SECONDS) goto no_time;

            if (NOT_END(++item)) {
                if (IS_INTEGER(item)) {
                    if ((i = Int32(KNOWN(item))) < 0) goto no_time;
                    secs += i;
                    if (secs > MAX_SECONDS) goto no_time;
                }
                else if (IS_DECIMAL(item)) {
                    if (secs + (REBI64)VAL_DECIMAL(item) + 1 > MAX_SECONDS) goto no_time;
                    // added in below
                }
                else goto no_time;
            }
        }
        secs *= SEC_SEC;
        if (IS_DECIMAL(item)) secs += DEC_TO_SECS(VAL_DECIMAL(item));
        if (neg) secs = -secs;
    }
    else
        no_time: return NO_TIME;

    return secs;
}


//
//  MAKE_Time: C
//
void MAKE_Time(REBVAL *out, enum Reb_Kind type, const REBVAL *arg)
{
    REBI64 secs = Make_Time(arg);
    if (secs == NO_TIME)
        fail (Error_Bad_Make(REB_TIME, arg));

    VAL_RESET_HEADER(out, REB_TIME);
    VAL_TIME(out) = secs;
    VAL_DATE(out).bits = 0;
}


//
//  TO_Time: C
//
void TO_Time(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    MAKE_Time(out, kind, arg);
}


//
//  Cmp_Time: C
// 
// Given two times, compare them.
//
REBINT Cmp_Time(const RELVAL *v1, const RELVAL *v2)
{
    REBI64 t1 = VAL_TIME(v1);
    REBI64 t2 = VAL_TIME(v2);

    if (t1 == NO_TIME) t1 = 0L;
    if (t2 == NO_TIME) t2 = 0L;
    if (t2 == t1) return 0;
    if (t1 > t2) return 1;
    return -1;
}


//
//  PD_Time: C
//
REBINT PD_Time(REBPVS *pvs)
{
    const REBVAL *sel = pvs->selector;
    const REBVAL *setval;
    REBINT i;
    REBINT n;
    REBDEC f;
    REB_TIMEF tf;

    if (IS_WORD(sel)) {
        switch (VAL_WORD_SYM(sel)) {
        case SYM_HOUR:   i = 0; break;
        case SYM_MINUTE: i = 1; break;
        case SYM_SECOND: i = 2; break;
        default:
            fail (Error_Bad_Path_Select(pvs));
        }
    }
    else if (IS_INTEGER(sel))
        i = VAL_INT32(sel) - 1;
    else
        fail (Error_Bad_Path_Select(pvs));

    Split_Time(VAL_TIME(pvs->value), &tf); // loses sign

    if (!(setval = pvs->opt_setval)) {
        REBVAL *store = pvs->store;

        switch(i) {
        case 0: // hours
            SET_INTEGER(store, tf.h);
            break;
        case 1:
            SET_INTEGER(store, tf.m);
            break;
        case 2:
            if (tf.n == 0)
                SET_INTEGER(store, tf.s);
            else
                SET_DECIMAL(store, cast(REBDEC, tf.s) + (tf.n * NANO));
            break;
        default:
            return PE_NONE;
        }

        return PE_USE_STORE;
    }
    else {
        if (IS_INTEGER(setval) || IS_DECIMAL(setval))
            n = Int32s(setval, 0);
        else if (IS_BLANK(setval))
            n = 0;
        else
            fail (Error_Bad_Path_Set(pvs));

        switch(i) {
        case 0:
            tf.h = n;
            break;
        case 1:
            tf.m = n;
            break;
        case 2:
            if (IS_DECIMAL(setval)) {
                f = VAL_DECIMAL(setval);
                if (f < 0.0)
                    fail (Error_Out_Of_Range(setval));

                tf.s = cast(REBINT, f);
                tf.n = cast(REBINT, (f - tf.s) * SEC_SEC);
            }
            else {
                tf.s = n;
                tf.n = 0;
            }
            break;
        default:
            fail (Error_Bad_Path_Select(pvs));
        }

        VAL_TIME(pvs->value) = Join_Time(&tf, FALSE);
        return PE_OK;
    }
}


//
//  REBTYPE: C
//
REBTYPE(Time)
{
    REBI64  secs;
    REBVAL  *val;
    REBVAL  *arg = NULL;
    REBI64  num;

    val = D_ARG(1);

    secs = VAL_TIME(val); // note: not always valid REB_TIME (e.g. MAKE)

    if (D_ARGC > 1) {
        arg = D_ARG(2);
    }

    // !!! This used to use IS_BINARY_ACT(), which is not available under
    // the symbol-based dispatch.  Consider doing another way.
    //
    if (
        action == SYM_ADD
        || action == SYM_SUBTRACT
        || action == SYM_MULTIPLY
        || action == SYM_DIVIDE
        || action == SYM_REMAINDER
    ){
        REBINT  type = VAL_TYPE(arg);

        assert(arg);

        if (type == REB_TIME) {     // handle TIME - TIME cases
            REBI64  secs2 = VAL_TIME(arg);

            switch (action) {

            case SYM_ADD:
                secs = Add_Max(REB_TIME, secs, secs2, MAX_TIME);
                goto fixTime;

            case SYM_SUBTRACT:
                secs = Add_Max(REB_TIME, secs, -secs2, MAX_TIME);
                goto fixTime;

            case SYM_DIVIDE:
                if (secs2 == 0) fail (Error(RE_ZERO_DIVIDE));
                //secs /= secs2;
                VAL_RESET_HEADER(D_OUT, REB_DECIMAL);
                VAL_DECIMAL(D_OUT) = (REBDEC)secs / (REBDEC)secs2;
                return R_OUT;

            case SYM_REMAINDER:
                if (secs2 == 0) fail (Error(RE_ZERO_DIVIDE));
                secs %= secs2;
                goto setTime;
            }
        }
        else if (type == REB_INTEGER) {     // handle TIME - INTEGER cases

            num = VAL_INT64(arg);

            switch(action) {
            case SYM_ADD:
                secs = Add_Max(REB_TIME, secs, num * SEC_SEC, MAX_TIME);
                goto fixTime;

            case SYM_SUBTRACT:
                secs = Add_Max(REB_TIME, secs, num * -SEC_SEC, MAX_TIME);
                goto fixTime;

            case SYM_MULTIPLY:
                secs *= num;
                if (secs < -MAX_TIME || secs > MAX_TIME)
                    fail (Error(RE_TYPE_LIMIT, Get_Type(REB_TIME)));
                goto setTime;

            case SYM_DIVIDE:
                if (num == 0) fail (Error(RE_ZERO_DIVIDE));
                secs /= num;
                SET_INTEGER(D_OUT, secs);
                goto setTime;

            case SYM_REMAINDER:
                if (num == 0) fail (Error(RE_ZERO_DIVIDE));
                secs %= num;
                goto setTime;
            }
        }
        else if (type == REB_DECIMAL) {     // handle TIME - DECIMAL cases
            REBDEC dec = VAL_DECIMAL(arg);

            switch(action) {
            case SYM_ADD:
                secs = Add_Max(REB_TIME, secs, (i64)(dec * SEC_SEC), MAX_TIME);
                goto fixTime;

            case SYM_SUBTRACT:
                secs = Add_Max(REB_TIME, secs, (i64)(dec * -SEC_SEC), MAX_TIME);
                goto fixTime;

            case SYM_MULTIPLY:
                secs = (REBI64)(secs * dec);
                goto setTime;

            case SYM_DIVIDE:
                if (dec == 0.0) fail (Error(RE_ZERO_DIVIDE));
                secs = (REBI64)(secs / dec);
                goto setTime;

//          case SYM_REMAINDER:
//              ld = fmod(ld, VAL_DECIMAL(arg));
//              goto decTime;
            }
        }
        else if (type == REB_DATE && action == SYM_ADD) { // TIME + DATE case
            // Swap args and call DATE datatupe:
            *D_ARG(3) = *val; // (temporary location for swap)
            *D_ARG(1) = *arg;
            *D_ARG(2) = *D_ARG(3);
            return T_Date(frame_, action);
        }
        fail (Error_Math_Args(REB_TIME, action));
    }
    else {
        // unary actions
        switch(action) {

        case SYM_ODD_Q:
            return ((SECS_IN(secs) & 1) != 0) ? R_TRUE : R_FALSE;

        case SYM_EVEN_Q:
            return ((SECS_IN(secs) & 1) == 0) ? R_TRUE : R_FALSE;

        case SYM_NEGATE:
            secs = -secs;
            goto setTime;

        case SYM_ABSOLUTE:
            if (secs < 0) secs = -secs;
            goto setTime;

        case SYM_ROUND:
            if (D_REF(2)) {
                arg = D_ARG(3);
                if (IS_TIME(arg)) {
                    secs = Round_Int(secs, Get_Round_Flags(frame_), VAL_TIME(arg));
                }
                else if (IS_DECIMAL(arg)) {
                    VAL_DECIMAL(arg) = Round_Dec(
                        cast(REBDEC, secs),
                        Get_Round_Flags(frame_),
                        Dec64(arg) * SEC_SEC
                    );
                    VAL_DECIMAL(arg) /= SEC_SEC;
                    VAL_RESET_HEADER(arg, REB_DECIMAL);
                    *D_OUT = *D_ARG(3);
                    return R_OUT;
                }
                else if (IS_INTEGER(arg)) {
                    VAL_INT64(arg) = Round_Int(secs, 1, Int32(arg) * SEC_SEC) / SEC_SEC;
                    VAL_RESET_HEADER(arg, REB_INTEGER);
                    *D_OUT = *D_ARG(3);
                    return R_OUT;
                }
                else fail (Error_Invalid_Arg(arg));
            }
            else {
                secs = Round_Int(secs, Get_Round_Flags(frame_) | 1, SEC_SEC);
            }
            goto fixTime;

        case SYM_RANDOM:
            if (D_REF(2)) {
                Set_Random(secs);
                return R_VOID;
            }
            secs = Random_Range(secs / SEC_SEC, D_REF(3)) * SEC_SEC;
            goto fixTime;

        case SYM_PICK:
            assert(arg);

            Pick_Path(D_OUT, val, arg, 0);
            return R_OUT;

///     case SYM_POKE:
///         Pick_Path(D_OUT, val, arg, D_ARG(3));
///         *D_OUT = *D_ARG(3);
///         return R_OUT;
///
        }
    }
    fail (Error_Illegal_Action(REB_TIME, action));

fixTime:
setTime:
    VAL_TIME(D_OUT) = secs;
    VAL_RESET_HEADER(D_OUT, REB_TIME);
    return R_OUT;
}
