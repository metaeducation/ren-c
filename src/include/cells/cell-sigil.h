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
// * Things can be Sigil'd and Quasi'd at the same time, e.g. ~$~ or ~@foo~
//   Current theory is that there are no Sigil'd antiforms, but that may end
//   up being too limiting in the future.
//
// * There used to be a & Sigil, which was for indicating interpretation of
//   things as datatypes.  That was removed in favor of antiform datatypes,
//   which is a more motivated design.  This dropped the number of Sigils
//   to just 3, which could be encoded along with the no-Sigil state in just
//   2 bits.  While it would not be a good idea for the implementation tail to
//   wag the design dog and say this is *why* there are only 3 Sigils, that's
//   not why: the design had already converged on 3.
//


#define Unchecked_Unlifted_Cell_Has_Sigil(sigil,cell) \
    (((cell)->header.bits & (CELL_MASK_LIFT | CELL_MASK_SIGIL)) == \
        (FLAG_LIFT_BYTE(NOQUOTE_3) | FLAG_SIGIL(sigil)))

#define Unlifted_Cell_Has_Sigil(sigil,cell) \
    Unchecked_Unlifted_Cell_Has_Sigil((sigil), Ensure_Readable(cell))

#define Any_Plain(v) \
    Unlifted_Cell_Has_Sigil(SIGIL_0, Known_Stable(v))

#define Is_Metaform(v) \
    Unlifted_Cell_Has_Sigil(SIGIL_META, Known_Stable(v))

#define Is_Pinned(v) \
    Unlifted_Cell_Has_Sigil(SIGIL_PIN, Known_Stable(v))

#define Is_Tied(v) \
    Unlifted_Cell_Has_Sigil(SIGIL_TIE, Known_Stable(v))

#define Is_Pinned_Form_Of(heartname, v) \
    Cell_Has_Lift_Sigil_Heart(Known_Stable(v), \
        NOQUOTE_3, SIGIL_PIN, TYPE_##heartname)

#define Is_Meta_Form_Of(heartname, v) \
    Cell_Has_Lift_Sigil_Heart(Known_Stable(v), \
        NOQUOTE_3, SIGIL_META, TYPE_##heartname)

#define Is_Tied_Form_Of(heartname, v) \
    Cell_Has_Lift_Sigil_Heart(Known_Stable(v), \
        NOQUOTE_3, SIGIL_TIE, TYPE_##heartname)

INLINE Option(Sigil) Sigil_Of(const Element* v) {
    assert(LIFT_BYTE(v) == NOQUOTE_3);
    return i_cast(Sigil, KIND_BYTE_RAW(v) >> KIND_SIGIL_SHIFT);
}

INLINE Option(Sigil) Cell_Underlying_Sigil(const Cell* cell) {
    possibly(LIFT_BYTE(cell) <= STABLE_ANTIFORM_2);  // SIGIL_0 if antiform
    return i_cast(Sigil, KIND_BYTE_RAW(cell) >> KIND_SIGIL_SHIFT);
}

#define Kind_From_Sigil_And_Heart(sigil,heart) \
    (ii_cast(KindByte, known(Option(Sigil), (sigil)) << KIND_SIGIL_SHIFT) \
        | i_cast(KindByte, known(HeartEnum, (heart))))


//=//// SIGIL MODIFICATION ////////////////////////////////////////////////=//
//
// 1. Rather than create a separate typeset for "Sigilable" values, we piggy
//    back on "Sequencable", which seems to cover the use cases.  RUNE! is
//    included as sequencable, so #/# is a PATH! vs. a RUNE! with a slash and
//    pound sign in it.
//
//    The cases must be expanded to account for sequences themselves, which
//    aren't in sequencable ATM.
//
// 2. Sigilizing is assumed to only work on cells that do not already have a
//    Sigil.  This is because you might otherwise expect e.g. META of @foo
//    to give you ^@foo.  Also, the Add_Cell_Sigil() function would be paying to
//    mask out bits a lot of time when it's not needed.  So if you really
//    intend to sigilize a plain form, make that clear at the callsite by
//    writing e.e. `Force_Cell_Sigil(elem)`.
//

INLINE bool Any_Sigilable_Type(Option(Type) t) {  // build on sequencable [1]
    return (
        Any_Sequence_Type(t) or Any_Sequencable_Type(t) or t == TYPE_DECIMAL
        or t == TYPE_BLANK
    );
}

INLINE bool Any_Sigiled_Type(Option(Type) t)
  { return t == TYPE_METAFORM or t == TYPE_PINNED or t == TYPE_TIED; }

#define Any_Sigilable(cell) \
    Any_Sigilable_Type(Type_Of(cell))

INLINE Element* Clear_Cell_Sigil(Element* v) {
    assert(LIFT_BYTE(v) == NOQUOTE_3);  // no quotes/quasiforms

  #if RUNTIME_CHECKS
    bool had_sigil = did (v->header.bits & CELL_MASK_SIGIL);
  #endif

    v->header.bits &= (~ CELL_MASK_SIGIL);

  #if RUNTIME_CHECKS
    assert(not had_sigil or Any_Sigilable(v));
  #endif

    return v;
}

INLINE Element* Add_Cell_Sigil(Element* v, Sigil sigil) {
    assert(Unlifted_Cell_Has_Sigil(SIGIL_0, v));  // no quotes/quasi [2]
    assert(Any_Sigilable(v));
    v->header.bits |= FLAG_SIGIL(sigil);
    return v;
}

#define Force_Cell_Sigil(v,sigil) \
    Add_Cell_Sigil(Clear_Cell_Sigil(v), (sigil))  // [2]

INLINE Element* Copy_Kind_Byte(Element* out, const Element* in) {
    KIND_BYTE(out) = KIND_BYTE(in);
    return out;
}


//=//// SIGIL-TO-CHARACTER CONVERSION /////////////////////////////////////=//

INLINE Option(char) Char_For_Sigil(Option(Sigil) sigil) {
    switch (opt sigil) {
      case SIGIL_0:     return '\0';
      case SIGIL_META:  return '^';
      case SIGIL_PIN:   return '@';
      case SIGIL_TIE:   return '$';
      default:  // compiler warns if there's no `default`
        crash (nullptr);
    }
}

INLINE Sigil Sigil_For_Sigiled_Type(Type type) {
    switch (type) {
      case TYPE_METAFORM:  return SIGIL_META;
      case TYPE_PINNED:    return SIGIL_PIN;
      case TYPE_TIED:      return SIGIL_TIE;
      default:
        crash (nullptr);
    }
}
