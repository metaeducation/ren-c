//
//  File: %sys-typeset.h
//  Summary: {Definitions for Typeset Values}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// A typeset is a collection of REB_XXX types, implemented as a 64-bit bitset.
// (Though user-defined types would clearly require a different approach to
// typechecking, using a bitset for built-in types could still be used as an
// optimization for common parameter cases.)
//
// While available to the user to manipulate directly as a TYPESET!, cells
// of this category have another use in describing the fields of objects
// ("KEYS") or parameters of function frames ("PARAMS").  When used for that
// purpose, they not only list the legal types...but also hold a symbol for
// naming the field or parameter.  R3-Alpha made these a special kind of WORD!
// called an "unword", but they lack bindings and have more technically
// in common with the evolving requirements of typesets.
//
// If values beyond REB_MAX (but still < 64) are used in the bitset, they are
// "pseudotypes", which signal properties of the typeset when acting in a
// paramlist or keylist.  REB_0 is also a pseduotype, as when the first bit
// (for 0) is set in the typeset, that means it is "<end>-able".
//
// !!! At present, a TYPESET! created with MAKE TYPESET! cannot set the
// internal symbol.  Nor can it set the pseudotype flags, though that might
// someday be allowed with a syntax like:
//
//      make typeset! [<hide> <quote> <protect> text! integer!]
//

inline static bool IS_KIND_SYM(REBSYM s)
  { return s != SYM_0 and s < cast(REBSYM, REB_MAX); }

inline static enum Reb_Kind KIND_FROM_SYM(REBSYM s) {
    assert(IS_KIND_SYM(s));
    return cast(enum Reb_Kind, cast(int, (s)));
}

#define SYM_FROM_KIND(k) \
    cast(REBSYM, cast(enum Reb_Kind, (k)))

inline static REBSYM VAL_TYPE_SYM(REBCEL(const*) v) {
    //
    // !!! The extension type list is limited to a finite set as a first step
    // of generalizing the approach.  Bridge compatibility for things like
    // molding the type with some built-in symbols.
    //
    enum Reb_Kind k = VAL_TYPE_KIND_OR_CUSTOM(v);
    if (k != REB_CUSTOM)
        return SYM_FROM_KIND(k);

    RELVAL *ext = ARR_HEAD(PG_Extension_Types);
    REBTYP *t = VAL_TYPE_CUSTOM(v);
    if (t == VAL_TYPE_CUSTOM(ext + 0))
        return SYM_LIBRARY_X;
    if (t == VAL_TYPE_CUSTOM(ext + 1))
        return SYM_IMAGE_X;
    if (t == VAL_TYPE_CUSTOM(ext + 2))
        return SYM_VECTOR_X;
    if (t == VAL_TYPE_CUSTOM(ext + 3))
        return SYM_GOB_X;
    assert(t == VAL_TYPE_CUSTOM(ext + 4));
    return SYM_STRUCT_X;
}


//=//// TYPESET BITS //////////////////////////////////////////////////////=//
//
// Operations when typeset is done with a bitset (currently all typesets)

#define VAL_TYPESET_STRING_NODE(v) \
    PAYLOAD(Any, (v)).first.node

#define VAL_TYPESET_STRING(v) \
    STR(VAL_TYPESET_STRING_NODE(v))


#define VAL_TYPESET_LOW_BITS(v) \
    PAYLOAD(Any, (v)).second.u32

#define VAL_TYPESET_HIGH_BITS(v) \
    EXTRA(Typeset, (v)).high_bits

inline static bool TYPE_CHECK(REBCEL(const*) v, REBYTE n) {
    assert(MIRROR_BYTE(v) == REB_TYPESET);

    if (n < 32)
        return did (VAL_TYPESET_LOW_BITS(v) & FLAGIT_KIND(n));

    assert(n < REB_MAX_PLUS_MAX);
    return did (VAL_TYPESET_HIGH_BITS(v) & FLAGIT_KIND(n - 32));
}

