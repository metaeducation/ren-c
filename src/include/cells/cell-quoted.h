//
//  file: %cell-quoted.h
//  summary: "Definitions for QUOTED! Cells"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2025 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Cells reserve a byte in their header called the LIFT_BYTE().  The most
// basic usage is that any value can be "quote" escaped.  The depth is the
// number of apostrophes, e.g. ''''X is a depth of 4.  The operator QUOTE can
// be used to add a quoting level to a value, UNQUOTE to remove one, and
// NOQUOTE to remove all quotes.
//
//     >> quote [a]
//     == '[a]
//
//     >> noquote first ['''''a]
//     == a
//
// But the LIFT_BYTE() is used to encode other states as well: all datatypes
// (besides QUOTED! itself) have an "antiform" form as well as a "quasi" form.
// The quasi form will evaluate to the antiform form, and the antiform form is
// expressly prohibited from being put in arrays:
//
//     >> nice: first [~foo~]
//     == ~foo~
//
//     >> nice
//     == ~foo~
//
//     >> mean: ~foo~
//     == ~foo~  ; anti
//
// With the use of the `^xxx` family of types and the `^` operator, it is
// possible to leverage a form of quoting to transition antiform to quasiform,
// and quasiforms to quoted:
//
//     >> ^nice
//     == '~foo~
//
//     >> ^mean
//     == ~foo~
//
// Antiforms are new in Ren-C and central to how the design solves historical
// problems in Rebol languages.
//

INLINE Count Quotes_Of(const Element* v) {
    assert(LIFT_BYTE_RAW(v) > ANTIFORM_1);
    return (LIFT_BYTE(v) - NOQUOTE_2) >> 1;
}

INLINE Count Quotes_From_Lift_Byte(LiftByte lift_byte) {
    assert(lift_byte > ANTIFORM_1);
    return (lift_byte - NOQUOTE_2) >> 1;
}

#define Is_Unquoted(cell) \
    (LIFT_BYTE(Ensure_Readable(cell)) == NOQUOTE_2)

#define Is_Quoted(cell) \
    (LIFT_BYTE(Ensure_Readable(cell)) >= ONEQUOTE_NONQUASI_4)

#define Any_Fundamental(v) \
    (LIFT_BYTE(Ensure_Readable(known(Stable*, (v)))) == NOQUOTE_2)


// Turns X into 'X, or '''[1 + 2] into '''''(1 + 2), etc.
//
INLINE Element* Quotify_Depth(Element* elem, Count depth) {
    assert(LIFT_BYTE_RAW(elem) != ANTIFORM_1);

    if (depth == 0)
        return elem;

    if (Quotes_Of(elem) + depth >  MAX_QUOTE_DEPTH)
        panic ("Quoting Depth of 126 Exceeded");

    LIFT_BYTE_RAW(elem) += Quote_Shift(depth);
    return elem;
}


// Turns 'X into X, or '''''[1 + 2] into '''(1 + 2), etc.
//
INLINE Element* Unquotify_Depth(Element* elem, Count depth) {
    assert(LIFT_BYTE_RAW(elem) != ANTIFORM_1);

    if (depth == 0)
        return elem;

    if (depth > Quotes_Of(elem))
        panic ("Attempt to set quoting level of value to less than 0");

    LIFT_BYTE_RAW(elem) -= Quote_Shift(depth);
    return elem;
}

#define Quotify(elem)      Quotify_Depth((elem), 1)
#define Unquotify(elem)    Unquotify_Depth((elem), 1)

INLINE Count Noquotify(Element* elem) {
    Count depth = Quotes_Of(elem);
    if (LIFT_BYTE_RAW(elem) & QUASI_BIT)
        LIFT_BYTE_RAW(elem) = QUASIFORM_3;  // already quasi
    else
        LIFT_BYTE_RAW(elem) = NOQUOTE_2;
    return depth;
}


