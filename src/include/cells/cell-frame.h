//
//  file: %cell-frame.h
//  summary: "Definitions for FRAME! Cells (Antiform of FRAME! is action)"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The FRAME! type originated as simply a variation of OBJECT!, which held
// a VarList* representing the parameters of a function, as well as a pointer
// to that function itself.  This meant you could do things like:
//
//     >> f: make frame! negate/
//     == &[frame! [value: ~]]  ; remembered it was for negate
//
//     >> f.value: 1020
//
//     >> eval f
//     == -1020
//
// But FRAME!s were not simply objects which represented the parameters to
// call the function with.  The actual in-memory representation of the VarList
// was used as the function's variables.
//
// This became more complex, because functions could be composed through
// things like specializations and adaptations.  In each adapted phase,
// different variables needed to be visible--e.g. someone could specialize
// fields out of a function with a certain name, and then augment the
// function with another field of the exact same name:
//
//     >> ap10: specialize append/ [value: 10]
//
//     >> ap10aug: augment ap10/ [value: make parameter! [any-element?]]
//     ; whoa, now the frame has two fields named `value`!
//
//     >> ap10plus: enclose ap10plus/ [append series value]
//
//     >> ap10plus [a b c] 10 20
//     == [a b c 10 20]
//
// Which `value` field should be visible depends on how far in the composition
// an execution is.  The ENCLOSE needs to see the augmented value, while the
// APPEND native needs to see the original parameter.  This gave rise to the
// concept of a "Lens" field in the FRAME! Cell to track which fields in the
// VarList should be active for a particular Cell reference...so that KeyLists
// containing duplicate keys be interpreted coherently.
//
// So the concept of "Lens" became another field in FRAME! cells.  Plus, it
// was added that the way to say a frame wasn't running was to store a label
// for the function in the lens slot so that a frame could indicate the
// name that should show in the stack when being invoked.
//
// Further contributing to the complexity of FRAME! was that it was decided
// that the concept of an "action" that would run when dispatched from WORD!
// would be an antiform frame.  This meant that a FRAME! was the result of
// functions like ADAPT and ENCLOSE...but those generated new function
// identities without creating new VarLists to represent the parameters.
// So the Details* and VarList* types were multiplexed into a Phase* type.
//


INLINE Phase* Frame_Phase(const Cell* c) {
    assert(Unchecked_Heart_Of(c) == TYPE_FRAME);

    Base* base = CELL_FRAME_PAYLOAD_1_PHASE(c);  // const irrelevant
    if (Not_Base_Readable(base))
        panic (Error_Series_Data_Freed_Raw());

    Flex* f = cast(Flex*, base);
    assert(Is_Stub_Details(f) or Is_Stub_Varlist(f));
    return cast(Phase*, f);
}

INLINE Details* Ensure_Frame_Details(const Cell* c) {
    Phase* phase = Frame_Phase(c);
    assert(Is_Stub_Details(phase));
    return cast(Details*, phase);
}

INLINE Option(Details*) Try_Frame_Details(const Cell* c) {
    Phase* phase = Frame_Phase(c);
    if (not Is_Stub_Details(phase))
        return nullptr;
    return cast(Details*, phase);
}


//=//// FRAME LENS AND LABELING ///////////////////////////////////////////=//
//
// When a FRAME! has a "Lens", that dictates what variables in the VarList
// should be exposed--which is important for executing frames (even though
// an adaptation's frame contains the adaptee's variables, it should not be
// able to do things like assign its locals).
//
// But if the base where a Lens would usually be found is a Symbol* then that
// implies there isn't any special Lens besides the action stored by the
// archetype.  Hence the value cell is storing a name to be used with the
// action when it is extracted from the frame.  That's why this works:
//
//     >> f: make frame! append/
//     >> label of f
//     == append  ; useful in debug stack traces if you `eval f`
//
// So extraction of the Lens has to be sensitive to this.
//
// !!! Theoretically, longer forms could be used here as labels...e.g. an
// entire array or pairing backing a sequence.  However, that would get
// tough if the sequence contained GROUP!s which were evaluated, and then
// you'd be storing something that wouldn't be stored otherwise, so it would
// stop being "cheap".

INLINE void Tweak_Frame_Lens(Stable* v, Phase* lens) {
    assert(Heart_Of(v) == TYPE_FRAME);  // may be protected (e.g. archetype)
    assert(Is_Stub_Varlist(lens) or Is_Stub_Details(lens));
    Tweak_Frame_Lens_Or_Label(v, lens);
}

