//
//  File: %sys-trash.h
//  Summary: "Unreadable Variant of none Available In Early Boot"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2022 Ren-C Open Source Contributors
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
// The debug build has the concept of making an unreadable "trash" cell that
// will fail on most forms of access in the system.  However, it will behave
// neutrally as far as the garbage collector is concerned.  This means that
// it can be used as a placeholder for a value that will be filled in at
// some later time--spanning an evaluation.
//
// Although the low-level type used to store these cells is a quasi-void (~),
// it will panic if you try to test it and will also refuse VAL_TYPE() checks.
// The only way to check if something IS_TRASH() is in the debug build, and
// hence should only appear in asserts.
//
// This is useful anytime a placeholder is needed in a slot temporarily where
// the code knows it's supposed to come back and fill in the correct thing
// later.  The panics help make sure it is never actually read.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * While an isotope form might seem more desirable to draw more attention
//   in the release build, trash cells can be used in blocks.  It would break
//   more invariants and possibly cause more damage for isotopes to appear
//   in those places, so a quasiform is used.
//
// * Something more obvious like the word ~trash~ might be better, but there
//   were some bootstrap issues with trash cells being created before the
//   symbol table was made.  If that's reviewed, then this might be changed.
//


#if DEBUG_UNREADABLE_TRASH
    //
    // Debug behavior: `~` with the CELL_FLAG_STALE set
    // Will trip up any access attempts via READABLE(), but is WRITABLE()

    inline static Value(*) Init_Trash_Untracked(Cell(*) out) {
        Init_Void_Untracked(out, QUASI_2);
        Set_Cell_Flag(out, STALE);
        return cast(Value(*), out);
    }

    inline static bool IS_TRASH(Cell(const*) v) {
        if (CELL_HEART(v) != REB_VOID)
            return false;
        if (QUOTE_BYTE(v) != QUASI_2)
            return false;
        return Get_Cell_Flag_Unchecked(v, STALE);
    }
#else
    // Release Build Behavior: Looks just like a meta-none (`~` value)

    #define Init_Trash_Untracked(out) \
        Init_Void_Untracked((out), QUASI_2)

    #undef IS_TRASH  // testing for trash in release builds is not meaningful
#endif

#define Init_Trash(out) \
    TRACK(Init_Trash_Untracked((out)))
