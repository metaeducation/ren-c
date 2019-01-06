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
// See also: the numeric datatypes
//

#include "sys-core.h"
#include "sys-deci-funcs.h"

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

#define AS_DECIMAL(n) (IS_INTEGER(n) ? (REBDEC)VAL_INT64(n) : VAL_DECIMAL(n))

enum {SINE, COSINE, TANGENT};


//
//  Trig_Value: C
//
// Convert integer arg, if present, to decimal and convert to radians
// if necessary.  Clip ranges for correct REBOL behavior.
//
static REBDEC Trig_Value(const REBVAL *value, bool radians, REBCNT which)
{
    REBDEC dval = AS_DECIMAL(value);

    if (not radians) {
        /* get dval between -360.0 and 360.0 */
        dval = fmod (dval, 360.0);

        /* get dval between -180.0 and 180.0 */
        if (fabs (dval) > 180.0) dval += dval < 0.0 ? 360.0 : -360.0;
        if (which == TANGENT) {
            /* get dval between -90.0 and 90.0 */
            if (fabs (dval) > 90.0) dval += dval < 0.0 ? 180.0 : -180.0;
        } else if (which == SINE) {
            /* get dval between -90.0 and 90.0 */
            if (fabs (dval) > 90.0) dval = (dval < 0.0 ? -180.0 : 180.0) - dval;
        }
        dval = dval * PI / 180.0; // to radians
    }

    return dval;
}


//
//  Arc_Trans: C
//
static void Arc_Trans(REBVAL *out, const REBVAL *value, bool radians, REBCNT kind)
{
    REBDEC dval = AS_DECIMAL(value);
    if (kind != TANGENT and (dval < -1 || dval > 1))
        fail (Error_Overflow_Raw());

    if (kind == SINE) dval = asin(dval);
    else if (kind == COSINE) dval = acos(dval);
    else dval = atan(dval);

    if (not radians)
        dval = dval * 180.0 / PI; // to degrees

    Init_Decimal(out, dval);
}


//
//  cosine: native [
//
//  "Returns the trigonometric cosine."
//
//      return: [decimal!]
//      angle [any-number!]
//      /radians
//          "Value is specified in radians (in degrees by default)"
//  ]
//
REBNATIVE(cosine)
{
    INCLUDE_PARAMS_OF_COSINE;

    REBDEC dval = cos(Trig_Value(ARG(angle), REF(radians), COSINE));
    if (fabs(dval) < DBL_EPSILON)
        dval = 0.0;

    return Init_Decimal(D_OUT, dval);
}


//
//  sine: native [
//
//  "Returns the trigonometric sine."
//
//      return: [decimal!]
//      angle [any-number!]
//      /radians
//          "Value is specified in radians (in degrees by default)"
//  ]
//
REBNATIVE(sine)
{
    INCLUDE_PARAMS_OF_SINE;

    REBDEC dval = sin(Trig_Value(ARG(angle), REF(radians), SINE));
    if (fabs(dval) < DBL_EPSILON)
        dval = 0.0;

    return Init_Decimal(D_OUT, dval);
}


//
//  tangent: native [
//
//  "Returns the trigonometric tangent."
//
//      return: [decimal!]
//      angle [any-number!]
//      /radians
//          "Value is specified in radians (in degrees by default)"
//  ]
//
REBNATIVE(tangent)
{
    INCLUDE_PARAMS_OF_TANGENT;

    REBDEC dval = Trig_Value(ARG(angle), REF(radians), TANGENT);
    if (Eq_Decimal(fabs(dval), PI / 2.0))
        fail (Error_Overflow_Raw());

    return Init_Decimal(D_OUT, tan(dval));
}


//
//  arccosine: native [
//
//  {Returns the trigonometric arccosine.}
//
//      return: [decimal!]
//      cosine [any-number!]
//      /radians
//          "Returns result in radians (in degrees by default)"
//  ]
//
REBNATIVE(arccosine)
{
    INCLUDE_PARAMS_OF_ARCCOSINE;

    Arc_Trans(D_OUT, ARG(cosine), REF(radians), COSINE);
    return D_OUT;
}


