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
// of this category have another use in describing the parameters of function
// frames ("PARAMS").  When used for that purpose, they not only list the legal
// types...but also hold a byte indicating the parameter class (PARAM_CLASS)
// as well as flags describing other attributes of the parameter (if it is
// optional/refinement, or const, etc.)
//


//=//// TYPESET BITS //////////////////////////////////////////////////////=//
//
// Operations when typeset is done with a bitset (currently all typesets)


#define VAL_PARAMETER_CLASS_BYTE(v) \
    FIRST_BYTE(EXTRA(Parameter, (v)).param_flags)

#define mutable_VAL_PARAMETER_CLASS_BYTE(v) \
    mutable_FIRST_BYTE(EXTRA(Parameter, (v)).param_flags)

inline static option(Array(const*)) VAL_PARAMETER_ARRAY(
    noquote(Cell(const*)) v
){
    assert(HEART_BYTE(v) == REB_PARAMETER);

    Array(const*) a = ARR(VAL_NODE1(v));
    if (a != nullptr and GET_SERIES_FLAG(a, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return a;
}

#define INIT_VAL_PARAMETER_ARRAY(v, a) \
    INIT_VAL_NODE1((v), (a))


inline static bool TYPE_CHECK(Cell(const*) typeset, Value(const*) v) {
    return Typecheck_Value(typeset, SPECIFIED, v, SPECIFIED);
}

#define TYPE_CHECK_CORE(typeset,v,v_specifier) \
    Typecheck_Value((typeset), SPECIFIED, (v), (v_specifier))


// isotopic type matcher (e.g. used by FIND, SWITCH)

inline static bool Is_Matcher(Cell(const*) v) {
    if (QUOTE_BYTE(v) != ISOTOPE_0)
        return false;
    return ANY_TYPE_VALUE_KIND(HEART_BYTE(v));
}

inline static bool Matcher_Matches(
    Cell(const*) matcher,
    REBSPC *matcher_specifier,
    Cell(const*) v,
    REBSPC *v_specifier
){
    assert(Is_Matcher(matcher));
    DECLARE_LOCAL (plain);
    Derelativize(plain, matcher, matcher_specifier);
    mutable_QUOTE_BYTE(plain) = UNQUOTED_1;

    if (TYPE_CHECK_CORE(plain, v, v_specifier))
        return true;

    return false;
}


//=//// PARAMETER TYPESET PROPERTIES ///////////////////////////////////////=//

#define VAL_PARAM_FLAGS(v)           EXTRA(Parameter, (v)).param_flags
#define FLAG_PARAM_CLASS_BYTE(b)     FLAG_FIRST_BYTE(b)


// Endability is distinct from optional, and it means that a parameter is
// willing to accept being at the end of the input.  This means either
// an infix dispatch's left argument is missing (e.g. `do [+ 5]`) or an
// ordinary argument hit the end (e.g. the trick used for `>> help` when
// the arity is 1 usually as `>> help foo`)
//
// NULL is used to represent the end state in all parameter types.  In the
// case of quoted arguments, this is unambiguous--as there can be no nulls
// in the input array to quote.  In the meta parameter case it's also not
// ambiguous, as all other meta parameter types are either quoted or quasi.
// With normal parameters it will collide with if the parameter can take
// nulls... but we assume anyone bothered by that would switch to using a
// meta parameter.
//
// When used on a `return:` parameter, this means invisibility is legal.
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

// Parameters can be marked such that if they are void, the action will not
// be run at all.  This is done via the `<maybe>` annotation.  The action
// will have its frame fulfilled, but not run.
//
#define PARAM_FLAG_NOOP_IF_VOID \
    FLAG_LEFT_BIT(13)

#define PARAM_FLAG_14 \
    FLAG_LEFT_BIT(14)

#define PARAM_FLAG_CONST \
    FLAG_LEFT_BIT(15)

#define PARAM_FLAG_VANISHABLE \
    FLAG_LEFT_BIT(16)

#define PARAM_FLAG_RETURN_NONE \
    FLAG_LEFT_BIT(17)

#define PARAM_FLAG_RETURN_VOID \
    FLAG_LEFT_BIT(18)

#define PARAM_FLAG_RETURN_TYPECHECKED \
    FLAG_LEFT_BIT(19)

#define PARAM_FLAG_WANT_RAISED \
    FLAG_LEFT_BIT(20)

#define PARAM_FLAG_21 \
    FLAG_LEFT_BIT(21)

#define PARAM_FLAG_22 \
    FLAG_LEFT_BIT(22)

#define PARAM_FLAG_WANT_PACKS \
    FLAG_LEFT_BIT(23)


#define SET_PARAM_FLAG(v,name) \
    (VAL_PARAM_FLAGS(v) |= PARAM_FLAG_##name)

#define GET_PARAM_FLAG(v,name) \
    ((VAL_PARAM_FLAGS(v) & PARAM_FLAG_##name) != 0)

#define CLEAR_PARAM_FLAG(v,name) \
    (VAL_PARAM_FLAGS(v) &= ~PARAM_FLAG_##name)

#define NOT_PARAM_FLAG(v,name) \
    ((VAL_PARAM_FLAGS(v) & PARAM_FLAG_##name) == 0)



inline static enum Reb_Param_Class VAL_PARAM_CLASS(const REBPAR *param) {
    assert(IS_PARAMETER(param));
    enum Reb_Param_Class pclass = cast(enum Reb_Param_Class,
        VAL_PARAMETER_CLASS_BYTE(param)
    );
    if (pclass == PARAM_CLASS_RETURN)
        assert(NOT_PARAM_FLAG(param, REFINEMENT));
    return pclass;
}


// A parameter can be any value (including isotopes) if it is specialized.
// But a typeset that does not have param class 0 is unspecialized.
//
inline static bool Is_Specialized(const REBPAR *param) {
    if (
        HEART_BYTE_UNCHECKED(param) == REB_PARAMETER  // no assert on isotope
        and VAL_PARAM_CLASS(param) != PARAM_CLASS_0  // non-parameter typeset
    ){
        assert(QUOTE_BYTE(param) == UNQUOTED_1);  // no quoted parameters
        if (Get_Cell_Flag(param, VAR_MARKED_HIDDEN))
            assert(!"Unspecialized parameter is marked hidden!");
        return false;
    }
    return true;
}

// Parameter class should be PARAM_CLASS_0 unless typeset in func paramlist.

inline static REBVAL *Init_Parameter_Core(Cell(*) out, Array(const*) array)
{
    Reset_Unquoted_Header_Untracked(out, CELL_MASK_PARAMETER);
    if (array)
        ASSERT_SERIES_MANAGED(array);
    INIT_VAL_PARAMETER_ARRAY(out, array);
    VAL_PARAM_FLAGS(out) = FLAG_PARAM_CLASS_BYTE(PARAM_CLASS_0);
    return cast(REBVAL*, out);
}

#define Init_Parameter(out,bits) \
    TRACK(Init_Parameter_Core((out), (bits)))


inline static REBPAR *Init_Param_Core(
    Cell(*) out,
    Flags param_flags,
    Array(const*) array
){
    Reset_Unquoted_Header_Untracked(out, CELL_MASK_PARAMETER);
    if (array)
        ASSERT_SERIES_MANAGED(array);

    VAL_PARAM_FLAGS(out) = param_flags;
    INIT_VAL_PARAMETER_ARRAY(out, array);

    REBPAR *param = cast(REBPAR*, cast(REBVAL*, out));
    assert(VAL_PARAM_CLASS(param) != PARAM_CLASS_0);  // must set

    assert(Not_Cell_Flag(param, VAR_MARKED_HIDDEN));
    return param;
}

#define Init_Param(out,param_flags,bits) \
    TRACK(Init_Param_Core((out), (param_flags), (bits)))


inline static REBVAL *Refinify(REBVAL *v);  // forward declaration
inline static bool IS_REFINEMENT(Cell(const*) v);  // forward decl
inline static bool IS_PREDICATE(Cell(const*) v);  // forward decl

// This is an interim workaround for the need to be able check constrained
// data types (e.g. PATH!-with-BLANK!-at-head being REFINEMENT!).  See
// Startup_Fake_Type_Constraint() for an explanation.
//
inline static bool Typecheck_Including_Constraints(
    const REBPAR *param,
    Value(*) v  // need mutability for ^META check
){
    if (VAL_PARAM_CLASS(param) == PARAM_CLASS_RETURN) {
        if (Is_Pack(v))
            return true;  // For now, assume multi-return typechecked it
        if (Is_Raised(v))
            return true;  // For now, all functions return definitional errors
    }

    // We do an adjustment of the argument to accommodate meta parameters,
    // which check the unquoted type.
    //
    bool unquoted = false;

    if (VAL_PARAM_CLASS(param) == PARAM_CLASS_META) {
        if (Is_Nulled(v))
            return GET_PARAM_FLAG(param, ENDABLE);

        if (not IS_QUASI(v) and not IS_QUOTED(v))
            return false;

        Meta_Unquotify_Undecayed(v);  // temporary adjustment (easiest option)
        unquoted = true;
    }
    else
        unquoted = false;

    if (TYPE_CHECK(param, v))
        goto return_true;

    if (Is_Nihil(v) and GET_PARAM_FLAG(param, VANISHABLE))
        goto return_true;

    // !!! Predicates check more complex properties than just the kind, and
    // so will mess up on meta parameters.  All of this needs review, but
    // the main point is to realize that a frame built for a meta parameter
    // has already "leveled up" and removed isotope status and added quotes,
    // so the type checking must effectively unquote (if not actually do so,
    // which may be the easiest approach)

    if (
        GET_PARAM_FLAG(param, REFINEMENT)
        and IS_PATH(v)
        and IS_REFINEMENT(v)
    ){
        goto return_true;
    }

    if (GET_PARAM_FLAG(param, PREDICATE) and IS_PREDICATE(v))
        goto return_true;

    if (unquoted)
        Meta_Quotify(v);

    return false;

  return_true:

    if (unquoted)
        Meta_Quotify(v);

    return true;
}


inline static bool Is_Parameter_Unconstrained(noquote(Cell(const*)) param) {
    return VAL_PARAMETER_ARRAY(param) == nullptr;  // e.g. `[/refine]`
}

inline static bool Is_Blackhole(Cell(const*) v);  // forward decl

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
    const REBKEY *key,
    const REBPAR *param,
    REBVAL *arg
){
    assert(
        GET_PARAM_FLAG(param, REFINEMENT)
        or GET_PARAM_FLAG(param, SKIPPABLE)
    );

    if (Is_Nulled(arg))  // not in use
        return;

    if (
        Is_Parameter_Unconstrained(param)
        and VAL_PARAM_CLASS(param) != PARAM_CLASS_OUTPUT
    ){
        if (not Is_Blackhole(arg))
            fail (Error_Bad_Argless_Refine(key));

        return;
    }

  typecheck_again:

    if (not Typecheck_Including_Constraints(param, arg)) {
        if (Is_Activation(arg)) {
            Deactivate_If_Activation(arg);
            goto typecheck_again;
        }
        fail (Error_Invalid_Type(VAL_TYPE(arg)));
    }
}
