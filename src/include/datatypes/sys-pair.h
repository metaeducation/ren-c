//
//  File: %sys-pair.h
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
// A "pairing" fits in a REBSER node, but actually holds two distinct REBVALs.

// !!! PAIR! is now generic, so it could theoretically store any type.  This
// was done to avoid creating new numeric representations in the core (e.g.
// 32-bit integers or lower precision floats) just so they could both fit in a
// cell.  But while it's technically possible, no rendering formats for
// other-valued pairs has been proposed.  So only integers and decimals are
// accepted for now in the PAIR! type.
//

inline static REBVAL *PAIRING_KEY(REBVAL *paired) {
    return paired + 1;
}


#define INIT_VAL_PAIR(v,pairing) \
    INIT_VAL_NODE1((v), (pairing))

inline static REBVAL *VAL_PAIRING(noquote(const Cell*) v) {
    assert(CELL_KIND(v) == REB_PAIR);
    return VAL(VAL_NODE1(v));
}

#define VAL_PAIR_X(v) \
    PAIRING_KEY(VAL(VAL_PAIRING(v)))

#define VAL_PAIR_Y(v) \
    VAL(VAL_PAIRING(v))

inline static REBDEC VAL_PAIR_X_DEC(noquote(const Cell*) v) {
    if (IS_INTEGER(VAL_PAIR_X(v)))
        return cast(REBDEC, VAL_INT64(VAL_PAIR_X(v)));
    return VAL_DECIMAL(VAL_PAIR_X(v));
}

inline static REBDEC VAL_PAIR_Y_DEC(noquote(const Cell*) v) {
    if (IS_INTEGER(VAL_PAIR_Y(v)))
        return cast(REBDEC, VAL_INT64(VAL_PAIR_Y(v)));
    return VAL_DECIMAL(VAL_PAIR_Y(v));
}

inline static REBI64 VAL_PAIR_X_INT(noquote(const Cell*) v) {
    if (IS_INTEGER(VAL_PAIR_X(v)))
        return VAL_INT64(VAL_PAIR_X(v));
    return ROUND_TO_INT(VAL_DECIMAL(VAL_PAIR_X(v)));
}

inline static REBDEC VAL_PAIR_Y_INT(noquote(const Cell*) v) {
    if (IS_INTEGER(VAL_PAIR_Y(v)))
        return VAL_INT64(VAL_PAIR_Y(v));
    return ROUND_TO_INT(VAL_DECIMAL(VAL_PAIR_Y(v)));
}

inline static REBVAL *Init_Pair(
    RELVAL *out,
    const RELVAL *x,
    const RELVAL *y
){
    assert(ANY_NUMBER(x));
    assert(ANY_NUMBER(y));

    Reset_Cell_Header_Untracked(out, REB_PAIR, CELL_FLAG_FIRST_IS_NODE);
    REBVAL *p = Alloc_Pairing();
    Copy_Cell(PAIRING_KEY(p), cast(const REBVAL*, x));
    Copy_Cell(p, cast(const REBVAL*, y));
    Manage_Pairing(p);
    INIT_VAL_PAIR(out, p);
    return cast(REBVAL*, out);
}

inline static REBVAL *Init_Pair_Int(RELVAL *out, REBI64 x, REBI64 y) {
    Reset_Cell_Header_Untracked(out, REB_PAIR, CELL_FLAG_FIRST_IS_NODE);
    REBVAL *p = Alloc_Pairing();
    Init_Integer(PAIRING_KEY(p), x);
    Init_Integer(p, y);
    Manage_Pairing(p);
    INIT_VAL_PAIR(out, p);
    return cast(REBVAL*, out);
}

inline static REBVAL *Init_Pair_Dec(RELVAL *out, REBDEC x, REBDEC y) {
    Reset_Cell_Header_Untracked(out, REB_PAIR, CELL_FLAG_FIRST_IS_NODE);
    REBVAL *p = Alloc_Pairing();
    Init_Decimal(PAIRING_KEY(p), x);
    Init_Decimal(p, y);
    Manage_Pairing(p);
    INIT_VAL_PAIR(out, p);
    return cast(REBVAL*, out);
}
