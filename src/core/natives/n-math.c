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
//  negate: native:generic [
//
//  "Changes the sign of a number (see COMPLEMENT for inversion of sets)"
//
//      return: [any-number? pair! money! time!]
//      number [any-number? pair! money! time!]
//  ]
//
DECLARE_NATIVE(NEGATE)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(NEGATE));
}


//
//  add: native:generic [
//
//  "Returns the addition of two values"
//
//      return: [char? any-scalar? date!]
//      value1 [char? any-scalar? date!]
//      value2 [char? any-scalar? date!]
//  ]
//
DECLARE_NATIVE(ADD)
//
// 1. See comments on Is_NUL() about #{00} as a NUL? state for the CHAR? type
//    constraint.  We preserve (NUL + 65) -> #A and (#A - NUL) -> 0 partially
//    because they were in the tests, but also because it may find use in
//    generalized code.  But we don't dispatch to BLOB! or RUNE! to handle
//    SYM_ADD for this case, instead localizing it here so it's easier to
//    reason about or delete.
{
    INCLUDE_PARAMS_OF_ADD;

    Element* e1 = Element_ARG(VALUE1);
    Element* e2 = Element_ARG(VALUE2);

    if (Is_NUL(e1)) {  // localize NUL handling to SUBTRACT native [1]
        if (not Is_Integer(e2))
            return PANIC("Can only add INTEGER! to NUL #{00} state");
        REBINT i = VAL_INT32(e2);
        if (i < 0)
            return PANIC(Error_Codepoint_Negative_Raw());
        Option(Error*) error = Trap_Init_Char(OUT, i);
        if (error)
            return FAIL(unwrap error);
        return OUT;
    }

    if (Is_NUL(e2)) {  // localize NUL handling to SUBTRACT native [1]
        if (not Is_Integer(e1))
            return PANIC("Can only add INTEGER! to NUL #{00} state");
        REBINT i = VAL_INT32(e1);
        if (i < 0)
            return PANIC(Error_Codepoint_Negative_Raw());
        Option(Error*) error = Trap_Init_Char(OUT, i);
        if (error)
            return FAIL(unwrap error);
        return OUT;
    }

    return Run_Generic_Dispatch(e1, LEVEL, CANON(ADD));
}


//
//  subtract: native:generic [
//
//  "Returns the second value subtracted from the first"
//
//      return: [char? any-scalar? date! ]
//      value1 [char? any-scalar? date!]
//      value2 [char? any-scalar? date!]
//  ]
//
DECLARE_NATIVE(SUBTRACT)
//
// 1. Preservation of R3-Alpha's NUL math behaviors is narrow, isolated here
//    for easy review and/or removal.
{
    INCLUDE_PARAMS_OF_SUBTRACT;

    Element* e1 = Element_ARG(VALUE1);
    Element* e2 = Element_ARG(VALUE2);

    if (Is_NUL(e1)) {  // localize NUL handling to SUBTRACT native [1]
        if (Is_NUL(e2))
            return Init_Integer(OUT, 0);
        if (IS_CHAR(e2))
            return Init_Integer(OUT, cast(REBINT, 0) - Cell_Codepoint(e2));
        return FAIL(Error_Codepoint_Negative_Raw());
    }

    if (Is_NUL(e2)) {  // localize NUL handling to SUBTRACT native [1]
        if (IS_CHAR(e1))
            return Init_Integer(OUT, Cell_Codepoint(e1));
        return PANIC("Only CHAR? can have NUL? #{00} state subtracted");
    }

    return Run_Generic_Dispatch(e1, LEVEL, CANON(SUBTRACT));
}


