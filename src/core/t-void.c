//
//  File: %t-void.c
//  Summary: "Symbolic type for representing an 'ornery' variable value"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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

#include "sys-core.h"


//
//  Makehook_Quasiform: C
//
// See also ANTI for making antiforms.
//
Bounce Makehook_Quasiform(Level* level_, Kind kind, Element* arg) {
    if (Is_Quoted(arg))  // QUOTED! competes for quote byte with quasiform
        return RAISE(Error_Bad_Make(kind, arg));

    // !!! Should it allow things that are already QUASIFORM! (?)  This does,
    // but Quasify() does not.

    Copy_Cell(OUT, arg);
    return Coerce_To_Quasiform(stable_OUT);
}


//
//  TO_Quasiform: C
//
// TO is disallowed at the moment (use QUASI)
//
Bounce TO_Quasiform(Level* level_, Kind kind, Element* arg) {
    return RAISE(Error_Bad_Make(kind, arg));
}


//
//  CT_Quasiform: C
//
REBINT CT_Quasiform(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(a); UNUSED(b); UNUSED(strict);
    assert(!"CT_Quasiform should never be called");
    return 0;
}


//
//  REBTYPE: C
//
REBTYPE(Quasiform)
{
    Value* quasi = D_ARG(1);

    switch (Symbol_Id(verb)) {
      case SYM_COPY: {  // since (copy:deep [1 ~ 2]) is legal, allow (copy '~)
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(ARG(value));

        if (REF(part))
            return FAIL(Error_Bad_Refines_Raw());

        UNUSED(REF(deep));

        return COPY(quasi); }

      default: break;
    }

    return UNHANDLED;
}
