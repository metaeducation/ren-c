// %cell-context.h

//=//// CONTEXT EXTRACTION ////////////////////////////////////////////////=//
//
// Extraction of a context from a value is a place where it is checked for if
// it is valid or has been "diminished" into a stub.  Thus any extraction of
// stored contexts from other locations (e.g. an ADJUNCT field) must either put
// the pointer directly into a value without dereferencing it and trust it to
// be checked elsewhere...or also check it before use.
//

INLINE VarList* Cell_Varlist(const Cell* c) {
    Option(Heart) heart = Heart_Of(c);
    assert(
        heart != TYPE_MODULE
        and Any_Context_Type(heart)
    );

    Base* base = CELL_PAYLOAD_1(c);  // ParamList or Details
    if (Not_Base_Readable(base)) {
        if (heart == TYPE_FRAME)
            panic (Error_Expired_Frame_Raw());  // !!! different warning?
        panic (Error_Series_Data_Freed_Raw());
    }

    while (not Is_Stub_Varlist(cast(Stub*, base))) {
        assert(Unchecked_Heart_Of(c) == TYPE_FRAME);
        assert(Is_Stub_Details(cast(Stub*, base)));
        c = Flex_Head_Dynamic(Cell,
            cast(Details*, CELL_FRAME_PAYLOAD_1_PHASE(c))
        );
        base = CELL_PAYLOAD_1(c);  // ParamList or Details
    }
    return cast(VarList*, base);
}

INLINE SeaOfVars* Cell_Module_Sea(const Cell* c) {
    assert(Heart_Of(c) == TYPE_MODULE);
    return cast(SeaOfVars*, CELL_PAYLOAD_1(c));
}


INLINE Error* Cell_Error(const Cell* c) {
    assert(Heart_Of(c) == TYPE_WARNING);
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


INLINE Element* Init_Let(Init(Element) out, Let* let) {
    assert(Is_Base_Managed(let));
    Reset_Cell_Header_Noquote(out, CELL_MASK_LET);
    CELL_EXTRA(out) = nullptr;
    CELL_PAYLOAD_1(out) = let;
    Corrupt_Unused_Field(out->payload.split.two.corrupt);
    return out;
}

INLINE Let* Cell_Let(const Cell* c) {
    assert(Heart_Of(c) == TYPE_LET);

    Base* base = CELL_PAYLOAD_1(c);
    if (Not_Base_Readable(base))
        panic (Error_Series_Data_Freed_Raw());

    return cast(Let*, base);
}


INLINE Element* Init_Module(Init(Element) out, SeaOfVars* sea) {
    assert(Is_Base_Managed(sea));
    Reset_Cell_Header_Noquote(out, CELL_MASK_MODULE);
    CELL_EXTRA(out) = nullptr;
    CELL_PAYLOAD_1(out) = sea;
    Corrupt_Unused_Field(out->payload.split.two.corrupt);
    return out;
}

INLINE Context* Cell_Context(const Cell* c) {
    if (Heart_Of(c) == TYPE_MODULE)
        return Cell_Module_Sea(c);
    if (Heart_Of(c) == TYPE_LET)
        return Cell_Let(c);
    return Cell_Varlist(c);
}
