//
//  File: %sys-bitset.h
//  Summary: "BITSET! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// R3-Alpha bitsets were essentially an alternate interpretation of a BINARY!
// as a set of bits corresponding to integer or character values.  They could
// be built using a small "dialect" that supplied ranges of numbers separated
// by `-`, e.g. `make bitset! [3 - 10 20 - 50]`.
//
// Because bitsets didn't contain any numbers outside of their range, truly
// negating the bitset could be prohibitive.  e.g. the size of all Unicode
// codepoints that *aren't* spaces would take a very large number of bits
// to represent.  Hence the NEGATE operation on a bitset would keep the
// underlying binary data with an annotation on the series node that it
// was in a negated state, and searches would invert their results.
//
// !!! There were several bugs related to routines not heeding the negated
// bits, and only operating on the binary bits.  These are being reviewed:
//
// https://github.com/rebol/rebol-issues/issues/2371
//

#define MAX_BITSET 0x7fffffff

inline static bool BITS_NOT(const REBSER *s)
  { return s->misc.negated; }

inline static void INIT_BITS_NOT(REBSER *s, bool negated)
  { s->misc.negated = negated; }


inline static REBBIN *VAL_BITSET(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_BITSET);
    return BIN(VAL_NODE1(v));
}

#define VAL_BITSET_ENSURE_MUTABLE(v) \
    m_cast(REBBIN*, VAL_BITSET(ENSURE_MUTABLE(v)))

inline static REBVAL *Init_Bitset(RELVAL *out, REBBIN *bits) {
    Reset_Cell_Header_Untracked(out, REB_BITSET, CELL_FLAG_FIRST_IS_NODE);
    ASSERT_SERIES_MANAGED(bits);
    INIT_VAL_NODE1(out, bits);
    return cast(REBVAL*, out);
}


// Mathematical set operations for UNION, INTERSECT, DIFFERENCE
enum {
    SOP_NONE = 0, // used by UNIQUE (other flags do not apply)
    SOP_FLAG_BOTH = 1 << 0, // combine and interate over both series
    SOP_FLAG_CHECK = 1 << 1, // check other series for value existence
    SOP_FLAG_INVERT = 1 << 2 // invert the result of the search
};
