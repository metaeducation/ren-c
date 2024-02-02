//
//  File: %n-math.c
//  Summary: "native functions for math"
//  Section: natives
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
// See also: the numeric datatypes
//

#include "sys-core.h"

#include "cells/cell-money.h"

#include <math.h>
#include <float.h>

#define LOG2    0.6931471805599453
#define EPS     2.718281828459045235360287471

#ifndef PI
    #define PI 3.14159265358979323846E0
#endif

#ifndef DBL_EPSILON
    #define DBL_EPSILON 2.2204460492503131E-16
#endif

#define AS_DECIMAL(n) (Is_Integer(n) ? (REBDEC)VAL_INT64(n) : VAL_DECIMAL(n))


//
//  Trig_Value: C
//
// Convert integer arg, if present, to decimal and convert to radians
// if necessary.  Clip ranges for correct REBOL behavior.
//
static REBDEC Trig_Value(
    const REBVAL *value,
    bool radians,
    SymId which
){
    REBDEC dval = AS_DECIMAL(value);

    if (not radians) {
        /* get dval between -360.0 and 360.0 */
        dval = fmod (dval, 360.0);

        /* get dval between -180.0 and 180.0 */
        if (fabs (dval) > 180.0)
            dval += dval < 0.0 ? 360.0 : -360.0;
        if (which == SYM_TANGENT) {
            /* get dval between -90.0 and 90.0 */
            if (fabs (dval) > 90.0)
                dval += dval < 0.0 ? 180.0 : -180.0;
        } else if (which == SYM_SINE) {
            /* get dval between -90.0 and 90.0 */
            if (fabs (dval) > 90.0)
                dval = (dval < 0.0 ? -180.0 : 180.0) - dval;
        }
        dval = dval * PI / 180.0; // to radians
    }

    return dval;
}


//
//  Arc_Trans: C
//
static void Arc_Trans(
    Sink(Value*) out,
    const Value* value,
    bool radians,
    SymId which
){
    REBDEC dval = AS_DECIMAL(value);
    if (which != SYM_TANGENT and (dval < -1 || dval > 1))
        fail (Error_Overflow_Raw());

    if (which == SYM_SINE)
        dval = asin(dval);
    else if (which == SYM_COSINE)
        dval = acos(dval);
    else {
        assert(which == SYM_TANGENT);
        dval = atan(dval);
    }

    if (not radians)
        dval = dval * 180.0 / PI; // to degrees

    Init_Decimal(out, dval);
}


//
//  cosine: native [
//
//  "Returns the trigonometric cosine"
//
//      return: [decimal!]
//      angle [any-number!]
//      /radians
//          "Value is specified in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(cosine)
{
    INCLUDE_PARAMS_OF_COSINE;

    REBDEC dval = cos(Trig_Value(ARG(angle), REF(radians), SYM_COSINE));
    if (fabs(dval) < DBL_EPSILON)
        dval = 0.0;

    return Init_Decimal(OUT, dval);
}


//
//  sine: native [
//
//  "Returns the trigonometric sine"
//
//      return: [decimal!]
//      angle [any-number!]
//      /radians
//          "Value is specified in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(sine)
{
    INCLUDE_PARAMS_OF_SINE;

    REBDEC dval = sin(Trig_Value(ARG(angle), REF(radians), SYM_SINE));
    if (fabs(dval) < DBL_EPSILON)
        dval = 0.0;

    return Init_Decimal(OUT, dval);
}


//
//  tangent: native [
//
//  "Returns the trigonometric tangent"
//
//      return: [decimal!]
//      angle [any-number!]
//      /radians
//          "Value is specified in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(tangent)
{
    INCLUDE_PARAMS_OF_TANGENT;

    REBDEC dval = Trig_Value(ARG(angle), REF(radians), SYM_TANGENT);
    if (Eq_Decimal(fabs(dval), PI / 2.0))
        fail (Error_Overflow_Raw());

    return Init_Decimal(OUT, tan(dval));
}


