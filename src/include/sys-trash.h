//
//  File: %sys-trash.h
//  Summary: "Unreadable Variant of BAD-WORD! Available In Early Boot"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// Although the low-level type used to store these cells is REB_BAD_WORD, it
// will panic if you try to test it with IS_BAD_WORD(), and will also refuse
// VAL_TYPE() checks.  The only way to check if something IS_TRASH() is in the
// debug build, and hence should only appear in asserts.
//
// This is useful anytime a placeholder is needed in a slot temporarily where
// the code knows it's supposed to come back and fill in the correct thing
// later.  The panics help make sure it is never actually read.
//


// !!! Originally this function lived in the %sys-bad-word.h file, and was
// forward declared only in the #if (! DEBUG_UNREADABLE_TRASH) case.  While
// this worked most of the time, older MinGW cross compilers seemed to have a
// problem with that forward inline declaration.  So just define it here.
// Be sure to re-run the MinGW CI Builds if you rearrange this...
//
inline static REBVAL *Init_Bad_Word_Untracked(
    RELVAL *out,
    option(const REBSYM *) label,
    REBFLGS flags
){
    Reset_Cell_Header_Untracked(
        out,
        REB_BAD_WORD,
        CELL_FLAG_FIRST_IS_NODE | flags
    );

    // Due to being evaluator active and not wanting to disrupt the order in
    // %types.r, bad words claim to be bindable...but set the binding to null.
    // See %sys-ordered.h for more on all the rules that make this so.
    //
    mutable_BINDING(out) = nullptr;

    INIT_VAL_NODE1(out, label);
  #ifdef ZERO_UNUSED_CELL_FIELDS
    PAYLOAD(Any, out).second.trash = ZEROTRASH;
  #endif
    return cast(REBVAL*, out);
}


#if DEBUG_UNREADABLE_TRASH
    //
    // Debug behavior: `~` isotope with the NODE_FLAG_FREE set
    // Will trip up any access attempts via READABLE(), but can be overwritten

    #define Init_Trash(out) \
        Init_Bad_Word_Untracked(TRACK(out), nullptr, NODE_FLAG_FREE)

    inline static bool IS_TRASH(const RELVAL *v) {
        if (KIND3Q_BYTE_UNCHECKED(v) != REB_BAD_WORD)
            return false;
        return did (v->header.bits & NODE_FLAG_FREE);
    }
#else
    // Release Build Behavior: Looks just like an unset (`~` isotope)

    #define Init_Trash(out) \
        Init_Bad_Word_Untracked(TRACK(out), nullptr, CELL_MASK_NONE)

    #define IS_TRASH(v) false
#endif
