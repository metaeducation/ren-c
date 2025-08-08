//
//  file: %t-logic.c
//  summary: "logic datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//  null?: native:intrinsic [
//
//  "Tells you if the argument is a light ~null~ antiform (branch inhibitor)"
//
//      return: [logic?]
//      ^value "Use DECAY if testing heavy null (e.g. PACK! containing null)"
//           [any-stable? pack!]
//  ]
//
DECLARE_NATIVE(NULL_Q)
{
    INCLUDE_PARAMS_OF_NULL_Q;

    const Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    if (not Is_Pack(atom))
        return LOGIC(Is_Light_Null(atom));

    DECLARE_ATOM (decayed);
    Copy_Cell(decayed, atom);
    trap (
        Decay_If_Unstable(decayed)
    );

    if (Is_Light_Null(decayed))  // e.g. oriinal first pack element was null
        return fail (
            "Heavy null detected by NULL?, use NULL? DECAY if intentional"
       );

    return LOGIC(false);
}


//
//  okay?: native:intrinsic [
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
    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(v, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Okay(v));
}


//
//  logic?: native:intrinsic [
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
    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(v, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Logic(v));
}


//
//  logical: native [
//
//  "Produces ~null~ antiform for 0, or ~okay~ antiform for all other integers"
//
//      return: [logic?]
//      value [integer!]
//  ]
//
DECLARE_NATIVE(LOGICAL)
{
    INCLUDE_PARAMS_OF_LOGICAL;

    Element* v = Element_ARG(VALUE);
    return LOGIC(VAL_INT64(v) != 0);
}


//
//  boolean?: native:intrinsic [
//
//  "Tells you if the argument is the TRUE or FALSE word"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(BOOLEAN_Q)
{
    INCLUDE_PARAMS_OF_BOOLEAN_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_Boolean(e));
}


//
//  onoff?: native:intrinsic [
//
//  "Tells you if the argument is the ON or OFF word"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(ONOFF_Q)
{
    INCLUDE_PARAMS_OF_ONOFF_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_OnOff(e));
}


