//
//  File: %t-money.c
//  Summary: "extended precision datatype"
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
#include "sys-deci-funcs.h"


//
//  CT_Money: C
//
REBINT CT_Money(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    REBOOL e = deci_is_equal(VAL_MONEY_AMOUNT(a), VAL_MONEY_AMOUNT(b));

    if (mode < 0) {
        REBOOL g = deci_is_lesser_or_equal(
            VAL_MONEY_AMOUNT(b), VAL_MONEY_AMOUNT(a)
        );
        if (mode == -1)
            e = (e or g);
        else
            e = (g and not e);
    }

    return e ? 1 : 0;
}


//
//  MAKE_Money: C
//
void MAKE_Money(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_MONEY);
    UNUSED(kind);

    switch (VAL_TYPE(arg)) {
    case REB_INTEGER:
        Init_Money(out, int_to_deci(VAL_INT64(arg)));
        break;

    case REB_DECIMAL:
    case REB_PERCENT:
        Init_Money(out, decimal_to_deci(VAL_DECIMAL(arg)));
        break;

    case REB_MONEY:
        Move_Value(out, arg);
        return;

    case REB_STRING: {
        REBYTE *bp = Analyze_String_For_Scan(NULL, arg, MAX_SCAN_MONEY);

        const REBYTE *end;
        Init_Money(out, string_to_deci(bp, &end));
        if (end == bp or *end != '\0')
            goto bad_make;
        break; }

//      case REB_ISSUE:
    case REB_BINARY:
        Bin_To_Money_May_Fail(out, arg);
        break;

    case REB_LOGIC:
        Init_Money(out, int_to_deci(VAL_LOGIC(arg) ? 1 : 0));
        break;

    default:
    bad_make:
        fail (Error_Bad_Make(REB_MONEY, arg));
    }

    RESET_VAL_HEADER(out, REB_MONEY);
}


//
//  TO_Money: C
//
void TO_Money(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    MAKE_Money(out, kind, arg);
}


//
//  MF_Money: C
//
void MF_Money(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    UNUSED(form);

    if (mo->opts & MOLD_FLAG_LIMIT) {
        // !!! In theory, emits should pay attention to the mold options,
        // at least the limit.
    }

    REBYTE buf[60];
    REBINT len = deci_to_string(buf, VAL_MONEY_AMOUNT(v), '$', '.');
    Append_Unencoded_Len(mo->series, s_cast(buf), len);
}


//
//  Bin_To_Money_May_Fail: C
//
// Will successfully convert or fail (longjmp) with an error.
//
void Bin_To_Money_May_Fail(REBVAL *result, const REBVAL *val)
{
    if (not IS_BINARY(val))
        fail (Error_Invalid(val));

    REBCNT len = VAL_LEN_AT(val);
    if (len > 12)
        len = 12;

    REBYTE buf[MAX_HEX_LEN+4] = {0}; // binary to convert
    memcpy(buf, VAL_BIN_AT(val), len);
    memcpy(buf + 12 - len, buf, len); // shift to right side
    memset(buf, 0, 12 - len);
    Init_Money(result, binary_to_deci(buf));
}


static REBVAL *Math_Arg_For_Money(REBVAL *store, REBVAL *arg, REBSYM verb)
{
    if (IS_MONEY(arg))
        return arg;

    if (IS_INTEGER(arg)) {
        Init_Money(store, int_to_deci(VAL_INT64(arg)));
        return store;
    }

    if (IS_DECIMAL(arg) or IS_PERCENT(arg)) {
        Init_Money(store, decimal_to_deci(VAL_DECIMAL(arg)));
        return store;
    }

    fail (Error_Math_Args(REB_MONEY, verb));
}


//
//  REBTYPE: C
//
REBTYPE(Money)
{
    REBVAL *val = D_ARG(1);
    REBVAL *arg;

    switch (verb) {
    case SYM_ADD:
        arg = Math_Arg_For_Money(D_OUT, D_ARG(2), verb);
        Init_Money(D_OUT, deci_add(
            VAL_MONEY_AMOUNT(val), VAL_MONEY_AMOUNT(arg)
        ));
        break;

    case SYM_SUBTRACT:
        arg = Math_Arg_For_Money(D_OUT, D_ARG(2), verb);
        Init_Money(D_OUT, deci_subtract(
            VAL_MONEY_AMOUNT(val), VAL_MONEY_AMOUNT(arg)
        ));
        break;

    case SYM_MULTIPLY:
        arg = Math_Arg_For_Money(D_OUT, D_ARG(2), verb);
        Init_Money(D_OUT, deci_multiply(
            VAL_MONEY_AMOUNT(val), VAL_MONEY_AMOUNT(arg)
        ));
        break;

    case SYM_DIVIDE:
        arg = Math_Arg_For_Money(D_OUT, D_ARG(2), verb);
        Init_Money(D_OUT, deci_divide(
            VAL_MONEY_AMOUNT(val), VAL_MONEY_AMOUNT(arg)
        ));
        break;

    case SYM_REMAINDER:
        arg = Math_Arg_For_Money(D_OUT, D_ARG(2), verb);
        Init_Money(D_OUT, deci_mod(
            VAL_MONEY_AMOUNT(val), VAL_MONEY_AMOUNT(arg)
        ));
        break;

    case SYM_NEGATE:
        val->payload.money.s = !val->payload.money.s;
        Move_Value(D_OUT, D_ARG(1));
        return R_OUT;

    case SYM_ABSOLUTE:
        val->payload.money.s = 0;
        Move_Value(D_OUT, D_ARG(1));
        return R_OUT;

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

        REBVAL *scale = ARG(scale);

        DECLARE_LOCAL (temp);
        if (REF(to)) {
            if (IS_INTEGER(scale))
                Init_Money(temp, int_to_deci(VAL_INT64(scale)));
            else if (IS_DECIMAL(scale) or IS_PERCENT(scale))
                Init_Money(temp, decimal_to_deci(VAL_DECIMAL(scale)));
            else if (IS_MONEY(scale))
                Move_Value(temp, scale);
            else
                fail (Error_Invalid(scale));
        }
        else
            Init_Money(temp, int_to_deci(0));

        Init_Money(D_OUT, Round_Deci(
            VAL_MONEY_AMOUNT(val),
            flags,
            VAL_MONEY_AMOUNT(temp)
        ));

        if (REF(to)) {
            if (IS_DECIMAL(scale) or IS_PERCENT(scale)) {
                REBDEC dec = deci_to_decimal(VAL_MONEY_AMOUNT(D_OUT));
                RESET_VAL_HEADER(D_OUT, VAL_TYPE(scale));
                VAL_DECIMAL(D_OUT) = dec;
                return R_OUT;
            }
            if (IS_INTEGER(scale)) {
                REBI64 i64 = deci_to_int(VAL_MONEY_AMOUNT(D_OUT));
                RESET_VAL_HEADER(D_OUT, REB_INTEGER);
                VAL_INT64(D_OUT) = i64;
                return R_OUT;
            }
        }
        break; }

    case SYM_EVEN_Q:
    case SYM_ODD_Q: {
        REBINT result = 1 & cast(REBINT, deci_to_int(VAL_MONEY_AMOUNT(val)));
        if (verb == SYM_EVEN_Q)
            result = not result;
        return result ? R_TRUE : R_FALSE; }

    default:
        fail (Error_Illegal_Action(REB_MONEY, verb));
    }

    RESET_VAL_HEADER(D_OUT, REB_MONEY);
    return R_OUT;
}

