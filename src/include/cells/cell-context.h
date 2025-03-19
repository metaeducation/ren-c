// %cell-context.h

//=//// CONTEXT EXTRACTION ////////////////////////////////////////////////=//
//
// Extraction of a context from a value is a place where it is checked for if
// it is valid or has been "decayed" into a stub.  Thus any extraction of
// stored contexts from other locations (e.g. a META field) must either put
// the pointer directly into a value without dereferencing it and trust it to
// be checked elsewhere...or also check it before use.
//

INLINE VarList* Cell_Varlist(const Cell* c) {
    assert(
        HEART_BYTE(c) != TYPE_MODULE
        and Any_Context_Type(HEART_BYTE(c))
    );

    Node* node = CELL_NODE1(c);  // ParamList or Details
    if (Not_Node_Readable(node)) {
        if (HEART_BYTE(c) == TYPE_FRAME)
            fail (Error_Expired_Frame_Raw());  // !!! different error?
        fail (Error_Series_Data_Freed_Raw());
    }

    while (not Is_Stub_Varlist(cast(Stub*, node))) {
        assert(Cell_Heart_Unchecked(c) == TYPE_FRAME);
        assert(Is_Stub_Details(cast(Stub*, node)));
        c = Flex_Head_Dynamic(Cell, cast(Details*, CELL_FRAME_PHASE(c)));
        node = CELL_NODE1(c);  // ParamList or Details
    }
    return cast(VarList*, node);
}

INLINE SeaOfVars* Cell_Module_Sea(const Cell* c) {
    assert(HEART_BYTE(c) == TYPE_MODULE);
    return cast(SeaOfVars*, CELL_NODE1(c));
}

INLINE Context* Cell_Context(const Cell* c) {
    if (HEART_BYTE(c) == TYPE_MODULE)
        return Cell_Module_Sea(c);
    return Cell_Varlist(c);
}

INLINE Error* Cell_Error(const Cell* c) {
    assert(Cell_Heart(c) == TYPE_ERROR);
    return cast(Error*, Cell_Varlist(c));
}


// Common routine for initializing OBJECT, MODULE!, PORT!, and ERROR!
//
// A fully constructed context can reconstitute the ANY-CONTEXT? cell
// that is its canon form from a single pointer...the cell sitting in
// the 0 slot of the context's varlist ("archetype")
//
INLINE Element* Init_Context_Cell(
    Init(Element) out,
    Heart heart,
    VarList* c
){
  #if RUNTIME_CHECKS
    Extra_Init_Context_Cell_Checks_Debug(heart, c);
  #endif
    UNUSED(heart);
    Assert_Flex_Managed(c);
    assert(CTX_TYPE(c) != TYPE_MODULE);  // catch straggling bad casts
    return Copy_Cell(out, Varlist_Archetype(c));
}

#define Init_Object(out,c) \
    Init_Context_Cell((out), TYPE_OBJECT, (c))

#define Init_Port(out,c) \
    Init_Context_Cell((out), TYPE_PORT, (c))



INLINE Element* Init_Module(Init(Element) out, SeaOfVars* sea) {
    assert(Is_Node_Managed(sea));
    Reset_Cell_Header_Noquote(out, CELL_MASK_MODULE);
    CELL_EXTRA(out) = nullptr;
    CELL_NODE1(out) = sea;
    Corrupt_Unused_Field(out->payload.split.two.corrupt);
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
        Varlist_Len(ctx) < MAX_STD_PORT
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
        var = Sea_Var(Cell_Module_Sea(context), symbol, strict);
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
