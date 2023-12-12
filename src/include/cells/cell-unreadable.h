//
//  File: %cell-unreadable.h
//  Summary: "Unreadable Variant of Quasi-Void Available In Early Boot"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// The debug build has the idea of making an unreadable write-only cell that
// will fail on most forms of access in the system.  However, it will behave
// neutrally as far as the garbage collector is concerned.
//
// This is useful anytime a placeholder is needed in a non-user-exposed slot,
// where the code knows it's supposed to come back and fill something in at
// a later time--spanning an evaluation.  Debug asserts help catch cases where
// it's accidentally read from.
//
// it will panic if you try to test it and will also refuse VAL_TYPE() checks.
// The only way to check if something is unreadable is in the debug build, and
// hence should only appear in asserts.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * The low-level type used to store these cells is a quasi-void (~) with
//   NODE_FLAG_FREE set in the debug build.  While an isotope form might seem
//   more desirable to draw attention if these leak to userspace in the
//   release build, quasiform cells can be used in blocks.  It would break
//   more invariants and possibly cause more damage for isotopes to appear in
//   those places.
//
// * Something like the quasi-word ~unreadable~ might be better in drawing
//   attention to accidental exposure of unreadables to userspace in the
//   release build.  But there were some bootstrap issues with unreadable
//   cells being created before the symbol table was made.  Revisit.
//

#if DEBUG_UNREADABLE_CELLS
    INLINE Value(*) Init_Unreadable_Untracked(Cell* out) {
        Init_Void_Untracked(out, QUASI_2);
        Set_Node_Free_Bit(out);  // cell won't be READABLE(), but WRITABLE()
        return cast(Value(*), out);
    }

    INLINE bool Is_Unreadable_Debug(const Cell* v) {
        if (not Is_Node_Free(v))
            return false;
        assert(Is_Node(v) and Is_Node_A_Cell(v));
        assert(HEART_BYTE(v) == REB_VOID);
        assert(QUOTE_BYTE(v) == QUASI_2);
        return true;
    }

    #define Assert_Is_Unreadable_If_Debug(v) \
        assert(Is_Unreadable_Debug(v))
#else
    #define Init_Unreadable_Untracked(out) \
        Init_Void_Untracked((out), QUASI_2)

    #undef Is_Unreadable_Debug  // testing in release builds is not meaningful

    #define Assert_Is_Unreadable_If_Debug(v) NOOP
#endif

#define Init_Unreadable(out) \
    TRACK(Init_Unreadable_Untracked((out)))
