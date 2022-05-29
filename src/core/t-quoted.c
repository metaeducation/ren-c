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
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_QUOTED);
    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap(parent)));

    return Quotify(Copy_Cell(out, arg), 1);
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

        return Quotify(D_OUT, num_quotes); }

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
REBNATIVE(the)
//
// Note: THE is not a perfect synonym for the action assigned to @.  See notes
// on THE* for why.
{
    INCLUDE_PARAMS_OF_THE;

    REBVAL *v = ARG(value);

    if (REF(soft) and ANY_ESCAPABLE_GET(v)) {
        if (Eval_Value_Throws(D_OUT, v, SPECIFIED))
            return_thrown (D_OUT);
        return D_OUT;  // Don't set UNEVALUATED flag
    }

    Copy_Cell(D_OUT, v);

    SET_CELL_FLAG(D_OUT, UNEVALUATED);
    return D_OUT;
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
REBNATIVE(the_p)
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

    if (IS_BAD_WORD(v)) {
        assert(NOT_CELL_FLAG(v, ISOTOPE));
        if (VAL_BAD_WORD_ID(v) == SYM_NULL)
            Init_Nulled(D_OUT);
        else
            fail ("@ and THE* only accept BAD-WORD! of ~NULL~ to make NULL");
    }
    else
        Copy_Cell(D_OUT, v);

    SET_CELL_FLAG(D_OUT, UNEVALUATED);
    return D_OUT;
}


