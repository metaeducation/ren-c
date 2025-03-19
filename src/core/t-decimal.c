//
//  File: %t-decimal.c
//  Summary: "decimal datatype"
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
#include <math.h>
#include <float.h>

#include "cells/cell-money.h"

#define COEF 0.0625 // Coefficient used for float comparision
#define EQ_RANGE 4

#ifdef NO_GCVT
static char *gcvt(double value, int digits, char *buffer)
{
    sprintf(buffer, "%.*g", digits, value);
    return buffer;
}
#endif

/*
    Purpose: {defines the almost_equal comparison function}
    Properties: {
        since floating point numbers are ordered and there is only
        a finite quantity of floating point numbers, it is possible
        to assign an ordinal (integer) number to any floating point number so,
        that the ordinal numbers of neighbors differ by one

        the function compares floating point numbers based on
        the difference of their ordinal numbers in the ordering
        of floating point numbers

        difference of 0 means exact equality, difference of 1 means, that
        the numbers are neighbors.
    }
    Advantages: {
        the function detects approximate equality.

        the function is more strict in the zero neighborhood than
        absolute-error-based approaches

        as opposed to relative-error-based approaches the error can be
        precisely specified, max_diff = 0 meaning exact match, max_diff = 1
        meaning that neighbors are deemed equal, max_diff = 10 meaning, that
        the numbers are deemed equal if at most 9
        distinct floating point numbers can be found between them

        the max_diff value may be one of the system options specified in
        the system.options object allowing users to exactly define the
        strictness of equality checks
    }
    Differences: {
        The approximate comparison currently used in R3 corresponds to the
        almost_equal function using max_diff = 10 (according to my tests).

        The main differences between the currently used comparison and the
        one based on the ordinal number comparison are:
        -   the max_diff parameter can be adjusted, allowing
            the user to precisely specify the strictness of the comparison
        -   the difference rule holds for zero too, which means, that
            zero is deemed equal with totally max_diff distinct (tiny) numbers
    }
    Notes: {
        the max_diff parameter does not need to be a REBI64 number,
        a smaller range like REBLEN may suffice
    }
*/

bool almost_equal(REBDEC a, REBDEC b, REBI64 max_diff) {
    union {REBDEC d; REBI64 i;} ua, ub;
    REBI64 int_diff;

    ua.d = a;
    ub.d = b;

    /* Make ua.i a twos-complement ordinal number */
    if (ua.i < 0) ua.i = INT64_MIN - ua.i;

    /* Make ub.i a twos-complement ordinal number */
    if (ub.i < 0) ub.i = INT64_MIN - ub.i;

    int_diff = ua.i - ub.i;
    if (int_diff < 0) int_diff = -int_diff;

    return int_diff <= max_diff;
}


// !!! The current thinking on the distinction between MAKE and TO is that
// TO should not do any evaluations (including not looking at what words are
// bound to, only their spellings).  Also, TO should be more based on the
// visual intuition vs. internal representational knowledge...this would
// suggest things like `to integer! #"1"` being the number 1, and not a
// codepoint.  Hence historical conversions have been split into the TO
// or MAKE as a rough idea of how these rules might be followed.
//
// 1. MAKE DECIMAL! from a PATH! is a strange idea that allows evaluation of
//    arbitrary code.  (TO DECIMAL! of PATH! previously existed as a version
//    that didn't evaluate groups, but still ran DIVIDE and could get things
//    like division by zero, so got rid of that).  Weird but trying this.
//
// 2. Rebol2 and Red do this for some reason (your guess as good as mine):
//
//        rebol2>> make decimal! [10 0]
//        == 10.0
//
//        rebol2>> make decimal! [10 2]
//        == 1000.0
//
IMPLEMENT_GENERIC(MAKE, Is_Decimal)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Cell_Datatype_Type(ARG(type)) == TYPE_DECIMAL);
    UNUSED(ARG(type));

    Element* arg = Element_ARG(def);

    switch (Type_Of(arg)) {
      case TYPE_ISSUE: {
        REBDEC d = cast(REBDEC, Cell_Codepoint(arg));
        return Init_Decimal(OUT, d); }

      case TYPE_TIME: {
        REBDEC d = VAL_NANO(arg) * NANO;
        return Init_Decimal(OUT, d); }

      case TYPE_PATH: {  // fractions as 1/2 are experimental use for PATH! [1]
        if (Cell_Sequence_Len(arg) != 2)
            return FAIL("Fraction experiment requires PATH! of length 2");

        DECLARE_ELEMENT (numerator);
        DECLARE_ELEMENT (denominator);
        Derelativize_Sequence_At(
            numerator,
            c_cast(Element*, arg),
            Cell_Sequence_Binding(arg),
            0
        );
        Derelativize_Sequence_At(
            denominator,
            c_cast(Element*, arg),
            Cell_Sequence_Binding(arg),
            1
        );
        Push_Lifeguard(numerator);  // might be GROUP!, so (1.2)/4
        Push_Lifeguard(denominator);

        Value* quotient = rebValue("divide", numerator, denominator);

        Drop_Lifeguard(denominator);
        Drop_Lifeguard(numerator);

        REBDEC d;
        if (Is_Integer(quotient))
            d = cast(REBDEC, VAL_INT64(quotient));
        else if (Is_Decimal(quotient))
            d = VAL_DECIMAL(quotient);
        else {
            rebRelease(quotient);
            return FAIL("Fraction PATH! didn't maket DECIMAL! or PERCENT!");
        }
        rebRelease(quotient);
        return Init_Decimal(OUT, d); }

      case TYPE_BLOCK: {  // !!! what the heck is this for? [2]
        REBLEN len;
        const Element* item = Cell_List_Len_At(&len, arg);

        if (len != 2)
            return RAISE(Error_Bad_Make(TYPE_DECIMAL, arg));

        REBDEC d;
        if (Is_Integer(item))
            d = cast(REBDEC, VAL_INT64(item));
        else if (Is_Decimal(item) || Is_Percent(item))
            d = VAL_DECIMAL(item);
        else
            return RAISE(Error_Bad_Value(item));

        ++item;

        REBDEC exp;
        if (Is_Integer(item))
            exp = cast(REBDEC, VAL_INT64(item));
        else if (Is_Decimal(item) || Is_Percent(item))
            exp = VAL_DECIMAL(item);
        else
            return RAISE(Error_Bad_Value(item));

        while (exp >= 1) {
            --exp;
            d *= 10.0;
            if (!FINITE(d))
                return RAISE(Error_Overflow_Raw());
        }

        while (exp <= -1) {
            ++exp;
            d /= 10.0;
        }
        return Init_Decimal(OUT, d); }

      default:
        break;
    }

    return RAISE(Error_Bad_Make(TYPE_DECIMAL, arg));
}


