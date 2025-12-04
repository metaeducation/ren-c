//
//  file: %cell-pair.h
//  summary: "Definitions for Stub-sized Pairings and the Pair Datatype"
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
// A "pairing" fits in a STUB_POOL allocation, but actually holds two Cells.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * R3-Alpha's PAIR! type compressed two integers into a single cell, which
//   meant using smaller integer representations than the INTEGER! cell.
//   Ren-C didn't want to manage the limits and math on different types of
//   integer, so created a new compact generalized allocation for exactly
//   two cells.
//

#define PAIRING_LEN_2  2

#define SERIESLIKE_PAYLOAD_1_BASE(c)  CELL_PAYLOAD_1(c)

#define PAIRLIKE_PAYLOAD_1_PAIRING_BASE(c)  SERIESLIKE_PAYLOAD_1_BASE(c)

#define Pairing_Head(p) \
    u_cast(Element*, ensure(Pairing*, (p)))

#define Pairing_Tail(p) \
    (Pairing_Head(p) + PAIRING_LEN_2)

#define Pairing_First(p) \
    Pairing_Head(p)

#define Pairing_Second(p) \
    (Pairing_Head(p) + 1)


INLINE bool Is_Cell_Pairlike(const Cell* v) {
    // called by core code, sacrifice Ensure_Readable() checks
    if (Unchecked_Heart_Of(v) == TYPE_PAIR)
        return true;
    if (not Any_Sequence_Type(Unchecked_Heart_Of(v)))
        return false;
    if (not Sequence_Has_Pointer(v))  // compressed bytes
        return false;
    return Is_Base_A_Cell(SERIESLIKE_PAYLOAD_1_BASE(v));
}

INLINE Pairing* Cell_Pairing(const Cell* v) {
    assert(Is_Cell_Pairlike(v));
    return cast(Pairing*, PAIRLIKE_PAYLOAD_1_PAIRING_BASE(v));
}

#define Cell_Pair_First(v) \
    Pairing_First(Cell_Pairing(v))

#define Cell_Pair_Second(v) \
    Pairing_Second(Cell_Pairing(v))

INLINE REBI64 Cell_Pair_X(const Cell* v)
  { return VAL_INT64(Cell_Pair_First(v)); }

INLINE REBI64 Cell_Pair_Y(const Cell* v)
  { return VAL_INT64(Cell_Pair_Second(v)); }

INLINE Stable* Init_Pair_Untracked(Init(Element) out, REBI64 x, REBI64 y) {
    Pairing* p = Alloc_Pairing(BASE_FLAG_MANAGED);
    Init_Integer(Pairing_First(p), x);
    Init_Integer(Pairing_Second(p), y);

    Reset_Cell_Header_Noquote(out, CELL_MASK_PAIR);
    Corrupt_Unused_Field(out->extra.corrupt);
    PAIRLIKE_PAYLOAD_1_PAIRING_BASE(out) = p;
    Corrupt_Unused_Field(out->payload.split.two.corrupt);

    return out;
}

#define Init_Pair(out,x,y) \
    TRACK(Init_Pair_Untracked((out), (x), (y)))
