//
//  File: %t-quoted.c
//  Summary: "QUOTED! datatype that acts as container for ANY-VALUE!"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2023 Ren-C Open Source Contributors
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
// new features.  THE takes the place of the former literalizing operator:
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

#include "sys-core.h"

//
//  CT_Quoted: C
//
// !!! Currently, in order to have a GENERIC dispatcher (e.g. REBTYPE())
// then one also must implement a comparison function.  However, compare
// functions specifically take noquote cells, so you can't pass REB_QUOTED to
// them.  The handling for QUOTED! is in the comparison dispatch itself.
//
REBINT CT_Quoted(NoQuote(const Cell*) a, NoQuote(const Cell*) b, bool strict)
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
    Level* level_,
    enum Reb_Kind kind,
    Option(Value(const*)) parent,
    const REBVAL *arg
){
    assert(kind == REB_QUOTED);
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    return Quotify(Copy_Cell(OUT, arg), 1);
}


//
//  TO_Quoted: C
//
// TO is disallowed at the moment, as there is no clear equivalence of things
// "to" a literal.  (to quoted! [[a]] => \\a, for instance?)
//
Bounce TO_Quoted(Level* level_, enum Reb_Kind kind, const REBVAL *data) {
    return RAISE(Error_Bad_Make(kind, data));
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

        REBLEN num_quotes = Dequotify(ARG(value));
        bool threw = Run_Generic_Dispatch_Throws(ARG(value), level_, verb);
        assert(not threw);  // can't throw
        UNUSED(threw);

        return Quotify(stable_OUT, num_quotes); }

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
//  "Returns value passed in without evaluation, but QUASI! become isotopes"
//
//      return: "If input is QUASI! then isotope, else input value, verbatim"
//          [<opt> any-value!]
//      'value [any-value!]
//  ]
//
DECLARE_NATIVE(the_p)
//
// THE* is the variant assigned to @.  It turns QUASI! forms into isotopes,
// but passes through all other values:
//
//     >> @ ~null~
//     == ~null~  ; isotope
//
//     >> @ ~[1 2 3]~
//     == ~[1 2 3]~  ; isotope
//
//     >> @ abc
//     == abc
//
// This is done as a convenience for the API so people can write:
//
//     rebElide("append block maybe @", value_might_be_null);
//
// ...instead of:
//
//     rebElide("append block maybe", rebQ(value_might_be_null));
//
// Because the API machinery puts FEED_NULL_SUBSTITUTE_CELL into the stream as
// a surrogate for a nullptr instead of asserting/erroring.
//
// The reason it isotopifies things is that the belief is that this will
// be more likely to generate something that will raise attention if it's not
// actually correct--otherwise the auto-reification would be more troublesome.
{
    INCLUDE_PARAMS_OF_THE_P;

    REBVAL *v = ARG(value);

    if (Is_Quasi(v)) {  // for `rebElide("@", nullptr, "else [...]");`
        Copy_Cell(OUT, v);
        QUOTE_BYTE(OUT) = ISOTOPE_0;
    }
    else {
        Copy_Cell(OUT, v);
        Set_Cell_Flag(OUT, UNEVALUATED);  // !!! Is this a good idea?
    }
    return OUT;
}


//
//  quote: native [
//
//  {Constructs a quoted form of the evaluated argument}
//
//      return: "Quoted value (if depth = 0, may not be quoted)"
//          [<void> element?]
//      optional [<void> element?]
//      /depth "Number of quoting levels to apply (default 1)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(quote)
{
    INCLUDE_PARAMS_OF_QUOTE;

    REBINT depth = REF(depth) ? VAL_INT32(ARG(depth)) : 1;

    if (depth < 0)
        fail (PARAM(depth));

    return COPY(Quotify(ARG(optional), depth));
}


//
//  meta: native/intrinsic [
//
//  {isotopes -> QUASI!, adds a quote to rest (behavior of ^^)}
//
//      return: [quoted! quasi!]
//      ^atom
//  ]
//
DECLARE_INTRINSIC(meta)
{
    UNUSED(phase);

    Copy_Cell(out, arg);  // arg was already ^META, no need to Meta_Quotify()
}


