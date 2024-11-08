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

#include "cells/cell-money.h"

//
//  Try_Scan_Money_To_Stack: C
//
// Scan and convert money.  Return zero if error.
//
Option(const Byte*) Try_Scan_Money_To_Stack(const Byte* cp, REBLEN len) {
    if (*cp == '$') {
        ++cp;
        --len;
    }
    if (len == 0)
        return nullptr;

    const Byte* ep;
    deci d = string_to_deci(cp, &ep);
    if (ep != cp + len)
        return nullptr;

    Init_Money(PUSH(), d);
    return ep;
}


//
//  CT_Money: C
//
REBINT CT_Money(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);

    bool e = deci_is_equal(VAL_MONEY_AMOUNT(a), VAL_MONEY_AMOUNT(b));
    if (e)
        return 0;

    bool g = deci_is_lesser_or_equal(
        VAL_MONEY_AMOUNT(b), VAL_MONEY_AMOUNT(a)
    );
    return g ? 1 : -1;
}


//
//  Makehook_Money: C
//
Bounce Makehook_Money(Level* level_, Heart heart, Element* arg) {
    assert(heart == REB_MONEY);
    UNUSED(heart);

    switch (VAL_TYPE(arg)) {
      case REB_INTEGER:
        return Init_Money(OUT, int_to_deci(VAL_INT64(arg)));

      case REB_DECIMAL:
      case REB_PERCENT:
        return Init_Money(OUT, decimal_to_deci(VAL_DECIMAL(arg)));

      case REB_MONEY:
        return Copy_Cell(OUT, arg);

      case REB_TEXT: {
        Option(Error*) error = Trap_Transcode_One(OUT, REB_0, arg);
        if (error)
            return RAISE(unwrap error);
        if (Is_Money(OUT))
            return OUT;
        if (Is_Decimal(OUT) or Is_Integer(OUT))
            return Init_Money(OUT, decimal_to_deci(Dec64(stable_OUT)));
        break; }

      case REB_BLOB:
        Bin_To_Money_May_Fail(OUT, arg);
        return OUT;

      default:
        break;
    }

    return RAISE(Error_Bad_Make(REB_MONEY, arg));
}


//
//  MF_Money: C
//
void MF_Money(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form);

    if (mo->opts & MOLD_FLAG_LIMIT) {
        // !!! In theory, emits should pay attention to the mold options,
        // at least the limit.
    }

    Byte buf[60];
    REBINT len = deci_to_string(buf, VAL_MONEY_AMOUNT(v), '$', '.');
    Append_Ascii_Len(mo->string, s_cast(buf), len);
}


//
//  Bin_To_Money_May_Fail: C
//
// Will successfully convert or fail (longjmp) with an error.
//
void Bin_To_Money_May_Fail(Sink(Value) result, const Value* val)
{
    if (not Is_Blob(val))
        fail (val);

    Size size;
    const Byte* at = Cell_Blob_Size_At(&size, val);
    if (size > 12)
        size = 12;

    Byte buf[MAX_HEX_LEN+4] = {0}; // binary to convert
    memcpy(buf, at, size);
    memcpy(buf + 12 - size, buf, size); // shift to right side
    memset(buf, 0, 12 - size);
    Init_Money(result, binary_to_deci(buf));
}


static Value* Math_Arg_For_Money(
    Sink(Value) store,
    Value* arg,
    const Symbol* verb
){
    if (Is_Money(arg))
        return arg;

    if (Is_Integer(arg)) {
        Init_Money(store, int_to_deci(VAL_INT64(arg)));
        return store;
    }

    if (Is_Decimal(arg) or Is_Percent(arg)) {
        Init_Money(store, decimal_to_deci(VAL_DECIMAL(arg)));
        return store;
    }

    fail (Error_Math_Args(REB_MONEY, verb));
}


