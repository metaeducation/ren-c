// %cell-string.h

INLINE const String* Cell_String(const Cell* v) {
    Heart heart = Cell_Heart(v);
    if (Any_Word_Kind(heart))
        return Cell_Word_Symbol(v);

    assert(Any_String_Kind(heart) or heart == REB_URL);
    return c_cast(String*, Cell_Flex(v));
}

#define Cell_String_Ensure_Mutable(v) \
    m_cast(String*, Cell_String(Ensure_Mutable(v)))

INLINE const String* Cell_Issue_String(const Cell* v) {
    assert(Cell_Heart(v) == REB_ISSUE);
    assert(Get_Cell_Flag(v, FIRST_IS_NODE));
    return c_cast(String*, Cell_Node1(v));
}

// This routine works with the notion of "length" that corresponds to the
// idea of the datatype which the series index is for.  Notably, a BINARY!
// can alias an ANY-STRING? or ANY-WORD? and address the individual bytes of
// that type.  So if the series is a STRING! and not a BINARY!, the special
// cache of the length in the String Stub must be used.
//
INLINE Length Cell_Series_Len_Head(const Cell* v) {
    const Flex* f = Cell_Flex(v);
    if (Is_Stub_String(f) and Cell_Heart(v) != REB_BINARY)
        return String_Len(c_cast(String*, f));
    return Flex_Used(f);
}

INLINE bool VAL_PAST_END(const Cell* v)
   { return VAL_INDEX(v) > Cell_Series_Len_Head(v); }

INLINE Length Cell_Series_Len_At(const Cell* v) {
    //
    // !!! At present, it is considered "less of a lie" to tell people the
    // length of a series is 0 if its index is actually past the end, than
    // to implicitly clip the data pointer on out of bounds access.  It's
    // still going to be inconsistent, as if the caller extracts the index
    // and low level length themselves, they'll find it doesn't add up.
    // This is a longstanding historical Rebol issue that needs review.
    //
    REBIDX i = VAL_INDEX(v);
    if (i > Cell_Series_Len_Head(v))
        fail ("Index past end of series");
    if (i < 0)
        fail ("Index before beginning of series");

    return Cell_Series_Len_Head(v) - i;  // take current index into account
}

INLINE Utf8(const*) Cell_Utf8_Head(const Cell* c) {
    assert(Any_Utf8_Kind(Cell_Heart(c)));

    if (Not_Cell_Flag(c, FIRST_IS_NODE))  // must store bytes in cell direct
        return PAYLOAD(Bytes, c).at_least_8;

    const String* str = cast(String*, Cell_Node1(c));  // symbols are strings
    return String_Head(str);
}

INLINE Utf8(const*) Cell_String_At(const Cell* v) {
    Heart heart = Cell_Heart(v);

    if (not Any_String_Kind(heart))  // non-positional: URL, ISSUE, WORD...
        return Cell_Utf8_Head(v);  // might store utf8 directly in cell

    const String* str = c_cast(String*, Cell_Flex(v));
    REBIDX i = VAL_INDEX_RAW(v);
    if (i < 0 or i > String_Len(str))
        fail (Error_Index_Out_Of_Range_Raw());

    return i == 0 ? String_Head(str) : String_At(str, i);
}


INLINE Utf8(const*) Cell_String_Tail(const Cell* c) {
    assert(Any_Utf8_Kind(Cell_Heart(c)));

    if (Not_Cell_Flag(c, STRINGLIKE_HAS_NODE)) {  // content in cell direct
        Size size = EXTRA(Bytes, c).at_least_4[IDX_EXTRA_USED];
        return PAYLOAD(Bytes, c).at_least_8 + size;
    }

    const String* str = cast(String*, Cell_Node1(c));
    return String_Tail(str);
}


#define Cell_String_At_Ensure_Mutable(v) \
    m_cast(Utf8(*), Cell_String_At(Ensure_Mutable(v)))

#define Cell_String_At_Known_Mutable(v) \
    m_cast(Utf8(*), Cell_String_At(Known_Mutable(v)))


INLINE REBLEN Cell_String_Len_At(const Cell* c) {
    Heart heart = Cell_Heart(c);
    if (Any_String_Kind(heart))  // can have an index position
        return Cell_Series_Len_At(c);

    if (Not_Cell_Flag(c, STRINGLIKE_HAS_NODE))  // content directly in cell
        return EXTRA(Bytes, c).at_least_4[IDX_EXTRA_LEN];

    const String* str = cast(String*, Cell_Node1(c));
    return String_Len(str);
}

INLINE Size Cell_String_Size_Limit_At(
    Option(Length*) length_out,  // length in chars to end (including limit)
    const Cell* v,
    Option(const Length*) limit
){
    if (limit)
        assert(*(unwrap limit) >= 0);

    Utf8(const*) at = Cell_String_At(v);  // !!! update cache if needed
    Utf8(const*) tail;

    REBLEN len_at = Cell_String_Len_At(v);
    if (not limit or *(unwrap limit) >= len_at) {
        if (length_out)
            *(unwrap length_out) = len_at;
        tail = Cell_String_Tail(v);  // byte count known (fast)
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

#define Cell_String_Size_At(v) \
    Cell_String_Size_Limit_At(nullptr, v, UNLIMITED)

INLINE Size VAL_BYTEOFFSET(const Cell* v) {
    return Cell_String_At(v) - String_Head(Cell_String(v));
}

INLINE Size VAL_BYTEOFFSET_FOR_INDEX(
    const Cell* v,
    REBLEN index
){
    assert(Any_String_Kind(Cell_Heart(v)));

    Utf8(const*) at;

    if (index == VAL_INDEX(v))
        at = Cell_String_At(v); // !!! update cache if needed
    else if (index == Cell_Series_Len_Head(v))
        at = String_Tail(Cell_String(v));
    else {
        // !!! arbitrary seeking...this technique needs to be tuned, e.g.
        // to look from the head or the tail depending on what's closer
        //
        at = String_At(Cell_String(v), index);
    }

    return at - String_Head(Cell_String(v));
}


//=//// ANY-STRING? CONVENIENCE MACROS ////////////////////////////////////=//
//
// Declaring as inline with type signature ensures you use a String* to
// initialize, and the C++ build can also validate managed consistent w/const.

INLINE Element* Init_Any_String_At(
    Sink(Element*) out,
    Heart heart,
    const_if_c String* s,
    REBLEN index
){
    Init_Series_At_Core(
        out,
        heart,
        Force_Flex_Managed_Core(s),
        index,
        UNBOUND
    );
    return out;
}

#if CPLUSPLUS_11
    INLINE Element* Init_Any_String_At(
        Sink(Element*) out,
        Heart heart,
        const String* s,
        REBLEN index
    ){
        // Init will assert if str is not managed...
        return Init_Series_At_Core(out, heart, s, index, UNBOUND);
    }
#endif

#define Init_Any_String(out,heart,s) \
    Init_Any_String_At((out), (heart), (s), 0)

#define Init_Text(v,s)      Init_Any_String((v), REB_TEXT, (s))
#define Init_File(v,s)      Init_Any_String((v), REB_FILE, (s))
#define Init_Email(v,s)     Init_Any_String((v), REB_EMAIL, (s))
#define Init_Tag(v,s)       Init_Any_String((v), REB_TAG, (s))
#define Init_Url(v,s)       Init_Any_String((v), REB_URL, (s))


//=//// GLOBAL STRING CONSTANTS //////////////////////////////////////////=//

#define EMPTY_TEXT \
    Root_Empty_Text