inline static bool TYPE_CHECK_BITS(REBCEL(const*) v, REBU64 bits) {
    assert(MIRROR_BYTE(v) == REB_TYPESET);

    uint_fast32_t low = bits & cast(uint32_t, 0xFFFFFFFF);
    if (low & VAL_TYPESET_LOW_BITS(v))
        return true;

    uint_fast32_t high = bits >> 32;
    if (high & VAL_TYPESET_HIGH_BITS(v))
        return true;

    return false;
}

inline static bool TYPE_CHECK_EXACT_BITS(REBCEL(const*) v, REBU64 bits) {
    assert(MIRROR_BYTE(v) == REB_TYPESET);

    uint_fast32_t low = bits & cast(uint32_t, 0xFFFFFFFF);
    if (low != VAL_TYPESET_LOW_BITS(v))
        return false;

    uint_fast32_t high = bits >> 32;
    if (high != VAL_TYPESET_HIGH_BITS(v))
        return false;

    return true;
}

inline static void TYPE_SET(RELVAL *v, REBYTE n) {
    assert(MIRROR_BYTE(v) == REB_TYPESET);

    if (n < 32) {
        VAL_TYPESET_LOW_BITS(v) |= FLAGIT_KIND(n);
        return;
    }
    assert(n < REB_MAX_PLUS_MAX);
    VAL_TYPESET_HIGH_BITS(v) |= FLAGIT_KIND(n - 32);
}

inline static void TYPE_CLEAR(RELVAL *v, REBYTE n) {
    assert(MIRROR_BYTE(v) == REB_TYPESET);

    if (n < 32) {
        VAL_TYPESET_HIGH_BITS(v) &= ~FLAGIT_KIND(n);
        return;
    }
    assert(n < REB_MAX_PLUS_MAX);
    VAL_TYPESET_HIGH_BITS(v) &= ~FLAGIT_KIND(n - 32);
}

inline static bool EQUAL_TYPESET(REBCEL(const*) v1, REBCEL(const*) v2) {
    assert(MIRROR_BYTE(v1) == REB_TYPESET);
    assert(MIRROR_BYTE(v2) == REB_TYPESET);

    if (VAL_TYPESET_LOW_BITS(v1) != VAL_TYPESET_LOW_BITS(v2))
        return false;
    if (VAL_TYPESET_HIGH_BITS(v1) != VAL_TYPESET_HIGH_BITS(v2))
        return false;
    return true;
}

inline static void CLEAR_ALL_TYPESET_BITS(RELVAL *v) {
    assert(MIRROR_BYTE(v) == REB_TYPESET);

    VAL_TYPESET_HIGH_BITS(v) = 0;
    VAL_TYPESET_LOW_BITS(v) = 0;
}


//=//// PARAMETER CLASS ///////////////////////////////////////////////////=//
//
// R3-Alpha called parameter cells that were used to make keys "unwords", and
// their VAL_TYPE() dictated their parameter behavior.  Ren-C saw them more
// as being like TYPESET!s with an optional symbol, which made the code easier
// to understand and less likely to crash, which would happen when the special
// "unwords" fell into any context that would falsely interpret their bindings
// as bitsets.
//
// Yet there needed to be a place to put the parameter's class.  So it is
// packed in with the TYPESET_FLAG_XXX bits.
//

