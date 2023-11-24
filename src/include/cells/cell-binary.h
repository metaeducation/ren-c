// %cell-binary.h

INLINE const Binary* Cell_Binary(NoQuote(const Cell*) v) {
    assert(Cell_Heart(v) == REB_BINARY);
    return c_cast(Binary*, Cell_Series(v));
}

#define Cell_Binary_Ensure_Mutable(v) \
    m_cast(Binary*, Cell_Binary(Ensure_Mutable(v)))

#define Cell_Binary_Known_Mutable(v) \
    m_cast(Binary*, Cell_Binary(Known_Mutable(v)))


INLINE const Byte* Cell_Binary_Size_At(
    Option(Size*) size_at_out,
    NoQuote(const Cell*) v
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
