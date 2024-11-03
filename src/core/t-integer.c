//
//  File: %t-integer.c
//  Summary: "integer datatype"
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

#include "sys-int-funcs.h"

#include "cells/cell-money.h"

//
//  CT_Integer: C
//
REBINT CT_Integer(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);  // no lax form of comparison

    if (VAL_INT64(a) == VAL_INT64(b))
        return 0;
    return (VAL_INT64(a) > VAL_INT64(b)) ? 1 : -1;
}


//
//  Makehook_Integer: C
//
// 1. This is a kind of crazy historical idea where this works:
//
//        rebol2>> make integer! <11.2e-1>
//        == 1
//
//    That seems like something you generally aren't interested in doing.
//    Here we constrain it at least to MAKE INTEGER! and not TO INTEGER! so
//    the field is a bit wider open, but I feel like if you want this you
//    should have to ask for a decimal! on purpose and then ROUND it.
//
// 2. While historical Rebol TO INTEGER! of BLOB! would interpret the
//    bytes as a big-endian form of their internal representations, wanting to
//    futureproof for BigNum integers has changed Ren-C's point of view...
//    delegating that highly parameterized conversion to operations currently
//    called ENBIN and DEBIN.
//
//      https://forum.rebol.info/t/1270
//
//    This is a stopgap while ENBIN and DEBIN are hammered out which preserves
//    the old behavior in the MAKE INTEGER! case.
//
// 3. Historical Rebol (to integer! 1:00) would give you 3600 despite it
//    being scarcely clear why that's a logical TO moreso than 1, or 100, or
//    anything else.  We move this oddity to MAKE.
//
Bounce Makehook_Integer(Level* level_, Kind kind, Element* arg) {
    assert(kind == REB_INTEGER);

    if (Any_Utf8(arg)) {  // !!! odd historical behavior [1]
        Option(Error*) error = Trap_Transcode_One(OUT, REB_0, arg);
        if (not error) {
            if (Is_Integer(OUT))
                return OUT;
            if (Is_Decimal(OUT))
                return rebValue(Canon(ROUND), stable_OUT);
            return RAISE(Error_User("Trap_Transcode_One() gave unwanted type"));
        }

        return FAIL(Error_Bad_Make(REB_INTEGER, arg));
    }

    if (Is_Time(arg))  // !!! (make integer! 1:00) -> 3600 :-( [3]
        return Init_Integer(OUT, SECS_FROM_NANO(VAL_NANO(arg)));

    if (Is_Decimal(arg) or Is_Percent(arg)) {  // !!! prefer ROUND
        if (VAL_DECIMAL(arg) < MIN_D64 or VAL_DECIMAL(arg) >= MAX_D64)
            return FAIL(Error_Overflow_Raw());

        return Init_Integer(OUT, cast(REBI64, VAL_DECIMAL(arg)));;
    }

    if (Is_Money(arg))  // !!! Better idea than MAKE for this?
        return Init_Integer(OUT, deci_to_int(VAL_MONEY_AMOUNT(arg)));

    return FAIL(Error_Bad_Make(kind, arg));
}


// Like converting a binary, except uses a string of ASCII characters.  Does
// not allow for signed interpretations, e.g. #FFFF => 65535, not -1.
// Unsigned makes more sense as these would be hexes likely typed in by users,
// who rarely do 2s-complement math in their head.
//
void Hex_String_To_Integer(Value* out, const Value* value)  // !!! UNUSED
{
    Size utf8_size;
    Utf8(const*) bp = Cell_Utf8_Size_At(&utf8_size, value);

    if (utf8_size > MAX_HEX_LEN) {
        // Lacks BLOB!'s accommodation of leading 00s or FFs
        fail (Error_Out_Of_Range_Raw(value));
    }

    if (not Try_Scan_Hex_Integer(out, bp, utf8_size, utf8_size))
        fail (Error_Bad_Make(REB_INTEGER, value));

    // !!! Unlike binary, always assumes unsigned (should it?).  Yet still
    // might run afoul of 64-bit range limit.
    //
    if (VAL_INT64(out) < 0)
        fail (Error_Out_Of_Range_Raw(value));
}


