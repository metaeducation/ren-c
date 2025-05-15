//
//  file: %cell-sigil.h
//  summary: "SIGIL! Decorator Type"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2024-2025 Ren-C Open Source Contributors
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
// There are three Sigils: LIFT (^), PIN (@), and TIE ($).  Like quoting,
// they are decorations that can be applied to any plain form.  Unlike
// quoting, they can be applied only once...so there is no $$ or @$
//
// Sigils (or their absence) are represented via 2 bits in the HEART_BYTE().
// This limits the number of fundamental types to 63 (as TYPE_0 is reserved
// for representing an extension type.)  This limitation is not of much
// concern in the modern system, as extension types allow making as many as
// are required.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * The quasiform state ~XXX~ was once thought of as the QUASI (~~) Sigil.
//   This was when it was believed something could not be both quoted and
//   quasi at the same time.  Being a 2-character Sigil broke the rhythm,
//   as did being derived from the QUOTE_BYTE() and not the HEART_BYTE().
//   Today it is believed that quoted and quasi at the same time is something
//   with legitimate use cases, e.g. ~$~ is useful and ~@foo~ may be too.
//   So the value of ~~ as a Sigil is not emergent.
//
// * There used to be a & Sigil, which was for indicating interpretation of
//   things as datatypes.  That was removed in favor of antiform datatypes,
//   which is a more motivated design.  This dropped the number of Sigils
//   to just 3, which could be encoded along with the no-Sigil state in just
//   2 bits.  While it would not be a good iea for the implementation tail to
//   wag the design dog and say this is *why* there are only 3 Sigils, that's
//   not why: the design had already converged on 3.
//

INLINE bool Any_Plain(const Element* e) {
    if (QUOTE_BYTE(e) != NOQUOTE_1)
        return false;
    return not (e->header.bits & CELL_MASK_SIGIL_BITS);
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


INLINE Option(Sigil) Sigil_Of(const Element* e)
  { return u_cast(Sigil, HEART_BYTE_RAW(e) >> HEART_SIGIL_SHIFT); }



//=//// SIGIL MODIFICATION ////////////////////////////////////////////////=//
//
// 1. Sigilizing is assumed to only work on cells that do not already have a
//    Sigil.  This is because you might otherwise expect e.g. LIFT of @foo
//    to give you ^@foo.  Also, the Sigilize() function would be paying to
//    mask out bits a lot of time when it's not needed.  So if you really
//    intend to sigilize a plain form, make that clear at the callsite by
//    writing e.e. `Liftify(Plainify(elem))`.
//

INLINE Element* Sigilize(Element* elem, Sigil sigil) {
    assert(QUOTE_BYTE(elem) == NOQUOTE_1);  // no quotes, no quasiforms
    assert(not (elem->header.bits & CELL_MASK_SIGIL_BITS));  // clearest [1]
    elem->header.bits |= FLAG_SIGIL_ENUM(sigil);
    return elem;
}

INLINE Element* Plainify(Element* elem) {
    assert(QUOTE_BYTE(elem) == NOQUOTE_1);  // no quotes, no quasiforms
    elem->header.bits &= ~(CELL_MASK_SIGIL_BITS);
    return elem;
}

#define Liftify(elem)  Sigilize((elem), SIGIL_LIFT)
#define Pinify(elem)   Sigilize((elem), SIGIL_PIN)
#define Tieify(elem)   Sigilize((elem), SIGIL_TIE)

INLINE Element* Copy_Heart_Byte(Element* out, const Element* in) {
    HEART_BYTE(out) = HEART_BYTE(in);
    return out;
}


//=//// STANDALONE "SIGIL?" ELEMENTS (@ ^ $) //////////////////////////////=//
//
// These are just sigilized versions of (_) which is the literal space char.
// Space itself is not thought of as a "Sigil" because (sigil of [a b]) is
// null, not space.
//

INLINE Element* Init_Sigil(Init(Element) out, Sigil sigil) {
    return Sigilize(Init_Space(out), sigil);
}

INLINE bool Any_Sigil(const Element* e) {
    if (QUOTE_BYTE(e) != NOQUOTE_1 or not Sigil_Of(e))
        return false;
    return IS_CHAR_CELL(e) and Cell_Codepoint(e) == ' ';
}

INLINE bool Is_Sigil(const Cell* c, Sigil sigil) {
    if (QUOTE_BYTE(c) != NOQUOTE_1 or Sigil_Of(c_cast(Element*, c)) != sigil)
        return false;
    return IS_CHAR_CELL(c) and Cell_Codepoint(c) == ' ';
}

#define Is_Pin_Sigil(cell)  Is_Sigil((cell), SIGIL_PIN)
#define Is_Lift_Sigil(cell)  Is_Sigil((cell), SIGIL_LIFT)
#define Is_Tie_Sigil(cell)  Is_Sigil((cell), SIGIL_TIE)


//=//// SIGIL-TO-CHARACTER CONVERSION /////////////////////////////////////=//

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
