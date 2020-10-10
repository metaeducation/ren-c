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

#include "datatypes/sys-money.h"

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
        the system/options object allowing users to exactly define the
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

bool almost_equal(REBDEC a, REBDEC b, REBLEN max_diff) {
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

    return cast(REBU64, int_diff) <= max_diff;
}


//
//  Init_Decimal_Bits: C
//
REBVAL *Init_Decimal_Bits(RELVAL *out, const REBYTE *bp)
{
    RESET_CELL(out, REB_DECIMAL, CELL_MASK_NONE);

    REBYTE *dp = cast(REBYTE*, &VAL_DECIMAL(out));

  #ifdef ENDIAN_LITTLE
    REBLEN n;
    for (n = 0; n < 8; ++n)
        dp[n] = bp[7 - n];
  #elif defined(ENDIAN_BIG)
    REBLEN n;
    for (n = 0; n < 8; ++n)
        dp[n] = bp[n];
  #else
    #error "Unsupported CPU endian"
  #endif

    return SPECIFIC(out);
}


//
//  MAKE_Decimal: C
//
// !!! The current thinking on the distinction between MAKE and TO is that
// TO should not do any evaluations (including not looking at what words are
// bound to, only their spellings).  Also, TO should be more based on the
// visual intuition vs. internal representational knowledge...this would
// suggest things like `to integer! #"1"` being the number 1, and not a
// codepoint.  Hence historical conversions have been split into the TO
// or MAKE as a rough idea of how these rules might be followed.
//
REB_R MAKE_Decimal(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_DECIMAL or kind == REB_PERCENT);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    REBDEC d;

    switch (VAL_TYPE(arg)) {
      case REB_LOGIC:
        d = VAL_LOGIC(arg) ? 1.0 : 0.0;
        goto dont_divide_if_percent;

      case REB_ISSUE:
        d = cast(REBDEC, VAL_CHAR(arg));
        goto dont_divide_if_percent;

      case REB_TIME:
        d = VAL_NANO(arg) * NANO;
        break;

      case REB_BINARY:
        if (VAL_LEN_AT(arg) < 8)
            fail (arg);

        Init_Decimal_Bits(out, VAL_BIN_AT(arg)); // makes REB_DECIMAL
        RESET_VAL_HEADER(out, kind, CELL_MASK_NONE); // resets if REB_PERCENT
        d = VAL_DECIMAL(out);
        break;

        // !!! It's not obvious that TEXT shouldn't provide conversions; and
        // possibly more kinds than TO does.  Allow it for now, even though
        // TO does it as well.
        //
      case REB_TEXT:
        return TO_Decimal(out, kind, arg);

        // !!! MAKE DECIMAL! from a PATH! ... as opposed to TO DECIMAL ...
        // will allow evaluation of arbitrary code.  This is an experiment on
        // the kinds of distinctions which TO and MAKE may have; it may not
        // be kept as a feature.  Especially since it is of limited use
        // when GROUP!s are evaluative, so `make decimal! '(50%)/2` would
        // require the quote to work if the path was in an evaluative slot.
        //
      case REB_PATH: {  // fractions as 1/2 are an intuitive use for PATH!
        if (VAL_SEQUENCE_LEN(arg) != 2)
            goto bad_make;

        DECLARE_LOCAL (temp1);  // decompress path from cell into values
        DECLARE_LOCAL (temp2);
        const RELVAL *numerator = VAL_SEQUENCE_AT(temp1, arg, 0);
        const RELVAL *denominator = VAL_SEQUENCE_AT(temp2, arg, 1);
        PUSH_GC_GUARD(numerator);  // might be GROUP!, so (1.2)/4
        PUSH_GC_GUARD(denominator);

        REBVAL *quotient = rebValue("divide", numerator, denominator, rebEND);

        DROP_GC_GUARD(denominator);
        DROP_GC_GUARD(numerator);

        if (IS_INTEGER(quotient))
            d = cast(REBDEC, VAL_INT64(quotient));
        else if (IS_DECIMAL(quotient) or IS_PERCENT(quotient))
            d = VAL_DECIMAL(quotient);
        else {
            rebRelease(quotient);
            goto bad_make;  // made *something*, but not DECIMAL! or PERCENT!
        }
        rebRelease(quotient);
        break; }

      case REB_BLOCK:
        if (VAL_ARRAY_LEN_AT(arg) == 2) {
            const RELVAL *item = VAL_ARRAY_AT(arg);
            if (IS_INTEGER(item))
                d = cast(REBDEC, VAL_INT64(item));
            else if (IS_DECIMAL(item) || IS_PERCENT(item))
                d = VAL_DECIMAL(item);
            else
                fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(arg)));

            ++item;

            REBDEC exp;
            if (IS_INTEGER(item))
                exp = cast(REBDEC, VAL_INT64(item));
            else if (IS_DECIMAL(item) || IS_PERCENT(item))
                exp = VAL_DECIMAL(item);
            else
                fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(arg)));

            while (exp >= 1) {
                //
                // !!! Comment here said "funky. There must be a better way"
                //
                --exp;
                d *= 10.0;
                if (!FINITE(d))
                    fail (Error_Overflow_Raw());
            }

            while (exp <= -1) {
                ++exp;
                d /= 10.0;
            }
        }
        else
            fail (Error_Bad_Make(kind, arg));

      default:
        goto bad_make;
    }

    if (kind == REB_PERCENT)
        d /= 100.0;

  dont_divide_if_percent:
    if (!FINITE(d))
        fail (Error_Overflow_Raw());

    RESET_CELL(out, kind, CELL_MASK_NONE);
    VAL_DECIMAL(out) = d;
    return out;

  bad_make:
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Decimal: C
//
// !!! The TO conversions for DECIMAL! are trying to honor the "only obvious"
// conversions, with MAKE used for less obvious (e.g. make decimal [1 5]
// giving you 100000).
//
REB_R TO_Decimal(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_DECIMAL or kind == REB_PERCENT);

    REBDEC d;

    switch (VAL_TYPE(arg)) {
      case REB_DECIMAL:
        assert(VAL_TYPE(arg) != kind);  // would have called COPY if same
        d = VAL_DECIMAL(arg);
        goto dont_divide_if_percent;

      case REB_PERCENT:
        d = VAL_DECIMAL(arg);
        goto dont_divide_if_percent;

      case REB_INTEGER:
        d = cast(REBDEC, VAL_INT64(arg));
        goto dont_divide_if_percent;

      case REB_MONEY:
        d = deci_to_decimal(VAL_MONEY_AMOUNT(arg));
        goto dont_divide_if_percent;

      case REB_TEXT: {
        REBSIZ size;
        const REBYTE *bp
            = Analyze_String_For_Scan(&size, arg, MAX_SCAN_DECIMAL);

        if (NULL == Scan_Decimal(out, bp, size, kind != REB_PERCENT))
            goto bad_to;

        d = VAL_DECIMAL(out); // may need to divide if percent, fall through
        break; }

      case REB_PATH: {  // fractions as 1/2 are an intuitive use for PATH!
        if (VAL_SEQUENCE_LEN(arg) != 2)
            goto bad_to;

        DECLARE_LOCAL (temp1);  // decompress path from cell into values
        DECLARE_LOCAL (temp2);
        const RELVAL *numerator = VAL_SEQUENCE_AT(temp1, arg, 0);
        const RELVAL *denominator = VAL_SEQUENCE_AT(temp2, arg, 1);

        if (not IS_INTEGER(numerator))
            goto bad_to;
        if (not IS_INTEGER(denominator))
            goto bad_to;

        if (VAL_INT64(denominator) == 0)
            fail (Error_Zero_Divide_Raw());

        d = cast(REBDEC, VAL_INT64(numerator))
            / cast(REBDEC, VAL_INT64(denominator));
        break; }

      case REB_TUPLE:  // Resist the urge for `make decimal 1x2` to be 1.2
        goto bad_to;  // it's bad (and 1x02 is the same as 1.2 anyway)

        // !!! This should likely not be a TO conversion, but probably should
        // not be a MAKE conversion either.  So it should be something like
        // AS...or perhaps a special codec like ENBIN?  Leaving compatible
        // for now so people don't have to change it twice.
        //
      case REB_BINARY:
        return MAKE_Decimal(out, kind, nullptr, arg);

      default:
        goto bad_to;
    }

    if (kind == REB_PERCENT)
        d /= 100.0;

  dont_divide_if_percent:
    if (not FINITE(d))
        fail (Error_Overflow_Raw());

    RESET_CELL(out, kind, CELL_MASK_NONE);
    VAL_DECIMAL(out) = d;
    return out;

  bad_to:
    fail (Error_Bad_Cast_Raw(arg, Datatype_From_Kind(kind)));
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
REBINT CT_Decimal(REBCEL(const*) a, REBCEL(const*) b, bool strict)
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


