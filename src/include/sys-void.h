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
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * The main way to get VOIDs is through a call to RESET().  This uses the
//   unique advantage of being the 0 type to get to the void state through
//   a single masking operation.
//
// * A cell with all its header bits 0 (Erased_Cell, CELL_MASK_0) is very
//   close to being a VOID.  Its HEART_BYTE() is 0 for REB_NULL, and its
//   QUOTE_BYTE() is ISOTOPE_0 to say it is an isotope.  However, it can't
//   be a valid cell from the API perspective because Detect_Rebol_Pointer()
//   would see the `\0` first byte, and that's a legal empty UTF-8 C string.
//
// * There is still leverage from the near overlap with erased cells...because
//   the evaluator will set NODE_FLAG_NODE and NODE_FLAG_CELL along with
//   CELL_FLAG_STALE on the output cells it receives.  Hence a fresh cell
//   such as one from DECLARE_LOCAL() doesn't need any extra preparation
//   beyond being erased in order to be interpreted as a stale void on output.
//

#define VOID_CELL \
    c_cast(const REBVAL*, &PG_Void_Cell)


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


// For reasons of both efficiency and semantics, initializing voids is only
// allowed into cells that have no content (e.g. their memory started out at
// zero, they were cleared with Erase_Cell(), or they've been RESET()).
//
// The efficiency reason is that it avoids needing to mask out the bits that
// are not in CELL_MASK_PERSIST.  The semantic reason is that you typically
// do not want voids to be overwriting content, because they represent
// "nothingness" and need special handling to *avoid* overwriting things:
//
//    >> 1 + 2 void
//    == 3  ; good to make it harder to accidentally overwrite the 3
//
// (Exceptions to this behavior would be when things like variable cells are
// being overwritten to become unset, e.g. `x: 10, x: ~` should not leave
// the 10 in the variable cell...but overwrite it with void.)
//
inline static Value(*) Finalize_Void_Untracked(Value(*) out) {
    ASSERT_CELL_FRESH_EVIL_MACRO(out);  // can bitwise OR, need node+cell flags
    assert(
        HEART_BYTE_UNCHECKED(out) == 0
        and QUOTE_BYTE_UNCHECKED(out) == 0
    );
    out->header.bits |= (
        NODE_FLAG_NODE | NODE_FLAG_CELL  // might already be set, might not...
            /* | FLAG_HEART_BYTE(REB_NULL) */  // already 0
            /* | FLAG_QUOTE_BYTE(ISOTOPE_0) */  // already 0
    );
    return out;
}

#define Finalize_Void(out) \
    TRACK(Finalize_Void_Untracked(out))


// Note: Some tests for void internal to the evaluator react to the flag
// CELL_FLAG_OUT_NOTE_VOIDED vs. looking at the heart byte.  This is because
// the cell may be remembering a value from a previous evaluation in case it
// needs it, while void is bein used as the left input to an enfix operation.
//
inline static bool Is_Void(Cell(const*) v) {
    return HEART_BYTE(v) == REB_NULL and QUOTE_BYTE(v) == ISOTOPE_0;
}

inline static bool Is_Stale_Void(Cell(const*) v) {
    if (not (v->header.bits & CELL_FLAG_STALE))
        return false;
    if (HEART_BYTE_UNCHECKED(v) != REB_NULL)
        return false;
    return QUOTE_BYTE_UNCHECKED(v) == ISOTOPE_0;
}


inline static REBVAL* Reset_Cell_Untracked(Cell(*) v) {
    ASSERT_CELL_WRITABLE_EVIL_MACRO(v);
    v->header.bits &= NODE_FLAG_NODE | NODE_FLAG_CELL | CELL_MASK_PERSIST;
    return cast(REBVAL*, v);
}

#define RESET(v) \
    TRACK(Reset_Cell_Untracked(v))
        // ^-- track AFTER reset, so you can diagnose cell origin in WRITABLE()
