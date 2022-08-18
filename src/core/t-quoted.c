//
//  File: %t-quoted.c
//  Summary: "QUOTED! datatype that acts as container for ANY-VALUE!"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2021 Ren-C Open Source Contributors
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
// new features.  THE takes the place of the former literalizing operator,
// and JUST will be literalizing but add a quoting level.
//
//    >> quote 1 + 2  ; now evaluative, adds a quoting level
//    == '3
//
//    >> the a  ; acts like Rebol2 QUOTE
//    == a
//
//    >> the 'a
//    == 'a
//
//    >> just a
//    == 'a
//

#include "sys-core.h"

//
//  CT_Quoted: C
//
// !!! Currently, in order to have a GENERIC dispatcher (e.g. REBTYPE())
// then one also must implement a comparison function.  However, compare
// functions specifically take noquote cells, so you can't pass REB_QUOTED to
// them.  The handling for QUOTED! is in the comparison dispatch itself.
//
REBINT CT_Quoted(noquote(Cell(const*)) a, noquote(Cell(const*)) b, bool strict)
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
Bounce MAKE_Quoted(
    Frame(*) frame_,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_QUOTED);
    if (parent)
        return FAIL(Error_Bad_Make_Parent(kind, unwrap(parent)));

    return Quotify(Copy_Cell(OUT, arg), 1);
}


//
//  TO_Quoted: C
//
// TO is disallowed at the moment, as there is no clear equivalence of things
// "to" a literal.  (to quoted! [[a]] => \\a, for instance?)
//
Bounce TO_Quoted(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *data) {
    return FAIL(Error_Bad_Make(kind, data));
}


//
//  REBTYPE: C
//
// It was for a time considered whether generics should be willing to operate
// on QUOTED!.  e.g. "do whatever the non-quoted version would do, then add
// the quotedness onto the result".
//
//     >> add (the '''1) 2
//     == '''3
//
//     >> first the '[a b c]
//     == a
//
// While a bit outlandish for ADD, it might seem to make more sense for FIND
// and SELECT when you have a QUOTED! block or GROUP!.  However, the solution
// that emerged after trying other options was to make REQUOTE:
//
// https://forum.rebol.info/t/1035
//
// So the number of things supported by QUOTED is limited to COPY at this time.
//
REBTYPE(Quoted)
{
    // Note: SYM_REFLECT is handled directly in the REFLECT native
    //
    switch (ID_OF_SYMBOL(verb)) {
      case SYM_COPY: {  // D_ARG(1) skips RETURN in first arg slot
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(REF(part));
        UNUSED(REF(deep));
        UNUSED(REF(types));

        REBLEN num_quotes = Dequotify(ARG(value));
        bool threw = Run_Generic_Dispatch_Throws(ARG(value), frame_, verb);
        assert(not threw);  // can't throw
        UNUSED(threw);

        return Quotify(OUT, num_quotes); }

      default:
        break;
    }

    fail ("QUOTED! has no GENERIC operations (use NOQUOTE/REQUOTE)");
}


//
//  the: native [
//
//  "Returns value passed in without evaluation"
//
//      return: "Input value, verbatim--unless /SOFT and soft quoted type"
//          [<opt> any-value!]
//      'value [any-value!]
//      /soft "Evaluate if a GET-GROUP!, GET-WORD!, or GET-PATH!"
//  ]
//
DECLARE_NATIVE(the)
//
// Note: THE is not a perfect synonym for the action assigned to @.  See notes
// on THE* for why.
{
    INCLUDE_PARAMS_OF_THE;

    REBVAL *v = ARG(value);

    if (REF(soft) and ANY_ESCAPABLE_GET(v)) {
        if (Eval_Value_Throws(OUT, v, SPECIFIED))
            return THROWN;
        return OUT;  // Don't set UNEVALUATED flag
    }

    Copy_Cell(OUT, v);

    Set_Cell_Flag(OUT, UNEVALUATED);
    return OUT;
}


