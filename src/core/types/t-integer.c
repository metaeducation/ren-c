//
//  file: %t-integer.c
//  summary: "integer datatype"
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

#include "sys-int-funcs.h"

//
//  CT_Integer: C
//
REBINT CT_Integer(const Element* a, const Element* b, bool strict)
{
    UNUSED(strict);  // no lax form of comparison

    if (VAL_INT64(a) == VAL_INT64(b))
        return 0;
    return (VAL_INT64(a) > VAL_INT64(b)) ? 1 : -1;
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Integer)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not Bool_ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Integer(v1, v2, strict) == 0);
}


IMPLEMENT_GENERIC(LESSER_Q, Is_Integer)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Integer(v1, v2, true) == -1);
}


IMPLEMENT_GENERIC(ZEROIFY, Is_Integer)
{
    INCLUDE_PARAMS_OF_ZEROIFY;
    UNUSED(ARG(EXAMPLE));  // always gives 0

    return Init_Integer(OUT, 0);
}



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
// 2. See rebDelegate() for notes on how it is legal to use the OUT, SPARE, or
//    SCRATCH cells in the delegation valist.
//
// 3. While historical Rebol TO INTEGER! of BLOB! would interpret the
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
// 4. Historical Rebol (to integer! 1:00) would give you 3600 despite it
//    being scarcely clear why that's a logical TO moreso than 1, or 100, or
//    anything else.  We move this oddity to MAKE.
//
IMPLEMENT_GENERIC(MAKE, Is_Integer)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Cell_Datatype_Builtin_Heart(ARG(TYPE)) == TYPE_INTEGER);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    if (Any_Utf8(arg)) {  // !!! odd historical behavior [1]
        Sink(Element) out = OUT;

        require (Transcode_One(out, TYPE_0, arg));  // not bad make (?)

        if (Is_Integer(out))
            return OUT;
        if (Is_Decimal(out))
            return rebDelegate(CANON(ROUND), out);  // out is legal ATM [2]

        return fail ("Transcode_One() gave unwanted type");
    }

    dont(Is_Blob(arg));  // [3]

    if (Is_Time(arg))  // !!! (make integer! 1:00) -> 3600 :-( [4]
        return Init_Integer(OUT, SECS_FROM_NANO(VAL_NANO(arg)));

    if (Is_Decimal(arg) or Is_Percent(arg)) {  // !!! prefer ROUND
        if (VAL_DECIMAL(arg) < MIN_D64 or VAL_DECIMAL(arg) >= MAX_D64)
            panic (Error_Overflow_Raw());

        return Init_Integer(OUT, cast(REBI64, VAL_DECIMAL(arg)));;
    }

    panic (Error_Bad_Make(TYPE_INTEGER, arg));
}


// Like converting a binary, except uses a string of ASCII characters.  Does
// not allow for signed interpretations, e.g. #FFFF => 65535, not -1.
// Unsigned makes more sense as these would be hexes likely typed in by users,
// who rarely do 2s-complement math in their head.
//
void Hex_String_To_Integer(Value* out, const Element* value)  // !!! UNUSED
{
    Size utf8_size;
    Utf8(const*) bp = Cell_Utf8_Size_At(&utf8_size, value);

    if (utf8_size > MAX_HEX_LEN) {
        // Lacks BLOB!'s accommodation of leading 00s or FFs
        abrupt_panic (Error_Out_Of_Range_Raw(value));
    }

    if (not Try_Scan_Hex_Integer(out, bp, utf8_size, utf8_size))
        abrupt_panic (Error_Bad_Make(TYPE_INTEGER, value));

    // !!! Unlike binary, always assumes unsigned (should it?).  Yet still
    // might run afoul of 64-bit range limit.
    //
    if (VAL_INT64(out) < 0)
        abrupt_panic (Error_Out_Of_Range_Raw(value));
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Integer)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form);

    Byte buf[60];
    REBINT len = Emit_Integer(buf, VAL_INT64(v));
    Append_Ascii_Len(mo->strand, s_cast(buf), len);

    return TRIPWIRE;
}


