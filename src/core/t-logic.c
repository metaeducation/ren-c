//
//  File: %t-logic.c
//  Summary: "logic datatype"
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

#include "datatypes/sys-money.h" // !!! For conversions (good dependency?)

//
//  and?: native [
//
//  {Returns true if both values are conditionally true (no "short-circuit")}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(and_q)
{
    INCLUDE_PARAMS_OF_AND_Q;

    if (IS_TRUTHY(ARG(value1)) && IS_TRUTHY(ARG(value2)))
        return Init_True(D_OUT);

    return Init_False(D_OUT);
}


//
//  nor?: native [
//
//  {Returns true if both values are conditionally false (no "short-circuit")}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(nor_q)
{
    INCLUDE_PARAMS_OF_NOR_Q;

    if (IS_FALSEY(ARG(value1)) && IS_FALSEY(ARG(value2)))
        return Init_True(D_OUT);

    return Init_False(D_OUT);
}


//
//  nand?: native [
//
//  {Returns false if both values are conditionally true (no "short-circuit")}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(nand_q)
{
    INCLUDE_PARAMS_OF_NAND_Q;

    return Init_Logic(
        D_OUT,
        IS_TRUTHY(ARG(value1)) and IS_TRUTHY(ARG(value2))
    );
}


//
//  did: native/body [
//
//  "Synonym for TO-LOGIC"
//
//      return: "true if value is NOT a LOGIC! false, BLANK!, or NULL"
//          [logic!]
//      optional [<opt> any-value!]
//  ][
//      not not :optional
//  ]
//
REBNATIVE(_did_)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF__DID_;

    return Init_Logic(D_OUT, IS_TRUTHY(ARG(optional)));
}


//
//  not: native [
//
//  "Returns the logic complement."
//
//      return: "Only LOGIC!'s FALSE, BLANK!, and NULL return TRUE"
//          [logic!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(_not_)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF__NOT_;

    return Init_Logic(D_OUT, IS_FALSEY(ARG(optional)));
}


// At the moment, it is permitted to use plain words or paths on the right
// hand side of an OR, XOR, AND.  This was for a time not allowed and you had
// to use @word or @pa/th.  However that datatype has (temporarily) been
// removed to make way for the more critical ^word and ^path.  Because of this
// the idea of going ahead and allowing the plain words was done.
//
inline static bool Do_Logic_Right_Side_Throws(
    REBVAL *out,
    REBVAL *right,  // Note: mutates type!
    const REBVAL *left
){
    if (IS_GROUP(right) or IS_GET_GROUP(right))  { // don't double execute
        mutable_KIND3Q_BYTE(right) = mutable_HEART_BYTE(right) = REB_BLOCK;

        if (Do_Branch_With_Throws(out, right, left))
            return true;
    }
    else {
        if (IS_WORD(right))
            Get_Word_May_Fail(out, right, SPECIFIED);
        else {
            assert (IS_PATH(right));
            if (Get_Path_Throws_Core(out, right, SPECIFIED))
                return true;
        }

        if (IS_ACTION(out))
            fail ("words/paths can't be ACTION! as right hand of OR, AND, XOR");
    }

    return false;
}


//
//  and: enfix native [
//
//  {Boolean AND, right hand side must be in GROUP! to allow short-circuit}
//
//      return: [logic!]
//      left [<opt> any-value!]
//      'right "Right is evaluated if left is true, or if GET-GROUP!"
//          [group! get-group! path! word!]
//  ]
//
REBNATIVE(_and_)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF__AND_;

    REBVAL *left = ARG(left);
    REBVAL *right = ARG(right);

    if (GET_CELL_FLAG(left, UNEVALUATED))
        if (IS_BLOCK(left) or ANY_META_KIND(VAL_TYPE(left)))
            fail (Error_Unintended_Literal_Raw(left));

    if (IS_FALSEY(left)) {
        if (IS_GET_GROUP(right)) {  // have to evaluate GET-GROUP! either way
            if (Do_Any_Array_At_Throws(D_SPARE, right, SPECIFIED))
                return R_THROWN;
        }
        return Init_False(D_OUT);
    }

    if (Do_Logic_Right_Side_Throws(D_OUT, right, left))
        return R_THROWN;

    return Init_Logic(D_OUT, IS_TRUTHY(D_OUT));
}


//  or: enfix native [
//
//  {Boolean OR, right hand side must be in GROUP! to allow short-circuit}
//
//      return: [logic!]
//      left [<opt> any-value!]
//      'right "Right is evaluated if left is false, or if GET-GROUP!"
//          [group! get-group! path! word!]
//  ]
//
REBNATIVE(_or_)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF__OR_;

    REBVAL *left = ARG(left);
    REBVAL *right = ARG(right);

    if (GET_CELL_FLAG(left, UNEVALUATED))
        if (IS_BLOCK(left) or ANY_META_KIND(VAL_TYPE(left)))
            fail (Error_Unintended_Literal_Raw(left));

    if (IS_TRUTHY(left)) {
        if (IS_GET_GROUP(right)) {  // have to evaluate GET-GROUP! either way
            if (Do_Any_Array_At_Throws(D_SPARE, right, SPECIFIED))
                return R_THROWN;
        }
        return Init_True(D_OUT);
    }

    if (Do_Logic_Right_Side_Throws(D_OUT, right, left))
        return R_THROWN;

    return Init_Logic(D_OUT, IS_TRUTHY(D_OUT));
}


