// %cell-array.h


INLINE bool Is_Cell_Listlike(const Cell* v) {  // PACK!s are allowed
    // called by core code, sacrifice Ensure_Readable() checks
    if (Any_List_Type(Unchecked_Heart_Of(v)))
        return true;
    if (not Any_Sequence_Type(Unchecked_Heart_Of(v)))
        return false;
    if (not Cell_Payload_1_Needs_Mark(v))
        return false;
    const Base* payload1 = SERIESLIKE_PAYLOAD_1_BASE(v);
    if (Is_Base_A_Cell(payload1))
        return true;  // List_At() works, but Cell_Array() won't work!
    return Stub_Flavor(u_cast(const Flex*, payload1)) == FLAVOR_SOURCE;
}

INLINE const Source* Cell_Array(const Cell* c) {  // PACK!s are allowed
    assert(Is_Cell_Listlike(c));

    const Base* series = SERIESLIKE_PAYLOAD_1_BASE(c);
    assert(Is_Base_A_Stub(series));  // not a pairing arraylike!
    if (Not_Base_Readable(series))
        panic (Error_Series_Data_Freed_Raw());

    return cast(Source*, series);
}

#define Cell_Array_Ensure_Mutable(v) \
    m_cast(Source*, Cell_Array(Ensure_Mutable(v)))

#define Cell_Array_Known_Mutable(v) \
    m_cast(Source*, Cell_Array(Known_Mutable(v)))


// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the Series_Index() of the value itself.
//
// IMPORTANT: This routine will trigger a panic if the array index is out
// of bounds of the data.  If a function can deal with such out of bounds
// arrays meaningfully, it should work with SERIES_INDEX_UNBOUNDED().
//
INLINE const Element* List_Len_At(
    Option(Sink(Length)) len_at_out,
    const Cell* cell  // want to be able to pass PACK!s, SPLICE!, etc.
){
    const Base* base = SERIESLIKE_PAYLOAD_1_BASE(cell);
    if (Is_Base_A_Cell(base)) {
        assert(Any_Sequence_Type(Unchecked_Heart_Of(cell)));
        assert(SERIESLIKE_PAYLOAD_2_INDEX(cell) == 0);
        if (len_at_out)
            *(unwrap len_at_out) = PAIRING_LEN_2;
        return cast(Element*, base);
    }
    const Source* array = cast(Source*, base);
    REBIDX i = SERIESLIKE_PAYLOAD_2_INDEX(cell);
    Length len = Array_Len(array);
    if (i < 0 or i > len)
        panic (Error_Index_Out_Of_Range_Raw());
    if (len_at_out)  // inlining should remove this if() for List_At()
        *(unwrap len_at_out) = len - i;
    return Array_At(array, i);
}

INLINE const Element* List_At(
    Option(const Element**) tail_out,
    const Cell* cell  // want to be able to pass PACK!s, SPLICE!, etc.
){
    const Base* base = SERIESLIKE_PAYLOAD_1_BASE(cell);
    if (Is_Base_A_Cell(base)) {
        assert(Any_Sequence_Type(Heart_Of(cell)));
        const Pairing* p = cast(Pairing*, base);
        if (tail_out)
            *(unwrap tail_out) = Pairing_Tail(p);
        return Pairing_Head(p);
    }
    const Source* array = cast(Source*, base);
    REBIDX i = SERIESLIKE_PAYLOAD_2_INDEX(cell);
    Length len = Array_Len(array);
    if (i < 0 or i > len)
        panic (Error_Index_Out_Of_Range_Raw());
    const Element* at = Array_At(array, i);
    if (tail_out)  // inlining should remove this if() for no tail
        *(unwrap tail_out) = at + (len - i);
    return at;
}

INLINE const Element* List_Item_At(const Value* v) {
    const Element* tail;
    const Element* item = List_At(&tail, v);
    assert(item != tail);  // should be a valid value
    return item;
}


#define List_At_Ensure_Mutable(tail_out,v) \
    m_cast(Element*, List_At((tail_out), Ensure_Mutable(v)))

#define List_At_Known_Mutable(tail_out,v) \
    m_cast(Element*, List_At((tail_out), Known_Mutable(v)))


// !!! R3-Alpha introduced concepts of immutable series with PROTECT, but
// did not consider the protected status to apply to binding.  Ren-C added
// more notions of immutability (const, holds, locking/freezing) and enforces
// it at compile-time...which caught many bugs.  But being able to bind
// "immutable" data was mechanically required by R3-Alpha for efficiency...so
// new answers will be needed. :-/
//
#define List_At_Mutable_Hack(tail_out,v) \
    m_cast(Element*, List_At((tail_out), (v)))


//=//// ANY-LIST? INITIALIZER HELPERS ////////////////////////////////////=//
//
// Declaring as inline with type signature ensures you use a Source* to
// initialize.