//
//  MF_Integer: C
//
void MF_Integer(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form);

    Byte buf[60];
    REBINT len = Emit_Integer(buf, VAL_INT64(v));
    Append_Ascii_Len(mo->string, s_cast(buf), len);
}


//
//  DECLARE_GENERICS: C
//
DECLARE_GENERICS(Integer)
{
    Option(SymId) id = Symbol_Id(verb);

    Element* val = cast(Element*, (id == SYM_TO) ? ARG_N(2) : ARG_N(1));
    REBI64 num = VAL_INT64(val);

    REBI64 arg;

    // !!! This used to rely on IS_BINARY_ACT, which is no longer available
    // in the symbol based dispatch.  Consider doing another way.
    //
    if (
        id == SYM_ADD
        or id == SYM_SUBTRACT
        or id == SYM_MULTIPLY
        or id == SYM_DIVIDE
        or id == SYM_POWER
        or id == SYM_BITWISE_AND
        or id == SYM_BITWISE_OR
        or id == SYM_BITWISE_XOR
        or id == SYM_BITWISE_AND_NOT
        or id == SYM_REMAINDER
    ){
        Value* val2 = ARG_N(2);

        if (Is_Integer(val2))
            arg = VAL_INT64(val2);
        else if (IS_CHAR(val2))
            arg = Cell_Codepoint(val2);
        else {
            // Decimal or other numeric second argument:
            REBLEN n = 0; // use to flag special case
            switch (id) {
            // Anything added to an integer is same as adding the integer:
            case SYM_ADD:
            case SYM_MULTIPLY: {
                // Swap parameter order:
                Move_Cell(stable_OUT, val2);  // Use as temp workspace
                Move_Cell(val2, val);
                Move_Cell(val, cast(Element*, OUT));
                return Run_Generic_Dispatch(cast(Element*, val), level_, verb); }

            // Only type valid to subtract from, divide into, is decimal/money:
            case SYM_SUBTRACT:
                n = 1;
                /* fall through */
            case SYM_DIVIDE:
            case SYM_REMAINDER:
            case SYM_POWER:
                if (Is_Decimal(val2) || Is_Percent(val2)) {
                    Init_Decimal(val, cast(REBDEC, num));  // convert
                    return T_Decimal(level_, verb);
                }
                if (Is_Money(val2)) {
                    Init_Money(val, int_to_deci(VAL_INT64(val)));
                    return T_Money(level_, verb);
                }
                if (n > 0) {
                    if (Is_Time(val2)) {
                        Init_Time_Nanoseconds(val, SEC_TIME(VAL_INT64(val)));
                        return T_Time(level_, verb);
                    }
                    if (Is_Date(val2))
                        return T_Date(level_, verb);
                }

            default:
                break;
            }
            return FAIL(Error_Math_Args(REB_INTEGER, verb));
        }
    }
    else
        arg = 0xDECAFBAD; // wasteful, but avoid maybe unassigned warning

    switch (id) {
      case SYM_COPY:
        Copy_Cell(OUT, val);
        return OUT;

    //=//// TO CONVERSIONS ////////////////////////////////////////////////=//

      case SYM_TO: {
        INCLUDE_PARAMS_OF_TO;
        UNUSED(ARG(element));  // val
        Heart to = VAL_TYPE_HEART(ARG(type));
        assert(REB_INTEGER != to);  // TO calls COPY in this case

        if (Any_Utf8_Kind(to)) {
            DECLARE_MOLDER (mo);
            SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            Push_Mold(mo);
            Mold_Element(mo, val);
            const String* s = Pop_Molded_String(mo);
            if (not Any_String_Kind(to))
                Freeze_Flex(s);
            return Init_Any_String(OUT, to, s);
        }

        if (Any_List_Kind(to))
            return rebValue(Canon(ENVELOP), ARG(type), val);

        if (to == REB_DECIMAL or to == REB_PERCENT) {
            REBDEC d = cast(REBDEC, VAL_INT64(val));
            if (to == REB_PERCENT)
                d = d / 100;
            return Init_Decimal_Or_Percent(OUT, to, d);
        }

        if (to == REB_MONEY) {
            deci d = int_to_deci(cast(REBDEC, VAL_INT64(val)));
            return Init_Money(OUT, d);
        }

        return FAIL(Error_Bad_Cast_Raw(val, ARG(type))); }

      case SYM_ADD: {
        REBI64 anum;
        if (REB_I64_ADD_OF(num, arg, &anum))
            return RAISE(Error_Overflow_Raw());
        return Init_Integer(OUT, anum); }

      case SYM_SUBTRACT: {
        REBI64 anum;
        if (REB_I64_SUB_OF(num, arg, &anum))
            return RAISE(Error_Overflow_Raw());
        return Init_Integer(OUT, anum); }

      case SYM_MULTIPLY: {
        REBI64 p;
        if (REB_I64_MUL_OF(num, arg, &p))
            return RAISE(Error_Overflow_Raw());
        return Init_Integer(OUT, p); }

      case SYM_DIVIDE:
        if (arg == 0)
            return RAISE(Error_Zero_Divide_Raw());
        if (num == INT64_MIN && arg == -1)
            return RAISE(Error_Overflow_Raw());
        if (num % arg == 0)
            return Init_Integer(OUT, num / arg);
        // Fall thru
      case SYM_POWER:
        Init_Decimal(ARG_N(1), cast(REBDEC, num));
        Init_Decimal(ARG_N(2), cast(REBDEC, arg));
        return T_Decimal(level_, verb);

      case SYM_REMAINDER:
        if (arg == 0)
            return RAISE(Error_Zero_Divide_Raw());
        return Init_Integer(OUT, (arg != -1) ? (num % arg) : 0);

      case SYM_BITWISE_AND:
        return Init_Integer(OUT, num & arg);

      case SYM_BITWISE_OR:
        return Init_Integer(OUT, num | arg);

      case SYM_BITWISE_XOR:
        return Init_Integer(OUT, num ^ arg);

      case SYM_BITWISE_AND_NOT:
        return Init_Integer(OUT, num & ~arg);

      case SYM_NEGATE:
        if (num == INT64_MIN)
            return RAISE(Error_Overflow_Raw());
        return Init_Integer(OUT, -num);

      case SYM_BITWISE_NOT:
        return Init_Integer(OUT, ~num);

      case SYM_ABSOLUTE:
        if (num == INT64_MIN)
            return RAISE(Error_Overflow_Raw());
        return Init_Integer(OUT, num < 0 ? -num : num);

      case SYM_EVEN_Q:
        num = ~num;
        // falls through
      case SYM_ODD_Q:
        if (num & 1)
            return Init_Logic(OUT, true);
        return Init_Logic(OUT, false);

      case SYM_ROUND: {
        INCLUDE_PARAMS_OF_ROUND;
        USED(ARG(value));  // extracted as d1, others are passed via level_
        USED(ARG(even)); USED(ARG(down)); USED(ARG(half_down));
        USED(ARG(floor)); USED(ARG(ceiling)); USED(ARG(half_ceiling));

        if (not REF(to))
            return Init_Integer(OUT, Round_Int(num, level_, 0L));

        Value* to = ARG(to);
        if (Is_Nulled(to))
            Init_Integer(to, 1);

        if (Is_Money(to))
            return Init_Money(
                OUT,
                Round_Deci(int_to_deci(num), level_)
            );

        if (Is_Decimal(to) || Is_Percent(to)) {
            REBDEC dec = Round_Dec(
                cast(REBDEC, num), level_, VAL_DECIMAL(to)
            );
            Reset_Cell_Header_Untracked(
                TRACK(OUT),
                FLAG_HEART_BYTE(VAL_TYPE(to)) | CELL_MASK_NO_NODES
            );
            VAL_DECIMAL(OUT) = dec;
            return OUT;
        }

        if (Is_Time(ARG(to)))
            return FAIL(PARAM(to));

        return Init_Integer(OUT, Round_Int(num, level_, VAL_INT64(to))); }

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PARAM(value));

        if (REF(only))
            return FAIL(Error_Bad_Refines_Raw());

        if (REF(seed)) {
            Set_Random(num);
            return NOTHING;
        }
        if (num == 0)
            return FAIL(ARG(value));
        return Init_Integer(OUT, Random_Range(num, REF(secure))); }

      default:
        break;
    }

    return UNHANDLED;
}
