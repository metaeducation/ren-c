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
DECLARE_NATIVE(NULL_Q)
{
    INCLUDE_PARAMS_OF_NULL_Q;

    DECLARE_VALUE (v);
    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(v, LEVEL);
    if (bounce)
        return unwrap bounce;

    return LOGIC(Is_Nulled(v));
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
DECLARE_NATIVE(OKAY_Q)
{
    INCLUDE_PARAMS_OF_OKAY_Q;

    DECLARE_VALUE (v);
    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(v, LEVEL);
    if (bounce)
        return unwrap bounce;

    return LOGIC(Is_Okay(v));
}


//
//  /logic?: native:intrinsic [
//
//  "Tells you if the argument is either the ~null~ or ~okay~ antiform"
//
//      return: [keyword!]  ; Note: using LOGIC? to typecheck is recursive
//      value
//  ]
//
DECLARE_NATIVE(LOGIC_Q)
{
    INCLUDE_PARAMS_OF_LOGIC_Q;

    DECLARE_VALUE (v);
    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(v, LEVEL);
    if (bounce)
        return unwrap bounce;

    return LOGIC(Is_Logic(v));
}


//
//  /logical: native [
//
//  "Produces ~null~ antiform for 0, or ~okay~ antiform for all other integers"
//
//      return: [logic?]
//      number [integer!]
//  ]
//
DECLARE_NATIVE(LOGICAL)
{
    INCLUDE_PARAMS_OF_LOGICAL;

    Element* n = Element_ARG(NUMBER);
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
DECLARE_NATIVE(BOOLEAN_Q)
{
    INCLUDE_PARAMS_OF_BOOLEAN_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_Boolean(e));
}


//
//  /onoff?: native:intrinsic [
//
//  "Tells you if the argument is the ON or OFF word"
//
//      return: [logic?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(ONOFF_Q)
{
    INCLUDE_PARAMS_OF_ONOFF_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_OnOff(e));
}


//
//  /yesno?: native:intrinsic [
//
//  "Tells you if the argument is the YES or NO word"
//
//      return: [logic?]
//      value [<maybe> element?]
//  ]
//
DECLARE_NATIVE(YESNO_Q)
{
    INCLUDE_PARAMS_OF_YESNO_Q;

    DECLARE_ELEMENT (e);
    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(e, LEVEL);
    if (b)
        return unwrap b;

    return LOGIC(Is_YesNo(e));
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
DECLARE_NATIVE(TRUE_Q)
{
    INCLUDE_PARAMS_OF_TRUE_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(WORD)) == SYM_TRUE);
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
DECLARE_NATIVE(FALSE_Q)
{
    INCLUDE_PARAMS_OF_FALSE_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(WORD)) == SYM_FALSE);
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
DECLARE_NATIVE(BOOLEAN)
{
    INCLUDE_PARAMS_OF_BOOLEAN;

    return Init_Word(OUT, Is_Trigger(ARG(VALUE)) ? CANON(TRUE) : CANON(FALSE));
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
DECLARE_NATIVE(YES_Q)
{
    INCLUDE_PARAMS_OF_YES_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(WORD)) == SYM_YES);
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
DECLARE_NATIVE(NO_Q)
{
    INCLUDE_PARAMS_OF_NO_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(WORD)) == SYM_NO);
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
DECLARE_NATIVE(TO_YESNO)
{
    INCLUDE_PARAMS_OF_TO_YESNO;

    return Init_Word(OUT, Is_Trigger(ARG(VALUE)) ? CANON(YES) : CANON(NO));
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
DECLARE_NATIVE(ON_Q)
{
    INCLUDE_PARAMS_OF_ON_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(WORD)) == SYM_ON);
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
DECLARE_NATIVE(OFF_Q)
{
    INCLUDE_PARAMS_OF_NO_Q;

    return Init_Logic(OUT, Cell_Word_Id(ARG(WORD)) == SYM_OFF);
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
DECLARE_NATIVE(TO_ONOFF)
{
    INCLUDE_PARAMS_OF_TO_ONOFF;

    return Init_Word(OUT, Is_Trigger(ARG(VALUE)) ? CANON(ON) : CANON(OFF));
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
DECLARE_NATIVE(AND_Q)
{
    INCLUDE_PARAMS_OF_AND_Q;

    if (Is_Trigger(ARG(VALUE1)) && Is_Trigger(ARG(VALUE2)))
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
DECLARE_NATIVE(NOR_Q)
{
    INCLUDE_PARAMS_OF_NOR_Q;

    if (Is_Inhibitor(ARG(VALUE1)) && Is_Inhibitor(ARG(VALUE2)))
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
DECLARE_NATIVE(NAND_Q)
{
    INCLUDE_PARAMS_OF_NAND_Q;

    return Init_Logic(
        OUT,
        Is_Trigger(ARG(VALUE1)) and Is_Trigger(ARG(VALUE2))
    );
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
DECLARE_NATIVE(NULL_IF_ZERO)
{
    INCLUDE_PARAMS_OF_NULL_IF_ZERO;

    return Init_Logic(OUT, VAL_INT64(ARG(INTEGER)) != 0);
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
DECLARE_NATIVE(NOT_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_NOT_1;

    DECLARE_VALUE (v);
    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(v, LEVEL);
    if (bounce)
        return unwrap bounce;

    return LOGIC(Is_Inhibitor(v));
}


//
//  /to-logic: native:intrinsic [
//
//  "Returns logic of what's given (null if null, okay for everything else)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(TO_LOGIC)
{
    INCLUDE_PARAMS_OF_TO_LOGIC;

    DECLARE_VALUE (v);
    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(v, LEVEL);
    if (bounce)
        return unwrap bounce;

    return LOGIC(Is_Trigger(v));
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
DECLARE_NATIVE(AND_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_AND_1;

    Value* left = ARG(LEFT);
    Element* right = Element_ARG(RIGHT);

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
DECLARE_NATIVE(OR_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_OR_1;

    Value* left = ARG(LEFT);
    Element* right = Element_ARG(RIGHT);

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
DECLARE_NATIVE(XOR_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_XOR_1;

    Value* left = ARG(LEFT);
    Element* right = Element_ARG(RIGHT);

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
//          [pack! any-value?]  ; not literal GROUP! as with XOR
//  ]
//
DECLARE_NATIVE(UNLESS)
//
// Though this routine is similar to XOR, it is different enough in usage and
// looks from AND/OR/XOR to warrant not needing XOR's protection (e.g. forcing
// a GROUP! on the right hand side, prohibiting literal blocks on left)
{
    INCLUDE_PARAMS_OF_UNLESS;

    Value* left = ARG(LEFT);
    Element* meta_right = Element_ARG(RIGHT);

    if (Is_Meta_Of_Void(meta_right) or Is_Meta_Of_Null(meta_right))
        return COPY(left);

    return UNMETA(meta_right);  // preserve packs
}
