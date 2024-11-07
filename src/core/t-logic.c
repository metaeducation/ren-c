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
// Copyright 2012-2024 Ren-C Open Source Contributors
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


//
//  /null?: native:intrinsic [
//
//  "Tells you if the argument is a ~null~ antiform (branch inhibitor)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(null_q)
{
    INCLUDE_PARAMS_OF_NULL_Q;

    return Init_Logic(OUT, Is_Nulled(ARG_1));
}


//
//  /okay?: native:intrinsic [
//
//  "Tells you if the argument is an ~okay~ antiform (canon branch trigger)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(okay_q)
{
    INCLUDE_PARAMS_OF_OKAY_Q;

    return Init_Logic(OUT, Is_Okay(ARG_1));
}


//
//  /logic?: native:intrinsic [
//
//  "Tells you if the argument is either the ~null~ or ~okay~ antiform"
//
//      return: "null or okay"  ; can't use LOGIC? to typecheck
//      value
//  ]
//
DECLARE_NATIVE(logic_q)
{
    INCLUDE_PARAMS_OF_LOGIC_Q;

    return Init_Logic(OUT, Is_Logic(ARG_1));
}


//
//  /logical: native:intrinsic [
//
//  "Produces ~null~ antiform for 0, or ~okay~ antiform for all other integers"
//
//      return: [logic?]
//      number [integer!]
//  ]
//
DECLARE_NATIVE(logical)
{
    INCLUDE_PARAMS_OF_LOGICAL;

    Element* n = cast(Element*, ARG_1);
    return Init_Logic(OUT, VAL_INT64(n) != 0);
}


//
//  /boolean?: native:intrinsic [
//
//  "Tells you if the argument is the TRUE or FALSE word"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(boolean_q)
{
    INCLUDE_PARAMS_OF_BOOLEAN_Q;

    Element* e = cast(Element*, ARG_1);
    return Init_Logic(OUT, Is_Boolean(e));
}


//
//  /onoff?: native:intrinsic [
//
//  "Tells you if the argument is the ON or OFF word"
//
//      return: [logic?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(onoff_q)
{
    INCLUDE_PARAMS_OF_ONOFF_Q;

    Element* e = cast(Element*, ARG_1);
    return Init_Logic(OUT, Is_OnOff(e));
}


//
//  /yesno?: native:intrinsic [
//
//  "Tells you if the argument is the YES or NO word"
//
//      return: [logic?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(yesno_q)
{
    INCLUDE_PARAMS_OF_YESNO_Q;

    Element* e = cast(Element*, ARG_1);
    return Init_Logic(OUT, Is_YesNo(e));
}


//
//  /true?: native [
//
//  "Tests if word is the word TRUE (errors if not TRUE or FALSE)"
//
//      return: [logic?]
//      word ['true 'false]
//  ]
//
DECLARE_NATIVE(true_q)
{
    INCLUDE_PARAMS_OF_TRUE_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(word)) == SYM_TRUE);
}


//
//  /false?: native [
//
//  "Tests if value is the word FALSE (errors if not TRUE or FALSE)"
//
//      return: [logic?]
//      word ['true 'false]
//  ]
//
DECLARE_NATIVE(false_q)
{
    INCLUDE_PARAMS_OF_FALSE_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(word)) == SYM_FALSE);
}


//
//  /boolean: native [
//
//  "The word TRUE if the supplied value is a branch trigger, otherwise FALSE"
//
//      return: [boolean?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(boolean)
{
    INCLUDE_PARAMS_OF_BOOLEAN;

    return Init_Word(OUT, Is_Trigger(ARG(value)) ? Canon(TRUE) : Canon(FALSE));
}


//
//  /yes?: native [
//
//  "Tests if word is the word YES (errors if not YES or NO)"
//
//      return: [logic?]
//      word ['yes 'no]
//  ]
//
DECLARE_NATIVE(yes_q)
{
    INCLUDE_PARAMS_OF_YES_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(word)) == SYM_YES);
}