//
//  the*: native [
//
//  "Returns value passed in without evaluation, BUT ~null~ becomes pure NULL"
//
//      return: "Input value, verbatim--unless /SOFT and soft quoted type"
//          [<opt> any-value!]
//      'value "Does not allow BAD-WORD! arguments except for ~null~"
//          [any-value!]
//  ]
//
DECLARE_NATIVE(the_p)
//
// THE* is the variant assigned to @.  It does not let you use it with
// BAD-WORD!, except for ~null~, which is transitioned to true NULL.
//
//     >> @ ~null~
//     ; null
//
// This is done as a convenience for the API so people can write:
//
//     rebElide("append block try @", value_might_be_null);
//
// ...instead of:
//
//     rebElide("append block try", rebQ(value_might_be_null));
//
// Because the API machinery will put a plain `~null~` into the stream as
// a surrogate for a NULL instead of asserting/erroring.  If you know that
// what you are dealing with might be a BAD-WORD!, then you should use
// rebQ() instead...
{
    INCLUDE_PARAMS_OF_THE_P;

    REBVAL *v = ARG(value);

    if (Is_Quasi_Word(v)) {
        if (VAL_WORD_ID(v) == SYM_NULL)
            Init_Nulled(OUT);
        else
            fail ("@ and THE* only accept BAD-WORD! of ~NULL~ to make NULL");
    }
    else
        Copy_Cell(OUT, v);

    Set_Cell_Flag(OUT, UNEVALUATED);
    return OUT;
}


//
//  just*: native [  ; deprecate temporarily due to isotopic block methodoloy
//
//  "Returns quoted eversion of value passed in without evaluation"
//
//      return: "Input value, verbatim--unless /SOFT and soft quoted type"
//          [<opt> any-value!]
//      'value [any-value!]
//      /soft "Evaluate if a GET-GROUP!, GET-WORD!, or GET-PATH!"
//  ]
//
DECLARE_NATIVE(just_p)
//
// Note: This could be defined as `chain [:the :quote]`.  However, it can be
// needed early in the boot (before REDESCRIBE is available), and it is also
// something that needs to perform well due to common use.  Having it be its
// own native is probably worthwhile.
{
    INCLUDE_PARAMS_OF_JUST_P;

    REBVAL *v = ARG(value);

    if (REF(soft) and ANY_ESCAPABLE_GET(v)) {
        if (Eval_Value_Throws(OUT, v, SPECIFIED))
            return THROWN;
        return Quotify(OUT, 1);  // Don't set UNEVALUATED flag
    }

    Copy_Cell(OUT, v);
    Set_Cell_Flag(OUT, UNEVALUATED);  // !!! should this bit be set?
    return Quotify(OUT, 1);
}


//
//  quote: native [
//
//  {Constructs a quoted form of the evaluated argument}
//
//      return: "Quoted value (if depth = 0, may not be quoted)"
//          [<opt> any-value!]
//      optional [<opt> any-value!]
//      /depth "Number of quoting levels to apply (default 1)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(quote)
{
    INCLUDE_PARAMS_OF_QUOTE;

    REBINT depth = REF(depth) ? VAL_INT32(ARG(depth)) : 1;

    if (depth == 0)
        return COPY(ARG(optional));

    if (depth < 0)
        fail (PAR(depth));

    Copy_Cell(OUT, ARG(optional));
    return Quotify(OUT, depth);
}


//
//  meta: native [
//
//  {VOID -> NULL, isotopes -> QUASI!, adds a quote to rest (behavior of ^^)}
//
//      return: [<opt> quoted! quasi!]
//      ^optional [<void> <opt> <fail> any-value!]
//  ]
//
DECLARE_NATIVE(meta)
{
    INCLUDE_PARAMS_OF_META;

    REBVAL *v = ARG(optional);

    return COPY(v);  // argument was already ^META, no need to Meta_Quotify()
}


//
//  meta*: native [
//
//  {META variant that passes through VOID and NULL, and doesn't take failures}
//
//      return: [<opt> <void> quoted! quasi!]
//      ^optional [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(meta_p)
{
    INCLUDE_PARAMS_OF_META_P;

    REBVAL *v = ARG(optional);

    if (Is_Meta_Of_Void(v))
        return VOID;

    if (Is_Meta_Of_Null(v))
        return nullptr;

    return COPY(v);  // argument was ^META, so no need to Meta_Quotify()
}