//
//  arcsine: native [
//
//  {Returns the trigonometric arcsine.}
//
//      return: [decimal!]
//      sine [any-number!]
//      /radians
//          "Returns result in radians (in degrees by default)"
//  ]
//
REBNATIVE(arcsine)
{
    INCLUDE_PARAMS_OF_ARCSINE;

    Arc_Trans(D_OUT, ARG(sine), REF(radians), SINE);
    return D_OUT;
}


//
//  arctangent: native [
//
//  {Returns the trigonometric arctangent.}
//
//      return: [decimal!]
//      tangent [any-number!]
//      /radians
//          "Returns result in radians (in degrees by default)"
//  ]
//
REBNATIVE(arctangent)
{
    INCLUDE_PARAMS_OF_ARCTANGENT;

    Arc_Trans(D_OUT, ARG(tangent), REF(radians), TANGENT);
    return D_OUT;
}


//
//  exp: native [
//
//  {Raises E (the base of natural logarithm) to the power specified}
//
//      power [any-number!]
//  ]
//
REBNATIVE(exp)
{
    INCLUDE_PARAMS_OF_EXP;

    static REBDEC eps = EPS;
    REBDEC dval = pow(eps, AS_DECIMAL(ARG(power)));

    // !!! Check_Overflow(dval);

    return Init_Decimal(D_OUT, dval);
}


