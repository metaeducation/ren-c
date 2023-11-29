// %cell-frame.h

INLINE Action* VAL_ACTION(NoQuote(const Cell*) v) {
    assert(HEART_BYTE(v) == REB_FRAME);
    Series* s = cast(Series*, Cell_Node1(v));  // maybe exemplar, maybe details
    if (Get_Series_Flag(s, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return cast(Action*, s);
}

#define VAL_ACTION_KEYLIST(v) \
    ACT_KEYLIST(VAL_ACTION(v))


//=//// ACTION LABELING ///////////////////////////////////////////////////=//
//
// When an ACTION! is stored in a cell (e.g. not an "archetype"), it can
// contain a label of the ANY-WORD! it was taken from.  If it is an array
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
    ASSERT_CELL_WRITABLE_EVIL_MACRO(v);  // archetype R/O
    if (label)
        INIT_VAL_ACTION_PARTIALS_OR_LABEL(v, unwrap(label));
    else
        INIT_VAL_ACTION_PARTIALS_OR_LABEL(v, ANONYMOUS);
}


//=//// FRAME BINDING /////////////////////////////////////////////////////=//
//
// Only FRAME! contexts store bindings at this time.  The reason is that a
// unique binding can be stored by individual ACTION! values, so when you make
// a frame out of an action it has to preserve that binding.
//
// Note: The presence of bindings in non-archetype values makes it possible
// for FRAME! values that have phases to carry the binding of that phase.
// This is a largely unexplored feature, but is used in REDO scenarios where
// a running frame gets re-executed.  More study is needed.
//

INLINE Context* VAL_FRAME_BINDING(NoQuote(const Cell*) v) {
    assert(HEART_BYTE(v) == REB_FRAME);
    return cast(Context*, BINDING(v));
}

INLINE void INIT_VAL_FRAME_BINDING(
    Cell* v,
    Context* binding
){
    assert(HEART_BYTE(v) == REB_FRAME);
    INIT_BINDING(v, binding);
}


// Only the archetype should be asked if it is native (because the archetype
// guides interpretation of the details array).
//
#define Is_Action_Native(a) \
    Get_Action_Flag(VAL_ACTION(ACT_ARCHETYPE(a)), IS_NATIVE)


// A fully constructed action can reconstitute the ACTION! REBVAL
// that is its canon form from a single pointer...the REBVAL sitting in
// the 0 slot of the action's details.  That action has no binding and
// no label.
//
INLINE REBVAL *Init_Frame_Details_Core(
    Cell* out,
    Phase* a,
    Option(const Symbol*) label,  // allowed to be ANONYMOUS
    Context* binding  // allowed to be UNBOUND
){
    assert(not binding or Is_Node_Managed(binding));

  #if !defined(NDEBUG)
    Extra_Init_Frame_Details_Checks_Debug(a);
  #endif
    Force_Series_Managed(a);

    Reset_Unquoted_Header_Untracked(out, CELL_MASK_FRAME);
    INIT_VAL_ACTION_DETAILS(out, a);
    INIT_VAL_ACTION_LABEL(out, label);
    INIT_VAL_FRAME_BINDING(out, binding);

    return cast(REBVAL*, out);
}

#define Init_Frame_Details(out,a,label,binding) \
    TRACK(Init_Frame_Details_Core((out), (a), (label), (binding)))



//=//// ACTIONS (FRAME! Isotopes) /////////////////////////////////////////=//
//
// Isotopic forms of actions exist for a couple of reasons.  They are the form
// that when stored in a variable leads to implicit execution by a reference
// from a WORD!...while non-isotopic ACTION! is inert.  This means you cannot
// accidentally run a function with the following code:
//
//     for-each item block [print ["The item's kind is" kind of item]]
//
// That reference to ITEM is guaranteed to not be the isotopic form, since it
// is enumerating over a block.  Various places in the system are geared for
// making it more difficult to assign isotopic actions accidentally.
//
// The other big reason is for a "non-literal" distinction in parameters.
// Historically, functions like REPLACE have chosen to run functions to
// calculate what the replacement should be.  However, that ruled out the
// ability to replace actual function instances--and doing otherwise would
// require extra parameterization.  This lets the isotopic state serve as
// the signal that the function should be invoked, and not searched for.
//

#define Init_Action(out,a,label,binding) \
    Actionify(Init_Frame_Details_Core(TRACK(out), (a), (label), (binding)))

INLINE Value(*) Actionify(Value(*) v) {
    assert(Is_Frame(v) and QUOTE_BYTE(v) == UNQUOTED_1);
    QUOTE_BYTE(v) = ISOTOPE_0;
    return v;
}

INLINE Cell* Deactivate_If_Action(Cell* v) {
    if (Is_Action(v))
        QUOTE_BYTE(v) = UNQUOTED_1;
    return v;
}


INLINE bool Is_Enfixed(NoQuote(const Cell*) v) {
    assert(HEART_BYTE(v) == REB_FRAME);
    return Get_Cell_Flag_Unchecked(v, ENFIX_FRAME);
}

#define Not_Enfixed(v) \
    (not Is_Enfixed(v))
