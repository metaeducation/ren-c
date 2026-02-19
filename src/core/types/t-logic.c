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
//  /null?: pure native:intrinsic [
//
//  "Tells you if the argument is a light ~null~ antiform (branch inhibitor)"
//
//      return: [logic!]
//      ^value "Use DECAY if testing heavy null (e.g. PACK! containing null)"
//           '[any-stable? pack!]
//  ]
//
DECLARE_NATIVE(NULL_Q)
{
    INCLUDE_PARAMS_OF_NULL_Q;

    Value* v = Unchecked_ARG(VALUE);  // we must decay!

    if (Is_Light_Null(v))
        return LOGIC_OUT(true);

    if (Is_Heavy_Null(v))
        return fail (
            "Heavy null detected by NULL?, use NOT or NULL? DECAY if meant"
       );

    Ensure_No_Failures_Including_In_Packs(v) except (Error* e) {
        if (Get_Level_Flag(LEVEL, RUNNING_TYPECHECK))
            return LOGIC_OUT(false);  // don't panic on undecayable in typecheck
        panic (e);
    }

    return LOGIC_OUT(false);
}


//
//  /okay?: pure native:intrinsic [
//
//  "Tells you if the argument is an ~okay~ antiform (canon branch trigger)"
//
//      return: [logic!]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(OKAY_Q)
{
    INCLUDE_PARAMS_OF_OKAY_Q;

    Stable* v = ARG(VALUE);

    return LOGIC_OUT(Is_Okay(v));
}


//
//  /int-to-logic: native [
//
//  "Produces ~null~ antiform for 0, or ~okay~ antiform for all other integers"
//
//      return: [logic!]
//      value [integer!]
//  ]
//
DECLARE_NATIVE(INT_TO_LOGIC)
{
    INCLUDE_PARAMS_OF_INT_TO_LOGIC;

    Element* v = Element_ARG(VALUE);
    return LOGIC_OUT(VAL_INT64(v) != 0);
}


//
//  /boolean?: pure native:intrinsic [
//
//  "Tells you if the argument is the TRUE or FALSE word"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(BOOLEAN_Q)
{
    INCLUDE_PARAMS_OF_BOOLEAN_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_Boolean(v));
}


//
//  /onoff?: pure native:intrinsic [
//
//  "Tells you if the argument is the ON or OFF word"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(ONOFF_Q)
{
    INCLUDE_PARAMS_OF_ONOFF_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_OnOff(v));
}


//
//  /yesno?: pure native:intrinsic [
//
//  "Tells you if the argument is the YES or NO word"
//
//      return: [logic!]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(YESNO_Q)
{
    INCLUDE_PARAMS_OF_YESNO_Q;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    return LOGIC_OUT(Is_YesNo(v));
}


//
//  /true?: pure native [
//
//  "Tests if word is the word TRUE (errors if not TRUE or FALSE)"
//
//      return: [logic!]
//      word [~[true false]~]
//  ]
//
DECLARE_NATIVE(TRUE_Q)
{
    INCLUDE_PARAMS_OF_TRUE_Q;

    return LOGIC_OUT(Word_Id(ARG(WORD)) == SYM_TRUE);
}


//
//  /false?: pure native [
//
//  "Tests if value is the word FALSE (errors if not TRUE or FALSE)"
//
//      return: [logic!]
//      word [~[true false]~]
//  ]
//
DECLARE_NATIVE(FALSE_Q)
{
    INCLUDE_PARAMS_OF_FALSE_Q;

    return LOGIC_OUT(Word_Id(ARG(WORD)) == SYM_FALSE);
}


//
//  /boolean: native [
//
//  "The word TRUE if the condition is a branch trigger, otherwise FALSE"
//
//      return: [~[true false]~]
//      value [any-stable?]
//  ]
//
DECLARE_NATIVE(BOOLEAN)
{
    INCLUDE_PARAMS_OF_BOOLEAN;

    bool logic = Logical_Test(ARG(VALUE));

    return Init_Word(OUT, logic ? CANON(TRUE) : CANON(FALSE));
}


//
//  /yes?: pure native [
//
//  "Tests if word is the word YES (errors if not YES or NO)"
//
//      return: [logic!]
//      word [~[yes no]~]
//  ]
//
DECLARE_NATIVE(YES_Q)
{
    INCLUDE_PARAMS_OF_YES_Q;

    return LOGIC_OUT(Word_Id(ARG(WORD)) == SYM_YES);
}