INLINE Option(Phase*) Frame_Lens(const Stable* c) {
    assert(Heart_Of(c) == TYPE_FRAME);
    Flex* f = cast(Flex*, CELL_FRAME_EXTRA_LENS_OR_LABEL(c));
    if (not f or Is_Stub_Symbol(f))
        return nullptr;
    assert(Is_Stub_Varlist(f) or Is_Stub_Details(f));
    return cast(Phase*, f);
}

INLINE Option(const Symbol*) Frame_Label(const Stable* c) {
    assert(Heart_Of(c) == TYPE_FRAME);
    Flex* f = cast(Flex*, CELL_FRAME_EXTRA_LENS_OR_LABEL(c));
    if (not f)
        return nullptr;
    if (not Is_Stub_Symbol(f)) { // label in value
        assert(Is_Stub_Varlist(f) or Is_Stub_Details(f));
        return nullptr;
    }
    return cast(Symbol*, f);
}

INLINE Option(const Symbol*) Frame_Label_Deep(const Stable* c) {
    Option(const Symbol*) label = Frame_Label(c);
    if (label)
        return label;
    return Frame_Label(Phase_Archetype(Frame_Phase(c)));
}

INLINE void Update_Frame_Cell_Label(Stable* c, Option(const Symbol*) label) {
    assert(Heart_Of(c) == TYPE_FRAME);
    Assert_Cell_Writable(c);  // archetype R/O
    Tweak_Frame_Lens_Or_Label(c, label);
}


//=//// FRAME CELL INITIALIZATION /////////////////////////////////////////=//
//
// When a FRAME! is initialized, it isn't running, so it is able to store a
// label in the slot that would usually hold the "current" Phase.
//
// 1. VarList inherits from Phase for the pragmatic reason that ParamList
//    wants to be a Phase as well as inherit from VarList.  But all VarList
//    are not actually ParamList--hence not always candidates for Phase.
//    Since we can't use multiple inheritance to solve this, do a little
//    prevention by stopping Init_Frame() calls with plain VarList.
//

INLINE Element* Init_Frame_Unchecked_Untracked(
    Init(Element) out,  // may be rootvar
    Stub* phase,  // may not be completed or managed if out is rootvar
    Option(const Stub*) lens_or_label,
    Option(VarList*) coupling
){
    Reset_Cell_Header_Noquote(
        out,
        BASE_FLAG_BASE | BASE_FLAG_CELL
            | FLAG_HEART(TYPE_FRAME)
            | (not CELL_FLAG_DONT_MARK_PAYLOAD_1)  // first is phase
            | (coupling ? 0 : CELL_FLAG_DONT_MARK_PAYLOAD_2)
    );
    CELL_FRAME_PAYLOAD_1_PHASE(out) = phase;
    CELL_FRAME_EXTRA_LENS_OR_LABEL(out) = m_cast(  // no flag
        Stub*, opt lens_or_label
    );
    CELL_FRAME_PAYLOAD_2_COUPLING(out) = opt coupling;  // flag sync above
    return out;
}

INLINE Element* Init_Frame_Untracked(
    Init(Element) out,
    Phase* phase,
    Option(const Stub*) lens_or_label,
    Option(VarList*) coupling
){
    Force_Stub_Managed(phase);

  #if RUNTIME_CHECKS
    Extra_Init_Frame_Checks_Debug(phase);
  #endif

    return Init_Frame_Unchecked_Untracked(out, phase, lens_or_label, coupling);
}

#define Init_Frame_Unchecked(out,identity,label,coupling) \
    TRACK(Init_Frame_Unchecked_Untracked( \
        (out), known(Phase*, (identity)), (label), (coupling)))

#define Init_Frame(out,identity,label,coupling) \
    TRACK(Init_Frame_Untracked((out), known(Phase*, (identity)), \
        known(Option(const Symbol*), (label)), (coupling)))

#define Init_Lensed_Frame(out,identity,lens,coupling) \
    TRACK(Init_Frame_Untracked((out), known(Phase*, (identity)), \
        known(Option(Phase*), (lens)), (coupling)))


//=//// ACTIONS (FRAME! Antiforms) ////////////////////////////////////////=//
//
// The antiforms of actions exist for a couple of reasons.  They are the form
// that when stored in a variable leads to implicit execution by a reference
// from a WORD!...while non-antiform ACTION! is inert.  This means you cannot
// accidentally run a function with the following code:
//
//     for-each 'item block [print ["The item's kind is" kind of item]]
//
// That reference to ITEM is guaranteed to not be an antiform, since it is
// enumerating over a block.  Various places in the system are geared for
// making it more difficult to assign antiform actions accidentally.
//
// The other big reason is for a "non-literal" distinction in parameters.
// Historically, functions like REPLACE have chosen to run functions to
// calculate what the replacement should be.  However, that ruled out the
// ability to replace actual function instances--and doing otherwise would
// require extra parameterization.  This lets the antiform state serve as
// the signal that the function should be invoked, and not searched for:
//
//     >> replace [1 2 3 4 5] even?/ <even>
//     == [1 <even> 3 <even> 5]  ; no actual EVEN? antiforms can be in block
//

