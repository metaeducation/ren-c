//
//  file: %d-validate.c
//  summary: "Non-inline helpers for cast validation"
//  project: "Ren-C Interpreter and Run-time"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//s
// Copyright 2025 Ren-C Open Source Contributors
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
// Casting hooks are defined early on in the build, because they have to be
// defined before any code using them.  But some of the validation work they
// do depends on inline functions that are defined later, and GCC is in
// particular picky about using inline functions in templated code.
//
// So when CTX_TYPE() wasn't able to be used in the ParamList* cast, the
// easiest way to work around it was to move the validation into a non-inline
// function that could be prototyped to use later in the build.
//

#include "sys-core.h"

#if DEBUG_CHECK_CASTS

//
//  Validate_Paramlist_Bits: C
//
void Validate_Paramlist_Bits(const Stub* stub) {
    if ((stub->header.bits & (
        (STUB_MASK_LEVEL_VARLIST
            & (~ STUB_FLAG_LINK_NEEDS_MARK)  // next virtual, maybe null
            & (~ STUB_FLAG_MISC_NEEDS_MARK)  // adjunct, maybe null
        )   | BASE_FLAG_UNREADABLE
            | BASE_FLAG_CELL
            | STUB_MASK_TASTE
    )) !=
        STUB_MASK_LEVEL_VARLIST
    ){
        crash (stub);
    }

    if (CTX_TYPE(x_cast(Context*, stub)) != HEART_FRAME)
        crash (stub);
}

#endif
