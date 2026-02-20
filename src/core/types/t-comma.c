//
//  file: %t-comma.c
//  summary: "Comma Datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020 Ren-C Open Source Contributors
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


// BLANK! does not have FORM or MOLD behavior at this time.  Decorated forms
// (like QUOTED! or PINNED! etc.) will render the decoration without anything
// else additional.  Non-decorated BLANK! shows up as a comma when a List
// type encounters them--but that molding is done by the list.
//
IMPLEMENT_GENERIC(MOLDIFY, Is_Blank)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = ARG(FORM);

    UNUSED(v);
    UNUSED(mo);
    UNUSED(form);

    return fail ("BLANK! does not have a molded/formed representation");
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Blank)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);
    UNUSED(ARG(RELAX));

    assert(Is_Blank(v1) and Is_Blank(v2));
    UNUSED(v1);
    UNUSED(v2);

    return LOGIC_OUT(true);  // all commas are equal
}
