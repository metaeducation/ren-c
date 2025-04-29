//
//  file: %stub-binary.h
//  summary: "Definitions for Binary, a width-1 Flex that can hold any byte"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// A BLOB! value holds a byte-size Flex, which is called a "Binary".  The bytes
// may be arbitrary, or if the Flex flavor is FLAVOR_NONSYMBOL or FLAVOR_SYMBOL
// then the data is constrained to only allow valid `\0`-terminated UTF-8 data.
//
// (Such binary "views" are possible due to things like the AS operator,
// e.g `as blob! "abc"`)
//


//=//// BINARY STUB SLOT USAGE //////////////////////////////////////////=//
//
// A BLOB! can be a "view" on a String Flex, which can be a Symbol or a
// non-Symbol String.  This means that generally speaking you can't use the
// Binary.misc or Binary.link for other purposes.
//
// Although...R3-Alpha used a Binary Flex to hold the data for BITSET!.  And
// the Binary.misc held a flag for whether the bitset was negated.  At the
// moment, bitsets binaries can't be extracted and aliases, so you can't do:
//
//    as text! as blob! make bitset! [...]
//
// But if you could do that, it would be a problem--as the negated bit would
// compete with the String's usages of misc/link.
//

#define MISC_BITSET_NEGATED(binary)  (binary)->misc.bit


//=//// BLOB (BYTE FLEX USED BY BLOB! SERIES) ///////////////////////////=//

#define Binary_At(b,i)        Flex_At(Byte, (b), (i))
#define Binary_Head(b)        Flex_Head(Byte, (b))
#define Binary_Tail(b)        Flex_Tail(Byte, (b))
#define Binary_Last(b)        Flex_Last(Byte, (b))

INLINE Length Binary_Len(const Binary* b) {
    assert(Flex_Wide(b) == 1);
    return Flex_Used(b);
}

#define Term_Binary(b) \
    *Binary_Tail(b) = '\0'

INLINE void Term_Binary_Len(Binary* b, Length len) {
    assert(Flex_Wide(b) == 1);
    Set_Flex_Used(b, len);
    Term_Binary(b);
}

// Make a byte-width Flex of length 0 with given capacity (plus 1, to permit
// a '\0' terminator).  Binaries are given enough capacity to have a NUL
// terminator in case they are aliased as UTF-8 later, e.g. `as word! binary`,
// since it could be costly to give them that capacity after-the-fact.
//
INLINE Binary* Make_Binary_Core(Flags flags, Size capacity)
{
    assert(Flavor_From_Flags(flags) == 0);  // shouldn't pass in a flavor

    Binary* b = Make_Flex(
        FLAG_FLAVOR(BINARY) | flags,
        Binary,
        capacity + 1
    );
  #if DEBUG_POISON_FLEX_TAILS
    *Flex_Head(Byte, b) = BINARY_BAD_UTF8_TAIL_BYTE;  // reserve for '\0'
  #endif
    return b;
}

#define Make_Binary(capacity) \
    Make_Binary_Core(FLEX_FLAGS_NONE, (capacity))