//
//  unquote: native [
//
//  {Remove quoting levels from the evaluated argument}
//
//      return: "Value with quotes removed"
//          [<opt> any-value!]
//      value "Any value allowed in case /DEPTH is 0"
//          [<opt> any-value!]
//      /depth "Number of quoting levels to remove (default 1)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(unquote)
{
    INCLUDE_PARAMS_OF_UNQUOTE;

    REBVAL *v = ARG(value);

    REBINT depth = (REF(depth) ? VAL_INT32(ARG(depth)) : 1);

    if (depth < 0)
        fail (PAR(depth));

    if (cast(REBLEN, depth) > VAL_NUM_QUOTES(v))
        fail ("Value not quoted enough for unquote depth requested");

    Unquotify(Copy_Cell(OUT, v), depth);
    return OUT;
}


//
//  quasi: native [
//
//  {Constructs a quasi form of the evaluated argument}
//
//      return: [quasi!]
//      value "Any non-QUOTED! value"
//          [any-value!]  ; there isn't an any-nonquoted! typeset
//  ]
//
DECLARE_NATIVE(quasi)
{
    INCLUDE_PARAMS_OF_QUASI;

    Value(*) v = ARG(value);

    if (IS_QUOTED(v))
        fail ("QUOTED! values do not have QUASI! forms");

    Copy_Cell(OUT, v);
    return Quasify(OUT);
}


//
//  unquasi: native [
//
//  {Remove QUASI! wrapper from the argument}
//
//      return: "Value with quasi state removed"
//          [<opt> any-value!]
//      value [quasi!]
//  ]
//
DECLARE_NATIVE(unquasi)
{
    INCLUDE_PARAMS_OF_UNQUASI;

    REBVAL *v = ARG(value);
    Unquasify(Copy_Cell(OUT, v));
    return OUT;
}


//
//  unmeta: native [
//
//  {Variant of UNQUOTE that also accepts QUASI! to make isotopes}
//
//      return: [<opt> <void> any-value!]
//      value [<opt> quoted! quasi!]
//  ]
//
DECLARE_NATIVE(unmeta)
{
    INCLUDE_PARAMS_OF_UNMETA;

    REBVAL *v = ARG(value);

    return UNMETA(v);
}


//
//  unmeta*: native [
//
//  {Variant of UNMETA that passes thru VOID and NULL}
//
//      return: [<opt> <void> any-value!]
//      ^value [<opt> <void> quoted! quasi!]
//  ]
//
DECLARE_NATIVE(unmeta_p)
{
    INCLUDE_PARAMS_OF_UNMETA_P;

    REBVAL *v = ARG(value);

    if (Is_Meta_Of_Void(v))
        return VOID;

    if (Is_Meta_Of_Null(v))
        return nullptr;

    if (IS_QUASI(v)) {
        Isotopify(v);
        fail (Error_Bad_Isotope(v));  // isotopes not allowed as input
    }

    assert(IS_QUOTED(v));  // handling the invisibility detour is done now...
    Unquotify(v, 1);  // drop quote level caused by ^META parameter convention

    // Now remove the level of meta the user was asking for.
    //
    return UNMETA(v);
}


//
//  unget: native [
//
//  {Interim tool for emulating future GET-WORD!/GET-TUPLE! semantics}
//
//      return: [<opt> <void> any-value!]
//      'var "Quoted for convenience"
//          [word! tuple!]
//  ]
//
DECLARE_NATIVE(unget)
{
    INCLUDE_PARAMS_OF_UNGET;

    if (Get_Var_Core_Throws(SPARE, GROUPS_OK, ARG(var), SPECIFIED))
        return THROWN;

    return UNMETA(SPARE);
}


