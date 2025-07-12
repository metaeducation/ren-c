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


// The special behavior of commas makes them "glue" their rendering to the
// thing on their left.
//
IMPLEMENT_GENERIC(MOLDIFY, Is_Comma)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form);
    UNUSED(v);

    Size size = Strand_Size(mo->strand);
    if (
        size > mo->base.size + 1
        and *Binary_At(mo->strand, size - 1) == ' '  // not multibyte char
        and *Binary_At(mo->strand, size - 2) != ','  // also safe compare
    ){
        *Binary_At(mo->strand, size - 1) = ',';
    }
    else
        Append_Codepoint(mo->strand, ',');

    return TRIPWIRE;
}


//
//  CT_Comma: C
//
// Must have a comparison function, otherwise SORT would not work on lists
// with commas in them.
//
REBINT CT_Comma(const Element* a, const Element* b, bool strict)
{
    UNUSED(strict);  // no strict form of comparison
    UNUSED(a);
    UNUSED(b);

    return 0;  // All commas are equal
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Comma)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not Bool_ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Comma(v1, v2, strict) == 0);
}
