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
void MF_Comma(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);
    UNUSED(v);

    REBSIZ size = STR_SIZE(mo->series);
    if (
        size > mo->offset + 1
        and *BIN_AT(SER(mo->series), size - 1) == ' '  // not multibyte char
        and *BIN_AT(SER(mo->series), size - 2) != ','  // also safe compare
    ){
        *BIN_AT(SER(mo->series), size - 1) = ',';
    }
    else
        Append_Codepoint(mo->series, ',');
}


//
//  CT_Comma: C
//
// Must have a comparison function, otherwise SORT would not work on arrays
// with commas in them.
//
REBINT CT_Comma(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);  // no strict form of comparison
    UNUSED(a);
    UNUSED(b);

    return 0;  // All commas are equal
}


//
//  REBTYPE: C
//
REBTYPE(Comma)
{
    switch (VAL_WORD_SYM(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // taken care of by `unit` above.

        // !!! REFLECT cannot use REB_TS_NOOP_IF_BLANK, because of the special
        // case of TYPE OF...where a BLANK! in needs to provide BLANK! the
        // datatype out.  Also, there currently exist "reflectors" that
        // return LOGIC!, e.g. TAIL?...and logic cannot blindly return null:
        //
        // https://forum.rebol.info/t/954
        //
        // So for the moment, we just ad-hoc return nullptr for some that
        // R3-Alpha returned NONE! for.  Review.
        //
        switch (VAL_WORD_SYM(ARG(property))) {
          case SYM_INDEX:
          case SYM_LENGTH:
            return nullptr;

          default: break;
        }
        break; }

      case SYM_COPY: { // since `copy/deep [1 , 2]` is legal, allow `copy ,`
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(ARG(value));

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        UNUSED(REF(deep));
        UNUSED(REF(types));

        return Init_Comma(D_OUT); }

      default: break;
    }

    return R_UNHANDLED;
}