//
//  /no?: pure native [
//
//  "Tests if value is the word NO (errors if not YES or NO)"
//
//      return: [logic!]
//      word [~[yes no]~]
//  ]
//
DECLARE_NATIVE(NO_Q)
{
    INCLUDE_PARAMS_OF_NO_Q;

    return LOGIC_OUT(Word_Id(ARG(WORD)) == SYM_NO);
}


//
//  /to-yesno: native [
//
//  "The word YES if the condition is a branch trigger, otherwise NO"
//
//      return: [~[yes no]~]
//      condition [any-stable?]
//  ]
//
DECLARE_NATIVE(TO_YESNO)
{
    INCLUDE_PARAMS_OF_TO_YESNO;

    bool logic = Logical_Test(ARG(CONDITION));

    return Init_Word(OUT, logic ? CANON(YES) : CANON(NO));
}


//
//  /on?: pure native [
//
//  "Tests if word is the word ON (errors if not ON or OFF)"
//
//      return: [logic!]
//      word [~[on off]~]
//  ]
//
DECLARE_NATIVE(ON_Q)
{
    INCLUDE_PARAMS_OF_ON_Q;

    return LOGIC_OUT(Word_Id(ARG(WORD)) == SYM_ON);
}


//
//  /off?: pure native [
//
//  "Tests if value is the word OFF (errors if not ON or OFF)"
//
//      return: [logic!]
//      word [~[on off]~]
//  ]
//
DECLARE_NATIVE(OFF_Q)
{
    INCLUDE_PARAMS_OF_NO_Q;

    return LOGIC_OUT(Word_Id(ARG(WORD)) == SYM_OFF);
}


//
//  /to-onoff: native [
//
//  "The word ON if the condition is a branch trigger, otherwise OFF"
//
//      return: [~[on off]~]
//      condition [any-stable?]
//  ]
//
DECLARE_NATIVE(TO_ONOFF)
{
    INCLUDE_PARAMS_OF_TO_ONOFF;

    bool logic = Logical_Test(ARG(CONDITION));

    return Init_Word(OUT, logic ? CANON(ON) : CANON(OFF));
}


//
//  /and?: pure native [
//
//  "Returns true if both values are conditionally true (no 'short-circuit')"
//
//      return: [logic!]
//      value1 [any-stable?]
//      value2 [any-stable?]
//  ]
//
DECLARE_NATIVE(AND_Q)
{
    INCLUDE_PARAMS_OF_AND_Q;

    bool logic1 = Logical_Test(ARG(VALUE1));
    bool logic2 = Logical_Test(ARG(VALUE2));

    if (logic1 and logic2)
        return LOGIC_OUT(true);

    return LOGIC_OUT(false);
}


//
//  /or?: pure native [
//
//  "Returns true if both values are conditionally false (no 'short-circuit')"
//
//      return: [logic!]
//      value1 [any-stable?]
//      value2 [any-stable?]
//  ]
//
DECLARE_NATIVE(OR_Q)
{
    INCLUDE_PARAMS_OF_OR_Q;

    bool logic1 = Logical_Test(ARG(VALUE1));
    bool logic2 = Logical_Test(ARG(VALUE2));

    if (logic1 or logic2)
        return LOGIC_OUT(true);

    return LOGIC_OUT(false);
}


//
//  /null-if-zero: pure native [
//
//  "Null if the integer input is a zero"
//
//      return: [logic!]
//      integer [integer!]
//  ]
//
DECLARE_NATIVE(NULL_IF_ZERO)
{
    INCLUDE_PARAMS_OF_NULL_IF_ZERO;

    return LOGIC_OUT(VAL_INT64(ARG(INTEGER)) != 0);
}


//
//  /not: pure native:intrinsic [
//
//  "Returns the logic complement (inverts the nullness of what's passed in)"
//
//      return: [logic!]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(NOT_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_NOT_1;

    Stable* v = ARG(VALUE);

    bool logic = Logical_Test(v);

    return LOGIC_OUT(not logic);
}


//
//  /to-logic: native:intrinsic [
//
//  "Returns logic of what's given (null if null, okay for everything else)"
//
//      return: [logic!]
//      value '[any-stable?]
//  ]
//
DECLARE_NATIVE(TO_LOGIC)
{
    INCLUDE_PARAMS_OF_TO_LOGIC;

    Stable* v = ARG(VALUE);

    bool logic = Logical_Test(v);

    return LOGIC_OUT(logic);
}


