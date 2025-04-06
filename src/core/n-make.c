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
//  make: native:generic [
//
//  "Constructs or allocates the specified datatype"
//
//      return: [element?]
//      type "The datatype or parent context to construct from"
//          [<maybe> datatype! any-context?]
//      def "Definition or size of the new value (binding may be modified)"
//          [<maybe> <unrun> element?]  ; <unrun> action for FRAME!
//  ]
//
DECLARE_NATIVE(MAKE)
{
    INCLUDE_PARAMS_OF_MAKE;

    Value* type = ARG(TYPE);  // !!! may not be datatype, but parent context
    USED(ARG(DEF));  // delegated to generic

    const Value* datatype;
    if (Is_Datatype(type)) {
        datatype = type;
    }
    else {
        datatype = Datatype_Of_Fundamental(type);
    }

    return Dispatch_Generic_Core(
        SYM_MAKE,
        &GENERIC_TABLE(MAKE),
        datatype,
        LEVEL
    );
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
//  copy: native:generic [
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
DECLARE_NATIVE(COPY)
//
// 1. R3-Alpha and Red limit COPY to series, object, or function.  Ren-C had
//    the idea that COPY should be able to copy any value, but the merits of
//    that being meaningful are actually questionable.  The old dispatch
//    model had many types doing trivial cell copies and failing if you
//    passed a :PART but ignoring :DEEP... rather than writing those trivial
//    generic handlers all over the place for now we make that the fallback
//    for all types that don't have a specific handler registered.  This may
//    well be taken out and just error if there's not a non-trivial copy.
{
    INCLUDE_PARAMS_OF_COPY;

    Element* elem = Element_ARG(VALUE);

    QuoteByte quote_byte = QUOTE_BYTE(elem);
    QUOTE_BYTE(elem) = NOQUOTE_1;  // dispatch requires unquoted items

    Option(Dispatcher*) dispatcher = Get_Generic_Dispatcher(
        &GENERIC_TABLE(COPY),
        Datatype_Of(elem)
    );

    if (not dispatcher) {  // trivial copy, is it good to do so? [1]
        if (Bool_ARG(PART))
            return FAIL(Error_Bad_Refines_Raw());

        UNUSED(Bool_ARG(DEEP));  // historically we ignore it

        QUOTE_BYTE(elem) = quote_byte;  // restore quote byte
        return COPY(elem);
    }

    if (quote_byte == NOQUOTE_1)  // don't have to requote/etc.
        return Apply_Cfunc(unwrap dispatcher, LEVEL);

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
    Heart to_or_as = u_cast(HeartEnum, LEVEL_STATE_BYTE(L));
    assert(to_or_as != TYPE_0);

    Element* input = cast(Element*, Level_Spare(L));
    Heart from = Heart_Of_Builtin_Fundamental(input);

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

    if (Heart_Of_Fundamental(OUT) != to_or_as)
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
    Erase_Cell(ARG(TYPE));
    Erase_Cell(ARG(ELEMENT));

    Copy_Cell(ARG(TYPE), Datatype_From_Type(from));
    Copy_Cell(ARG(ELEMENT), cast(Element*, stable_OUT));
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

    if (to_or_as == TYPE_MAP) {  // doesn't preserve order requirement :-/
        if (Type_Of(cast(Value*, reverse)) != Type_Of(input))
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

    Value* datatype = ARG(TYPE);
    STATE = cast(Byte, Cell_Datatype_Builtin_Heart(datatype));  // might trash
    Copy_Cell(SPARE, ARG(ELEMENT));  // may trash ELEMENT too, save in SPARE

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
//  to: native:generic [
//
//  "Converts to a specified datatype, copying any underying data"
//
//      return: "ELEMENT converted to TYPE (copied if same type as ELEMENT)"
//          [element?]
//      type [<maybe> datatype!]
//      element [<maybe> fundamental? datatype!]
//  ]
//
DECLARE_NATIVE(TO)
{
    INCLUDE_PARAMS_OF_TO;

    Option(Type) to = Cell_Datatype_Type(ARG(TYPE));
    if (not to)
        return FAIL("TO doesn't work with extension types");
    if (unwrap to > MAX_HEART)
        return FAIL("TO can't produce quoted/quasiform/antiform");

    if (Is_Datatype(ARG(ELEMENT))) {  // do same coercions as WORD!
        Value* datatype = ARG(ELEMENT);
        Option(Type) type = Cell_Datatype_Type(datatype);
        if (not type)
            return FAIL("TO doesn't work with extension types");

        SymId id = Symbol_Id_From_Type(unwrap type);
        Init_Word(ARG(ELEMENT), Canon_Symbol(id));
    }

    Element* e = Element_ARG(ELEMENT);

  #if NO_RUNTIME_CHECKS

    Bounce bounce = Dispatch_Generic(TO, e, LEVEL);
    /*if (bounce == UNHANDLED)  // distinct error for AS or TO ?
        return Error_Bad_Cast_Raw(ARG(ELEMENT), ARG(TYPE)); */
    return bounce;

  #else  // add monitor to ensure result is right

    if (LEVEL->prior->executor == &To_Or_As_Checker_Executor)
        return Dispatch_Generic(TO, e, LEVEL);

    assert(Not_Level_Flag(LEVEL, CHECKING_TO));
    Set_Level_Flag(LEVEL, CHECKING_TO);
    return Downshift_For_To_Or_As_Checker(LEVEL);
  #endif
}


//
//  as: native:generic [
//
//  "Aliases underlying data of one value to act as another of same class"
//
//      return: [~null~ fundamental?]
//      type [datatype!]
//      element [<maybe> fundamental?]
//  ]
//
DECLARE_NATIVE(AS)
{
    INCLUDE_PARAMS_OF_AS;

    Element* e = Element_ARG(ELEMENT);
    Option(Type) as = Cell_Datatype_Type(ARG(TYPE));
    if (not as)
        return FAIL("TO doesn't work with extension types");
    if ((unwrap as) > MAX_HEART)
        return FAIL("AS can't alias to quoted/quasiform/antiform");

  #if NO_RUNTIME_CHECKS

    return Dispatch_Generic(AS, e, LEVEL);

  #else  // add monitor to ensure result is right

    if (LEVEL->prior->executor == &To_Or_As_Checker_Executor)
        return Dispatch_Generic(AS, e, TOP_LEVEL);

    assert(Not_Level_Flag(LEVEL, CHECKING_TO));
    return Downshift_For_To_Or_As_Checker(LEVEL);
  #endif
}
