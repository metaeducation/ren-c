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
//  PD_Quoted: C
//
// Historically you could ask a LIT-PATH! questions like its length/etc, just
// like any other path.  So it seems types wrapped in QUOTED! should respond
// more or less like their non-quoted counterparts...
//
//     >> first just '[a b c]
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
    option(const REBVAL*) setval
){
    UNUSED(picker);
    UNUSED(setval);

    if (KIND3Q_BYTE(pvs->out) == REB_QUOTED)
        Copy_Cell(pvs->out, VAL_QUOTED_PAYLOAD_CELL(pvs->out));
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
//     >> add (the '''1) 2
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
    switch (VAL_WORD_ID(verb)) {
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

    fail ("QUOTED! has no GENERIC operations (use NOQUOTE/REQUOTE)");
}


//
//  the: native/body [
//
//  "Returns value passed in without evaluation"
//
//      return: "Input value, verbatim--unless /SOFT and soft quoted type"
//          [<opt> any-value!]
//      'value [any-value!]
//      /soft "Evaluate if a GET-GROUP!, GET-WORD!, or GET-PATH!"
//  ][
//      if soft and (match [get-group! get-word! get-path!] :value) [
//          reeval value
//      ] else [
//          :value  ; also sets unevaluated bit, how could a user do so?
//      ]
//  ]
//
REBNATIVE(the)
{
    INCLUDE_PARAMS_OF_THE;

    REBVAL *v = ARG(value);

    if (REF(soft) and ANY_ESCAPABLE_GET(v)) {
        if (Eval_Value_Throws(D_OUT, v, SPECIFIED))
            return R_THROWN;
        return D_OUT;  // Don't set UNEVALUATED flag
    }

    Copy_Cell(D_OUT, v);
    SET_CELL_FLAG(D_OUT, UNEVALUATED);
    return D_OUT;
}


//
//  just: native/body [
//
//  "Returns quoted eversion of value passed in without evaluation"
//
//      return: "Input value, verbatim--unless /SOFT and soft quoted type"
//          [<opt> any-value!]
//      'value [any-value!]
//      /soft "Evaluate if a GET-GROUP!, GET-WORD!, or GET-PATH!"
//  ][
//      if soft and (match [get-group! get-word! get-path!] :value) [
//          reeval value
//      ] else [
//          :value  ; also sets unevaluated bit, how could a user do so?
//      ]
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
            return R_THROWN;
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
        RETURN (ARG(optional));  

    if (depth < 0)
        fail (PAR(depth));

    Copy_Cell(D_OUT, ARG(optional));
    return Isotopic_Quotify(D_OUT, depth);
}


//
//  metaquote: native [
//
//  {Like single quote, but keeps ordinary NULL as-is}
//
//      return: "Quoted value (if depth = 0, may not be quoted)"
//          [<opt> any-value!]
//      optional [<opt> <meta> any-value!]
//  ]
//
REBNATIVE(metaquote)
{
    INCLUDE_PARAMS_OF_METAQUOTE;

    return Move_Cell(D_OUT, ARG(optional));  // argument is ^meta
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
//          [<opt> any-value!]
//      value [<opt> <meta> bad-word! quoted!]
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
        RETURN (v);  // ^(null) => null, so the reverse must be true

    // !!! This needs to be handled more generally, but the idea is that if you
    // are to write:
    //
    //      >> x: ^()
    //      == ~void~  ; isotope
    //
    // Then you have captured the notion of invisibility by virtue of doing so.
    // Had it been a void isotope inside the GROUP!, the literalization would
    // have been a *non*-isotope ~void~.
    //
    // But since we take a ^meta parameter here, we get that ~void~ isotope
    // as a non-isotope ~void~.  If we were to try and "unquote" the intent of
    // invisibility, then UNQUOTE would return invisibily...but that idea is
    // being saved for DEVOID to keep UNMETA more predictable.
    //
    // So we just return a void isotope in this case that DEVOID can handle.
    //
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
    RETURN (v);
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
    RETURN (v);
}


//
//  MF_Meta: C
//
void MF_Meta(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);
    UNUSED(v);

    Append_Codepoint(mo->series, '^');
}


//
//  CT_Meta: C
//
// Must have a comparison function, otherwise SORT would not work on arrays
// with ^ in them.
//
REBINT CT_Meta(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);  // no strict form of comparison
    UNUSED(a);
    UNUSED(b);

    return 0;  // All ^ are equal
}


//
//  REBTYPE: C
//
REBTYPE(Meta)
{
    switch (VAL_WORD_ID(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // taken care of by `unit` above.

        // !!! REFLECT cannot use REB_TS_NOOP_IF_BLANK, because of the special
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

        return Init_Lit(D_OUT); }

      default: break;
    }

    return R_UNHANDLED;
}


//
//  MF_The: C
//
void MF_The(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);
    UNUSED(v);

    Append_Codepoint(mo->series, '@');
}


//
//  CT_The: C
//
// Must have a comparison function, otherwise SORT would not work on arrays
// with @ in them.
//
REBINT CT_The(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);  // no strict form of comparison
    UNUSED(a);
    UNUSED(b);

    return 0;  // All ^ are equal
}


//
//  REBTYPE: C
//
REBTYPE(The)
{
    switch (VAL_WORD_ID(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // taken care of by `unit` above.

        // !!! REFLECT cannot use REB_TS_NOOP_IF_BLANK, because of the special
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

      case SYM_COPY: { // since `copy/deep [1 @ 2]` is legal, allow `copy @`
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(ARG(value));

        if (REF(part))
            fail (Error_Bad_Refines_Raw());

        UNUSED(REF(deep));
        UNUSED(REF(types));

        return Init_The(D_OUT); }

      default: break;
    }

    return R_UNHANDLED;
}
