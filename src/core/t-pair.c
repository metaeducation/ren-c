//
//  File: %t-pair.c
//  Summary: "pair datatype"
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

#include "sys-core.h"


//
//  CT_Pair: C
//
REBINT CT_Pair(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode >= 0) return Cmp_Pair(a, b) == 0; // works for INTEGER=0 too (spans x y)
    if (IS_PAIR(b) && 0 == VAL_INT64(b)) { // for negative? and positive?
        if (mode == -1)
            return (VAL_PAIR_X(a) >= 0 || VAL_PAIR_Y(a) >= 0); // not LT
        return (VAL_PAIR_X(a) > 0 && VAL_PAIR_Y(a) > 0); // NOT LTE
    }
    return -1;
}


//
//  MAKE_Pair: C
//
void MAKE_Pair(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_PAIR);
    UNUSED(kind);

    if (IS_PAIR(arg)) {
        Move_Value(out, arg);
        return;
    }

    if (IS_STRING(arg)) {
        //
        // -1234567890x-1234567890
        //
        REBCNT len;
        REBYTE *bp
            = Temp_Byte_Chars_May_Fail(arg, VAL_LEN_AT(arg), &len, FALSE);

        if (NULL == Scan_Pair(out, bp, len))
            goto bad_make;

        return;
    }

    REBDEC x;
    REBDEC y;

    if (IS_INTEGER(arg)) {
        x = VAL_INT32(arg);
        y = VAL_INT32(arg);
    }
    else if (IS_DECIMAL(arg)) {
        x = VAL_DECIMAL(arg);
        y = VAL_DECIMAL(arg);
    }
    else if (IS_BLOCK(arg) && VAL_LEN_AT(arg) == 2) {
        RELVAL *item = VAL_ARRAY_AT(arg);

        if (IS_INTEGER(item))
            x = cast(REBDEC, VAL_INT64(item));
        else if (IS_DECIMAL(item))
            x = cast(REBDEC, VAL_DECIMAL(item));
        else
            goto bad_make;

        ++item;
        if (IS_END(item))
            goto bad_make;

        if (IS_INTEGER(item))
            y = cast(REBDEC, VAL_INT64(item));
        else if (IS_DECIMAL(item))
            y = cast(REBDEC, VAL_DECIMAL(item));
        else
            goto bad_make;
    }
    else
        goto bad_make;

    SET_PAIR(out, x, y);
    return;

bad_make:
    fail (Error_Bad_Make(REB_PAIR, arg));
}


//
//  TO_Pair: C
//
void TO_Pair(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    MAKE_Pair(out, kind, arg);
}


//
//  Cmp_Pair: C
//
// Given two pairs, compare them.
//
REBINT Cmp_Pair(const RELVAL *t1, const RELVAL *t2)
{
    REBDEC diff;

    if ((diff = VAL_PAIR_Y(t1) - VAL_PAIR_Y(t2)) == 0)
        diff = VAL_PAIR_X(t1) - VAL_PAIR_X(t2);
    return (diff > 0.0) ? 1 : ((diff < 0.0) ? -1 : 0);
}


//
//  Min_Max_Pair: C
//
void Min_Max_Pair(REBVAL *out, const REBVAL *a, const REBVAL *b, REBOOL maxed)
{
    // !!! This used to use REBXYF (a structure containing "X" and "Y" as
    // floats).  It's not clear why floats would be preferred here, and
    // also not clear what the types should be if they were mixed (INTEGER!
    // vs. DECIMAL! for the X or Y).  REBXYF is now a structure only used
    // in GOB! so it is taken out of mention here.

    float ax;
    float ay;
    if (IS_PAIR(a)) {
        ax = VAL_PAIR_X(a);
        ay = VAL_PAIR_Y(a);
    }
    else if (IS_INTEGER(a))
        ax = ay = cast(REBDEC, VAL_INT64(a));
    else
        fail (a);

    float bx;
    float by;
    if (IS_PAIR(b)) {
        bx = VAL_PAIR_X(b);
        by = VAL_PAIR_Y(b);
    }
    else if (IS_INTEGER(b))
        bx = by = cast(REBDEC, VAL_INT64(b));
    else
        fail (b);

    if (maxed)
        SET_PAIR(out, MAX(ax, bx), MAX(ay, by));
    else
        SET_PAIR(out, MIN(ax, bx), MIN(ay, by));
}


//
//  PD_Pair: C
//
REBINT PD_Pair(REBPVS *pvs)
{
    const REBVAL *sel = pvs->selector;
    REBINT n = 0;
    REBDEC dec;

    if (IS_WORD(sel)) {
        if (VAL_WORD_SYM(sel) == SYM_X)
            n = 1;
        else if (VAL_WORD_SYM(sel) == SYM_Y)
            n = 2;
        else
            fail (Error_Bad_Path_Select(pvs));
    }
    else if (IS_INTEGER(sel)) {
        n = Int32(sel);
        if (n != 1 && n != 2)
            fail (Error_Bad_Path_Select(pvs));
    }
    else fail (Error_Bad_Path_Select(pvs));

    if (pvs->opt_setval) {
        const REBVAL *setval = pvs->opt_setval;

        if (IS_INTEGER(setval))
            dec = cast(REBDEC, VAL_INT64(setval));
        else if (IS_DECIMAL(setval))
            dec = VAL_DECIMAL(setval);
        else
            fail (Error_Bad_Path_Set(pvs));

        if (n == 1)
            VAL_PAIR_X(pvs->value) = dec;
        else
            VAL_PAIR_Y(pvs->value) = dec;
    }
    else {
        dec = (n == 1 ? VAL_PAIR_X(pvs->value) : VAL_PAIR_Y(pvs->value));
        Init_Decimal(pvs->store, dec);
        return PE_USE_STORE;
    }

    return PE_OK;
}