//
//  arccosine: native [
//
//  "Returns the trigonometric arccosine"
//
//      return: [decimal!]
//      cosine [any-number!]
//      /radians
//          "Returns result in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(arccosine)
{
    INCLUDE_PARAMS_OF_ARCCOSINE;

    Arc_Trans(OUT, ARG(cosine), REF(radians), SYM_COSINE);
    return OUT;
}


//
//  arcsine: native [
//
//  "Returns the trigonometric arcsine"
//
//      return: [decimal!]
//      sine [any-number!]
//      /radians
//          "Returns result in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(arcsine)
{
    INCLUDE_PARAMS_OF_ARCSINE;

    Arc_Trans(OUT, ARG(sine), REF(radians), SYM_SINE);
    return OUT;
}


//
//  arctangent: native [
//
//  "Returns the trigonometric arctangent"
//
//      return: [decimal!]
//      tangent [any-number!]
//      /radians
//          "Returns result in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(arctangent)
{
    INCLUDE_PARAMS_OF_ARCTANGENT;

    Arc_Trans(OUT, ARG(tangent), REF(radians), SYM_TANGENT);
    return OUT;
}


//
//  exp: native [
//
//  "Raises E (the base of natural logarithm) to the power specified"
//
//      return: [decimal!]
//      power [any-number!]
//  ]
//
DECLARE_NATIVE(exp)
{
    INCLUDE_PARAMS_OF_EXP;

    static REBDEC eps = EPS;
    REBDEC dval = pow(eps, AS_DECIMAL(ARG(power)));

    // !!! Check_Overflow(dval);

    return Init_Decimal(OUT, dval);
}


