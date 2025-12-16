//
//  file: %t-decimal.c
//  summary: "decimal datatype"
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
#include <math.h>
#include <float.h>

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

    assert(Datatype_Type(ARG(TYPE)) == TYPE_DECIMAL);

    Element* arg = Element_ARG(DEF);

    Option(Type) type = Type_Of(arg);
    if (Any_Utf8_Type(type)) {
        trap (
          Transcode_One(OUT, TYPE_DECIMAL, arg)
        );
        return OUT;
    }
    else switch (opt type) {
      case TYPE_RUNE: {
        trap (
          Codepoint c = Get_Rune_Single_Codepoint(arg)
        );
        return Init_Decimal(OUT, cast(REBDEC, c)); }

      case TYPE_TIME: {
        REBDEC d = VAL_NANO(arg) * NANO;
        return Init_Decimal(OUT, d); }

      case TYPE_PATH: {  // fractions as 1/2 are experimental use for PATH! [1]
        if (Sequence_Len(arg) != 2)
            panic ("Fraction experiment requires PATH! of length 2");

        DECLARE_ELEMENT (numerator);
        DECLARE_ELEMENT (denominator);
        Derelativize_Sequence_At(numerator, arg, 0, Sequence_Binding(arg));
        Derelativize_Sequence_At(denominator, arg, 1, Sequence_Binding(arg));
        Push_Lifeguard(numerator);  // might be GROUP!, so (1.2)/4
        Push_Lifeguard(denominator);

        Api(Stable*) quotient = rebStable("divide", numerator, denominator);

        Drop_Lifeguard(denominator);
        Drop_Lifeguard(numerator);

        REBDEC d;
        if (Is_Integer(quotient))
            d = cast(REBDEC, VAL_INT64(quotient));
        else if (Is_Decimal(quotient))
            d = VAL_DECIMAL(quotient);
        else {
            rebRelease(quotient);
            panic ("Fraction PATH! didn't maket DECIMAL! or PERCENT!");
        }
        rebRelease(quotient);
        return Init_Decimal(OUT, d); }

      case TYPE_BLOCK: {  // !!! what the heck is this for? [2]
        REBLEN len;
        const Element* item = List_Len_At(&len, arg);

        if (len != 2)
            return fail (Error_Bad_Make(TYPE_DECIMAL, arg));

        REBDEC d;
        if (Is_Integer(item))
            d = cast(REBDEC, VAL_INT64(item));
        else if (Is_Decimal(item) || Is_Percent(item))
            d = VAL_DECIMAL(item);
        else
            return fail (Error_Bad_Value(item));

        ++item;

        REBDEC exp;
        if (Is_Integer(item))
            exp = cast(REBDEC, VAL_INT64(item));
        else if (Is_Decimal(item) || Is_Percent(item))
            exp = VAL_DECIMAL(item);
        else
            return fail (Error_Bad_Value(item));

        while (exp >= 1) {
            --exp;
            d *= 10.0;
            if (!FINITE(d))
                return fail (Error_Overflow_Raw());
        }

        while (exp <= -1) {
            ++exp;
            d /= 10.0;
        }
        return Init_Decimal(OUT, d); }

      default:
        break;
    }

    return fail (Error_Bad_Make(TYPE_DECIMAL, arg));
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

    panic ("MAKE of PERCENT! not supported at this time");  // [1]
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
REBINT CT_Decimal(const Element* a, const Element* b, bool strict)
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
    bool strict = not ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Decimal(v1, v2, strict) == 0);
}


IMPLEMENT_GENERIC(LESSER_Q, Is_Decimal)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Decimal(v1, v2, true) == -1);
}


IMPLEMENT_GENERIC(ZEROIFY, Is_Decimal)
{
    INCLUDE_PARAMS_OF_ZEROIFY;
    UNUSED(ARG(EXAMPLE));  // always gives 0x0

    return Init_Decimal(OUT, 0.0);;
}


IMPLEMENT_GENERIC(MOLDIFY, Any_Float)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Heart heart = Heart_Of_Builtin_Fundamental(v);
    assert(heart == TYPE_DECIMAL or heart == TYPE_PERCENT);

    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = did ARG(FORM);

    UNUSED(form);

    Byte buf[60];
    REBINT len = Emit_Decimal(
        buf,
        VAL_DECIMAL(v),
        heart == TYPE_PERCENT ? DEC_MOLD_MINIMAL : 0,
        GET_MOLD_FLAG(mo, MOLD_FLAG_COMMA_PT) ? ',' : '.',
        mo->digits
    );
    require (
      Append_Ascii_Len(mo->strand, s_cast(buf), len)
    );

    if (heart == TYPE_PERCENT) {
        require (
          Append_Ascii(mo->strand, "%")
        );
    }

    return TRASH;
}


