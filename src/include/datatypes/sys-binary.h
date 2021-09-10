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
    inline static REBBIN *BIN(void *p)
        { return reinterpret_cast<REBBIN*>(p); }
    inline static const REBBIN *BIN(const void *p)
        { return reinterpret_cast<const REBBIN*>(p); }
#else
    #define BIN(p) cast(REBBIN*, (p))
#endif


//=//// BINARY! SERIES ////////////////////////////////////////////////////=//

inline static REBYTE *BIN_AT(const_if_c REBBIN *bin, REBLEN n)
  { return SER_AT(REBYTE, bin, n); }

inline static REBYTE *BIN_HEAD(const_if_c REBBIN *bin)
  { return SER_HEAD(REBYTE, bin); }

inline static REBYTE *BIN_TAIL(const_if_c REBBIN *bin)
  { return SER_TAIL(REBYTE, bin); }

inline static REBYTE *BIN_LAST(const_if_c REBBIN *bin)
  { return SER_LAST(REBYTE, bin); }

#if CPLUSPLUS_11
    inline static const REBYTE *BIN_AT(const REBBIN *bin, REBLEN n)
      { return SER_AT(const REBYTE, bin, n); }

    inline static const REBYTE *BIN_HEAD(const REBBIN *bin)
      { return SER_HEAD(const REBYTE, bin); }

    inline static const REBYTE *BIN_TAIL(const REBBIN *bin)
      { return SER_TAIL(const REBYTE, bin); }

    inline static const REBYTE *BIN_LAST(const REBBIN *bin)
      { return SER_LAST(const REBYTE, bin); }
#endif

inline static REBLEN BIN_LEN(const REBBIN *s) {
    assert(SER_WIDE(s) == 1);
    return SER_USED(s);
}

inline static void TERM_BIN(REBBIN *s) {
    *BIN_TAIL(s) = '\0';
}

inline static void TERM_BIN_LEN(REBBIN *s, REBLEN len) {
    assert(SER_WIDE(s) == 1);
    SET_SERIES_USED(s, len);
    *BIN_TAIL(s) = '\0';
}

// Make a byte series of length 0 with the given capacity (plus 1, to permit
// a '\0' terminator).  Binaries are given enough capacity to have a null
// terminator in case they are aliased as UTF-8 later, e.g. `as word! binary`,
// since it could be costly to give them that capacity after-the-fact.
//
inline static REBBIN *Make_Binary_Core(REBLEN capacity, REBFLGS flags)
{
    assert(FLAVOR_BYTE(flags) == 0);  // shouldn't pass in a flavor

    REBSER *s = Make_Series(capacity + 1, FLAG_FLAVOR(BINARY) | flags);
  #if !defined(NDEBUG)
    *SER_HEAD(REBYTE, s) = BINARY_BAD_UTF8_TAIL_BYTE;  // reserve for '\0'
  #endif
    return BIN(s);
}

#define Make_Binary(capacity) \
    Make_Binary_Core(capacity, SERIES_FLAGS_NONE)


//=//// BINARY! VALUES ////////////////////////////////////////////////////=//

inline static const REBBIN *VAL_BINARY(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_BINARY);
    return BIN(VAL_SERIES(v));
}

#define VAL_BINARY_ENSURE_MUTABLE(v) \
    m_cast(REBBIN*, VAL_BINARY(ENSURE_MUTABLE(v)))

#define VAL_BINARY_KNOWN_MUTABLE(v) \
    m_cast(REBBIN*, VAL_BINARY(KNOWN_MUTABLE(v)))


inline static const REBYTE *VAL_BINARY_SIZE_AT(
    REBSIZ *size_at_out,
    REBCEL(const*) v
){
    const REBBIN *bin = VAL_BINARY(v);
    REBIDX i = VAL_INDEX_RAW(v);
    REBSIZ size = BIN_LEN(bin);
    if (i < 0 or i > cast(REBIDX, size))
        fail (Error_Index_Out_Of_Range_Raw());
    if (size_at_out)
        *size_at_out = size - i;
    return BIN_AT(bin, i);
}

#define VAL_BINARY_SIZE_AT_ENSURE_MUTABLE(size_out,v) \
    m_cast(REBYTE*, VAL_BINARY_SIZE_AT((size_out), ENSURE_MUTABLE(v)))

#define VAL_BINARY_AT(v) \
    VAL_BINARY_SIZE_AT(nullptr, (v))

#define VAL_BINARY_AT_ENSURE_MUTABLE(v) \
    m_cast(REBYTE*, VAL_BINARY_AT(ENSURE_MUTABLE(v)))

#define VAL_BINARY_AT_KNOWN_MUTABLE(v) \
    m_cast(REBYTE*, VAL_BINARY_AT(KNOWN_MUTABLE(v)))

#define Init_Binary(out,bin) \
    Init_Any_Series((out), REB_BINARY, (bin))

#define Init_Binary_At(out,bin,offset) \
    Init_Any_Series_At((out), REB_BINARY, (bin), (offset))


//=//// GLOBAL BINARIES //////////////////////////////////////////////////=//

#define EMPTY_BINARY \
    Root_Empty_Binary

#define BYTE_BUF TG_Byte_Buf