//
//  yesno?: native:intrinsic [
//
//  "Tells you if the argument is the YES or NO word"
//
//      return: [logic?]
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(YESNO_Q)
{
    INCLUDE_PARAMS_OF_YESNO_Q;

    DECLARE_ELEMENT (e);
    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(e, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    return LOGIC(Is_YesNo(e));
}


//
//  true?: native [
//
//  "Tests if word is the word TRUE (errors if not TRUE or FALSE)"
//
//      return: [logic?]
//      word [~(true false)~]
//  ]
//
DECLARE_NATIVE(TRUE_Q)
{
    INCLUDE_PARAMS_OF_TRUE_Q;

    return LOGIC(Word_Id(ARG(WORD)) == SYM_TRUE);
}


//
//  false?: native [
//
//  "Tests if value is the word FALSE (errors if not TRUE or FALSE)"
//
//      return: [logic?]
//      word [~(true false)~]
//  ]
//
DECLARE_NATIVE(FALSE_Q)
{
    INCLUDE_PARAMS_OF_FALSE_Q;

    return LOGIC(Word_Id(ARG(WORD)) == SYM_FALSE);
}


//
//  boolean: native [
//
//  "The word TRUE if the condition is a branch trigger, otherwise FALSE"
//
//      return: [~(true false)~]
//      value [any-stable?]
//  ]
//
DECLARE_NATIVE(BOOLEAN)
{
    INCLUDE_PARAMS_OF_BOOLEAN;

    require (
      bool cond = Test_Conditional(ARG(VALUE))
    );
    return Init_Word(OUT, cond ? CANON(TRUE) : CANON(FALSE));
}


//
//  yes?: native [
//
//  "Tests if word is the word YES (errors if not YES or NO)"
//
//      return: [logic?]
//      word [~(yes no)~]
//  ]
//
DECLARE_NATIVE(YES_Q)
{
    INCLUDE_PARAMS_OF_YES_Q;

    return LOGIC(Word_Id(ARG(WORD)) == SYM_YES);
}


//
//  no?: native [
//
//  "Tests if value is the word NO (errors if not YES or NO)"
//
//      return: [logic?]
//      word [~(yes no)~]
//  ]
//
DECLARE_NATIVE(NO_Q)
{
    INCLUDE_PARAMS_OF_NO_Q;

    return LOGIC(Word_Id(ARG(WORD)) == SYM_NO);
}


//
//  to-yesno: native [
//
//  "The word YES if the condition is a branch trigger, otherwise NO"
//
//      return: [~(yes no)~]
//      condition [any-stable?]
//  ]
//
DECLARE_NATIVE(TO_YESNO)
{
    INCLUDE_PARAMS_OF_TO_YESNO;

    require (
      bool cond = Test_Conditional(ARG(CONDITION))
    );
    return Init_Word(OUT, cond ? CANON(YES) : CANON(NO));
}


//
//  on?: native [
//
//  "Tests if word is the word ON (errors if not ON or OFF)"
//
//      return: [logic?]
//      word [~(on off)~]
//  ]
//
DECLARE_NATIVE(ON_Q)
{
    INCLUDE_PARAMS_OF_ON_Q;

    return LOGIC(Word_Id(ARG(WORD)) == SYM_ON);
}


//
//  off?: native [
//
//  "Tests if value is the word OFF (errors if not ON or OFF)"
//
//      return: [logic?]
//      word [~(on off)~]
//  ]
//
DECLARE_NATIVE(OFF_Q)
{
    INCLUDE_PARAMS_OF_NO_Q;

    return LOGIC(Word_Id(ARG(WORD)) == SYM_OFF);
}


//
//  to-onoff: native [
//
//  "The word ON if the condition is a branch trigger, otherwise OFF"
//
//      return: [~(on off)~]
//      condition [any-stable?]
//  ]
//
DECLARE_NATIVE(TO_ONOFF)
{
    INCLUDE_PARAMS_OF_TO_ONOFF;

    require (
      bool cond = Test_Conditional(ARG(CONDITION))
    );
    return Init_Word(OUT, cond ? CANON(ON) : CANON(OFF));
}


//
//  and?: native [
//
//  "Returns true if both values are conditionally true (no 'short-circuit')"
//
//      return: [logic?]
//      value1 [any-stable?]
//      value2 [any-stable?]
//  ]
//
DECLARE_NATIVE(AND_Q)
{
    INCLUDE_PARAMS_OF_AND_Q;

    require (
      bool cond1 = Test_Conditional(ARG(VALUE1))
    );
    require (
      bool cond2 = Test_Conditional(ARG(VALUE2))
    );

    if (cond1 and cond2)
        return LOGIC(true);

    return LOGIC(false);
}


//
//  or?: native [
//
//  "Returns true if both values are conditionally false (no 'short-circuit')"
//
//      return: [logic?]
//      value1 [any-stable?]
//      value2 [any-stable?]
//  ]
//
DECLARE_NATIVE(OR_Q)
{
    INCLUDE_PARAMS_OF_OR_Q;

    require (
      bool cond1 = Test_Conditional(ARG(VALUE1))
    );
    require (
      bool cond2 = Test_Conditional(ARG(VALUE2))
    );

    if (cond1 or cond2)
        return LOGIC(true);

    return LOGIC(false);
}


//
//  null-if-zero: native [
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

    return LOGIC(VAL_INT64(ARG(INTEGER)) != 0);
}


//
//  not: native:intrinsic [
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
    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(v, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    require (
      bool cond = Test_Conditional(v)
    );
    return LOGIC(not cond);
}


