//
//  File: %n-make.c
//  Summary: "Creation-oriented natives (MAKE, TO, COPY)"
//  Section: natives
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
//  /make: native:generic [
//
//  "Constructs or allocates the specified datatype"
//
//      return: [element?]
//      type "The datatype or parent context to construct from"
//          [<maybe> type-block! any-context?]
//      def "Definition or size of the new value (binding may be modified)"
//          [<maybe> <unrun> element?]  ; <unrun> action for FRAME!
//  ]
//
DECLARE_NATIVE(make)
{
    INCLUDE_PARAMS_OF_MAKE;

    Element* type = Element_ARG(type);
    UNUSED(ARG(def));

    return Dispatch_Generic(make, type, LEVEL);
}


//
//  Copy_Quoter_Executor: C
//
Bounce Copy_Quoter_Executor(Level* level_)
{
    if (STATE == NOQUOTE_1)  // actually means antiform
        QUOTE_BYTE(OUT) = ANTIFORM_0_COERCE_ONLY;
    else
        QUOTE_BYTE(OUT) = STATE;

    return OUT;
}


//
//  /copy: native:generic [
//
//  "Copies a series, object, or other value"
//
//      return: "Return type will match the input type"
//          [any-value?]
//      value "If an ANY-SERIES?, it is only copied from its current position"
//          [<maybe> element?]
//      :part "Limits to a given length or position"
//          [any-number? any-series? pair!]
//      :deep "Also copies series values within the block"
//      ; Once had :TYPES, but that is disabled for now
//  ]
//
DECLARE_NATIVE(copy)
{
    Value* v = ARG_N(1);

    if (QUOTE_BYTE(v) == NOQUOTE_1)  // don't have to requote/etc.
        return Run_Generic_Dispatch(cast(Element*, v), LEVEL, CANON(COPY));

    Byte quote_byte = QUOTE_BYTE(v);
    QUOTE_BYTE(v) = NOQUOTE_1;

    Option(const Symbol*) label = Level_Label(level_);
    Option(VarList*) coupling = Level_Coupling(level_);

    Level* sub = Push_Downshifted_Level(OUT, level_);

    level_->executor = &Copy_Quoter_Executor;

    assert(Not_Level_Flag(sub, TRAMPOLINE_KEEPALIVE));
    assert(Get_Executor_Flag(ACTION, sub, IN_DISPATCH));

    Phase* phase = Cell_Frame_Phase(LIB(COPY));
    Tweak_Level_Phase(sub, phase);
    Tweak_Level_Coupling(sub, coupling);

    sub->u.action.original = Cell_Frame_Phase(LIB(COPY));
    Set_Action_Level_Label(sub, label);

    if (quote_byte == ANTIFORM_0)
        STATE = NOQUOTE_1;  // 0 state is reserved
    else
        STATE = quote_byte;

    return BOUNCE_DOWNSHIFTED;
}


#if RUNTIME_CHECKS

#define CELL_FLAG_SPARE_NOTE_REVERSE_CHECKING CELL_FLAG_NOTE

#define LEVEL_FLAG_CHECKING_TO  LEVEL_FLAG_MISCELLANEOUS

//
//  To_Or_As_Checker_Executor: C
//
Bounce To_Or_As_Checker_Executor(Level* const L)
{
    Heart to_or_as = cast(Heart, LEVEL_STATE_BYTE(L));
    assert(to_or_as != REB_0);

    Element* input = cast(Element*, Level_Spare(L));
    Heart from = Cell_Heart_Ensure_Noquote(input);

    Atom* reverse = Level_Scratch(L);

    if (Get_Cell_Flag(Level_Spare(L), SPARE_NOTE_REVERSE_CHECKING))
        goto ensure_results_equal;

    Erase_Cell(reverse);
    goto check_type_and_run_reverse_to;

  check_type_and_run_reverse_to: {  //////////////////////////////////////////

    if (Is_Throwing(L)) {
        assert(L == TOP_LEVEL);  // sublevel automatically dropped
        return BOUNCE_THROWN;
    }

    Level* level_ = TOP_LEVEL;  // sublevel stole the varlist
    assert(level_->prior == L);

    if (Is_Raised(OUT)) {
        Drop_Level(level_);
        return OUT;
    }

    Decay_If_Unstable(OUT);  // should packs from TO be legal?

    if (VAL_TYPE(OUT) != to_or_as)
        return FAIL("Forward TO/AS transform produced wrong type");

    if (
        Get_Level_Flag(L, CHECKING_TO)
        and (Any_List(OUT) or Any_String(OUT) or Is_Blob(OUT))
    ){
        if (Is_Flex_Read_Only(Cell_Flex(OUT)))
            fail ("TO transform of LIST/STRING/BLOB made immutable series");
    }

    // Reset TO_P sublevel to do reverse transformation

    level_->executor = &Action_Executor;  // Drop_Action() nulled it
    SymId id = Get_Level_Flag(L, CHECKING_TO) ? SYM_TO : SYM_AS;
    Push_Action(level_, Lib_Var(id));
    Begin_Action(level_, Canon_Symbol(id), PREFIX_0);
    Set_Executor_Flag(ACTION, level_, IN_DISPATCH);

    INCLUDE_PARAMS_OF_TO;
    Erase_Cell(ARG(type));
    Erase_Cell(ARG(element));

    Copy_Cell(ARG(type), Datatype_From_Kind(from));
    Copy_Cell(ARG(element), cast(Element*, stable_OUT));
    STATE = STATE_0;

    assert(Get_Level_Flag(level_, TRAMPOLINE_KEEPALIVE));
    Clear_Level_Flag(level_, TRAMPOLINE_KEEPALIVE);

    Set_Cell_Flag(Level_Spare(L), SPARE_NOTE_REVERSE_CHECKING);
    level_->out = reverse;  // don't overwrite OUT
    return CONTINUE_SUBLEVEL(level_);  // wasn't action, no DOWNSHIFT

} ensure_results_equal: {  ///////////////////////////////////////////////////

    USE_LEVEL_SHORTHANDS (L);  // didn't need to keepalive reverse sublevel

    if (THROWING)
        return BOUNCE_THROWN;

    if (Is_Raised(reverse))
        return FAIL(Cell_Error(reverse));

    Decay_If_Unstable(reverse);  // should packs from TO be legal?

    if (to_or_as == REB_MAP) {  // doesn't preserve order requirement :-/
        if (VAL_TYPE(cast(Value*, reverse)) != VAL_TYPE(input))
            return FAIL("Reverse TO/AS of MAP! didn't produce original type");
        return OUT;
    }

    Push_Lifeguard(reverse);  // was guarded as level_->OUT, but no longer
    bool equal_reversal = rebUnboxLogic(
        CANON(EQUAL_Q), rebQ(input), rebQ(cast(Value*, reverse))
    );
    Drop_Lifeguard(reverse);

    if (not equal_reversal)
        return FAIL("Reverse TO/AS transform didn't produce original result");

    if (to_or_as == from and Get_Level_Flag(L, CHECKING_TO)) {
        bool equal_copy = rebUnboxLogic(
            CANON(EQUAL_Q), rebQ(input), CANON(COPY), rebQ(input)
        );
        if (not equal_copy)
            return FAIL("Reverse TO/AS transform not same as COPY");
    }

    return OUT;
}}

