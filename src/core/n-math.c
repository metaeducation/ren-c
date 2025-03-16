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
//  /negate: native:generic [
//
//  "Changes the sign of a number (see COMPLEMENT for inversion of sets)"
//
//      return: [any-number? pair! money! time!]
//      number [any-number? pair! money! time!]
//  ]
//
DECLARE_NATIVE(negate)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(NEGATE));
}


//
//  /add: native:generic [
//
//  "Returns the addition of two values"
//
//      return: [char? any-scalar? date!]
//      value1 [char? any-scalar? date!]
//      value2 [char? any-scalar? date!]
//  ]
//
DECLARE_NATIVE(add)
//
// 1. See comments on Is_NUL() about #{00} as a NUL? state for the CHAR? type
//    constraint.  We preserve (NUL + 65) -> #A and (#A - NUL) -> 0 partially
//    because they were in the tests, but also because it may find use in
//    generalized code.  But we don't dispatch to BLOB! or ISSUE! to handle
//    SYM_ADD for this case, instead localizing it here so it's easier to
//    reason about or delete.
{
    INCLUDE_PARAMS_OF_ADD;

    Element* e1 = Element_ARG(value1);
    Element* e2 = Element_ARG(value2);

    if (Is_NUL(e1)) {  // localize NUL handling to SUBTRACT native [1]
        if (not Is_Integer(e2))
            return FAIL("Can only add INTEGER! to NUL #{00} state");
        REBINT i = VAL_INT32(e2);
        if (i < 0)
            return FAIL(Error_Codepoint_Negative_Raw());
        Option(Error*) error = Trap_Init_Char(OUT, i);
        if (error)
            return RAISE(unwrap error);
        return OUT;
    }

    if (Is_NUL(e2)) {  // localize NUL handling to SUBTRACT native [1]
        if (not Is_Integer(e1))
            return FAIL("Can only add INTEGER! to NUL #{00} state");
        REBINT i = VAL_INT32(e1);
        if (i < 0)
            return FAIL(Error_Codepoint_Negative_Raw());
        Option(Error*) error = Trap_Init_Char(OUT, i);
        if (error)
            return RAISE(unwrap error);
        return OUT;
    }

    return Run_Generic_Dispatch(e1, LEVEL, CANON(ADD));
}


//
//  /subtract: native:generic [
//
//  "Returns the second value subtracted from the first"
//
//      return: [char? any-scalar? date! ]
//      value1 [char? any-scalar? date!]
//      value2 [char? any-scalar? date!]
//  ]
//
DECLARE_NATIVE(subtract)
//
// 1. Preservation of R3-Alpha's NUL math behaviors is narrow, isolated here
//    for easy review and/or removal.
{
    INCLUDE_PARAMS_OF_SUBTRACT;

    Element* e1 = Element_ARG(value1);
    Element* e2 = Element_ARG(value2);

    if (Is_NUL(e1)) {  // localize NUL handling to SUBTRACT native [1]
        if (Is_NUL(e2))
            return Init_Integer(OUT, 0);
        if (IS_CHAR(e2))
            return Init_Integer(OUT, cast(REBINT, 0) - Cell_Codepoint(e2));
        return RAISE(Error_Codepoint_Negative_Raw());
    }

    if (Is_NUL(e2)) {  // localize NUL handling to SUBTRACT native [1]
        if (IS_CHAR(e1))
            return Init_Integer(OUT, Cell_Codepoint(e1));
        return FAIL("Only CHAR? can have NUL? #{00} state subtracted");
    }

    return Run_Generic_Dispatch(e1, LEVEL, CANON(SUBTRACT));
}