typedef enum Reb_Kind Reb_Param_Class;

    //
    // `REB_P_LOCAL` is a "pure" local, which will be set to null by
    // argument fulfillment.  It is indicated by a SET-WORD! in the function
    // spec, or by coming after a <local> tag in the function generators.
    //

    // `REB_P_NORMAL` is cued by an ordinary WORD! in the function spec
    // to indicate that you would like that argument to be evaluated normally.
    //
    //     >> foo: function [a] [print [{a is} a]]
    //
    //     >> foo 1 + 2
    //     a is 3
    //
    // Special outlier EVAL/ONLY can be used to subvert this:
    //
    //     >> eval/only :foo 1 + 2
    //     a is 1
    //     ** Script error: + does not allow void! for its value1 argument
    //

    // `REB_P_HARD_QUOTE` is cued by a GET-WORD! in the function spec
    // dialect.  It indicates that a single value of content at the callsite
    // should be passed through *literally*, without any evaluation:
    //
    //     >> foo: function [:a] [print [{a is} a]]
    //
    //     >> foo 1 + 2
    //     a is 1
    //
    //     >> foo (1 + 2)
    //     a is (1 + 2)
    //

    // `REB_P_REFINEMENT`
    //

    // `REB_P_SOFT_QUOTE` is cued by a LIT-WORD! in the function spec
    // dialect.  It quotes with the exception of GROUP!, GET-WORD!, and
    // GET-PATH!...which will be evaluated:
    //
    //     >> foo: function ['a] [print [{a is} a]
    //
    //     >> foo 1 + 2
    //     a is 1
    //
    //     >> foo (1 + 2)
    //     a is 3
    //
    // Although possible to implement soft quoting with hard quoting, it is
    // a convenient way to allow callers to "escape" a quoted context when
    // they need to.
    //


inline static Reb_Param_Class VAL_PARAM_CLASS(const RELVAL *v) {
    assert(IS_PARAM(v));
    return cast(Reb_Param_Class, KIND_BYTE_UNCHECKED(v));
}


//=////////////////////////////////////////////////////////////////////////=//
//
// TYPESET FLAGS and PSEUDOTYPES USED AS FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// At the moment, typeset flags are folded into the 64-bit test of the typeset
// bits using out-of-range of 1...REB_MAX datatypes as "psuedo-types".
//

// Endability is distinct from optional, and it means that a parameter is
// willing to accept being at the end of the input.  This means either
// an infix dispatch's left argument is missing (e.g. `do [+ 5]`) or an
// ordinary argument hit the end (e.g. the trick used for `>> help` when
// the arity is 1 usually as `>> help foo`)
//
#define Is_Param_Endable(v) \
    TYPE_CHECK((v), REB_TS_ENDABLE)

// Indicates that when this parameter is fulfilled, it will do so with a
// value of type VARARGS!, that actually just holds a pointer to the frame
// state and allows more arguments to be gathered at the callsite *while the
// function body is running*.
//
// Note the important distinction, that a variadic parameter and taking
// a VARARGS! type are different things.  (A function may accept a
// variadic number of VARARGS! values, for instance.)
//
#define Is_Param_Variadic(v) \
    TYPE_CHECK((v), REB_TS_VARIADIC)

// Skippability is used on quoted arguments to indicate that they are willing
// to "pass" on something that isn't a matching type.  This gives an ability
// that a variadic doesn't have, which is to make decisions about rejecting
// a parameter *before* the function body runs.
//
#define Is_Param_Skippable(v) \
    TYPE_CHECK((v), REB_TS_SKIPPABLE)

// Can't be reflected (set with PROTECT/HIDE) or specialized out
//
// !!! Note: Currently, the semantics of Is_Param_Hidden() are rather sketchy.
// The flag (REB_TS_HIDDEN) is not put on REB_P_LOCAL and it hasn't been
// figured out how such a flag would be managed on a per object or frame
// instance while sharing the same paramlist/keylist (a method like
// CELL_FLAG_PROTECTED might be needed if that feature is interesting).
//
#define Is_Param_Hidden(v) \
    TYPE_CHECK((v), REB_TS_HIDDEN)

