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

#define Is_Sigiled(heart,sigil,v) \
    ((Ensure_Readable(v)->header.bits & CELL_HEART_QUOTE_MASK) == \
        (FLAG_QUOTE_BYTE(NOQUOTE_1) | \
            (FLAG_HEART_ENUM(heart) | FLAG_SIGIL_ENUM(sigil))))

#define Is_Pinned(heartname, v) \
    Is_Sigiled(TYPE_##heartname, SIGIL_PIN, (v))

#define Is_Lifted(heartname, v) \
    Is_Sigiled(TYPE_##heartname, SIGIL_LIFT, (v))

#define Is_Tied(heartname, v) \
    Is_Sigiled(TYPE_##heartname, SIGIL_TIE, (v))


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

INLINE Option(Sigil) Sigil_Of(const Element* e)
  { return u_cast(Sigil, HEART_BYTE_RAW(e) >> HEART_SIGIL_SHIFT); }

INLINE Element* Sigilize(Element* elem, Option(Sigil) sigil) {
    elem->header.bits &= ~(CELL_MASK_SIGIL_BITS);
    elem->header.bits |= FLAG_SIGIL_ENUM(maybe sigil);
    return elem;
}

INLINE Element* Init_Sigil(Init(Element) out, Sigil sigil) {
    return Sigilize(Init_Blank(out), sigil);
}

INLINE bool Is_Sigil(const Element* e) {
    if (QUOTE_BYTE(e) != NOQUOTE_1)
        return false;
    return Heart_Of(e) == TYPE_BLANK and Sigil_Of(e);
}

INLINE bool Is_Lift_Sigil(const Cell* cell) {
    if (QUOTE_BYTE(cell) != NOQUOTE_1)
        return false;
    return HEART_BYTE(cell) == (u_cast(Byte, TYPE_BLANK) | FLAG_SIGIL(LIFT));
}

INLINE bool Is_Pin_Sigil(const Cell* cell) {
    if (QUOTE_BYTE(cell) != NOQUOTE_1)
        return false;
    return HEART_BYTE(cell) == (u_cast(Byte, TYPE_BLANK) | FLAG_SIGIL(PIN));
}

INLINE bool Is_Tie_Sigil(const Cell* cell) {
    if (QUOTE_BYTE(cell) != NOQUOTE_1)
        return false;
    return HEART_BYTE(cell) == (u_cast(Byte, TYPE_BLANK) | FLAG_SIGIL(TIE));
}


#define Plainify(elem) Sigilize((elem), SIGIL_0)
#define Liftify(elem)  Sigilize((elem), SIGIL_LIFT)
#define Pinify(elem)   Sigilize((elem), SIGIL_PIN)
#define Tieify(elem)   Sigilize((elem), SIGIL_TIE)

INLINE Element* Copy_Heart_Byte(Element* out, const Element* in) {
    HEART_BYTE(out) = HEART_BYTE(in);
    return out;
}