IMPLEMENT_GENERIC(OLDGENERIC, Is_Integer)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* val = cast(Element*, ARG_N(1));
    REBI64 num = VAL_INT64(val);

    REBI64 arg;

    // !!! This used to rely on IS_BINARY_ACT, which is no longer available
    // in the symbol based dispatch.  Consider doing another way.
    //
    if (
        id == SYM_ADD
        or id == SYM_SUBTRACT
        or id == SYM_DIVIDE
        or id == SYM_POWER
        or id == SYM_BITWISE_AND
        or id == SYM_BITWISE_OR
        or id == SYM_BITWISE_XOR
        or id == SYM_BITWISE_AND_NOT
        or id == SYM_REMAINDER
    ){
        INCLUDE_PARAMS_OF_ADD;
        USED(ARG(VALUE1));
        Element* val2 = Element_ARG(VALUE2);

        if (Is_Integer(val2))
            arg = VAL_INT64(val2);
        else if (Is_Rune_And_Is_Char(val2))
            arg = Rune_Known_Single_Codepoint(val2);
        else {
            // Decimal or other numeric second argument:
            REBLEN n = 0; // use to flag special case
            switch (id) {
            // Anything added to an integer is same as adding the integer:
            case SYM_ADD: {
                // Swap parameter order:
                Element* out = Move_Cell(OUT, val2);
                Move_Cell(val2, val);
                Move_Cell(val, out);
                return Run_Generic_Dispatch(val, level_, verb); }

            // Only type valid to subtract from, divide into, is decimal/money:
            case SYM_SUBTRACT:
                n = 1;
                /* fall through */
            case SYM_DIVIDE:
            case SYM_REMAINDER:
            case SYM_POWER:
                if (Is_Decimal(val2) || Is_Percent(val2)) {
                    Init_Decimal(val, cast(REBDEC, num));  // convert
                    return GENERIC_CFUNC(OLDGENERIC, Is_Decimal)(level_);
                }
                if (n > 0) {
                    if (Is_Time(val2)) {
                        Init_Time_Nanoseconds(val, SEC_TIME(VAL_INT64(val)));
                        return GENERIC_CFUNC(OLDGENERIC, Is_Time)(level_);
                    }
                    if (Is_Date(val2))
                        return GENERIC_CFUNC(OLDGENERIC, Is_Date)(level_);
                }

            default:
                break;
            }
            panic (Error_Math_Args(TYPE_INTEGER, verb));
        }
    }
    else
        arg = 0xDECAFBAD; // wasteful, but avoid maybe unassigned warning

    switch (id) {
      case SYM_ADD: {
        REBI64 anum;
        if (Add_I64_Overflows(&anum, num, arg))
            return fail (Error_Overflow_Raw());
        return Init_Integer(OUT, anum); }

      case SYM_SUBTRACT: {
        REBI64 anum;
        if (Subtract_I64_Overflows(&anum, num, arg))
            return fail (Error_Overflow_Raw());
        return Init_Integer(OUT, anum); }

      case SYM_DIVIDE:
        if (arg == 0)
            return fail (Error_Zero_Divide_Raw());
        if (num == INT64_MIN && arg == -1)
            return fail (Error_Overflow_Raw());
        if (num % arg == 0)
            return Init_Integer(OUT, num / arg);
        // Fall thru
      case SYM_POWER:
        Init_Decimal(ARG_N(1), cast(REBDEC, num));
        Init_Decimal(ARG_N(2), cast(REBDEC, arg));
        return GENERIC_CFUNC(OLDGENERIC, Is_Decimal)(level_);

      case SYM_REMAINDER:
        if (arg == 0)
            return fail (Error_Zero_Divide_Raw());
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
            return fail (Error_Overflow_Raw());
        return Init_Integer(OUT, -num);

      case SYM_BITWISE_NOT:
        return Init_Integer(OUT, ~num);

      case SYM_ABSOLUTE:
        if (num == INT64_MIN)
            return fail (Error_Overflow_Raw());
        return Init_Integer(OUT, num < 0 ? -num : num);

      case SYM_EVEN_Q:
        num = ~num;
        // falls through
      case SYM_ODD_Q:
        if (num & 1)
            return LOGIC(true);
        return LOGIC(false);

      default:
        break;
    }

    panic (UNHANDLED);
}