//
//  /multiply: native:generic [
//
//  "Returns the second value multiplied by the first"
//
//      return: [char? any-scalar?]
//      value1 [char? any-scalar?]
//      value2 [char? any-scalar?]
//  ]
//
DECLARE_NATIVE(multiply)
//
// 1. Most languages want multiplication to be commutative (exceptions like
//    matrix multiplication do exist, though that likely should be a different
//    operation and reserve MULTIPLY for element-wise multiplication).  To
//    ensure commutativity, we swap the arguments if their heart bytes are
//    not in "canon order".
//
//    (Using the HEART_BYTE as the canon order is a bit of a hack, as the
//    table can be reordered.  But we try to order the types in %types.r
//    such that more complex types come later, so that we dispatch to the
//    more complex type...e.g. multiplying a PAIR! by a DECIMAL! should
//    should dispatch to the PAIR! code.)
//
// 2. Historical Redbol was very liberal about allowing you to perform a
//    multiplication with non-DECIMAL!, non-INTEGER!.  For the sake of sanity
//    it's being restricted.  MONEY! multiplication by MONEY! is an exception,
//    due to the fact that it's a way of doing numbers in the fixed point
//    math domain.
{
    INCLUDE_PARAMS_OF_MULTIPLY;

    Element* e1 = Element_ARG(value1);
    Element* e2 = Element_ARG(value2);

    if (HEART_BYTE(e1) < HEART_BYTE(e2)) {  // simpler type is on left [1]
        Move_Cell(stable_SPARE, e2);
        Move_Cell(e2, e1);  // ...so move simpler type to be on the right
        Move_Cell(e1, cast(Element*, SPARE));
    }

    if (
        (not Is_Integer(e2) and not Is_Decimal(e2))
        and not (Is_Money(e1) and Is_Money(e2))  // exception [2]
    ){
        return FAIL("Can only multiply by INTEGER! or DECIMAL!");  // [2]
    }

    return Dispatch_Generic(multiply, e1, LEVEL);
}


//
//  /divide: native:generic [
//
//  "Returns the first value divided by the second"
//
//      return: [char? any-scalar?]
//      value1 [char? any-scalar?]
//      value2 [char? any-scalar?]
//  ]
//
DECLARE_NATIVE(divide)
{
    Element* e1 = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e1, LEVEL, CANON(DIVIDE));
}



//
//  /remainder: native:generic [
//
//  "Returns the remainder of first value divided by second"
//
//      return: [char? any-scalar?]
//      value1 [char? any-scalar?]
//      value2 [char? any-scalar?]
//  ]
//
DECLARE_NATIVE(remainder)
{
    Element* e1 = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e1, LEVEL, CANON(REMAINDER));
}


//
//  /power: native:generic [
//
//  "Returns the first number raised to the second number"
//
//      return: [any-number?]
//      number [any-number?]
//      exponent [any-number?]
//  ]
//
DECLARE_NATIVE(power)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(POWER));
}


//
//  /absolute: native:generic [
//
//  "Returns the absolute value"
//
//      return: [any-number? pair! money! time!]
//      value [any-number? pair! money! time!]
//  ]
//
DECLARE_NATIVE(absolute)
{
    Element* e = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e, LEVEL, CANON(ABSOLUTE));
}


//
//  /round: native:generic [
//
//  "Returns the first number raised to the second number"
//
//      return: [any-number? pair! money! time!]
//      value [any-number? pair! money! time!]
//      :to "Return the nearest multiple of the parameter (must be non-zero)"
//          [any-number? money! time!]
//      :even "Halves round toward even results"
//      :down "Round toward zero, ignoring discarded digits. (truncate)"
//      :half-down "Halves round toward zero"
//      :floor "Round in negative direction"
//      :ceiling "Round in positive direction"
//      :half-ceiling "Halves round in positive direction"
//  ]
//
DECLARE_NATIVE(round)
{
    Element* e = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e, LEVEL, CANON(ROUND));
}


//
//  /odd?: native:generic [
//
//  "Returns OKAY if the number is odd"
//
//      return: [logic?]
//      number [any-number? char? date! money! time! pair!]
//  ]
//
DECLARE_NATIVE(odd_q)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(ODD_Q));
}


//
//  /even?: native:generic [
//
//  "Returns OKAY if the number is even"
//
//      return: [logic?]
//      number [any-number? char? date! money! time! pair!]
//  ]
//
DECLARE_NATIVE(even_q)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(EVEN_Q));
}


