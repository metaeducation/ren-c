//
//  File: %cell-quoted.h
//  Summary: "Definitions for QUOTED! Cells"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// Cells reserve a byte in their header called the QUOTE_BYTE().  The most
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
// But the QUOTE_BYTE() is used to encode other states as well: all datatypes
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
    assert(QUOTE_BYTE(v) != ANTIFORM_0);
    return (QUOTE_BYTE(v) - NOQUOTE_1) >> 1;
}

#define Is_Unquoted(cell) \
    (QUOTE_BYTE(Ensure_Readable(cell)) == NOQUOTE_1)

#define Is_Quoted(cell) \
    (QUOTE_BYTE(Ensure_Readable(cell)) >= ONEQUOTE_NONQUASI_3)

INLINE bool Any_Metaform(const Cell* cell) {  // quasiform or quoted
    QuoteByte quote_byte = QUOTE_BYTE(Ensure_Readable(cell));
    return (
        quote_byte == QUASIFORM_2
        or quote_byte == ONEQUOTE_NONQUASI_3
        or quote_byte == ONEQUOTE_QUASI_4
    );
}


// Turns X into 'X, or '''[1 + 2] into '''''(1 + 2), etc.
//
INLINE Element* Quotify_Depth(Element* elem, Count depth) {
    assert(QUOTE_BYTE(elem) != ANTIFORM_0);

    if (depth == 0)
        return elem;

    if (Quotes_Of(elem) + depth >  MAX_QUOTE_DEPTH)
        fail ("Quoting Depth of 126 Exceeded");

    QUOTE_BYTE(elem) += Quote_Shift(depth);
    return elem;
}


// Turns 'X into X, or '''''[1 + 2] into '''(1 + 2), etc.
//
INLINE Element* Unquotify_Depth(Element* elem, Count depth) {
    assert(QUOTE_BYTE(elem) != ANTIFORM_0);

    if (depth == 0)
        return elem;

    if (depth > Quotes_Of(elem))
        fail ("Attempt to set quoting level of value to less than 0");

    QUOTE_BYTE(elem) -= Quote_Shift(depth);
    return elem;
}

#define Quotify(elem)      Quotify_Depth((elem), 1)
#define Unquotify(elem)    Unquotify_Depth((elem), 1)

INLINE Count Dequotify(Element* elem) {
    Count depth = Quotes_Of(elem);
    if (QUOTE_BYTE(elem) & NONQUASI_BIT)
        QUOTE_BYTE(elem) = NOQUOTE_1;
    else
        QUOTE_BYTE(elem) = QUASIFORM_2_COERCE_ONLY;  // already quasi
    return depth;
}


//=//// ANTIFORMS /////////////////////////////////////////////////////////=//

// Antiforms are foundational in covering edge cases in representation which
// plague Rebol2 and Red.  They enable shifting into a "non-literal" domain,
// where whatever "weird" condition the antiform was attempting to capture can
// be handled without worrying about conflating with more literal usages.
// A good example is addressing the splicing intent for blocks:
//
//     >> append [a b c] [d e]
//     == [a b c [d e]]
//
//     >> ~(d e)~
//     == ~(d e)~  ; anti (this connotes a "splice")
//
//     >> append [a b c] ~(d e)~
//     == [a b c d e]
//
//     >> append [a b c] '~(d e)~
//     == [a b c ~(d e)~]
//
// As demonstrated, the reified QUASIFORM! and the "ghostly" ANTIFORM! work
// in concert to solve the problem.
//
// * A special parameter convention must be used to receive unstable antiforms.
//   Code that isn't expecting such strange circumstances can error if they
//   happen, while more sensitive code can be adapted to cleanly handle the
//   intents that they care about.
//
// Unstable antiforms like packs (block antiforms), error antiforms, and object
// antiforms aren't just not allowed in blocks, they can't be in variables.