//
//  multiply: native:generic [
//
//  "Returns the second value multiplied by the first"
//
//      return: [char? any-scalar? element?]
//      value1 [char? any-scalar? element?]  ; !!! expand types for DECI!
//      value2 [char? any-scalar? element?]
//  ]
//
DECLARE_NATIVE(MULTIPLY)
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
{
    INCLUDE_PARAMS_OF_MULTIPLY;

    Element* e1 = Element_ARG(VALUE1);
    Element* e2 = Element_ARG(VALUE2);

    if (
        not Heart_Of_Is_0(e1)  // left is not an extension type
        and cast(Byte, Heart_Of(e1)) < cast(Byte, Heart_Of(e2))  // [1]
    ){
        Element* spare = Move_Cell(SPARE, e2);
        Move_Cell(e2, e1);  // ...so move simpler type to be on the right
        Move_Cell(e1, spare);
    }

    return Dispatch_Generic(MULTIPLY, e1, LEVEL);
}


//
//  divide: native:generic [
//
//  "Returns the first value divided by the second"
//
//      return: [char? any-scalar?]
//      value1 [char? any-scalar?]
//      value2 [char? any-scalar?]
//  ]
//
DECLARE_NATIVE(DIVIDE)
{
    Element* e1 = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e1, LEVEL, CANON(DIVIDE));
}



//
//  remainder: native:generic [
//
//  "Returns the remainder of first value divided by second"
//
//      return: [char? any-scalar?]
//      value1 [char? any-scalar?]
//      value2 [char? any-scalar?]
//  ]
//
DECLARE_NATIVE(REMAINDER)
{
    Element* e1 = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e1, LEVEL, CANON(REMAINDER));
}


//
//  power: native:generic [
//
//  "Returns the first number raised to the second number"
//
//      return: [any-number?]
//      number [any-number?]
//      exponent [any-number?]
//  ]
//
DECLARE_NATIVE(POWER)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(POWER));
}


//
//  absolute: native:generic [
//
//  "Returns the absolute value"
//
//      return: [any-number? pair! money! time!]
//      value [any-number? pair! money! time!]
//  ]
//
DECLARE_NATIVE(ABSOLUTE)
{
    Element* e = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(e, LEVEL, CANON(ABSOLUTE));
}


//
//  round: native:generic [
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
DECLARE_NATIVE(ROUND)
{
    INCLUDE_PARAMS_OF_ROUND;

    USED(ARG(TO));  // passed through via LEVEL

    Count num_refinements = 0;
    if (Bool_ARG(EVEN))
        ++num_refinements;
    if (Bool_ARG(DOWN))
        ++num_refinements;
    if (Bool_ARG(HALF_DOWN))
        ++num_refinements;
    if (Bool_ARG(FLOOR))
        ++num_refinements;
    if (Bool_ARG(CEILING))
        ++num_refinements;
    if (Bool_ARG(HALF_CEILING))
        ++num_refinements;

    if (num_refinements > 1)
        return FAIL("ROUND only accepts one of EVEN, DOWN, HALF-DOWN,"
            " FLOOR, CEILING, or HALF-CEILING refinements");

    Element* elem = Element_ARG(VALUE);
    return Dispatch_Generic(ROUND, elem, LEVEL);
}


//
//  odd?: native:generic [
//
//  "Returns OKAY if the number is odd"
//
//      return: [logic?]
//      number [any-number? char? date! money! time! pair!]
//  ]
//
DECLARE_NATIVE(ODD_Q)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(ODD_Q));
}


//
//  even?: native:generic [
//
//  "Returns OKAY if the number is even"
//
//      return: [logic?]
//      number [any-number? char? date! money! time! pair!]
//  ]
//
DECLARE_NATIVE(EVEN_Q)
{
    Element* number = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(number, LEVEL, CANON(EVEN_Q));
}


//
//  randomize: native:generic [
//
//  "Seed random number generator"
//
//      return: []
//      seed "Pass e.g. NOW:TIME:PRECISE for nondeterminism"
//          [fundamental?]
//  ]
//
DECLARE_NATIVE(RANDOMIZE)
//
// Note: It may not be a great idea to allow randomization on lists, it
// may be the case that there's some kind of "randomize dialect" in which a
// a block specification could be meaningful.  If someone wants to use a
// block as a random seed they could randomize on the mold of it... but
// also we may want to expose the hash of a block for other reasons.
{
    Element* seed = cast(Element*, ARG_N(1));
    return Dispatch_Generic(RANDOMIZE, seed, LEVEL);
}


