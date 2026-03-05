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
// Sigils (or their absence) are stored via 2 bits in the HEARTSIGIL_BYTE().
// This limits the number of fundamental types to 60 (as TYPE_0 is reserved
// for representing an extension type, and 3 LIFT_BYTE() are reserved for
// the Sigil pseudotypes).  This limitation is not of much concern in the
// modern system, as extension types allow making as many as are required.
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

// Note: aggressive test of ii_cast() inside a static assertion, casting a
// C++ enum class (in some builds) to a C enum.
//

#define Type_From_Sigil(sigil) \
    Type_From_Byte_Or_0(i_cast(Byte, known(Sigil, (sigil))))

#define Sigil_From_Type(type) \
    i_cast(Sigil, Byte_From_Type(type))

STATIC_ASSERT(SIGIL_META == Sigil_From_Type(TYPE_METAFORM));
STATIC_ASSERT(SIGIL_PIN == Sigil_From_Type(TYPE_PINNED));
STATIC_ASSERT(SIGIL_TIE == Sigil_From_Type(TYPE_TIED));

INLINE bool Any_Sigiled_Type(Option(Type) t)
  { return t == TYPE_METAFORM or t == TYPE_PINNED or t == TYPE_TIED; }

#define Unchecked_Unlifted_Cell_Has_Sigil(sigil,cell) \
    (((cell)->header.bits & \
        (CELL_MASK_LIFTED_OR_ANTIFORM_OR_DUAL | CELL_MASK_SIGIL)) == \
        FLAG_SIGIL(sigil))

#define Unlifted_Cell_Has_Sigil(sigil,cell) \
    Unchecked_Unlifted_Cell_Has_Sigil((sigil), Readable_Cell(cell))

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
        TYPE_PINNED, SIGIL_PIN, HEART_##heartname)

#define Is_Meta_Form_Of(heartname, v) \
    Cell_Has_Lift_Sigil_Heart(Known_Stable(v), \
        TYPE_METAFORM, SIGIL_META, HEART_##heartname)

#define Is_Tied_Form_Of(heartname, v) \
    Cell_Has_Lift_Sigil_Heart(Known_Stable(v), \
        TYPE_TIED, SIGIL_TIE, HEART_##heartname)

INLINE Option(Sigil) Sigil_Of(const Element* v) {
    assert(Type_Of_Raw(v) <= MAX_TYPE_NOQUOTE_NOQUASI);
    if (Type_Of_Raw(v) <= MAX_TYPE_SIGIL) {
        assert((HEARTSIGIL_BYTE(v) >> BYTE_SIGIL_SHIFT) == LIFT_BYTE(v));
        return Sigil_From_Type(Type_Of_Raw(v));  // cheap to reuse type :-/
    }
    assert(not (v->header.bits & CELL_MASK_SIGIL));
    return SIGIL_0;
}

#define Sigil_From_Crumb(crumb) \
    i_cast(Sigil, known(Byte, (crumb)))

INLINE Sigil Sigil_For_Sigiled_Type(TypeEnum type) {
    assert(Any_Sigiled_Type(type));
    return Sigil_From_Crumb(Byte_From_Type(type));
}

INLINE Option(Sigil) Cell_Underlying_Sigil(const Cell* cell) {
    possibly(Type_Of_Raw(cell) >= MIN_TYPE_ANTIFORM);  // SIGIL_0 if antiform
    possibly(Type_Of_Raw(cell) >= MIN_TYPE_BEDROCK);  // !!! allow or not?
    Sigil sigil = Sigil_From_Crumb(
        HEARTSIGIL_BYTE(cell) >> BYTE_SIGIL_SHIFT
    );
    if (sigil and Type_Of_Raw(cell) <= MAX_TYPE_NOQUOTE_NOQUASI)  // raw [2]
        assert(sigil == Sigil_From_Type(Type_Of_Raw(cell)));
    return sigil;
}


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
    );
}

#define Any_Sigilable(cell) \
    Any_Sigilable_Type(Type_Of(cell))

#define Any_Sigilable_Heart(heart) \
    Any_Sigilable_Type(Type_From_Heart(heart))

INLINE Element* Clear_Cell_Sigil(Element* v) {
    assert(Type_Of_Raw(v) <= MAX_TYPE_NOQUOTE_NOQUASI);

    if (Type_Of_Raw(v) <= MAX_TYPE_SIGIL) {
        assert(v->header.bits & CELL_MASK_SIGIL or not Type_Of_Raw(v));
        HEARTSIGIL_BYTE(v) &= HEARTSIGIL_BYTEMASK_HEART;
        Tweak_Cell_Lift_Byte(
            v,
            Type_From_Byte(HEARTSIGIL_BYTE(v))  // *after* sigil removed
        );
    }

    return v;
}

INLINE Element* Add_Cell_Sigil(Element* v, Sigil sigil) {
    assert(not (v->header.bits & CELL_MASK_SIGIL));  // no quotes/quasi [2]
    assert(
        Type_Of_Raw(v) == TYPE_0_constexpr
        or (
            Type_Of_Raw(v) >= MIN_TYPE_HEART
            and Type_Of_Raw(v) <= MAX_TYPE_NOQUOTE_NOQUASI
        )
    );
    assert(Any_Sigilable(v));
    v->header.bits |= FLAG_SIGIL(sigil);
    Tweak_Cell_Lift_Byte(v, Type_From_Sigil(sigil));
    return v;
}

#define Force_Cell_Sigil(v,sigil) \
    Add_Cell_Sigil(Clear_Cell_Sigil(v), (sigil))  // [2]


//=//// SIGIL-TO-CHARACTER CONVERSION /////////////////////////////////////=//

INLINE Option(char) Char_For_Sigil(Option(Sigil) sigil) {
    switch (opt sigil) {
      case SIGIL_0_constexpr: return '\0';
      case SIGIL_META:  return '^';
      case SIGIL_PIN:   return '@';
      case SIGIL_TIE:   return '$';
      default:  // compiler warns if there's no `default`
        crash (nullptr);
    }
}


//=//// UPDATING CELL TYPES ///////////////////////////////////////////////=//
//
// The rules are delicate for tweaking the bytes determining a cell's type,
// so it's better to go through helpers than to write this pattern at every
// callsite that does it.
//

INLINE void Tweak_Cell_Type_Matching_Heart(Cell* v, Heart heart) {
    Tweak_Cell_Heart(v, heart);
    Tweak_Cell_Lift_Byte(v, Type_From_Heart(heart));
}

INLINE void Tweak_Cell_Quoted_Type(Cell* v, Heart heart) {
    Tweak_Cell_Heart(v, heart);
    Tweak_Cell_Lift_Byte(v, TYPE_QUOTED_1_TIME_NONQUASI);
}

INLINE void Tweak_Cell_Type_Matching_Heartsigil(
    Cell* cell,
    Heart heart,
    Sigil sigil
){
    assert(sigil);
    Tweak_Cell_Heart_And_Sigil(cell, heart, sigil);
    Tweak_Cell_Lift_Byte(cell, Type_From_Sigil(sigil));
}