// Can't be bound to beyond the current bindings.
//
// !!! This flag was implied in R3-Alpha by TYPESET_FLAG_HIDDEN.  However,
// the movement of SELF out of being a hardcoded keyword in the binding
// machinery made it start to be considered as being a by-product of the
// generator, and hence a "userspace" word (like definitional return).
// To avoid disrupting all object instances with a visible SELF, it was
// made hidden...which worked until a bugfix restored the functionality
// of checking to not bind to hidden things.  UNBINDABLE is an interim
// solution to separate the property of bindability from visibility, as
// the SELF solution shakes out--so that SELF may be hidden but bind.
//
#define Is_Param_Unbindable(v) \
    TYPE_CHECK((v), REB_TS_UNBINDABLE)

// Parameters can be marked such that if they are blank, the action will not
// be run at all.  This is done via the `<blank>` annotation, which indicates
// "handle blanks specially" (in contrast to BLANK!, which just means a
// parameter can be passed in as a blank, and the function runs normally)
//
#define Is_Param_Noop_If_Blank(v) \
    TYPE_CHECK((v), REB_TS_NOOP_IF_BLANK

// !!! TBD: find a bit to store this in (param is full at the moment, so we
// force all parameters to be rechecked in a skin.
//
#define Set_Param_Skin_Expanded(v) NOOP
#define Is_Param_Skin_Expanded(v) true


//=//// PARAMETER SYMBOL //////////////////////////////////////////////////=//
//
// Name should be NULL unless typeset in object keylist or func paramlist

inline static const REBSTR *VAL_KEY_SPELLING(const RELVAL *v) {
    assert(IS_PARAM_KIND(KIND_BYTE_UNCHECKED(v)));
    return VAL_TYPESET_STRING(v);
}

inline static const REBSTR *VAL_KEY_CANON(const RELVAL *v) {
    assert(IS_PARAM_KIND(KIND_BYTE_UNCHECKED(v)));
    return STR_CANON(VAL_KEY_SPELLING(v));
}

inline static OPT_REBSYM VAL_KEY_SYM(const RELVAL *v) {
    assert(IS_PARAM_KIND(KIND_BYTE_UNCHECKED(v)));
    return STR_SYMBOL(VAL_KEY_SPELLING(v)); // mirrors canon's symbol
}

#define VAL_PARAM_SPELLING(p) VAL_KEY_SPELLING(p)
#define VAL_PARAM_CANON(p) VAL_KEY_CANON(p)
#define VAL_PARAM_SYM(p) VAL_KEY_SYM(p)

inline static REBVAL *Init_Typeset(RELVAL *out, REBU64 bits)
{
    RESET_CELL(out, REB_TYPESET, CELL_MASK_NONE);
    VAL_TYPESET_LOW_BITS(out) = bits & cast(uint32_t, 0xFFFFFFFF);
    VAL_TYPESET_HIGH_BITS(out) = bits >> 32;
    return cast(REBVAL*, out);
}


// For the moment, a param has a cell kind that is a REB_TYPESET, but then
// overlays an actual kind as being a pseudotype for a parameter.  This would
// be better done with bits in the typeset node...which requires making
// typesets more complex (the original "64 bit flags" design is insufficient
// for a generalized typeset!)
//
inline static REBVAL *Init_Param(
    RELVAL *out,
    Reb_Param_Class pclass,
    const REBSTR *spelling,
    REBU64 bits
){
    RESET_CELL(out, REB_TYPESET, CELL_FLAG_FIRST_IS_NODE);
    mutable_KIND_BYTE(out) = pclass;

    VAL_TYPESET_STRING_NODE(out) = NOD(spelling);
    VAL_TYPESET_LOW_BITS(out) = bits & cast(uint32_t, 0xFFFFFFFF);
    VAL_TYPESET_HIGH_BITS(out) = bits >> 32;
    assert(IS_PARAM(out));
    return cast(REBVAL*, out);
}

// Context keys and action parameters use a compatible representation (this
// enables using action paramlists as FRAME! context keylists).  However,
// Rebol objects historically don't do any typechecking, so this just says
// any value is legal.
//
// !!! An API for hinting types in FRAME! contexts could be useful, if that
// was then used to make an ACTION! out of it...which is a conceptual idea
// for the "real way to make actions":
//
// https://forum.rebol.info/t/1002
//
#define Init_Context_Key(out,spelling) \
    Init_Param((out), REB_P_NORMAL, (spelling), TS_VALUE)


// This is an interim workaround for the need to be able check constrained
// data types (e.g. PATH!-with-BLANK!-at-head being REFINEMENT!).  See
// Startup_Fake_Type_Constraint() for an explanation.
//
// !!! All type constraints have been temporarily removed from typesets in
// order to reclaim bits.  However, type checks that want to ultimately
// include parameter constraints (e.g. function arguments) should call this
// instead of checking typeset bit flags directly.
//
inline static bool Typecheck_Including_Constraints(
    const RELVAL *param,
    const RELVAL *v
){
    if (TYPE_CHECK(param, VAL_TYPE(v)))
        return true;

    return false;
}


inline static bool Is_Typeset_Invisible(const RELVAL *param) {
    REBU64 bits = VAL_TYPESET_LOW_BITS(param);
    bits |= cast(REBU64, VAL_TYPESET_HIGH_BITS(param)) << 32;
    return (bits & TS_OPT_VALUE) == 0;  // e.g. `return: []` or `[/refine]`
}


inline static REBVAL *Refinify(REBVAL *v);  // forward declaration needed


// During the process of specialization, a NULL refinement means that it has
// not been specified one way or the other (MAKE FRAME! creates a frame with
// all nulled cells).  However, by the time a user function runs with that
// frame, those nulled cells are turned to BLANK! so they can be checked via
// a plain WORD! (not GET-WORD!).  The exception is <opt> refinements--which
// treat null as the unused state (or state when null is explicitly passed).
//
// Note: This does not cover features like "skippability", "endability",
// dequoting and requoting, etc.  Those are evaluator mechanics for filling
// the slot--this happens after that.
//
inline static void Typecheck_Refinement_And_Canonize(
    const RELVAL *param,
    REBVAL *arg
){
    assert(NOT_CELL_FLAG(arg, ARG_MARKED_CHECKED));
    assert(TYPE_CHECK(param, REB_TS_REFINEMENT));

    if (IS_NULLED(arg)) {
        //
        // Not in use
    }
    else if (Is_Typeset_Invisible(param)) {
        //
        // Refinements that don't have a corresponding argument are in a
        // sense LOGIC!-based.  But for convenience, Ren-C canonizes them as
        // either a NULL or a refinement-style PATH!--providing logical
        // false/true behavior while making it easier to chain them, e.g.
        //
        //    keep: func [value /only] [... append/(only) ...]
        //
        // It might be argued that any truthy value should be fair game for
        // being canonized, but be a bit more conservative to try and catch
        // likely mistakes.  Accepting refinement-style paths means accepting
        // one's own canonizations (which seems important) or being able to
        // use one logic-seeming refinement to assign another.
        if (
            (IS_LOGIC(arg) and VAL_LOGIC(arg))
            or IS_PATH(arg)  // !!! Is this too lax?
        ){
            Refinify(Init_Word(arg, VAL_PARAM_SPELLING(param)));
        }
        else if (IS_LOGIC(arg)) {
            assert(not VAL_LOGIC(arg));
            Init_Nulled(arg);
        }
        else if (IS_BLANK(arg))  // give an error to show where this is
            fail ("Use NULL/FALSE for unused refinements, not BLANK!");
        else
            fail (Error_Invalid_Type(VAL_TYPE(arg)));
    }
    else if (not Typecheck_Including_Constraints(param, arg))
        fail (Error_Invalid_Type(VAL_TYPE(arg)));

    SET_CELL_FLAG(arg, ARG_MARKED_CHECKED);
}