//=//// ANTIFORMS /////////////////////////////////////////////////////////=//
//
// Antiforms are foundational in covering edge cases in representation which
// plague Rebol2 and Red.  They enable shifting into a "non-literal" domain,
// where whatever "weird" condition the antiform was attempting to capture can
// be handled without worrying about conflating with more literal usages.
//
// A good example is addressing the splicing intent for blocks:
//
//     >> append [a b c] [d e]
//     == [a b c [d e]]
//
//     >> ~(d e)~
//     == \~(d e)~\  ; antiform (splice!)
//
//     >> append [a b c] ~(d e)~
//     == [a b c d e]
//
//     >> append [a b c] '~(d e)~
//     == [a b c ~(d e)~]
//
// As demonstrated, the reified QUASIFORM! and the non-reified ANTIFORM! work
// in concert to solve the problem.
//
// 1. Each antiform gets a synthetic TYPE_XXX enum value, and these states are
//    all numerically greater than the TYPE_XXX for non-antiforms.  However,
//    calculating the synthetic type and then seeing if it's a large value
//    is slower than just checking the LIFT_BYTE() for ANTIFORM_1.  So even
//    though the range-based Any_Antiform() macro was auto-generated with
//    other enum checks from %types.r, C code should prefer Is_Antiform().
//

INLINE bool Is_Antiform(const Value* a)
  { return LIFT_BYTE(Ensure_Readable(a)) == ANTIFORM_1; }

#if CHECK_CELL_SUBCLASSES
    INLINE bool Is_Antiform(const Element* elem) = delete;
#endif

#define Not_Antiform(a) (not Is_Antiform(a))

#undef Any_Antiform  // Is_Antiform() faster than auto-generated macro [1]

INLINE bool Is_Lifted_Antiform(const Value* a)
  { return LIFT_BYTE(Ensure_Readable(a)) == QUASIFORM_3; }


//=//// UNSTABLE ANTIFORMS ////////////////////////////////////////////////=//
//
// Unstable antiforms like PACK!, ERROR!, and GHOST! antiforms aren't just
// not allowed in blocks, they can't be in stored in "normal" variables
// (only ^META variables can hold them).  They will either decay to stable
// forms or cause errors in decay.
//
// The ^META parameter convention must be used to get unstable antiforms.
// Code that isn't expecting such strange circumstances can error if they
// happen, while more sensitive code can be adapted to cleanly handle the
// intents that they care about.
//
// 1. There's enough checking in the system that ANTIFORM_1 cells do not have
//    sigils that double-checking it here would just waste CPU cycles.  We
//    can assume the KIND_BYTE() gives the same answer as Heart_Of() and
//    dodge the modulus to drop the sigil.
//
// 2. It's possible to look for one of 3 patterns in masked header bits to
//    identify unstable antiforms.  But you don't have to do the mask or
//    compare if the LIFT_BYTE() isn't ANTIFORM_1.  This should be tested for
//    performance to see whether the branch helps or hurts.  I'd assume that
//    it helps, but branch impact can be counterintuitive sometimes.
//

INLINE bool Is_Antiform_Unstable(const Value* a) {
    unnecessary(Ensure_Readable(a));  // assume Is_Antiform() checked readable
    assert(LIFT_BYTE(a) == ANTIFORM_1);
    impossible(0 != (a->header.bits & CELL_MASK_SIGIL));  // kind = heart [1]
    return (
        KIND_BYTE(a) == TYPE_BLOCK  // Is_Pack()
        or KIND_BYTE(a) == TYPE_WARNING  // Is_Error()
        or KIND_BYTE(a) == TYPE_COMMA  // Is_Ghost()
    );
}

#define Is_Antiform_Stable(a) \
    (not Is_Antiform_Unstable(a))

