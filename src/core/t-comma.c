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


//
//  MF_Comma: C
//
// The special behavior of commas makes them "glue" their rendering to the
// thing on their left.
//
void MF_Comma(Molder* mo, const Cell* v, bool form)
{
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


IMPLEMENT_GENERIC(equal_q, comma)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    return LOGIC(CT_Comma(ARG(value1), ARG(value2), REF(strict)) == 0);
}


//
//  DECLARE_GENERICS: C
//
DECLARE_GENERICS(Comma)
{
    switch (Symbol_Id(verb)) {
      case SYM_COPY: { // since (copy:deep [1, 2]) is legal, allow (copy ',)
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(ARG(value));

        if (REF(part))
            return FAIL(Error_Bad_Refines_Raw());

        UNUSED(REF(deep));

        return Init_Comma(OUT); }

      default: break;
    }

    return UNHANDLED;
}