IMPLEMENT_GENERIC(OLDGENERIC, Is_Decimal)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* val = cast(Element*, ARG_N(1));
    REBDEC d1 = VAL_DECIMAL(val);

    Element* arg;
    REBDEC  d2;
    Heart heart;  // heart of ARG guaranteed to be integer, decimal, or percent
    // (this invariant hasn't been taken advantage of yet, but will be when
    // this is converted to sane new generic code)

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
        INCLUDE_PARAMS_OF_ADD;  // must have same layout as others
        USED(ARG(VALUE1));  // is val
        arg = Element_ARG(VALUE2);
        if (LIFT_BYTE(arg) != NOQUOTE_2)
            panic (Error_Not_Related_Raw(verb, Datatype_Of(arg)));

        heart = Heart_Of_Builtin_Fundamental(arg);
        if ((
            heart == TYPE_PAIR
            or heart == TYPE_TUPLE
            or heart == TYPE_TIME
        ) and (
            id == SYM_ADD
        )){
            Element* out = Move_Cell(OUT, arg);
            Move_Cell(arg, val);
            Move_Cell(val, out);
            return Run_Generic_Dispatch(val, level_, verb);
        }

        // If the type of the second arg is something we can handle:
        if (heart == TYPE_DECIMAL
            || heart == TYPE_INTEGER
            || heart == TYPE_PERCENT
            || heart == TYPE_RUNE
        ){
            if (heart == TYPE_DECIMAL) {
                d2 = VAL_DECIMAL(arg);
            }
            else if (heart == TYPE_PERCENT) {
                d2 = VAL_DECIMAL(arg);
                if (id == SYM_DIVIDE)
                    heart = TYPE_DECIMAL;
                else if (not Is_Percent(val))
                    heart = Heart_Of_Builtin_Fundamental(val);
            }
            else if (heart == TYPE_RUNE) {
                require (
                  Codepoint c = Get_Rune_Single_Codepoint(arg)
                );
                d2 = cast(REBDEC, c);
                heart = TYPE_DECIMAL;
            }
            else {
                d2 = cast(REBDEC, VAL_INT64(arg));
                heart = Heart_Of_Builtin_Fundamental(val);  // 10% * 2 => 20%
            }

            switch (opt id) {

            case SYM_ADD:
                d1 += d2;
                return Init_Decimal_Or_Percent(OUT, heart, d1);

            case SYM_SUBTRACT:
                d1 -= d2;
                return Init_Decimal_Or_Percent(OUT, heart, d1);

            case SYM_DIVIDE:
            case SYM_REMAINDER:
                if (d2 == 0.0)
                    panic (Error_Zero_Divide_Raw());
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
                panic (Error_Not_Related_Raw(verb, Datatype_Of(val)));
            }
        }
        panic (Error_Not_Related_Raw(verb, Datatype_Of(val)));
    }

    heart = Heart_Of_Builtin_Fundamental(val);

    panic (UNHANDLED);
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

    Element* val = Element_ARG(VALUE);
    Heart to = Datatype_Builtin_Heart(ARG(TYPE));

    REBDEC d = VAL_DECIMAL(val);

    if (Any_Utf8_Type(to)) {
        DECLARE_MOLDER (mo);
        SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
        Push_Mold(mo);
        Mold_Element(mo, val);

        if (Is_Percent(val)) { // leverage (buggy) rendering 1% vs 1.0% [1]
            Term_Strand_Len_Size(
                mo->strand,
                Strand_Len(mo->strand) - 1,
                Strand_Size(mo->strand) - 1
            );
        }

        if (Any_String_Type(to))
            return Init_Any_String(OUT, to, Pop_Molded_Strand(mo));

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
        const Strand* s = Pop_Molded_Strand(mo);
        Freeze_Flex(s);
        return Init_Any_String(OUT, to, s);
    }

    if (to == TYPE_DECIMAL or to == TYPE_PERCENT)
        return Init_Decimal_Or_Percent(OUT, to, d);


    if (to == TYPE_INTEGER) {
        REBDEC leftover = d - cast(REBDEC, cast(REBI64, d));
        if (leftover != 0.0)
            panic (
                "Can't TO INTEGER! a DECIMAL! w/digits after decimal point"
            );
        return Init_Integer(OUT, cast(REBI64, d));
    }

    panic (UNHANDLED);
}