//
//  random: native:generic [
//
//  "Returns random value of the given type, 'zero' to max (see also SHUFFLE)"
//
//      return: [element?]
//      max "Maximum value of result (inclusive)"
//          [fundamental?]
//      :secure "Old refinement from R3-Alpha: Review"
//  ]
//
DECLARE_NATIVE(RANDOM)
//
// RANDOM may be a good candidate for a dialect, e.g.:
//
//     random [between 10 and 20 distribution 'normal]
//
// This application opens up now, since RANDOM-PICK is used to pick a random
// item out of a block, and SHUFFLE and SHUFFLE-OF give you shuffled lists.
{
    Element* max = cast(Element*, ARG_N(1));
    return Dispatch_Generic(RANDOM, max, LEVEL);
}


//
//  random-between: native:generic [
//
//  "Random value of the given type between min and max (inclusive)"
//
//      return: [element?]
//      min [fundamental?]
//      max [fundamental?]
//      :secure "Old refinement from R3-Alpha: Review"
//  ]
//
DECLARE_NATIVE(RANDOM_BETWEEN)
//
// !!! Should this function make sure the types are comparable, and that max
// is greater than min, before dispatching?  Probably not, that's exppensive.
{
    INCLUDE_PARAMS_OF_RANDOM_BETWEEN;

    Element* min = Element_ARG(MIN);
    Element* max = Element_ARG(MAX);
    USED(Bool_ARG(SECURE));  // passed through via LEVEL

    if (Type_Of(min) != Type_Of(max))
        return FAIL("RANDOM-BETWEEN requires MIN and MAX of same type");

    return Dispatch_Generic(RANDOM_BETWEEN, min, LEVEL);
}


//
//  random-pick: native:generic [
//
//  "Picks an arbitrary member out of a collection (see also SHUFFLE, RANDOM)"
//
//      return: "Error if collection empty (use TRY RANDOM-PICK to get NULL)"
//          [element? error!]
//      collection [fundamental?]
//      :secure "Old refinement from R3-Alpha: Review"
//  ]
//
DECLARE_NATIVE(RANDOM_PICK)
//
// While RANDOM_PICK is written as its own generic that can be optimized, for
// most types it can easily be implemented based on RANDOM + LENGTH_OF + PICK.
// The choice to have specialized implementations for ANY-LIST? and BLOB?
// and ANY-STRING? are mostly based on history.  However there was no code
// for RUNE!, and the details of cells that don't have nodes make it such
// that it makes more sense to avoid the pitfallls of reimplementing all that.
//
// It may be that the RANDOM_PICK specializations should be deleted where
// they are not necessary, to cut down on the total amount of code and
// potential for error.
{
    Element* collection = cast(Element*, ARG_N(1));

    Bounce bounce;
    if (Try_Dispatch_Generic(&bounce, RANDOM_PICK, collection, LEVEL))
        return bounce;

    const Value* datatype = Datatype_Of_Builtin_Fundamental(collection);
    if (
        not Handles_Generic(LENGTH_OF, datatype)
        or not Handles_Generic(PICK, datatype)
    ){
        return UNHANDLED;
    }

    Quotify(collection);
    return rebDelegate(
        CANON(PICK), collection, CANON(RANDOM), CANON(LENGTH_OF), collection
    );
}


//
//  shuffle: native:generic [
//
//  "Randomly shuffle the contents of a series in place (see also RANDOM)"
//
//      return: [element?]
//      series [fundamental?]
//      :secure "Old refinement from R3-Alpha: Review"
//  ]
//
DECLARE_NATIVE(SHUFFLE)
{
    Element* series = cast(Element*, ARG_N(1));
    return Dispatch_Generic(SHUFFLE, series, LEVEL);
}


