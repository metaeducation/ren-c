// %cell-binary.h

INLINE const Binary* Cell_Binary(const Cell* cell) {
    assert(Unchecked_Heart_Of(cell) == TYPE_BLOB);
    return c_cast(Binary*, Cell_Flex(cell));
}

#define Cell_Binary_Ensure_Mutable(cell) \
    m_cast(Binary*, Cell_Binary(Ensure_Mutable(cell)))

#define Cell_Binary_Known_Mutable(cell) \
    m_cast(Binary*, Cell_Binary(Known_Mutable(cell)))


INLINE const Byte* Blob_Size_At(Option(Sink(Size)) size_at, const Cell* cell)
{
    const Binary* b = Cell_Binary(cell);
    REBIDX i = SERIES_INDEX_UNBOUNDED(cell);
    Size size = Binary_Len(b);
    if (i < 0 or i > size)
        abrupt_panic (Error_Index_Out_Of_Range_Raw());
    if (size_at)
        *(unwrap size_at) = size - i;
    return Binary_At(b, i);
}

#define Blob_Size_At_Ensure_Mutable(size_out,v) \
    m_cast(Byte*, Blob_Size_At((size_out), Ensure_Mutable(v)))

#define Blob_At(v) \
    Blob_Size_At(nullptr, (v))

#define Blob_At_Ensure_Mutable(v) \
    m_cast(Byte*, Blob_At(Ensure_Mutable(v)))

#define Blob_At_Known_Mutable(v) \
    m_cast(Byte*, Blob_At(Known_Mutable(v)))

#define Init_Blob(out,blob) \
    Init_Series((out), TYPE_BLOB, (blob))

#define Init_Blob_At(out,blob,offset) \
    Init_Series_At((out), TYPE_BLOB, (blob), (offset))


//=//// GLOBAL BINARIES //////////////////////////////////////////////////=//

#define EMPTY_BINARY \
    g_empty_blob

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
    const Value* cell,
    Option(const Length*) limit_in
){
    Option(Heart) heart = Heart_Of(cell);
    assert(Any_Bytes_Heart(heart));

    Length len_at;
    if (heart == TYPE_BLOB)
        Blob_Size_At(&len_at, cell);
    else
        len_at = String_Len_At(cell);

    Length limit;
    if (limit_in == UNLIMITED or *(unwrap limit_in) > len_at)
        limit = len_at;
    else
        limit = *(unwrap limit_in);

    Corrupt_If_Needful(limit_in);

    if (heart == TYPE_BLOB) {
        *size_out = limit;
        return Blob_At(cell);
    }

    if (Any_Utf8_Type(heart)) {
        *size_out = String_Size_Limit_At(nullptr, cell, &limit);
        return String_At(cell);
    }

    assert(Heart_Of(cell) == TYPE_WORD);
    assert(limit == Series_Len_At(cell));

    const Symbol* symbol = Word_Symbol(cell);
    *size_out = Strand_Size(symbol);
    return Strand_Head(symbol);
}

#define Cell_Bytes_At(size_out,v) \
    Cell_Bytes_Limit_At((size_out), (v), UNLIMITED)



//=//// CELL REPRESENTATION OF NUL CODEPOINT (USES #{00} BLOB!) ///////////=//
//
// Ren-C's unification of chars and "RUNE!" to a single immutable stringlike
// type meant they could not physically contain a zero codepoint.
//
// It would be possible to declare the empty rune of #"" representing the
// NUL codepoint state.  But that would be odd, since inserting empty strings
// into other strings is considered to be legal and not change the string.
// saying that (insert "abc" #"") would generate an illegal-zero-byte error
// doesn't seem right.
//
// So to square this circle, the NUL state is chosen to be represented simply
// as the #{00} binary BLOB!.  That gives it the desired properties of an
// error if you try to insert it into a string, but still allowing you to
// insert it into blobs.
//
// To help make bring some uniformity to this, the CODEPOINT OF function
// will give back codepoints for binaries that represent UTF-8, including
// giving back 0 for #{00}.  CODEPOINT OF thus works on all strings, e.g.
// (codepoint of <A>) -> 65.  But the only way you can get 0 back is if you
// call it on #{00}
//

INLINE bool Is_Blob_And_Is_Zero(const Value* v) {
    if (Heart_Of(v) != TYPE_BLOB)
        return false;

    Size size;
    const Byte* at = Blob_Size_At(&size, v);
    if (size != 1)
        return false;

    return *at == 0;
}
