// %cell-binary.h

INLINE const Binary* Cell_Binary(const Cell* v) {
    assert(Cell_Heart(v) == REB_BINARY);
    return c_cast(Binary*, Cell_Series(v));
}

#define Cell_Binary_Ensure_Mutable(v) \
    m_cast(Binary*, Cell_Binary(Ensure_Mutable(v)))

#define Cell_Binary_Known_Mutable(v) \
    m_cast(Binary*, Cell_Binary(Known_Mutable(v)))


INLINE const Byte* Cell_Binary_Size_At(
    Option(Size*) size_at_out,
    const Cell* v
){
    const Binary* bin = Cell_Binary(v);
    REBIDX i = VAL_INDEX_RAW(v);
    Size size = Binary_Len(bin);
    if (i < 0 or i > cast(REBIDX, size))
        fail (Error_Index_Out_Of_Range_Raw());
    if (size_at_out)
        *unwrap(size_at_out) = size - i;
    return Binary_At(bin, i);
}

#define Cell_Binary_Size_At_Ensure_Mutable(size_out,v) \
    m_cast(Byte*, Cell_Binary_Size_At((size_out), Ensure_Mutable(v)))

#define Cell_Binary_At(v) \
    Cell_Binary_Size_At(nullptr, (v))

#define Cell_Binary_At_Ensure_Mutable(v) \
    m_cast(Byte*, Cell_Binary_At(Ensure_Mutable(v)))

#define Cell_Binary_At_Known_Mutable(v) \
    m_cast(Byte*, Cell_Binary_At(Known_Mutable(v)))

#define Init_Binary(out,bin) \
    Init_Series_Cell((out), REB_BINARY, (bin))

#define Init_Binary_At(out,bin,offset) \
    Init_Series_Cell_At((out), REB_BINARY, (bin), (offset))


//=//// GLOBAL BINARIES //////////////////////////////////////////////////=//

#define EMPTY_BINARY \
    Root_Empty_Binary

#define BYTE_BUF TG_Byte_Buf


// Historically, it was popular for routines that wanted BINARY! data to also
// accept a STRING!, which would be automatically converted to UTF-8 binary
// data.  This makes those more convenient to write.
//
// !!! With the existence of AS, this might not be as useful as leaving
// STRING! open for a different meaning (or an error as a sanity check).
//
INLINE const Byte* Cell_Bytes_Limit_At(
    Size* size_out,
    const Cell* v,
    REBINT limit
){
    if (limit == UNLIMITED or limit > cast(REBINT, Cell_Series_Len_At(v)))
        limit = Cell_Series_Len_At(v);

    if (Cell_Heart(v) == REB_BINARY) {
        *size_out = limit;
        return Cell_Binary_At(v);
    }

    if (Any_String_Kind(Cell_Heart(v))) {
        *size_out = Cell_String_Size_Limit_At(nullptr, v, limit);
        return Cell_String_At(v);
    }

    assert(Any_Word_Kind(Cell_Heart(v)));
    assert(cast(REBLEN, limit) == Cell_Series_Len_At(v));

    const String* spelling = Cell_Word_Symbol(v);
    *size_out = String_Size(spelling);
    return String_Head(spelling);
}

#define Cell_Bytes_At(size_out,v) \
    Cell_Bytes_Limit_At((size_out), (v), UNLIMITED)