INLINE bool Is_Antiform(const Atom* a)
  { return QUOTE_BYTE(Ensure_Readable(a)) == ANTIFORM_0; }

#if CPLUSPLUS_11
    INLINE bool Is_Antiform(const Element* elem) = delete;
#endif

#define Not_Antiform(a) (not Is_Antiform(a))

#undef Any_Antiform  // range-based check useful for typesets, but slower

INLINE bool Is_Antiform_Unstable(const Atom* a) {
    // Assume Is_Antiform() checked Ensure_Readable()
    assert(QUOTE_BYTE(a) == ANTIFORM_0);
    return (
        HEART_BYTE(a) == TYPE_BLOCK  // Is_Pack()
        or HEART_BYTE(a) == TYPE_ERROR  // Is_Raised()
        or HEART_BYTE(a) == TYPE_COMMA  // Is_Barrier()
        or HEART_BYTE(a) == TYPE_OBJECT  // Is_Lazy()
    );
}

#define Is_Antiform_Stable(a) \
    (not Is_Antiform_Unstable(a))

INLINE bool Is_Stable_Antiform_Heart(Heart heart) {
    return (
        heart != TYPE_BLOCK  // Is_Pack()
        and heart != TYPE_ERROR  // Is_Raised()
        and heart != TYPE_COMMA  // Is_Barrier()
        and heart != TYPE_OBJECT  // Is_Lazy()
    );
}

INLINE bool Is_Stable(Need(const Atom*) a) {  // repeat for non-inlined speed
    Assert_Cell_Readable(a);
    if (QUOTE_BYTE(a) != ANTIFORM_0)
        return true;
    return (
        HEART_BYTE(a) != TYPE_BLOCK  // Is_Pack()
        and HEART_BYTE(a) != TYPE_ERROR  // Is_Raised()
        and HEART_BYTE(a) != TYPE_COMMA  // Is_Barrier()
        and HEART_BYTE(a) != TYPE_OBJECT  // Is_Lazy()
    );
}

#define Not_Stable(atom) (not Is_Stable(atom))

#if NO_RUNTIME_CHECKS
    #define Assert_Cell_Stable(c)  NOOP
#else
    #define Assert_Cell_Stable(c) \
        assert(Is_Stable(cast(const Atom*, (c))));
#endif


//=//// ENSURE THINGS ARE ELEMENTS ////////////////////////////////////////=//
//
// An array element can't be an antiform.  Use Known_Element() when you are
// sure you have an element and only want it checked in the debug build, and
// Ensure_Element() when you are not sure and want to raise an error.
//

#if NO_RUNTIME_CHECKS
    #define Known_Element(cell) \
        cast(Element*, (cell))
#else
    INLINE Element* Known_Element(const_if_c Atom* cell) {
        assert(QUOTE_BYTE(cell) != ANTIFORM_0);
        return u_cast(Element*, cell);
    }
#endif

INLINE Element* Ensure_Element(const_if_c Atom* cell) {
    if (QUOTE_BYTE(cell) == ANTIFORM_0)
        fail (Error_Bad_Antiform(cell));
    return u_cast(Element*, cell);
}

#if CPLUSPLUS_11
    INLINE const Element* Ensure_Element(const Atom* cell)
      { return Ensure_Element(m_cast(Atom*, cell)); }

  #if RUNTIME_CHECKS
    INLINE const Element* Known_Element(const Atom* cell) {
        assert(QUOTE_BYTE(cell) != ANTIFORM_0);
        return c_cast(Element*, cell);
    }
  #endif

  #if CHECK_CELL_SUBCLASSES
    void Ensure_Element(const Element*) = delete;

    #if RUNTIME_CHECKS
        void Known_Element(const Element*) = delete;
    #endif
  #endif
#endif


//=//// QUASIFORM! ////////////////////////////////////////////////////////=//

// * Quasiforms are truthy.  There's a reason for this, because it allows
//   operations in the ^META domain to easily use functions like ALL and ANY
//   on the meta values.  (See the FOR-BOTH example.)

