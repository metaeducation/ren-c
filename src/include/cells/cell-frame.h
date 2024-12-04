//
//  File: %cell-frame.h
//  Summary: "Definitions for FRAME! Cells (Antiform of FRAME! is action)"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//     == #[frame! [value: ~]]  ; remembered it was for negate
//
//     >> f.value: 1020
//
//     >> eval f
//     == -1020
//
// But FRAME!s were not simply objects which represented the parameters to
// call the function with.  The actual in-memory reprsentation of the VarList
// was used as the function's variables.
//
// This became more complex, because functions could be composed through
// things like specializations and adaptations.  In each adapted phase,
// different variables needed to be visible--e.g. someone could specialize
// fields out of a function with a certain name, and then augment the
// function with another field of the same name.  Which field should be
// visible depended on how far into the composition an execution had gotten.
//
// That concept of "Phase" became another field in FRAME! cells.  Plus, it
// was added that the way to say a frame wasn't running was to store a label
// for the function in the phase slot so that a frame could indicate the
// name that should show in the stack when being invoked.
//
// Further contributing to the complexity of FRAME! was that it was decided
// that the concept of an "action" that would run when dispatched from WORD!
// would be an antiform frame.  This meant that a FRAME! was the result of
// functions like ADAPT and ENCLOSE...but those generated new function
// identities without creating new VarLists to represent the parameters.
// Rather than making copies for no reason, the alternative was to make frames
// able to point directly at an Initial_Phase() of a function...which would
// then have to be queried for a VarList*.
//


INLINE Phase* VAL_ACTION(const Cell* v) {
    assert(HEART_BYTE(v) == REB_FRAME);
    if (Not_Node_Readable(Cell_Node1(v)))
        fail (Error_Series_Data_Freed_Raw());

    Flex* f = cast(Flex*, Cell_Node1(v));  // maybe exemplar, maybe details
    return cast(Phase*, f);
}

#define VAL_ACTION_KEYLIST(v) \
    ACT_KEYLIST(VAL_ACTION(v))


//=//// ACTION LABELING ///////////////////////////////////////////////////=//
//
// When an ACTION! is stored in a cell (e.g. not an "archetype"), it can
// contain a label of the ANY-WORD? it was taken from.  If it is an array
// node, it is presumed an archetype and has no label.
//
// !!! Theoretically, longer forms could be used here as labels...e.g. an
// entire array or pairing backing a sequence.  However, that would get
// tough if the sequence contained GROUP!s which were evaluated, and then
// you'd be storing something that wouldn't be stored otherwise, so it would
// stop being "cheap".

INLINE void Update_Frame_Cell_Label(Cell* c, Option(const Symbol*) label) {
    assert(Cell_Heart(c) == REB_FRAME);
    Assert_Cell_Writable(c);  // archetype R/O
    Tweak_Cell_Frame_Phase_Or_Label(c, label);
}


// A fully constructed action can reconstitute the ACTION! cell
// that is its canon form from a single pointer...the cell sitting in
// the 0 slot of the action's details.  That action has no binding and
// no label.
//
INLINE Element* Init_Frame_Details_Core(
    Init(Element) out,
    Details* details,
    Option(const Symbol*) label,
    Option(VarList*) coupling
){
  #if RUNTIME_CHECKS
    Extra_Init_Frame_Details_Checks_Debug(details);
  #endif
    Force_Flex_Managed(details);

    Reset_Cell_Header_Noquote(out, CELL_MASK_FRAME);
    Tweak_Cell_Frame_Details(out, details);
    Tweak_Cell_Frame_Phase_Or_Label(out, label);
    Tweak_Cell_Coupling(out, coupling);

    return out;
}

#define Init_Frame_Details(out,a,label,coupling) \
    TRACK(Init_Frame_Details_Core((out), (a), (label), (coupling)))



//=//// ACTIONS (FRAME! Antiforms) ////////////////////////////////////////=//
//
// The antiforms of actions exist for a couple of reasons.  They are the form
// that when stored in a variable leads to implicit execution by a reference
// from a WORD!...while non-antiform ACTION! is inert.  This means you cannot
// accidentally run a function with the following code:
//
//     for-each 'item block [print ["The item's kind is" kind of item]]
//
// That reference to ITEM is guaranteed to not be the antiform form, since it
// is enumerating over a block.  Various places in the system are geared for
// making it more difficult to assign antiform actions accidentally.
//
// The other big reason is for a "non-literal" distinction in parameters.
// Historically, functions like REPLACE have chosen to run functions to
// calculate what the replacement should be.  However, that ruled out the
// ability to replace actual function instances--and doing otherwise would
// require extra parameterization.  This lets the antiform state serve as
// the signal that the function should be invoked, and not searched for.
//

#define Init_Action(out,a,label,binding) \
    Actionify(cast(Value*, Init_Frame_Details_Core( \
        ensure(Sink(Value), TRACK(out)), (a), (label), (binding)) \
    ))

INLINE Value* Actionify(Need(Value*) v) {
    assert(Is_Frame(v) and QUOTE_BYTE(v) == NOQUOTE_1);
    return Coerce_To_Stable_Antiform(v);
}

INLINE Element* Deactivate_If_Action(Need(Value*) v) {
    if (Is_Action(v))
        QUOTE_BYTE(v) = NOQUOTE_1;
    return cast(Element*, v);
}


//=//// CELL INFIX MODE ///////////////////////////////////////////////////=//
//
// Historical Rebol had a separate datatype (OP!) for infix functions.  In
// Ren-C, each cell holding a FRAME! has in its header a 2-bit quantity
// (a "Crumb") which encodes one of four possible infix modes.  This can be
// checked quickly by the evaluator.
//

INLINE Option(InfixMode) Get_Cell_Infix_Mode(const Cell* c) {
    assert(HEART_BYTE(c) == REB_FRAME);
    return u_cast(InfixMode, Get_Cell_Crumb(c));
}

INLINE void Set_Cell_Infix_Mode(Cell* c, Option(InfixMode) mode) {
    assert(HEART_BYTE(c) == REB_FRAME);
    Set_Cell_Crumb(c, maybe mode);
}

INLINE bool Is_Cell_Infix(const Cell* c) {  // slightly faster than != PREFIX_0
    assert(HEART_BYTE(c) == REB_FRAME);
    return did (c->header.bits & CELL_MASK_CRUMB);
}