//
//  meta*: native [
//
//  {META variant that passes through VOID and NULL, and doesn't take failures}
//
//      return: [<opt> <void> quoted! quasi!]
//      ^optional [<opt> <void> pack? any-value!]
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
//          [<void> any-value!]
//      value "Void allowed in case input is void and /DEPTH is 0"
//          [<void> any-value!]
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
        fail (PARAM(depth));

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
//          [<opt> any-value!]  ; there isn't an any-nonquoted! typeset
//  ]
//
DECLARE_NATIVE(quasi)
{
    INCLUDE_PARAMS_OF_QUASI;

    Value(*) v = ARG(value);

    if (Is_Quoted(v))
        fail ("QUOTED! values do not have QUASI! forms");

    return COPY(Quasify(v));
}


//
//  unquasi: native/intrinsic [
//
//  {Remove QUASI! wrapper from the argument}
//
//      return: "Value with quasi state removed"
//          [element?]  ; more narrowly, a non-quasi non-quoted element
//      value [quasi!]
//  ]
//
DECLARE_INTRINSIC(unquasi)
{
    UNUSED(phase);

    Copy_Cell(out, arg);
    Unquasify(out);
}


//
//  isotopic: native [
//
//  {Give the isotopic form of the plain argument (same as UNMETA QUASI)}
//
//      return: [isotope!]
//      value "Any non-QUOTED!, non-QUASI value"
//          [<opt> any-value!]  ; there isn't an any-nonquoted! typeset
//  ]
//
DECLARE_NATIVE(isotopic)
{
    INCLUDE_PARAMS_OF_ISOTOPIC;

    Value(*) v = ARG(value);

    if (Is_Quoted(v))
        fail ("QUOTED! values have no isotopic form (isotopes are quoted -1");

    if (Is_Quasi(v))  // Review: Allow this?
        fail ("QUASI! values can be made isotopic with UNMETA");

    Copy_Cell(OUT, v);
    QUOTE_BYTE(OUT) = ISOTOPE_0;
    return OUT;
}


//
//  unmeta: native/intrinsic [
//
//  {Variant of UNQUOTE that also accepts QUASI! to make isotopes}
//
//      return: [<opt> <void> nihil? any-value!]
//      value [quoted! quasi!]
//  ]
//
DECLARE_INTRINSIC(unmeta)
{
    UNUSED(phase);

    Copy_Cell(out, arg);
    Meta_Unquotify_Undecayed(out);
}


//
//  unmeta*: native/intrinsic [
//
//  {Variant of UNMETA that passes thru VOID and NULL}
//
//      return: [<opt> <void> nihil? any-value!]
//      value [<opt> <void> quoted! quasi!]
//  ]
//
DECLARE_INTRINSIC(unmeta_p)
{
    UNUSED(phase);

    if (Is_Void(arg)) {
        Init_Void(out);
    }
    else if (Is_Nulled(arg)) {
        Init_Nulled(out);
    }
    else {
        Copy_Cell(out, arg);
        Meta_Unquotify_Undecayed(out);
    }
}


//
//  spread: native/intrinsic [
//
//  {Make block arguments splice}
//
//      return: "Isotope of BLOCK! or unquoted value (passthru null and void)"
//          [<opt> <void> element? splice?]
//      value [<opt> <void> quoted! blank! any-array!]
//  ]
//
DECLARE_INTRINSIC(spread)
//
// !!! The name SPREAD is being chosen because it is more uncommon than splice,
// and there is no particular contention for its design.  SPLICE may be a more
// complex operation.
//
// 1. The current thinking on SPREAD is that it acts as passthru for null and
//    for void, and whatever you were going to pass the result of spread to
//    is responsible for raising errors or MAYBE'ing it.  Seems to work out.
//
// 2. BLANK! is sort of the universal placeholder meaning agnostically "no
//    value here".  If you wanted SPREAD to give an error you could use the
//    more ornery single-character placeholder `~` (a quasi-void).
//
// 3. !!! The idea that quoted elements spread to be their unquoted forms was
//    presumably added here to provide an efficiency hack, so that you could
//    avoid making a series.  This has the added behavior that a single quote
//    (') would SPREAD to make a VOID...which may be desirable even if the
//    generic unquoting behavior is not.  This might be something controlled
//    with a refinement (which would prevent spread from being an intrinsic)
//    but it may just be undesirable.  Review.
{
    UNUSED(phase);

    if (Is_Void(arg)) {
        Init_Void(out);  // pass through [1]
    }
    else if (Is_Nulled(arg)) {
        Init_Nulled(out);  // pass through [1]
    }
    else if (Is_Blank(arg)) {
        Init_Splice(out, EMPTY_ARRAY);  // treat blank as if it was [] [2]
    }
    else if (Is_Quoted(arg)) {
        Unquotify(Copy_Cell(out, arg), 1);  // !!! good idea or not?  [3]
    }
    else {
        assert(Any_Array(arg));
        Copy_Cell(out, arg);
        HEART_BYTE(out) = REB_GROUP;
        QUOTE_BYTE(out) = ISOTOPE_0;
    }
}


