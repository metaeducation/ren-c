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

#include "sys-core.h"
#include "sys-deci-funcs.h"


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
//  did?: native [
//
//  "Clamps a value to LOGIC! (e.g. a synonym for NOT? NOT? or TO-LOGIC)"
//
//      return: [logic!]
//          "Only LOGIC!'s FALSE and BLANK! for value return FALSE"
//      value [any-value!]
//  ]
//
REBNATIVE(did_q)
{
    INCLUDE_PARAMS_OF_DID_Q;

    return Init_Logic(D_OUT, IS_TRUTHY(ARG(value)));
}


//
//  did: native/body [
//
//  "Variant of TO-LOGIC which considers null values to also be false"
//
//      return: [logic!]
//          {true if value is NOT a LOGIC! false, BLANK!, or null}
//      optional [<opt> any-value!]
//  ][
//      not not :optional
//  ]
//
REBNATIVE(did)
{
    INCLUDE_PARAMS_OF_DID;

    return Init_Logic(D_OUT, IS_TRUTHY(ARG(optional)));
}


//
//  not?: native [
//
//  "Returns the logic complement."
//
//      return: [logic!]
//          "Only LOGIC!'s FALSE and BLANK! for value return TRUE"
//      value [any-value!]
//  ]
//
REBNATIVE(not_q)
{
    INCLUDE_PARAMS_OF_NOT_Q;

    return Init_Logic(D_OUT, IS_FALSEY(ARG(value)));
}


//
//  not: native [
//
//  "Returns the logic complement, considering voids to be false."
//
//      return: [logic!]
//          "Only LOGIC!'s FALSE, BLANK!, and void for cell return TRUE"
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(not)
{
    INCLUDE_PARAMS_OF_NOT;

    return Init_Logic(D_OUT, IS_FALSEY(ARG(optional)));
}


//
//  or?: native [
//
//  {Returns true if either value is conditionally true (no "short-circuit")}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(or_q)
{
    INCLUDE_PARAMS_OF_OR_Q;

    return Init_Logic(
        D_OUT,
        IS_TRUTHY(ARG(value1)) or IS_TRUTHY(ARG(value2))
    );
}


//
//  xor?: native [
//
//  {Returns true if only one of the two values is conditionally true.}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(xor_q)
{
    INCLUDE_PARAMS_OF_XOR_Q;

    // Note: no boolean ^^ in C; check unequal
    //
    return Init_Logic(
        D_OUT,
        IS_TRUTHY(ARG(value1)) != IS_TRUTHY(ARG(value2))
    );
}


//
//  and: enfix native [
//
//  {Boolean AND, with short-circuit mode if right hand side is BLOCK!}
//
//      return: "Conditionally true or false value (not coerced to LOGIC!)"
//          [<opt> any-value!]
//      left "Expression which will always be evaluated"
//          [<opt> any-value!]
//      :right "If BLOCK!, evaluated only if TO LOGIC! of LEFT is true"
//          [block! group!]
//  ]
//
REBNATIVE(and)
{
    INCLUDE_PARAMS_OF_AND;

    Value* left = ARG(left);
    Value* right = ARG(right);

    if (IS_BLOCK(left) and GET_VAL_FLAG(left, VALUE_FLAG_UNEVALUATED))
        fail ("left hand side of AND should not be literal block");

    if (IS_FALSEY(left)) {
        if (IS_GROUP(right)) { // no need to evaluate right if BLOCK!
            if (Do_Any_Array_At_Throws(D_OUT, right))
                return R_THROWN;
        }
        RETURN (left); // preserve falsey value
    }

    if (Do_Any_Array_At_Throws(D_OUT, right))
        return R_THROWN;

    return D_OUT; // preserve the exact truthy or falsey value
}