//
//  /no?: native [
//
//  "Tests if value is the word NO (errors if not YES or NO)"
//
//      return: [logic?]
//      word ['yes 'no]
//  ]
//
DECLARE_NATIVE(no_q)
{
    INCLUDE_PARAMS_OF_NO_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(word)) == SYM_NO);
}


//
//  /to-yesno: native [
//
//  "The word YES if the supplied value is a branch trigger, otherwise NO"
//
//      return: [yesno?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(to_yesno)
{
    INCLUDE_PARAMS_OF_TO_YESNO;

    return Init_Word(OUT, Is_Trigger(ARG(value)) ? Canon(YES) : Canon(NO));
}


//
//  /on?: native [
//
//  "Tests if word is the word ON (errors if not ON or OFF)"
//
//      return: [logic?]
//      word ['on 'off]
//  ]
//
DECLARE_NATIVE(on_q)
{
    INCLUDE_PARAMS_OF_ON_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(word)) == SYM_ON);
}


//
//  /off?: native [
//
//  "Tests if value is the word OFF (errors if not ON or OFF)"
//
//      return: [logic?]
//      word ['on 'off]
//  ]
//
DECLARE_NATIVE(off_q)
{
    INCLUDE_PARAMS_OF_NO_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(word)) == SYM_OFF);
}


//
//  /to-onoff: native [
//
//  "The word ON if the supplied value is a branch trigger, otherwise OFF"
//
//      return: [onoff?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(to_onoff)
{
    INCLUDE_PARAMS_OF_TO_ONOFF;

    return Init_Word(OUT, Is_Trigger(ARG(value)) ? Canon(ON) : Canon(OFF));
}


//
//  /and?: native [
//
//  "Returns true if both values are conditionally true (no 'short-circuit')"
//
//      return: [logic?]
//      value1 [any-value?]
//      value2 [any-value?]
//  ]
//
DECLARE_NATIVE(and_q)
{
    INCLUDE_PARAMS_OF_AND_Q;

    if (Is_Trigger(ARG(value1)) && Is_Trigger(ARG(value2)))
        return Init_Logic(OUT, true);

    return Init_Logic(OUT, false);
}


//
//  /nor?: native [
//
//  "Returns true if both values are conditionally false (no 'short-circuit')"
//
//      return: [logic?]
//      value1 [any-value?]
//      value2 [any-value?]
//  ]
//
DECLARE_NATIVE(nor_q)
{
    INCLUDE_PARAMS_OF_NOR_Q;

    if (Is_Inhibitor(ARG(value1)) && Is_Inhibitor(ARG(value2)))
        return Init_Logic(OUT, true);

    return Init_Logic(OUT, false);
}


//
//  /nand?: native [
//
//  "Returns false if both values are conditionally true (no 'short-circuit')"
//
//      return: [logic?]
//      value1 [any-value?]
//      value2 [any-value?]
//  ]
//
DECLARE_NATIVE(nand_q)
{
    INCLUDE_PARAMS_OF_NAND_Q;

    return Init_Logic(
        OUT,
        Is_Trigger(ARG(value1)) and Is_Trigger(ARG(value2))
    );
}


//
//  /to-logic: native [
//
//  "true if value is NOT a LOGIC! false or NULL"
//
//      return: [logic?]
//      value [any-value?]
//  ]
//
DECLARE_NATIVE(to_logic)
{
    INCLUDE_PARAMS_OF_TO_LOGIC;

    return Init_Logic(OUT, Is_Trigger(ARG(value)));
}


//
//  /null-if-zero: native [
//
//  "Null if the integer input is a zero"
//
//      return: [logic?]
//      integer [integer!]
//  ]
//
DECLARE_NATIVE(null_if_zero)
{
    INCLUDE_PARAMS_OF_NULL_IF_ZERO;

    return Init_Logic(OUT, VAL_INT64(ARG(integer)) != 0);
}


