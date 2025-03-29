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
    Option(Heart) heart = Heart_Of(c);
    assert(
        heart != TYPE_MODULE
        and Any_Context_Type(heart)
    );

    Node* node = CELL_NODE1(c);  // ParamList or Details
    if (Not_Node_Readable(node)) {
        if (heart == TYPE_FRAME)
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
    assert(Heart_Of(c) == TYPE_ERROR);
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
