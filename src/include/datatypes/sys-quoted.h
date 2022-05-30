//
//  File: %sys-quoted.h
//  Summary: {Definitions for QUOTED! Datatype}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2021 Ren-C Open Source Contributors
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
// As an efficiency trick, up to 3 quoting levels can be represented in a
// cell without a separate allocation.  The KIND3Q_BYTE() div 4 is the "quote
// level" of a value.  Then the byte mod 4 becomes the actual type.  This
// means only an actual REB_QUOTED at "apparent quote-level 0" has its own
// payload...as a last resort if the level exceeded what the type byte can
// encode.
//
// This saves on storage and GC load for small levels of quotedness, at the
// cost of making VAL_TYPE() do an extra comparison to clip all values above
// 64 to act as REB_QUOTED.  Operations like IS_WORD() are not speed affected,
// as they do not need to worry about the aliasing and can just test the byte
// against the unquoted REB_WORD value they are interested in.
//
// At 4 or more quotes, a "pairing" array is used (a compact form with only a
// series tracking node, sizeof(REBVAL)*2).  This is the smallest size of a
// GC'able entity--the same size as a singular array, but a pairing is used so
// the GC picks up from a cell pointer that it is a pairing and be placed as a
// REBVAL* in the cell.  These payloads can be shared between higher-level
// quoted instances.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * Quoting levels are limited to about 192.  That might seem strange, since
//   once you've paid for an allocation there seem to be two free slots in a
//   REB_QUOTED to use...which could even be used for a 64-bit quoting level
//   on 32-bit platforms.  *HOWEVER* it turns out there is significant value
//   in leaving this space free so that when instances share the allocation
//   they can still bind their cells distinctly...it avoids needing to copy
//   the allocation at each instance.
//
//   (Having to pay for a series node allocation to get less than 8-bits
//   seems pretty rough; but when you run out of bits, you run out.  64-bit
//   builds could apply an extra byte in the header for a quoting level.)
//
// * There's a possibility for "churn" where something on the brink of needing
//   an allocation gets repeatedly quoted and unquoted.  Each time it drops
//   a quote level it forgets its allocation, thus not able to share it if
//   it gets quoted again.  This suggests it might be better not to immediately
//   collapse the quotes to the non-allocated form at 0-3 levels, but rather
//   to wait and let the GC canonize it once any churn is likely to have
//   settled down.


//=//// WORD DEFINITION CODE //////////////////////////////////////////////=//
//
// !!! The code should get reorganized to not have these definitions in the
// quoting header.  But for the moment this untangles the dependencies so
// that it will compile.
//

inline static void Unbind_Any_Word(RELVAL *v);  // forward define


#define MAX_QUOTE_DEPTH (255 - REB_QUOTED)

inline static REBLEN VAL_QUOTED_PAYLOAD_DEPTH(const RELVAL *v) {
    assert(IS_QUOTED(v));
    assert(HEART_BYTE(v) >= REB_QUOTED);
    return HEART_BYTE(v) - REB_QUOTED;
}

inline static REBVAL* VAL_QUOTED_PAYLOAD_CELL(const RELVAL *v) {
    assert(VAL_QUOTED_PAYLOAD_DEPTH(v) > 3);  // else quote fits in one cell
    return VAL(VAL_NODE1(v));
}

inline static REBLEN VAL_QUOTED_DEPTH(const RELVAL *v) {
    if (KIND3Q_BYTE(v) >= REB_64)  // shallow enough to use type byte trick...
        return KIND3Q_BYTE(v) / REB_64;  // ...see explanation above
    return VAL_QUOTED_PAYLOAD_DEPTH(v);
}

inline static REBLEN VAL_NUM_QUOTES(const RELVAL *v) {
    if (not IS_QUOTED(v))
        return 0;
    return VAL_QUOTED_DEPTH(v);
}


