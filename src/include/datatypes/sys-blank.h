//
//  File: %sys-blank.h
//  Summary: "BLANK! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// BLANK! values are a kind of "reified" null, and you can convert between
// them using TRY and OPT:
//
//     >> try null
//     == _
//
//     >> opt _
//     ; null
//
// Like NULL, they are considered to be false (like the LOGIC! #[false], which
// is the only other conditionally false value).  But unlike NULL they can be
// put in blocks.  Hence they can serve as a placeholder when one wishes to
// convey "nothing".
//
// Blanks play an important role in a convention known as "blank in, null out".
// Many functions do not accept NULL as input, but will allow a BLANK! as
// input but then return NULL.  This helps create chains with error locality:
//
//    >> length of select [a 10 b 20] 'c
//    ** Error: Can't take LENGTH OF NULL
//
//    >> length of try select [a 10 b 20] 'c  ; try produces a BLANK!
//    ; null
//
// This is part of a broader idea that blanks act as a purposeful "opt-out"
// while NULL is more of a "soft failure".  TRY is a tool for converting what
// would cause an error message into something that can then be further tested
// for soft failure.
//

inline static REBVAL *Init_Blank_Untracked(RELVAL *out) {
    INIT_VAL_HEADER(out, REB_BLANK, CELL_MASK_NONE);

  #ifdef ZERO_UNUSED_CELL_FIELDS
    EXTRA(Any, out).trash = ZEROTRASH;
    PAYLOAD(Any, out).first.trash = ZEROTRASH;
    PAYLOAD(Any, out).second.trash = ZEROTRASH;
  #endif

    return cast(REBVAL*, out);
}

#define Init_Blank(out) \
    Init_Blank_Untracked(TRACK_CELL_IF_DEBUG(out))
