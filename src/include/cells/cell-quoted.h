//
//  File: %cell-quoted.h
//  Summary: {Definitions for QUOTED! Datatype}
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
    (QUOTE_BYTE(Ensure_Readable(v)) >= ONEQUOTE_3)  // '~a~ quoted, not quasi

// Turns X into 'X, or '''[1 + 2] into '''''(1 + 2), etc.
//
INLINE Cell* Quotify_Core(Cell* v, Count depth) {
    if (depth == 0)
        return v;

    if (Cell_Num_Quotes(v) + depth >  MAX_QUOTE_DEPTH)
        fail ("Quoting Depth of 126 Exceeded");

    QUOTE_BYTE(v) += (depth << 1);
    return v;
}

#if (! DEBUG_USE_CELL_SUBCLASSES)
    #define Quotify Quotify_Core
#else
    INLINE Value* Quotify(Value* v, Count depth)
        { return c_cast(Value*, Quotify_Core(v, depth)); }

    INLINE Cell* Quotify(Cell* v, Count depth)
        { return Quotify_Core(v, depth); }
#endif


// Turns 'X into X, or '''''[1 + 2] into '''(1 + 2), etc.
//
INLINE Cell* Unquotify_Core(Cell* v, Count unquotes) {
    if (unquotes == 0) {
        assert(QUOTE_BYTE(v) != ANTIFORM_0);
        return v;
    }

    if (unquotes > Cell_Num_Quotes(v))
        fail ("Attempt to set quoting level of value to less than 0");

    QUOTE_BYTE(v) -= (unquotes << 1);
    return v;
}

#if (! DEBUG_USE_CELL_SUBCLASSES)
    #define Unquotify Unquotify_Core
#else
    INLINE Element* Unquotify(Value* v, Count depth)
        { return cast(Element*, Unquotify_Core(v, depth)); }

    INLINE Cell* Unquotify(Cell* v, Count depth)
        { return Unquotify_Core(v, depth); }
#endif

INLINE Count Dequotify(Cell* v) {
    Count depth = Cell_Num_Quotes(v);
    if (QUOTE_BYTE(v) & NONQUASI_BIT)
        QUOTE_BYTE(v) = NOQUOTE_1;
    else
        QUOTE_BYTE(v) = QUASIFORM_2;
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
//

INLINE bool Is_Antiform(Need(const Value*) v)
  { return QUOTE_BYTE(Ensure_Readable(v)) == ANTIFORM_0; }

INLINE bool Is_Antiform_Unstable(Need(const Atom*) v) {
    // Assume Is_Antiform() checked Ensure_Readable()
    assert(QUOTE_BYTE(v) == ANTIFORM_0);
    return (
        HEART_BYTE(v) == REB_BLOCK  // Is_Pack()
        or HEART_BYTE(v) == REB_ERROR  // Is_Raised()
        or HEART_BYTE(v) == REB_COMMA  // Is_Barrier()
        or HEART_BYTE(v) == REB_OBJECT  // Is_Lazy()
    );
}

#define Is_Antiform_Stable(v) \
    (not Is_Antiform_Unstable(v))

INLINE bool Is_Stable_Antiform_Heart(Heart heart) {
    return (
        heart != REB_BLOCK  // Is_Pack()
        and heart != REB_ERROR  // Is_Raised()
        and heart != REB_COMMA  // Is_Barrier()
        and heart != REB_OBJECT  // Is_Lazy()
    );
}

INLINE bool Is_Stable(Need(const Atom*) v) {  // repeat for non-inlined speed
    Assert_Cell_Readable(v);
    if (QUOTE_BYTE(v) != ANTIFORM_0)
        return true;
    return (
        HEART_BYTE(v) != REB_BLOCK  // Is_Pack()
        and HEART_BYTE(v) != REB_ERROR  // Is_Raised()
        and HEART_BYTE(v) != REB_COMMA  // Is_Barrier()
        and HEART_BYTE(v) != REB_OBJECT  // Is_Lazy()
    );
}

#define Not_Stable(atom) (not Is_Stable(atom))

#if !defined(NDEBUG)
    #define Assert_Cell_Stable(v) \
        assert(Is_Stable(cast(const Atom*, (v))));
#else
    #define Assert_Cell_Stable(v)
#endif


//=//// ENSURE THINGS ARE ELEMENTS ////////////////////////////////////////=//
//
// An array element can't be an antiform.

INLINE Element* Ensure_Element(const_if_c Atom* cell) {
    if (QUOTE_BYTE(cell) == ANTIFORM_0)
        fail (Error_Bad_Antiform(cell));
    return u_cast(Element*, cell);
}

#if CPLUSPLUS_11
    INLINE const Element* Ensure_Element(const Atom* cell)
      { return Ensure_Element(m_cast(Atom*, cell)); }

  #if DEBUG_USE_CELL_SUBCLASSES
    void Ensure_Element(const Element*) = delete;
  #endif
#endif


//=//// QUASIFORM! ////////////////////////////////////////////////////////=//

// * Quasiforms are truthy.  There's a reason for this, because it allows
//   operations in the ^META domain to easily use functions like ALL and ANY
//   on the meta values.  (See the FOR-BOTH example.)

#define Is_Quasiform(v) \
    (QUOTE_BYTE(Ensure_Readable(v)) == QUASIFORM_2)

INLINE Element* Unquasify(Value* v) {
    assert(QUOTE_BYTE(v) == QUASIFORM_2);
    QUOTE_BYTE(v) = NOQUOTE_1;
    return u_cast(Element*, v);
}

INLINE Element* Quasify(Value* v) {
    assert(QUOTE_BYTE(v) == NOQUOTE_1);  // e.g. can't quote void
    QUOTE_BYTE(v) = QUASIFORM_2;
    return u_cast(Element*, v);
}

INLINE Element* Quasify_Antiform(Atom* v) {
    assert(Is_Antiform(v));
    QUOTE_BYTE(v) = QUASIFORM_2;
    return u_cast(Element*, v);
}

INLINE Value* Reify(Atom* v) {
    if (QUOTE_BYTE(v) == ANTIFORM_0)
        QUOTE_BYTE(v) = QUASIFORM_2;
    return cast(Value*, v);
}

INLINE Atom* Degrade(Atom* v) {
    if (QUOTE_BYTE(v) == QUASIFORM_2)
        QUOTE_BYTE(v) = ANTIFORM_0;
    return v;
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
        QUOTE_BYTE(v) = QUASIFORM_2;
        return cast(Element*, v);
    }
    return cast(Element*, Quotify(v, 1));  // a non-antiform winds up quoted
}

INLINE Atom* Meta_Unquotify_Undecayed(Atom* v) {
    if (QUOTE_BYTE(v) == QUASIFORM_2)
        QUOTE_BYTE(v) = ANTIFORM_0;
    else
        Unquotify_Core(v, 1);  // will assert the input is quoted
    return v;
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