//
//  lazy: native [
//
//  {Make objects lazy}
//
//      return: "Isotope of OBJECT! or unquoted value (passthru null and void)"
//          [<opt> <void> any-value!]
//      object "Will do MAKE OBJECT! on BLOCK!"
//          [<opt> <void> quoted! object! block!]
//  ]
//
DECLARE_NATIVE(lazy)
{
    INCLUDE_PARAMS_OF_LAZY;

    Value(*) v = ARG(object);
    if (Is_Void(v))
        return VOID;
    if (Is_Nulled(v))
        return nullptr;

    if (Is_Quoted(v))
        return Unquotify(Copy_Cell(OUT, v), 1);

    if (Is_Block(v)) {
        if (rebRunThrows(cast(REBVAL*, OUT), Canon(MAKE), Canon(OBJECT_X), v))
            return THROWN;
    }
    else
        Copy_Cell(OUT, v);

    assert(Is_Object(OUT));
    QUOTE_BYTE(OUT) = ISOTOPE_0;
    return OUT;
}


//
//  pack: native [
//
//  {Create a pack of arguments from an array}
//
//      return: "Isotope of BLOCK! or unquoted value (passthru null and void)"
//          [<opt> <void> any-value!]
//      array [<opt> <void> quoted! the-block! block!]
//  ]
//
DECLARE_NATIVE(pack)
//
// We create a new array for a pack, because we need to turn each item into
// its meta form.  But for literal packs, this could be optimized with a
// "pretend everything in the block is quoted" flag.
{
    INCLUDE_PARAMS_OF_PACK;

    Value(*) v = ARG(array);
    if (Is_Void(v))
        return VOID;
    if (Is_Nulled(v))
        return nullptr;

    if (Is_Quoted(v))
        return Unquotify(Copy_Cell(OUT, v), 1);

    if (Is_The_Block(v)) {
        const Cell* tail;
        const Cell* at = VAL_ARRAY_AT(&tail, v);
        for (; at != tail; ++at)
            Quotify(Derelativize(PUSH(), at, VAL_SPECIFIER(v)), 1);

        return Init_Pack(OUT, Pop_Stack_Values(BASELINE->stack_base));
    }

    assert(Is_Block(v));

    if (rebRunThrows(
        cast(REBVAL*, SPARE),  // output cell
        Canon(QUASI), "reduce/predicate", v, Lib(META)
    )){
        return THROWN;
    }

    return UNMETA(stable_SPARE);
}


//
//  matches: native [
//
//  {Create isotopic pattern to signal a desire to test types non-literally}
//
//      return: "Isotope of TYPE-XXX!"
//          [<opt> any-matcher!]
//      types [<opt> block! any-type-value!]
//  ]
//
DECLARE_NATIVE(matches)
{
    INCLUDE_PARAMS_OF_MATCHES;

    Value(*) v = ARG(types);

    if (Is_Nulled(v))
        return nullptr;  // Put TRY on the FIND or whatever, not MATCHES

    if (Any_Type_Value(v))
        return UNMETA(Quasify(v));

    assert(Is_Block(v));
    Copy_Cell(OUT, v);
    HEART_BYTE(OUT) = REB_TYPE_BLOCK;
    QUOTE_BYTE(OUT) = ISOTOPE_0;

    return OUT;
}