//
//  shuffle-of: native:generic [
//
//  "Give back a shuffled copy of the argument (can be immutable)"
//
//      return: [element?]
//      element [fundamental?]
//      :secure "Returns a cryptographically secure random number"
//      :part "Limits to a given length or position"
//          [any-number? any-series?]
//  ]
//
DECLARE_NATIVE(SHUFFLE_OF)
{
    INCLUDE_PARAMS_OF_SHUFFLE_OF;

    Element* elem = cast(Element*, ARG(ELEMENT));
    USED(Bool_ARG(SECURE));  // other args get passed via LEVEL
    USED(Bool_ARG(PART));

    Bounce bounce;
    if (Try_Dispatch_Generic(&bounce, SHUFFLE_OF, elem, LEVEL))
        return bounce;

    const Value* datatype = Datatype_Of_Fundamental(elem);
    if (
        not Handles_Generic(SHUFFLE, datatype)
        or not Handles_Generic(COPY, datatype)
    ){
        return UNHANDLED;
    }

    Quotify(elem);
    return rebDelegate(CANON(SHUFFLE), CANON(COPY), elem);
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
    return SUCCESS;
}


//
//  cosine: native [
//
//  "Returns the trigonometric cosine"
//
//      return: [decimal!]
//      angle [any-number?]
//      :radians "ANGLE is specified in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(COSINE)
{
    INCLUDE_PARAMS_OF_COSINE;

    REBDEC dval = cos(Trig_Value(ARG(ANGLE), Bool_ARG(RADIANS), SYM_COSINE));
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
//      angle [any-number?]
//      :radians "ANGLE is specified in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(SINE)
{
    INCLUDE_PARAMS_OF_SINE;

    REBDEC dval = sin(Trig_Value(ARG(ANGLE), Bool_ARG(RADIANS), SYM_SINE));
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
//      angle [any-number?]
//      :radians "ANGLE is specified in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(TANGENT)
{
    INCLUDE_PARAMS_OF_TANGENT;

    REBDEC dval = Trig_Value(ARG(ANGLE), Bool_ARG(RADIANS), SYM_TANGENT);
    if (Eq_Decimal(fabs(dval), PI / 2.0))
        panic (Error_Overflow_Raw());

    return Init_Decimal(OUT, tan(dval));
}


//
//  arccosine: native [
//
//  "Returns the trigonometric arccosine"
//
//      return: [decimal!]
//      cosine [any-number?]
//      :radians "Returns result in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(ARCCOSINE)
{
    INCLUDE_PARAMS_OF_ARCCOSINE;

    Option(Error*) e = Trap_Arc_Trans(
        OUT, ARG(COSINE), Bool_ARG(RADIANS), SYM_COSINE
    );
    if (e)
        return PANIC(unwrap e);
    return OUT;
}


//
//  arcsine: native [
//
//  "Returns the trigonometric arcsine"
//
//      return: [decimal!]
//      sine [any-number?]
//      :radians "Returns result in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(ARCSINE)
{
    INCLUDE_PARAMS_OF_ARCSINE;

    Option(Error*) e = Trap_Arc_Trans(OUT, ARG(SINE), Bool_ARG(RADIANS), SYM_SINE);
    if (e)
        return PANIC(unwrap e);
    return OUT;
}


//
//  arctangent: native [
//
//  "Returns the trigonometric arctangent"
//
//      return: [decimal!]
//      tangent [any-number?]
//      :radians "Returns result in radians (in degrees by default)"
//  ]
//
DECLARE_NATIVE(ARCTANGENT)
{
    INCLUDE_PARAMS_OF_ARCTANGENT;

    Option(Error*) e = Trap_Arc_Trans(
        OUT, ARG(TANGENT), Bool_ARG(RADIANS), SYM_TANGENT
    );
    if (e)
        return PANIC(unwrap e);
    return OUT;
}


//
//  exp: native [
//
//  "Raises E (the base of natural logarithm) to the power specified"
//
//      return: [decimal!]
//      power [any-number?]
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
//  "Returns the base-10 logarithm"
//
//      return: [decimal!]
//      value [any-number?]
//  ]
//
DECLARE_NATIVE(LOG_10)
{
    INCLUDE_PARAMS_OF_LOG_10;

    REBDEC dval = AS_DECIMAL(ARG(VALUE));
    if (dval <= 0)
        panic (Error_Positive_Raw());

    return Init_Decimal(OUT, log10(dval));
}


//
//  log-2: native [
//
//  "Return the base-2 logarithm"
//
//      return: [decimal!]
//      value [any-number?]
//  ]
//
DECLARE_NATIVE(LOG_2)
{
    INCLUDE_PARAMS_OF_LOG_2;

    REBDEC dval = AS_DECIMAL(ARG(VALUE));
    if (dval <= 0)
        panic (Error_Positive_Raw());

    return Init_Decimal(OUT, log(dval) / LOG2);
}


//
//  log-e: native [
//
//  "Returns the natural (base-E) logarithm of the given value"
//
//      return: [decimal!]
//      value [any-number?]
//  ]
//
DECLARE_NATIVE(LOG_E)
{
    INCLUDE_PARAMS_OF_LOG_E;

    REBDEC dval = AS_DECIMAL(ARG(VALUE));
    if (dval <= 0)
        panic (Error_Positive_Raw());

    return Init_Decimal(OUT, log(dval));
}


//
//  square-root: native [
//
//  "Returns the square root of a number"
//
//      return: [decimal!]
//      value [any-number?]
//  ]
//
DECLARE_NATIVE(SQUARE_ROOT)
{
    INCLUDE_PARAMS_OF_SQUARE_ROOT;

    REBDEC dval = AS_DECIMAL(ARG(VALUE));
    if (dval < 0)
        panic (Error_Positive_Raw());

    return Init_Decimal(OUT, sqrt(dval));
}


//
//  vacant?: native [
//
//  "Tells you if default would overwrite a value (TRASH, NULL?, BLANK?)"
//
//      return: [logic?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(VACANT_Q)
//
// 1. Because TRASH! antiforms signify unspecialized function call slots,
//    they must be taken as ^META values if passed as an argument--even
//    though they are stable antiforms.
{
    INCLUDE_PARAMS_OF_VACANT_Q;

    Value* v = ARG(VALUE);  // meta
    Meta_Unquotify_Known_Stable(v);  // checked as ANY-VALUE?, so stable [1]
    return Init_Logic(OUT, Is_Trash(v) or Is_Nulled(v) or Is_Blank(v));
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
//  equal?: native:generic [
//
//  "TRUE if the values are equal"
//
//      return: [logic?]
//      ^value1 [any-value? void?]
//      ^value2 [any-value? void?]
//      :relax "Use less strict comparison rules (e.g. caseless comparison)"
//  ]
//
DECLARE_NATIVE(EQUAL_Q)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    Value* v1 = ARG(VALUE1);
    Value* v2 = ARG(VALUE2);
    bool relax = Bool_ARG(RELAX);

    if (Is_Meta_Of_Trash(v1)) {
        QUOTE_BYTE(v1) = ANTIFORM_0_COERCE_ONLY;
        return PANIC(PARAM(VALUE1));
    }

    if (Is_Meta_Of_Trash(v2)) {
        QUOTE_BYTE(v2) = ANTIFORM_0_COERCE_ONLY;
        return PANIC(PARAM(VALUE2));
    }

    if (QUOTE_BYTE(v1) != QUOTE_BYTE(v2))
        return nullptr;

    QUOTE_BYTE(v1) = NOQUOTE_1;  // should work for VOID equality, too
    QUOTE_BYTE(v2) = NOQUOTE_1;

    if (Sigil_Of(u_cast(Element*, v1)) != Sigil_Of(u_cast(Element*, v2)))
        return nullptr;

    Plainify(u_cast(Element*, v1));
    Plainify(u_cast(Element*, v2));

    if (Type_Of(v1) != Type_Of(v2)) {  // !!! need generic "coercibility"
        if (not relax)
            return nullptr;

        if (Is_Integer(v1) and Is_Decimal(v2))
            Init_Decimal(v1, cast(REBDEC, VAL_INT64(v1)));
        else if (Is_Decimal(v1) and Is_Integer(v2))
            Init_Decimal(v2, cast(REBDEC, VAL_INT64(v2)));
        else
            return nullptr;
    }

    return Dispatch_Generic(EQUAL_Q, v1, LEVEL);
}


//
//  lesser?: native:generic [
//
//  "TRUE if the first value is less than the second value"
//
//      return: [logic?]
//      value1 [fundamental?]  ; !!! Don't allow antiforms? [1]
//      value2 [fundamental?]
//  ]
//
DECLARE_NATIVE(LESSER_Q)
//
// 1. Although EQUAL? has to allow antiforms, e.g. for (value = null), it's
//    not clear that LESSER? should accept them.
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Value* v1 = ARG(VALUE1);
    Value* v2 = ARG(VALUE2);

    if (QUOTE_BYTE(v1) != QUOTE_BYTE(v2))
        return FAIL("Differing quote levels are not comparable");

    QUOTE_BYTE(v1) = NOQUOTE_1;
    QUOTE_BYTE(v2) = NOQUOTE_1;

    if (Type_Of(v1) != Type_Of(v2)) {  // !!! need generic "coercibility"
        if (Is_Integer(v1) and Is_Decimal(v2))
            Init_Decimal(v1, cast(REBDEC, VAL_INT64(v1)));
        else if (Is_Decimal(v1) and Is_Integer(v2))
            Init_Decimal(v2, cast(REBDEC, VAL_INT64(v2)));
        else
            return FAIL("Types are not comparable");
    }

    return Dispatch_Generic(LESSER_Q, v1, LEVEL);
}


// We want LESSER? to always give a soft failure through an error antiform, so
// that we can fall back on EQUAL?.  e.g.
//
//    >> [1 -> "a"] < [2 -> "b"]
//    == ~okay~  ; null
//
// Even though -> can't be compared with less than, the equality means
// we let the test go through.
//
IMPLEMENT_GENERIC(LESSER_Q, Any_Element)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    UNUSED(ARG(VALUE1));
    UNUSED(ARG(VALUE2));

    return FAIL("Types are not comparable");
}