//
//  spread: native [
//
//  {Make block arguments splice}
//
//      return: "Isotope of BLOCK! or unquoted value"
//          [<opt> any-value!]
//      array [<opt> quoted! any-array!]
//  ]
//
DECLARE_NATIVE(spread)
//
// !!! The name SPREAD is being chosen because it is more uncommon than splice,
// and there is no particular contention for its design.  SPLICE may be a more
// complex operation.
{
    INCLUDE_PARAMS_OF_SPREAD;

    REBVAL *v = ARG(array);

    if (Is_Nulled(v))
        return nullptr;  // Put TRY on the APPEND or whatever, not SPREAD

    if (IS_QUOTED(v))
        return Unquotify(Copy_Cell(OUT, v), 1);

    return Splicify(Copy_Cell(OUT, v));
}


//
//  splice?: native [
//
//  "Tells you if argument is a splice (isotopic block)"
//
//      return: [logic!]
//      ^optional [<opt> <void> <fail> any-value!]
//  ]
//
DECLARE_NATIVE(splice_q)
{
    INCLUDE_PARAMS_OF_SPLICE_Q;

    return Init_Logic(OUT, Is_Meta_Of_Splice(ARG(optional)));
}


//
//  maybe: native [
//
//  {If argument is null or none, make it void (also pass through voids)}
//
//      return: "Value (if it's anything other than the states being checked)"
//          [<opt> <void> any-value!]
//      ^optional [<opt> <void> <fail> any-value!]
//  ]
//
DECLARE_NATIVE(maybe)
{
    INCLUDE_PARAMS_OF_MAYBE;

    REBVAL *v = ARG(optional);

    if (
        Is_Meta_Of_Void(v)
        or Is_Meta_Of_Null(v) or Is_Meta_Of_Null_Isotope(v)
        or Is_Meta_Of_None(v)
    ){
        return VOID;
    }

    if (Is_Meta_Of_Failure(v)) {  // fold in TRY behavior as well
        ERROR_VARS *vars = ERR_VARS(VAL_CONTEXT(v));
        if (
            IS_WORD(&vars->id)
            and VAL_WORD_ID(&vars->id) == SYM_TRY_IF_NULL_MEANT
        ){
            return VOID;
        }
        return FAIL(VAL_CONTEXT(v));
    }

    Move_Cell(OUT, v);
    Meta_Unquotify(OUT);
    Decay_If_Isotope(OUT);
    return OUT;
}


//
//  maybe+: native [
//
//  {Special Test: Potential future of a MAYBE Intrinsic}
//
//      return: "Value (if it's anything other than void)"
//          [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(maybe_a)
//
// !!! One aspect of the implementation of translucency is that functions like
// IF do not actually overwrite the output cell when they don't run their
// branch (nor WHILE if they don't run their body, etc.)
//
// Something interesting about that is that we could implement MAYBE as
// writing its argument directly onto its output, and looking for if it was
// stale or not.  This implements that experiment.
{
    INCLUDE_PARAMS_OF_MAYBE_A;

    if (Eval_Step_In_Subframe_Throws(
        OUT,
        frame_,
        FRAME_FLAG_MAYBE_STALE
            | EVAL_EXECUTOR_FLAG_SINGLE_STEP
    )){
        return THROWN;
    }

    if (Is_Stale(OUT))
        return VOID;

    return OUT;
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
DECLARE_NATIVE(quoted_q)
{
    INCLUDE_PARAMS_OF_QUOTED_Q;

    return Init_Logic(OUT, VAL_TYPE(ARG(optional)) == REB_QUOTED);
}


//
//  quasi?: native [
//
//  {Tells you if the argument is QUASI! or not}
//
//      return: [logic!]
//      optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(quasi_q)
{
    INCLUDE_PARAMS_OF_QUASI_Q;

    return Init_Logic(OUT, VAL_TYPE(ARG(optional)) == REB_QUASI);
}


//
//  noquote: native [
//
//  {Removes all levels of quoting from a quoted value}
//
//      return: [<opt> any-value!]
//      optional [<opt> any-value!]
//  ]
//
DECLARE_NATIVE(noquote)
{
    INCLUDE_PARAMS_OF_NOQUOTE;

    REBVAL *v = ARG(optional);
    Unquotify(v, VAL_NUM_QUOTES(v));
    return COPY(v);
}