//
//  log-10: native [
//
//  "Returns the base-10 logarithm."
//
//      value [any-number!]
//  ]
//
REBNATIVE(log_10)
{
    INCLUDE_PARAMS_OF_LOG_10;

    REBDEC dval = AS_DECIMAL(ARG(value));
    if (dval <= 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(D_OUT, log10(dval));
}


//
//  log-2: native [
//
//  "Return the base-2 logarithm."
//
//      value [any-number!]
//  ]
//
REBNATIVE(log_2)
{
    INCLUDE_PARAMS_OF_LOG_2;

    REBDEC dval = AS_DECIMAL(ARG(value));
    if (dval <= 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(D_OUT, log(dval) / LOG2);
}


//
//  log-e: native [
//
//  {Returns the natural (base-E) logarithm of the given value}
//
//      value [any-number!]
//  ]
//
REBNATIVE(log_e)
{
    INCLUDE_PARAMS_OF_LOG_E;

    REBDEC dval = AS_DECIMAL(ARG(value));
    if (dval <= 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(D_OUT, log(dval));
}


//
//  square-root: native [
//
//  "Returns the square root of a number."
//
//      value [any-number!]
//  ]
//
REBNATIVE(square_root)
{
    INCLUDE_PARAMS_OF_SQUARE_ROOT;

    REBDEC dval = AS_DECIMAL(ARG(value));
    if (dval < 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(D_OUT, sqrt(dval));
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
//  {Shifts an integer left or right by a number of bits.}
//
//      value [integer!]
//      bits [integer!]
//          "Positive for left shift, negative for right shift"
//      /logical
//          "Logical shift (sign bit ignored)"
//  ]
//
REBNATIVE(shift)
{
    INCLUDE_PARAMS_OF_SHIFT;

    REBI64 b = VAL_INT64(ARG(bits));
    REBVAL *a = ARG(value);

    if (b < 0) {
        REBU64 c = - cast(REBU64, b); // defined, see note on #pragma above
        if (c >= 64) {
            if (REF(logical))
                VAL_INT64(a) = 0;
            else
                VAL_INT64(a) >>= 63;
        }
        else {
            if (REF(logical))
                VAL_INT64(a) = cast(REBU64, VAL_INT64(a)) >> c;
            else
                VAL_INT64(a) >>= cast(REBI64, c);
        }
    }
    else {
        if (b >= 64) {
            if (REF(logical))
                VAL_INT64(a) = 0;
            else if (VAL_INT64(a) != 0)
                fail (Error_Overflow_Raw());
        }
        else {
            if (REF(logical))
                VAL_INT64(a) = cast(REBU64, VAL_INT64(a)) << b;
            else {
                REBU64 c = cast(REBU64, INT64_MIN) >> b;
                REBU64 d = VAL_INT64(a) < 0
                    ? - cast(REBU64, VAL_INT64(a)) // again, see #pragma
                    : cast(REBU64, VAL_INT64(a));
                if (c <= d) {
                    if ((c < d) || (VAL_INT64(a) >= 0))
                        fail (Error_Overflow_Raw());

                    VAL_INT64(a) = INT64_MIN;
                }
                else
                    VAL_INT64(a) <<= b;
            }
        }
    }

    RETURN (ARG(value));
}


// See above for the temporary disablement and reasoning.
//
#if defined(_MSC_VER) && _MSC_VER > 1800
    #pragma warning (default : 4146)
#endif


//  CT_Fail: C
//
REBINT CT_Fail(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    UNUSED(a);
    UNUSED(b);
    UNUSED(mode);

    fail ("Cannot compare type");
}


//  CT_Unhooked: C
//
REBINT CT_Unhooked(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    UNUSED(a);
    UNUSED(b);
    UNUSED(mode);

    fail ("Datatype does not have type comparison handler registered");
}


//
//  Compare_Modify_Values: C
//
// Compare 2 values depending on level of strictness.  It leans
// upon the per-type comparison functions (that have a more typical
// interface of returning [1, 0, -1] and taking a CASE parameter)
// but adds a layer of being able to check for specific types
// of equality...which those comparison functions do not discern.
//
// Strictness:
//     0 - coerced equality
//     1 - strict equality
//
//    -1 - greater or equal
//    -2 - greater
//
// !!! This routine (may) modify the value cells for 'a' and 'b' in
// order to coerce them for easier comparison.  Most usages are
// in native code that can overwrite its argument values without
// that being a problem, so it doesn't matter.
//
REBINT Compare_Modify_Values(RELVAL *a, RELVAL *b, REBINT strictness)
{
    // !!! `(first ['a]) = (first [a])` was true in historical Rebol, due
    // the rules of "lax equality".  These rules are up in the air as they
    // pertain to the IS and ISN'T transition.  But to avoid having to
    // worry about changing all the tests right now, this defines quoted
    // equality as only worryig about the depth in strict equalty.
    //
    if (strictness == 1)
        if (VAL_NUM_QUOTES(a) != VAL_NUM_QUOTES(b))
            return 0;

    // This code wants to modify the value, but we can't modify the
    // embedded values in highly-escaped literals.  Move the data out.

    enum Reb_Kind ta;
    if (KIND_BYTE(a) == REB_QUOTED) { // 4 or more quote levels
        const REBCEL *acell = VAL_UNESCAPED(a);
        Move_Value_Header(a, cast(const RELVAL*, acell));
        a->extra = acell->extra;
        a->payload = acell->payload;
        ta = CELL_KIND(a);
    }
    else
        mutable_KIND_BYTE(a) = ta = CELL_KIND(a); // maybe quoted or not

    enum Reb_Kind tb;
    if (KIND_BYTE(b) == REB_QUOTED) { // 4 or more quote levels
        const REBCEL *bcell = VAL_UNESCAPED(b);
        Move_Value_Header(b, cast(const RELVAL*, bcell));
        b->extra = bcell->extra;
        b->payload = bcell->payload;
        tb = CELL_KIND(b);
    }
    else
        mutable_KIND_BYTE(b) = tb = CELL_KIND(b); // maybe quoted or not

    if (ta != tb) {
        if (strictness == 1)
            return 0;

        switch (ta) {
          case REB_MAX_NULLED:
            return 0; // nothing coerces to void

          case REB_INTEGER:
            if (tb == REB_DECIMAL || tb == REB_PERCENT) {
                REBDEC dec_a = cast(REBDEC, VAL_INT64(a));
                Init_Decimal(a, dec_a);
                goto compare;
            }
            else if (tb == REB_MONEY) {
                deci amount = int_to_deci(VAL_INT64(a));
                Init_Money(a, amount);
                goto compare;
            }
            break;

          case REB_DECIMAL:
          case REB_PERCENT:
            if (tb == REB_INTEGER) {
                REBDEC dec_b = cast(REBDEC, VAL_INT64(b));
                Init_Decimal(b, dec_b);
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
          case REB_ISSUE:
            if (ANY_WORD(b)) goto compare;
            break;

          case REB_TEXT:
          case REB_FILE:
          case REB_EMAIL:
          case REB_URL:
          case REB_TAG:
            if (ANY_STRING(b)) goto compare;
            break;

          default:
            break;
        }

        if (strictness == 0) return 0;

        fail (Error_Invalid_Compare_Raw(Type_Of(a), Type_Of(b)));
    }

  compare:;

    enum Reb_Kind kind = VAL_TYPE(a);

    if (kind == REB_MAX_NULLED) {
        assert(VAL_TYPE(b) == REB_MAX_NULLED);
        return 1; // nulls always equal
    }

    // At this point, the types should match...e.g. be able to be passed to
    // the same comparison dispatcher.  They might not be *exactly* equal.
    //
    COMPARE_HOOK hook = Compare_Hooks[kind];
    assert(Compare_Hooks[VAL_TYPE(b)] == hook);

    REBINT result = hook(a, b, strictness);
    if (result < 0)
        fail (Error_Invalid_Compare_Raw(Type_Of(a), Type_Of(b)));
    return result;
}


//  EQUAL? < EQUIV? < STRICT-EQUAL? < SAME?

//
//  equal?: native [
//
//  {TRUE if the values are equal}
//
//      return: [logic!]
//      value1 [<opt> any-value!]
//      value2 [<opt> any-value!]
//  ]
//
REBNATIVE(equal_q)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    if (Compare_Modify_Values(ARG(value1), ARG(value2), 0))
        return Init_True(D_OUT);

    return Init_False(D_OUT);
}


//
//  not-equal?: native [
//
//  {TRUE if the values are not equal}
//
//      return: [logic!]
//      value1 [<opt> any-value!]
//      value2 [<opt> any-value!]
//  ]
//
REBNATIVE(not_equal_q)
{
    INCLUDE_PARAMS_OF_NOT_EQUAL_Q;

    if (Compare_Modify_Values(ARG(value1), ARG(value2), 0))
        return Init_False(D_OUT);

    return Init_True(D_OUT);
}


//
//  strict-equal?: native [
//
//  {TRUE if the values are strictly equal}
//
//      return: [logic!]
//      value1 [<opt> any-value!]
//      value2 [<opt> any-value!]
//  ]
//
REBNATIVE(strict_equal_q)
{
    INCLUDE_PARAMS_OF_STRICT_EQUAL_Q;

    if (Compare_Modify_Values(ARG(value1), ARG(value2), 1))
        return Init_True(D_OUT);

    return Init_False(D_OUT);
}


//
//  strict-not-equal?: native [
//
//  {TRUE if the values are not strictly equal}
//
//      return: [logic!]
//      value1 [<opt> any-value!]
//      value2 [<opt> any-value!]
//  ]
//
REBNATIVE(strict_not_equal_q)
{
    INCLUDE_PARAMS_OF_STRICT_NOT_EQUAL_Q;

    if (Compare_Modify_Values(ARG(value1), ARG(value2), 1))
        return Init_False(D_OUT);

    return Init_True(D_OUT);
}


//
//  same?: native [
//
//  {TRUE if the values are identical}
//
//      return: [logic!]
//      value1 [<opt> any-value!]
//      value2 [<opt> any-value!]
//  ]
//
REBNATIVE(same_q)
//
// This used to be "strictness mode 3" of Compare_Modify_Values.  However,
// folding SAME?-ness in required the comparisons to take REBVALs instead
// of just REBVALs, when only a limited number of types supported it.
// Rather than incur a cost for all comparisons, this handles the issue
// specially for those types which support it.
{
    INCLUDE_PARAMS_OF_SAME_Q;

    REBVAL *value1 = ARG(value1);
    REBVAL *value2 = ARG(value2);

    if (VAL_TYPE(value1) != VAL_TYPE(value2))
        return Init_False(D_OUT); // can't be "same" value if not same type

    if (IS_BITSET(value1)) {
        //
        // BITSET! only has a series, no index.
        //
        if (VAL_SERIES(value1) != VAL_SERIES(value2))
            return Init_False(D_OUT);
        return Init_True(D_OUT);
    }

    if (ANY_SERIES(value1) || IS_IMAGE(value1)) {
        //
        // ANY-SERIES! can only be the same if pointers and indices match.
        //
        if (VAL_SERIES(value1) != VAL_SERIES(value2))
            return Init_False(D_OUT);
        if (VAL_INDEX(value1) != VAL_INDEX(value2))
            return Init_False(D_OUT);
        return Init_True(D_OUT);
    }

    if (ANY_CONTEXT(value1)) {
        //
        // ANY-CONTEXT! are the same if the varlists match.
        //
        if (VAL_CONTEXT(value1) != VAL_CONTEXT(value2))
            return Init_False(D_OUT);
        return Init_True(D_OUT);
    }

    if (IS_MAP(value1)) {
        //
        // MAP! will be the same if the map pointer matches.
        //
        if (VAL_MAP(value1) != VAL_MAP(value2))
            return Init_False(D_OUT);
        return Init_True(D_OUT);
    }

    if (ANY_WORD(value1)) {
        //
        // ANY-WORD! must match in binding as well as be otherwise equal.
        //
        if (VAL_WORD_SPELLING(value1) != VAL_WORD_SPELLING(value2))
            return Init_False(D_OUT);
        if (VAL_BINDING(value1) != VAL_BINDING(value2))
            return Init_False(D_OUT);
        return Init_True(D_OUT);
    }

    if (IS_DECIMAL(value1) || IS_PERCENT(value1)) {
        //
        // The tolerance on strict-equal? for decimals is apparently not
        // a requirement of exactly the same bits.
        //
        if (
            memcmp(
                &VAL_DECIMAL(value1), &VAL_DECIMAL(value2), sizeof(REBDEC)
            ) == 0
        ){
            return Init_True(D_OUT);
        }

        return Init_False(D_OUT);
    }

    if (IS_MONEY(value1)) {
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
        if (deci_is_same(VAL_MONEY_AMOUNT(value1), VAL_MONEY_AMOUNT(value2)))
            return Init_True(D_OUT);
        return Init_False(D_OUT);
    }

    // For other types, just fall through to strict equality comparison
    //
    if (Compare_Modify_Values(value1, value2, 1))
        return Init_True(D_OUT);

    return Init_False(D_OUT);
}


//
//  lesser?: native [
//
//  {TRUE if the first value is less than the second value}
//
//      return: [logic!]
//      value1 value2
//  ]
//
REBNATIVE(lesser_q)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    if (Compare_Modify_Values(ARG(value1), ARG(value2), -1))
        return Init_False(D_OUT);

    return Init_True(D_OUT);
}


//
//  equal-or-lesser?: native [
//
//  {TRUE if the first value is equal to or less than the second value}
//
//      return: [logic!]
//      value1 value2
//  ]
//
REBNATIVE(equal_or_lesser_q)
{
    INCLUDE_PARAMS_OF_EQUAL_OR_LESSER_Q;

    if (Compare_Modify_Values(ARG(value1), ARG(value2), -2))
        return Init_False(D_OUT);

    return Init_True(D_OUT);
}


//
//  greater?: native [
//
//  {TRUE if the first value is greater than the second value}
//
//      return: [logic!]
//      value1 value2
//  ]
//
REBNATIVE(greater_q)
{
    INCLUDE_PARAMS_OF_GREATER_Q;

    if (Compare_Modify_Values(ARG(value1), ARG(value2), -2))
        return Init_True(D_OUT);

    return Init_False(D_OUT);
}


//
//  greater-or-equal?: native [
//
//  {TRUE if the first value is greater than or equal to the second value}
//
//      return: [logic!]
//      value1 value2
//  ]
//
REBNATIVE(greater_or_equal_q)
{
    INCLUDE_PARAMS_OF_GREATER_OR_EQUAL_Q;

    if (Compare_Modify_Values(ARG(value1), ARG(value2), -1))
        return Init_True(D_OUT);

    return Init_False(D_OUT);
}


//
//  maximum: native [
//
//  "Returns the greater of the two values."
//
//      value1 [any-scalar! date! any-series!]
//      value2 [any-scalar! date! any-series!]
//  ]
//
REBNATIVE(maximum)
{
    INCLUDE_PARAMS_OF_MAXIMUM;

    const REBVAL *value1 = ARG(value1);
    const REBVAL *value2 = ARG(value2);

    if (IS_PAIR(value1) || IS_PAIR(value2)) {
        Min_Max_Pair(D_OUT, value1, value2, true);
    }
    else {
        DECLARE_LOCAL (coerced1);
        Move_Value(coerced1, value1);
        DECLARE_LOCAL (coerced2);
        Move_Value(coerced2, value2);

        if (Compare_Modify_Values(coerced1, coerced2, -1))
            Move_Value(D_OUT, value1);
        else
            Move_Value(D_OUT, value2);
    }
    return D_OUT;
}


//
//  minimum: native [
//
//  "Returns the lesser of the two values."
//
//      value1 [any-scalar! date! any-series!]
//      value2 [any-scalar! date! any-series!]
//  ]
//
REBNATIVE(minimum)
{
    INCLUDE_PARAMS_OF_MINIMUM;

    const REBVAL *value1 = ARG(value1);
    const REBVAL *value2 = ARG(value2);

    if (IS_PAIR(ARG(value1)) || IS_PAIR(ARG(value2))) {
        Min_Max_Pair(D_OUT, ARG(value1), ARG(value2), false);
    }
    else {
        DECLARE_LOCAL (coerced1);
        Move_Value(coerced1, value1);
        DECLARE_LOCAL (coerced2);
        Move_Value(coerced2, value2);

        if (Compare_Modify_Values(coerced1, coerced2, -1))
            Move_Value(D_OUT, value2);
        else
            Move_Value(D_OUT, value1);
    }
    return D_OUT;
}


//
//  negative?: native [
//
//  "Returns TRUE if the number is negative."
//
//      number [any-number! money! time! pair!]
//  ]
//
REBNATIVE(negative_q)
{
    INCLUDE_PARAMS_OF_NEGATIVE_Q;

    DECLARE_LOCAL (zero);
    Init_Zeroed_Hack(zero, VAL_TYPE(ARG(number)));

    if (Compare_Modify_Values(ARG(number), zero, -1))
        return Init_False(D_OUT);

    return Init_True(D_OUT);
}


//
//  positive?: native [
//
//  "Returns TRUE if the value is positive."
//
//      number [any-number! money! time! pair!]
//  ]
//
REBNATIVE(positive_q)
{
    INCLUDE_PARAMS_OF_POSITIVE_Q;

    DECLARE_LOCAL (zero);
    Init_Zeroed_Hack(zero, VAL_TYPE(ARG(number)));

    if (Compare_Modify_Values(ARG(number), zero, -2))
        return Init_True(D_OUT);

    return Init_False(D_OUT);
}


//
//  zero?: native [
//
//  {Returns TRUE if the value is zero (for its datatype).}
//
//      value
//  ]
//
REBNATIVE(zero_q)
{
    INCLUDE_PARAMS_OF_ZERO_Q;

    enum Reb_Kind type = VAL_TYPE(ARG(value));

    if (type >= REB_INTEGER and type <= REB_TIME) {
        DECLARE_LOCAL (zero);
        Init_Zeroed_Hack(zero, type);

        if (Compare_Modify_Values(ARG(value), zero, 1))
            return Init_True(D_OUT);
    }
    return Init_False(D_OUT);
}
