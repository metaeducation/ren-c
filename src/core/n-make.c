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
//  Makehook_Fail: C
//
Bounce Makehook_Fail(Level* level_, Kind kind, Element* arg) {
    UNUSED(kind);
    UNUSED(arg);

    return RAISE("Datatype does not have a MAKE handler registered");
}


//
//  Makehook_Unhooked: C
//
// MAKE STRUCT! is part of the FFI extension, but since user defined types
// aren't ready yet as a general concept, this hook is overwritten in the
// dispatch table when the extension loads.
//
Bounce Makehook_Unhooked(Level* level_, Kind kind, Element* arg) {
    UNUSED(arg);

    const Value* type = Datatype_From_Kind(kind);
    UNUSED(type); // !!! put in error message?

    return RAISE(
        "Datatype is provided by an extension that's not currently loaded"
    );
}


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
    Element* type = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(type, LEVEL, Canon(MAKE));
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
//          [<maybe> element? action?]
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
        return Run_Generic_Dispatch(cast(Element*, v), LEVEL, Canon(COPY));

    Byte quote_byte = QUOTE_BYTE(v);
    QUOTE_BYTE(v) = NOQUOTE_1;

    Option(const Symbol*) label = level_->label;
    Option(VarList*) coupling = Level_Coupling(level_);

    Level* sub = Push_Downshifted_Level(OUT, level_);

    level_->executor = &Copy_Quoter_Executor;

    assert(Not_Level_Flag(sub, TRAMPOLINE_KEEPALIVE));
    assert(Get_Executor_Flag(ACTION, sub, IN_DISPATCH));

    Phase* phase = cast(Phase*, VAL_ACTION(Lib(COPY)));
    Tweak_Level_Phase(sub, phase);
    Tweak_Level_Coupling(sub, coupling);

    sub->u.action.original = VAL_ACTION(Lib(COPY));
    sub->label = label;

  #if RUNTIME_CHECKS
    sub->label_utf8 = label
        ? String_UTF8(unwrap label)
        : "(anonymous)";
  #endif

    if (quote_byte == ANTIFORM_0)
        STATE = NOQUOTE_1;  // 0 state is reserved
    else
        STATE = quote_byte;

    return BOUNCE_DOWNSHIFTED;
}


#if RUNTIME_CHECKS

#define CELL_FLAG_SPARE_NOTE_REVERSE_CHECKING CELL_FLAG_NOTE

//
//  To_Checker_Executor: C
//
Bounce To_Checker_Executor(Level* const L)
{
    Heart to = cast(Heart, Level_State_Byte(L));
    assert(to != REB_0);

    Element* input = cast(Element*, Level_Spare(L));
    Heart from = Cell_Heart_Ensure_Noquote(input);

    Atom* reverse = cast(Atom*, &L->u.eval.current);

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
    assert(VAL_TYPE(OUT) == to);

    // Reset TO_P sublevel to do reverse transformation

    level_->executor = &Action_Executor;  // Drop_Action() nulled it
    Push_Action(level_, VAL_ACTION(Lib(TO)), nullptr);
    Begin_Action(level_, Canon(TO), PREFIX_0);
    Set_Executor_Flag(ACTION, level_, IN_DISPATCH);

    INCLUDE_PARAMS_OF_TO;
    Erase_Cell(ARG(return));
    Erase_Cell(ARG(type));
    Erase_Cell(ARG(element));

    Init_Nulled(ARG(return));
    Copy_Cell(ARG(type), Datatype_From_Kind(from));
    Copy_Cell(ARG(element), cast(Element*, stable_OUT));
    STATE = STATE_0;
    level_->executor = &Action_Executor;
    Phase* phase = cast(Phase*, VAL_ACTION(Lib(TO)));
    Tweak_Level_Phase(level_, phase);
    Tweak_Level_Coupling(level_, nullptr);

    Option(const Symbol*) label = Canon(TO);
    level_->u.action.original = VAL_ACTION(Lib(TO));
    level_->label = label;
    level_->label_utf8 = label
        ? String_UTF8(unwrap label)
        : "(anonymous)";

    assert(Get_Level_Flag(level_, TRAMPOLINE_KEEPALIVE));
    Clear_Level_Flag(level_, TRAMPOLINE_KEEPALIVE);

    Set_Cell_Flag(Level_Spare(L), SPARE_NOTE_REVERSE_CHECKING);
    level_->out = reverse;  // don't overwrite OUT
    return CATCH_CONTINUE_SUBLEVEL(level_);  // wasn't action, no DOWNSHIFT

} ensure_results_equal: {  ///////////////////////////////////////////////////

    USE_LEVEL_SHORTHANDS (L);  // didn't need to keepalive reverse sublevel

    if (THROWING)
        return BOUNCE_THROWN;

    if (Is_Raised(reverse))
        return FAIL(Cell_Error(reverse));

    Decay_If_Unstable(reverse);  // should packs from TO be legal?

    if (to == REB_MAP) {  // doesn't preserve order requirement :-/
        if (VAL_TYPE(cast(Value*, reverse)) != VAL_TYPE(input))
            return FAIL("Reverse TO of MAP! didn't produce original type");
        return OUT;
    }

    Push_Lifeguard(reverse);  // was guarded as level_->OUT, but no longer
    bool equal_reversal = rebUnboxLogic(
        Canon(EQUAL_Q), rebQ(input), rebQ(cast(Value*, reverse))
    );
    Drop_Lifeguard(reverse);

    if (not equal_reversal)
        return FAIL("Reverse TO transform didn't produce original result");

    if (to == from) {
        bool equal_copy = rebUnboxLogic(
            Canon(EQUAL_Q), rebQ(input), Canon(COPY), rebQ(input)
        );
        if (not equal_copy)
            return FAIL("Reverse TO transform to equal type not same as COPY");
    }

    return OUT;
}}

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

    Element* e = cast(Element*, ARG(element));

  #if NO_RUNTIME_CHECKS

    UNUSED(ARG(type));
    return Run_Generic_Dispatch(e, TOP_LEVEL, Canon(TO));

  #else  // add monitor to ensure result is right

    if (level_->prior->executor == &To_Checker_Executor)
        return Run_Generic_Dispatch(e, TOP_LEVEL, Canon(TO));

    Option(const Symbol*) label = level_->label;
    Option(VarList*) coupling = Level_Coupling(level_);

    Element* type = cast(Element*, ARG(type));
    Heart to = VAL_TYPE_HEART(type);
    STATE = to;  // generic code may trash TYPE when it runs, save the Heart
    Copy_Cell(Level_Spare(level_), e);  // may trash ELEMENT, save in SPARE

    Level* sub = Push_Downshifted_Level(OUT, level_);

    assert(Not_Level_Flag(sub, TRAMPOLINE_KEEPALIVE));
    Set_Level_Flag(sub, TRAMPOLINE_KEEPALIVE);

    level_->executor = &To_Checker_Executor;

    Phase* phase = cast(Phase*, VAL_ACTION(Lib(TO)));
    Tweak_Level_Phase(sub, phase);
    Tweak_Level_Coupling(sub, coupling);

    sub->u.action.original = VAL_ACTION(Lib(TO));
    sub->label = label;
    sub->label_utf8 = label
        ? String_UTF8(unwrap label)
        : "(anonymous)";

    return BOUNCE_DOWNSHIFTED;  // avoids trampoline, action executor updates L
  #endif
}
