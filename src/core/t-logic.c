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

    if (IS_CONDITIONAL_TRUE(ARG(value1)) && IS_CONDITIONAL_TRUE(ARG(value2)))
        return R_TRUE;

    return R_FALSE;
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

    if (IS_CONDITIONAL_FALSE(ARG(value1)) && IS_CONDITIONAL_FALSE(ARG(value2)))
        return R_TRUE;

    return R_FALSE;
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

    return R_FROM_BOOL(LOGICAL(
        IS_CONDITIONAL_TRUE(ARG(value1)) && IS_CONDITIONAL_TRUE(ARG(value2))
    ));
}


//
//  not?: native [
//
//  "Returns the logic complement."
//
//      value [any-value!]
//          "(Only LOGIC!'s FALSE and BLANK! return TRUE)"
//  ]
//
REBNATIVE(not_q)
{
    INCLUDE_PARAMS_OF_NOT_Q;

    return R_FROM_BOOL(IS_CONDITIONAL_FALSE(ARG(value)));
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

    return R_FROM_BOOL(LOGICAL(
        IS_CONDITIONAL_TRUE(ARG(value1)) || IS_CONDITIONAL_TRUE(ARG(value2))
    ));
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

    // Note: no boolean ^^ in C; normalize to booleans and check unequal
    //
    return R_FROM_BOOL(LOGICAL(
        !IS_CONDITIONAL_TRUE(ARG(value1)) != !IS_CONDITIONAL_TRUE(ARG(value2))
    ));
}


//
//  CT_Logic: C
//
REBINT CT_Logic(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode >= 0)  return (VAL_LOGIC(a) == VAL_LOGIC(b));
    return -1;
}


//
//  MAKE_Logic: C
//
void MAKE_Logic(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    assert(kind == REB_LOGIC);
    UNUSED(kind);

    // As a construction routine, MAKE takes more liberties in the
    // meaning of its parameters, so it lets zero values be false.
    //
    // !!! Is there a better idea for MAKE that does not hinge on the
    // "zero is false" concept?  Is there a reason it should?
    //
    if (
        IS_CONDITIONAL_FALSE(arg)
        || (IS_INTEGER(arg) && VAL_INT64(arg) == 0)
        || (
            (IS_DECIMAL(arg) || IS_PERCENT(arg))
            && (VAL_DECIMAL(arg) == 0.0)
        )
        || (IS_MONEY(arg) && deci_is_zero(VAL_MONEY_AMOUNT(arg)))
    ) {
        Init_False(out);
    }
    else
        Init_True(out);
}


//
//  TO_Logic: C
//
void TO_Logic(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    assert(kind == REB_LOGIC);
    UNUSED(kind);

    // As a "Rebol conversion", TO falls in line with the rest of the
    // interpreter canon that all non-none non-logic-false values are
    // considered effectively "truth".
    //
    if (IS_CONDITIONAL_TRUE(arg))
        Init_True(out);
    else
        Init_False(out);
}


static inline REBOOL Math_Arg_For_Logic(REBVAL *arg)
{
    if (IS_LOGIC(arg))
        return VAL_LOGIC(arg);

    if (IS_BLANK(arg))
        return FALSE;

    fail (Error_Unexpected_Type(REB_LOGIC, VAL_TYPE(arg)));
}


//
//  REBTYPE: C
//
REBTYPE(Logic)
{
    REBOOL val1 = VAL_LOGIC(D_ARG(1));
    REBOOL val2;

    switch (action) {

    case SYM_AND_T:
        val2 = Math_Arg_For_Logic(D_ARG(2));
        val1 = LOGICAL(val1 && val2);
        break;

    case SYM_OR_T:
        val2 = Math_Arg_For_Logic(D_ARG(2));
        val1 = LOGICAL(val1 || val2);
        break;

    case SYM_XOR_T:
        val2 = Math_Arg_For_Logic(D_ARG(2));
        val1 = LOGICAL(!val1 != !val2);
        break;

    case SYM_COMPLEMENT:
        val1 = NOT(val1);
        break;

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            // random/seed false restarts; true randomizes
            Set_Random(val1 ? (REBINT)OS_DELTA_TIME(0, 0) : 1);
            return R_VOID;
        }
        if (Random_Int(REF(secure)) & 1)
            return R_TRUE;
        return R_FALSE; }

    default:
        fail (Error_Illegal_Action(REB_LOGIC, action));
    }

    return val1 ? R_TRUE : R_FALSE;
}