//
//  same?: native [
//
//  "TRUE if the values are identical"
//
//      return: [logic?]
//      ^value1 [any-value? void?]  ; !!! antiforms okay? e.g. "same splice"?
//      ^value2 [any-value? void?]
//  ]
//
DECLARE_NATIVE(SAME_Q)
//
// !!! It's not clear that SAME? should be answering for types like INTEGER!
// or other immediates with the same answer as EQUAL?.  It might should be
// that SAME? only works on things that are references, like series and
// objects, and gives you an error antiform that you can TRY on to then fall
// back on equality if that is meaningful to your situation.
{
    INCLUDE_PARAMS_OF_SAME_Q;

    Value* v1 = ARG(VALUE1);
    Value* v2 = ARG(VALUE2);

    if (QUOTE_BYTE(v1) != QUOTE_BYTE(v2))
        return Init_Logic(OUT, false);  // not "same" value if not same quote

    if (HEART_BYTE(v1) != HEART_BYTE(v2))
        return Init_Logic(OUT, false);  // not "same" value if not same heart

    QUOTE_BYTE(v1) = NOQUOTE_1;  // trick works for VOID equality, too
    QUOTE_BYTE(v2) = NOQUOTE_1;

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

    Meta_Quotify(v1);  // may be null or other antiform :-/
    Meta_Quotify(v2);

    return rebDelegate(CANON(EQUAL_Q), v1, v2);
}


