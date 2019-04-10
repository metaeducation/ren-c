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
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"

#include "datatypes/sys-money.h"

//
//  Scan_Money: C
//
// Scan and convert money.  Return zero if error.
//
const REBYTE *Scan_Money(
    RELVAL *out, // may live in data stack (do not call DS_PUSH(), GC, eval)
    const REBYTE *cp,
    REBCNT len
) {
    TRASH_CELL_IF_DEBUG(out);

    const REBYTE *end;

    if (*cp == '$') {
        ++cp;
        --len;
    }
    if (len == 0)
        return nullptr;

    Init_Money(out, string_to_deci(cp, &end));
    if (end != cp + len)
        return nullptr;

    return end;
}


//
//  CT_Money: C
//
REBINT CT_Money(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    bool e = deci_is_equal(VAL_MONEY_AMOUNT(a), VAL_MONEY_AMOUNT(b));

    if (mode < 0) {
        bool g = deci_is_lesser_or_equal(
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
REB_R MAKE_Money(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_MONEY);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    switch (VAL_TYPE(arg)) {
      case REB_INTEGER:
        return Init_Money(out, int_to_deci(VAL_INT64(arg)));

      case REB_DECIMAL:
      case REB_PERCENT:
        return Init_Money(out, decimal_to_deci(VAL_DECIMAL(arg)));

      case REB_MONEY:
        return Move_Value(out, arg);

      case REB_TEXT: {
        const REBYTE *bp = Analyze_String_For_Scan(NULL, arg, MAX_SCAN_MONEY);

        const REBYTE *end;
        Init_Money(out, string_to_deci(bp, &end));
        if (end == bp or *end != '\0')
            goto bad_make;
        return out; }

//      case REB_ISSUE:
      case REB_BINARY:
        Bin_To_Money_May_Fail(out, arg);
        return out;

      case REB_LOGIC:
        return Init_Money(out, int_to_deci(VAL_LOGIC(arg) ? 1 : 0));

      default:
        break;
    }

  bad_make:
    fail (Error_Bad_Make(REB_MONEY, arg));
}


//
//  TO_Money: C
//
REB_R TO_Money(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Money(out, kind, nullptr, arg);
}


//
//  MF_Money: C
//
void MF_Money(REB_MOLD *mo, const REBCEL *v, bool form)
{
    UNUSED(form);

    if (mo->opts & MOLD_FLAG_LIMIT) {
        // !!! In theory, emits should pay attention to the mold options,
        // at least the limit.
    }

    REBYTE buf[60];
    REBINT len = deci_to_string(buf, VAL_MONEY_AMOUNT(v), '$', '.');
    Append_Ascii_Len(mo->series, s_cast(buf), len);
}


//
//  Bin_To_Money_May_Fail: C
//
// Will successfully convert or fail (longjmp) with an error.
//
void Bin_To_Money_May_Fail(REBVAL *result, const REBVAL *val)
{
    if (not IS_BINARY(val))
        fail (val);

    REBCNT len = VAL_LEN_AT(val);
    if (len > 12)
        len = 12;

    REBYTE buf[MAX_HEX_LEN+4] = {0}; // binary to convert
    memcpy(buf, VAL_BIN_AT(val), len);
    memcpy(buf + 12 - len, buf, len); // shift to right side
    memset(buf, 0, 12 - len);
    Init_Money(result, binary_to_deci(buf));
}


static REBVAL *Math_Arg_For_Money(REBVAL *store, REBVAL *arg, const REBVAL *verb)
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
    REBVAL *v = D_ARG(1);

    switch (VAL_WORD_SYM(verb)) {
      case SYM_ADD: {
        REBVAL *arg = Math_Arg_For_Money(D_OUT, D_ARG(2), verb);
        return Init_Money(
            D_OUT,
            deci_add(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_SUBTRACT: {
        REBVAL *arg = Math_Arg_For_Money(D_OUT, D_ARG(2), verb);
        return Init_Money(
            D_OUT,
            deci_subtract(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_MULTIPLY: {
        REBVAL *arg = Math_Arg_For_Money(D_OUT, D_ARG(2), verb);
        return Init_Money(
            D_OUT,
            deci_multiply(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_DIVIDE: {
        REBVAL *arg = Math_Arg_For_Money(D_OUT, D_ARG(2), verb);
        return Init_Money(
            D_OUT,
            deci_divide(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_REMAINDER: {
        REBVAL *arg = Math_Arg_For_Money(D_OUT, D_ARG(2), verb);
        return Init_Money(
            D_OUT,
            deci_mod(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_NEGATE: // sign bit is the 32nd bit, highest one used
        PAYLOAD(Any, v).second.u ^= (cast(uintptr_t, 1) << 31);
        RETURN (v);

      case SYM_ABSOLUTE:
        PAYLOAD(Any, v).second.u &= ~(cast(uintptr_t, 1) << 31);
        RETURN (v);

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

        REBVAL *to = ARG(to);

        DECLARE_LOCAL (temp);
        if (REF(to)) {
            if (IS_INTEGER(to))
                Init_Money(temp, int_to_deci(VAL_INT64(to)));
            else if (IS_DECIMAL(to) or IS_PERCENT(to))
                Init_Money(temp, decimal_to_deci(VAL_DECIMAL(to)));
            else if (IS_MONEY(to))
                Move_Value(temp, to);
            else
                fail (PAR(to));
        }
        else
            Init_Money(temp, int_to_deci(0));

        Init_Money(
            D_OUT,
            Round_Deci(VAL_MONEY_AMOUNT(v), flags, VAL_MONEY_AMOUNT(temp))
        );

        if (REF(to)) {
            if (IS_DECIMAL(to) or IS_PERCENT(to)) {
                REBDEC dec = deci_to_decimal(VAL_MONEY_AMOUNT(D_OUT));
                RESET_CELL(D_OUT, VAL_TYPE(to), CELL_MASK_NONE);
                VAL_DECIMAL(D_OUT) = dec;
                return D_OUT;
            }
            if (IS_INTEGER(to)) {
                REBI64 i64 = deci_to_int(VAL_MONEY_AMOUNT(D_OUT));
                return Init_Integer(D_OUT, i64);
            }
        }
        RESET_VAL_HEADER(D_OUT, REB_MONEY, CELL_MASK_NONE);
        return D_OUT; }

      case SYM_EVEN_Q:
      case SYM_ODD_Q: {
        REBINT result = 1 & cast(REBINT, deci_to_int(VAL_MONEY_AMOUNT(v)));
        if (VAL_WORD_SYM(verb) == SYM_EVEN_Q)
            result = not result;
        return Init_Logic(D_OUT, result != 0); }

      case SYM_COPY:
        RETURN (v);

      default:
        break;
    }

    return R_UNHANDLED;
}

