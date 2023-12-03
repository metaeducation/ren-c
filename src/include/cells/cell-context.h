// %cell-context.h

//=//// CONTEXT EXTRACTION ////////////////////////////////////////////////=//
//
// Extraction of a context from a value is a place where it is checked for if
// it is valid or has been "decayed" into a stub.  Thus any extraction of
// stored contexts from other locations (e.g. a META field) must either put
// the pointer directly into a value without dereferencing it and trust it to
// be checked elsewhere...or also check it before use.
//

INLINE Context* VAL_CONTEXT(NoQuote(const Cell*) v) {
    assert(Any_Context_Kind(Cell_Heart_Unchecked(v)));
    Context* c;

    if (IS_VARLIST(cast(Stub*, Cell_Node1(v)))) {
        c = cast(Context*, Cell_Node1(v));
    }
    else {
        assert(Cell_Heart_Unchecked(v) == REB_FRAME);
        assert(IS_DETAILS(cast(Stub*, Cell_Node1(v))));
        c = INODE(Exemplar, cast(Array*, Cell_Node1(v)));
    }
    FAIL_IF_INACCESSIBLE_CTX(c);
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
//     == append  ; useful in debug stack traces if you `do f`
//
// So extraction of the phase has to be sensitive to this.
//

INLINE void INIT_VAL_FRAME_PHASE(Cell* v, Phase* phase) {
    assert(Is_Frame(v));  // may be marked protected (e.g. archetype)
    INIT_VAL_FRAME_PHASE_OR_LABEL(v, phase);
}

INLINE Phase* VAL_FRAME_PHASE(NoQuote(const Cell*) v) {
    Series* s = VAL_FRAME_PHASE_OR_LABEL(v);
    if (not s or Is_String_Symbol(s))  // ANONYMOUS or label, not a phase
        return CTX_FRAME_PHASE(VAL_CONTEXT(v));  // use archetype
    return cast(Phase*, s);  // cell has its own phase, return it
}

INLINE bool IS_FRAME_PHASED(NoQuote(const Cell*) v) {
    assert(Cell_Heart(v) == REB_FRAME);
    Series* s = VAL_FRAME_PHASE_OR_LABEL(v);
    return s and not Is_String_Symbol(s);
}

INLINE Option(const Symbol*) VAL_FRAME_LABEL(NoQuote(const Cell*) v) {
    Series* s = VAL_FRAME_PHASE_OR_LABEL(v);  // VAL_ACTION_PARTIALS_OR_LABEL as well
    if (s and Is_String_Symbol(s))  // label in value
        return cast(Symbol*, s);
    return ANONYMOUS;  // has a phase (or partials), so no label (maybe findable if running)
}

INLINE void INIT_VAL_FRAME_LABEL(
    Cell* v,
    Option(const String*) label
){
    assert(Is_Frame(v));
    ASSERT_CELL_WRITABLE(v);  // No label in archetype
    INIT_VAL_FRAME_PHASE_OR_LABEL(v, try_unwrap(label));
}


//=//// ANY-CONTEXT! VALUE EXTRACTORS /////////////////////////////////////=//
//
// There once were more helpers like `VAL_CONTEXT_VAR(v,n)` which were macros
// for things like `CTX_VAR(VAL_CONTEXT(v), n)`.  However, once VAL_CONTEXT()
// became a test point for failure on inaccessibility, it's not desirable to
// encourage calling with repeated extractions that pay that cost each time.
//
// However, this does not mean that all functions should early extract a
// VAL_CONTEXT() and then do all operations in terms of that...because this
// potentially loses information present in the Cell* cell.  If the value
// is a frame, then the phase information conveys which fields should be
// visible for that phase of execution and which aren't.
//

INLINE const Key* VAL_CONTEXT_KEYS_HEAD(NoQuote(const Cell*) context)
{
    if (Cell_Heart(context) != REB_FRAME)
        return CTX_KEYS_HEAD(VAL_CONTEXT(context));

    Phase* phase = VAL_FRAME_PHASE(context);
    return ACT_KEYS_HEAD(phase);
}

#define VAL_CONTEXT_VARS_HEAD(context) \
    CTX_VARS_HEAD(VAL_CONTEXT(context))  // all views have same varlist


// Common routine for initializing OBJECT, MODULE!, PORT!, and ERROR!
//
// A fully constructed context can reconstitute the ANY-CONTEXT! REBVAL
// that is its canon form from a single pointer...the REBVAL sitting in
// the 0 slot of the context's varlist.
//
INLINE REBVAL *Init_Context_Cell(
    Cell* out,
    enum Reb_Kind kind,
    Context* c
){
  #if !defined(NDEBUG)
    Extra_Init_Context_Cell_Checks_Debug(kind, c);
  #endif
    UNUSED(kind);
    Assert_Series_Managed(CTX_VARLIST(c));
    if (CTX_TYPE(c) != REB_MODULE)
        Assert_Series_Managed(CTX_KEYLIST(c));
    return Copy_Cell(out, CTX_ARCHETYPE(c));
}

#define Init_Object(out,c) \
    Init_Context_Cell((out), REB_OBJECT, (c))

#define Init_Port(out,c) \
    Init_Context_Cell((out), REB_PORT, (c))

INLINE REBVAL *Init_Frame(
    Cell* out,
    Context* c,
    Option(const String*) label  // nullptr (ANONYMOUS) is okay
){
    Init_Context_Cell(out, REB_FRAME, c);
    INIT_VAL_FRAME_LABEL(out, label);
    return cast(REBVAL*, out);
}


// Ports are unusual hybrids of user-mode code dispatched with native code, so
// some things the user can do to the internals of a port might cause the
// C code to crash.  This wasn't very well thought out in R3-Alpha, but there
// was some validation checking.  This factors out that check instead of
// repeating the code.
//
INLINE void FAIL_IF_BAD_PORT(REBVAL *port) {
    if (not Any_Context(port))
        fail (Error_Invalid_Port_Raw());

    Context* ctx = VAL_CONTEXT(port);
    if (
        CTX_LEN(ctx) < (STD_PORT_MAX - 1)
        or not Is_Object(CTX_VAR(ctx, STD_PORT_SPEC))
    ){
        fail (Error_Invalid_Port_Raw());
    }
}

// It's helpful to show when a test for a native port actor is being done,
// rather than just having the code say Is_Handle().
//
INLINE bool Is_Native_Port_Actor(const REBVAL *actor) {
    if (Is_Handle(actor))
        return true;
    assert(Is_Object(actor));
    return false;
}


INLINE Value(const*) TRY_VAL_CONTEXT_VAR_CORE(
    const REBVAL *context,
    const Symbol* symbol,
    bool writable
){
    bool strict = false;
    Value(*) var;
    if (Is_Module(context)) {
        var = MOD_VAR(VAL_CONTEXT(context), symbol, strict);
    }
    else {
        REBLEN n = Find_Symbol_In_Context(context, symbol, strict);
        if (n == 0)
            var = nullptr;
        else
            var = CTX_VAR(VAL_CONTEXT(context), n);
    }
    if (var and writable and Get_Cell_Flag(var, PROTECTED))
        fail (Error_Protected_Key(symbol));
    return var;
}

#define TRY_VAL_CONTEXT_VAR(context,symbol) \
    TRY_VAL_CONTEXT_VAR_CORE((context), (symbol), false)

#define TRY_VAL_CONTEXT_MUTABLE_VAR(context,symbol) \
    m_cast(Value(*), TRY_VAL_CONTEXT_VAR_CORE((context), (symbol), true))