//
//  /random: native:generic [
//
//  "Rounds a numeric value; halves round up (away from zero) by default"
//
//      return: [~ element?]  ; !!! nothing if :SEED, should be RANDOMIZE?
//      value "Maximum value of result (modified when series)"
//      :seed "Restart or randomize"
//      :secure "Returns a cryptographically secure random number"
//      :only "Pick a random value from a series"  ; !!! consider SHUFFLE
//  ]
//
DECLARE_NATIVE(random)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(RANDOM));
}


//
//  Trig_Value: C
//
// Convert integer arg, if present, to decimal and convert to radians
// if necessary.  Clip ranges for correct REBOL behavior.
//
static REBDEC Trig_Value(
    const Value* value,
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
//  Trap_Arc_Trans: C
//
static Option(Error*) Trap_Arc_Trans(
    Sink(Value) out,
    const Value* value,
    bool radians,
    SymId which
){
    REBDEC dval = AS_DECIMAL(value);
    if (which != SYM_TANGENT and (dval < -1 or dval > 1))
        return Error_Overflow_Raw();

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
    return nullptr;
}


//
//  /cosine: native [
//
//  "Returns the trigonometric cosine"
//
//      return: [decimal!]
//      angle [any-number?]
//      :radians "ANGLE is specified in radians (in degrees by default)"
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
//  /sine: native [
//
//  "Returns the trigonometric sine"
//
//      return: [decimal!]
//      angle [any-number?]
//      :radians "ANGLE is specified in radians (in degrees by default)"
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
//  /tangent: native [
//
//  "Returns the trigonometric tangent"
//
//      return: [decimal!]
//      angle [any-number?]
//      :radians "ANGLE is specified in radians (in degrees by default)"
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
//  /arccosine: native [
//
//  "Returns the trigonometric arccosine"
//
//      return: [decimal!]
//      cosine [any-number?]
//      :radians "Returns result in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(arccosine)
{
    INCLUDE_PARAMS_OF_ARCCOSINE;

    Option(Error*) e = Trap_Arc_Trans(
        OUT, ARG(cosine), REF(radians), SYM_COSINE
    );
    if (e)
        return FAIL(unwrap e);
    return OUT;
}


//
//  /arcsine: native [
//
//  "Returns the trigonometric arcsine"
//
//      return: [decimal!]
//      sine [any-number?]
//      :radians "Returns result in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(arcsine)
{
    INCLUDE_PARAMS_OF_ARCSINE;

    Option(Error*) e = Trap_Arc_Trans(OUT, ARG(sine), REF(radians), SYM_SINE);
    if (e)
        return FAIL(unwrap e);
    return OUT;
}


//
//  /arctangent: native [
//
//  "Returns the trigonometric arctangent"
//
//      return: [decimal!]
//      tangent [any-number?]
//      :radians "Returns result in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(arctangent)
{
    INCLUDE_PARAMS_OF_ARCTANGENT;

    Option(Error*) e = Trap_Arc_Trans(
        OUT, ARG(tangent), REF(radians), SYM_TANGENT
    );
    if (e)
        return FAIL(unwrap e);
    return OUT;
}


//
//  /exp: native [
//
//  "Raises E (the base of natural logarithm) to the power specified"
//
//      return: [decimal!]
//      power [any-number?]
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
//  /log-10: native [
//
//  "Returns the base-10 logarithm"
//
//      return: [decimal!]
//      value [any-number?]
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
//  /log-2: native [
//
//  "Return the base-2 logarithm"
//
//      return: [decimal!]
//      value [any-number?]
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
//  /log-e: native [
//
//  "Returns the natural (base-E) logarithm of the given value"
//
//      return: [decimal!]
//      value [any-number?]
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
//  /square-root: native [
//
//  "Returns the square root of a number"
//
//      return: [decimal!]
//      value [any-number?]
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
//  /vacancy?: native [
//
//  "Tells you if the argument causes errors on WORD! access (and defaultable)"
//
//      return: [logic?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(vacancy_q)
//
// 1. Because BLANK! antiforms signify unspecialized function call slots,
//    they must be taken as ^META values if passed as an argument--even
//    though they are stable antiforms.
{
    INCLUDE_PARAMS_OF_VACANCY_Q;

    Value* v = ARG(value);  // meta
    Meta_Unquotify_Known_Stable(v);  // checked ANY-VALUE?, so stable [1]
    return Init_Logic(OUT, Any_Vacancy(v));
}


//
//  /defaultable?: native [
//
//  "Tells you if default would overwrite a value (VACANCY?, NULL?, VOID?)"
//
//      return: [logic?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(defaultable_q)
//
// 1. Because PARAMETER! antiforms signify unspecialized function call slots,
//    they must be taken as ^META values if passed as an argument--even
//    though they are stable antiforms.
{
    INCLUDE_PARAMS_OF_DEFAULTABLE_Q;

    Value* v = ARG(value);  // meta
    Meta_Unquotify_Known_Stable(v);  // checked as ANY-VALUE?, so stable [1]
    return Init_Logic(OUT, Any_Vacancy(v) or Is_Void(v) or Is_Nulled(v));
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  EQUAL? and LESSER?: BASIS FOR ALL COMPARISONS
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// The way things work in Ren-C are similar to Ord and Eq in Haskell, or how
// C++ standard library sorts solely in terms of operator< and operator==.
//
// So GREATER? is defined as just NOT LESSER? and NOT EQUAL?.
//
// LESSER? is more limited in Ren-C than in R3-Alpha or Red.  You can only
// compare like types, and you can only compare blocks that are element-wise
// comparable.
//
//     >> [1 "a"] < [1 "b"]
//     == ~okay~  ; anti
//
//     >> ["a" 1] < [1 "b"]
//     ** Error: Non-comparable types (e.g. "a" < 1 is nonsensical)
//
// Hence you cannot sort an arbitrary block by the default LESSER? comparator.
// If you want to impose order on non-comparable types, you must use a custom
// comparison function that knows how to compare them.
//


//
//  /equal?: native:generic [
//
//  "TRUE if the values are equal"
//
//      return: [logic?]
//      value1 [something?]
//      value2 [something?]
//      :strict "Use strict comparison rules"
//  ]
//
DECLARE_NATIVE(equal_q)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    Value* v1 = ARG(value1);
    Value* v2 = ARG(value2);
    bool strict = REF(strict);

    if (QUOTE_BYTE(v1) != QUOTE_BYTE(v2))
        return nullptr;

    QUOTE_BYTE(v1) = NOQUOTE_1;
    QUOTE_BYTE(v2) = NOQUOTE_1;

    if (VAL_TYPE(v1) != VAL_TYPE(v2)) {  // !!! need generic "coercibility"
        if (strict)
            return nullptr;

        if (Is_Integer(v1) and Is_Decimal(v2))
            Init_Decimal(v1, cast(REBDEC, VAL_INT64(v1)));
        else if (Is_Decimal(v1) and Is_Integer(v2))
            Init_Decimal(v2, cast(REBDEC, VAL_INT64(v2)));
        else
            return nullptr;
    }

    return Dispatch_Generic(equal_q, v1, LEVEL);
}


//
//  /lesser?: native:generic [
//
//  "TRUE if the first value is less than the second value"
//
//      return: [logic?]
//      value1 [element?]  ; !!! Don't allow antiforms? [1]
//      value2 [element?]
//  ]
//
DECLARE_NATIVE(lesser_q)
//
// 1. Although EQUAL? has to allow antiforms, e.g. for (value = null), it's
//    not clear that LESSER? should accept them.
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Value* v1 = ARG(value1);
    Value* v2 = ARG(value2);

    if (QUOTE_BYTE(v1) != QUOTE_BYTE(v2))
        return RAISE("Differing quote levels are not comparable");

    QUOTE_BYTE(v1) = NOQUOTE_1;
    QUOTE_BYTE(v2) = NOQUOTE_1;

    if (VAL_TYPE(v1) != VAL_TYPE(v2)) {  // !!! need generic "coercibility"
        if (Is_Integer(v1) and Is_Decimal(v2))
            Init_Decimal(v1, cast(REBDEC, VAL_INT64(v1)));
        else if (Is_Decimal(v1) and Is_Integer(v2))
            Init_Decimal(v2, cast(REBDEC, VAL_INT64(v2)));
        else
            return RAISE("Types are not comparable");
    }

    return Dispatch_Generic(lesser_q, v1, LEVEL);
}


