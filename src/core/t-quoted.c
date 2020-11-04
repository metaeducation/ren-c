//
//  File: %t-quoted.c
//  Summary: "QUOTED! datatype that acts as container for ANY-VALUE!"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// In historical Rebol, a WORD! and PATH! had variants which were "LIT" types.
// e.g. FOO was a word, while 'FOO was a LIT-WORD!.  The evaluator behavior
// was that the literalness would be removed, leaving a WORD! or PATH! behind,
// making it suitable for comparisons (e.g. `word = 'foo`)
//
// Ren-C has a generic QUOTED! datatype, a container which can be arbitrarily
// deep in escaping.  This faciliated a more succinct way to QUOTE, as well as
// new features.  It also cleared up a naming issue (1 is a "literal integer",
// not `'1`).  They are "quoted", while LITERAL and LIT take the place of the
// former QUOTE operator (e.g. `lit 1` => `1`).
//

#include "sys-core.h"

//
//  CT_Quoted: C
//
// !!! Currently, in order to have a GENERIC dispatcher (e.g. REBTYPE())
// then one also must implement a comparison function.  However, compare
// functions specifically take REBCEL, so you can't pass REB_QUOTED to them.
// The handling for QUOTED! is in the comparison dispatch itself.
//
REBINT CT_Quoted(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(a); UNUSED(b); UNUSED(strict);
    assert(!"CT_Quoted should never be called");
    return 0;
}


//
//  MAKE_Quoted: C
//
// !!! This can be done with QUOTE (currently EVAL) which has the ability
// to take a refinement of how deep.  Having a MAKE variant may be good or
// may not be good; if it were to do a level more than 1 it would need to
// take a BLOCK! with an INTEGER! and the value.  :-/
//
REB_R MAKE_Quoted(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_QUOTED);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    return Quotify(Move_Value(out, arg), 1);
}


//
//  TO_Quoted: C
//
// TO is disallowed at the moment, as there is no clear equivalence of things
// "to" a literal.  (to quoted! [[a]] => \\a, for instance?)
//
REB_R TO_Quoted(REBVAL *out, enum Reb_Kind kind, const REBVAL *data) {
    UNUSED(out);
    fail (Error_Bad_Make(kind, data));
}


//
//  PD_Quoted: C
//
// Historically you could ask a LIT-PATH! questions like its length/etc, just
// like any other path.  So it seems types wrapped in QUOTED! should respond
// more or less like their non-quoted counterparts...
//
//     >> first lit '[a b c]
//     == a
//
// !!! It might be interesting if the answer were 'a instead, adding on a
// level of quotedness that matched the argument...and if arguments had to be
// quoted in order to go the reverse and had the quote levels taken off.
// That would need strong evidence of being useful, however.
//
REB_R PD_Quoted(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
){
    UNUSED(picker);
    UNUSED(opt_setval);

    if (KIND3Q_BYTE(pvs->out) == REB_QUOTED)
        Move_Value(pvs->out, VAL_QUOTED_PAYLOAD_CELL(pvs->out));
    else {
        assert(KIND3Q_BYTE(pvs->out) >= REB_MAX);
        mutable_KIND3Q_BYTE(pvs->out) %= REB_64;
        assert(
            mutable_HEART_BYTE(pvs->out)
            == mutable_KIND3Q_BYTE(pvs->out)
        );
    }

    // We go through a dispatcher here and use R_REDO_UNCHECKED here because
    // it avoids having to pay for the check of literal types in the general
    // case--the cost is factored in the dispatch.

    return R_REDO_UNCHECKED;
}