INLINE Stable* Actionify(Exact(Stable*) val) {
    assert(Is_Frame(val) and LIFT_BYTE(val) == NOQUOTE_2);
    Stably_Antiformize_Unbound_Fundamental(val);
    assert(Is_Action(val));
    return val;
}

INLINE Stable* Init_Action_By_Phase(
    Sink(Stable) out,
    Phase* phase,
    Option(const Symbol*) label,
    Option(VarList*) coupling
){
    Init_Frame(out, phase, label, coupling);
    Stably_Antiformize_Unbound_Fundamental(out);
    assert(Is_Action(out));
    return out;
}

#define Init_Action(out,identity,label,coupling) \
    Init_Action_By_Phase((out), known(Phase*, (identity)), (label), (coupling))

INLINE Stable* Deactivate_If_Action(Exact(Stable*) v) {
    if (Is_Action(v))
        LIFT_BYTE(v) = NOQUOTE_2;
    return v;
}


//=//// PACK!ed ACTIONS FOR SAFE SET-WORD ASSIGNMENTS /////////////////////=//
//
// Traditionally Redbol was very permissive about SET-WORD being able to
// assign active functions.  It was easy to write code that thinks it's just
// assigning an inert variable when, it's assigning something that will invoke
// a function if referenced.
//
//     rebol2>> foo: get $bar
//
//     rebol2>> if foo [print "my secret"]
//     MUHAHAHA I AM WHAT WAS STORED IN BAR AND I TRICKED YOU!
//     I see your BLOCK! it was my PARAMETER!  [print "my secret"]
//
// Writing "safe" code created a sort of "pox" where :GET-WORD access had to
// be used to dodge the default function-calling behavior of WORD! access, in
// case a variable might wind up holding an active function.
//
// Ren-C's has one level of safety with word-active ACTION!s as antiforms,
// so you won't accidentally find them while enumerating over lists.  But it
// adds another level of safety by making SET-WORD assignments require any
// action assigns to come from a PACK! containing the action.  This unstable
// state isn't returned by things like PICK, but comes back from generators...
// and you can turn any ACTION! into an ACTION-PACK! using the RUNS native.
//
// This means the "approval" state for purposes of SET-WORD assigns is
// persistable with LIFT, and can be manipulated consciously in usermode.
//

INLINE Value* Packify_Action(Value* atom) {  // put ACTION! in a PACK! [1]
    assert(Is_Action(Known_Stable(atom)));
    Source *a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
    Copy_Lifted_Cell(Stub_Cell(a), atom);
    return Init_Pack(atom, a);
}


//=//// CELL INFIX MODE ///////////////////////////////////////////////////=//
//
// Historical Rebol had a separate datatype (OP!) for infix functions.  In
// Ren-C, each cell holding a FRAME! has in its header a 2-bit quantity
// (a "Crumb") which encodes one of four possible infix modes.  This can be
// checked quickly by the evaluator.
//

INLINE Option(InfixMode) Frame_Infix_Mode(const Stable* c) {
    assert(Heart_Of(c) == TYPE_FRAME);
    return u_cast(InfixMode, Get_Cell_Crumb(c));
}

INLINE void Tweak_Frame_Infix_Mode(Stable* c, Option(InfixMode) mode) {
    assert(Heart_Of(c) == TYPE_FRAME);
    Set_Cell_Crumb(c, opt mode);
}

INLINE bool Is_Frame_Infix(const Stable* c) {  // faster than != PREFIX_0
    assert(Heart_Of(c) == TYPE_FRAME);
    return did (c->header.bits & CELL_MASK_CRUMB);
}


//=//// ACTION! CELL VANISHABILITY ////////////////////////////////////////=//
//
// See CELL_FLAG_WEIRD_VANISHABLE.  When you derive one function from another,
// you generally want to mirror its vanishable status.
//

INLINE void Copy_Vanishability(Stable* to, const Stable* from) {
    assert(Is_Action(to) or Is_Frame(to));
    assert(Is_Action(from) or Is_Frame(from));

    if (Get_Cell_Flag(from, WEIRD_VANISHABLE))
        Set_Cell_Flag(to, WEIRD_VANISHABLE);
    else
        Clear_Cell_Flag(to, WEIRD_VANISHABLE);
}