// The handling of logic has gone through several experiments, some of which
// made it more like a branching structure (so able to pass the result of the
// left hand side to the right).  There was also behavior for GET-GROUP!, to
// run the provided code whether the condition on the left was true or not.
//
// This scales the idea back to a very simple concept of a literal GROUP!,
// WORD!, or TUPLE!.
//
INLINE bool Eval_Logic_Op_Right_Side_Uses_Scratch_And_Out(
    Level* level_
){
    INCLUDE_PARAMS_OF_AND_1;  // should be same as OR and XOR

    Element* right = ARG(RIGHT);
    STATIC_ASSERT(  // !!! temporary until flagging mechanism rethought
        CELL_FLAG_SCRATCH_VAR_NOTE_ONLY_ACTION
        == CELL_FLAG_PARAM_NOTE_TYPECHECKED
    );
    Clear_Cell_Flag(right, PARAM_NOTE_TYPECHECKED);

    if (Is_Group(right)) {
        if (Eval_Any_List_At_Throws(OUT, right, SPECIFIED))
            panic (Error_No_Catch_For_Throw(level_));
    }
    else {
        heeded (Corrupt_Cell_If_Needful(SPARE));
        heeded (Corrupt_Cell_If_Needful(SCRATCH));

        assert(STATE == STATE_0);
        STATE = ST_TWEAK_GETTING;

        if (Is_Chain(right)) {
            assume (
              Unsingleheart_Sequence(right)
            );
            Add_Cell_Sigil(right, SIGIL_META);  // ACTION!s ok
        }

        require (
          Get_Var_To_Out_Use_Toplevel(right, GROUP_EVAL_NO)
        );

        if (Is_Chain(right)) {
            if (Not_Cell_Stable(OUT)) {
                if (Is_Void(OUT) or Is_Trash(OUT))
                    Init_Null(OUT);
                else if (Is_Action(OUT))
                    Deactivate_Action(OUT);
                else
                    panic (
                        ":WORD! or :TUPLE! unstable not void/trash/action"
                    );
            }
        }
        else if ((Is_Word(right) or Is_Tuple(right)) and Is_Action(OUT))
            panic (
                "words/tuples can't be action as right side of OR AND XOR"
            );
    }

    require (
      Stable* stable_out = Decay_If_Unstable(OUT)
    );

    return Logical_Test(stable_out);
}


//
//  /and: infix native [
//
//  "Boolean AND, right hand side must be in GROUP! to allow short-circuit"
//
//      return: [logic!]
//      left [any-stable?]
//      @right "Right is evaluated if left is true"
//          [group! word! tuple! ^word! ^tuple! :word! :tuple!]
//  ]
//
DECLARE_NATIVE(AND_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_AND_1;

    bool left = Logical_Test(ARG(LEFT));
    if (not left)
        return LOGIC_OUT(false);  // if left is false, don't run right

    bool right = Eval_Logic_Op_Right_Side_Uses_Scratch_And_Out(LEVEL);

    return LOGIC_OUT(right);
}


//
//  /or: infix native [
//
//  "Boolean OR, right hand side must be in GROUP! to allow short-circuit"
//
//      return: [logic!]
//      left [any-stable?]
//      @right "Right is evaluated if left is false"
//          [group! word! tuple! ^word! ^tuple! :word! :tuple!]
//  ]
//
DECLARE_NATIVE(OR_1)  // see TO-C-NAME
{
    INCLUDE_PARAMS_OF_OR_1;

    bool left = Logical_Test(ARG(LEFT));
    if (left)
        return LOGIC_OUT(true);  // if left is true, don't run right

    bool right = Eval_Logic_Op_Right_Side_Uses_Scratch_And_Out(LEVEL);

    return LOGIC_OUT(right);
}


//
//  /xor: infix native [
//
//  "Boolean XOR (operation cannot be short-circuited)"
//
//      return: [logic!]
//      left [any-stable?]
//      @right "Always evaluated"  ; [1]
//          [group! word! tuple! ^word! ^tuple! :word! :tuple!]
//  ]
//
DECLARE_NATIVE(XOR_1)  // see TO-C-NAME
//
// 1. XOR has to always evaluate its right hand side.
{
    INCLUDE_PARAMS_OF_XOR_1;

    bool right = Eval_Logic_Op_Right_Side_Uses_Scratch_And_Out(LEVEL);  // [1]

    bool left = Logical_Test(ARG(LEFT));
    if (not left)
        return LOGIC_OUT(right);

    return LOGIC_OUT(not right);
}


//
//  /unless: infix native [
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

    Stable* left = ARG(LEFT);
    Value* right = ARG(RIGHT);

    if (Any_Void(right))
        panic ("UNLESS can't be used with VOID");

    if (Is_Light_Null(right) or Is_Heavy_Null(right))
        return COPY_TO_OUT(left);

    return COPY_TO_OUT(right);  // preserve packs
}