//
//  xor: enfix native [
//
//  {Boolean XOR (operation cannot be short-circuited)}
//
//      return: [logic!]
//      left [<opt> any-value!]
//      'right "Always evaluated, but is a GROUP! for consistency with AND/OR"
//          [group! get-group! path! word!]
//  ]
//
REBNATIVE(_xor_)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF__XOR_;

    REBVAL *left = ARG(left);
    REBVAL *right = ARG(right);

    if (GET_CELL_FLAG(left, UNEVALUATED))
        if (IS_BLOCK(left) or ANY_META_KIND(VAL_TYPE(left)))
            fail (Error_Unintended_Literal_Raw(left));

    if (Do_Logic_Right_Side_Throws(D_OUT, right, left))
        return R_THROWN;

    if (IS_FALSEY(left))
        return Init_Logic(D_OUT, IS_TRUTHY(D_OUT));

    return Init_Logic(D_OUT, IS_FALSEY(D_OUT));
}


//
//  unless: enfix native [
//
//  {Variant of non-short-circuit OR which favors the right-hand side result}
//
//      return: "Conditionally true or false value (not coerced to LOGIC!)"
//          [<opt> any-value!]
//      left "Expression which will always be evaluated"
//          [<opt> any-value!]
//      right "Expression that's also always evaluated (can't short circuit)"
//          [<opt> any-value!]  ; not a literal GROUP! as with XOR
//  ]
//
REBNATIVE(unless)
//
// Though this routine is similar to XOR, it is different enough in usage and
// looks from AND/OR/XOR to warrant not needing XOR's protection (e.g. forcing
// a GROUP! on the right hand side, prohibiting literal blocks on left)
{
    INCLUDE_PARAMS_OF_UNLESS;

    if (IS_TRUTHY(ARG(right)))
        RETURN (ARG(right));

    RETURN (ARG(left)); // preserve the exact truthy or falsey value
}


//
//  CT_Logic: C
//
REBINT CT_Logic(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);

    if (VAL_LOGIC(a) == VAL_LOGIC(b))
        return 0;
    return VAL_LOGIC(a) ? 1 : -1;  // only one is true
}


//
//  MAKE_Logic: C
//
REB_R MAKE_Logic(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_LOGIC);
    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    // As a construction routine, MAKE takes more liberties in the
    // meaning of its parameters, so it lets zero values be false.
    //
    // !!! Is there a better idea for MAKE that does not hinge on the
    // "zero is false" concept?  Is there a reason it should?
    //
    if (
        IS_FALSEY(arg)
        || (IS_INTEGER(arg) && VAL_INT64(arg) == 0)
        || (
            (IS_DECIMAL(arg) || IS_PERCENT(arg))
            && (VAL_DECIMAL(arg) == 0.0)
        )
        || (IS_MONEY(arg) && deci_is_zero(VAL_MONEY_AMOUNT(arg)))
    ){
        return Init_False(out);
    }

    return Init_True(out);
}


//
//  TO_Logic: C
//
REB_R TO_Logic(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    assert(kind == REB_LOGIC);
    UNUSED(kind);

    // As a "Rebol conversion", TO falls in line with the rest of the
    // interpreter canon that all non-none non-logic-false values are
    // considered effectively "truth".
    //
    return Init_Logic(out, IS_TRUTHY(arg));
}


static inline bool Math_Arg_For_Logic(REBVAL *arg)
{
    if (IS_LOGIC(arg))
        return VAL_LOGIC(arg);

    if (IS_BLANK(arg))
        return false;

    fail (Error_Unexpected_Type(REB_LOGIC, VAL_TYPE(arg)));
}


//
//  MF_Logic: C
//
void MF_Logic(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    if (not form)
        Append_Ascii(mo->series, "#[");

    if (VAL_LOGIC(v))
        Append_Spelling(mo->series, Canon(SYM_TRUE));
    else
        Append_Spelling(mo->series, Canon(SYM_FALSE));

    if (not form)
        Append_Ascii(mo->series, "]");
}


//
//  REBTYPE: C
//
REBTYPE(Logic)
{
    bool b1 = VAL_LOGIC(D_ARG(1));
    bool b2;

    switch (VAL_WORD_ID(verb)) {

    case SYM_BITWISE_AND:
        b2 = Math_Arg_For_Logic(D_ARG(2));
        return Init_Logic(D_OUT, b1 and b2);

    case SYM_BITWISE_OR:
        b2 = Math_Arg_For_Logic(D_ARG(2));
        return Init_Logic(D_OUT, b1 or b2);

    case SYM_BITWISE_XOR:
        b2 = Math_Arg_For_Logic(D_ARG(2));
        return Init_Logic(D_OUT, b1 != b2);

    case SYM_BITWISE_AND_NOT:
        b2 = Math_Arg_For_Logic(D_ARG(2));
        return Init_Logic(D_OUT, b1 and not b2);

    case SYM_BITWISE_NOT:
        return Init_Logic(D_OUT, not b1);

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            //
            // !!! For some reason, a random LOGIC! used OS_DELTA_TIME, while
            // it wasn't used elsewhere:
            //
            //     /* random/seed false restarts; true randomizes */
            //     Set_Random(b1 ? cast(REBINT, OS_DELTA_TIME(0)) : 1);
            //
            // This created a dependency on the host's model for time, which
            // the core is trying to be agnostic about.  This one appearance
            // for getting a random LOGIC! was a non-sequitur which was in
            // the way of moving time to an extension, so it was removed.
            //
            fail ("LOGIC! random seed currently not implemented");
        }

        if (Random_Int(did REF(secure)) & 1)
            return Init_True(D_OUT);
        return Init_False(D_OUT); }

    default:
        break;
    }

    return R_UNHANDLED;
}
