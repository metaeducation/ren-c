//
//  File: %sys-binary.h
//  Summary: {Definitions for binary series}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A BINARY! value holds a byte-size series.  The bytes may be arbitrary, or
// if the series has SERIES_FLAG_IS_STRING then modifications are constrained
// to only allow valid UTF-8 data.  Such binary "views" are possible due to
// things like the AS operator (`as binary! "abc"`).
//
// R3-Alpha used a binary series to hold the data for BITSET!.  See notes in
// %sys-bitset.h regarding this usage (which has a "negated" bit in the
// MISC() field).
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Since strings use MISC() and LINK() for various features, and binaries
//   can be "views" on string series, this means that generally speaking a
//   binary series can't use MISC() and LINK() for its own purposes.  (For
//   the moment, typesets cannot be aliased, so you can't get into a situation
//   like `as text! as binary! make bitset! [...]`)


#if CPLUSPLUS_11  // !!! Make fancier checks, as with SER() and ARR()
    inline static Binary* BIN(void *p)
        { return reinterpret_cast<Binary*>(p); }
    inline static const Binary* BIN(const void *p)
        { return reinterpret_cast<const Binary*>(p); }
#else
    #define BIN(p) cast(Binary*, (p))
#endif


//=//// BINARY! SERIES ////////////////////////////////////////////////////=//

inline static Byte* Binary_At(const_if_c Binary* bin, REBLEN n)
  { return Series_At(Byte, bin, n); }

inline static Byte* Binary_Head(const_if_c Binary* bin)
  { return Series_Head(Byte, bin); }

inline static Byte* Binary_Tail(const_if_c Binary* bin)
  { return Series_Tail(Byte, bin); }

inline static Byte* Binary_Last(const_if_c Binary* bin)
  { return Series_Last(Byte, bin); }

#if CPLUSPLUS_11
    inline static const Byte* Binary_At(const Binary* bin, REBLEN n)
      { return Series_At(const Byte, bin, n); }

    inline static const Byte* Binary_Head(const Binary* bin)
      { return Series_Head(const Byte, bin); }

    inline static const Byte* Binary_Tail(const Binary* bin)
      { return Series_Tail(const Byte, bin); }

    inline static const Byte* Binary_Last(const Binary* bin)
      { return Series_Last(const Byte, bin); }
#endif

inline static REBLEN Binary_Len(const Binary* s) {
    assert(Series_Wide(s) == 1);
    return Series_Used(s);
}

inline static void Term_Binary(Binary* s) {
    *Binary_Tail(s) = '\0';
}

inline static void Term_Binary_Len(Binary* s, REBLEN len) {
    assert(Series_Wide(s) == 1);
    Set_Series_Used(s, len);
    *Binary_Tail(s) = '\0';
}

// Make a byte series of length 0 with the given capacity (plus 1, to permit
// a '\0' terminator).  Binaries are given enough capacity to have a null
// terminator in case they are aliased as UTF-8 later, e.g. `as word! binary`,
// since it could be costly to give them that capacity after-the-fact.
//
inline static Binary* Make_Binary_Core(REBLEN capacity, Flags flags)
{
    assert(Flavor_From_Flags(flags) == 0);  // shouldn't pass in a flavor

    Binary* bin = Make_Series(Binary,
        capacity + 1,
        FLAG_FLAVOR(BINARY) | flags
    );
  #if DEBUG_POISON_SERIES_TAILS
    *Series_Head(Byte, bin) = BINARY_BAD_UTF8_TAIL_BYTE;  // reserve for '\0'
  #endif
    return bin;
}

#define Make_Binary(capacity) \
    Make_Binary_Core(capacity, SERIES_FLAGS_NONE)


//=//// BINARY! VALUES ////////////////////////////////////////////////////=//

inline static const Binary* VAL_BINARY(NoQuote(const Cell*) v) {
    assert(CELL_HEART(v) == REB_BINARY);
    return BIN(VAL_SERIES(v));
}

#define VAL_BINARY_Ensure_Mutable(v) \
    m_cast(Binary*, VAL_BINARY(Ensure_Mutable(v)))

#define VAL_BINARY_Known_Mutable(v) \
    m_cast(Binary*, VAL_BINARY(Known_Mutable(v)))


inline static const Byte* VAL_BINARY_SIZE_AT(
    Option(Size*) size_at_out,
    NoQuote(const Cell*) v
){
    const Binary* bin = VAL_BINARY(v);
    REBIDX i = VAL_INDEX_RAW(v);
    Size size = Binary_Len(bin);
    if (i < 0 or i > cast(REBIDX, size))
        fail (Error_Index_Out_Of_Range_Raw());
    if (size_at_out)
        *unwrap(size_at_out) = size - i;
    return Binary_At(bin, i);
}

#define VAL_BINARY_SIZE_AT_Ensure_Mutable(size_out,v) \
    m_cast(Byte*, VAL_BINARY_SIZE_AT((size_out), Ensure_Mutable(v)))

#define VAL_BINARY_AT(v) \
    VAL_BINARY_SIZE_AT(nullptr, (v))

#define VAL_BINARY_AT_Ensure_Mutable(v) \
    m_cast(Byte*, VAL_BINARY_AT(Ensure_Mutable(v)))

#define VAL_BINARY_AT_Known_Mutable(v) \
    m_cast(Byte*, VAL_BINARY_AT(Known_Mutable(v)))

#define Init_Binary(out,bin) \
    Init_Series_Cell((out), REB_BINARY, (bin))

#define Init_Binary_At(out,bin,offset) \
    Init_Series_Cell_At((out), REB_BINARY, (bin), (offset))


//=//// GLOBAL BINARIES //////////////////////////////////////////////////=//

#define EMPTY_BINARY \
    Root_Empty_Binary

#define BYTE_BUF TG_Byte_Buf