// 1. It isn't entirely clear why MAKE of PERCENT! should be allowed, the
//    historical cases are strange:
//
//        >> make percent! 10:00
//        == 36000%
//
//    It may be that MAKE PERCENT! of DECIMAL! would multiply by 100, and
//    MAKE DECIMAL! of PERCENT! would divide by 100.  Other than that the
//    scenarios are not clear.
//
IMPLEMENT_GENERIC(MAKE, Is_Percent)
{
    INCLUDE_PARAMS_OF_MAKE;

    UNUSED(ARG(type));
    UNUSED(ARG(def));

    return FAIL("MAKE of PERCENT! not supported at this time");  // [1]
}


//
//  Eq_Decimal: C
//
bool Eq_Decimal(REBDEC a, REBDEC b)
{
    return almost_equal(a, b, 10);
}


//
//  Eq_Decimal2: C
//
bool Eq_Decimal2(REBDEC a, REBDEC b)
{
    return almost_equal(a, b, 0);
}


//
//  CT_Decimal: C
//
REBINT CT_Decimal(const Cell* a, const Cell* b, bool strict)
{
    if (strict) {
        if (almost_equal(VAL_DECIMAL(a), VAL_DECIMAL(b), 0))
            return 0;
    }
    else {
        if (almost_equal(VAL_DECIMAL(a), VAL_DECIMAL(b), 10))
            return 0;
    }

    return (VAL_DECIMAL(a) > VAL_DECIMAL(b)) ? 1 : -1;
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Decimal)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    return LOGIC(CT_Decimal(ARG(value1), ARG(value2), REF(strict)) == 0);
}


IMPLEMENT_GENERIC(LESSER_Q, Is_Decimal)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    return LOGIC(CT_Decimal(ARG(value1), ARG(value2), true) == -1);
}


IMPLEMENT_GENERIC(ZEROIFY, Is_Decimal)
{
    INCLUDE_PARAMS_OF_ZEROIFY;
    UNUSED(ARG(example));  // always gives 0x0

    return Init_Decimal(OUT, 0.0);;
}


IMPLEMENT_GENERIC(MOLDIFY, Any_Float)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(element);
    Heart heart = Cell_Heart_Ensure_Noquote(v);
    assert(heart == TYPE_DECIMAL or heart == TYPE_PERCENT);

    Molder* mo = Cell_Handle_Pointer(Molder, ARG(molder));
    bool form = REF(form);

    UNUSED(form);

    Byte buf[60];
    REBINT len = Emit_Decimal(
        buf,
        VAL_DECIMAL(v),
        heart == TYPE_PERCENT ? DEC_MOLD_MINIMAL : 0,
        GET_MOLD_FLAG(mo, MOLD_FLAG_COMMA_PT) ? ',' : '.',
        mo->digits
    );
    Append_Ascii_Len(mo->string, s_cast(buf), len);

    if (heart == TYPE_PERCENT)
        Append_Ascii(mo->string, "%");

    return NOTHING;
}


