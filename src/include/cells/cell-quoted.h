//
//  File: %cell-quoted.h
//  Summary: "Definitions for QUOTED! Cells"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2022 Ren-C Open Source Contributors
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

INLINE Count Cell_Num_Quotes(const Cell* v) {
    assert(QUOTE_BYTE(v) != ANTIFORM_0);
    return (QUOTE_BYTE(v) - NOQUOTE_1) >> 1;
}

#define Is_Unquoted(v) \
    (QUOTE_BYTE(Ensure_Readable(v)) == NOQUOTE_1)

#define Is_Quoted(v) \
    (QUOTE_BYTE(Ensure_Readable(v)) >= ONEQUOTE_NONQUASI_3)

// Turns X into 'X, or '''[1 + 2] into '''''(1 + 2), etc.
//
INLINE Cell* Quotify_Depth_Core(Cell* v, Count depth) {
    if (depth == 0)
        return v;

    if (Cell_Num_Quotes(v) + depth >  MAX_QUOTE_DEPTH)
        fail ("Quoting Depth of 126 Exceeded");

    QUOTE_BYTE(v) += Quote_Shift(depth);
    return v;
}

#if DONT_CHECK_CELL_SUBCLASSES
    #define Quotify_Depth Quotify_Depth_Core
#else
    INLINE Value* Quotify_Depth(Value* v, Count depth)
        { return c_cast(Value*, Quotify_Depth_Core(v, depth)); }

    INLINE Cell* Quotify_Depth(Cell* v, Count depth)
        { return Quotify_Depth_Core(v, depth); }
#endif


// Turns 'X into X, or '''''[1 + 2] into '''(1 + 2), etc.
//
INLINE Cell* Unquotify_Depth_Core(Cell* v, Count depth) {
    if (depth == 0) {
        assert(QUOTE_BYTE(v) != ANTIFORM_0);
        return v;
    }

    if (depth > Cell_Num_Quotes(v))
        fail ("Attempt to set quoting level of value to less than 0");

    QUOTE_BYTE(v) -= Quote_Shift(depth);
    return v;
}

#if DONT_CHECK_CELL_SUBCLASSES
    #define Unquotify_Depth Unquotify_Depth_Core
#else
    INLINE Element* Unquotify_Depth(Value* v, Count depth)
        { return cast(Element*, Unquotify_Depth_Core(v, depth)); }

    INLINE Cell* Unquotify_Depth(Cell* v, Count depth)
        { return Unquotify_Depth_Core(v, depth); }
#endif

#define Quotify(v)      Quotify_Depth((v), 1)
#define Unquotify(v)    Unquotify_Depth((v), 1)

INLINE Count Dequotify(Cell* v) {
    Count depth = Cell_Num_Quotes(v);
    if (QUOTE_BYTE(v) & NONQUASI_BIT)
        QUOTE_BYTE(v) = NOQUOTE_1;
    else
        QUOTE_BYTE(v) = QUASIFORM_2_COERCE_ONLY;  // exception--already quasi!
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
        HEART_BYTE(a) == REB_BLOCK  // Is_Pack()
        or HEART_BYTE(a) == REB_ERROR  // Is_Raised()
        or HEART_BYTE(a) == REB_COMMA  // Is_Barrier()
        or HEART_BYTE(a) == REB_OBJECT  // Is_Lazy()
    );
}

#define Is_Antiform_Stable(a) \
    (not Is_Antiform_Unstable(a))

INLINE bool Is_Stable_Antiform_Heart(Heart heart) {
    return (
        heart != REB_BLOCK  // Is_Pack()
        and heart != REB_ERROR  // Is_Raised()
        and heart != REB_COMMA  // Is_Barrier()
        and heart != REB_OBJECT  // Is_Lazy()
    );
}

INLINE bool Is_Stable(Need(const Atom*) a) {  // repeat for non-inlined speed
    Assert_Cell_Readable(a);
    if (QUOTE_BYTE(a) != ANTIFORM_0)
        return true;
    return (
        HEART_BYTE(a) != REB_BLOCK  // Is_Pack()
        and HEART_BYTE(a) != REB_ERROR  // Is_Raised()
        and HEART_BYTE(a) != REB_COMMA  // Is_Barrier()
        and HEART_BYTE(a) != REB_OBJECT  // Is_Lazy()
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

INLINE Cell* Coerce_To_Antiform(Cell* c);
INLINE Value* Coerce_To_Stable_Antiform(Value* v);
INLINE Atom* Coerce_To_Unstable_Antiform(Atom* a);
INLINE Element* Coerce_To_Quasiform(Value* v);

#define Is_Quasiform(v) \
    (QUOTE_BYTE(Ensure_Readable(v)) == QUASIFORM_2)

INLINE Element* Unquasify(Value* v) {
    assert(QUOTE_BYTE(v) == QUASIFORM_2);
    QUOTE_BYTE(v) = NOQUOTE_1;
    return u_cast(Element*, v);
}

INLINE Element* Quasify(Value* v) {
    assert(QUOTE_BYTE(v) == NOQUOTE_1);  // e.g. can't quote void
    Coerce_To_Quasiform(v);
    return u_cast(Element*, v);
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

INLINE Atom* Degrade(Atom* a) {
    assert(not Is_Antiform(a));
    if (QUOTE_BYTE(a) == QUASIFORM_2)
        Coerce_To_Antiform(a);
    return a;
}


//=//// META QUOTING ///////////////////////////////////////////////////////=//

// Meta quoting is a superset of plain quoting.  It has the twist that it can
// quote antiforms to produce quasiforms.  This is done by META (alias ^)
// and the REB_META_XXX family of values (like ^WORD, ^TU.P.LE...)
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

INLINE Element* Meta_Quotify(Cell* v) {
    if (QUOTE_BYTE(v) == ANTIFORM_0) {
        QUOTE_BYTE(v) = QUASIFORM_2_COERCE_ONLY;  // anti must mean valid quasi
        return cast(Element*, v);
    }
    return cast(Element*, Quotify(v));  // a non-antiform winds up quoted
}

INLINE Atom* Meta_Unquotify_Undecayed(Atom* a) {
    if (QUOTE_BYTE(a) == QUASIFORM_2)
        Coerce_To_Antiform(a);  // Note: not all quasiforms are valid antiforms
    else
        Unquotify(a);  // will assert the input is quoted
    return a;
}

INLINE Value* Meta_Unquotify_Known_Stable(Value* v) {
    Meta_Unquotify_Undecayed(v);
    Assert_Cell_Stable(v);
    return v;
}

INLINE Value* Decay_If_Unstable(Need(Atom*) v);

INLINE Value* Meta_Unquotify_Decayed(Value* v) {
    return Decay_If_Unstable(Meta_Unquotify_Undecayed(cast(Atom*, v)));
}
