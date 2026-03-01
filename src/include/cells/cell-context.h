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
        heart != HEART_MODULE
        and Any_Context_Heart(heart)
    );

    Base* base = CELL_PAYLOAD_1(c);  // ParamList or Details
    if (Not_Base_Readable(base)) {
        if (heart == HEART_FRAME)
            panic (Error_Expired_Frame_Raw());  // !!! different error?
        panic (Error_Series_Data_Freed_Raw());
    }

    if (Is_Stub_Varlist(cast(Stub*, base)))
        return cast(VarList*, base);

    assert(Unchecked_Heart_Of(c) == HEART_FRAME);
    assert(Is_Stub_Details(cast(Stub*, base)));
    return Phase_Paramlist(cast(Phase*, base));
}

INLINE SeaOfVars* Cell_Module_Sea(const Cell* c) {
    assert(Heart_Of(c) == HEART_MODULE);
    return cast(SeaOfVars*, CELL_PAYLOAD_1(c));
}


INLINE Error* Cell_Error(const Cell* c) {
    assert(Heart_Of(c) == HEART_ERROR);
    return cast(Error*, Cell_Varlist(c));
}


// Historically, VarLists stored an "archetype" cell in their 0 slot, which
// was a match for the context itself.  This degraded into where the 0 slot
// was only trustworthy as a value for the context's type, but not necessarily
// the same cell...when the "archetype" of a frame was completely different.
//
// Maintaining the archetype became too slippery.  This means a better idea
// is needed, and generally we want to have other ways of reconstituting a
// full cell from a context.  This is a step in that direction.
//
INLINE Dual* Init_Context_Cell_Untracked(Init(Dual) out, VarList* vlist) {
    Heart heart = CTX_TYPE(vlist);
    Reset_Cell_Header(
        out,
        FLAG_HEART_AND_LIFT(heart)
            | (not CELL_FLAG_DONT_MARK_PAYLOAD_1)  // first is phase/varlist
            | (CELL_FLAG_DONT_MARK_PAYLOAD_2)  // no coupling
    );
    CELL_FRAME_PAYLOAD_1_PHASE(out) = vlist;
    if (heart == HEART_FRAME)  // "self-lensing" Lens_Self()
        CELL_FRAME_EXTRA_LENS_OR_LABEL(out) = u_cast(ParamList*, vlist);
    else
        CELL_FRAME_EXTRA_LENS_OR_LABEL(out) = nullptr;
    CELL_FRAME_PAYLOAD_2_COUPLING(out) = nullptr;  // UNCOUPLED
    return out;
}

#define Init_Context_Cell(out, c) \
    TRACK(Init_Context_Cell_Untracked((out), (c)))

#if RUNTIME_CHECKS
    INLINE Dual* Init_Varlist_Cell_Untracked(
        Init(Dual) out,
        Heart heart,
        VarList* c
    ){
        Extra_Init_Context_Cell_Checks_Debug(heart, c);
        return Init_Context_Cell_Untracked(out, c);
    }
#else
    #define Init_Varlist_Cell_Untracked(out, heart, c) \
        Init_Context_Cell_Untracked((out), (c))
#endif

#define Init_Port(out,c) \
    TRACK(Init_Varlist_Cell_Untracked((out), HEART_PORT, (c)))

#define Init_Object(out,c) \
    TRACK(Init_Varlist_Cell_Untracked((out), HEART_OBJECT, (c)))

#define Init_Error_Cell(out,c) \
    TRACK(Init_Varlist_Cell_Untracked((out), HEART_ERROR, (c)))


INLINE Element* Init_Let(Init(Element) out, Let* let) {
    assert(Is_Base_Managed(let));
    Reset_Cell_Header(out, CELL_MASK_LET);
    CELL_EXTRA(out) = nullptr;
    CELL_PAYLOAD_1(out) = let;
    Corrupt_Unused_Field(out->payload.split.two.corrupt);
    return out;
}

INLINE Let* Cell_Let(const Cell* c) {
    assert(Heart_Of(c) == HEART_LET);

    Base* base = CELL_PAYLOAD_1(c);
    if (Not_Base_Readable(base))
        panic (Error_Series_Data_Freed_Raw());

    return cast(Let*, base);
}


INLINE Element* Init_Module(Init(Element) out, SeaOfVars* sea) {
    assert(Is_Base_Managed(sea));
    Reset_Cell_Header(out, CELL_MASK_MODULE);
    CELL_EXTRA(out) = nullptr;
    CELL_PAYLOAD_1(out) = sea;
    Corrupt_Unused_Field(out->payload.split.two.corrupt);
    return out;
}

INLINE Context* Cell_Context(const Cell* c) {
    if (Heart_Of(c) == HEART_MODULE)
        return Cell_Module_Sea(c);
    if (Heart_Of(c) == HEART_LET)
        return Cell_Let(c);
    return Cell_Varlist(c);
}
