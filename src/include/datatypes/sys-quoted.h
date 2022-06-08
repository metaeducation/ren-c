//
//  File: %sys-quoted.h
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
// In Ren-C, any value can be "quote" escaped.  The depth is the number of
// apostrophes, e.g. ''''X is a depth of 4.  The operator QUOTE can be used
// to add a quoting level to a value, UNQUOTE to remove one, and NOQUOTE to
// remove all quotes.
//
//     >> quote [a]
//     == '[a]
//
//     >> noquote first ['''''a]
//     == a
//
// The quote level of a value (up to 255) is stored in the QUOTE_BYTE() of
// a cell's header.  If the quoting byte is nonzero, then the type of the
// value is reported as QUOTED!.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//


//=//// WORD DEFINITION CODE //////////////////////////////////////////////=//
//
// !!! The code should get reorganized to not have these definitions in the
// quoting header.  But for the moment this untangles the dependencies so
// that it will compile.
//

inline static void Unbind_Any_Word(Cell *v);  // forward define


#define MAX_QUOTE_DEPTH 255


inline static REBLEN VAL_QUOTED_DEPTH(const Cell *v) {
    assert(IS_QUOTED(v));
    return QUOTE_BYTE(READABLE(v));
}

inline static REBLEN VAL_NUM_QUOTES(const Cell *v) {
    return QUOTE_BYTE(READABLE(v));
}


// It is necessary to be able to store relative values in escaped cells.
//
inline static Cell *Quotify_Core(
    Cell *v,
    REBLEN depth
){
    if (depth == 0)
        return v;

    if (VAL_NUM_QUOTES(v) + depth >  MAX_QUOTE_DEPTH)
        fail ("Quoting Depth of 255 Exceeded");

    mutable_QUOTE_BYTE(v) += depth;
    return v;
}

#if (! CPLUSPLUS_11)
    #define Quotify Quotify_Core
#else
    inline static REBVAL *Quotify(REBVAL *v, REBLEN depth)
        { return cast(REBVAL*, Quotify_Core(v, depth)); }

    inline static Cell *Quotify(Cell *v, REBLEN depth)
        { return Quotify_Core(v, depth); }
#endif


// Turns 'X into X, or '''''[1 + 2] into '''(1 + 2), etc.
//
// Works on escape levels that fit in the cell (<= 3) as well as those that
// require a second cell to point at in a REB_QUOTED payload.
//
inline static Cell *Unquotify_Core(Cell *v, REBLEN unquotes) {
    if (unquotes == 0)
        return v;

    if (unquotes > VAL_NUM_QUOTES(v))
        fail ("Attempt to set quoting level of value to less than 0");

    mutable_QUOTE_BYTE(v) -= unquotes;
    return v;
}

#if (! CPLUSPLUS_11)
    #define Unquotify Unquotify_Core
#else
    inline static REBVAL *Unquotify(REBVAL *v, REBLEN depth)
        { return cast(REBVAL*, Unquotify_Core(v, depth)); }

    inline static Cell *Unquotify(Cell *v, REBLEN depth)
        { return Unquotify_Core(v, depth); }
#endif


//=//// ISOTOPIC QUOTING ///////////////////////////////////////////////////=//

// When a plain BAD-WORD! evaluates, it stays as the same BAD-WORD! but with
// the isotope bit set.  If you want that to be generically reversible, then
// quoting an isotopic BAD-WORD! has to give a plain one...then quoting a
// plain one gives a QUOTED!, etc.
//
// Because QUOTE doesn't take isotope BAD-WORD!s as parameters, it doesn't have
// to deal with this problem.  But rebQ() in the API does, as does the idea
// of "literalization".

inline static Cell *Isotopic_Quote(Cell *v) {
    if (IS_BAD_WORD(v) and GET_CELL_FLAG(v, ISOTOPE)) {
        CLEAR_CELL_FLAG(v, ISOTOPE);  // ...make it "friendly" now...
        return v;  // ...but differentiate its status by not quoting it...
    }
    return Quotify(v, 1);  // a non-isotope BAD-WORD! winds up quoted
}