// It is necessary to be able to store relative values in escaped cells.
//
inline static RELVAL *Quotify_Core(
    RELVAL *v,
    REBLEN depth
){
    if (depth == 0)
        return v;

    if (KIND3Q_BYTE_UNCHECKED(v) == REB_QUOTED) {  // reuse payload
        assert(VAL_QUOTED_PAYLOAD_DEPTH(v) + depth <= MAX_QUOTE_DEPTH);
        mutable_HEART_BYTE(v) += depth;
        return v;
    }

    REBYTE kind = KIND3Q_BYTE_UNCHECKED(v) % REB_64;  // HEART_BYTE may differ
    assert(kind <= REB_MAX);

    if (kind == REB_BAD_WORD)
        assert(NOT_CELL_FLAG(v, ISOTOPE));  // invariant is no quoted isotopes

    depth += KIND3Q_BYTE_UNCHECKED(v) / REB_64;

    if (depth <= 3) { // can encode in a cell with no REB_QUOTED payload
        mutable_KIND3Q_BYTE(v) = kind + (REB_64 * depth);
    }
    else {
        // An efficiency trick here could point to Lib(VOID), Lib(BLANK),
        // Lib(NULL), etc. in those cases, so long as GC knew.  (But how
        // efficient do 4-level-deep-quoted nulls need to be, really?)

        // This is an uncomfortable situation of moving values without a
        // specifier; but it needs to be done otherwise you could not have
        // literals in function bodies.  What it means is that you should
        // not be paying attention to the cell bits for making decisions
        // about specifiers and such.  The format bits of this cell are
        // essentially noise, and only the literal's specifier should be used.

        REBVAL *unquoted = Alloc_Pairing();
        SET_END(PAIRING_KEY(unquoted));  // Key not used ATM, want GC-safe

        Copy_Cell_Header(unquoted, v);
        mutable_KIND3Q_BYTE(unquoted) = kind;  // escaping only in literal

        unquoted->payload = v->payload;

        Manage_Pairing(unquoted);

        Reset_Cell_Header_Untracked(v, REB_QUOTED, CELL_FLAG_FIRST_IS_NODE);
        INIT_VAL_NODE1(v, unquoted);
        mutable_HEART_BYTE(v) = REB_QUOTED + depth;

        if (ANY_WORD_KIND(CELL_HEART(cast(REBCEL(const*), unquoted)))) {
            //
            // The shared word is put in an unbound state, since each quoted
            // instance can be bound differently.
            //
            assert(VAL_WORD_INDEX_U32(v) == VAL_WORD_INDEX_U32(unquoted));
            unquoted->extra = v->extra;  // !!! for easier Unbind, review
            Unbind_Any_Word(unquoted);  // so that binding is a spelling
            // leave `v` binding as it was
        }
        else if (Is_Bindable(unquoted)) {
            mutable_BINDING(unquoted) = UNBOUND;  // must look unbound
            // leave `v` to hold the binding as it was
        }
        else {
            // We say all REB_QUOTED cells are bindable, so their binding gets
            // checked even if the contained cell isn't bindable.  By setting
            // the binding to UNBOUND if the contained cell isn't bindable, it
            // prevents needing to make Is_Bindable() a more complex check,
            // we can just say yes always but have it unbound if not.
            //
            unquoted->extra = v->extra;  // save the non-binding-related data
            mutable_BINDING(v) = UNBOUND;
        }

      #if !defined(NDEBUG)
        SET_CELL_FLAG(unquoted, PROTECTED); // maybe shared; can't change
      #endif
    }

    return v;
}

#if (! CPLUSPLUS_11)
    #define Quotify Quotify_Core
#else
    inline static REBVAL *Quotify(REBVAL *v, REBLEN depth)
        { return cast(REBVAL*, Quotify_Core(v, depth)); }

    inline static RELVAL *Quotify(RELVAL *v, REBLEN depth)
        { return Quotify_Core(v, depth); }
#endif