//
//  greater?: native [
//
//  "TRUE if the first value is greater than the second value"
//
//      return: [logic?]
//      value1 [fundamental?]
//      value2 [fundamental?]
//  ]
//
DECLARE_NATIVE(GREATER_Q)
{
    INCLUDE_PARAMS_OF_GREATER_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    Quotify(v1);
    Quotify(v2);

    return rebDelegate(
        "none [equal?", v1, v2, "lesser?", v1, v2, "]"
    );
}


//
//  equal-or-lesser?: native [
//
//  "TRUE if the first value is equal to or less than the second value"
//
//      return: [logic?]
//      value1 [fundamental?]
//      value2 [fundamental?]
//  ]
//
DECLARE_NATIVE(EQUAL_OR_LESSER_Q)
{
    INCLUDE_PARAMS_OF_EQUAL_OR_LESSER_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    Quotify(v1);
    Quotify(v2);

    return rebDelegate(
        "any [equal?", v1, v2, "lesser?", v1, v2, "]"
    );
}


//
//  greater-or-equal?: native [
//
//  "TRUE if the first value is greater than or equal to the second value"
//
//      return: [logic?]
//      value1 [fundamental?]
//      value2 [fundamental?]
//  ]
//
DECLARE_NATIVE(GREATER_OR_EQUAL_Q)
{
    INCLUDE_PARAMS_OF_GREATER_OR_EQUAL_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    Quotify(v1);
    Quotify(v2);

    return rebDelegate(
        "any [equal?", v1, v2, "not lesser?", v1, v2, "]"
    );
}