INLINE Option(Error*) Trap_Coerce_To_Antiform(Need(Atom*) atom);
INLINE Option(Error*) Trap_Coerce_To_Quasiform(Need(Element*) v);

#define Is_Quasiform(v) \
    (QUOTE_BYTE(Ensure_Readable(v)) == QUASIFORM_2)

INLINE Element* Unquasify(Element* elem) {
    assert(QUOTE_BYTE(elem) == QUASIFORM_2);
    QUOTE_BYTE(elem) = NOQUOTE_1;
    return elem;
}

INLINE Element* Quasify_Isotopic_Fundamental(Element* elem) {
    assert(Any_Isotopic(elem));
    if (Is_Bindable(elem))
        assert(not Cell_Binding(elem));
    assert(QUOTE_BYTE(elem) == NOQUOTE_1);
    QUOTE_BYTE(elem) = QUASIFORM_2_COERCE_ONLY;
    return elem;
}

INLINE Atom* Destabilize_Unbound_Fundamental(Need(Atom*) atom) {
    assert(Any_Isotopic(atom));
    assert(QUOTE_BYTE(atom) == NOQUOTE_1);
    QUOTE_BYTE(atom) = ANTIFORM_0_COERCE_ONLY;
    assert(Is_Antiform_Unstable(atom));
    return atom;
}

INLINE Element* Quasify_Antiform(Atom* v) {
    assert(Is_Antiform(v));
    QUOTE_BYTE(v) = QUASIFORM_2_COERCE_ONLY;  // all antiforms can be quasi
    return u_cast(Element*, v);
}

INLINE Element* Reify(Atom* v) {
    if (QUOTE_BYTE(v) == ANTIFORM_0)
        QUOTE_BYTE(v) = QUASIFORM_2_COERCE_ONLY;  // all antiforms can be quasi
    return cast(Element*, v);
}


//=//// META QUOTING ///////////////////////////////////////////////////////=//

// Meta quoting is a superset of plain quoting.  It has the twist that it can
// quote antiforms to produce quasiforms.  This is done by META (alias ^)
// and the TYPE_META_XXX family of values (like ^WORD, ^TU.P.LE...)
//
// It's hard to summarize in one place all the various applications of this
// feature!  But it's critical to accomplishing composability by which a
// usermode function can accomplish what the system is able to do internally
// with C.  See FOR-BOTH for at least one good example.
//
//  https://forum.rebol.info/t/1833
//

#define Is_Metaform(v) \
    (QUOTE_BYTE(Ensure_Readable(v)) >= QUASIFORM_2)  // quasi or quoted

INLINE Element* Meta_Quotify(Atom* atom) {
    if (QUOTE_BYTE(atom) == ANTIFORM_0) {
        QUOTE_BYTE(atom) = QUASIFORM_2_COERCE_ONLY;  // anti means quasi valid
        return cast(Element*, atom);
    }
    return Quotify(cast(Element*, atom));  // a non-antiform winds up quoted
}

INLINE Atom* Meta_Unquotify_Undecayed(Need(Atom*) atom) {
    if (QUOTE_BYTE(atom) == QUASIFORM_2) {
        Option(Error*) e = Trap_Coerce_To_Antiform(atom);
        if (e)
            fail (unwrap e);  // !!! shouldn't abruptly fail :-(

        return atom;
    }
    return Unquotify(cast(Element*, atom));  // asserts that it's quoted
}

INLINE Value* Meta_Unquotify_Known_Stable(Need(Value*) val) {
    Meta_Unquotify_Undecayed(cast(Atom*, val));
    Assert_Cell_Stable(val);
    return val;
}

INLINE Value* Decay_If_Unstable(Need(Atom*) v);

INLINE Value* Meta_Unquotify_Decayed(Value* v) {
    return Decay_If_Unstable(Meta_Unquotify_Undecayed(cast(Atom*, v)));
}