IMPLEMENT_GENERIC(OLDGENERIC, Is_Decimal)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* val = cast(Element*, ARG_N(1));
    REBDEC d1 = VAL_DECIMAL(val);

    Value* arg;
    REBDEC  d2;
    Heart heart;

    // !!! This used to use IS_BINARY_ACT() which is no longer available with
    // symbol-based dispatch.  Consider doing this another way.
    //
    if (
        id == SYM_ADD
        || id == SYM_SUBTRACT
        || id == SYM_DIVIDE
        || id == SYM_REMAINDER
        || id == SYM_POWER
    ){
        arg = ARG_N(2);
        if (QUOTE_BYTE(arg) != NOQUOTE_1)
            return FAIL(Error_Math_Args(Type_Of(arg), verb));

        heart = Cell_Heart(arg);
        if ((
            heart == TYPE_PAIR
            or heart == TYPE_TUPLE
            or heart == TYPE_MONEY
            or heart == TYPE_TIME
        ) and (
            id == SYM_ADD
        )){
            Move_Cell(stable_OUT, ARG_N(2));
            Move_Cell(ARG_N(2), ARG_N(1));
            Move_Cell(ARG_N(1), stable_OUT);
            return Run_Generic_Dispatch(cast(Element*, ARG_N(1)), level_, verb);
        }

        // If the type of the second arg is something we can handle:
        if (heart == TYPE_DECIMAL
            || heart == TYPE_INTEGER
            || heart == TYPE_PERCENT
            || heart == TYPE_MONEY
            || heart == TYPE_ISSUE
        ){
            if (heart == TYPE_DECIMAL) {
                d2 = VAL_DECIMAL(arg);
            }
            else if (heart == TYPE_PERCENT) {
                d2 = VAL_DECIMAL(arg);
                if (id == SYM_DIVIDE)
                    heart = TYPE_DECIMAL;
                else if (not Is_Percent(val))
                    heart = Cell_Heart_Ensure_Noquote(val);
            }
            else if (heart == TYPE_MONEY) {
                Init_Money(val, decimal_to_deci(VAL_DECIMAL(val)));
                return GENERIC_CFUNC(OLDGENERIC, Is_Money)(level_);
            }
            else if (heart == TYPE_ISSUE) {
                d2 = cast(REBDEC, Cell_Codepoint(arg));
                heart = TYPE_DECIMAL;
            }
            else {
                d2 = cast(REBDEC, VAL_INT64(arg));
                heart = Cell_Heart(val);  // 10% * 2 => 20%
            }

            switch (id) {

            case SYM_ADD:
                d1 += d2;
                return Init_Decimal_Or_Percent(OUT, heart, d1);

            case SYM_SUBTRACT:
                d1 -= d2;
                return Init_Decimal_Or_Percent(OUT, heart, d1);

            case SYM_DIVIDE:
            case SYM_REMAINDER:
                if (d2 == 0.0)
                    return FAIL(Error_Zero_Divide_Raw());
                if (id == SYM_DIVIDE)
                    d1 /= d2;
                else
                    d1 = fmod(d1, d2);
                return Init_Decimal_Or_Percent(OUT, heart, d1);

            case SYM_POWER:
                if (d2 == 0) {
                    //
                    // This means `power 0 0` is 1.0, despite it not being
                    // defined.  It's a pretty general programming consensus:
                    //
                    // https://rosettacode.org/wiki/Zero_to_the_zero_power
                    //
                    d1 = 1.0;
                    return Init_Decimal_Or_Percent(OUT, heart, d1);
                }
                if (d1 == 0)
                    return Init_Decimal_Or_Percent(OUT, heart, d1);
                d1 = pow(d1, d2);
                return Init_Decimal_Or_Percent(OUT, heart, d1);

            default:
                return FAIL(Error_Math_Args(Type_Of(val), verb));
            }
        }
        return FAIL(Error_Math_Args(Type_Of(val), verb));
    }

    heart = Cell_Heart_Ensure_Noquote(val);

    // unary actions
    switch (id) {
      case SYM_NEGATE:
        d1 = -d1;
        return Init_Decimal_Or_Percent(OUT, heart, d1);

      case SYM_ABSOLUTE:
        if (d1 < 0) d1 = -d1;
        return Init_Decimal_Or_Percent(OUT, heart, d1);

      case SYM_EVEN_Q:
        d1 = fabs(fmod(d1, 2.0));
        if (d1 < 0.5 || d1 >= 1.5)
            return Init_Logic(OUT, true);
        return Init_Logic(OUT, false);

      case SYM_ODD_Q:
        d1 = fabs(fmod(d1, 2.0));
        if (d1 < 0.5 || d1 >= 1.5)
            return Init_Logic(OUT, false);
        return Init_Logic(OUT, true);

      case SYM_ROUND: {
        INCLUDE_PARAMS_OF_ROUND;
        USED(ARG(value));  // extracted as d1, others are passed via level_
        USED(ARG(even)); USED(ARG(down)); USED(ARG(half_down));
        USED(ARG(floor)); USED(ARG(ceiling)); USED(ARG(half_ceiling));

        if (not REF(to)) {
            if (heart == TYPE_PERCENT)
                Init_Decimal(ARG(to), 0.01L);  // round 5.5% -> 6%
            else
                Init_Integer(ARG(to), 1);
        }

        if (Is_Money(ARG(to)))
            return Init_Money(OUT, Round_Deci(decimal_to_deci(d1), level_));

        if (Is_Time(ARG(to)))
            return FAIL(PARAM(to));

        d1 = Round_Dec(d1, level_, Dec64(ARG(to)));
        if (Is_Percent(ARG(to))) {
            heart = TYPE_PERCENT;
            return Init_Decimal_Or_Percent(OUT, heart, d1);
        }

        if (Is_Integer(ARG(to)))
            return Init_Integer(OUT, cast(REBI64, d1));
        return Init_Decimal_Or_Percent(OUT, heart, d1); }

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PARAM(value));
        if (REF(only))
            return FAIL(Error_Bad_Refines_Raw());

        if (REF(seed)) {
            REBDEC d = VAL_DECIMAL(val);
            REBI64 i;
            assert(sizeof(d) == sizeof(i));
            memcpy(&i, &d, sizeof(d));
            Set_Random(i); // use IEEE bits
            return NOTHING;
        }
        d1 = Random_Dec(d1, REF(secure));
        return Init_Decimal_Or_Percent(OUT, heart, d1); }

      default:
        break;
    }

    return UNHANDLED;
}