//
//  MF_Decimal: C
//
// Code mostly duplicated in MF_Percent.
//
void MF_Decimal(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);

    REBYTE buf[60];
    REBINT len = Emit_Decimal(
        buf,
        VAL_DECIMAL(v),
        0, // e.g. not DEC_MOLD_PERCENT
        GET_MOLD_FLAG(mo, MOLD_FLAG_COMMA_PT) ? ',' : '.',
        mo->digits
    );
    Append_Ascii_Len(mo->series, s_cast(buf), len);
}


//
//  MF_Percent: C
//
// Code mostly duplicated in MF_Decimal.
//
void MF_Percent(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);

    REBYTE buf[60];
    REBINT len = Emit_Decimal(
        buf,
        VAL_DECIMAL(v),
        DEC_MOLD_PERCENT,
        GET_MOLD_FLAG(mo, MOLD_FLAG_COMMA_PT) ? ',' : '.',
        mo->digits
    );
    Append_Ascii_Len(mo->series, s_cast(buf), len);
}


//
//  REBTYPE: C
//
REBTYPE(Decimal)
{
    REBVAL  *val = D_ARG(1);
    REBVAL  *arg;
    REBDEC  d2;
    enum Reb_Kind type;

    REBDEC d1 = VAL_DECIMAL(val);

    REBSYM sym = VAL_WORD_SYM(verb);

    // !!! This used to use IS_BINARY_ACT() which is no longer available with
    // symbol-based dispatch.  Consider doing this another way.
    //
    if (
        sym == SYM_ADD
        || sym == SYM_SUBTRACT
        || sym == SYM_MULTIPLY
        || sym == SYM_DIVIDE
        || sym == SYM_REMAINDER
        || sym == SYM_POWER
    ){
        arg = D_ARG(2);
        type = VAL_TYPE(arg);
        if ((
            type == REB_PAIR
            or type == REB_TUPLE
            or type == REB_MONEY
            or type == REB_TIME
        ) and (
            sym == SYM_ADD ||
            sym == SYM_MULTIPLY
        )){
            Move_Value(D_OUT, D_ARG(2));
            Move_Value(D_ARG(2), D_ARG(1));
            Move_Value(D_ARG(1), D_OUT);
            return Run_Generic_Dispatch(D_ARG(1), frame_, verb);
        }

        // If the type of the second arg is something we can handle:
        if (type == REB_DECIMAL
            || type == REB_INTEGER
            || type == REB_PERCENT
            || type == REB_MONEY
            || type == REB_ISSUE
        ){
            if (type == REB_DECIMAL) {
                d2 = VAL_DECIMAL(arg);
            }
            else if (type == REB_PERCENT) {
                d2 = VAL_DECIMAL(arg);
                if (sym == SYM_DIVIDE)
                    type = REB_DECIMAL;
                else if (not IS_PERCENT(val))
                    type = VAL_TYPE(val);
            }
            else if (type == REB_MONEY) {
                Init_Money(val, decimal_to_deci(VAL_DECIMAL(val)));
                return T_Money(frame_, verb);
            }
            else if (type == REB_ISSUE) {
                d2 = cast(REBDEC, VAL_CHAR(arg));
                type = REB_DECIMAL;
            }
            else {
                d2 = cast(REBDEC, VAL_INT64(arg));
                type = REB_DECIMAL;
            }

            switch (sym) {

            case SYM_ADD:
                d1 += d2;
                goto setDec;

            case SYM_SUBTRACT:
                d1 -= d2;
                goto setDec;

            case SYM_MULTIPLY:
                d1 *= d2;
                goto setDec;

            case SYM_DIVIDE:
            case SYM_REMAINDER:
                if (d2 == 0.0)
                    fail (Error_Zero_Divide_Raw());
                if (sym == SYM_DIVIDE)
                    d1 /= d2;
                else
                    d1 = fmod(d1, d2);
                goto setDec;

            case SYM_POWER:
                if (d2 == 0) {
                    //
                    // This means `power 0 0` is 1.0, despite it not being
                    // defined.  It's a pretty general programming consensus:
                    //
                    // https://rosettacode.org/wiki/Zero_to_the_zero_power
                    //
                    d1 = 1.0;
                    goto setDec;
                }
                if (d1 == 0)
                    goto setDec;
                d1 = pow(d1, d2);
                goto setDec;

            default:
                fail (Error_Math_Args(VAL_TYPE(val), verb));
            }
        }
        fail (Error_Math_Args(VAL_TYPE(val), verb));
    }

    type = VAL_TYPE(val);

    // unary actions
    switch (sym) {

    case SYM_COPY:
        return Move_Value(D_OUT, val);

    case SYM_NEGATE:
        d1 = -d1;
        goto setDec;

    case SYM_ABSOLUTE:
        if (d1 < 0) d1 = -d1;
        goto setDec;

    case SYM_EVEN_Q:
        d1 = fabs(fmod(d1, 2.0));
        if (d1 < 0.5 || d1 >= 1.5)
            return Init_True(D_OUT);
        return Init_False(D_OUT);

    case SYM_ODD_Q:
        d1 = fabs(fmod(d1, 2.0));
        if (d1 < 0.5 || d1 >= 1.5)
            return Init_False(D_OUT);
        return Init_True(D_OUT);

    case SYM_ROUND: {
        INCLUDE_PARAMS_OF_ROUND;
        USED(ARG(value));  // extracted as d1, others are passed via frame_
        USED(ARG(even)); USED(ARG(down)); USED(ARG(half_down));
        USED(ARG(floor)); USED(ARG(ceiling)); USED(ARG(half_ceiling));

        if (REF(to)) {
            if (IS_MONEY(ARG(to)))
                return Init_Money(D_OUT, Round_Deci(
                    decimal_to_deci(d1), frame_, VAL_MONEY_AMOUNT(ARG(to))
                ));

            if (IS_TIME(ARG(to)))
                fail (PAR(to));

            d1 = Round_Dec(d1, frame_, Dec64(ARG(to)));
            if (IS_INTEGER(ARG(to)))
                return Init_Integer(D_OUT, cast(REBI64, d1));

            if (IS_PERCENT(ARG(to)))
                type = REB_PERCENT;
        }
        else {
            Init_True(ARG(to));  // default a rounding amount
            d1 = Round_Dec(
                d1, frame_, type == REB_PERCENT ? 0.01L : 1.0L
            );
        }
        goto setDec; }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));
        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            REBDEC d = VAL_DECIMAL(val);
            REBI64 i;
            assert(sizeof(d) == sizeof(i));
            memcpy(&i, &d, sizeof(d));
            Set_Random(i); // use IEEE bits
            return nullptr;
        }
        d1 = Random_Dec(d1, did REF(secure));
        goto setDec; }

    case SYM_COMPLEMENT:
        return Init_Integer(D_OUT, ~cast(REBINT, d1));

    default:
        break;
    }

    return R_UNHANDLED;

setDec:
    if (not FINITE(d1))
        fail (Error_Overflow_Raw());

    RESET_CELL(D_OUT, type, CELL_MASK_NONE);
    VAL_DECIMAL(D_OUT) = d1;

    return D_OUT;
}
