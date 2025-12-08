//
//  file: %n-make.c
//  summary: "Creation-oriented natives (MAKE, TO, COPY)"
//  section: natives
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
//  make: native:generic [
//
//  "Constructs or allocates the specified datatype"
//
//      return: [element?]
//      type "The datatype or parent context to construct from"
//          [<opt-out> datatype! any-context?]
//      def "Definition or size of the new value (binding may be modified)"
//          [<opt-out> <unrun> element?]  ; <unrun> action for FRAME!
//  ]
//
DECLARE_NATIVE(MAKE)
{
    INCLUDE_PARAMS_OF_MAKE;

    Stable* type = ARG(TYPE);  // !!! may not be datatype, but parent context
    USED(ARG(DEF));  // delegated to generic

    const Stable* datatype;
    if (Is_Datatype(type)) {
        datatype = type;
    }
    else {
        datatype = Datatype_Of_Fundamental(Known_Element(type));
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
    if (STATE == NOQUOTE_2)  // actually means antiform
        LIFT_BYTE_RAW(OUT) = ANTIFORM_1;
    else
        LIFT_BYTE_RAW(OUT) = STATE;

    return OUT;
}


//
//  copy: native:generic [
//
//  "Copies a series, object, or other value; return value of same type"
//
//      return: [any-stable?]
//      value "If an ANY-SERIES?, it is only copied from its current position"
//          [<opt-out> element?]
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

    LiftByte lift_byte = LIFT_BYTE(elem);
    LIFT_BYTE(elem) = NOQUOTE_2;  // dispatch requires unquoted items

    Option(Dispatcher*) dispatcher = Get_Generic_Dispatcher(
        &GENERIC_TABLE(COPY),
        Datatype_Of(elem)
    );

    if (not dispatcher) {  // trivial copy, is it good to do so? [1]
        if (Bool_ARG(PART))
            panic (Error_Bad_Refines_Raw());

        UNUSED(Bool_ARG(DEEP));  // historically we ignore it

        LIFT_BYTE(elem) = lift_byte;  // restore
        return COPY(elem);
    }

    if (lift_byte == NOQUOTE_2)  // don't have to requote/etc.
        return Apply_Cfunc(unwrap dispatcher, LEVEL);

    Option(const Symbol*) label = Level_Label(level_);
    Option(VarList*) coupling = Level_Coupling(level_);

    Level* sub = Push_Downshifted_Level(OUT, level_);

    level_->executor = &Copy_Quoter_Executor;

    assert(Not_Level_Flag(sub, TRAMPOLINE_KEEPALIVE));
    assert(Get_Executor_Flag(ACTION, sub, IN_DISPATCH));

    Phase* phase = Frame_Phase(LIB(COPY));
    Tweak_Level_Phase(sub, phase);
    Tweak_Level_Coupling(sub, coupling);

    sub->u.action.original = Frame_Phase(LIB(COPY));
    Set_Action_Level_Label(sub, label);

    if (lift_byte == ANTIFORM_1)
        STATE = NOQUOTE_2;  // 0 state is reserved
    else
        STATE = lift_byte;

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

    Element* spare_input = cast(Element*, Level_Spare(L));
    Heart from = Heart_Of_Builtin_Fundamental(spare_input);

    Value* scratch_reverse_atom = Level_Scratch(L);

    if (Get_Cell_Flag(Level_Spare(L), SPARE_NOTE_REVERSE_CHECKING))
        goto ensure_results_equal;

    Erase_Cell(scratch_reverse_atom);
    goto check_type_and_run_reverse_to;

  check_type_and_run_reverse_to: {  //////////////////////////////////////////

    if (Is_Throwing(L)) {
        assert(L == TOP_LEVEL);  // sublevel automatically dropped
        return BOUNCE_THROWN;
    }

    Level* level_ = TOP_LEVEL;  // sublevel stole the varlist
    assert(level_->prior == L);

    if (Is_Error(OUT)) {
        Drop_Level(level_);
        return OUT;
    }

    require (  // should packs be legal?
      Stable* out = Decay_If_Unstable(OUT)
    );

    if (Heart_Of_Fundamental(out) != to_or_as)
        panic ("Forward TO/AS transform produced wrong type");

    if (
        Get_Level_Flag(L, CHECKING_TO)
        and (Any_List(out) or Any_String(out) or Is_Blob(out))
    ){
        if (Is_Flex_Read_Only(Cell_Flex(out)))
            panic ("TO transform of LIST/STRING/BLOB made immutable series");
    }

    // Reset TO_P sublevel to do reverse transformation

    level_->executor = &Action_Executor;  // Drop_Action() nulled it
    SymId id = Get_Level_Flag(L, CHECKING_TO) ? SYM_TO : SYM_AS;
    require (
      Push_Action(level_, Lib_Stable(id), PREFIX_0)
    );
    Set_Executor_Flag(ACTION, level_, IN_DISPATCH);

    INCLUDE_PARAMS_OF_TO;

    Copy_Cell(Erase_ARG(TYPE), Datatype_From_Type(from));
    Copy_Cell(Erase_ARG(VALUE), out);

    STATE = STATE_0;

    assert(Get_Level_Flag(level_, TRAMPOLINE_KEEPALIVE));
    Clear_Level_Flag(level_, TRAMPOLINE_KEEPALIVE);

    Set_Cell_Flag(Level_Spare(L), SPARE_NOTE_REVERSE_CHECKING);
    level_->out = scratch_reverse_atom;  // don't overwrite OUT
    return CONTINUE_SUBLEVEL(level_);  // wasn't action, no DOWNSHIFT

} ensure_results_equal: {  ///////////////////////////////////////////////////

    USE_LEVEL_SHORTHANDS (L);  // didn't need to keepalive reverse sublevel

    if (THROWING)
        return BOUNCE_THROWN;

    if (Is_Error(scratch_reverse_atom))
        panic (Cell_Error(scratch_reverse_atom));

    require (
      Stable* scratch_reverse = Decay_If_Unstable(scratch_reverse_atom)
    );

    if (to_or_as == TYPE_MAP) {  // doesn't preserve order requirement :-/
        if (Type_Of(scratch_reverse) != Type_Of(spare_input))
            panic ("Reverse TO/AS of MAP! didn't produce original type");
        return OUT;
    }

    bool equal_reversal = rebUnboxLogic(
        CANON(EQUAL_Q), rebQ(spare_input), rebQ(scratch_reverse)
    );

    if (not equal_reversal)
        panic ("Reverse TO/AS transform didn't produce original result");

    if (to_or_as == from and Get_Level_Flag(L, CHECKING_TO)) {
        bool equal_copy = rebUnboxLogic(
            CANON(EQUAL_Q), rebQ(spare_input), CANON(COPY), rebQ(spare_input)
        );
        if (not equal_copy)
            panic ("Reverse TO/AS transform not same as COPY");
    }

    return OUT;
}}