// 1. Right now the intelligence that gets 1% to render that way instead
//    of 1.0% is in FORM.  We don't repeat that here, but just call the
//    form process and drop the trailing %.  Should be factored better.
//
//    !!! Note this is buggy right now (doesn't happen in Red):
//
//        >> form 1.1%
//        == "1.1000000000000001%"
//
IMPLEMENT_GENERIC(TO, Is_Decimal)
{
    INCLUDE_PARAMS_OF_TO;

    Element* val = Element_ARG(element);
    Heart to = Cell_Datatype_Heart(ARG(type));

    REBDEC d = VAL_DECIMAL(val);

    if (Any_Utf8_Type(to)) {
        DECLARE_MOLDER (mo);
        SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
        Push_Mold(mo);
        Mold_Element(mo, val);
        const String* s = Pop_Molded_String(mo);
        if (not Any_String_Type(to))
            Freeze_Flex(s);
        Init_Any_String(OUT, to, s);
        if (Is_Percent(val))  // leverage (buggy) rendering 1% vs 1.0% [1]
            rebElide("take:last", OUT);
        return OUT;
    }

    if (to == TYPE_DECIMAL or to == TYPE_PERCENT)
        return Init_Decimal_Or_Percent(OUT, to, d);

    if (to == TYPE_MONEY)
        return Init_Money(OUT, decimal_to_deci(d));

    if (to == TYPE_INTEGER) {
        REBDEC leftover = d - cast(REBDEC, cast(REBI64, d));
        if (leftover != 0.0)
            return FAIL(
                "Can't TO INTEGER! a DECIMAL! w/digits after decimal point"
            );
        return Init_Integer(OUT, cast(REBI64, d));
    }

    return UNHANDLED;
}


// 1. See DECLARE_NATIVE(multiply) for commutativity method of ordering types.
//
IMPLEMENT_GENERIC(MULTIPLY, Any_Float)
{
    INCLUDE_PARAMS_OF_MULTIPLY;

    Heart heart = Cell_Heart_Ensure_Noquote(ARG(value1));
    REBDEC d1 = VAL_DECIMAL(ARG(value1));

    Value* v2 = ARG(value2);
    REBDEC d2;
    if (Is_Integer(v2))
        d2 = cast(REBDEC, VAL_INT64(v2));
    else
        d2 = VAL_DECIMAL(v2);  // decimal/percent ensured by MULTIPLY [1]

    return Init_Decimal_Or_Percent(OUT, heart, d1 * d2);
}


IMPLEMENT_GENERIC(COMPLEMENT, Any_Float)
{
    INCLUDE_PARAMS_OF_COMPLEMENT;

    REBDEC d = VAL_DECIMAL(ARG(value));

    return Init_Integer(OUT, ~cast(REBINT, d));  // !!! What is this good for?
}
