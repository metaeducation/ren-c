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
//  /make: native:generic [
//
//  "Constructs or allocates the specified datatype"
//
//      return: [element?]
//      type "The datatype or parent context to construct from"
//          [datatype! any-context?]
//      def "Definition or size of the new value (binding may be modified)"
//          [element?]  ; !!! ACTION! decays to FRAME!, should it?
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
        datatype = Datatype_Of_Fundamental(As_Element(type));
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
    Tweak_Cell_Lift_Byte(OUT, Type_From_Byte(STATE));

    return OUT;
}


//
//  /copy: native:generic [
//
//  "Copies a series, object, or other value; return value of same type"
//
//      return: [any-stable?]
//      value "If an ANY-SERIES?, it is only copied from its current position"
//          [element?]
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

    Element* elem = ARG(VALUE);

    TypeEnum lift = Type_Of_Raw(elem);
    Clear_Cell_Quotes_And_Quasi(elem);  // dispatch requires unquoted items

    Option(Dispatcher*) dispatcher = Get_Generic_Dispatcher(
        &GENERIC_TABLE(COPY),
        Datatype_Of(elem)
    );

    if (not dispatcher) {  // trivial copy, is it good to do so? [1]
        if (ARG(PART))
            panic (Error_Bad_Refines_Raw());

        UNUSED(ARG(DEEP));  // historically we ignore it

        Tweak_Cell_Lift_Byte(elem, lift);  // restore
        return COPY_TO_OUT(elem);
    }

    if (lift <= MAX_TYPE_NOQUOTE_NOQUASI)  // don't need requote/quasi
        return Apply_Cfunc(unwrap dispatcher, LEVEL);

    Option(const Symbol*) label = Level_Label(level_);
    Option(VarList*) coupling = Level_Coupling(level_);

    Level* sub = Push_Downshifted_Level(OUT, level_);

    level_->executor = &Copy_Quoter_Executor;

    assert(Not_Level_Flag(sub, TRAMPOLINE_KEEPALIVE));
    assert(Get_Executor_Flag(ACTION, sub, IN_DISPATCH));

    Phase* phase = Frame_Phase(LIB(COPY));

  require (
    Tweak_Level_Phase(sub, phase)
  );

    Tweak_Level_Coupling(sub, coupling);

    sub->u.action.original = Frame_Phase(LIB(COPY));
    Set_Action_Level_Label(sub, label);

    STATE = Byte_From_Type(lift);

    return BOUNCE_DOWNSHIFTED;
}


//
//  /to: native:generic [
//
//  "Reversibly convert VALUE to TYPE (copied if TYPE is already VALUE's type)"
//
//      return: [element?]
//      type [datatype!]
//      value [element? datatype!]
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
        Mark_Typechecked(u_cast(Param*, ARG(VALUE)));
    }

    Element* value = Element_ARG(VALUE);

    Option(Type) to = Datatype_Type(ARG(TYPE));
    if (not to)
        panic ("TO doesn't work with extension types");

    if ((unwrap to) > MAX_TYPE_FUNDAMENTAL)
        panic ("TO can't produce antiforms or quoteds");  // !!! handle quoted?

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
        switch (unwrap Heart_Of(value)) {
          case HEART_INTEGER:
          case HEART_WORD:
          case HEART_RUNE:
            break;

          default:
            panic ("Only non-compound types can be TO converted from Sigil");
        }
        Clear_Cell_Sigil(value);
    }

    if (Any_Sigiled_Type(to)) {  // limited handling for adding Sigils [2]
        switch (unwrap Heart_Of(value)) {
          case HEART_INTEGER:
          case HEART_WORD:
            break;

          case HEART_RUNE:
            if (not Is_Space(value))  // #a <=> $a <=> <a> <=> [a], eventually
                panic ("SPACE is the only RUNE! converting TO Sigil ATM");
            break;

          default:
            panic ("Only [INTEGER! WORD! space-RUNE!] convert TO Sigil ATM");
        }
        Add_Cell_Sigil(value, Sigil_For_Sigiled_Type(unwrap to));
        return COPY_TO_OUT(value);
    }

} handle_non_sigil_cases: {

    assert(Any_Plain(value));

    Element* e = Element_ARG(VALUE);

    return Dispatch_Generic(TO, e, LEVEL);
}}


