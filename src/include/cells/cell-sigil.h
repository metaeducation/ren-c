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
// Sigils (or their absence) are represented via 2 bits in the KIND_BYTE().
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
//   as did being derived from the LIFT_BYTE() and not the KIND_BYTE().
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


#define Unchecked_Unlifted_Cell_Has_Sigil(sigil,cell) \
    (((cell)->header.bits & (CELL_MASK_LIFT | CELL_MASK_SIGIL)) == \
        (FLAG_LIFT_BYTE(NOQUOTE_2) | FLAG_SIGIL(sigil)))

#define Unlifted_Cell_Has_Sigil(sigil,cell) \
    Unchecked_Unlifted_Cell_Has_Sigil((sigil), Ensure_Readable(cell))

#define Any_Plain(v) \
    Unlifted_Cell_Has_Sigil(SIGIL_0, known(Stable*, (v)))

#define Is_Metaform(v) \
    Unlifted_Cell_Has_Sigil(SIGIL_META, known(Stable*, (v)))

#define Is_Pinned(v) \
    Unlifted_Cell_Has_Sigil(SIGIL_PIN, known(Stable*, (v)))

#define Is_Tied(v) \
    Unlifted_Cell_Has_Sigil(SIGIL_TIE, known(Stable*, (v)))


#define Is_Pinned_Form_Of(heartname, v) \
    Cell_Has_Lift_Sigil_Heart(NOQUOTE_2, SIGIL_PIN, TYPE_##heartname, (v))

#define Is_Meta_Form_Of(heartname, v) \
    Cell_Has_Lift_Sigil_Heart(NOQUOTE_2, SIGIL_META, TYPE_##heartname, (v))

#define Is_Tied_Form_Of(heartname, v) \
    Cell_Has_Lift_Sigil_Heart(NOQUOTE_2, SIGIL_TIE, TYPE_##heartname, (v))


INLINE Option(Sigil) Sigil_Of(const Element* e) {
    assert(LIFT_BYTE(e) == NOQUOTE_2);
    return u_cast(Sigil, KIND_BYTE_RAW(e) >> KIND_SIGIL_SHIFT);
}

INLINE Option(Sigil) Underlying_Sigil_Of(const Element* e) {
    possibly(LIFT_BYTE(e) != NOQUOTE_2);
    return u_cast(Sigil, KIND_BYTE_RAW(e) >> KIND_SIGIL_SHIFT);
}



//=//// SIGIL MODIFICATION ////////////////////////////////////////////////=//
//
// 1. Not all values can be sigilized.  Consider something like:
//
//        (dollar: '$, at: '@, caret: '^)
//
//    When you think about what's intended there, you realize `$,` shouldn't
//    be a sigilized COMMA!, because then `'$,` would be a quoted sigilized
//    COMMA!.  The user's intent was clear.  This is a disproof of the idea
//    that all types should allow Sigils.  Rather than create a separate
//    typeset for "Sigilable" values, we piggy-back on "Sequencable", which
//    seems to cover the use cases (and formally makes RUNE! a sequencable
//    type, since it needs to carry sigils, meaning #/# is a PATH! vs. a
//    RUNE! with a slash and pound sign in it).  The cases must be expanded
//    to account for sequences themselves, which aren't in sequencable ATM.
//
// 2. Sigilizing is assumed to only work on cells that do not already have a
//    Sigil.  This is because you might otherwise expect e.g. LIFT of @foo
//    to give you ^@foo.  Also, the Sigilize() function would be paying to
//    mask out bits a lot of time when it's not needed.  So if you really
//    intend to sigilize a plain form, make that clear at the callsite by
//    writing e.e. `Metafy(Plainify(elem))`.
//

INLINE bool Any_Sigilable_Type(Option(Type) t)  // build on sequencable [1]
  { return Any_Sequence_Type(t) or Any_Sequencable_Type(t); }

INLINE bool Any_Sigiled_Type(Option(Type) t)
  { return t == TYPE_METAFORM or t == TYPE_PINNED or t == TYPE_TIED; }

#define Any_Sigilable(cell) \
    Any_Sigilable_Type(Type_Of(cell))

INLINE Element* Sigilize(Element* elem, Sigil sigil) {
    assert(Unlifted_Cell_Has_Sigil(SIGIL_0, elem));  // no quotes/quasi [2]
    assert(Any_Sigilable(elem));
    elem->header.bits |= FLAG_SIGIL(sigil);
    return elem;
}

INLINE Element* Plainify(Element* elem) {
    assert(LIFT_BYTE(elem) == NOQUOTE_2);  // no quotes/quasiforms

  #if RUNTIME_CHECKS
    bool had_sigil = did (elem->header.bits & CELL_MASK_SIGIL);
  #endif

    elem->header.bits &= (~ CELL_MASK_SIGIL);

  #if RUNTIME_CHECKS
    assert(not had_sigil or Any_Sigilable(elem));
  #endif

    return elem;
}

#define Metafy(elem)  Sigilize((elem), SIGIL_META)
#define Pinify(elem)  Sigilize((elem), SIGIL_PIN)
#define Tieify(elem)  Sigilize((elem), SIGIL_TIE)

INLINE Element* Copy_Kind_Byte(Element* out, const Element* in) {
    KIND_BYTE(out) = KIND_BYTE(in);
    return out;
}


//=//// SIGIL-TO-CHARACTER CONVERSION /////////////////////////////////////=//

INLINE Option(char) Char_For_Sigil(Option(Sigil) sigil) {
    switch (opt sigil) {
      case SIGIL_META:  return '^';
      case SIGIL_PIN:   return '@';
      case SIGIL_TIE:   return '$';
      default:          return '\0';
    }
}

INLINE Sigil Sigil_For_Type(Type type) {
    switch (type) {
      case TYPE_METAFORM:  return SIGIL_META;
      case TYPE_PINNED:    return SIGIL_PIN;
      case TYPE_TIED:      return SIGIL_TIE;
      default:
        crash (nullptr);
    }
}
