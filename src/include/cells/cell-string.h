// %cell-string.h


INLINE bool Stringlike_Cell(const Cell* v) {
    return Any_Utf8_Type(Heart_Of(v)) and Stringlike_Has_Stub(v);
}

INLINE const Strand* Cell_Strand(const Cell* v) {
    Option(Heart) heart = Heart_Of(v);
    if (heart == TYPE_WORD)
        return Word_Symbol(v);

    assert(Stringlike_Cell(v));
    return cast(Strand*, Cell_Flex(v));
}

#define Cell_Strand_Ensure_Mutable(v) \
    m_cast(Strand*, Cell_Strand(Ensure_Mutable(v)))


// This routine works with the notion of "length" that corresponds to the
// idea of the datatype which the series index is for.  Notably, a BLOB!
// can alias an ANY-STRING? or ANY-WORD? and address the individual bytes of
// that type.  So if the series is a STRING! and not a BLOB!, the special
// cache of the length in the String Stub must be used.
//
INLINE Length Series_Len_Head(const Cell* v) {
    const Flex* f = Cell_Flex(v);
    if (Is_Stub_Strand(f) and Heart_Of(v) != TYPE_BLOB)
        return Strand_Len(cast(Strand*, f));
    return Flex_Used(f);
}

INLINE bool VAL_PAST_END(const Cell* v)
   { return Series_Index(v) > Series_Len_Head(v); }

INLINE Length Series_Len_At(const Cell* v) {
    //
    // !!! At present, it is considered "less of a lie" to tell people the
    // length of a series is 0 if its index is actually past the end, than
    // to implicitly clip the data pointer on out of bounds access.  It's
    // still going to be inconsistent, as if the caller extracts the index
    // and low level length themselves, they'll find it doesn't add up.
    // This is a longstanding historical Rebol issue that needs review.
    //
    REBIDX i = Series_Index(v);
    if (i < 0 or i > Series_Len_Head(v))
        panic (Error_Index_Out_Of_Range_Raw());

    return Series_Len_Head(v) - i;  // take current index into account
}

INLINE Utf8(const*) Cell_Utf8_Head(const Cell* c) {
    assert(Any_Utf8_Type(Heart_Of(c)));

    if (not Cell_Payload_1_Needs_Mark(c))  // must store bytes in cell direct
        return u_cast(Utf8(const*), c->payload.at_least_8);

    const Strand* str = cast(Strand*, SERIESLIKE_PAYLOAD_1_BASE(c));
    return Strand_Head(str);  // symbols are strings
}

INLINE Utf8(const*) String_At(const Cell* v) {
    Option(Heart) heart = Heart_Of(v);

    if (not Any_String_Type(heart))  // non-positional: URL, RUNE, WORD...
        return Cell_Utf8_Head(v);  // might store utf8 directly in cell

    const Strand* str = cast(Strand*, Cell_Flex(v));
    REBIDX i = SERIES_INDEX_UNBOUNDED(v);
    if (i < 0 or i > Strand_Len(str))
        panic (Error_Index_Out_Of_Range_Raw());

    return i == 0 ? Strand_Head(str) : Strand_At(str, i);
}


INLINE Utf8(const*) Cell_Strand_Tail(const Cell* c) {
    assert(Any_Utf8_Type(Heart_Of(c)));

    if (not Stringlike_Has_Stub(c)) {  // content in cell direct
        Size size = c->extra.at_least_4[IDX_EXTRA_USED];
        return cast(Utf8(const*), c->payload.at_least_8 + size);
    }

    const Strand* str = cast(Strand*, SERIESLIKE_PAYLOAD_1_BASE(c));
    return Strand_Tail(str);
}


#define String_At_Ensure_Mutable(v) \
    u_cast(Utf8(*), m_cast(Byte*, String_At(Ensure_Mutable(v))))

#define String_At_Known_Mutable(v) \
    u_cast(Utf8(*), m_cast(Byte*, String_At(Known_Mutable(v))))


INLINE REBLEN String_Len_At(const Cell* c) {
    Option(Heart) heart = Heart_Of(c);
    if (Any_String_Type(heart))  // can have an index position
        return Series_Len_At(c);

    if (not Stringlike_Has_Stub(c))  // content directly in cell
        return c->extra.at_least_4[IDX_EXTRA_LEN];

    const Strand* str = cast(Strand*, SERIESLIKE_PAYLOAD_1_BASE(c));
    return Strand_Len(str);
}

INLINE Size String_Size_Limit_At(
    Option(Sink(Length)) length_out,  // length in chars to end or limit
    const Cell* cell,
    Option(const Length*) limit
){
    if (limit)
        assert(*(unwrap limit) >= 0);

    Utf8(const*) at = String_At(cell);  // !!! update cache if needed
    Utf8(const*) tail;

    REBLEN len_at = String_Len_At(cell);
    if (not limit or *(unwrap limit) >= len_at) {
        if (length_out)
            *(unwrap length_out) = len_at;
        tail = Cell_Strand_Tail(cell);  // byte count known (fast)
    }
    else {
        tail = at;
        Length len = 0;
        for (; len < *(unwrap limit); ++len)
            tail = Skip_Codepoint(tail);
        if (length_out)
            *(unwrap length_out) = len;
    }

    return tail - at;
}

#define String_Size_At(cell) \
    String_Size_Limit_At(nullptr, (cell), UNLIMITED)

INLINE Size String_Byte_Offset_At(const Cell* cell) {
    return String_At(cell) - Strand_Head(Cell_Strand(cell));
}

// 1. Arbitrary seeking...this technique needs to be tuned, e.g. to look from
//    the head or the tail depending on what's closer
//
INLINE Size String_Byte_Offset_For_Index(const Cell* cell, REBLEN index)
{
    assert(Any_String_Type(Unchecked_Heart_Of(cell)));

    const Strand* strand = Cell_Strand(cell);
    Utf8(const*) at;

    if (index == Series_Index(cell))
        at = String_At(cell);  // !!! update cache if needed
    else if (index == Series_Len_Head(cell))
        at = Strand_Tail(strand);
    else
        at = Strand_At(strand, index);  // !!! needs tuning [1]

    return at - Strand_Head(strand);
}


//=//// ANY-STRING? CONVENIENCE MACROS ////////////////////////////////////=//
//
// Declaring as inline with type signature ensures you use a Strand* to
// initialize.

INLINE Element* Init_Any_String_At_Untracked(
    Init(Element) out,
    Heart heart,
    const Strand* s,
    REBLEN index
){
    return Init_Series_At_Core(out, heart, s, index, UNBOUND);
}

#define Init_Any_String_At(out,heart,s,index) \
    TRACK(Init_Any_String_At_Untracked((out), (heart), (s), (index)))

#define Init_Any_String_Untracked(out,heart,s) \
    Init_Any_String_At_Untracked((out), (heart), (s), 0)

#define Init_Any_String(out,heart,s) \
    TRACK(Init_Any_String_Untracked((out), (heart), (s)))

#define Init_Text(v,s)      Init_Any_String((v), TYPE_TEXT, (s))
#define Init_File(v,s)      Init_Any_String((v), TYPE_FILE, (s))
#define Init_Tag(v,s)       Init_Any_String((v), TYPE_TAG, (s))


INLINE Element* Textify_Any_Utf8(Element* any_utf8) {  // always works
    DECLARE_ELEMENT (temp);
    assume (
      Alias_Any_Utf8_As(temp, any_utf8, TYPE_TEXT)
    );
    Copy_Cell(any_utf8, temp);
    return any_utf8;
}