INLINE Element* Init_Any_List_At_Core_Untracked(
    Init(Element) out,
    Heart heart,
    const Source* array,
    REBLEN index,
    Context* binding
){
    return Init_Series_At_Core_Untracked(
        out, heart, array, index, binding
    );
}

#define Init_Any_List_At_Core(v,t,a,i,b) \
    TRACK(Init_Any_List_At_Core_Untracked((v), (t), (a), (i), (b)))

#define Init_Any_List_At(v,t,a,i) \
    Init_Any_List_At_Core((v), (t), (a), (i), UNBOUND)

#define Init_Any_List(v,t,a) \
    Init_Any_List_At((v), (t), (a), 0)

#define Init_Block(v,a)     Init_Any_List((v), TYPE_BLOCK, (a))
#define Init_Group(v,a)     Init_Any_List((v), TYPE_GROUP, (a))
#define Init_Fence(v,a)     Init_Any_List((v), TYPE_FENCE, (a))


INLINE Element* Init_Relative_Block_At(
    Init(Element) out,
    Details* details,  // action to which array has relative bindings
    Array* array,
    REBLEN index
){
    Reset_Cell_Header_Noquote(out, CELL_MASK_BLOCK);
    SERIESLIKE_PAYLOAD_1_BASE(out) = array;
    SERIES_INDEX_UNBOUNDED(out) = index;
    Tweak_Cell_Relative_Binding(out, details);
    return out;
}

#define Init_Relative_Block(out,action,array) \
    Init_Relative_Block_At((out), (action), (array), 0)


#if NO_RUNTIME_CHECKS
    #define List_Binding(v) \
        Cell_Binding(v)
#else
    INLINE Context* List_Binding(const Element* v) {
        assert(Is_Cell_Listlike(v));
        Context* c = Cell_Binding(v);
        if (not c)
            return SPECIFIED;

        Flavor flavor = Stub_Flavor(c);
        assert(
            flavor == FLAVOR_LET
            or flavor == FLAVOR_USE
            or flavor == FLAVOR_VARLIST
            or flavor == FLAVOR_SEA
        );
        return c;
    }
#endif


//=//// "PACKS" (BLOCK! Antiforms) ////////////////////////////////////////=//
//
// BLOCK! antiforms are exploited as a mechanism for bundling values in a way
// that they can be passed around as a single value.  They are leveraged in
// particular for multi-return, because a SET-WORD! will unpack only the
// first item, while a SET-BLOCK! will unpack others.
//
//      >> pack [<a> <b>]
//      == ~['<a> '<b>]~  ; anti
//
//      >> x: pack [<a> <b>]
//      == <a>
//
//      >> [x y]: pack [<a> <b>]
//      == <a>
//
//      >> x
//      == <a>
//
//      >> y
//      == <b>
//

INLINE Atom* Init_Pack_Untracked(Init(Atom) out, Source* a) {
    Init_Any_List_At_Core_Untracked(out, TYPE_BLOCK, a, 0, SPECIFIED);
    Unstably_Antiformize_Unbound_Fundamental(out);
    assert(Is_Pack(out));
    return out;
}

#define Init_Pack(out,a) \
    TRACK(Init_Pack_Untracked((out), (a)))

#define Init_Lifted_Pack(out,a) \
    TRACK(Quasify_Isotopic_Fundamental(Init_Any_List_At_Core_Untracked( \
        (out), TYPE_BLOCK, (a), 0, SPECIFIED)))


//=//// "SPLICES" (GROUP! Antiforms) //////////////////////////////////////=//
//
// Group antiforms are understood by routines like APPEND or INSERT or CHANGE
// to mean that you intend to splice their content (the default is to append
// as-is, which is changed from Rebol2/Red).  The typical way of making these
// antiforms is the SPREAD function.
//
//    >> append [a b c] [d e]
//    == [a b c] [d e]
//
//    >> spread [d e]
//    == ~(d e)~  ; anti
//
//    >> append [a b c] ~(d e)~
//    == [a b c d e]
//

INLINE Value* Splicify(Need(Value*) val) {
    assert(Any_List(val) and LIFT_BYTE(val) == NOQUOTE_2);
    KIND_BYTE(val) = TYPE_GROUP;  // splice drops knowledge of list type
    Tweak_Cell_Binding(u_cast(Element*, val), UNBOUND);
    Stably_Antiformize_Unbound_Fundamental(val);
    assert(Is_Splice(val));
    return val;
}

INLINE Value* Init_Splice_Untracked(Init(Value) out, Source* a) {
    Init_Group(out, a);
    Stably_Antiformize_Unbound_Fundamental(out);
    assert(Is_Splice(out));
    return out;
}

#define Init_Splice(out,a) \
    TRACK(Init_Splice_Untracked((out), (a)))

INLINE bool Is_Blank(const Value* v) {
    if (not Is_Splice(v))
        return false;
    const Element* tail;
    const Element* at = List_At(&tail, v);
    return tail == at;
}

#define Init_Blank(out) \
    TRACK(Init_Splice_Untracked((out), g_empty_array))
