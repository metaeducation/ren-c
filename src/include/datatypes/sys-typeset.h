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

inline static bool IS_KIND_SYM(SYMID s)
  { return s != SYM_0 and s < cast(SYMID, REB_MAX); }

inline static enum Reb_Kind KIND_FROM_SYM(SYMID s) {
    assert(IS_KIND_SYM(s));
    return cast(enum Reb_Kind, cast(int, (s)));
}

#define SYM_FROM_KIND(k) \
    cast(SYMID, cast(enum Reb_Kind, (k)))

inline static SYMID VAL_TYPE_SYM(REBCEL(const*) v) {
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


#define VAL_TYPESET_PARAM_CLASS_BYTE(v) \
    FIRST_BYTE(PAYLOAD(Any, (v)).first.u)

#define mutable_VAL_TYPESET_PARAM_CLASS_BYTE(v) \
    mutable_FIRST_BYTE(PAYLOAD(Any, (v)).first.u)

#define VAL_TYPESET_LOW_BITS(v) \
    PAYLOAD(Any, (v)).second.u32

#define VAL_TYPESET_HIGH_BITS(v) \
    EXTRA(Typeset, (v)).high_bits

inline static bool TYPE_CHECK(REBCEL(const*) v, REBYTE n) {
    assert(CELL_HEART(v) == REB_TYPESET);

    if (n < 32)
        return did (VAL_TYPESET_LOW_BITS(v) & FLAGIT_KIND(n));

    assert(n < REB_MAX);
    return did (VAL_TYPESET_HIGH_BITS(v) & FLAGIT_KIND(n - 32));
}

inline static bool TYPE_CHECK_BITS(REBCEL(const*) v, REBU64 bits) {
    assert(CELL_HEART(v) == REB_TYPESET);

    uint_fast32_t low = bits & cast(uint32_t, 0xFFFFFFFF);
    if (low & VAL_TYPESET_LOW_BITS(v))
        return true;

    uint_fast32_t high = bits >> 32;
    if (high & VAL_TYPESET_HIGH_BITS(v))
        return true;

    return false;
}

inline static bool TYPE_CHECK_EXACT_BITS(
    REBCEL(const*) v,
    REBU64 bits
){
    assert(CELL_HEART(v) == REB_TYPESET);

    uint_fast32_t low = bits & cast(uint32_t, 0xFFFFFFFF);
    if (low != VAL_TYPESET_LOW_BITS(v))
        return false;

    uint_fast32_t high = bits >> 32;
    if (high != VAL_TYPESET_HIGH_BITS(v))
        return false;

    return true;
}

inline static void TYPE_SET(RELVAL *v, REBYTE n) {
    assert(IS_TYPESET(v));

    if (n < 32) {
        VAL_TYPESET_LOW_BITS(v) |= FLAGIT_KIND(n);
        return;
    }
    assert(n < REB_MAX);
    VAL_TYPESET_HIGH_BITS(v) |= FLAGIT_KIND(n - 32);
}

inline static void TYPE_CLEAR(RELVAL *v, REBYTE n) {
    assert(IS_TYPESET(v));

    if (n < 32) {
        VAL_TYPESET_HIGH_BITS(v) &= ~FLAGIT_KIND(n);
        return;
    }
    assert(n < REB_MAX);
    VAL_TYPESET_HIGH_BITS(v) &= ~FLAGIT_KIND(n - 32);
}

inline static bool EQUAL_TYPESET(
    REBCEL(const*) v1,
    REBCEL(const*) v2
){
    assert(CELL_HEART(v1) == REB_TYPESET);
    assert(CELL_HEART(v2) == REB_TYPESET);

    if (VAL_TYPESET_LOW_BITS(v1) != VAL_TYPESET_LOW_BITS(v2))
        return false;
    if (VAL_TYPESET_HIGH_BITS(v1) != VAL_TYPESET_HIGH_BITS(v2))
        return false;
    return true;
}

inline static void CLEAR_ALL_TYPESET_BITS(RELVAL *v) {
    assert(VAL_TYPE(v) == REB_TYPESET);

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
    //     ** Script error: + does not allow bad-word! for its value1 argument
    //

    // `REB_P_HARD` is cued by a quoted WORD! in the function spec
    // dialect.  It indicates that a single value of content at the callsite
    // should be passed through *literally*, without any evaluation:
    //
    //     >> foo: function ['a] [print [{a is} a]]
    //
    //     >> foo (1 + 2)
    //     a is (1 + 2)
    //
    //     >> foo :(1 + 2)
    //     a is :(1 + 2)
    //

    // `REB_P_MEDIUM` is cued by a QUOTED GET-WORD! in the function spec
    // dialect.  It quotes with the exception of GET-GROUP!, GET-WORD!, and
    // GET-PATH!...which will be evaluated:
    //
    //     >> foo: function [':a] [print [{a is} a]
    //
    //     >> foo (1 + 2)
    //     a is (1 + 2)
    //
    //     >> foo :(1 + 2)
    //     a is 3
    //
    // Although possible to implement soft quoting with hard quoting, it is
    // a convenient way to allow callers to "escape" a quoted context when
    // they need to.

    // `REB_P_SOFT` is cued by a PLAIN GET-WORD!.  It acts as a more nuanced
    // version of REB_P_MEDIUM which is escapable but will defer to enfix.
    // This covers cases like:
    //
    //     if true [...] then :(func [...] [...])  ; want escapability
    //     if true [...] then x -> [...]  ; but want enfix -> lookback to win
    //
    // Hence it is the main mode of quoting for branches.  It would be
    // unsuitable for cases like OF, however, due to this problem:
    //
    //     integer! = type of 1  ; want left quoting semantics on `type` WORD!
    //     integer! = :(first [type length]) of 1  ; want escapability
    //
    // OF wants its left hand side to be escapable, however it wants the
    // quoting behavior to out-prioritize the completion of enfix on the
    // left.  Contrast this with how THEN wants the enfix on the right to
    // win out ahead of its quoting.
    //
    // This is a subtlety that most functions don't have to worry about, so
    // using soft quoting is favored to medium quoting for being one less
    // character to type.


inline static enum Reb_Param_Class VAL_PARAM_CLASS(const REBPAR *param) {
    assert(IS_TYPESET(param));
    return cast(enum Reb_Param_Class, VAL_TYPESET_PARAM_CLASS_BYTE(param));
}

#define VAL_PARAM_FLAGS(v)           PAYLOAD(Any, (v)).first.u
#define FLAG_PARAM_CLASS_BYTE(b)     FLAG_FIRST_BYTE(b)


//=////////////////////////////////////////////////////////////////////////=//
//
// TYPESET FLAGS and PSEUDOTYPES USED AS FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// At the moment, typeset flags are folded into the 64-bit test of the typeset
// bits using out-of-range of 1...REB_MAX datatypes as "psuedo-types".
//

#define TS_NOTHING 0

// Endability is distinct from optional, and it means that a parameter is
// willing to accept being at the end of the input.  This means either
// an infix dispatch's left argument is missing (e.g. `do [+ 5]`) or an
// ordinary argument hit the end (e.g. the trick used for `>> help` when
// the arity is 1 usually as `>> help foo`)
//
#define PARAM_FLAG_ENDABLE \
    FLAG_LEFT_BIT(8)

// Indicates that when this parameter is fulfilled, it will do so with a
// value of type VARARGS!, that actually just holds a pointer to the frame
// state and allows more arguments to be gathered at the callsite *while the
// function body is running*.
//
// Note the important distinction, that a variadic parameter and taking
// a VARARGS! type are different things.  (A function may accept a
// variadic number of VARARGS! values, for instance.)
//
#define PARAM_FLAG_VARIADIC \
    FLAG_LEFT_BIT(9)

// Skippability is used on quoted arguments to indicate that they are willing
// to "pass" on something that isn't a matching type.  This gives an ability
// that a variadic doesn't have, which is to make decisions about rejecting
// a parameter *before* the function body runs.
//
#define PARAM_FLAG_SKIPPABLE \
    FLAG_LEFT_BIT(10)

#define PARAM_FLAG_REFINEMENT \
    FLAG_LEFT_BIT(11)

#define PARAM_FLAG_PREDICATE \
    FLAG_LEFT_BIT(12)

// Parameters can be marked such that if they are blank, the action will not
// be run at all.  This is done via the `<blank>` annotation, which indicates
// "handle blanks specially" (in contrast to BLANK!, which just means a
// parameter can be passed in as a blank, and the function runs normally)
//
#define PARAM_FLAG_NOOP_IF_BLANK \
    FLAG_LEFT_BIT(13)

#define PARAM_FLAG_CONST \
    FLAG_LEFT_BIT(14)

#define SET_PARAM_FLAG(v,name) \
    (VAL_PARAM_FLAGS(v) |= PARAM_FLAG_##name)

#define GET_PARAM_FLAG(v,name) \
    ((VAL_PARAM_FLAGS(v) & PARAM_FLAG_##name) != 0)

#define CLEAR_PARAM_FLAG(v,name) \
    (VAL_PARAM_FLAGS(v) &= ~PARAM_FLAG_##name)

#define NOT_PARAM_FLAG(v,name) \
    ((VAL_PARAM_FLAGS(v) & PARAM_FLAG_##name) == 0)


// All hidden parameters in the exemplar frame of an ACTION! are not shown
// on the public interface of that function.  This means type information
// is not relevant (though the type information for later phases of that
// slot may be pertinent).  So instead of type information, hidden param slots
// hold the initialization value for that position.
//
// In terms of whether the parameter is truly "hidden" from a view of a FRAME!
// with MOLD or to BIND depends on the frame's phase.  For instance, while a
// frame is running the body of an interpreted function...that phase has to
// see the locals defined for that function.  This means you can't tell from a
// frame context node pointer alone whether a key is visible...the full FRAME!
// cell--phase included--must be used.
//
// Hiding is used to accomplish the desire of signaling that an ~unset~ be
// removed rom the interface for a frame, so you can actualy specialize a
// value out as ~unset~:
//
//     >> f: make frame! :append
//     >> f/value: '~unset~  ; typically this would mean "unspecialized"
//     >> protect/hide 'f/value  ; hiding it means "no, it's the final value"
//     >> apu: make action! f
//     >> apu [a b c]
//     == [a b c ~unset~]
//
// For this mechanic to work, there has to be a bit on frames that tracks
// visibility on a per-instance basis.  To avoid having to make a new keylist
// each time this happens, the NODE_FLAG_MARKED bit on a context is taken
// to mean this.  It won't be copied by Copy_Cell() that reads the variable,
// and it is heeded here as VAR_MARKED_HIDDEN if a value cell is given.
//
inline static bool Is_Param_Hidden(const REBPAR *param)
{
    if (GET_CELL_FLAG(param, VAR_MARKED_HIDDEN))
        return true;

    // unchecked parameters in an exemplar frame may be PARAM!, but if they
    // are an ordinary FRAME! they will not be.  Review if better asserts are
    // needed here that make it worth passing in the context being checked.
    //
    return false;
}

inline static bool Is_Var_Hidden(const REBVAR *var)
{
    if (GET_CELL_FLAG(var, VAR_MARKED_HIDDEN))
        return true;

    // unchecked parameters in an exemplar frame may be PARAM!, but if they
    // are an ordinary FRAME! they will not be.  Review if better asserts are
    // needed here that make it worth passing in the context being checked.
    //
    return false;
}

inline static void Hide_Param(RELVAL *param) {
    UNUSED(param);
}

inline static void Seal_Param(RELVAL *param) {
    UNUSED(param);
}


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
inline static bool Is_Param_Sealed(const REBPAR *param) {
    UNUSED(param);
    return false;  // !!! temporary, needs to use cell flag
}


//=//// PARAMETER SYMBOL //////////////////////////////////////////////////=//
//
// Name should be NULL unless typeset in object keylist or func paramlist

inline static REBVAL *Init_Typeset(RELVAL *out, REBU64 bits)
{
    RESET_CELL(out, REB_TYPESET, CELL_MASK_NONE);
    VAL_PARAM_FLAGS(out) = FLAG_PARAM_CLASS_BYTE(REB_P_NORMAL);
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
inline static REBVAL *Init_Param_Core(
    RELVAL *out,
    REBFLGS param_flags,
    REBU64 bits
){
    RESET_VAL_HEADER(out, REB_TYPESET, CELL_MASK_NONE);

    VAL_PARAM_FLAGS(out) = param_flags;
    VAL_TYPESET_LOW_BITS(out) = bits & cast(uint32_t, 0xFFFFFFFF);
    VAL_TYPESET_HIGH_BITS(out) = bits >> 32;
    return cast(REBVAL*, out);
}

#define Init_Param(out,param_flags,bits) \
    Init_Param_Core(TRACK_CELL_IF_DEBUG(out), (param_flags), (bits))


inline static REBVAL *Refinify(REBVAL *v);  // forward declaration
inline static bool IS_REFINEMENT(const RELVAL *v);  // forward decl
inline static bool IS_PREDICATE(const RELVAL *v);  // forward decl


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
    const REBPAR *param,
    const RELVAL *v
){
    if (VAL_PARAM_CLASS(param) == REB_P_RETURN) {
        if (IS_BAD_WORD(v) and GET_CELL_FLAG(v, ISOTOPE)) {
            //
            // The strategy is that you can return any "mean" bad-word you
            // like from a function, as it's nearly as problematic as raising
            // a return type checking error.  This helps keep BAD-WORD! in the
            // spec for actually returning the "friendly" isotopes as values,
            // which would be things you want to support with ANY-TYPE!.  And
            // it means `return: []` works with `return ~none~`, which is the
            // default for RETURN with no arguments.
            //
            return true;
        }
    }

    if (VAL_PARAM_CLASS(param) == REB_P_OUTPUT) {
        //
        // !!! For the moment, output parameters don't actually check the
        // typeset for the value being written... they just check that you've
        // given a location to write.
        //
        const REBU64 ts_out =
            FLAGIT_KIND(REB_NULL)
            | FLAGIT_KIND(REB_ISSUE)  // for Is_Blackhole() use with SET
            | FLAGIT_KIND(REB_WORD)
            | FLAGIT_KIND(REB_PATH);
        return (ts_out & FLAGIT_KIND(VAL_TYPE(v))) != 0;
    }

    // We do an adjustment of the argument to accommodate meta parameters,
    // which check the unquoted type.  But what's built for the frame must be
    // quoted--e.g. MAKE FRAME! or specialized, since isotopes can't be passed
    // legally in frames.
    //
    enum Reb_Kind kind;

    if (VAL_PARAM_CLASS(param) == REB_P_META) {
        if (IS_BAD_WORD(v)) {
            assert(Is_Void(v) or NOT_CELL_FLAG(v, ISOTOPE));
            return true;  // all META parameters take BAD-WORD! isotopes
        }
        else if (IS_NULLED(v)) {
            kind = REB_NULL;  // meaningful to check <opt> for ^META
        }
        else {
            assert(IS_QUOTED(v));  // must be quoted otherwise
            if (VAL_NUM_QUOTES(v) > 1)
                kind = REB_QUOTED;
            else
                kind = CELL_KIND(VAL_UNESCAPED(v));
        }
    }
    else {
        kind = VAL_TYPE(v);
    }

    if (TYPE_CHECK(param, kind))
        return true;

    // !!! Predicates check more complex properties than just the kind, and
    // so will mess up on meta parameters.  All of this needs review, but
    // the main point is to realize that a frame built for a meta parameter
    // has already "leveled up" and removed isotope status and added quotes,
    // so the type checking must effectively unquote (if not actually do so,
    // which may be the easiest approach)

    if (
        GET_PARAM_FLAG(param, REFINEMENT)
        and kind == REB_PATH
        and IS_REFINEMENT(v)
    ){
        return true;
    }

    if (GET_PARAM_FLAG(param, PREDICATE) and IS_PREDICATE(v))
        return true;

    return false;
}


inline static bool Is_Typeset_Empty(REBCEL(const*) param) {
    assert(CELL_HEART(param) == REB_TYPESET);
    REBU64 bits = VAL_TYPESET_LOW_BITS(param);
    bits |= cast(REBU64, VAL_TYPESET_HIGH_BITS(param)) << 32;
    return (bits & TS_OPT_VALUE) == 0;  // e.g. `[/refine]`
}

// Forward definition needed...
//
inline static bool Is_Blackhole(const RELVAL *v);


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
inline static void Typecheck_Refinement(
    const REBPAR *param,
    REBVAL *arg
){
    assert(GET_PARAM_FLAG(param, REFINEMENT));

    if (IS_NULLED(arg)) {
        //
        // Not in use
    }
    else if (
        Is_Typeset_Empty(param)
        and VAL_PARAM_CLASS(param) != REB_P_OUTPUT
    ){
        if (not Is_Blackhole(arg))
            fail ("Parameterless Refinements Must be either # or NULL");
    }
    else if (not Typecheck_Including_Constraints(param, arg))
        fail (Error_Invalid_Type(VAL_TYPE(arg)));
}