//
//  REBTYPE: C
//
// It was for a time considered whether generics should be willing to operate
// on QUOTED!.  e.g. "do whatever the non-quoted version would do, then add
// the quotedness onto the result".
//
//     >> add lit '''1 2
//     == '''3
//
// While a bit outlandish for ADD, it might seem to make more sense for FIND
// and SELECT when you have a QUOTED! block or GROUP!.  However, the solution
// that emerged after trying other options was to make REQUOTE:
//
// https://forum.rebol.info/t/1035
//
// So the number of things supported by QUOTED is limited to COPY.
//
REBTYPE(Quoted)
{
    // Note: SYM_REFLECT is handled directly in the REFLECT native
    //
    switch (VAL_WORD_SYM(verb)) {
      case SYM_COPY: {  // D_ARG(1) skips RETURN in first arg slot
        REBLEN num_quotes = Dequotify(D_ARG(1));
        REB_R r = Run_Generic_Dispatch(D_ARG(1), frame_, verb);
        assert(r != R_THROWN);  // can't throw
        if (r == nullptr)
            r = Init_Nulled(FRM_OUT(frame_));
        return Quotify(r, num_quotes); }

      default:
        break;
    } 

    fail ("QUOTED! has no GENERIC operations (use DEQUOTE/REQUOTE)");
}


//
//  literal: native/body [
//
//  "Returns value passed in without evaluation"
//
//      return: {The input value, verbatim--unless /SOFT and soft quoted type}
//          [<opt> any-value!]
//      :value {Value to quote, <opt> is impossible (see UNEVAL)}
//          [any-value!]
//      /soft {Evaluate if a GROUP!, GET-WORD!, or GET-PATH!}
//  ][
//      if soft and [match [group! get-word! get-path!] :value] [
//          reeval value
//      ] else [
//          :value  ; also sets unevaluated bit, how could a user do so?
//      ]
//  ]
//
REBNATIVE(literal) // aliased in %base-defs.r as LIT
{
    INCLUDE_PARAMS_OF_LITERAL;

    REBVAL *v = ARG(value);

    if (REF(soft) and IS_QUOTABLY_SOFT(v))
        fail ("LITERAL/SOFT not currently implemented, should clone EVAL");

    Move_Value(D_OUT, v);
    SET_CELL_FLAG(D_OUT, UNEVALUATED);
    return D_OUT;
}


//
//  quote: native [
//
//  {Constructs a quoted form of the evaluated argument}
//
//      return: [quoted!]
//      optional [<opt> any-value!]
//      /depth "Number of quoting levels to apply (default 1)"
//          [integer!]
//  ]
//
REBNATIVE(quote)
{
    INCLUDE_PARAMS_OF_QUOTE;

    REBINT depth = REF(depth) ? VAL_INT32(ARG(depth)) : 1;
    if (depth < 0)
        fail (PAR(depth));

    return Quotify(Move_Value(D_OUT, ARG(optional)), depth);
}


//
//  unquote: native [
//
//  {Remove quoting levels from the evaluated argument}
//
//      return: "Value with quotes removed (NULL is passed through as NULL)"
//          [<opt> any-value!]
//      value [<opt> any-value!]
//      /depth "Number of quoting levels to remove (default 1)"
//          [integer!]
//  ]
//
REBNATIVE(unquote)
{
    INCLUDE_PARAMS_OF_UNQUOTE;

    REBVAL *v = ARG(value);

    if (IS_NULLED(v))
        return nullptr;  // It's more convenient to allow NULL than not

    REBINT depth = REF(depth) ? VAL_INT32(ARG(depth)) : 1;
    if (depth < 0)
        fail (PAR(depth));
    if (cast(REBLEN, depth) > VAL_NUM_QUOTES(v))
        fail ("Value not quoted enough for unquote depth requested");

    return Unquotify(Move_Value(D_OUT, v), depth);
}


//
//  quoted?: native [
//
//  {Tells you if the argument is QUOTED! or not}
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(quoted_q)
{
    INCLUDE_PARAMS_OF_QUOTED_Q;

    return Init_Logic(D_OUT, VAL_TYPE(ARG(optional)) == REB_QUOTED);
}


//
//  dequote: native [
//
//  {Removes all levels of quoting from a quoted value}
//
//      return: [<opt> any-value!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(dequote)
{
    INCLUDE_PARAMS_OF_DEQUOTE;

    REBVAL *v = ARG(optional);
    Unquotify(v, VAL_NUM_QUOTES(v));
    RETURN (v);
}