//
//  /as: native:generic [
//
//  "Aliases underlying data of one value to act as another of same class"
//
//      return: [plain?]
//      type [datatype!]
//      value [plain?]
//  ]
//
DECLARE_NATIVE(AS)
{
    INCLUDE_PARAMS_OF_AS;

    Element* e = Element_ARG(VALUE);
    Option(Type) as = Datatype_Type(ARG(TYPE));
    if (not as)
        panic ("TO doesn't work with extension types");
    if ((unwrap as) > MAX_TYPE_HEART)
        panic ("AS can't alias to quoted/quasiform/antiform");

    return Dispatch_Generic(AS, e, LEVEL);
}


//
//  /check-to-or-as: native [
//
//  "Checked build utility that makes sure TO and AS are reversible"
//
//      return: [element?]
//      frame [frame!]
//  ]
//
DECLARE_NATIVE(CHECK_TO_OR_AS)
{
    INCLUDE_PARAMS_OF_CHECK_TO_OR_AS;

  #if (! RUNTIME_CHECKS)
    crash ("CHECK-TO-OR-AS is only for checked builds");
  #else
    Element* frame = ARG(FRAME);

    Phase* phase = Frame_Phase(frame);
    assert(Is_Stub_Varlist(phase));
    ParamList* varlist = cast(ParamList*, phase);
    const Element* archetype = Varlist_Archetype(varlist);

    Stable* type = As_Stable(Varlist_Slot(varlist, 1));  // we alter
    Stable* v = As_Stable(Varlist_Slot(varlist, 2));  // we alter

    enum {
        ST_CHECK_TO_OR_AS_INITIAL_ENTRY = STATE_0,
        // reverse state saves original TO enum type as the STATE
        ST_CHECK_TO_OR_AS_CHECKING_FORWARD = 255
    };

    switch (STATE) {
      case ST_CHECK_TO_OR_AS_INITIAL_ENTRY: goto initial_entry;
      case ST_CHECK_TO_OR_AS_CHECKING_FORWARD: goto forward_result_in_out;
      default: goto backward_result_in_spare;
    }

 initial_entry: { ///////////////////////////////////////////////////////////

    STATE = ST_CHECK_TO_OR_AS_CHECKING_FORWARD;
    return CONTINUE(OUT, frame);  // operates on copy, leaves frame intact

} forward_result_in_out: {  //////////////////////////////////////////////////

    if (Is_Datatype(v))  // skip reverse for DATATYPE! (review)
        return OUT;

    if (Is_Failure(OUT))
        return OUT;  // don't enforce failure both directions (?)

    Copy_Cell(SCRATCH, v);  // save original value being TO or AS'd from

    Type to_or_as = unwrap Datatype_Type(type);
    STATE = Byte_From_Type(to_or_as);  // save original TO/AS

    Copy_Cell(type, Datatype_From_Type(unwrap Type_Of(v)));  // reverse
    Copy_Cell(v, As_Stable(OUT));

    return CONTINUE(SPARE, frame);  // !!! could consume this frame (how?)

} backward_result_in_spare: {  ///////////////////////////////////////////////

    TypeEnum to_or_as = Type_From_Byte_Or_0(STATE);
    Stable* forward = As_Stable(SCRATCH);
    TypeEnum from = unwrap Type_Of(forward);
    Stable* backward = As_Stable(SPARE);

    if (to_or_as == TYPE_MAP) {  // doesn't preserve order requirement :-/
        if (not Have_Same_Type(forward, backward))
            panic ("Reverse TO/AS of MAP! didn't produce original type");
        return OUT;
    }

    bool equal_reversal = rebUnboxLogic(
        CANON(EQUAL_Q), rebQ(forward), rebQ(backward)
    );

    if (not equal_reversal)
        panic ("Reverse TO/AS transform didn't produce original result");

    if (Frame_Label(archetype) == CANON(AS))
        return OUT;

    assert(Frame_Label(archetype) == CANON(TO));

    if (to_or_as == from) {  // TO conversion check for COPY (review);
        bool equal_copy = rebUnboxLogic(
            CANON(EQUAL_Q), rebQ(forward), CANON(COPY), rebQ(backward)
        );
        if (not equal_copy)
            panic ("Reverse TO/AS transform not same as COPY");
    }

    return OUT; }
#endif
}