//  or: enfix native [
//
//  {Boolean OR, with short-circuit mode if right hand side is BLOCK!}
//
//      return: "Conditionally true or false value (not coerced to LOGIC!)"
//          [<opt> any-value!]
//      left "Expression which will always be evaluated"
//          [<opt> any-value!]
//      :right "If BLOCK!, evaluated only if TO LOGIC! of LEFT is false"
//          [block! group!]
//  ]
REBNATIVE(or)
{
    INCLUDE_PARAMS_OF_OR;

    Value* left = ARG(left);
    Value* right = ARG(right);

    if (IS_BLOCK(left) and GET_VAL_FLAG(left, VALUE_FLAG_UNEVALUATED))
        fail ("left hand side of OR should not be literal block");

    if (IS_TRUTHY(left)) {
        if (IS_GROUP(right)) { // no need to evaluate right if BLOCK!
            if (Do_Any_Array_At_Throws(D_OUT, right))
                return R_THROWN;
        }
        RETURN (left);
    }

    if (Do_Any_Array_At_Throws(D_OUT, right))
        return R_THROWN;

    return D_OUT; // preserve the exact truthy or falsey value
}


//
//  xor: enfix native [
//
//  {Boolean XOR}
//
//      return: "Conditionally true value, or LOGIC! false for failure case"
//          [<opt> any-value!]
//      left "Expression which will always be evaluated"
//          [<opt> any-value!]
//      :right "Expression that's also always evaluated (can't short circuit)"
//          [group!]
//  ]
//
REBNATIVE(xor)
{
    INCLUDE_PARAMS_OF_XOR;

    Value* left = ARG(left);

    if (IS_BLOCK(left) and GET_VAL_FLAG(left, VALUE_FLAG_UNEVALUATED))
        fail ("left hand side of XOR should not be literal block");

    if (Do_Any_Array_At_Throws(D_OUT, ARG(right))) // always evaluated
        return R_THROWN;

    Value* right = D_OUT;

    if (IS_FALSEY(left)) {
        if (IS_FALSEY(right))
            return Init_False(D_OUT); // default to logic false if both false

        return right;
    }

    if (IS_TRUTHY(right))
        return Init_False(D_OUT); // default to logic false if both true

    RETURN (left);
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
//          [<opt> any-value!] ;-- not a literal GROUP! as with XOR
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
REBINT CT_Logic(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode >= 0)  return (VAL_LOGIC(a) == VAL_LOGIC(b));
    return -1;
}


//
//  MAKE_Logic: C
//
REB_R MAKE_Logic(Value* out, enum Reb_Kind kind, const Value* arg) {
    assert(kind == REB_LOGIC);
    UNUSED(kind);

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
REB_R TO_Logic(Value* out, enum Reb_Kind kind, const Value* arg) {
    assert(kind == REB_LOGIC);
    UNUSED(kind);

    // As a "Rebol conversion", TO falls in line with the rest of the
    // interpreter canon that all non-none non-logic-false values are
    // considered effectively "truth".
    //
    return Init_Logic(out, IS_TRUTHY(arg));
}


static inline bool Math_Arg_For_Logic(Value* arg)
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
void MF_Logic(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form); // currently no distinction between MOLD and FORM

    Emit(mo, "+N", VAL_LOGIC(v) ? Canon(SYM_TRUE) : Canon(SYM_FALSE));
}


//
//  REBTYPE: C
//
REBTYPE(Logic)
{
    bool b1 = VAL_LOGIC(D_ARG(1));
    bool b2;

    switch (VAL_WORD_SYM(verb)) {

    case SYM_INTERSECT:
        b2 = Math_Arg_For_Logic(D_ARG(2));
        return Init_Logic(D_OUT, b1 and b2);

    case SYM_UNION:
        b2 = Math_Arg_For_Logic(D_ARG(2));
        return Init_Logic(D_OUT, b1 or b2);

    case SYM_DIFFERENCE:
        b2 = Math_Arg_For_Logic(D_ARG(2));
        return Init_Logic(D_OUT, b1 != b2);

    case SYM_COMPLEMENT:
        return Init_Logic(D_OUT, not b1);

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            // random/seed false restarts; true randomizes
            Set_Random(b1 ? cast(REBINT, OS_DELTA_TIME(0)) : 1);
            return nullptr;
        }
        if (Random_Int(REF(secure)) & 1)
            return Init_True(D_OUT);
        return Init_False(D_OUT); }

    default:
        fail (Error_Illegal_Action(REB_LOGIC, verb));
    }
}
