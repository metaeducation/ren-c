//
//  File: %cell-unreadable.h
//  Summary: "Unreadable Variant of Quasi-Blank Available In Early Boot"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// Unreadable cells are write-only cells that in the debug build, will fail
// on most forms of read access in the system.  However, it will behave
// neutrally as far as the garbage collector is concerned.
//
// This is useful anytime a placeholder is needed in a non-user-exposed slot,
// where the code knows it's supposed to come back and fill something in at
// a later time--spanning an evaluation.  Debug asserts help catch cases where
// it's accidentally read from.
//
// It will panic if you try to test it and will also refuse VAL_TYPE() checks.
// The only way to check if something is unreadable is with Is_Unreadable().
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * The low-level type used to store these cells is a quasiform ~unreadable~
//   word with NODE_FLAG_FREE set.  While an antiform might seem more desirable
//   to draw attention if these leak to userspace in the release build,
//   quasiform cells can be used in blocks.  It would break more invariants and
//   possibly cause more damage for antiforms to appear in those places.
//
// * This was originally a debug-build-only feature...so release builds would
//   not set the NODE_FLAG_FREE bit on unreadable cells.  That means the
//   unreadability couldn't be used for things like unused map elements,
//   because the release build wouldn't see the bit.  Yet it turns out that
//   it's fairly desirable to allow the unreadable bit to be meaningful for
//   such cases.  So the only difference is that the release build does not
//   raise alerts about the bit being set--not that the bit isn't there.

INLINE Element* Init_Unreadable_Untracked(Sink(Element*) out) {
    Init_Quasi_Word(out, Canon(UNREADABLE));
    Set_Node_Free_Bit(out);  // won't be readable, but still writable
    return out;
}

INLINE bool Is_Unreadable(const Cell* c) {
    if (not Is_Node_Free(c))
        return false;
    assert(Is_Node(c) and Is_Node_A_Cell(c));
    assert(HEART_BYTE(c) == REB_WORD);
    assert(QUOTE_BYTE(c) == QUASIFORM_2);
    assert(Cell_Word_Id(c) == SYM_UNREADABLE);
    return true;
}

#define Init_Unreadable(out) \
    TRACK(Init_Unreadable_Untracked((out)))
