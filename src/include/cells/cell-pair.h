//
//  File: %cell-pair.h
//  Summary: {Definitions for Pairing Nodes and the Pair Datatype}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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

#define PAIRING_LEN 2

#define Pairing_Head(p) \
    c_cast(Element*, ensure(const Pairing*, (p)))

#define Pairing_Tail(p) \
    (Pairing_Head(p) + 2)

#define Pairing_First(p) \
    Pairing_Head(p)

#define Pairing_Second(p) \
    (Pairing_Head(p) + 1)


INLINE bool Any_Pairlike(const Cell* v) {
    // called by core code, sacrifice Ensure_Readable() checks
    if (Cell_Heart_Unchecked(v) == REB_PAIR)
        return true;
    if (not Any_Sequence_Kind(Cell_Heart_Unchecked(v)))
        return false;
    if (Not_Cell_Flag(v, SEQUENCE_HAS_NODE))  // compressed bytes
        return false;
    return Is_Node_A_Cell(Cell_Node1(v));
}

#define Tweak_Cell_Pairing(v,pairing) \
    Tweak_Cell_Node1((v), (pairing))

INLINE Pairing* Cell_Pairing(const Cell* v) {
    assert(Any_Pairlike(v));
    return x_cast(Pairing*, Cell_Node1(v));
}

#define Cell_Pair_First(v) \
    Pairing_First(Cell_Pairing(v))

#define Cell_Pair_Second(v) \
    Pairing_Second(Cell_Pairing(v))

INLINE REBI64 Cell_Pair_X(const Cell* v)
  { return VAL_INT64(Cell_Pair_First(v)); }

INLINE REBI64 Cell_Pair_Y(const Cell* v)
  { return VAL_INT64(Cell_Pair_Second(v)); }

INLINE Value* Init_Pair_Untracked(Sink(Element*) out, REBI64 x, REBI64 y) {
    Pairing* p = Alloc_Pairing(NODE_FLAG_MANAGED);
    Init_Integer(Pairing_First(p), x);
    Init_Integer(Pairing_Second(p), y);

    Reset_Cell_Header_Untracked(
        out,
        CELL_MASK_PAIR | FLAG_HEART_BYTE(REB_PAIR)
    );
    Tweak_Cell_Pairing(out, p);
  #ifdef ZERO_UNUSED_CELL_FIELDS
    PAYLOAD(Any, out).second.corrupt = CORRUPTZERO;  // payload second not used
  #endif
    BINDING(out) = UNBOUND;  // "arraylike", needs binding

    return out;
}

#define Init_Pair(out,x,y) \
    TRACK(Init_Pair_Untracked((out), (x), (y)))
