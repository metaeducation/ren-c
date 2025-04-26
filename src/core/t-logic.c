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


//
//  true?: native [
//
//  {Tests if word is the word TRUE}
//
//      word "Must be the word TRUE or FALSE"
//          [word!]
//  ]
//
DECLARE_NATIVE(TRUE_Q)
{
    INCLUDE_PARAMS_OF_TRUE_Q;

    Value* word = ARG(WORD);

    if (Cell_Word_Id(word) == SYM_TRUE)
        return Init_Logic(OUT, true);
    if (Cell_Word_Id(word) == SYM_FALSE)
        return Init_Logic(OUT, false);
    fail ("TRUE? requires word to be TRUE or FALSE");
}


//
//  false?: native [
//
//  {Tests if value is the word FALSE}
//
//      word "Must be the word TRUE or FALSE"
//          [word!]
//  ]
//
DECLARE_NATIVE(FALSE_Q)
{
    INCLUDE_PARAMS_OF_FALSE_Q;

    Value* word = ARG(WORD);

    if (Cell_Word_Id(word) == SYM_FALSE)
        return Init_Logic(OUT, true);
    if (Cell_Word_Id(word) == SYM_TRUE)
        return Init_Logic(OUT, false);
    fail ("FALSE? requires word to be TRUE or FALSE");
}


//
//  boolean: native [
//
//  "The word TRUE if the supplied value is a branch trigger, otherwise FALSE"
//
//      return: [word!]
//      value [any-value!]
//  ]
//
DECLARE_NATIVE(BOOLEAN)
{
    INCLUDE_PARAMS_OF_BOOLEAN;

    return Init_Word(OUT, Is_Nulled(ARG(VALUE)) ? CANON(FALSE) : CANON(TRUE));
}


//
//  yes?: native [
//
//  {Tests if word is the word YES}
//
//      word "Must be the word YES or NO"
//          [word!]
//  ]
//
DECLARE_NATIVE(YES_Q)
{
    INCLUDE_PARAMS_OF_YES_Q;

    Value* word = ARG(WORD);

    if (Cell_Word_Id(word) == SYM_YES)
        return Init_Logic(OUT, true);
    if (Cell_Word_Id(word) == SYM_NO)
        return Init_Logic(OUT, false);
    fail ("YES? requires word to be YES or NO");
}


//
//  no?: native [
//
//  {Tests if value is the word NO}
//
//      word "Must be the word YES or NO"
//          [word!]
//  ]
//
DECLARE_NATIVE(NO_Q)
{
    INCLUDE_PARAMS_OF_NO_Q;

    Value* word = ARG(WORD);

    if (Cell_Word_Id(word) == SYM_NO)
        return Init_Logic(OUT, true);
    if (Cell_Word_Id(word) == SYM_YES)
        return Init_Logic(OUT, false);
    fail ("NO? requires word to be YES or NO");
}


//
//  on?: native [
//
//  {Tests if word is the word ON}
//
//      word "Must be the word ON or OFF"
//          [word!]
//  ]
//
DECLARE_NATIVE(ON_Q)
{
    INCLUDE_PARAMS_OF_ON_Q;

    Value* word = ARG(WORD);

    if (Cell_Word_Id(word) == SYM_ON)
        return Init_Logic(OUT, true);
    if (Cell_Word_Id(word) == SYM_OFF)
        return Init_Logic(OUT, false);
    fail ("ON? requires word to be ON or OFF");
}


//
//  off?: native [
//
//  {Tests if value is the word OFF}
//
//      word "Must be the word ON or OFF"
//          [word!]
//  ]
//
DECLARE_NATIVE(OFF_Q)
{
    INCLUDE_PARAMS_OF_NO_Q;

    Value* word = ARG(WORD);

    if (Cell_Word_Id(word) == SYM_OFF)
        return Init_Logic(OUT, true);
    if (Cell_Word_Id(word) == SYM_ON)
        return Init_Logic(OUT, false);
    fail ("OFF? requires word to be ON or OFF");
}