// We want LESSER? to always give a soft failure through a raised error, so
// that we can fall back on EQUAL?.  e.g.
//
//    >> [1 _ "a"] < [2 _ "b"]
//    == ~okay~  ; null
//
// Even though BLANK! can't be compared with less than, the equality means
// we let the test go through.
//
IMPLEMENT_GENERIC(lesser_q, any_element)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    UNUSED(ARG(value1));
    UNUSED(ARG(value2));

    return RAISE("Types are not comparable");
}


//
//  /same?: native [
//
//  "TRUE if the values are identical"
//
//      return: [logic?]
//      value1 [something?]  ; !!! antiforms okay? e.g. "same splice"?
//      value2 [something?]
//  ]
//
DECLARE_NATIVE(same_q)
//
// !!! It's not clear that SAME? should be answering for types like INTEGER!
// or other immediates with the same answer as EQUAL?.  It might should be
// that SAME? only works on things that are references, like series and
// objects, and gives you a raised error that you can TRY on to then fall
// back on equality if that is meaningful to your situation.
{
    INCLUDE_PARAMS_OF_SAME_Q;

    Value* v1 = ARG(value1);
    Value* v2 = ARG(value2);

    if (VAL_TYPE(v1) != VAL_TYPE(v2))
        return Init_Logic(OUT, false);  // not "same" value if not same type

    if (Is_Bitset(v1))  // same if binaries are same
        return Init_Logic(OUT, VAL_BITSET(v1) == VAL_BITSET(v2));

    if (Any_Series(v1))  // pointers -and- indices must match
        return Init_Logic(
            OUT,
            Cell_Flex(v1) == Cell_Flex(v2)
                and VAL_INDEX_RAW(v1) == VAL_INDEX_RAW(v2)  // permissive
        );

    if (Any_Context(v1))  // same if varlists match
        return Init_Logic(OUT, Cell_Varlist(v1) == Cell_Varlist(v2));

    if (Is_Map(v1))  // same if map pointer matches
        return Init_Logic(OUT, VAL_MAP(v1) == VAL_MAP(v2));

    if (Any_Word(v1))  // !!! "same" was spelling -and- binding in R3-Alpha
        return Init_Logic(
            OUT,
            Cell_Word_Symbol(v1) == Cell_Word_Symbol(v2)
                and Cell_Binding(v1) == Cell_Binding(v2)
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

    Meta_Quotify(v1);  // may be null or other antiform :-/
    Meta_Quotify(v2);

    return rebDelegate("strict-equal?", v1, v2);
}


//
//  /greater?: native [
//
//  "TRUE if the first value is greater than the second value"
//
//      return: [logic?]
//      value1 [something?]
//      value2 [something?]
//  ]
//
DECLARE_NATIVE(greater_q)
{
    INCLUDE_PARAMS_OF_GREATER_Q;

    Value* v1 = ARG(value1);
    Value* v2 = ARG(value2);

    Quotify(v1, 1);
    Quotify(v2, 1);

    return rebDelegate(
        "not any [equal?:strict", v1, v2, "lesser?", v1, v2, "]"
    );
}


//
//  /equal-or-lesser?: native [
//
//  "TRUE if the first value is equal to or less than the second value"
//
//      return: [logic?]
//      value1 [something?]
//      value2 [something?]
//  ]
//
DECLARE_NATIVE(equal_or_lesser_q)
{
    INCLUDE_PARAMS_OF_EQUAL_OR_LESSER_Q;

    Value* v1 = ARG(value1);
    Value* v2 = ARG(value2);

    Quotify(v1, 1);
    Quotify(v2, 1);

    return rebDelegate(
        "any [equal?:strict", v1, v2, "lesser?", v1, v2, "]"
    );
}


//
//  /greater-or-equal?: native [
//
//  "TRUE if the first value is greater than or equal to the second value"
//
//      return: [logic?]
//      value1 [something?]
//      value2 [something?]
//  ]
//
DECLARE_NATIVE(greater_or_equal_q)
{
    INCLUDE_PARAMS_OF_GREATER_OR_EQUAL_Q;

    Value* v1 = ARG(value1);
    Value* v2 = ARG(value2);

    Quotify(v1, 1);
    Quotify(v2, 1);

    return rebDelegate(
        "any [equal?:strict", v1, v2, "not lesser?", v1, v2, "]"
    );
}


//
//  /maximum: native [
//
//  "Returns the greater of the two values"
//
//      return: [any-scalar? date! any-series?]
//      value1 [any-scalar? date! any-series?]
//      value2 [any-scalar? date! any-series?]
//  ]
//
DECLARE_NATIVE(maximum)
{
    INCLUDE_PARAMS_OF_MAXIMUM;

    Value* v1 = ARG(value1);
    Value* v2 = ARG(value2);

    Quotify(v1, 1);
    Quotify(v2, 1);

    return rebDelegate(
        "either lesser?", v1, v2,
            v2,  // quoted, so acts as "soft quoted branch"
            v1
    );
}


//
//  /minimum: native [
//
//  "Returns the lesser of the two values"
//
//      return: [any-scalar? date! any-series?]
//      value1 [any-scalar? date! any-series?]
//      value2 [any-scalar? date! any-series?]
//  ]
//
DECLARE_NATIVE(minimum)
{
    INCLUDE_PARAMS_OF_MINIMUM;

    Value* v1 = ARG(value1);
    Value* v2 = ARG(value2);

    Quotify(v1, 1);
    Quotify(v2, 1);

    return rebDelegate(
        "either lesser?", v1, v2,
            v1,  // quoted, so acts as "soft quoted branch"
            v2
    );
}


//
//  /zeroify: native:generic [
//
//  "Zeroed value of the same type and length (1.5 => 1.0, 1.2.3 => 0.0.0)"
//
//     return: [any-element?]
//     example [any-element?]
//  ]
//
DECLARE_NATIVE(zeroify)
{
    INCLUDE_PARAMS_OF_ZEROIFY;

    Element* example = Element_ARG(example);

    return Dispatch_Generic(zeroify, example, LEVEL);
}


//
//  /negative?: native [
//
//  "Returns TRUE if the value is negative"
//
//      return: [logic?]
//      value [any-number? money! time! pair!]
//  ]
//
DECLARE_NATIVE(negative_q)
{
    INCLUDE_PARAMS_OF_NEGATIVE_Q;

    Value* v = ARG(value);
    Quotify(v, 1);  // not necessary for scalars, but futureproof it

    return rebDelegate(CANON(LESSER_Q), v, CANON(ZEROIFY), v);
}


//
//  /positive?: native [
//
//  "Returns TRUE if the value is positive"
//
//      return: [logic?]
//      value [any-number? money! time! pair!]
//  ]
//
DECLARE_NATIVE(positive_q)
{
    INCLUDE_PARAMS_OF_POSITIVE_Q;

    Value* v = ARG(value);
    Quotify(v, 1);  // not necessary for scalars, but futureproof it

    return rebDelegate(CANON(GREATER_Q), v, CANON(ZEROIFY), v);
}


//
//  /zero?: native [
//
//  "Returns TRUE if the value is zero (for its datatype)"
//
//      return: [logic?]
//      value [any-scalar? pair! char?]
//  ]
//
DECLARE_NATIVE(zero_q)
{
    INCLUDE_PARAMS_OF_ZERO_Q;

    Value* v = ARG(value);
    Quotify(v, 1);  // not necessary for scalars, but futureproof it

    return rebDelegate(CANON(EQUAL_Q), v, CANON(ZEROIFY), v);
}