//
//  to-logic: native:intrinsic [
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
    require (
      Bounce b = Bounce_Decay_Value_Intrinsic(v, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    require (
      bool cond = Test_Conditional(v)
    );
    return LOGIC(cond);
}


// The handling of logic has gone through several experiments, some of which
// made it more like a branching structure (so able to pass the result of the
// left hand side to the right).  There was also behavior for GET-GROUP!, to
// run the provided code whether the condition on the left was true or not.
//
// This scales the idea back to a very simple concept of a literal GROUP!,
// WORD!, or TUPLE!.
//
INLINE Result(bool) Eval_Logic_Op_Right_Side(Level* level_)
{
    INCLUDE_PARAMS_OF_AND_1;  // should be same as OR and XOR

    USED(ARG(LEFT));  // caller examines
    Element* right = Element_ARG(RIGHT);

    Value* synthesized;
    if (Is_Group(right)) {
        if (Eval_Any_List_At_Throws(SPARE, right, SPECIFIED))
            panic (Error_No_Catch_For_Throw(level_));

        require (
          synthesized = Decay_If_Unstable(SPARE)
        );
    }
    else {
        assert(Is_Word(right) or Is_Tuple(right));

        require (
          Value* spare = Get_Var(SPARE, NO_STEPS, right, SPECIFIED)
        );
        if (Is_Action(spare))
            panic (
                "words/tuples can't be action as right side of OR AND XOR"
            );

        synthesized = spare;
    }

    return Test_Conditional(synthesized);
}


//
//  and: infix native [
//
//  "Boolean AND, right hand side must be in GROUP! to allow short-circuit"
//
//      return: [logic?]
//      left [any-stable?]
//      @right "Right is evaluated if left is true"
//          [group! tuple! word!]
//  ]
//
DECLARE_NATIVE(AND_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_AND_1;

    require (
      bool left = Test_Conditional(ARG(LEFT))
    );
    if (not left)
        return LOGIC(false);  // if left is false, don't run right hand side

    require (
      bool right = Eval_Logic_Op_Right_Side(LEVEL)
    );
    USED(ARG(RIGHT));

    return LOGIC(right);
}


//
//  or: infix native [
//
//  "Boolean OR, right hand side must be in GROUP! to allow short-circuit"
//
//      return: [logic?]
//      left [any-stable?]
//      @right "Right is evaluated if left is false"
//          [group! tuple! word!]
//  ]
//
DECLARE_NATIVE(OR_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_OR_1;

    require (
      bool left = Test_Conditional(ARG(LEFT))
    );
    if (left)
        return LOGIC(true);  // if left is true, don't run right hand side

    require (
      bool right = Eval_Logic_Op_Right_Side(LEVEL)
    );
    USED(ARG(RIGHT));

    return LOGIC(right);
}


//
//  xor: infix native [
//
//  "Boolean XOR (operation cannot be short-circuited)"
//
//      return: [logic?]
//      left [any-stable?]
//      @right "Always evaluated"
//          [group! tuple! word!]
//  ]
//
DECLARE_NATIVE(XOR_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_XOR_1;

    require (
      bool right = Eval_Logic_Op_Right_Side(LEVEL)
    );  // always evals
    USED(ARG(RIGHT));

    require (
      bool left = Test_Conditional(ARG(LEFT))
    );
    if (not left)
        return LOGIC(right);

    return LOGIC(not right);
}


//
//  unless: infix native [
//
//  "Give left hand side when right hand side is not pure null"
//
//      return: [any-stable?]
//      left "Expression which will always be evaluated"
//          [any-stable?]
//      ^right "Expression that's also always evaluated (can't short circuit)"
//          [any-value?]  ; not literal GROUP! as with XOR
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
    Atom* right = Atom_ARG(RIGHT);

    if (Is_Ghost(right))
        panic ("UNLESS can't be used with GHOST! antiform");

    if (Is_Light_Null(right) or Is_Heavy_Null(right))
        return COPY(left);

    return COPY(right);  // preserve packs
}
