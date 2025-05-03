//
//  file: %cell-void.h
//  summary: "Non-Array-Element antiform state used for opting out"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// "Stable void" (the antiform of the word void) is being rethought.
//
// NIHIL will become true unstable void.  Right now stable void is the state
// created to bridge nihil to stable parameters.  The new plan is that frames
// will always speak in terms of meta-values.
//

INLINE bool Is_Void(Need(const Value*) v) {
    Assert_Cell_Readable(v);
    return QUOTE_BYTE(v) == ANTIFORM_0
        and HEART_BYTE(v) == TYPE_WORD
        and Cell_Word_Id(v) == SYM_VOID;
}

#define Init_Void_Untracked(out) \
    Init_Any_Word_Untracked( \
        (out), \
        TYPE_WORD, \
        ANTIFORM_0_COERCE_ONLY,  /* VOID is valid keyword symbol */ \
        CANON(VOID))

#define Init_Void(out) \
    TRACK(Init_Void_Untracked(out))
