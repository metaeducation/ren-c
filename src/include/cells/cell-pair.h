//
//  File: %cell-pair.h
//  Summary: {Definitions for Pairing Series and the Pair Datatype}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// A "pairing" fits in a Stub, but actually holds two distinct Cells.
//
// !!! PAIR! is now generic, so it could theoretically store any type.  This
// was done to avoid creating new numeric representations in the core (e.g.
// 32-bit integers or lower precision floats) just so they could both fit in a
// cell.  But while it's technically possible, no rendering formats for
// other-valued pairs has been proposed.  So only integers and decimals are
// accepted for now in the PAIR! type.
//

INLINE bool Any_Pairlike(NoQuote(const Cell*) v) {
    // called by core code, sacrifice READABLE() checks
    if (Cell_Heart_Unchecked(v) == REB_PAIR)
        return true;
    if (not Any_Sequence_Kind(Cell_Heart_Unchecked(v)))
        return false;
    if (Not_Cell_Flag(v, SEQUENCE_HAS_NODE))  // compressed bytes
        return false;
    return Is_Node_A_Cell(Cell_Node1(v));
}

#define INIT_VAL_PAIR(v,pairing) \
    Init_Cell_Node1((v), (pairing))

INLINE Cell* VAL_PAIRING(NoQuote(const Cell*) v) {
    assert(Any_Pairlike(v));
    return x_cast(Value(*), Cell_Node1(v));
}

#define VAL_PAIR_X(v) \
    SPECIFIC(VAL_PAIRING(v))

#define VAL_PAIR_Y(v) \
    SPECIFIC(Pairing_Second(VAL_PAIRING(v)))

INLINE REBI64 VAL_PAIR_X_INT(NoQuote(const Cell*) v) {
    if (Is_Integer(VAL_PAIR_X(v)))
        return VAL_INT64(VAL_PAIR_X(v));
    return ROUND_TO_INT(VAL_DECIMAL(VAL_PAIR_X(v)));
}

INLINE REBDEC VAL_PAIR_Y_INT(NoQuote(const Cell*) v) {
    if (Is_Integer(VAL_PAIR_Y(v)))
        return VAL_INT64(VAL_PAIR_Y(v));
    return ROUND_TO_INT(VAL_DECIMAL(VAL_PAIR_Y(v)));
}

INLINE Value(*) Init_Pair_Untracked(Cell* out, Cell* pairing) {
    assert(Is_Node_Managed(pairing));
    Reset_Unquoted_Header_Untracked(out, CELL_MASK_PAIR);
    INIT_VAL_PAIR(out, pairing);
    VAL_INDEX_RAW(out) = 0;  // "arraylike", needs an index
    mutable_BINDING(out) = UNBOUND;  // "arraylike", needs binding
    return SPECIFIC(out);
}

#define Init_Pair(out,pairing) \
    TRACK(Init_Pair_Untracked((out), (pairing)))

INLINE Value(*) Init_Pair_Int_Untracked(Cell* out, REBI64 x, REBI64 y) {
    Cell* p = Alloc_Pairing(NODE_FLAG_MANAGED);
    Init_Integer(p, x);
    Init_Integer(Pairing_Second(p), y);
    return Init_Pair_Untracked(out, p);
}

#define Init_Pair_Int(out,x,y) \
    TRACK(Init_Pair_Int_Untracked((out), (x), (y)))
