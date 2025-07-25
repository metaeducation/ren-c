//
//  file: %cell-bitset.h
//  summary: "BITSET! Datatype Header"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha bitsets were essentially an alternate interpretation of a BLOB!
// as a set of bits corresponding to integer or character values.  They could
// be built using a small "dialect" that supplied ranges of numbers separated
// by `-`, e.g. `make bitset! [3 - 10 20 - 50]`.
//
// Because bitsets didn't contain any numbers outside of their range, truly
// negating the bitset could be prohibitive.  e.g. the size of all Unicode
// codepoints that *aren't* spaces would take a very large number of bits
// to represent.  Hence the NEGATE operation on a bitset would keep the
// underlying byte data with an annotation on the Binary Stub that it
// was in a negated state, and searches would invert their results.
//
// !!! There were several bugs related to routines not heeding the negated
// bits, and only operating on the binary bits.  These are being reviewed:
//
// https://github.com/rebol/rebol-issues/issues/2371
//

#define MAX_BITSET 0x7fffffff

#define CELL_BITSET_BINARY(c)  CELL_PAYLOAD_1(c)


// Because a BITSET! can get very large, the negation state is stored
// as a boolean in the Flex.  Since negating a BITSET! is intended
// to affect all references, it has to be stored somewhere that all
// Cells would see a change--hence the field is in the Flex.

INLINE bool BITS_NOT(const Flex* f)
  { return MISC_BITSET_NEGATED(f); }

INLINE void INIT_BITS_NOT(Flex* f, bool negated)
  { MISC_BITSET_NEGATED(f) = negated; }


INLINE Binary* VAL_BITSET(const Cell* v) {
    assert(Heart_Of(v) == TYPE_BITSET);
    return cast(Binary*, CELL_BITSET_BINARY(v));
}

#define VAL_BITSET_Ensure_Mutable(v) \
    m_cast(Binary*, VAL_BITSET(Ensure_Mutable(v)))

INLINE Element* Init_Bitset(Init(Element) out, Binary* bset) {
    Assert_Stub_Managed(bset);

    Reset_Cell_Header_Noquote(out, CELL_MASK_BITSET);
    Corrupt_Unused_Field(out->extra.corrupt);
    CELL_BITSET_BINARY(out) = bset;
    Corrupt_Unused_Field(out->payload.split.two.corrupt);

    return out;
}


// Mathematical set operations for UNION, INTERSECT, DIFFERENCE
enum {
    SOP_NONE = 0, // used by UNIQUE (other flags do not apply)
    SOP_FLAG_BOTH = 1 << 0, // combine and interate over both series
    SOP_FLAG_CHECK = 1 << 1, // check other series for value existence
    SOP_FLAG_INVERT = 1 << 2 // invert the result of the search
};