IMPLEMENT_GENERIC(TO, Is_Integer)
{
    INCLUDE_PARAMS_OF_TO;

    Element* val = Element_ARG(ELEMENT);
    Heart to = Cell_Datatype_Builtin_Heart(ARG(TYPE));

    if (Any_Utf8_Type(to) and to != TYPE_WORD) {
        DECLARE_MOLDER (mo);
        SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
        Push_Mold(mo);
        Mold_Element(mo, val);

        const Strand* s;
        if (Any_String_Type(to))
            s = Pop_Molded_Strand(mo);
        else {
            if (Try_Init_Small_Utf8(
                OUT,
                to,
                cast(Utf8(const*), Binary_At(mo->strand, mo->base.size)),
                Strand_Len(mo->strand) - mo->base.index,
                Strand_Size(mo->strand) - mo->base.size
            )){
                Drop_Mold(mo);
                return OUT;
            }
            s = Pop_Molded_Strand(mo);
            Freeze_Flex(s);
        }
        return Init_Any_String(OUT, to, s);
    }

    if (Any_List_Type(to))
        return rebValue(CANON(ENVELOP), ARG(TYPE), val);

    if (to == TYPE_DECIMAL or to == TYPE_PERCENT) {
        REBDEC d = cast(REBDEC, VAL_INT64(val));
        return Init_Decimal_Or_Percent(OUT, to, d);
    }


    if (to == TYPE_INTEGER)
        return COPY(val);

    panic (UNHANDLED);
}


IMPLEMENT_GENERIC(RANDOMIZE, Is_Integer)
{
    INCLUDE_PARAMS_OF_RANDOMIZE;

    REBI64 num = VAL_INT64(Element_ARG(SEED));

    Set_Random(num);
    return TRIPWIRE;
}


IMPLEMENT_GENERIC(RANDOM, Is_Integer)
{
    INCLUDE_PARAMS_OF_RANDOM;

    REBI64 max = VAL_INT64(Element_ARG(MAX));

    if (max == 0)
        panic (PARAM(MAX));  // range is 1 to max, inclusive

    return Init_Integer(OUT, Random_Range(max, Bool_ARG(SECURE)));
}


IMPLEMENT_GENERIC(RANDOM_BETWEEN, Is_Integer)
{
    INCLUDE_PARAMS_OF_RANDOM_BETWEEN;

    REBI64 min = VAL_INT64(Element_ARG(MIN));
    REBI64 max = VAL_INT64(Element_ARG(MAX));

    if (max < min)
        panic (PARAM(MAX));  // 0 to 0 is okay, but disallow 1 to 0

    REBI64 rand = Random_Range(1 + max - min, Bool_ARG(SECURE));  // 1-based

    return Init_Integer(OUT, rand + min - 1);
}


IMPLEMENT_GENERIC(MULTIPLY, Is_Integer)
{
    INCLUDE_PARAMS_OF_MULTIPLY;

    if (not Is_Integer(ARG(VALUE2)))
        panic (PARAM(VALUE2));

    REBI64 num1 = VAL_INT64(ARG(VALUE1));
    REBI64 num2 = VAL_INT64(ARG(VALUE2));

    REBI64 result;
    if (Multipy_I64_Overflows(&result, num1, num2))
        return fail (Error_Overflow_Raw());
    return Init_Integer(OUT, result);
}


IMPLEMENT_GENERIC(ROUND, Is_Integer)
{
    INCLUDE_PARAMS_OF_ROUND;

    REBI64 num = VAL_INT64(Element_ARG(VALUE));

    USED(ARG(EVEN)); USED(ARG(DOWN)); USED(ARG(HALF_DOWN));
    USED(ARG(FLOOR)); USED(ARG(CEILING)); USED(ARG(HALF_CEILING));

    if (not Bool_ARG(TO))
        return Init_Integer(OUT, Round_Int(num, level_, 0L));

    Element* to = Is_Nulled(ARG(TO))
        ? Init_Integer(ARG(TO), 1)  // default to round to integer 1
        : Element_ARG(TO);

    if (Is_Decimal(to) || Is_Percent(to)) {
        REBDEC dec = Round_Dec(
            cast(REBDEC, num), level_, VAL_DECIMAL(to)
        );
        Heart to_heart = Heart_Of_Builtin_Fundamental(to);
        Init(Element) out = OUT;
        Reset_Cell_Header_Noquote(
            TRACK(out),
            FLAG_HEART(to_heart) | CELL_MASK_NO_MARKING
        );
        VAL_DECIMAL(out) = dec;
        return OUT;
    }

    if (Is_Time(ARG(TO)))
        panic (PARAM(TO));

    return Init_Integer(OUT, Round_Int(num, level_, VAL_INT64(to)));
}