//
//  log-10: native [
//
//  "Returns the base-10 logarithm"
//
//      return: [decimal!]
//      value [any-number!]
//  ]
//
DECLARE_NATIVE(log_10)
{
    INCLUDE_PARAMS_OF_LOG_10;

    REBDEC dval = AS_DECIMAL(ARG(value));
    if (dval <= 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(OUT, log10(dval));
}


//
//  log-2: native [
//
//  "Return the base-2 logarithm"
//
//      return: [decimal!]
//      value [any-number!]
//  ]
//
DECLARE_NATIVE(log_2)
{
    INCLUDE_PARAMS_OF_LOG_2;

    REBDEC dval = AS_DECIMAL(ARG(value));
    if (dval <= 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(OUT, log(dval) / LOG2);
}


//
//  log-e: native [
//
//  "Returns the natural (base-E) logarithm of the given value"
//
//      return: [decimal!]
//      value [any-number!]
//  ]
//
DECLARE_NATIVE(log_e)
{
    INCLUDE_PARAMS_OF_LOG_E;

    REBDEC dval = AS_DECIMAL(ARG(value));
    if (dval <= 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(OUT, log(dval));
}


//
//  square-root: native [
//
//  "Returns the square root of a number"
//
//      return: [decimal!]
//      value [any-number!]
//  ]
//
DECLARE_NATIVE(square_root)
{
    INCLUDE_PARAMS_OF_SQUARE_ROOT;

    REBDEC dval = AS_DECIMAL(ARG(value));
    if (dval < 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(OUT, sqrt(dval));
}



//
// The SHIFT native uses negation of an unsigned number.  Although the
// operation is well-defined in the C language, it is usually a mistake.
// MSVC warns about it, so temporarily disable that.
//
// !!! The usage of negation of unsigned in SHIFT is from R3-Alpha.  Should it
// be rewritten another way?
//
// http://stackoverflow.com/a/36349666/211160
//
#if defined(_MSC_VER) && _MSC_VER > 1800
    #pragma warning (disable : 4146)
#endif


//
//  shift: native [
//
//  "Shifts an integer left or right by a number of bits"
//
//      return: [integer!]
//      value [integer!]
//      bits [integer!]
//          "Positive for left shift, negative for right shift"
//      /logical
//          "Logical shift (sign bit ignored)"
//  ]
//
DECLARE_NATIVE(shift)
{
    INCLUDE_PARAMS_OF_SHIFT;

    REBI64 b = VAL_INT64(ARG(bits));
    REBVAL *a = ARG(value);

    if (b < 0) {
        REBU64 c = - cast(REBU64, b); // defined, see note on #pragma above
        if (c >= 64) {
            if (REF(logical))
                mutable_VAL_INT64(a) = 0;
            else
                mutable_VAL_INT64(a) >>= 63;
        }
        else {
            if (REF(logical))
                mutable_VAL_INT64(a) = cast(REBU64, VAL_INT64(a)) >> c;
            else
                mutable_VAL_INT64(a) >>= cast(REBI64, c);
        }
    }
    else {
        if (b >= 64) {
            if (REF(logical))
                mutable_VAL_INT64(a) = 0;
            else if (VAL_INT64(a) != 0)
                fail (Error_Overflow_Raw());
        }
        else {
            if (REF(logical))
                mutable_VAL_INT64(a) = cast(REBU64, VAL_INT64(a)) << b;
            else {
                REBU64 c = cast(REBU64, INT64_MIN) >> b;
                REBU64 d = VAL_INT64(a) < 0
                    ? - cast(REBU64, VAL_INT64(a)) // again, see #pragma
                    : cast(REBU64, VAL_INT64(a));
                if (c <= d) {
                    if ((c < d) || (VAL_INT64(a) >= 0))
                        fail (Error_Overflow_Raw());

                    mutable_VAL_INT64(a) = INT64_MIN;
                }
                else
                    mutable_VAL_INT64(a) <<= b;
            }
        }
    }

    return COPY(ARG(value));
}


// See above for the temporary disablement and reasoning.
//
#if defined(_MSC_VER) && _MSC_VER > 1800
    #pragma warning (default : 4146)
#endif


//  CT_Fail: C
//
REBINT CT_Fail(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(a);
    UNUSED(b);
    UNUSED(strict);

    fail ("Cannot compare type");
}


//  CT_Unhooked: C
//
REBINT CT_Unhooked(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(a);
    UNUSED(b);
    UNUSED(strict);

    fail ("Datatype does not have type comparison handler registered");
}


//
//  Compare_Modify_Values: C
//
// Compare 2 values depending on level of strictness.
//
// !!! This routine (may) modify the value cells for 'a' and 'b' in
// order to coerce them for easier comparison.  Most usages are
// in native code that can overwrite its argument values without
// that being a problem, so it doesn't matter.
//
REBINT Compare_Modify_Values(Cell* a, Cell* b, bool strict)
{
    // Note: `(first ['a]) = (first [a])` was true in historical Rebol, due
    // the rules of "lax equality".  This is a harmful choice, and has been
    // removed:
    //
    // https://forum.rebol.info/t/1133/7
    //
    if (QUOTE_BYTE(a) != QUOTE_BYTE(b))
        return QUOTE_BYTE(a) > QUOTE_BYTE(b) ? 1 : -1;

    QUOTE_BYTE(a) = NOQUOTE_1;
    QUOTE_BYTE(b) = NOQUOTE_1;

    enum Reb_Kind ta = Cell_Heart(a);
    enum Reb_Kind tb = Cell_Heart(b);

    assert(ta < REB_MAX);  // we dequoted it
    assert(tb < REB_MAX);  // we dequoted this as well

    if (ta != tb) {
        //
        // If types not matching is a problem, callers to this routine should
        // check that for themselves before calling.  It is assumed that
        // "strict" here still allows coercion, e.g. `1 < 1.1` should work.
        //
        switch (ta) {
          case REB_VOID:
            return -1;  // consider always less than anything else

          case REB_INTEGER:
            if (tb == REB_DECIMAL || tb == REB_PERCENT) {
                Init_Decimal(a, cast(REBDEC, VAL_INT64(a)));
                goto compare;
            }
            else if (tb == REB_MONEY) {
                Init_Money(a, int_to_deci(VAL_INT64(a)));
                goto compare;
            }
            break;

          case REB_DECIMAL:
          case REB_PERCENT:
            if (tb == REB_INTEGER) {
                Init_Decimal(b, cast(REBDEC, VAL_INT64(b)));
                goto compare;
            }
            else if (tb == REB_MONEY) {
                Init_Money(a, decimal_to_deci(VAL_DECIMAL(a)));
                goto compare;
            }
            else if (tb == REB_DECIMAL || tb == REB_PERCENT) // equivalent types
                goto compare;
            break;

          case REB_MONEY:
            if (tb == REB_INTEGER) {
                Init_Money(b, int_to_deci(VAL_INT64(b)));
                goto compare;
            }
            if (tb == REB_DECIMAL || tb == REB_PERCENT) {
                Init_Money(b, decimal_to_deci(VAL_DECIMAL(b)));
                goto compare;
            }
            break;

          case REB_WORD:
          case REB_SET_WORD:
          case REB_GET_WORD:
          case REB_META_WORD:
            if (Any_Word_Kind(tb)) goto compare;
            break;

          case REB_TEXT:
          case REB_FILE:
          case REB_EMAIL:
          case REB_URL:
          case REB_TAG:
            if (Any_String_Kind(tb)) goto compare;
            break;

          default:
            break;
        }

        if (not strict)
            return ta > tb ? 1 : -1;  // !!! Review

        fail (Error_Invalid_Compare_Raw(
            Datatype_From_Kind(ta),
            Datatype_From_Kind(tb)
        ));
    }

  compare:;

    enum Reb_Kind kind = Cell_Heart(a);

    if (kind == REB_VOID) {
        assert(HEART_BYTE(b) == REB_VOID);
        return 0;  // voids always equal
    }

    // At this point, the types should match...e.g. be able to be passed to
    // the same comparison dispatcher.  They might not be *exactly* equal.
    //
    COMPARE_HOOK *hook = Compare_Hook_For_Type_Of(a);
    assert(Compare_Hook_For_Type_Of(b) == hook);

    REBINT diff = hook(a, b, strict);
    assert(diff == 0 or diff == 1 or diff == -1);
    return diff;
}


//  EQUAL? < EQUIV? < STRICT-EQUAL? < SAME?

//
//  equal?: native [
//
//  "TRUE if the values are equal"
//
//      return: [logic?]
//      value1 [any-value?]
//      value2 [any-value?]
//  ]
//
DECLARE_NATIVE(equal_q)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    bool strict = false;
    REBINT diff = Compare_Modify_Values(ARG(value1), ARG(value2), strict);
    return Init_Logic(OUT, diff == 0);
}


//
//  not-equal?: native [
//
//  "TRUE if the values are not equal"
//
//      return: [logic?]
//      value1 [any-value?]
//      value2 [any-value?]
//  ]
//
DECLARE_NATIVE(not_equal_q)
{
    INCLUDE_PARAMS_OF_NOT_EQUAL_Q;

    bool strict = false;
    REBINT diff = Compare_Modify_Values(ARG(value1), ARG(value2), strict);
    return Init_Logic(OUT, diff != 0);
}


//
//  strict-equal?: native [
//
//  "TRUE if the values are strictly equal"
//
//      return: [logic?]
//      value1 [any-value?]
//      value2 [any-value?]
//  ]
//
DECLARE_NATIVE(strict_equal_q)
{
    INCLUDE_PARAMS_OF_STRICT_EQUAL_Q;

    if (VAL_TYPE(ARG(value1)) != VAL_TYPE(ARG(value2)))
        return Init_False(OUT);  // don't allow coercion

    bool strict = true;
    REBINT diff = Compare_Modify_Values(ARG(value1), ARG(value2), strict);
    return Init_Logic(OUT, diff == 0);
}


//
//  strict-not-equal?: native [
//
//  "TRUE if the values are not strictly equal"
//
//      return: [logic?]
//      value1 [any-value?]
//      value2 [any-value?]
//  ]
//
DECLARE_NATIVE(strict_not_equal_q)
{
    INCLUDE_PARAMS_OF_STRICT_NOT_EQUAL_Q;

    if (VAL_TYPE(ARG(value1)) != VAL_TYPE(ARG(value2)))
        return Init_True(OUT);  // don't allow coercion

    bool strict = true;
    REBINT diff = Compare_Modify_Values(ARG(value1), ARG(value2), strict);
    return Init_Logic(OUT, diff != 0);
}


//
//  same?: native [
//
//  "TRUE if the values are identical"
//
//      return: [logic?]
//      value1 [any-value?]
//      value2 [any-value?]
//  ]
//
DECLARE_NATIVE(same_q)
//
// This used to be "strictness mode 3" of Compare_Modify_Values.  However,
// folding SAME?-ness in required the comparisons to take REBVALs instead
// of just Cells, when only a limited number of types supported it.
// Rather than incur a cost for all comparisons, this handles the issue
// specially for those types which support it.
{
    INCLUDE_PARAMS_OF_SAME_Q;

    REBVAL *v1 = ARG(value1);
    REBVAL *v2 = ARG(value2);

    if (VAL_TYPE(v1) != VAL_TYPE(v2))
        return Init_False(OUT);  // can't be "same" value if not same type

    if (Is_Bitset(v1))  // same if binaries are same
        return Init_Logic(OUT, VAL_BITSET(v1) == VAL_BITSET(v2));

    if (Any_Series(v1))  // pointers -and- indices must match
        return Init_Logic(
            OUT,
            Cell_Series(v1) == Cell_Series(v2)
                and VAL_INDEX_RAW(v1) == VAL_INDEX_RAW(v2)  // permissive
        );

    if (Any_Context(v1))  // same if varlists match
        return Init_Logic(OUT, VAL_CONTEXT(v1) == VAL_CONTEXT(v2));

    if (Is_Map(v1))  // same if map pointer matches
        return Init_Logic(OUT, VAL_MAP(v1) == VAL_MAP(v2));

    if (Any_Word(v1))  // !!! "same" was spelling -and- binding in R3-Alpha
        return Init_Logic(
            OUT,
            Cell_Word_Symbol(v1) == Cell_Word_Symbol(v2)
                and BINDING(v1) == BINDING(v2)
        );

    if (Is_Decimal(v1) or Is_Percent(v1)) {
        //
        // !!! R3-Alpha's STRICT-EQUAL? for DECIMAL! did not require *exactly*
        // the same bits, but SAME? did.  :-/
        //
        return Init_Logic(
            OUT,
            0 == memcmp(&VAL_DECIMAL(v1), &VAL_DECIMAL(v2), sizeof(REBDEC))
        );
    }

    if (Is_Money(v1)) {
        //
        // There is apparently a distinction between "strict equal" and "same"
        // when it comes to the MONEY! type:
        //
        // >> strict-equal? $1 $1.0
        // == true
        //
        // >> same? $1 $1.0
        // == false
        //
        return Init_Logic(
            OUT,
            deci_is_same(VAL_MONEY_AMOUNT(v1), VAL_MONEY_AMOUNT(v2))
        );
    }

    // For other types, just fall through to strict equality comparison
    //
    // !!! What about user extension types, like IMAGE! and STRUCT!?  It
    // seems that "sameness" should go through whatever extension mechanism
    // for comparison user defined types would have.
    //
    bool strict = true;
    return Init_Logic(OUT, Compare_Modify_Values(v1, v2, strict) == 0);
}


//
//  lesser?: native [
//
//  "TRUE if the first value is less than the second value"
//
//      return: [logic?]
//      value1 value2
//  ]
//
DECLARE_NATIVE(lesser_q)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    // !!! R3-Alpha and Red both behave thusly:
    //
    //     >> -4.94065645841247E-324 < 0.0
    //     == true
    //
    //     >> -4.94065645841247E-324 = 0.0
    //     == true
    //
    // This is to say that the `=` is operating under non-strict rules, while
    // the `<` is still strict to see the difference.  Kept this way for
    // compatibility for now.
    //
    // BUT one exception is made for dates, so that they will compare
    // (26-Jul-2021/7:41:45.314 > 26-Jul-2021) to be false.  This requires
    // being willing to consider them equal, hence non-strict.
    //
    bool strict =
        HEART_BYTE(ARG(value1)) != REB_DATE
        and HEART_BYTE(ARG(value2)) != REB_DATE;

    REBINT diff = Compare_Modify_Values(ARG(value1), ARG(value2), strict);
    return Init_Logic(OUT, diff == -1);
}


//
//  equal-or-lesser?: native [
//
//  "TRUE if the first value is equal to or less than the second value"
//
//      return: [logic?]
//      value1 value2
//  ]
//
DECLARE_NATIVE(equal_or_lesser_q)
{
    INCLUDE_PARAMS_OF_EQUAL_OR_LESSER_Q;

    bool strict = true;  // see notes in LESSER?
    REBINT diff = Compare_Modify_Values(ARG(value1), ARG(value2), strict);
    return Init_Logic(OUT, diff == -1 or diff == 0);
}


//
//  greater?: native [
//
//  "TRUE if the first value is greater than the second value"
//
//      return: [logic?]
//      value1 value2
//  ]
//
DECLARE_NATIVE(greater_q)
{
    INCLUDE_PARAMS_OF_GREATER_Q;

    bool strict =  // see notes in LESSER?
        HEART_BYTE(ARG(value1)) != REB_DATE
        and HEART_BYTE(ARG(value2)) != REB_DATE;

    REBINT diff = Compare_Modify_Values(ARG(value1), ARG(value2), strict);
    return Init_Logic(OUT, diff == 1);
}


//
//  greater-or-equal?: native [
//
//  "TRUE if the first value is greater than or equal to the second value"
//
//      return: [logic?]
//      value1 value2
//  ]
//
DECLARE_NATIVE(greater_or_equal_q)
{
    INCLUDE_PARAMS_OF_GREATER_OR_EQUAL_Q;

    bool strict = true;  // see notes in LESSER?
    REBINT diff = Compare_Modify_Values(ARG(value1), ARG(value2), strict);
    return Init_Logic(OUT, diff == 1 or diff == 0);
}


//
//  maximum: native [
//
//  "Returns the greater of the two values"
//
//      return: [any-scalar! date! any-series!]
//      value1 [any-scalar! date! any-series!]
//      value2 [any-scalar! date! any-series!]
//  ]
//
DECLARE_NATIVE(maximum)
{
    INCLUDE_PARAMS_OF_MAXIMUM;

    const Value* value1 = ARG(value1);
    const Value* value2 = ARG(value2);

    if (Is_Pair(value1) || Is_Pair(value2)) {
        Min_Max_Pair(OUT, value1, value2, true);
    }
    else {
        DECLARE_LOCAL (coerced1);
        Copy_Cell(coerced1, value1);
        DECLARE_LOCAL (coerced2);
        Copy_Cell(coerced2, value2);

        bool strict = false;
        REBINT diff = Compare_Modify_Values(coerced1, coerced2, strict);
        if (diff == 1)
            Copy_Cell(OUT, value1);
        else {
            assert(diff == 0 or diff == -1);
            Copy_Cell(OUT, value2);
        }
    }
    return OUT;
}


//
//  minimum: native [
//
//  "Returns the lesser of the two values"
//
//      return: [any-scalar! date! any-series!]
//      value1 [any-scalar! date! any-series!]
//      value2 [any-scalar! date! any-series!]
//  ]
//
DECLARE_NATIVE(minimum)
{
    INCLUDE_PARAMS_OF_MINIMUM;

    const REBVAL *value1 = ARG(value1);
    const REBVAL *value2 = ARG(value2);

    if (Is_Pair(ARG(value1)) || Is_Pair(ARG(value2))) {
        Min_Max_Pair(OUT, ARG(value1), ARG(value2), false);
    }
    else {
        DECLARE_LOCAL (coerced1);
        Copy_Cell(coerced1, value1);
        DECLARE_LOCAL (coerced2);
        Copy_Cell(coerced2, value2);

        bool strict = false;
        REBINT diff = Compare_Modify_Values(coerced1, coerced2, strict);
        if (diff == -1)
            Copy_Cell(OUT, value1);
        else {
            assert(diff == 0 or diff == 1);
            Copy_Cell(OUT, value2);
        }
    }
    return OUT;
}


inline static REBVAL *Init_Zeroed_Hack(Cell* out, enum Reb_Kind kind) {
    //
    // !!! This captures of a dodgy behavior of R3-Alpha, which was to assume
    // that clearing the payload of a value and then setting the header made
    // it the `zero?` of that type.  Review uses.
    //
    if (kind == REB_PAIR) {
        Init_Pair_Int(out, 0, 0);
    }
    else {
        Reset_Unquoted_Header_Untracked(
            TRACK(out), FLAG_HEART_BYTE(kind) | CELL_MASK_NO_NODES
        );
        memset(&out->extra, 0, sizeof(union ValueExtraUnion));
        memset(&out->payload, 0, sizeof(union ValuePayloadUnion));
    }
    return cast(REBVAL*, out);
}


//
//  negative?: native [
//
//  "Returns TRUE if the number is negative"
//
//      return: [logic?]
//      number [any-number! money! time! pair!]
//  ]
//
DECLARE_NATIVE(negative_q)
{
    INCLUDE_PARAMS_OF_NEGATIVE_Q;

    DECLARE_LOCAL (zero);
    Init_Zeroed_Hack(zero, VAL_TYPE(ARG(number)));

    bool strict = true;  // don't report "close to zero" as "equal to zero"
    REBINT diff = Compare_Modify_Values(ARG(number), zero, strict);
    return Init_Logic(OUT, diff == -1);
}


//
//  positive?: native [
//
//  "Returns TRUE if the value is positive"
//
//      return: [logic?]
//      number [any-number! money! time! pair!]
//  ]
//
DECLARE_NATIVE(positive_q)
{
    INCLUDE_PARAMS_OF_POSITIVE_Q;

    DECLARE_LOCAL (zero);
    Init_Zeroed_Hack(zero, VAL_TYPE(ARG(number)));

    bool strict = true;  // don't report "close to zero" as "equal to zero"
    REBINT diff = Compare_Modify_Values(ARG(number), zero, strict);
    return Init_Logic(OUT, diff == 1);
}


//
//  zero?: native [
//
//  "Returns TRUE if the value is zero (for its datatype)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(zero_q)
{
    INCLUDE_PARAMS_OF_ZERO_Q;

    REBVAL *v = ARG(value);
    enum Reb_Kind type = VAL_TYPE(v);

    if (type == REB_ISSUE)  // special case, `#` represents the '\0' codepoint
        return Init_Logic(OUT, IS_CHAR(v) and Cell_Codepoint(v) == 0);

    if (not Any_Scalar_Kind(type))
        return Init_False(OUT);

    if (type == REB_TUPLE) {
        REBLEN len = Cell_Sequence_Len(v);
        REBLEN i;
        for (i = 0; i < len; ++i) {
            Copy_Sequence_At(SPARE, v, i);
            if (not Is_Integer(SPARE) or VAL_INT64(SPARE) != 0)
                return Init_False(OUT);
        }
        return Init_True(OUT);
    }

    DECLARE_LOCAL (zero);
    Init_Zeroed_Hack(zero, type);

    bool strict = true;  // don't report "close to zero" as "equal to zero"
    REBINT diff = Compare_Modify_Values(ARG(value), zero, strict);
    return Init_Logic(OUT, diff == 0);
}