//
//  and?: native [
//
//  {Returns true if both values are conditionally true (no "short-circuit")}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
DECLARE_NATIVE(AND_Q)
{
    INCLUDE_PARAMS_OF_AND_Q;

    if (IS_TRUTHY(ARG(VALUE1)) && IS_TRUTHY(ARG(VALUE2)))
        return LOGIC(true);

    return LOGIC(false);
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
DECLARE_NATIVE(NOR_Q)
{
    INCLUDE_PARAMS_OF_NOR_Q;

    if (IS_FALSEY(ARG(VALUE1)) && IS_FALSEY(ARG(VALUE2)))
        return LOGIC(true);

    return LOGIC(false);
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
DECLARE_NATIVE(NAND_Q)
{
    INCLUDE_PARAMS_OF_NAND_Q;

    return Init_Logic(
        OUT,
        IS_TRUTHY(ARG(VALUE1)) and IS_TRUTHY(ARG(VALUE2))
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
DECLARE_NATIVE(DID_Q)
{
    INCLUDE_PARAMS_OF_DID_Q;

    return Init_Logic(OUT, IS_TRUTHY(ARG(VALUE)));
}


//
//  did: native [
//
//  "Variant of TO-LOGIC which considers null values to also be false"
//
//      return: [logic!]
//          {true if value is NOT a LOGIC! false, BLANK!, or null}
//      optional [any-value!]
//  ]
//
DECLARE_NATIVE(DID)
{
    INCLUDE_PARAMS_OF_DID;

    return Init_Logic(OUT, IS_TRUTHY(ARG(OPTIONAL)));
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
DECLARE_NATIVE(NOT_Q)
{
    INCLUDE_PARAMS_OF_NOT_Q;

    return Init_Logic(OUT, IS_FALSEY(ARG(VALUE)));
}


//
//  not: native [
//
//  "Returns the logic complement, considering voids to be false."
//
//      return: [logic!]
//          "Only LOGIC!'s FALSE, BLANK!, and void for cell return TRUE"
//      optional [any-value!]
//  ]
//
DECLARE_NATIVE(NOT)
{
    INCLUDE_PARAMS_OF_NOT;

    return Init_Logic(OUT, IS_FALSEY(ARG(OPTIONAL)));
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
DECLARE_NATIVE(OR_Q)
{
    INCLUDE_PARAMS_OF_OR_Q;

    return Init_Logic(
        OUT,
        IS_TRUTHY(ARG(VALUE1)) or IS_TRUTHY(ARG(VALUE2))
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
DECLARE_NATIVE(XOR_Q)
{
    INCLUDE_PARAMS_OF_XOR_Q;

    // Note: no boolean ^^ in C; check unequal
    //
    return Init_Logic(
        OUT,
        IS_TRUTHY(ARG(VALUE1)) != IS_TRUTHY(ARG(VALUE2))
    );
}


//
//  and: infix native [
//
//  {Boolean AND, with short-circuit mode if right hand side is BLOCK!}
//
//      return: "Conditionally true or false value (not coerced to LOGIC!)"
//          [any-value!]
//      left "Expression which will always be evaluated"
//          [any-value!]
//      :right "If BLOCK!, evaluated only if TO LOGIC! of LEFT is true"
//          [block! group!]
//  ]
//
DECLARE_NATIVE(AND)
{
    INCLUDE_PARAMS_OF_AND;

    Value* left = ARG(LEFT);
    Value* right = ARG(RIGHT);

    if (IS_FALSEY(left)) {
        if (Is_Group(right)) { // no need to evaluate right if BLOCK!
            if (Eval_List_At_Throws(OUT, right))
                return BOUNCE_THROWN;
        }
        RETURN (left); // preserve falsey value
    }

    if (Eval_List_At_Throws(OUT, right))
        return BOUNCE_THROWN;

    return OUT; // preserve the exact truthy or falsey value
}


//  or: infix native [
//
//  {Boolean OR, with short-circuit mode if right hand side is BLOCK!}
//
//      return: "Conditionally true or false value (not coerced to LOGIC!)"
//          [any-value!]
//      left "Expression which will always be evaluated"
//          [any-value!]
//      :right "If BLOCK!, evaluated only if TO LOGIC! of LEFT is false"
//          [block! group!]
//  ]
DECLARE_NATIVE(OR)
{
    INCLUDE_PARAMS_OF_OR;

    Value* left = ARG(LEFT);
    Value* right = ARG(RIGHT);

    if (IS_TRUTHY(left)) {
        if (Is_Group(right)) { // no need to evaluate right if BLOCK!
            if (Eval_List_At_Throws(OUT, right))
                return BOUNCE_THROWN;
        }
        RETURN (left);
    }

    if (Eval_List_At_Throws(OUT, right))
        return BOUNCE_THROWN;

    return OUT; // preserve the exact truthy or falsey value
}


//
//  xor: infix native [
//
//  {Boolean XOR}
//
//      return: "Conditionally true value, or nullptr for failure case"
//          [any-value!]
//      left "Expression which will always be evaluated"
//          [any-value!]
//      :right "Expression that's also always evaluated (can't short circuit)"
//          [group!]
//  ]
//
DECLARE_NATIVE(XOR)
{
    INCLUDE_PARAMS_OF_XOR;

    Value* left = ARG(LEFT);

    if (Eval_List_At_Throws(OUT, ARG(RIGHT))) // always evaluated
        return BOUNCE_THROWN;

    Value* right = OUT;

    if (IS_FALSEY(left)) {
        if (IS_FALSEY(right))
            return nullptr; // default to logic false if both false

        return right;
    }

    if (IS_TRUTHY(right))
        return nullptr; // default to logic false if both true

    RETURN (left);
}


//
//  unless: infix/defer native [
//
//  {Variant of non-short-circuit OR which favors the right-hand side result}
//
//      return: "Conditionally true or false value (not coerced to LOGIC!)"
//          [any-value!]
//      left "Expression which will always be evaluated"
//          [any-value!]
//      right "Expression that's also always evaluated (can't short circuit)"
//          [any-value!] ;-- not a literal GROUP! as with XOR
//  ]
//
DECLARE_NATIVE(UNLESS)
//
// Though this routine is similar to XOR, it is different enough in usage and
// looks from AND/OR/XOR to warrant not needing XOR's protection (e.g. forcing
// a GROUP! on the right hand side, prohibiting literal blocks on left)
{
    INCLUDE_PARAMS_OF_UNLESS;

    if (IS_TRUTHY(ARG(RIGHT)))
        RETURN (ARG(RIGHT));

    RETURN (ARG(LEFT)); // preserve the exact truthy or falsey value
}


INLINE bool Math_Arg_For_Logic(Value* arg)
{
    if (Is_Logic(arg))
        return VAL_LOGIC(arg);

    if (Is_Blank(arg))
        return false;

    fail (Error_Unexpected_Type(TYPE_BLANK, Type_Of(arg)));
}


//
//  MF_Logic: C
//
void MF_Logic(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form); // currently no distinction between MOLD and FORM

    Emit(mo, "+N", VAL_LOGIC(v) ? CANON(TRUE) : CANON(FALSE));
}


//
//  REBTYPE: C
//
REBTYPE(Logic)
{
    bool b1 = VAL_LOGIC(D_ARG(1));
    bool b2;

    switch (Cell_Word_Id(verb)) {

    case SYM_INTERSECT:
        b2 = Math_Arg_For_Logic(D_ARG(2));
        return Init_Logic(OUT, b1 and b2);

    case SYM_UNION:
        b2 = Math_Arg_For_Logic(D_ARG(2));
        return Init_Logic(OUT, b1 or b2);

    case SYM_DIFFERENCE:
        b2 = Math_Arg_For_Logic(D_ARG(2));
        return Init_Logic(OUT, b1 != b2);

    case SYM_COMPLEMENT:
        return Init_Logic(OUT, not b1);

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PARAM(VALUE));

        if (Bool_ARG(ONLY))
            fail (Error_Bad_Refines_Raw());

        if (Bool_ARG(SEED)) {
            // random/seed false restarts; true randomizes
            Set_Random(b1 ? cast(REBINT, OS_DELTA_TIME(0)) : 1);
            return nullptr;
        }
        if (Random_Int(Bool_ARG(SECURE)) & 1)
            return LOGIC(true);
        return LOGIC(false); }

    default:
        fail (Error_Illegal_Action(TYPE_BLANK, verb));
    }
}