IMPLEMENT_GENERIC(NEGATE, Any_Float)
{
    INCLUDE_PARAMS_OF_NEGATE;

    Element* val = Element_ARG(VALUE);
    REBDEC d = VAL_DECIMAL(val);
    Heart heart = Heart_Of_Builtin_Fundamental(val);

    d = -d;
    return Init_Decimal_Or_Percent(OUT, heart, d);
}


IMPLEMENT_GENERIC(ABSOLUTE, Any_Float)
{
    INCLUDE_PARAMS_OF_ABSOLUTE;

    Element* val = Element_ARG(VALUE);
    REBDEC d = VAL_DECIMAL(val);
    Heart heart = Heart_Of_Builtin_Fundamental(val);

    if (d < 0)
        d = -d;
    return Init_Decimal_Or_Percent(OUT, heart, d);
}


IMPLEMENT_GENERIC(RANDOMIZE, Any_Float)
{
    INCLUDE_PARAMS_OF_RANDOMIZE;

    const Element* val = Element_ARG(SEED);

    REBDEC d = VAL_DECIMAL(val);
    REBI64 i;
    assert(sizeof(d) == sizeof(i));
    memcpy(&i, &d, sizeof(d));  // use IEEE bits (is there a better way?)
    Set_Random(i);
    return TRASH;
}


IMPLEMENT_GENERIC(RANDOM, Any_Float)
{
    INCLUDE_PARAMS_OF_RANDOM;

    const Element* val = Element_ARG(MAX);
    Heart heart = Heart_Of_Builtin_Fundamental(val);
    assert(heart == TYPE_DECIMAL or heart == TYPE_PERCENT);

    REBDEC d = VAL_DECIMAL(val);
    REBDEC rand = Random_Dec(d, did ARG(SECURE));

    return Init_Decimal_Or_Percent(OUT, heart, rand);
}


// 1. See DECLARE_NATIVE(MULTIPLY) for commutativity method of ordering types.
//
IMPLEMENT_GENERIC(MULTIPLY, Any_Float)
{
    INCLUDE_PARAMS_OF_MULTIPLY;

    Heart heart = Heart_Of_Builtin_Fundamental(Element_ARG(VALUE1));
    REBDEC d1 = VAL_DECIMAL(Element_ARG(VALUE1));

    Stable* v2 = ARG(VALUE2);
    REBDEC d2;
    if (Is_Integer(v2))
        d2 = cast(REBDEC, VAL_INT64(v2));
    else
        d2 = VAL_DECIMAL(v2);  // decimal/percent ensured by MULTIPLY [1]

    return Init_Decimal_Or_Percent(OUT, heart, d1 * d2);
}


IMPLEMENT_GENERIC(ROUND, Any_Float)
{
    INCLUDE_PARAMS_OF_ROUND;

    REBDEC d1 = VAL_DECIMAL(ARG(VALUE));
    Heart heart = Heart_Of_Builtin_Fundamental(Element_ARG(VALUE));

    USED(ARG(EVEN)); USED(ARG(DOWN)); USED(ARG(HALF_DOWN));
    USED(ARG(FLOOR)); USED(ARG(CEILING)); USED(ARG(HALF_CEILING));

    Stable* to = opt ARG(TO);
    if (not to) {
        if (heart == TYPE_PERCENT)
            to = Init_Decimal(LOCAL(TO), 0.01L);  // round 5.5% -> 6%
        else
            to = Init_Integer(LOCAL(TO), 1);
    }
    else if (Is_Time(to))
        panic (PARAM(TO));

    d1 = Round_Dec(d1, level_, Dec64(to));

    if (Is_Percent(to)) {
        heart = TYPE_PERCENT;
        return Init_Decimal_Or_Percent(OUT, heart, d1);
    }

    if (Is_Integer(to))
        return Init_Integer(OUT, cast(REBI64, d1));

    return Init_Decimal_Or_Percent(OUT, heart, d1);
}


IMPLEMENT_GENERIC(COMPLEMENT, Any_Float)
{
    INCLUDE_PARAMS_OF_COMPLEMENT;

    REBDEC d = VAL_DECIMAL(ARG(VALUE));

    return Init_Integer(OUT, ~cast(REBINT, d));  // !!! What is this good for?
}
