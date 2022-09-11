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
// VOID is the isotopic state of NULL.  It represents the idea of "no value".
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * A cell with all its header bits 0 (Erased_Cell, CELL_MASK_0) is very
//   close to being a VOID.  Its HEART_BYTE() is 0 for REB_NULL, and its
//   QUOTE_BYTE() is ISOTOPE_0 to say it is an isotope.  However, it can't
//   be a valid cell from the API perspective because Detect_Rebol_Pointer()
//   would see the `\0` first byte, and that's a legal empty UTF-8 C string.
//
// * There is still leverage from the near overlap with erased cells...because
//   it only takes a single masking operation to add NODE_FLAG_NODE and
//   NODE_FLAG_CELL to make a valid void...in theory.  However, for debug
//   purposes, it may be desirable to add additional info to the cell.
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


inline static bool Is_Void(Cell(const*) v) {
    return HEART_BYTE(v) == REB_NULL and QUOTE_BYTE(v) == ISOTOPE_0;
}

#define Init_Void_Untracked(out) \
    Init_Nothing_Untracked((out), REB_NULL, ISOTOPE_0)

#define Init_Void(out) \
    TRACK(Init_Void_Untracked(out))



// The `~` isotope is chosen in particular by the system to represent variables
// that have not been assigned.  It has many benefits over choosing `~unset~`:
//
//  * Reduces noise in FRAME! to see which variables specialized
//
//  * Less chance for confusion since UNSET? takes a variable; if it were named
//    ~unset~ people would likely expect `(unset? ~unset~)` to work.
//
//  * Quick way to unset variables, simply `(var: ~)`

#define Init_Meta_Of_Void(out)       Init_Quasi_Null(out)
#define Is_Meta_Of_Void(v)           Is_Quasi_Null(v)


//=//// "HEAVY VOIDS" (BLOCK! Isotope Pack with `~` in it) ////////////////=//
//
// This is a way of making it so that branches which evaluate to void can
// carry the void intent, while being in a parameter pack--which is not
// considered a candidate for running ELSE branches:
//
//     >> if false [<a>]
//     ; void (will trigger ELSE)
//
//     >> if true []
//     == ~[~]~  ; isotope (will trigger THEN, not ELSE)
//
//     >> append [a b c] if false [<a>]
//     == [a b c]
//
//     >> append [a b c] if true []
//     == [a b c]
//
// ("Heavy Nulls" are an analogous concept for NULL.)
//
// Note: Before parameter packs were created, this was done with empty splices
// as ~()~, due to having similar behavior to voids when used with things like
// APPEND.  However, once parameter packs were allowed to represent voids in
// any of their arguments...it was less of a stretch to say that this was
// simply a parameter pack with a void in it.
//

#define Init_Heavy_Void(out) \
    Init_Pack((out), PG_1_Tilde_Array)

inline static bool Is_Heavy_Void(Cell(const*) v) {
    if (not Is_Pack(v))
        return false;
    Cell(const*) tail;
    Cell(const*) at = VAL_ARRAY_AT(&tail, v);
    return (tail == at + 1) and Is_Meta_Of_Void(at);
}

inline static bool Is_Meta_Of_Heavy_Void(Cell(const*) v) {
    if (not Is_Meta_Of_Pack(v))
        return false;
    Cell(const*) tail;
    Cell(const*) at = VAL_ARRAY_AT(&tail, v);
    return (tail == at + 1) and Is_Meta_Of_Void(at);
}


//=//// "NONE" (empty BLOCK! Isotope Pack, ~[]~) //////////////////////////=//
//
// This is the default RETURN for when you just write something like
// `func [return: <none>] [...]`.  It represents the intention of not having a
// return value, but reserving the right to not be treated as invisible, so
// that if one ever did imagine an interesting value for it to return, the
// callsites wouldn't have assumed it was invisible.
//
// (Even a function like PRINT has a potentially interesting return value,
// given that it channels through NULL if the print content vaporized and
// it printed nothing (not even a newline).  This lets you use it with ELSE,
// e.g. `print [...] else [...]`)
//
// It sensibly uses a parameter pack of length 0, to say there are no values.
// Hence it is an error to try to assign them (to SET-WORD!, SET-BLOCK!, etc.)
//

#define Init_None_Untracked(out) \
    Init_Pack_Untracked((out), ISOTOPE_0, EMPTY_ARRAY)

#define Init_None(out) \
    TRACK(Init_None_Untracked(out))

#define Init_Meta_Of_None(out) \
    TRACK(Init_Pack_Untracked((out), QUASI_2, EMPTY_ARRAY))

inline static bool Is_None(Cell(const*) v) {
    if (not Is_Pack(v))
        return false;
    Cell(const*) tail;
    Cell(const*) at = VAL_ARRAY_AT(&tail, v);
    return tail == at;
}

inline static bool Is_Meta_Of_None(Cell(const*) v) {
    if (not Is_Meta_Of_Pack(v))
        return false;
    Cell(const*) tail;
    Cell(const*) at = VAL_ARRAY_AT(&tail, v);
    return tail == at;
}
