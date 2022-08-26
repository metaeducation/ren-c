//
//  File: %sys-void.h
//  Summary: {Non-value type that signals feed termination and invisibility}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// VOID reprsents a state which is "more empty than NULL".  Some slots
// (such as the output slot of a frame) will tolerate this marker, but they
// are illegal most places...and will assert on typical tests like IS_BLOCK()
// or IS_WORD().  So tests on values must be guarded with Is_Void() to tolerate
// them...or the HEART_BYTE() lower-level accessors must be used.
//
// Another use for the VOID cell state is in an optimized array representation
// that fits 0 or 1 cells into the series node itself.  Since the cell lives
// where the content tracking information would usually be, there's no length.
// Hence the presence of an VOID cell in the slot indicates length 0.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * The main way to get VOIDs is through a call to RESET().  This uses the
//   unique advantage of being the 0 type to get to the void state through
//   a single masking operation.
//
// * Due to the needs of Detect_Rebol_Pointer(), we unfortunately can't use
//   the optimization that a header of all 0 would be interpreted as being
//   VOID, e.g. with a REB_NULL heart and ISOTOPE_0 set.  The reason is that
//   this conflates with an empty UTF-8 string.

#define VOID_CELL \
    c_cast(const REBVAL*, &PG_Void_Cell)


inline static Cell(*) Erase_Cell_Untracked(Cell(*) c) {
    ALIGN_CHECK_CELL_EVIL_MACRO(c);
    c->header.bits = CELL_MASK_0;
    return c;
}

#define Erase_Cell(c) \
    TRACK(Erase_Cell_Untracked(c))


inline static REBVAL *Prep_Void_Untracked(Cell(*) out) {
    ALIGN_CHECK_CELL_EVIL_MACRO(out);
    out->header.bits = (
        NODE_FLAG_NODE | NODE_FLAG_CELL
            | FLAG_HEART_BYTE(REB_NULL) | FLAG_QUOTE_BYTE(ISOTOPE_0)
            | CELL_MASK_NO_NODES
    );
    return cast(REBVAL*, out);
}

#define Prep_Void(out) \
    TRACK(Prep_Void_Untracked(out))



inline static bool Is_Void(Value(const*) v) {
    return HEART_BYTE(v) == REB_NULL and QUOTE_BYTE(v) == ISOTOPE_0;
}

inline static bool Is_Stale_Void(Value(const*) v) {
    if (not (v->header.bits & CELL_FLAG_STALE))
        return false;
    if (HEART_BYTE_UNCHECKED(v) != REB_NULL)
        return false;
    return QUOTE_BYTE_UNCHECKED(v) == ISOTOPE_0;
}