//
//  splice?: native/intrinsic [
//
//  "Tells you if argument is a splice (isotopic group)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(splice_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Splice(arg));
}


//
//  any-matcher?: native/intrinsic [
//
//  "Tells you if argument is any kind of matcher (TYPE-XXX! isotope)"
//
//      return: [logic?]
//      ^value [<opt> <void> raised? pack? any-value!]
//  ]
//
DECLARE_INTRINSIC(any_matcher_q)
{
    UNUSED(phase);

    if (Is_Quasi(arg) and Any_Type_Value_Kind(HEART_BYTE(arg)))
        Init_True(out);
    else
        Init_False(out);
}


//
//  lazy?: native/intrinsic [
//
//  "Tells you if argument is a lazy value (isotopic object)"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_INTRINSIC(lazy_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Meta_Of_Lazy(arg));
}


//
//  pack?: native/intrinsic [
//
//  "Tells you if argument is a parameter pack (isotopic block)"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_INTRINSIC(pack_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Meta_Of_Pack(arg));
}


//
//  isoword?: native/intrinsic [
//
//  "Tells you if argument is an isotopic word"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(isoword_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Isoword(arg));
}


//
//  activation?: native/intrinsic [
//
//  "Tells you if argument is an activation (isotopic action)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(activation_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Activation(arg));
}


//
//  runs: native/intrinsic [
//
//  {Make frames run when fetched through word access}
//
//      return: [activation!]
//      frame [<maybe> frame! activation?]
//  ]
//
DECLARE_INTRINSIC(runs)
{
    UNUSED(phase);

    Copy_Cell(out, arg);  // may or may not be isotope
    QUOTE_BYTE(out) = ISOTOPE_0;  // now it's known to be an isotope
}


//
//  unrun: native/intrinsic [
//
//  {Give back a frame! for activation? input}
//
//      return: [frame!]
//      action [<maybe> frame! activation?]
//  ]
//
DECLARE_INTRINSIC(unrun)
{
    UNUSED(phase);

    Copy_Cell(out, arg);  // may or may not be isotope
    QUOTE_BYTE(out) = UNQUOTED_1;  // now it's known to not be isotopic
}


//
//  maybe: native/intrinsic [
//
//  {If argument is null, make it void (also pass through voids)}
//
//      return: "Null if input value was void"
//      value
//  ]
//
DECLARE_INTRINSIC(maybe)
//
// 1. !!! Should MAYBE of a parameter pack be willing to twist that parameter
//    pack, e.g. with a NULL in the first slot--into one with a void in the
//    first slot?  Currently this does not, meaning you can't say
//
//        [a b]: maybe multi-return
//
//    ...and leave the `b` element left untouched.  Review.
//
// 2. !!! Should MAYBE of a raised error pass through the raised error?
{
    UNUSED(phase);

    if (Is_Void(arg)) {
        Init_Void(out);  // passthru
    }
    else if (Is_Nulled(arg)) {
        Init_Void(out);  // main purpose of function: NULL => VOID
    }
    else
        Copy_Cell(out, arg);  // passthru
}


//
//  quoted?: native/intrinsic [
//
//  {Tells you if the argument is QUOTED! or not}
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(quoted_q)
{
    UNUSED(phase);

    Init_Logic(out, VAL_TYPE(arg) == REB_QUOTED);
}


//
//  quasi?: native/intrinsic [
//
//  {Tells you if the argument is QUASI! or not}
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_INTRINSIC(quasi_q)
{
    UNUSED(phase);

    Init_Logic(out, VAL_TYPE(arg) == REB_QUASI);
}


//
//  noquote: native/intrinsic [
//
//  {Removes all levels of quoting from a quoted value}
//
//      return: [<void> element?]
//      optional [<void> element?]
//  ]
//
DECLARE_INTRINSIC(noquote)
{
    UNUSED(phase);

    Copy_Cell(out, arg);
    Unquotify(out, VAL_NUM_QUOTES(out));
}


//
//  atom?: native/intrinsic [
//
//  "Tells you if argument is truly anything--e.g. packs, raised, voids, etc."
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_INTRINSIC(atom_q)
{
    UNUSED(phase);
    UNUSED(arg);

    Init_Logic(out, true);
}
