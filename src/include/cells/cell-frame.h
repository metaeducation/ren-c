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
//     >> /ap10: specialize append/ [value: 10]
//
//     >> /ap10aug: augment ap10/ [value: make parameter! [any-element?]]
//     ; whoa, now the frame has two fields named `value`!
//
//     >> /ap10plus: enclose ap10plus/ [append series value]
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


INLINE Phase* Cell_Frame_Phase(const Value* c) {
    assert(Heart_Of(c) == TYPE_FRAME);

    Node* node = CELL_FRAME_PHASE(c);  // const irrelevant
    if (Not_Node_Readable(node))
        panic (Error_Series_Data_Freed_Raw());

    Flex* f = cast(Flex*, node);
    assert(Is_Stub_Details(f) or Is_Stub_Varlist(f));
    return cast(Phase*, f);
}

INLINE Details* Ensure_Cell_Frame_Details(const Value* c) {
    Phase* phase = Cell_Frame_Phase(c);
    assert(Is_Stub_Details(phase));
    return cast(Details*, phase);
}

INLINE Option(Details*) Try_Cell_Frame_Details(const Value* c) {
    Phase* phase = Cell_Frame_Phase(c);
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
// But if the node where a Lens would usually be found is a Symbol* then that
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

INLINE void Tweak_Cell_Frame_Lens(Value* v, Phase* lens) {
    assert(Heart_Of(v) == TYPE_FRAME);  // may be protected (e.g. archetype)
    assert(Is_Stub_Varlist(lens) or Is_Stub_Details(lens));
    Tweak_Cell_Frame_Lens_Or_Label(v, lens);
}

INLINE Option(Phase*) Cell_Frame_Lens(const Value* c) {
    assert(Heart_Of(c) == TYPE_FRAME);
    Flex* f = cast(Flex*, CELL_FRAME_LENS_OR_LABEL(c));
    if (not f or Is_Stub_Symbol(f))
        return nullptr;
    assert(Is_Stub_Varlist(f) or Is_Stub_Details(f));
    return cast(Phase*, f);
}

INLINE Option(const Symbol*) Cell_Frame_Label(const Value* c) {
    assert(Heart_Of(c) == TYPE_FRAME);
    Flex* f = cast(Flex*, CELL_FRAME_LENS_OR_LABEL(c));
    if (not f)
        return nullptr;
    if (not Is_Stub_Symbol(f)) { // label in value
        assert(Is_Stub_Varlist(f) or Is_Stub_Details(f));
        return nullptr;
    }
    return cast(Symbol*, f);
}

INLINE Option(const Symbol*) Cell_Frame_Label_Deep(const Value* c) {
    Option(const Symbol*) label = Cell_Frame_Label(c);
    if (label)
        return label;
    return Cell_Frame_Label(Phase_Archetype(Cell_Frame_Phase(c)));
}

INLINE void Update_Frame_Cell_Label(Value* c, Option(const Symbol*) label) {
    assert(Heart_Of(c) == TYPE_FRAME);
    Assert_Cell_Writable(c);  // archetype R/O
    Tweak_Cell_Frame_Lens_Or_Label(c, label);
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
    Flex* phase,  // may not be completed or managed if out is rootvar
    Option(const Flex*) lens_or_label,
    Option(VarList*) coupling
){
    Reset_Cell_Header_Noquote(out, CELL_MASK_FRAME);
    CELL_FRAME_PHASE(out) = phase;
    Tweak_Cell_Frame_Lens_Or_Label(out, maybe lens_or_label);
    Tweak_Cell_Frame_Coupling(out, coupling);
    return out;
}

#define Init_Frame_Unchecked(out,identity,label,coupling) \
    TRACK(Init_Frame_Unchecked_Untracked( \
        (out), (identity), (label), (coupling)))

INLINE Element* Init_Frame_Untracked(
    Init(Element) out,
    Phase* phase,
    Option(const Flex*) lens_or_label,
    Option(VarList*) coupling
){
    Force_Flex_Managed(phase);

  #if RUNTIME_CHECKS
    Extra_Init_Frame_Checks_Debug(phase);
  #endif

    return Init_Frame_Unchecked_Untracked(out, phase, lens_or_label, coupling);
}

#if CPLUSPLUS_11
    template<
        typename P,
        typename L,
        typename std::enable_if<
            std::is_same<P,VarList*>::value
            or not (
                std::is_convertible<L,Option(const Symbol*)>::value
                or std::is_convertible<L,Option(Phase*)>::value
            )
        >::type* = nullptr
    >
    void Init_Frame_Untracked(
        Init(Element) out,
        P phase,  // mitigate awkward "VarList inherits Phase" [1]
        L lens_or_label,  // restrict to Option(const Symbol*), Option(Phase*)
        Option(VarList*) coupling
    ) = delete;
#endif

#define Init_Frame(out,identity,label,coupling) \
    TRACK(Init_Frame_Untracked((out), (identity), \
        ensure(Option(const Symbol*), (label)), (coupling)))

#define Init_Lensed_Frame(out,identity,lens,coupling) \
    TRACK(Init_Frame_Untracked((out), (identity), \
        ensure(Option(Phase*), (lens)), (coupling)))


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

INLINE Value* Actionify(Need(Value*) val) {
    assert(Is_Frame(val) and LIFT_BYTE(val) == NOQUOTE_1);
    Option(Error*) e = Trap_Coerce_To_Antiform(cast(Atom*, val));
    assert(not e);
    UNUSED(e);
    assert(Is_Action(val));
    return val;
}

#define Init_Action(out,a,label,coupling) \
    Actionify(cast(Value*, Init_Frame( \
        ensure(Sink(Value), (out)), (a), (label), (coupling)) \
    ))  // note that antiform frames can't have lenses, only labels!

INLINE Element* Deactivate_If_Action(Need(Value*) v) {
    if (Is_Action(v))
        LIFT_BYTE(v) = NOQUOTE_1;
    return cast(Element*, v);
}


//=//// CELL INFIX MODE ///////////////////////////////////////////////////=//
//
// Historical Rebol had a separate datatype (OP!) for infix functions.  In
// Ren-C, each cell holding a FRAME! has in its header a 2-bit quantity
// (a "Crumb") which encodes one of four possible infix modes.  This can be
// checked quickly by the evaluator.
//

INLINE Option(InfixMode) Cell_Frame_Infix_Mode(const Value* c) {
    assert(Heart_Of(c) == TYPE_FRAME);
    return u_cast(InfixMode, Get_Cell_Crumb(c));
}

INLINE void Tweak_Cell_Frame_Infix_Mode(Value* c, Option(InfixMode) mode) {
    assert(Heart_Of(c) == TYPE_FRAME);
    Set_Cell_Crumb(c, maybe mode);
}

INLINE bool Is_Cell_Frame_Infix(const Value* c) {  // faster than != PREFIX_0
    assert(Heart_Of(c) == TYPE_FRAME);
    return did (c->header.bits & CELL_MASK_CRUMB);
}