//
//  just: native [
//
//  "Returns quoted eversion of value passed in without evaluation"
//
//      return: "Input value, verbatim--unless /SOFT and soft quoted type"
//          [<opt> any-value!]
//      'value [any-value!]
//      /soft "Evaluate if a GET-GROUP!, GET-WORD!, or GET-PATH!"
//  ]
//
REBNATIVE(just)
//
// Note: This could be defined as `chain [:the | :quote]`.  However, it can be
// needed early in the boot (before REDESCRIBE is available), and it is also
// something that needs to perform well due to common use.  Having it be its
// own native is probably worthwhile.
{
    INCLUDE_PARAMS_OF_THE;

    REBVAL *v = ARG(value);

    if (REF(soft) and ANY_ESCAPABLE_GET(v)) {
        if (Eval_Value_Throws(D_OUT, v, SPECIFIED))
            return_thrown (D_OUT);
        return Quotify(D_OUT, 1);  // Don't set UNEVALUATED flag
    }

    Copy_Cell(D_OUT, v);
    SET_CELL_FLAG(D_OUT, UNEVALUATED);  // !!! should this bit be set?
    return Quotify(D_OUT, 1);
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
REBNATIVE(quote)
{
    INCLUDE_PARAMS_OF_QUOTE;

    REBINT depth = REF(depth) ? VAL_INT32(ARG(depth)) : 1;

    if (depth == 0)
        return ARG(optional);

    if (depth < 0)
        fail (PAR(depth));

    Copy_Cell(D_OUT, ARG(optional));
    return Isotopic_Quotify(D_OUT, depth);
}


//
//  meta: native [
//
//  {Turns BAD-WORD! isotopes into plain BAD-WORD!, ignores NULL, quotes rest}
//
//      return: "Will be a ~void~ isotope if input was <end>"
//          [<opt> any-value!]
//      ^optional [<opt> <end> any-value!]
//  ]
//
REBNATIVE(meta)
{
    INCLUDE_PARAMS_OF_META;

    return ARG(optional);  // argument was already ^meta
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
REBNATIVE(unquote)
{
    INCLUDE_PARAMS_OF_UNQUOTE;

    REBVAL *v = ARG(value);

    REBINT depth = (REF(depth) ? VAL_INT32(ARG(depth)) : 1);

    if (depth < 0)
        fail (PAR(depth));

    if (cast(REBLEN, depth) > VAL_NUM_QUOTES(v))
        fail ("Value not quoted enough for unquote depth requested");

    Unquotify(Copy_Cell(D_OUT, v), depth);
    return D_OUT;
}


//
//  unmeta: native [
//
//  {Variant of UNQUOTE that accepts BAD-WORD! and makes isotopes}
//
//      return: "Potentially an isotope"
//          [<opt> <invisible> any-value!]
//      ^value [<opt> bad-word! quoted!]
//      /void "Invisible if input is ~void~ BAD-WORD! (else ~none~ isotope)"
//  ]
//
REBNATIVE(unmeta)
//
// Note: Taking ^meta parameters allows `unquote ~meanie~` e.g. on what
// would usually be an error-inducing stable bad word.  This was introduced as
// a way around a situation like this:
//
//     result: ^(some expression)  ; NULL -> NULL, ~null~ isotope => ~null~
//     do compose [
//         detect-isotope unmeta (
//              match bad-word! result else [
//                  quote result  ; NULL -> ' and ' -> ''
//              ]
//          )
//     ]
//
// DETECT-ISOTOPE wants to avoid forcing its caller to use a quoted argument
// calling convention.  Yet it still wants to know if its argument is a ~null~
// isotope vs/ NULL, or a BAD-WORD! vs. an isotope BAD-WORD!.  That's what
// ^meta arguments are for...but it runs up against a wall when forced to
// run from code hardened into a BLOCK!.
//
// This could go another route with an added operation, something along the
// lines of `unmeta make-friendly ~meanie~`.  But given that the output from
// an unmeta on a plain BAD-WORD! will be mean regardless of the input makes
// it superfluous...the UNMETA doesn't have any side effects to worry about,
// and if the output is just going to be mean again it's not somehow harmful
// to understanding.
//
// (It's also not clear offering a MAKE-FRIENDLY operation is a good idea.)
{
    INCLUDE_PARAMS_OF_UNMETA;

    REBVAL *v = ARG(value);

    if (IS_NULLED(v))
        return v;  // ^(null) => null, so the reverse must be true

    if (IS_BAD_WORD(v)) {
        if (GET_CELL_FLAG(v, ISOTOPE))
            fail ("Cannot UNMETA end of input");  // no <end>, shouldn't happen
        Move_Cell(D_OUT, v);
        SET_CELL_FLAG(D_OUT, ISOTOPE);
        return D_OUT;
    }

    assert(IS_QUOTED(v));  // already handled NULL and BAD-WORD! possibilities
    Unquotify(v, 1);  // Remove meta level caused by parameter convention

    Meta_Unquotify(v);  // now remove the level of meta the user was asking for

    // !!! This needs to be handled more generally, but the idea is that if you
    // are to write:
    //
    //      >> x: ^()
    //      == ~void~
    //
    // Then you have captured the notion of invisibility by virtue of doing so.
    // But to truly "unmeta" that we get invisibility back.  That's tricky if
    // you don't want it...although DO and APPLY are leaning that way.  We
    // could error by default, but instead try defaulting to ~none~ isotope.
    //
    if (IS_END(v)) {
        if (REF(void))
            return D_OUT;  // invisible

        return Init_None(D_OUT);
    }

    return v;
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
//  noquote: native [
//
//  {Removes all levels of quoting from a quoted value}
//
//      return: [<opt> any-value!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(noquote)
{
    INCLUDE_PARAMS_OF_NOQUOTE;

    REBVAL *v = ARG(optional);
    Unquotify(v, VAL_NUM_QUOTES(v));
    return v;
}


//
//  MF_Symbol: C
//
void MF_Symbol(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    MF_Word(mo, v, form);
}


//
//  CT_Symbol: C
//
// Must have a comparison function, otherwise SORT would not work on arrays
// with ^ in them.
//
REBINT CT_Symbol(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    return CT_Word(a, b, strict);
}


//
//  REBTYPE: C
//
REBTYPE(Symbol)
{
    switch (ID_OF_SYMBOL(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // taken care of by `unit` above.

        // !!! REFLECT cannot use PARAM_FLAG_NOOP_IF_BLANK, due to the special
        // case of TYPE OF...where a BLANK! in needs to provide BLANK! the
        // datatype out.  Also, there currently exist "reflectors" that
        // return LOGIC!, e.g. TAIL?...and logic cannot blindly return null:
        //
        // https://forum.rebol.info/t/954
        //
        // So for the moment, we just ad-hoc return nullptr for some that
        // R3-Alpha returned NONE! for.  Review.
        //
        switch (VAL_WORD_ID(ARG(property))) {
          case SYM_INDEX:
          case SYM_LENGTH:
            return nullptr;

          default: break;
        }
        break; }

      case SYM_COPY: { // since `copy/deep [1 ^ 2]` is legal, allow `copy ^`
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(ARG(value));

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        UNUSED(REF(deep));
        UNUSED(REF(types));

        return ARG(value); }

      default: break;
    }

    return R_UNHANDLED;
}