// Only works on small escape levels that fit in a cell (<=3).  So it can
// do '''X -> ''X, ''X -> 'X or 'X -> X.  Use Unquotify() for the more
// generic routine, but this is needed by the evaluator most commonly.
//
// Note: Strangely pretentious name is on purpose, to discourage general use.
//
inline static RELVAL *Unquotify_In_Situ(RELVAL *v, REBLEN unquotes)
{
    assert(KIND3Q_BYTE(v) >= REB_64);  // not an in-situ quoted value otherwise
    assert(cast(REBLEN, KIND3Q_BYTE(v) / REB_64) >= unquotes);
    mutable_KIND3Q_BYTE(v) -= REB_64 * unquotes;
    return v;
}


inline static void Collapse_Quoted_Internal(RELVAL *v)
{
    REBVAL *unquoted = VAL_QUOTED_PAYLOAD_CELL(v);
    assert(
        KIND3Q_BYTE(unquoted) != REB_0
        and KIND3Q_BYTE(unquoted) != REB_QUOTED
        and KIND3Q_BYTE(unquoted) < REB_MAX
    );
    Copy_Cell_Header(v, unquoted);
    if (ANY_WORD_KIND(CELL_HEART(cast(REBCEL(const*), unquoted)))) {
        //
        // `v` needs to retain the primary binding index (which was
        // kept in its QUOTED! form), but sync with the virtual binding
        // information in the escaped form.
        //
        INIT_VAL_WORD_SYMBOL(v, VAL_WORD_SYMBOL(unquoted));
        mutable_HEART_BYTE(v) = HEART_BYTE(unquoted);
        // Note: leave binding as is...
    }
    else {
        v->payload = unquoted->payload;
        if (not Is_Bindable(v))  // non-bindable types need the extra data
            v->extra = unquoted->extra;
    }
}


// Turns 'X into X, or '''''[1 + 2] into '''(1 + 2), etc.
//
// Works on escape levels that fit in the cell (<= 3) as well as those that
// require a second cell to point at in a REB_QUOTED payload.
//
inline static RELVAL *Unquotify_Core(RELVAL *v, REBLEN unquotes) {
    if (unquotes == 0)
        return v;

    if (KIND3Q_BYTE(v) != REB_QUOTED)
        return Unquotify_In_Situ(v, unquotes);

    REBLEN depth = VAL_QUOTED_PAYLOAD_DEPTH(v);
    assert(depth > 3 and depth >= unquotes);
    depth -= unquotes;

    if (depth > 3) // still can't do in-situ escaping within a single cell
        mutable_HEART_BYTE(v) -= unquotes;
    else {
        Collapse_Quoted_Internal(v);
        mutable_KIND3Q_BYTE(v) += (REB_64 * depth);
    }
    return v;
}

#if (! CPLUSPLUS_11)
    #define Unquotify Unquotify_Core
#else
    inline static REBVAL *Unquotify(REBVAL *v, REBLEN depth)
        { return cast(REBVAL*, Unquotify_Core(v, depth)); }

    inline static RELVAL *Unquotify(RELVAL *v, REBLEN depth)
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

inline static RELVAL *Isotopic_Quote(RELVAL *v) {
    if (IS_BAD_WORD(v) and GET_CELL_FLAG(v, ISOTOPE)) {
        CLEAR_CELL_FLAG(v, ISOTOPE);  // ...make it "friendly" now...
        return v;  // ...but differentiate its status by not quoting it...
    }
    return Quotify(v, 1);  // a non-isotope BAD-WORD! winds up quoted
}