static Bounce Downshift_For_To_Or_As_Checker(Level *level_) {
    INCLUDE_PARAMS_OF_TO;  // frame compatible with AS

    Option(const Symbol*) label = Level_Label(level_);

    Stable* datatype = ARG(TYPE);
    STATE = cast(Byte, Datatype_Builtin_Heart(datatype));  // might alter
    Copy_Cell(SPARE, ARG(VALUE));  // may alter ELEMENT too, save in SPARE

    Level* sub = Push_Downshifted_Level(OUT, level_);

    assert(Not_Level_Flag(sub, TRAMPOLINE_KEEPALIVE));
    Set_Level_Flag(sub, TRAMPOLINE_KEEPALIVE);

    level_->executor = &To_Or_As_Checker_Executor;

    SymId id = Get_Level_Flag(level_, CHECKING_TO) ? SYM_TO : SYM_AS;

    sub->u.action.original = Frame_Phase(Lib_Stable(id));
    Set_Action_Level_Label(sub, label);

    return BOUNCE_DOWNSHIFTED;  // avoids trampoline, action executor updates L
}

#endif


//
//  to: native:generic [
//
//  "Reversibly convert VALUE to TYPE (copied if TYPE is already VALUE's type)"
//
//      return: [element?]
//      type [<opt-out> datatype!]
//      value [<opt-out> element? datatype!]
//  ]
//
DECLARE_NATIVE(TO)
{
    INCLUDE_PARAMS_OF_TO;

    if (Is_Datatype(ARG(VALUE))) {  // do same coercions as WORD!
        Stable* datatype = ARG(VALUE);
        Option(Type) type = Datatype_Type(datatype);
        if (not type)
            panic ("TO doesn't work with extension types");

        SymId id = Symbol_Id_From_Type(unwrap type);
        Init_Word(ARG(VALUE), Canon_Symbol(id));
    }

    Element* value = Element_ARG(VALUE);

    Option(Type) to = Datatype_Type(ARG(TYPE));
    if (not to)
        panic ("TO doesn't work with extension types");

    if (to > MAX_TYPE_FUNDAMENTAL)  // !!! is quoted applicable, or dumb?
        panic ("TO can't produce antiforms or quoteds");

  handle_sigil_cases: {

  // 1. TO for a sigilized type can't carry more than one decorator.  Though
  //    @<foo> may be lexically legal, if you TO WORD! that and get `foo` you
  //    lost information--it's effectively a "composite type".  So you can
  //    only do things like `@foo <=> foo` or `@foo <=> <foo>` etc.
  //
  // 2. We do only the most limited handling as a proof of concept here.  To
  //    do it correctly would require delegating to the ordinary TO handling
  //    logic and then getting control back to add the Sigil on (if we want
  //    continuations to be working in the TO handlers).  This would give us
  //    things like:
  //
  //        >> to tag! '$
  //        == <>  ; not <$>
  //
  //        >> to tied! <>
  //        == $  ; not $<>

    Option(Sigil) sigil = Sigil_Of(value);
    if (sigil) {  // to or from a sigiled form [1]
        switch (Heart_Of(value)) {
          case TYPE_INTEGER:
          case TYPE_WORD:
          case TYPE_RUNE:
            break;

          default:
            panic ("Only non-compound types can be TO converted from Sigil");
        }
        Plainify(value);
    }

    if (Any_Sigiled_Type(to)) {  // limited handling for adding Sigils [2]
        switch (Heart_Of(value)) {
          case TYPE_INTEGER:
          case TYPE_WORD:
            break;

          case TYPE_RUNE:
            if (not Is_Space(value))  // #a <=> $a <=> <a> <=> [a], eventually
                panic ("SPACE is the only RUNE! converting TO Sigil ATM");
            break;

          default:
            panic ("Only [INTEGER! WORD! space-RUNE!] convert TO Sigil ATM");
        }
        Sigilize(value, Sigil_For_Type(unwrap to));
        return COPY(value);
    }

} handle_non_sigil_cases: {

    assert(Any_Plain(value));

    Element* e = Element_ARG(VALUE);

  #if NO_RUNTIME_CHECKS

    Bounce bounce = Dispatch_Generic(TO, e, LEVEL);
    /*if (bounce == UNHANDLED)  // distinct error for AS or TO ?
        return Error_Bad_Cast_Raw(ARG(VALUE), ARG(TYPE)); */
    return bounce;

  #else  // add monitor to ensure result is right

    if (LEVEL->prior->executor == &To_Or_As_Checker_Executor)
        return Dispatch_Generic(TO, e, LEVEL);

    assert(Not_Level_Flag(LEVEL, CHECKING_TO));
    Set_Level_Flag(LEVEL, CHECKING_TO);
    return Downshift_For_To_Or_As_Checker(LEVEL);
  #endif
}}


//
//  as: native:generic [
//
//  "Aliases underlying data of one value to act as another of same class"
//
//      return: [<null> plain?]
//      type [datatype!]
//      value [<opt-out> plain?]
//  ]
//
DECLARE_NATIVE(AS)
{
    INCLUDE_PARAMS_OF_AS;

    Element* e = Element_ARG(VALUE);
    Option(Type) as = Datatype_Type(ARG(TYPE));
    if (not as)
        panic ("TO doesn't work with extension types");
    if ((unwrap as) > MAX_HEART)
        panic ("AS can't alias to quoted/quasiform/antiform");

  #if NO_RUNTIME_CHECKS

    return Dispatch_Generic(AS, e, LEVEL);

  #else  // add monitor to ensure result is right

    if (LEVEL->prior->executor == &To_Or_As_Checker_Executor)
        return Dispatch_Generic(AS, e, TOP_LEVEL);

    assert(Not_Level_Flag(LEVEL, CHECKING_TO));
    return Downshift_For_To_Or_As_Checker(LEVEL);
  #endif
}
