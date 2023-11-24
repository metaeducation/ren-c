//
//  File: %stub-binary.h
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


//=//// BINARY! SERIES ////////////////////////////////////////////////////=//

#define Binary_At(bin,i)        Series_At(Byte, (bin), (i))
#define Binary_Head(bin)        Series_Head(Byte, (bin))
#define Binary_Tail(bin)        Series_Tail(Byte, (bin))
#define Binary_Last(bin)        Series_Last(Byte, (bin))

INLINE Length Binary_Len(const Binary* s) {
    assert(Series_Wide(s) == 1);
    return Series_Used(s);
}

#define Term_Binary(bin) \
    *Binary_Tail(bin) = '\0'

INLINE void Term_Binary_Len(Binary* bin, Length len) {
    assert(Series_Wide(bin) == 1);
    Set_Series_Used(bin, len);
    Term_Binary(bin);
}

// Make a byte series of length 0 with the given capacity (plus 1, to permit
// a '\0' terminator).  Binaries are given enough capacity to have a null
// terminator in case they are aliased as UTF-8 later, e.g. `as word! binary`,
// since it could be costly to give them that capacity after-the-fact.
//
INLINE Binary* Make_Binary_Core(Length capacity, Flags flags)
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