static void Get_Math_Arg_For_Pair(
    REBDEC *x_out,
    REBDEC *y_out,
    REBVAL *arg,
    REBSYM action
){
    switch (VAL_TYPE(arg)) {
    case REB_PAIR:
        *x_out = VAL_PAIR_X(arg);
        *y_out = VAL_PAIR_Y(arg);
        break;

    case REB_INTEGER:
        *x_out = *y_out = cast(REBDEC, VAL_INT64(arg));
        break;

    case REB_DECIMAL:
    case REB_PERCENT:
        *x_out = *y_out = cast(REBDEC, VAL_DECIMAL(arg));
        break;

    default:
        fail (Error_Math_Args(REB_PAIR, action));
    }

}


//
//  REBTYPE: C
//
REBTYPE(Pair)
{
    REBVAL *val = D_ARG(1);

    REBDEC x1 = VAL_PAIR_X(val);
    REBDEC y1 = VAL_PAIR_Y(val);

    REBDEC x2;
    REBDEC y2;

    switch (action) {

    case SYM_COPY: {
        goto setPair;
    }

    case SYM_ADD:
        Get_Math_Arg_For_Pair(&x2, &y2, D_ARG(2), action);
        x1 += x2;
        y1 += y2;
        goto setPair;

    case SYM_SUBTRACT:
        Get_Math_Arg_For_Pair(&x2, &y2, D_ARG(2), action);
        x1 -= x2;
        y1 -= y2;
        goto setPair;

    case SYM_MULTIPLY:
        Get_Math_Arg_For_Pair(&x2, &y2, D_ARG(2), action);
        x1 *= x2;
        y1 *= y2;
        goto setPair;

    case SYM_DIVIDE:
    case SYM_REMAINDER:
        Get_Math_Arg_For_Pair(&x2, &y2, D_ARG(2), action);
        if (x2 == 0 || y2 == 0) fail (Error_Zero_Divide_Raw());
        if (action == SYM_DIVIDE) {
            x1 /= x2;
            y1 /= y2;
        }
        else {
            x1 = cast(REBDEC, fmod(x1, x2));
            y1 = cast(REBDEC, fmod(y1, y2));
        }
        goto setPair;

    case SYM_NEGATE:
        x1 = -x1;
        y1 = -y1;
        goto setPair;

    case SYM_ABSOLUTE:
        if (x1 < 0) x1 = -x1;
        if (y1 < 0) y1 = -y1;
        goto setPair;

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

        if (REF(to)) {
            x1 = Round_Dec(x1, flags, Dec64(ARG(scale)));
            y1 = Round_Dec(y1, flags, Dec64(ARG(scale)));
        }
        else {
            x1 = Round_Dec(x1, flags | RF_TO, 1.0L);
            y1 = Round_Dec(y1, flags | RF_TO, 1.0L);
        }
        goto setPair; }

    case SYM_REVERSE:
        x2 = x1;
        x1 = y1;
        y1 = x2;
        goto setPair;

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(only))
            fail (Error_Bad_Refines_Raw());
        if (REF(seed))
            fail (Error_Bad_Refines_Raw());

        x1 = cast(REBDEC, Random_Range(cast(REBINT, x1), REF(secure)));
        y1 = cast(REBDEC, Random_Range(cast(REBINT, y1), REF(secure)));
        goto setPair; }

    case SYM_PICK_P: {
        REBVAL *arg = D_ARG(2);
        REBINT n;
        if (IS_WORD(arg)) {
            if (VAL_WORD_SYM(arg) == SYM_X)
                n = 0;
            else if (VAL_WORD_SYM(arg) == SYM_Y)
                n = 1;
            else
                fail (arg);
        }
        else {
            n = Get_Num_From_Arg(arg);
            if (n < 1 || n > 2) fail (Error_Out_Of_Range(arg));
            n--;
        }
///     case SYM_POKE:
///         if (action == SYM_POKE) {
///             arg = D_ARG(3);
///             if (IS_INTEGER(arg)) {
///                 if (index == 0) x1 = VAL_INT32(arg);
///                 else y1 = VAL_INT32(arg);
///             }
///             else if (IS_DECIMAL(arg)) {
///                 if (index == 0) x1 = (REBINT)VAL_DECIMAL(arg);
///                 else y1 = (REBINT)VAL_DECIMAL(arg);
///             } else
///                 fail (arg);
///             goto setPair;
///         }
        Init_Decimal(D_OUT, n == 0 ? x1 : y1);
        return R_OUT; }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PAIR, action));

setPair:
    SET_PAIR(D_OUT, x1, y1);
    return R_OUT;
}

