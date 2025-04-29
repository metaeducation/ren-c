//
//  file: %n-math.c
//  summary: "native functions for math"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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

enum {SINE, COSINE, TANGENT};


//
//  Trig_Value: C
//
// Convert integer arg, if present, to decimal and convert to radians
// if necessary.  Clip ranges for correct REBOL behavior.
//
static REBDEC Trig_Value(const Value* value, bool radians, REBLEN which)
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
static void Arc_Trans(Value* out, const Value* value, bool radians, REBLEN kind)
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
DECLARE_NATIVE(COSINE)
{
    INCLUDE_PARAMS_OF_COSINE;

    REBDEC dval = cos(Trig_Value(ARG(ANGLE), Bool_ARG(RADIANS), COSINE));
    if (fabs(dval) < DBL_EPSILON)
        dval = 0.0;

    return Init_Decimal(OUT, dval);
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
DECLARE_NATIVE(SINE)
{
    INCLUDE_PARAMS_OF_SINE;

    REBDEC dval = sin(Trig_Value(ARG(ANGLE), Bool_ARG(RADIANS), SINE));
    if (fabs(dval) < DBL_EPSILON)
        dval = 0.0;

    return Init_Decimal(OUT, dval);
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
DECLARE_NATIVE(TANGENT)
{
    INCLUDE_PARAMS_OF_TANGENT;

    REBDEC dval = Trig_Value(ARG(ANGLE), Bool_ARG(RADIANS), TANGENT);
    if (Eq_Decimal(fabs(dval), PI / 2.0))
        fail (Error_Overflow_Raw());

    return Init_Decimal(OUT, tan(dval));
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
DECLARE_NATIVE(ARCCOSINE)
{
    INCLUDE_PARAMS_OF_ARCCOSINE;

    Arc_Trans(OUT, ARG(COSINE), Bool_ARG(RADIANS), COSINE);
    return OUT;
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
DECLARE_NATIVE(ARCSINE)
{
    INCLUDE_PARAMS_OF_ARCSINE;

    Arc_Trans(OUT, ARG(SINE), Bool_ARG(RADIANS), SINE);
    return OUT;
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
DECLARE_NATIVE(ARCTANGENT)
{
    INCLUDE_PARAMS_OF_ARCTANGENT;

    Arc_Trans(OUT, ARG(TANGENT), Bool_ARG(RADIANS), TANGENT);
    return OUT;
}


//
//  exp: native [
//
//  {Raises E (the base of natural logarithm) to the power specified}
//
//      power [any-number!]
//  ]
//
DECLARE_NATIVE(EXP)
{
    INCLUDE_PARAMS_OF_EXP;

    static REBDEC eps = EPS;
    REBDEC dval = pow(eps, AS_DECIMAL(ARG(POWER)));

    // !!! Check_Overflow(dval);

    return Init_Decimal(OUT, dval);
}


//
//  log-10: native [
//
//  "Returns the base-10 logarithm."
//
//      value [any-number!]
//  ]
//
DECLARE_NATIVE(LOG_10)
{
    INCLUDE_PARAMS_OF_LOG_10;

    REBDEC dval = AS_DECIMAL(ARG(VALUE));
    if (dval <= 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(OUT, log10(dval));
}


//
//  log-2: native [
//
//  "Return the base-2 logarithm."
//
//      value [any-number!]
//  ]
//
DECLARE_NATIVE(LOG_2)
{
    INCLUDE_PARAMS_OF_LOG_2;

    REBDEC dval = AS_DECIMAL(ARG(VALUE));
    if (dval <= 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(OUT, log(dval) / LOG2);
}


//
//  log-e: native [
//
//  {Returns the natural (base-E) logarithm of the given value}
//
//      value [any-number!]
//  ]
//
DECLARE_NATIVE(LOG_E)
{
    INCLUDE_PARAMS_OF_LOG_E;

    REBDEC dval = AS_DECIMAL(ARG(VALUE));
    if (dval <= 0)
        fail (Error_Positive_Raw());

    return Init_Decimal(OUT, log(dval));
}


//
//  square-root: native [
//
//  "Returns the square root of a number."
//
//      value [any-number!]
//  ]
//
DECLARE_NATIVE(SQUARE_ROOT)
{
    INCLUDE_PARAMS_OF_SQUARE_ROOT;

    REBDEC dval = AS_DECIMAL(ARG(VALUE));
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
//  {Shifts an integer left or right by a number of bits.}
//
//      value [integer!]
//      bits [integer!]
//          "Positive for left shift, negative for right shift"
//      /logical
//          "Logical shift (sign bit ignored)"
//  ]
//
DECLARE_NATIVE(SHIFT)
{
    INCLUDE_PARAMS_OF_SHIFT;

    REBI64 b = VAL_INT64(ARG(BITS));
    Value* a = ARG(VALUE);

    if (b < 0) {
        REBU64 c = - cast(REBU64, b); // defined, see note on #pragma above
        if (c >= 64) {
            if (Bool_ARG(LOGICAL))
                VAL_INT64(a) = 0;
            else
                VAL_INT64(a) >>= 63;
        }
        else {
            if (Bool_ARG(LOGICAL))
                VAL_INT64(a) = cast(REBU64, VAL_INT64(a)) >> c;
            else
                VAL_INT64(a) >>= cast(REBI64, c);
        }
    }
    else {
        if (b >= 64) {
            if (Bool_ARG(LOGICAL))
                VAL_INT64(a) = 0;
            else if (VAL_INT64(a) != 0)
                fail (Error_Overflow_Raw());
        }
        else {
            if (Bool_ARG(LOGICAL))
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

    RETURN (ARG(VALUE));
}


// See above for the temporary disablement and reasoning.
//
#if defined(_MSC_VER) && _MSC_VER > 1800
    #pragma warning (default : 4146)
#endif


//  CT_Fail: C
//
REBINT CT_Fail(const Cell* a, const Cell* b, REBINT mode)
{
    UNUSED(a);
    UNUSED(b);
    UNUSED(mode);

    fail ("Cannot compare type");
}


//  CT_Unhooked: C
//
REBINT CT_Unhooked(const Cell* a, const Cell* b, REBINT mode)
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
REBINT Compare_Modify_Values(Cell* a, Cell* b, REBINT strictness)
{
    REBLEN ta = Type_Of(a);
    REBLEN tb = Type_Of(b);

    if (ta != tb) {
        if (strictness == 1) return 0;

        switch (ta) {
        case TYPE_NULLED:
            return 0; // nothing coerces to void

        case TYPE_INTEGER:
            if (tb == TYPE_DECIMAL || tb == TYPE_PERCENT) {
                REBDEC dec_a = cast(REBDEC, VAL_INT64(a));
                Init_Decimal(a, dec_a);
                goto compare;
            }
            else if (tb == TYPE_MONEY) {
                fail ("Money-to-int comparison not implemented in bootstrap");
            }
            break;

        case TYPE_DECIMAL:
        case TYPE_PERCENT:
            if (tb == TYPE_INTEGER) {
                REBDEC dec_b = cast(REBDEC, VAL_INT64(b));
                Init_Decimal(b, dec_b);
                goto compare;
            }
            else if (tb == TYPE_MONEY) {
                fail ("Numeric money comparisons not implemented in bootstrap");
            }
            else if (tb == TYPE_DECIMAL || tb == TYPE_PERCENT) // equivalent types
                goto compare;
            break;

        case TYPE_MONEY:
            if (tb == TYPE_INTEGER or tb == TYPE_DECIMAL or tb == TYPE_PERCENT)
                fail ("Numeric money comparisons not implemented in bootstrap");
            break;

        case TYPE_WORD:
        case TYPE_SET_WORD:
        case TYPE_GET_WORD:
        case TYPE_LIT_WORD:
        case TYPE_REFINEMENT:
        case TYPE_ISSUE:
            if (Any_Word(b)) goto compare;
            break;

        case TYPE_TEXT:
        case TYPE_FILE:
        case TYPE_EMAIL:
        case TYPE_URL:
        case TYPE_TAG:
            if (Any_String(b)) goto compare;
            break;
        }

        if (strictness == 0) return 0;

        fail (Error_Invalid_Compare_Raw(Datatype_Of(a), Datatype_Of(b)));
    }

    if (ta == TYPE_NULLED)
        return 1; // nulls always equal

  compare:;

    // At this point, both args are of the same datatype.
    COMPARE_HOOK hook = Compare_Hooks[Type_Of(a)];
    if (hook == nullptr)
        return 0; // !!! Is this correct (?)

    REBINT result = hook(a, b, strictness);
    if (result < 0)
        fail (Error_Invalid_Compare_Raw(Datatype_Of(a), Datatype_Of(b)));
    return result;
}


//
//  lax-equal?: native [
//
//  {TRUE if the values are approximately equal}
//
//      return: [logic!]
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
DECLARE_NATIVE(LAX_EQUAL_Q)
{
    INCLUDE_PARAMS_OF_LAX_EQUAL_Q;

    if (Compare_Modify_Values(ARG(VALUE1), ARG(VALUE2), 0))
        return LOGIC(true);

    return LOGIC(false);
}


//
//  lax-not-equal?: native [
//
//  {TRUE if the values are not approximately equal}
//
//      return: [logic!]
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
DECLARE_NATIVE(LAX_NOT_EQUAL_Q)
{
    INCLUDE_PARAMS_OF_LAX_NOT_EQUAL_Q;

    if (Compare_Modify_Values(ARG(VALUE1), ARG(VALUE2), 0))
        return LOGIC(false);

    return LOGIC(true);
}


//
//  equal?: native [
//
//  {TRUE if the values are strictly equal}
//
//      return: [logic!]
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
DECLARE_NATIVE(EQUAL_Q)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    if (Compare_Modify_Values(ARG(VALUE1), ARG(VALUE2), 1))
        return LOGIC(true);

    return LOGIC(false);
}


//
//  not-equal?: native [
//
//  {TRUE if the values are not strictly equal}
//
//      return: [logic!]
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
DECLARE_NATIVE(NOT_EQUAL_Q)
{
    INCLUDE_PARAMS_OF_NOT_EQUAL_Q;

    if (Compare_Modify_Values(ARG(VALUE1), ARG(VALUE2), 1))
        return LOGIC(false);

    return LOGIC(true);
}


//
//  same?: native [
//
//  {TRUE if the values are identical}
//
//      return: [logic!]
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
DECLARE_NATIVE(SAME_Q)
//
// This used to be "strictness mode 3" of Compare_Modify_Values.  However,
// folding SAME?-ness in required the comparisons to take REBVALs instead
// of just REBVALs, when only a limited number of types supported it.
// Rather than incur a cost for all comparisons, this handles the issue
// specially for those types which support it.
{
    INCLUDE_PARAMS_OF_SAME_Q;

    Value* value1 = ARG(VALUE1);
    Value* value2 = ARG(VALUE2);

    if (Type_Of(value1) != Type_Of(value2))
        return LOGIC(false); // can't be "same" value if not same type

    if (Is_Bitset(value1)) {
        //
        // BITSET! only has a series, no index.
        //
        if (Cell_Flex(value1) != Cell_Flex(value2))
            return LOGIC(false);
        return LOGIC(true);
    }

    if (Any_Series(value1)) {
        //
        // ANY-SERIES! can only be the same if pointers and indices match.
        //
        if (Cell_Flex(value1) != Cell_Flex(value2))
            return LOGIC(false);
        if (VAL_INDEX(value1) != VAL_INDEX(value2))
            return LOGIC(false);
        return LOGIC(true);
    }

    if (Any_Context(value1)) {
        //
        // ANY-CONTEXT! are the same if the varlists match.
        //
        if (Cell_Varlist(value1) != Cell_Varlist(value2))
            return LOGIC(false);
        return LOGIC(true);
    }

    if (Is_Map(value1)) {
        //
        // MAP! will be the same if the map pointer matches.
        //
        if (VAL_MAP(value1) != VAL_MAP(value2))
            return LOGIC(false);
        return LOGIC(true);
    }

    if (Any_Word(value1)) {
        //
        // ANY-WORD! must match in binding as well as be otherwise equal.
        //
        if (Cell_Word_Symbol(value1) != Cell_Word_Symbol(value2))
            return LOGIC(false);
        if (VAL_BINDING(value1) != VAL_BINDING(value2))
            return LOGIC(false);
        return LOGIC(true);
    }

    if (Is_Decimal(value1) || Is_Percent(value1)) {
        //
        // The tolerance on equal? for decimals is apparently not
        // a requirement of exactly the same bits.
        //
        if (
            memcmp(
                &VAL_DECIMAL(value1), &VAL_DECIMAL(value2), sizeof(REBDEC)
            ) == 0
        ){
            return LOGIC(true);
        }

        return LOGIC(false);
    }

    // For other types, just fall through to strict equality comparison
    //
    if (Compare_Modify_Values(value1, value2, 1))
        return LOGIC(true);

    return LOGIC(false);
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
DECLARE_NATIVE(LESSER_Q)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    if (Compare_Modify_Values(ARG(VALUE1), ARG(VALUE2), -1))
        return LOGIC(false);

    return LOGIC(true);
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
DECLARE_NATIVE(EQUAL_OR_LESSER_Q)
{
    INCLUDE_PARAMS_OF_EQUAL_OR_LESSER_Q;

    if (Compare_Modify_Values(ARG(VALUE1), ARG(VALUE2), -2))
        return LOGIC(false);

    return LOGIC(true);
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
DECLARE_NATIVE(GREATER_Q)
{
    INCLUDE_PARAMS_OF_GREATER_Q;

    if (Compare_Modify_Values(ARG(VALUE1), ARG(VALUE2), -2))
        return LOGIC(true);

    return LOGIC(false);
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
DECLARE_NATIVE(GREATER_OR_EQUAL_Q)
{
    INCLUDE_PARAMS_OF_GREATER_OR_EQUAL_Q;

    if (Compare_Modify_Values(ARG(VALUE1), ARG(VALUE2), -1))
        return LOGIC(true);

    return LOGIC(false);
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
DECLARE_NATIVE(MAXIMUM)
{
    INCLUDE_PARAMS_OF_MAXIMUM;

    const Value* value1 = ARG(VALUE1);
    const Value* value2 = ARG(VALUE2);

    if (Is_Pair(value1) || Is_Pair(value2)) {
        Min_Max_Pair(OUT, value1, value2, true);
    }
    else {
        DECLARE_VALUE (coerced1);
        Copy_Cell(coerced1, value1);
        DECLARE_VALUE (coerced2);
        Copy_Cell(coerced2, value2);

        if (Compare_Modify_Values(coerced1, coerced2, -1))
            Copy_Cell(OUT, value1);
        else
            Copy_Cell(OUT, value2);
    }
    return OUT;
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
DECLARE_NATIVE(MINIMUM)
{
    INCLUDE_PARAMS_OF_MINIMUM;

    const Value* value1 = ARG(VALUE1);
    const Value* value2 = ARG(VALUE2);

    if (Is_Pair(ARG(VALUE1)) || Is_Pair(ARG(VALUE2))) {
        Min_Max_Pair(OUT, ARG(VALUE1), ARG(VALUE2), false);
    }
    else {
        DECLARE_VALUE (coerced1);
        Copy_Cell(coerced1, value1);
        DECLARE_VALUE (coerced2);
        Copy_Cell(coerced2, value2);

        if (Compare_Modify_Values(coerced1, coerced2, -1))
            Copy_Cell(OUT, value2);
        else
            Copy_Cell(OUT, value1);
    }
    return OUT;
}


//
//  negative?: native [
//
//  "Returns TRUE if the number is negative."
//
//      number [any-number! time! pair!]
//  ]
//
DECLARE_NATIVE(NEGATIVE_Q)
{
    INCLUDE_PARAMS_OF_NEGATIVE_Q;

    DECLARE_VALUE (zero);
    Init_Zeroed_Hack(zero, Type_Of(ARG(NUMBER)));

    if (Compare_Modify_Values(ARG(NUMBER), zero, -1))
        return LOGIC(false);

    return LOGIC(true);
}


//
//  positive?: native [
//
//  "Returns TRUE if the value is positive."
//
//      number [any-number! time! pair!]
//  ]
//
DECLARE_NATIVE(POSITIVE_Q)
{
    INCLUDE_PARAMS_OF_POSITIVE_Q;

    DECLARE_VALUE (zero);
    Init_Zeroed_Hack(zero, Type_Of(ARG(NUMBER)));

    if (Compare_Modify_Values(ARG(NUMBER), zero, -2))
        return LOGIC(true);

    return LOGIC(false);
}


//
//  zero?: native [
//
//  {Returns TRUE if the value is zero (for its datatype).}
//
//      value
//  ]
//
DECLARE_NATIVE(ZERO_Q)
{
    INCLUDE_PARAMS_OF_ZERO_Q;

    enum Reb_Kind type = Type_Of(ARG(VALUE));

    if (type >= TYPE_INTEGER and type <= TYPE_TIME) {
        DECLARE_VALUE (zero);
        Init_Zeroed_Hack(zero, type);

        if (Compare_Modify_Values(ARG(VALUE), zero, 1))
            return LOGIC(true);
    }
    return LOGIC(false);
}
