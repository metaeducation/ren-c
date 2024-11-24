// %cell-frame.h

INLINE Action* VAL_ACTION(const Cell* v) {
    assert(HEART_BYTE(v) == REB_FRAME);
    if (Not_Node_Readable(Cell_Node1(v)))
        fail (Error_Series_Data_Freed_Raw());

    Flex* f = cast(Flex*, Cell_Node1(v));  // maybe exemplar, maybe details
    return cast(Action*, f);
}

#define VAL_ACTION_KEYLIST(v) \
    ACT_KEYLIST(VAL_ACTION(v))


//=//// ACTION LABELING ///////////////////////////////////////////////////=//
//
// When an ACTION! is stored in a cell (e.g. not an "archetype"), it can
// contain a label of the ANY-WORD? it was taken from.  If it is an array
// node, it is presumed an archetype and has no label.
//
// !!! Theoretically, longer forms like `.not.equal?` for PREDICATE! could
// use an array node here.  But since CHAINs store ACTION!s that can cache
// the words, you get the currently executing label instead...which may
// actually make more sense.

INLINE void INIT_VAL_ACTION_LABEL(
    Cell* v,
    Option(const Symbol*) label
){
    Assert_Cell_Writable(v);  // archetype R/O
    if (label)
        Tweak_Cell_Action_Partials_Or_Label(v, unwrap label);
    else
        Tweak_Cell_Action_Partials_Or_Label(v, ANONYMOUS);
}


// Only the archetype should be asked if it is native (because the archetype
// guides interpretation of the details array).
//
#define Is_Action_Native(a) \
    Get_Action_Flag(VAL_ACTION(ACT_ARCHETYPE(a)), IS_NATIVE)


// A fully constructed action can reconstitute the ACTION! cell
// that is its canon form from a single pointer...the cell sitting in
// the 0 slot of the action's details.  That action has no binding and
// no label.
//
INLINE Element* Init_Frame_Details_Core(
    Init(Element) out,
    Phase* a,
    Option(const Symbol*) label,
    Option(VarList*) coupling
){
  #if RUNTIME_CHECKS
    Extra_Init_Frame_Details_Checks_Debug(a);
  #endif
    Force_Flex_Managed(a);

    Reset_Cell_Header_Noquote(out, CELL_MASK_FRAME);
    Tweak_Cell_Action_Details(out, a);
    INIT_VAL_ACTION_LABEL(out, label);
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
