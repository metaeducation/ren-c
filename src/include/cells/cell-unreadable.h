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
// Unreadable cells are write-only cells.  They're used when placeholder is
// needed in a non-user-exposed slot, where the code knows it's supposed to
// come back and fill something in at a later time--spanning an evaluation.
// Debug asserts help catch cases where it's accidentally read from.
//
// It will panic if you try to test it and will also refuse VAL_TYPE() checks.
// To check if something is unreadable, use Is_Cell_Unreadable().
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * This was originally a debug-build-only feature...so release builds would
//   not set the NODE_FLAG_UNREADABLE bit on unreadable cells.  That means the
//   unreadability couldn't be used for things like unused map elements,
//   because the release build wouldn't see the bit.  Yet it turns out that
//   it's fairly desirable to allow the unreadable bit to be meaningful for
//   such cases.  So the only difference is that the release build does not
//   raise alerts about the bit being set--not that the bit isn't there.

#define CELL_MASK_UNREADABLE \
    (NODE_FLAG_NODE | NODE_FLAG_CELL | NODE_FLAG_UNREADABLE \
        | CELL_FLAG_DONT_MARK_NODE1 | CELL_FLAG_DONT_MARK_NODE2 \
        | FLAG_HEART_BYTE(255) | FLAG_QUOTE_BYTE(255))

#define Init_Unreadable_Untracked(out) do { \
    STATIC_ASSERT_LVALUE(out);  /* evil macro: make it safe */ \
    Assert_Cell_Initable(out); \
    (out)->header.bits |= CELL_MASK_UNREADABLE; \
} while (0)


INLINE Element* Init_Unreadable_Untracked_Inline(Init(Element) out) {
    Init_Unreadable_Untracked(out);
    return out;
}

INLINE bool Is_Cell_Unreadable(const Cell* c) {
    if (not Not_Node_Readable(c))
        return false;
    assert((c->header.bits & CELL_MASK_UNREADABLE) == CELL_MASK_UNREADABLE);
    return true;
}

#define Init_Unreadable(out) \
    TRACK(Init_Unreadable_Untracked_Inline((out)))


#if RUNTIME_CHECKS && CPLUSPLUS_11 && (! DEBUG_STATIC_ANALYZING)
    //
    // We don't actually want things like Sink(Value) to set a cell's bits to
    // a corrupt pattern, as we need to be able to call Init_Xxx() routines
    // and can't do that on garbage.  But we don't want to Erase_Cell() either
    // because that would lose header bits like whether the cell is an API
    // value.  We use the Init_Unreadable_Untracked().
    //
    // Note that Init_Unreadable_Untracked() is an "evil macro" that checks
    // to be sure that its argument is an LVALUE, so we have to take an
    // address locally...but there's no function call.

    INLINE void Corrupt_If_Debug(Cell& ref)
      { Cell* c = &ref; Init_Unreadable_Untracked(c); }

  #if DEBUG_USE_CELL_SUBCLASSES
    INLINE void Corrupt_If_Debug(Atom& ref)
      { Atom* a = &ref; Init_Unreadable_Untracked(a); }

    INLINE void Corrupt_If_Debug(Value& ref)
      { Value* v = &ref; Init_Unreadable_Untracked(v); }

    INLINE void Corrupt_If_Debug(Element& ref)
      { Element* e = &ref; Init_Unreadable_Untracked(e); }
  #endif
#endif
