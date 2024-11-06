// %cell-binary.h

INLINE const Binary* Cell_Binary(const Cell* v) {
    assert(Cell_Heart(v) == REB_BLOB);
    return c_cast(Binary*, Cell_Flex(v));
}

#define Cell_Binary_Ensure_Mutable(v) \
    m_cast(Binary*, Cell_Binary(Ensure_Mutable(v)))

#define Cell_Binary_Known_Mutable(v) \
    m_cast(Binary*, Cell_Binary(Known_Mutable(v)))


INLINE const Byte* Cell_Blob_Size_At(
    Option(Sink(Size)) size_at,
    const Cell* v
){
    const Binary* b = Cell_Binary(v);
    REBIDX i = VAL_INDEX_RAW(v);
    Size size = Binary_Len(b);
    if (i < 0 or i > size)
        fail (Error_Index_Out_Of_Range_Raw());
    if (size_at)
        *(unwrap size_at) = size - i;
    return Binary_At(b, i);
}

#define Cell_Blob_Size_At_Ensure_Mutable(size_out,v) \
    m_cast(Byte*, Cell_Blob_Size_At((size_out), Ensure_Mutable(v)))

#define Cell_Blob_At(v) \
    Cell_Blob_Size_At(nullptr, (v))

#define Cell_Blob_At_Ensure_Mutable(v) \
    m_cast(Byte*, Cell_Blob_At(Ensure_Mutable(v)))

#define Cell_Blob_At_Known_Mutable(v) \
    m_cast(Byte*, Cell_Blob_At(Known_Mutable(v)))

#define Init_Blob(out,blob) \
    Init_Series((out), REB_BLOB, (blob))

#define Init_Blob_At(out,blob,offset) \
    Init_Series_At((out), REB_BLOB, (blob), (offset))


//=//// GLOBAL BINARIES //////////////////////////////////////////////////=//

#define EMPTY_BINARY \
    Root_Empty_Binary

#define BYTE_BUF TG_Byte_Buf


// Historically, it was popular for routines that wanted BLOB! data to also
// accept a TEXT!, which would be interpreted as UTF-8.
//
// This makes those more convenient to write.
//
// !!! With the existence of AS, this might not be as useful as leaving
// TEXT! open for a different meaning (or an error as a sanity check)?
//
INLINE const Byte* Cell_Bytes_Limit_At(
    Size* size_out,
    const Cell* c,
    Option(const Length*) limit_in
){
    Heart heart = Cell_Heart(c);
    assert(Any_Bytes_Kind(heart));

    Length len_at;
    if (heart == REB_BLOB)
        Cell_Blob_Size_At(&len_at, c);
    else
        len_at = Cell_String_Len_At(c);

    Length limit;
    if (limit_in == UNLIMITED or *(unwrap limit_in) > len_at)
        limit = len_at;
    else
        limit = *(unwrap limit_in);

    Corrupt_Pointer_If_Debug(limit_in);

    if (heart == REB_BLOB) {
        *size_out = limit;
        return Cell_Blob_At(c);
    }

    if (Any_Utf8_Kind(heart)) {
        *size_out = Cell_String_Size_Limit_At(nullptr, c, &limit);
        return Cell_String_At(c);
    }

    assert(Any_Word_Kind(Cell_Heart(c)));
    assert(limit == Cell_Series_Len_At(c));

    const String* spelling = Cell_Word_Symbol(c);
    *size_out = String_Size(spelling);
    return String_Head(spelling);
}

#define Cell_Bytes_At(size_out,v) \
    Cell_Bytes_Limit_At((size_out), (v), UNLIMITED)
