// %cell-series.h

INLINE const Flex* Cell_Flex(const Cell* v) {
    Option(Heart) heart = Heart_Of(v);
    assert(
        Any_Series_Type(heart)
        or (Any_Utf8_Type(heart) and Stringlike_Has_Stub(v))
    );
    UNUSED(heart);
    if (Not_Base_Readable(CELL_SERIESLIKE_NODE(v)))
        panic (Error_Series_Data_Freed_Raw());

    return cast(Flex*, CELL_SERIESLIKE_NODE(v));
}

#define Cell_Flex_Ensure_Mutable(v) \
    m_cast(Flex*, Cell_Flex(Ensure_Mutable(v)))

#define Cell_Flex_Known_Mutable(v) \
    m_cast(Flex*, Cell_Flex(Known_Mutable(v)))


// It is possible that the index could be to a point beyond the range of the
// Flex.  This is intrinsic, because the Flex data can be modified through
// one cell and not update the other cells referring to it.  Hence VAL_INDEX()
// must be checked, or the routine called with it must.

#define VAL_INDEX_RAW(c) \
    (c)->payload.split.two.i

#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define VAL_INDEX_UNBOUNDED(v) \
        VAL_INDEX_RAW(v)
#else
    // allows an assert, but uses C++ reference for lvalue:
    //
    //     VAL_INDEX_UNBOUNDED(v) = xxx;  // ensures v is ANY-SERIES?
    //
    // Avoids Ensure_Readable(), because it's assumed that it was done in the
    // type checking to ensure VAL_INDEX() applied.  (This is called often.)
    //
    INLINE REBIDX VAL_INDEX_UNBOUNDED(const Cell* c) {
        assert(Any_Series_Type(Unchecked_Heart_Of(c)));
        assert(Cell_Payload_1_Needs_Mark(c));
        return VAL_INDEX_RAW(c);
    }
    INLINE REBIDX & VAL_INDEX_UNBOUNDED(Cell* c) {
        Assert_Cell_Writable(c);
        assert(Any_Series_Type(Unchecked_Heart_Of(c)));
        assert(Cell_Payload_1_Needs_Mark(c));
        return VAL_INDEX_RAW(c);  // returns a C++ reference
    }
#endif


INLINE REBLEN Cell_Series_Len_Head(const Cell* v);  // forward decl
INLINE bool Stringlike_Cell(const Cell* v);  // forward decl

// Unlike VAL_INDEX_UNBOUNDED() that may give a negative number or past the
// end of series, VAL_INDEX() does bounds checking and always returns an
// unsigned REBLEN.
//
INLINE REBLEN VAL_INDEX_STRINGLIKE_OK(const Cell* v) {
    Option(Heart) heart = Heart_Of(v);
    assert(Any_Series_Type(heart) or Stringlike_Cell(v));
    UNUSED(heart);
    assert(Cell_Payload_1_Needs_Mark(v));
    REBIDX i = VAL_INDEX_RAW(v);
    if (i < 0 or i > Cell_Series_Len_Head(v))
        panic (Error_Index_Out_Of_Range_Raw());
    return i;
}

// Unlike VAL_INDEX_UNBOUNDED() that may give a negative number or past the
// end of series, VAL_INDEX() does bounds checking and always returns an
// unsigned REBLEN.
//
INLINE REBLEN VAL_INDEX(const Cell* v) {
    assert(Any_Series_Type(Heart_Of(v)));
    return VAL_INDEX_STRINGLIKE_OK(v);
}


INLINE Size Cell_String_Size_Limit_At(
    Option(Sink(Length)) length_out,  // length in chars to end or limit
    const Cell* v,
    Option(const Length*) limit
);


// 1. An advantage of making all binaries terminate in 0 is that it means
//    that if they were valid UTF-8, they could be aliased as Rebol strings,
//    which are zero terminated.  So it's the rule.
//
// 2. Many Array Flexes (such as varlists) allow antiforms.  We don't want
//    these making it into things like BLOCK! or GROUP! values, as the user
//    should never see antiforms in what they see as "ANY-ARRAY!".  Plus we
//    want to interpret the LINK as a filename and the MISC as a line number.
//    That's contentious with other array forms' purposes for LINK and MISC.
//
INLINE Element* Init_Series_At_Core_Untracked(
    Init(Element) out,
    Heart heart,
    const Flex* f,  // ensured managed by calling macro
    REBLEN index,
    Context* binding
){
  #if RUNTIME_CHECKS
    Assert_Flex_Term_If_Needed(f);  // even binaries [1]

    if (Any_List_Type(heart)) {
        assert(Stub_Flavor(f) == FLAVOR_SOURCE);  // no antiforms [2]
    }
    else if (Any_String_Type(heart)) {
        assert(Is_Stub_Strand(f));
    }
    else if (Any_Utf8_Type(heart)) {  // see also Init_Utf8_Non_String()
        assert(heart != TYPE_WORD);  // can't use this init!
        assert(Is_Stub_Strand(f));
        assert(Is_Flex_Frozen(f));
    }
    else if (heart == TYPE_BLOB) {
        assert(Flex_Wide(f) == 1);  // Note: Binary is allowed to alias String
    }
    else {
        assert(Any_Sequence_Type(heart));
        assert(Is_Stub_Source(f));
        assert(Is_Source_Frozen_Shallow(c_cast(Source*, f)));
    }
  #endif

    Force_Flex_Managed(f);

    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(heart)
            | (not CELL_FLAG_DONT_MARK_PAYLOAD_1)  // series stub needs mark
            | CELL_FLAG_DONT_MARK_PAYLOAD_2  // index shouldn't be marked
    );
    CELL_SERIESLIKE_NODE(out) = m_cast(Flex*, f);  // const bit guides extract
    VAL_INDEX_RAW(out) = index;

    out->extra.base = binding;  // checked below if DEBUG_CHECK_BINDING

  #if DEBUG_CHECK_BINDING
    if (Is_Cell_Bindable(out))
        Assert_Cell_Binding_Valid(out);
    else
        assert(binding == nullptr);  // all non-bindables use nullptr for now
  #endif

  #if RUNTIME_CHECKS  // if non-string UTF-8 fits in cell, should be in cell
    if (Any_Utf8_Type(heart) and not Any_String_Type(heart)) {
        Size utf8_size = Cell_String_Size_Limit_At(nullptr, out, UNLIMITED);

        assert(utf8_size + 1 > Size_Of(out->payload.at_least_8));
    }
  #endif

    return out;
}

#define Init_Series_At_Core(v,t,f,i,s) \
    TRACK(Init_Series_At_Core_Untracked((v), (t), (f), (i), (s)))

#define Init_Series_At(v,t,f,i) \
    Init_Series_At_Core((v), (t), (f), (i), UNBOUND)

#define Init_Series(v,t,f) \
    Init_Series_At((v), (t), (f), 0)
