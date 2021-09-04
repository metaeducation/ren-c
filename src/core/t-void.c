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
//  MF_Bad_word: C
//
// Bad words have a label to help make it clearer why an ornery error-like
// value would be existing.
//
void MF_Bad_word(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form); // no distinction between MOLD and FORM

    Append_Codepoint(mo->series, '~');

    const REBSTR* label = VAL_BAD_WORD_LABEL(v);
    Append_Utf8(mo->series, STR_UTF8(label), STR_SIZE(label));

    Append_Codepoint(mo->series, '~');
}


//
//  MAKE_Bad_word: C
//
// Can be created from a label.
//
// !!! How to create an isotope form of a BAD-WORD! in usermode, without
// having to run an evaluation on a bad-word?  `make-isotope`?
//
REB_R MAKE_Bad_word(
    REBVAL *out,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(not parent);
    UNUSED(parent);

    if (IS_WORD(arg))
        return Init_Bad_Word(out, VAL_WORD_SYMBOL(arg));

    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Bad_word: C
//
// TO is disallowed, e.g. you can't TO convert an integer of 0 to a blank.
//
REB_R TO_Bad_word(REBVAL *out, enum Reb_Kind kind, const REBVAL *data) {
    UNUSED(out);
    fail (Error_Bad_Make(kind, data));
}


//
//  CT_Bad_word: C
//
// To make BAD-WORD! more useful, the spellings are used in comparison.  This
// makes this code very similar to CT_Word(), so it is shared.
//
REBINT CT_Bad_word(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    const REBSYM* label_a = VAL_BAD_WORD_LABEL(a);
    const REBSYM* label_b = VAL_BAD_WORD_LABEL(b);

    return Compare_Spellings(label_a, label_b, strict);
}


//
//  REBTYPE: C
//
REBTYPE(Bad_word)
{
    REBVAL *voided = D_ARG(1);

    switch (ID_OF_SYMBOL(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // taken care of by `voided` above.

        switch (VAL_WORD_ID(ARG(property))) {
          case SYM_LABEL:
            return Init_Word(D_OUT, VAL_BAD_WORD_LABEL(voided));

          default:
            break;
        }
        break; }

      case SYM_COPY: { // since `copy/deep [1 _ 2]` is legal, allow `copy _`
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(ARG(value)); // already referenced as `unit`

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        UNUSED(REF(deep));
        UNUSED(REF(types));

        RETURN (voided); }

      default: break;
    }

    return R_UNHANDLED;
}