INLINE bool Not_Cell_Stable(Need(const Value*) a) {
    possibly(not Is_Antiform(a));  // this is a general check for any Value

    Assert_Cell_Readable(a);
    assert(LIFT_BYTE_RAW(a) != DUAL_0);

  #if defined(NO_BRANCH_UNSTABLE_ANTIFORM_CHECK)  // !!! TBD: measure speed [2]
    impossible(  // [1]
        LIFT_BYTE_RAW(a) == ANTIFORM_1 and (a->header.bits & CELL_MASK_SIGIL)
    );
  #else
    if (LIFT_BYTE_RAW(a) != ANTIFORM_1)
        return false;
    impossible(0 != (a->header.bits & CELL_MASK_SIGIL));  // [1]
  #endif

    uintptr_t masked = a->header.bits & CELL_MASK_HEART_AND_SIGIL_AND_LIFT;
    return (
        masked == (FLAG_HEART(TYPE_WARNING) | FLAG_LIFT_BYTE(ANTIFORM_1))
        or masked == (FLAG_HEART(TYPE_COMMA) | FLAG_LIFT_BYTE(ANTIFORM_1))
        or masked == (FLAG_HEART(TYPE_BLOCK) | FLAG_LIFT_BYTE(ANTIFORM_1))
    );
}

#define Is_Cell_Stable(atom)  (not Not_Cell_Stable(atom))

#if NO_RUNTIME_CHECKS
    #define Assert_Cell_Stable(c)  NOOP
    #define Known_Stable(a)  u_cast(Stable*, (a))
#else
    #define Assert_Cell_Stable(c) \
        assert(Is_Cell_Stable(cast(const Value*, (c))));

    MUTABLE_IF_C(Stable*, INLINE) Known_Stable(CONST_IF_C(Value*) v_) {
        CONSTABLE(Value*) v = m_cast(Value*, v_);
        assert(Is_Cell_Stable(v));
        return u_cast(Stable*, v);
    }
#endif

MUTABLE_IF_C(Result(Stable*), INLINE) Ensure_Stable(CONST_IF_C(Value*) v_) {
    CONSTABLE(Value*) v = m_cast(Value*, v_);
    if (Not_Cell_Stable(v))
        return fail ("Value is an unstable antiform");
    return u_cast(Stable*, v);
}


//=//// ENSURE THINGS ARE ELEMENTS ////////////////////////////////////////=//
//
// An array element can't be an antiform.  Use Known_Element() when you are
// sure you have an element and only want it checked in the debug build, and
// Ensure_Element() when you are not sure and want to panic if not.
//

MUTABLE_IF_C(Option(Element*), INLINE) As_Element(CONST_IF_C(Stable*) v_) {
    CONSTABLE(Stable*) v = m_cast(Stable*, Ensure_Readable(v_));
    if (Is_Antiform(v))
        return nullptr;
    return u_cast(Element*, v);
}

#if NO_RUNTIME_CHECKS
    #define Known_Element(cell) \
        cast(Element*, (cell))
#else
    MUTABLE_IF_C(Element*, INLINE) Known_Element(CONST_IF_C(Value*) cell) {
        CONSTABLE(Value*) a = m_cast(Value*, cell);
        assert(LIFT_BYTE(a) != ANTIFORM_1);
        return cast(Element*, a);
    }
#endif

MUTABLE_IF_C(Element*, INLINE) Ensure_Element(CONST_IF_C(Value*) cell) {
    CONSTABLE(Value*) a = m_cast(Value*, cell);
    if (LIFT_BYTE(a) == ANTIFORM_1)
        panic (Error_Bad_Antiform(a));
    return cast(Element*, a);
}

#if CHECK_CELL_SUBCLASSES
    void Ensure_Element(const Element*) = delete;

    #if RUNTIME_CHECKS
        void Known_Element(const Element*) = delete;
    #endif
#endif


//=//// QUASIFORM! ////////////////////////////////////////////////////////=//

// * Quasiforms are truthy.  There's a reason for this, because it allows
//   operations in the ^META domain to easily use functions like ALL and ANY
//   on the lifted values.  (See the FOR-BOTH example.)

INLINE Result(Value*) Coerce_To_Antiform(Need(Value*) atom);
INLINE Result(Element*) Coerce_To_Quasiform(Need(Element*) v);