//
//  DECLARE_GENERICS: C
//
DECLARE_GENERICS(Money)
{
    Option(SymId) id = Symbol_Id(verb);

    Element* v = cast(Element*,
        (id == SYM_TO or id == SYM_AS) ? ARG_N(2) : ARG_N(1)
    );

    switch (id) {

    //=//// TO CONVERSIONS ////////////////////////////////////////////////=//

      case SYM_TO: {
        INCLUDE_PARAMS_OF_TO;
        UNUSED(ARG(element));  // v
        Heart to = VAL_TYPE_HEART(ARG(type));

        deci d = VAL_MONEY_AMOUNT(v);

        if (to == REB_DECIMAL or to == REB_PERCENT)
            return Init_Decimal_Or_Percent(OUT, to, deci_to_decimal(d));

        if (to == REB_INTEGER) {  // !!! how to check for digits after dot?
            REBI64 i = deci_to_int(d);
            deci reverse = int_to_deci(i);
            if (not deci_is_equal(d, reverse))
                return RAISE(
                    "Can't TO INTEGER! a MONEY! w/digits after decimal point"
                );
            return Init_Integer(OUT, i);
        }

        if (Any_Utf8_Kind(to)) {
            if (d.e != 0 or d.m1 != 0 or d.m2 != 0)
                Init_Decimal(v, deci_to_decimal(d));
            else
                Init_Integer(v, deci_to_int(d));

            DECLARE_MOLDER (mo);
            SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            Push_Mold(mo);
            Mold_Element(mo, v);
            const String* s = Pop_Molded_String(mo);
            if (not Any_String_Kind(to))
                Freeze_Flex(s);
            return Init_Any_String(OUT, to, s);;
        }

        if (to == REB_MONEY)
            return COPY(v);

        return UNHANDLED; }

      case SYM_ADD: {
        Value* arg = Math_Arg_For_Money(SPARE, ARG_N(2), verb);
        return Init_Money(
            OUT,
            deci_add(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_SUBTRACT: {
        Value* arg = Math_Arg_For_Money(SPARE, ARG_N(2), verb);
        return Init_Money(
            OUT,
            deci_subtract(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_MULTIPLY: {
        Value* arg = Math_Arg_For_Money(SPARE, ARG_N(2), verb);
        return Init_Money(
            OUT,
            deci_multiply(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_DIVIDE: {
        Value* arg = Math_Arg_For_Money(SPARE, ARG_N(2), verb);
        return Init_Money(
            OUT,
            deci_divide(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_REMAINDER: {
        Value* arg = Math_Arg_For_Money(SPARE, ARG_N(2), verb);
        return Init_Money(
            OUT,
            deci_mod(VAL_MONEY_AMOUNT(v), VAL_MONEY_AMOUNT(arg))
        ); }

      case SYM_NEGATE: // sign bit is the 32nd bit, highest one used
        PAYLOAD(Any, v).second.u ^= (cast(uintptr_t, 1) << 31);
        return COPY(v);

      case SYM_ABSOLUTE:
        PAYLOAD(Any, v).second.u &= ~(cast(uintptr_t, 1) << 31);
        return COPY(v);

      case SYM_ROUND: {
        INCLUDE_PARAMS_OF_ROUND;
        USED(ARG(value));  // aliased as v, others are passed via level_
        USED(ARG(even)); USED(ARG(down)); USED(ARG(half_down));
        USED(ARG(floor)); USED(ARG(ceiling)); USED(ARG(half_ceiling));

        Value* to = ARG(to);
        if (Is_Nulled(to))
            Init_Money(to, decimal_to_deci(1.0L));

        Init_Money(
            OUT,
            Round_Deci(VAL_MONEY_AMOUNT(v), level_)
        );

        if (Is_Decimal(to) or Is_Percent(to)) {
            REBDEC dec = deci_to_decimal(VAL_MONEY_AMOUNT(OUT));
            Reset_Cell_Header_Untracked(
                TRACK(OUT),
                FLAG_HEART_BYTE(VAL_TYPE(to)) | CELL_MASK_NO_NODES
            );
            VAL_DECIMAL(OUT) = dec;
            return OUT;
        }
        if (Is_Integer(to)) {
            REBI64 i64 = deci_to_int(VAL_MONEY_AMOUNT(OUT));
            return Init_Integer(OUT, i64);
        }
        HEART_BYTE(OUT) = REB_MONEY;
        return OUT; }

      case SYM_EVEN_Q:
      case SYM_ODD_Q: {
        REBINT result = 1 & cast(REBINT, deci_to_int(VAL_MONEY_AMOUNT(v)));
        if (Symbol_Id(verb) == SYM_EVEN_Q)
            result = not result;
        return Init_Logic(OUT, result != 0); }

      case SYM_COPY:
        return COPY(v);

      default:
        break;
    }

    return UNHANDLED;
}
