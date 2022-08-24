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

inline static void Unbind_Any_Word(Cell(*) v);  // forward define



inline static REBLEN VAL_QUOTED_DEPTH(Cell(const*) v) {
    assert(IS_QUOTED(v));
    return (QUOTE_BYTE(v) - UNQUOTED_1) >> 1;
}

inline static REBLEN VAL_NUM_QUOTES(Cell(const*) v) {
    assert(QUOTE_BYTE(v) != ISOTOPE_0);
    return (QUOTE_BYTE(v) - UNQUOTED_1) >> 1;
}


// It is necessary to be able to store relative values in escaped cells.
//
inline static Cell(*) Quotify_Core(
    Cell(*) v,
    REBLEN depth
){
    if (depth == 0)
        return v;

    if (VAL_NUM_QUOTES(v) + depth >  MAX_QUOTE_DEPTH)
        fail ("Quoting Depth of 126 Exceeded");

    mutable_QUOTE_BYTE(v) += (depth << 1);
    return v;
}

#if (! CPLUSPLUS_11)
    #define Quotify Quotify_Core
#else
    inline static REBVAL *Quotify(REBVAL *v, REBLEN depth)
        { return cast(REBVAL*, Quotify_Core(v, depth)); }

    inline static Cell(*) Quotify(Cell(*) v, REBLEN depth)
        { return Quotify_Core(v, depth); }
#endif


// Turns 'X into X, or '''''[1 + 2] into '''(1 + 2), etc.
//
// Works on escape levels that fit in the cell (<= 3) as well as those that
// require a second cell to point at in a REB_QUOTED payload.
//
inline static Cell(*) Unquotify_Core(Cell(*) v, REBLEN unquotes) {
    if (unquotes == 0) {
        assert(QUOTE_BYTE(v) != ISOTOPE_0);
        return v;
    }

    if (unquotes > VAL_NUM_QUOTES(v))
        fail ("Attempt to set quoting level of value to less than 0");

    mutable_QUOTE_BYTE(v) -= (unquotes << 1);
    return v;
}

#if (! CPLUSPLUS_11)
    #define Unquotify Unquotify_Core
#else
    inline static REBVAL *Unquotify(REBVAL *v, REBLEN depth)
        { return cast(REBVAL*, Unquotify_Core(v, depth)); }

    inline static Cell(*) Unquotify(Cell(*) v, REBLEN depth)
        { return Unquotify_Core(v, depth); }
#endif



inline static void Unquotify_Dont_Expect_Meta(Value(*) v) {
    if (Is_Meta_Of_Void(v)) {
        Init_Nulled(v);
        return;
    }
    if (not IS_QUOTED(v))
        fail (v);
    Unquotify(v, 1);
}

#define VAL_UNESCAPED(v) \
    x_cast(noquote(Cell(const*)), (v))


inline static REBLEN Dequotify(Cell(*) v) {
    REBLEN depth = VAL_NUM_QUOTES(v);
    if (mutable_QUOTE_BYTE(v) & NONQUASI_BIT)
        mutable_QUOTE_BYTE(v) = UNQUOTED_1;
    else
        mutable_QUOTE_BYTE(v) = QUASI_2;
    return depth;
}


// !!! Temporary workaround for what was IS_META_WORD() (now not its own type)
//
inline static bool IS_QUOTED_WORD(Cell(const*) v) {
    return VAL_NUM_QUOTES(v) == 1
        and CELL_HEART(VAL_UNESCAPED(v)) == REB_WORD;
}

// !!! Temporary workaround for what was IS_META_PATH() (now not its own type)
//
inline static bool IS_QUOTED_PATH(Cell(const*) v) {
    return VAL_NUM_QUOTES(v) == 1
        and CELL_HEART(v) == REB_PATH;
}

// Checks if ANY-GROUP! is like ((...)) or (...), used by COMPOSE & PARSE
//
inline static bool Is_Any_Doubled_Group(noquote(Cell(const*)) group) {
    assert(ANY_GROUP_KIND(CELL_HEART(group)));
    Cell(const*) tail;
    Cell(const*) inner = VAL_ARRAY_AT(&tail, group);
    if (inner + 1 != tail)  // should be exactly one item
        return false;
    return IS_GROUP(inner);  // if true, it's a ((...)) GROUP!
}