#define Is_Quasiform(v) \
    (LIFT_BYTE(Ensure_Readable(v)) == QUASIFORM_3)

INLINE Element* Unquasify(Element* elem) {
    assert(LIFT_BYTE(elem) == QUASIFORM_3);
    LIFT_BYTE(elem) = NOQUOTE_2;
    return elem;
}

INLINE Element* Quasify_Isotopic_Fundamental(Element* elem) {
    assert(Any_Isotopic(elem));
    assert(LIFT_BYTE(elem) == NOQUOTE_2);
    LIFT_BYTE_RAW(elem) = QUASIFORM_3;
    return elem;
}

INLINE Stable* Stably_Antiformize_Unbound_Fundamental(Need(Stable*) v) {
    assert(Any_Isotopic(v));
    assert(LIFT_BYTE(v) == NOQUOTE_2);
    assert(Is_Stable_Antiform_Kind_Byte(KIND_BYTE(v)));
    if (Is_Bindable_Heart(Unchecked_Heart_Of(v)))
        assert(not Cell_Binding(v));
    LIFT_BYTE_RAW(v) = ANTIFORM_1;
    return v;
}

INLINE Value* Unstably_Antiformize_Unbound_Fundamental(Need(Value*) atom) {
    assert(Any_Isotopic(atom));
    assert(LIFT_BYTE(atom) == NOQUOTE_2);
    assert(not Is_Stable_Antiform_Kind_Byte(KIND_BYTE(atom)));
    if (Is_Bindable_Heart(Unchecked_Heart_Of(atom)))
        assert(not Cell_Binding(atom));
    LIFT_BYTE_RAW(atom) = ANTIFORM_1;
    return atom;
}

INLINE Element* Quasify_Antiform(Need(Stable*) v) {
    assert(Is_Antiform(v));
    LIFT_BYTE_RAW(v) = QUASIFORM_3;  // all antiforms can be quasi
    return u_cast(Element*, v);
}

INLINE Element* Reify(Value* v) {
    if (LIFT_BYTE(v) == ANTIFORM_1)
        LIFT_BYTE_RAW(v) = QUASIFORM_3;  // all antiforms can be quasi
    return cast(Element*, v);
}


//=//// LIFTING ///////////////////////////////////////////////////////////=//

// Lifting is a superset of plain quoting.  It has the twist that it can
// "quote antiforms" to produce quasiforms.  This is done by LIFT, but also on
// assignment by metaforms (^foo: ...) and metaforms UNLIFT when fetching.
//
// It's hard to summarize in one place all the various applications of this
// feature!  But it's critical to accomplishing composability by which a
// usermode function can accomplish what the system is able to do internally
// with C.  See FOR-BOTH for at least one good example.
//
//  https://forum.rebol.info/t/1833
//

#define Any_Lifted(v) \
    (LIFT_BYTE(Ensure_Readable(v)) >= QUASIFORM_3)  // quasi or quoted

#define Not_Lifted(v) \
    (LIFT_BYTE(Ensure_Readable(v)) < QUASIFORM_3)  // anti or fundamental

INLINE Element* Liftify(Value* atom) {
    if (LIFT_BYTE_RAW(atom) == ANTIFORM_1) {
        LIFT_BYTE_RAW(atom) = QUASIFORM_3;  // anti means quasi valid
        return cast(Element*, atom);
    }
    return Quotify(cast(Element*, atom));  // a non-antiform winds up quoted
}

INLINE Result(Value*) Unliftify_Undecayed(Need(Value*) atom) {
    if (LIFT_BYTE_RAW(atom) == QUASIFORM_3) {
        trap (
          Coerce_To_Antiform(atom)
        );
        return atom;
    }
    return Unquotify(cast(Element*, atom));  // asserts that it's quoted
}

INLINE Stable* Unliftify_Known_Stable(Need(Stable*) val) {
    assume (
      Unliftify_Undecayed(cast(Value*, val))
    );
    Assert_Cell_Stable(val);
    return val;
}