static Bounce Downshift_For_To_Or_As_Checker(Level *level_) {
    INCLUDE_PARAMS_OF_TO;  // frame compatible with AS

    Option(const Symbol*) label = Level_Label(level_);

    Element* type = Element_ARG(type);
    STATE = VAL_TYPE_HEART(type);  // generic code may trash TYPE when it runs
    Copy_Cell(SPARE, ARG(element));  // may trash ELEMENT too, save in SPARE

    Level* sub = Push_Downshifted_Level(OUT, level_);

    assert(Not_Level_Flag(sub, TRAMPOLINE_KEEPALIVE));
    Set_Level_Flag(sub, TRAMPOLINE_KEEPALIVE);

    level_->executor = &To_Or_As_Checker_Executor;

    SymId id = Get_Level_Flag(level_, CHECKING_TO) ? SYM_TO : SYM_AS;

    sub->u.action.original = Cell_Frame_Phase(Lib_Var(id));
    Set_Action_Level_Label(sub, label);

    return BOUNCE_DOWNSHIFTED;  // avoids trampoline, action executor updates L
}

#endif


//
//  /to: native:generic [
//
//  "Converts to a specified datatype, copying any underying data"
//
//      return: "ELEMENT converted to TYPE (copied if same type as ELEMENT)"
//          [element?]
//      type [<maybe> type-block!]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(to)
{
    INCLUDE_PARAMS_OF_TO;

    Element* e = Element_ARG(element);
    UNUSED(ARG(type));

  #if NO_RUNTIME_CHECKS

    Bounce bounce = Run_Generic_Dispatch(e, TOP_LEVEL, CANON(TO));
    /*if (bounce == UNHANDLED)  // distinct error for AS or TO ?
        return Error_Bad_Cast_Raw(ARG(element), ARG(type)); */
    return bounce;

  #else  // add monitor to ensure result is right

    if (LEVEL->prior->executor == &To_Or_As_Checker_Executor)
        return Run_Generic_Dispatch(e, TOP_LEVEL, CANON(TO));

    assert(Not_Level_Flag(LEVEL, CHECKING_TO));
    Set_Level_Flag(LEVEL, CHECKING_TO);
    return Downshift_For_To_Or_As_Checker(level_);
  #endif
}


//
//  /as: native:generic [
//
//  "Aliases underlying data of one value to act as another of same class"
//
//      return: [
//          ~null~ integer!
//          any-sequence? any-series? any-word? any-utf8?
//          frame!
//          blank!
//      ]
//      type [type-block!]
//      element [
//          <maybe>
//          integer!
//          any-sequence? any-series? any-word? any-utf8?
//          frame!
//          blank!
//      ]
//  ]
//
DECLARE_NATIVE(as)
{
    INCLUDE_PARAMS_OF_AS;

    Element* e = Element_ARG(element);
    Kind as = VAL_TYPE_KIND(ARG(type));
    if (as >= REB_MAX_HEART)
        return FAIL("AS can't alias to quoted/quasiform/antiform");

  #if NO_RUNTIME_CHECKS

    UNUSED(ARG(type));
    return Run_Generic_Dispatch(e, TOP_LEVEL, CANON(AS));

  #else  // add monitor to ensure result is right

    if (LEVEL->prior->executor == &To_Or_As_Checker_Executor)
        return Run_Generic_Dispatch(e, TOP_LEVEL, CANON(AS));

    assert(Not_Level_Flag(LEVEL, CHECKING_TO));
    return Downshift_For_To_Or_As_Checker(LEVEL);
  #endif
}
