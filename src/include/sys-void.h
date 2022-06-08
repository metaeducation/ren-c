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
// * Cells are allowed to be memset() to 0 and report that they are void, for
//  efficiency...even though they don't have NODE_FLAG_NODE or NODE_FLAG_CELL.


inline static RELVAL *Prep_Cell_Untracked(RELVAL *c) {
    ALIGN_CHECK_CELL_EVIL_MACRO(c);
    c->header.bits = CELL_MASK_PREP;
    return c;
}

#define Prep_Cell(c) \
    TRACK(Prep_Cell_Untracked(c))  // TRACK() expects REB_0, so track *after*


// Optimized Prep, with no guarantee about the prior condition of the bits.
//
inline static REBVAL *Prep_Stale_Void_Untracked(RELVAL *out) {
    out->header.bits = NODE_FLAG_NODE | NODE_FLAG_CELL | CELL_FLAG_STALE;
    return cast(REBVAL*, out);
}

#define Prep_Stale_Void(out) \
    TRACK(Prep_Stale_Void_Untracked(out))  // TRACK() expects REB_0, call after


inline static REBVAL *Prep_Void_Untracked(RELVAL *out) {
    out->header.bits = NODE_FLAG_NODE | NODE_FLAG_CELL;
    return cast(REBVAL*, out);
}

#define Prep_Void(out) \
    TRACK(Prep_Void_Untracked(out))  // TRACK() expects REB_0, call after


inline static bool Is_Void(const REBVAL *out) {
    assert(not (out->header.bits & CELL_FLAG_STALE));
    return VAL_TYPE_UNCHECKED(out) == REB_0;
}

inline static bool Is_Stale_Void(const RELVAL *out) {
    if (not (out->header.bits & CELL_FLAG_STALE))
        return false;
    return VAL_TYPE_UNCHECKED(out) == REB_0;
}

inline static REBVAL *Init_Stale_Void_Untracked(RELVAL *out) {
    Reset_Cell_Header_Untracked(out, REB_0, CELL_FLAG_STALE);

  #ifdef ZERO_UNUSED_CELL_FIELDS
    EXTRA(Any, out).trash = ZEROTRASH;
    PAYLOAD(Any, out).first.trash = ZEROTRASH;
    PAYLOAD(Any, out).second.trash = ZEROTRASH;
  #endif

    return cast(REBVAL*, out);
}

#define Init_Stale_Void(out) \
    Init_Stale_Void_Untracked(TRACK(out))
