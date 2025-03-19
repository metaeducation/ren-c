//
//  File: %t-comma.c
//  Summary: "Comma Datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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


// The special behavior of commas makes them "glue" their rendering to the
// thing on their left.
//
IMPLEMENT_GENERIC(MOLDIFY, Is_Comma)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(element);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(molder));
    bool form = REF(form);

    UNUSED(form);
    UNUSED(v);

    Size size = String_Size(mo->string);
    if (
        size > mo->base.size + 1
        and *Binary_At(mo->string, size - 1) == ' '  // not multibyte char
        and *Binary_At(mo->string, size - 2) != ','  // also safe compare
    ){
        *Binary_At(mo->string, size - 1) = ',';
    }
    else
        Append_Codepoint(mo->string, ',');

    return NOTHING;
}


//
//  CT_Comma: C
//
// Must have a comparison function, otherwise SORT would not work on lists
// with commas in them.
//
REBINT CT_Comma(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);  // no strict form of comparison
    UNUSED(a);
    UNUSED(b);

    return 0;  // All commas are equal
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Comma)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    return LOGIC(CT_Comma(ARG(value1), ARG(value2), REF(strict)) == 0);
}