//
//  /not: native:intrinsic [
//
//  "Returns the logic complement (inverts the nullness of what's passed in)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(not_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_NOT_1;

    return Init_Logic(OUT, Is_Inhibitor(ARG_1));
}


// The handling of logic has gone through several experiments, some of which
// made it more like a branching structure (so able to pass the result of the
// left hand side to the right).  There was also behavior for GET-GROUP!, to
// run the provided code whether the condition on the left was true or not.
//
// This scales the idea back to a very simple concept of a quoted GROUP!,
// WORD!, or TUPLE!.
//
INLINE bool Do_Logic_Right_Side_Throws(
    Sink(Value) out,
    const Element* right
){
    if (Is_Group(right)) {
        Atom* atom_out = out;
        if (Eval_Any_List_At_Throws(atom_out, right, SPECIFIED))
            return true;
        Decay_If_Unstable(atom_out);
        return false;
    }

    assert(Is_Word(right) or Is_Tuple(right));

    Get_Var_May_Fail(out, right, SPECIFIED);

    if (Is_Action(out))
        fail ("words/tuples can't be action as right hand of OR, AND, XOR");

    return false;
}


//
//  /and: infix native [
//
//  "Boolean AND, right hand side must be in GROUP! to allow short-circuit"
//
//      return: [logic?]
//      left [any-value?]
//      @right "Right is evaluated if left is true"
//          [group! tuple! word!]
//  ]
//
DECLARE_NATIVE(and_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_AND_1;

    Value* left = ARG(left);
    Element* right = cast(Element*, ARG(right));

    if (Is_Inhibitor(left))
        return Init_Logic(OUT, false);

    if (Do_Logic_Right_Side_Throws(SPARE, right))
        return THROWN;

    return Init_Logic(OUT, Is_Trigger(stable_SPARE));
}


//
//  /or: infix native [
//
//  "Boolean OR, right hand side must be in GROUP! to allow short-circuit"
//
//      return: [logic?]
//      left [any-value?]
//      @right "Right is evaluated if left is false"
//          [group! tuple! word!]
//  ]
//
DECLARE_NATIVE(or_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_OR_1;

    Value* left = ARG(left);
    Element* right = cast(Element*, ARG(right));

    if (Is_Trigger(left))
        return Init_Logic(OUT, true);

    if (Do_Logic_Right_Side_Throws(SPARE, right))
        return THROWN;

    return Init_Logic(OUT, Is_Trigger(stable_SPARE));
}


//
//  /xor: infix native [
//
//  "Boolean XOR (operation cannot be short-circuited)"
//
//      return: [logic?]
//      left [any-value?]
//      @right "Always evaluated"
//          [group! tuple! word!]
//  ]
//
DECLARE_NATIVE(xor_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_XOR_1;

    Value* left = ARG(left);
    Element* right = cast(Element*, ARG(right));

    if (Do_Logic_Right_Side_Throws(SPARE, right))
        return THROWN;

    if (Is_Inhibitor(left))
        return Init_Logic(OUT, Is_Trigger(stable_SPARE));

    return Init_Logic(OUT, Is_Inhibitor(stable_SPARE));
}


//
//  /unless: infix native [
//
//  "Give left hand side when right hand side is not null or void"
//
//      return: [any-value?]
//      left "Expression which will always be evaluated"
//          [any-value?]
//      ^right "Expression that's also always evaluated (can't short circuit)"
//          [pack? any-value?]  ; not literal GROUP! as with XOR
//  ]
//
DECLARE_NATIVE(unless)
//
// Though this routine is similar to XOR, it is different enough in usage and
// looks from AND/OR/XOR to warrant not needing XOR's protection (e.g. forcing
// a GROUP! on the right hand side, prohibiting literal blocks on left)
{
    INCLUDE_PARAMS_OF_UNLESS;

    Value* left = ARG(left);
    Element* meta_right = cast(Element*, ARG(right));

    if (Is_Meta_Of_Void(meta_right) or Is_Meta_Of_Null(meta_right))
        return COPY(left);

    return UNMETA(meta_right);  // preserve packs
}