inline static RELVAL *Isotopic_Unquote(RELVAL *v) {
    assert(not IS_NULLED(v));  // use Meta_Unquotify() instead
    if (IS_BAD_WORD(v)) {  // Meta quote flipped isotope off, flip back on.
        assert(NOT_CELL_FLAG(v, ISOTOPE));
        assert(VAL_BAD_WORD_ID(v) != SYM_VOID);  // enforce special handling
        assert(VAL_BAD_WORD_ID(v) != SYM_END);  // END isotopes shouldn't exist
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

inline static RELVAL *Isotopic_Quotify(RELVAL *v, REBLEN depth) {
    if (depth == 0)
        return v;
    Isotopic_Quote(v);
    return Quotify(v, depth - 1);
}

inline static RELVAL *Isotopic_Unquotify(RELVAL *v, REBLEN depth) {
    assert(not IS_NULLED(v));  // see Meta_Unquotify
    if (depth == 0)
        return v;
    Unquotify(v, depth - 1);
    return Isotopic_Unquote(v);
}

#if CPLUSPLUS_11
    inline static REBVAL *Isotopic_Quote(REBVAL *v)
      { return SPECIFIC(Isotopic_Quote(cast(RELVAL*, v))); }

    inline static REBVAL *Isotopic_Unquote(REBVAL *v)
      { return SPECIFIC(Isotopic_Unquote(cast(RELVAL*, v))); }

    inline static REBVAL *Isotopic_Quotify(REBVAL *v, REBLEN depth)
      { return SPECIFIC(Isotopic_Quotify(cast(RELVAL*, v), depth)); }

    inline static REBVAL *Isotopic_Unquotify(REBVAL *v, REBLEN depth)
      { return SPECIFIC(Isotopic_Unquotify(cast(RELVAL*, v), depth)); }
#endif


//=//// META QUOTING ///////////////////////////////////////////////////////=//

inline static bool Is_Blackhole(const RELVAL *v);  // forward decl

// Meta quoting is almost exactly like isotopic quoting, but it has a twist
// that NULL does not become a single tick mark (') but rather it stays as
// NULL.  It also translates emptiness (e.g. an END marker) into an isotope
// BAD-WORD! of ~void~.  It is done by ^ and the the REB_META_XXX family.

inline static RELVAL *Meta_Quotify(RELVAL *v) {
    if (IS_END(v))
        return Init_Blank(v);
    if (IS_NULLED(v))
        return v;  // as-is
    return Isotopic_Quote(v);
}

inline static RELVAL *Meta_Unquotify(RELVAL *v) {
    if (IS_NULLED(v))
        return v;  // do nothing
    return Isotopic_Unquote(v);
}

#if CPLUSPLUS_11
    inline static REBVAL *Meta_Quotify(REBVAL *v)
        { return SPECIFIC(Meta_Quotify(cast(RELVAL*, v))); }

    inline static REBVAL *Meta_Unquotify(REBVAL *v)
        { return SPECIFIC(Meta_Unquotify(cast(RELVAL*, v))); }
#endif


inline static REBCEL(const*) VAL_UNESCAPED(const RELVAL *v) {
    if (KIND3Q_BYTE_UNCHECKED(v) != REB_QUOTED)  // allow unreadable trash
        return v;  // Note: kind byte may be > 64

    // The reason this routine returns `const` is because you can't modify
    // the contained value without affecting other views of it, if it is
    // shared in an escaping.  Modifications must be done with awareness of
    // the original RELVAL, and that it might be a QUOTED!.
    //
    return VAL_QUOTED_PAYLOAD_CELL(v);
}


inline static REBLEN Dequotify(RELVAL *v) {
    if (KIND3Q_BYTE(v) != REB_QUOTED) {
        REBLEN depth = KIND3Q_BYTE(v) / REB_64;
        mutable_KIND3Q_BYTE(v) %= REB_64;
        return depth;
    }

    REBLEN depth = VAL_QUOTED_PAYLOAD_DEPTH(v);
    Collapse_Quoted_Internal(v);
    return depth;
}


// !!! Temporary workaround for what was IS_META_WORD() (now not its own type)
//
inline static bool IS_QUOTED_WORD(const RELVAL *v) {
    return IS_QUOTED(v)
        and VAL_QUOTED_DEPTH(v) == 1
        and CELL_KIND(VAL_UNESCAPED(v)) == REB_WORD;
}

// !!! Temporary workaround for what was IS_META_PATH() (now not its own type)
//
inline static bool IS_QUOTED_PATH(const RELVAL *v) {
    return IS_QUOTED(v)
        and VAL_QUOTED_DEPTH(v) == 1
        and CELL_KIND(VAL_UNESCAPED(v)) == REB_PATH;
}