inline static Cell *Isotopic_Unquote(Cell *v) {
    assert(not IS_NULLED(v));  // use Meta_Unquotify() instead
    if (IS_BAD_WORD(v)) {  // Meta quote flipped isotope off, flip back on.
        assert(NOT_CELL_FLAG(v, ISOTOPE));
        SET_CELL_FLAG(v, ISOTOPE);
    }
    else {
        Unquotify_Core(v, 1);
        if (IS_BAD_WORD(v))  // ...was friendly before meta-quoting it...
            assert(NOT_CELL_FLAG(v, ISOTOPE));  // ...should still be friendly
    }
    return v;
}

// It's easiest to write the isotopic general forms by doing a single isotopic
// step, and then N - 1 non-isotopic steps.

inline static Cell *Isotopic_Quotify(Cell *v, REBLEN depth) {
    if (depth == 0)
        return v;
    Isotopic_Quote(v);
    return Quotify(v, depth - 1);
}

inline static Cell *Isotopic_Unquotify(Cell *v, REBLEN depth) {
    assert(not IS_NULLED(v));  // see Meta_Unquotify
    if (depth == 0)
        return v;
    Unquotify(v, depth - 1);
    return Isotopic_Unquote(v);
}

#if CPLUSPLUS_11
    inline static REBVAL *Isotopic_Quote(REBVAL *v)
      { return SPECIFIC(Isotopic_Quote(cast(Cell*, v))); }

    inline static REBVAL *Isotopic_Unquote(REBVAL *v)
      { return SPECIFIC(Isotopic_Unquote(cast(Cell*, v))); }

    inline static REBVAL *Isotopic_Quotify(REBVAL *v, REBLEN depth)
      { return SPECIFIC(Isotopic_Quotify(cast(Cell*, v), depth)); }

    inline static REBVAL *Isotopic_Unquotify(REBVAL *v, REBLEN depth)
      { return SPECIFIC(Isotopic_Unquotify(cast(Cell*, v), depth)); }
#endif


//=//// META QUOTING ///////////////////////////////////////////////////////=//

inline static bool Is_Blackhole(const Cell *v);  // forward decl

// Meta quoting is almost exactly like isotopic quoting, but it has a twist
// that NULL does not become a single tick mark (') but rather it stays as
// NULL.  It also translates emptiness (e.g. an END marker) into an isotope
// BAD-WORD! of ~void~.  It is done by ^ and the the REB_META_XXX family.

inline static Cell *Meta_Quotify(Cell *v) {
    if (IS_NULLED(v))
        return v;  // as-is
    return Isotopic_Quote(v);
}

inline static Cell *Meta_Unquotify(Cell *v) {
    if (IS_NULLED(v))
        return v;  // do nothing
    return Isotopic_Unquote(v);
}

#if CPLUSPLUS_11
    inline static REBVAL *Meta_Quotify(REBVAL *v)
        { return SPECIFIC(Meta_Quotify(cast(Cell*, v))); }

    inline static REBVAL *Meta_Unquotify(REBVAL *v)
        { return SPECIFIC(Meta_Unquotify(cast(Cell*, v))); }
#endif


#define VAL_UNESCAPED(v) \
    x_cast(noquote(const Cell*), (v))


inline static REBLEN Dequotify(Cell *v) {
    REBLEN depth = VAL_NUM_QUOTES(v);
    mutable_QUOTE_BYTE(v) = 0;
    return depth;
}


// !!! Temporary workaround for what was IS_META_WORD() (now not its own type)
//
inline static bool IS_QUOTED_WORD(const Cell *v) {
    return VAL_NUM_QUOTES(v) == 1
        and CELL_HEART(VAL_UNESCAPED(v)) == REB_WORD;
}

// !!! Temporary workaround for what was IS_META_PATH() (now not its own type)
//
inline static bool IS_QUOTED_PATH(const Cell *v) {
    return VAL_NUM_QUOTES(v) == 1
        and CELL_HEART(v) == REB_PATH;
}

// Checks if ANY-GROUP! is like ((...)) or (...), used by COMPOSE & PARSE
//
inline static bool Is_Any_Doubled_Group(noquote(const Cell*) group) {
    assert(ANY_GROUP_KIND(CELL_HEART(group)));
    const Cell *tail;
    const Cell *inner = VAL_ARRAY_AT(&tail, group);
    if (inner + 1 != tail)  // should be exactly one item
        return false;
    return IS_GROUP(inner);  // if true, it's a ((...)) GROUP!
}
