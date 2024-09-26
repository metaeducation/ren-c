// %cell-context.h

//=//// CONTEXT EXTRACTION ////////////////////////////////////////////////=//
//
// Extraction of a context from a value is a place where it is checked for if
// it is valid or has been "decayed" into a stub.  Thus any extraction of
// stored contexts from other locations (e.g. a META field) must either put
// the pointer directly into a value without dereferencing it and trust it to
// be checked elsewhere...or also check it before use.
//

INLINE VarList* Cell_Varlist(const Cell* v) {
    assert(Any_Context_Kind(Cell_Heart_Unchecked(v)));
    VarList* c;
    if (Not_Node_Accessible(Cell_Node1(v))) {
        if (HEART_BYTE(v) == REB_FRAME)
            fail (Error_Expired_Frame_Raw());  // !!! different error?
        fail (Error_Series_Data_Freed_Raw());
    }

    if (Is_Stub_Varlist(cast(Stub*, Cell_Node1(v)))) {
        c = cast(VarList*, Cell_Node1(v));
    }
    else {
        assert(Cell_Heart_Unchecked(v) == REB_FRAME);
        assert(Is_Stub_Details(cast(Stub*, Cell_Node1(v))));
        c = INODE(Exemplar, cast(Array*, Cell_Node1(v)));
    }
    return c;
}


//=//// FRAME PHASE AND LABELING //////////////////////////////////////////=//
//
// A frame's phase is usually a pointer to the component action in effect for
// a composite function (e.g. an ADAPT).
//
// But if the node where a phase would usually be found is a String* then that
// implies there isn't any special phase besides the action stored by the
// archetype.  Hence the value cell is storing a name to be used with the
// action when it is extracted from the frame.  That's why this works:
//
//     >> f: make frame! unrun :append
//     >> label of f
//     == append  ; useful in debug stack traces if you `eval f`
//
// So extraction of the phase has to be sensitive to this.
//

INLINE void Tweak_Cell_Frame_Phase(Cell* v, Phase* phase) {
    assert(Cell_Heart(v) == REB_FRAME);  // may be protected (e.g. archetype)
    Tweak_Cell_Frame_Phase_Or_Label(v, phase);
}

INLINE Phase* VAL_FRAME_PHASE(const Cell* v) {
    Flex* f = Extract_Cell_Frame_Phase_Or_Label(v);
    if (not f or Is_Stub_Symbol(f))  // ANONYMOUS or label, not a phase
        return CTX_FRAME_PHASE(Cell_Varlist(v));  // use archetype
    return cast(Phase*, f);  // cell has its own phase, return it
}

INLINE bool IS_FRAME_PHASED(const Cell* v) {
    assert(Cell_Heart(v) == REB_FRAME);
    Flex* f = Extract_Cell_Frame_Phase_Or_Label(v);
    return f and not Is_Stub_Symbol(f);
}

// 1. Extract_Cell_Action_Partials_Or_Label as well
//
// 2. as a phase (or partials), so no label (maybe findable if running)
//
INLINE Option(const Symbol*) VAL_FRAME_LABEL(const Cell* v) {
    Flex* f = Extract_Cell_Frame_Phase_Or_Label(v);  // [1]
    if (f and Is_Stub_Symbol(f))  // label in value
        return cast(Symbol*, f);
    return ANONYMOUS;  // [2]
}

INLINE void INIT_VAL_FRAME_LABEL(
    Cell* v,
    Option(const String*) label
){
    assert(Cell_Heart(v) == REB_FRAME);
    Assert_Cell_Writable(v);  // No label in archetype
    Tweak_Cell_Frame_Phase_Or_Label(v, maybe label);
}


// Common routine for initializing OBJECT, MODULE!, PORT!, and ERROR!
//
// A fully constructed context can reconstitute the ANY-CONTEXT? cell
// that is its canon form from a single pointer...the cell sitting in
// the 0 slot of the context's varlist ("archetype")
//
INLINE Element* Init_Context_Cell(
    Sink(Element*) out,
    Heart heart,
    VarList* c
){
  #if !defined(NDEBUG)
    Extra_Init_Context_Cell_Checks_Debug(heart, c);
  #endif
    UNUSED(heart);
    Assert_Flex_Managed(Varlist_Array(c));
    if (CTX_TYPE(c) != REB_MODULE)
        Assert_Flex_Managed(Keylist_Of_Varlist(c));
    return Copy_Cell(out, Varlist_Archetype(c));
}

#define Init_Object(out,c) \
    Init_Context_Cell((out), REB_OBJECT, (c))

#define Init_Port(out,c) \
    Init_Context_Cell((out), REB_PORT, (c))

INLINE Element* Init_Frame(
    Sink(Element*) out,
    VarList* c,
    Option(const String*) label  // nullptr (ANONYMOUS) is okay
){
    Init_Context_Cell(out, REB_FRAME, c);
    INIT_VAL_FRAME_LABEL(out, label);
    return out;
}


// Ports are unusual hybrids of user-mode code dispatched with native code, so
// some things the user can do to the internals of a port might cause the
// C code to crash.  This wasn't very well thought out in R3-Alpha, but there
// was some validation checking.  This factors out that check instead of
// repeating the code.
//
INLINE void FAIL_IF_BAD_PORT(Value* port) {
    if (not Any_Context(port))
        fail (Error_Invalid_Port_Raw());

    VarList* ctx = Cell_Varlist(port);
    if (
        Varlist_Len(ctx) < (STD_PORT_MAX - 1)
        or not Is_Object(Varlist_Slot(ctx, STD_PORT_SPEC))
    ){
        fail (Error_Invalid_Port_Raw());
    }
}

// It's helpful to show when a test for a native port actor is being done,
// rather than just having the code say Is_Handle().
//
INLINE bool Is_Native_Port_Actor(const Value* actor) {
    if (Is_Handle(actor))
        return true;
    assert(Is_Object(actor));
    return false;
}


INLINE const Value* TRY_VAL_CONTEXT_VAR_CORE(
    const Value* context,
    const Symbol* symbol,
    bool writable
){
    bool strict = false;
    Value* var;
    if (Is_Module(context)) {
        var = MOD_VAR(Cell_Varlist(context), symbol, strict);
    }
    else {
        Option(Index) index = Find_Symbol_In_Context(context, symbol, strict);
        if (not index)
            var = nullptr;
        else
            var = Varlist_Slot(Cell_Varlist(context), unwrap index);
    }
    if (var and writable and Get_Cell_Flag(var, PROTECTED))
        fail (Error_Protected_Key(symbol));
    return var;
}

#define TRY_VAL_CONTEXT_VAR(context,symbol) \
    TRY_VAL_CONTEXT_VAR_CORE((context), (symbol), false)

#define TRY_VAL_CONTEXT_MUTABLE_VAR(context,symbol) \
    m_cast(Value*, TRY_VAL_CONTEXT_VAR_CORE((context), (symbol), true))
