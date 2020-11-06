//
//  File: %sys-comma.h
//  Summary: "COMMA! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020 Ren-C Open Source Contributors
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
// The COMMA! is a datatype whose evaluator behavior is to act as what is
// referred to as an "expression barrier".  It will stop argument fulfillment,
// but if no argument fulfillment is in place then it has no effect.
//
//     >> 1 + 2,
//     == 3
//
//     >> 1 +, 2
//     ** Error: barrier hit during evaluation
//
// It has the property that it renders "glued" to the element to the left.
//
// (An original implementation of expression barriers used the heavier `|`
// character.  However that was considered non-negotiable as "alternate" in
// PARSE, where expression barriers would also be needed.  Also, it was a
// fairly big interruption visually...so comma was preferred.  It is still
// possible to get the same effect of an expression barrier with any user
// function, so `|` could be used for this in normal evaluation.)


inline static REBVAL *Init_Comma(RELVAL *out) {
    RESET_CELL(out, REB_COMMA, CELL_MASK_NONE);

    // Although COMMA! carries no data, it is not inert.  To make ANY_INERT()
    // fast, it's in the part of the list of bindable evaluative types.
    // This means the binding has to be nulled out in the cell to keep the
    // GC from crashing on it.
    //
    INIT_BINDING(out, nullptr);
    return cast(REBVAL*, out);
}
