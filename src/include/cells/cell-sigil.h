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

INLINE char Symbol_For_Sigil(Sigil sigil) {
    switch (sigil) {
      case SIGIL_META:  return '^';
      case SIGIL_TYPE:  return '&';
      case SIGIL_THE:   return '@';
      case SIGIL_VAR:   return '$';
      case SIGIL_QUOTE: return '\'';
      default:
        assert(false);
        return 0;  // silence warning
    }
}

INLINE Element* Init_Sigil(Init(Element) out, Sigil sigil) {
    if (sigil == SIGIL_QUASI)
        Init_Utf8_Non_String(
            out,
            TYPE_SIGIL,
            cast(Utf8(const*), "~~"),  // must be "validated UTF-8"
            2,  // 2 codepoints
            2);  // in 2 bytes
    else {
        Init_Char_Unchecked(out, Symbol_For_Sigil(sigil));
        HEART_BYTE(out) = TYPE_SIGIL;
    }

    out->extra.at_least_4[IDX_EXTRA_SIGIL] = sigil;
    return out;
}

INLINE Sigil Cell_Sigil(const Cell* cell) {
    assert(Cell_Heart(cell) == TYPE_SIGIL);
    Byte sigil_byte = cell->extra.at_least_4[IDX_EXTRA_SIGIL];
    assert(sigil_byte != SIGIL_0 and sigil_byte <= MAX_SIGIL);
    return u_cast(Sigil, sigil_byte);
}

INLINE Option(Sigil) Sigil_Of_Type(Type t) {
    if (t == TYPE_QUOTED)
        return SIGIL_QUOTE;
    if (t == TYPE_QUASIFORM)
        return SIGIL_QUASI;
    if (Any_Meta_Type(t))
        return SIGIL_META;
    if (Any_Type_Type(t))
        return SIGIL_TYPE;
    if (Any_The_Type(t))
        return SIGIL_THE;
    if (Any_Var_Type(t))
        return SIGIL_VAR;
    return SIGIL_0;
}

#define Sigil_Of(e) \
    Sigil_Of_Type(Type_Of(e))
