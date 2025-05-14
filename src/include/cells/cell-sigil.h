//
//  file: %cell-sigil.h
//  summary: "SIGIL! Decorator Type"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// At one time, things like $ and ^ and @ were "special" WORD!s.  These words
// caused problems since they could not be turned into forms with sigils,
// without a complex escaping mechanism.  Once the 64 total datatypes limit
// was lifted, it became feasible to give them the type SIGIL!
//

INLINE bool Any_Plain(const Element* e) {
    if (QUOTE_BYTE(e) != NOQUOTE_1)
        return false;
    return not (HEART_BYTE(e) & CELL_MASK_SIGIL_BITS);
}

#define Any_Lifted(v)  (Type_Of(v) == TYPE_LIFTED)
#define Any_Pinned(v)  (Type_Of(v) == TYPE_PINNED)
#define Any_Tied(v)    (Type_Of(v) == TYPE_TIED)

#define Is_Pinned(heartname, v) \
    (HEART_BYTE(v) == (u_cast(Byte, TYPE_##heartname) \
        | (u_cast(Byte, SIGIL_PIN) << HEART_SIGIL_SHIFT)))

#define Is_Lifted(heartname, v) \
    (HEART_BYTE(v) == (u_cast(Byte, TYPE_##heartname) \
        | (u_cast(Byte, SIGIL_LIFT) << HEART_SIGIL_SHIFT)))

#define Is_Tied(heartname, v) \
    (HEART_BYTE(v) == (u_cast(Byte, TYPE_##heartname) \
        | (u_cast(Byte, SIGIL_TIE) << HEART_SIGIL_SHIFT)))


INLINE char Char_For_Sigil(Sigil sigil) {
    switch (sigil) {
      case SIGIL_LIFT:  return '^';
      case SIGIL_PIN:   return '@';
      case SIGIL_TIE:   return '$';
      default:
        assert(false);
        return 0;  // silence warning
    }
}

INLINE Element* Init_Sigil(Init(Element) out, Sigil sigil) {
    Init_Char_Unchecked(out, Char_For_Sigil(sigil));
    HEART_BYTE(out) = TYPE_SIGIL;
    out->extra.at_least_4[IDX_EXTRA_SIGIL] = sigil;
    return out;
}

INLINE Sigil Cell_Sigil(const Cell* cell) {
    assert(Heart_Of(cell) == TYPE_SIGIL);
    Byte sigil_byte = cell->extra.at_least_4[IDX_EXTRA_SIGIL];
    assert(sigil_byte != SIGIL_0 and sigil_byte <= MAX_SIGIL);
    return u_cast(Sigil, sigil_byte);
}

INLINE Option(Sigil) Sigil_Of(const Element* e)
  { return u_cast(Sigil, HEART_BYTE_RAW(e) >> HEART_SIGIL_SHIFT); }

INLINE Element* Sigilize(Element* elem, Option(Sigil) sigil) {
    elem->header.bits &= ~(CELL_MASK_SIGIL_BITS);
    elem->header.bits |= FLAG_CELL_SIGIL(maybe sigil);
    return elem;
}

#define Plainify(elem) Sigilize((elem), SIGIL_0)
#define Liftify(elem)  Sigilize((elem), SIGIL_LIFT)
#define Pinify(elem)   Sigilize((elem), SIGIL_PIN)
#define Tieify(elem)   Sigilize((elem), SIGIL_TIE)

INLINE Element* Copy_Heart_Byte(Element* out, const Element* in) {
    HEART_BYTE(out) = HEART_BYTE(in);
    return out;
}
