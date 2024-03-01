//
//  File: %cell-void.h
//  Summary: {Non-"Element" for opting out, antiform used for unset variables}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// VOID is the result of branching constructs that don't take a branch, and if
// code evaluates to void then there will be no `==` in the console.
//
//     >> if false [<d>]
//
//     >> if true [<d>]
//     == <d>
//
// However, its implementation is the WORD! antiform of "void", so you can
// reveal that with a META operation:
//
//     >> meta if false [<d>]
//     == ~void~
//
// Array operations that try to add voids will be no-ops instead of errors:
//
//     >> append [a b c] if false [<d>]
//     == [a b c]
//

INLINE bool Is_Void(Need(const Value*) v) {
    ASSERT_CELL_READABLE(v);
    return QUOTE_BYTE(v) == ANTIFORM_0
        and HEART_BYTE(v) == REB_WORD
        and Cell_Word_Id(v) == SYM_VOID;
}

#define Init_Void(out) \
    Init_Anti_Word((out), Canon(VOID))

#define Init_Void_Untracked(out) \
    Init_Any_Word_Untracked(ensure(Sink(Value*), (out)), REB_WORD, \
        Canon(VOID), ANTIFORM_0)

#define Init_Quasi_Void(out) \
    Init_Quasi_Word((out), Canon(VOID))

INLINE bool Is_Quasi_Void(const Cell* v) {
    if (not Is_Quasiform(v))
        return false;
    if (HEART_BYTE(v) != REB_WORD)
        return false;
    return Cell_Word_Id(v) == SYM_VOID;
}

#define Init_Meta_Of_Void(out) \
    Init_Quasi_Void(out)

#define Is_Meta_Of_Void(v) \
    Is_Quasi_Void(v)


//=//// "HEAVY VOIDS" (BLOCK! Antiform Pack with ['] in it) ////////////////=//
//
// This is a way of making it so that branches which evaluate to void can
// carry the void intent, while being in a parameter pack--which is not
// considered a candidate for running ELSE branches:
//
//     >> if false [<a>]
//     ; void (will trigger ELSE)
//
//     >> if true []
//     == ~[~void~]~  ; anti (will trigger THEN, not ELSE)
//
//     >> append [a b c] if false [<a>]
//     == [a b c]
//
//     >> append [a b c] if true []
//     == [a b c]
//
// ("Heavy Nulls" are an analogous concept for NULL.)
//

#define Init_Heavy_Void(out) \
    Init_Pack((out), PG_1_Quasi_Void_Array)

INLINE bool Is_Heavy_Void(const Atom* v) {
    if (not Is_Pack(v))
        return false;
    const Element* tail;
    const Element* at = Cell_Array_At(&tail, v);
    return (tail == at + 1) and Is_Meta_Of_Void(at);
}

INLINE bool Is_Meta_Of_Heavy_Void(const Atom* v) {
    if (not Is_Meta_Of_Pack(v))
        return false;
    const Element* tail;
    const Element* at = Cell_Array_At(&tail, v);
    return (tail == at + 1) and Is_Meta_Of_Void(at);
}
