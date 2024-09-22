//
//  File: %cell-sigil.h
//  Summary: "SIGIL! Decorator Type"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2024 Ren-C Open Source Contributors
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
// At one time, things like $ and ^ and : were "special" WORD!s.  These words
// caused problems since they could not be turned into forms with sigils,
// without a complex escaping mechanism.  Once the 64 total datatypes limit
// was lifted, it became feasible to give them the type SIGIL!
//

INLINE Element* Init_Sigil(Sink(Element*) out, Sigil sigil) {
    if (sigil == SIGIL_SET)
        Init_Issue_Utf8(out, cb_cast("::"), 2, 2);  // codepoints 2, size 2
    else if (sigil == SIGIL_QUASI)
        Init_Issue_Utf8(out, cb_cast("~~"), 2, 2);  // codepoints 2, size 2
    else {
        Codepoint c;
        switch (sigil) {
          case SIGIL_GET:   c = ':';    break;
          case SIGIL_META:  c = '^';    break;
          case SIGIL_TYPE:  c = '&';    break;
          case SIGIL_THE:   c = '@';    break;
          case SIGIL_VAR:   c = '$';    break;
          case SIGIL_QUOTE: c = '\'';   break;
          default:
            assert(false);
            c = 0;  // silence warning
        }
        Init_Char_Unchecked(out, c);
    }
    HEART_BYTE(out) = REB_SIGIL;
    EXTRA(Bytes, out).at_least_4[IDX_EXTRA_SIGIL] = sigil;
    return out;
}

INLINE Sigil Cell_Sigil(const Cell* cell) {
    assert(Cell_Heart(cell) == REB_SIGIL);
    Byte sigil_byte = EXTRA(Bytes, cell).at_least_4[IDX_EXTRA_SIGIL];
    assert(sigil_byte != 0 and sigil_byte < SIGIL_MAX);
    return u_cast(Sigil, sigil_byte);
}

INLINE Option(Sigil) Sigil_Of_Kind(Kind k) {
    if (k == REB_QUOTED)
        return SIGIL_QUOTE;
    if (k == REB_QUASIFORM)
        return SIGIL_QUASI;
    if (Any_Set_Kind(k))
        return SIGIL_SET;
    if (Any_Get_Kind(k))
        return SIGIL_GET;
    if (Any_Meta_Kind(k))
        return SIGIL_META;
    if (Any_Type_Kind(k))
        return SIGIL_TYPE;
    if (Any_The_Kind(k))
        return SIGIL_THE;
    if (Any_Var_Kind(k))
        return SIGIL_VAR;
    return SIGIL_0;
}

#define Sigil_Of(e) \
    Sigil_Of_Kind(VAL_TYPE(e))