//
//  maximum: native [
//
//  "Returns the greater of the two values"
//
//      return: [any-scalar? date! any-series?]
//      value1 [any-scalar? date! any-series?]
//      value2 [any-scalar? date! any-series?]
//  ]
//
DECLARE_NATIVE(MAXIMUM)
{
    INCLUDE_PARAMS_OF_MAXIMUM;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    Quotify(v1);
    Quotify(v2);

    return rebDelegate(
        "either lesser?", v1, v2,
            v2,  // quoted, so acts as "soft quoted branch"
            v1
    );
}


//
//  minimum: native [
//
//  "Returns the lesser of the two values"
//
//      return: [any-scalar? date! any-series?]
//      value1 [any-scalar? date! any-series?]
//      value2 [any-scalar? date! any-series?]
//  ]
//
DECLARE_NATIVE(MINIMUM)
{
    INCLUDE_PARAMS_OF_MINIMUM;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    Quotify(v1);
    Quotify(v2);

    return rebDelegate(
        "either lesser?", v1, v2,
            v1,  // quoted, so acts as "soft quoted branch"
            v2
    );
}


//
//  zeroify: native:generic [
//
//  "Zeroed value of the same type and length (1.5 => 1.0, 1.2.3 => 0.0.0)"
//
//     return: [any-element?]
//     example [any-element?]
//  ]
//
DECLARE_NATIVE(ZEROIFY)
{
    INCLUDE_PARAMS_OF_ZEROIFY;

    Element* example = Element_ARG(EXAMPLE);

    return Dispatch_Generic(ZEROIFY, example, LEVEL);
}


//
//  negative?: native [
//
//  "Returns TRUE if the value is negative"
//
//      return: [logic?]
//      value [any-number? money! time! pair!]
//  ]
//
DECLARE_NATIVE(NEGATIVE_Q)
{
    INCLUDE_PARAMS_OF_NEGATIVE_Q;

    Element* v = Element_ARG(VALUE);

    Quotify(v);  // not necessary for scalars, but futureproof it
    return rebDelegate(CANON(LESSER_Q), v, CANON(ZEROIFY), v);
}


//
//  positive?: native [
//
//  "Returns TRUE if the value is positive"
//
//      return: [logic?]
//      value [any-number? money! time! pair!]
//  ]
//
DECLARE_NATIVE(POSITIVE_Q)
{
    INCLUDE_PARAMS_OF_POSITIVE_Q;

    Element* v = Element_ARG(VALUE);

    Quotify(v);  // not necessary for scalars, but futureproof it
    return rebDelegate(CANON(GREATER_Q), v, CANON(ZEROIFY), v);
}


//
//  zero?: native [
//
//  "Returns TRUE if the value is zero (for its datatype)"
//
//      return: [logic?]
//      value [any-scalar? pair! char?]
//  ]
//
DECLARE_NATIVE(ZERO_Q)
{
    INCLUDE_PARAMS_OF_ZERO_Q;

    Element* v = Element_ARG(VALUE);

    Quotify(v);  // not necessary for scalars, but futureproof it
    return rebDelegate(CANON(EQUAL_Q), v, CANON(ZEROIFY), v);
}
