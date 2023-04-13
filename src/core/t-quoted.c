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
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    return Quotify(Copy_Cell(OUT, arg), 1);
}


//
//  TO_Quoted: C
//
// TO is disallowed at the moment, as there is no clear equivalence of things
// "to" a literal.  (to quoted! [[a]] => \\a, for instance?)
//
Bounce TO_Quoted(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *data) {
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
//  MAKE_Isotope: C
//
Bounce MAKE_Isotope(
    Frame(*) frame_,
    enum Reb_Kind kind,
    option(const REBVAL*) parent,
    const REBVAL *arg
){
    assert(kind == REB_ISOTOPE);
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    return Quotify(Copy_Cell(OUT, arg), 1);
}


//
//  TO_Isotope: C
//
Bounce TO_Isotope(Frame(*) frame_, enum Reb_Kind kind, const REBVAL *data) {
    return RAISE(Error_Bad_Make(kind, data));
}


//
//  REBTYPE: C
//
REBTYPE(Isotope)
{
    UNUSED(frame_);
    UNUSED(verb);

    fail ("ISOTOPE! has no GENERIC operations");
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

    if (IS_QUASI(v)) {  // for `rebElide("@", nullptr, "else [...]");`
        Copy_Cell(OUT, v);
        mutable_QUOTE_BYTE(OUT) = ISOTOPE_0;
    }
    else {
        Copy_Cell(OUT, v);
        Set_Cell_Flag(OUT, UNEVALUATED);  // !!! Is this a good idea?
    }
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
// Note: This could be defined as `chain [^the, ^quote]`.  However, it can be
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
//          [<void> any-value!]
//      optional [<void> any-value!]
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
        fail (PARAM(depth));

    Copy_Cell(OUT, ARG(optional));
    return Quotify(OUT, depth);
}


//
//  meta: native [
//
//  {VOID -> NULL, isotopes -> QUASI!, adds a quote to rest (behavior of ^^)}
//
//      return: [quoted! quasi!]
//      ^optional [<void> <opt> <fail> <pack> any-value!]
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
//      ^optional [<opt> <void> <pack> any-value!]
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
//  isotopic: native [
//
//  {Give the isotopic form of the plain argument (same as UNMETA QUASI)}
//
//      return: []  ; isotope!
//      value "Any non-QUOTED!, non-QUASI value"
//          [<opt> any-value!]  ; there isn't an any-nonquoted! typeset
//  ]
//
DECLARE_NATIVE(isotopic)
{
    INCLUDE_PARAMS_OF_ISOTOPIC;

    Value(*) v = ARG(value);

    if (IS_QUOTED(v))
        fail ("QUOTED! values have no isotopic form (isotopes are quoted -1");

    if (IS_QUASI(v))  // Review: Allow this?
        fail ("QUASI! values can be made isotopic with UNMETA");

    Copy_Cell(OUT, v);
    mutable_QUOTE_BYTE(OUT) = ISOTOPE_0;
    return OUT;
}


//
//  unmeta: native [
//
//  {Variant of UNQUOTE that also accepts QUASI! to make isotopes}
//
//      return: [<opt> <void> any-value!]
//      value [blank! quoted! quasi!]
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
        Meta_Unquotify(v);
        fail (Error_Bad_Isotope(v));  // isotopes not allowed as input
    }

    // handling the invisibility detour is done now...

    Unquotify(v, 1);  // drop quote level caused by ^META parameter convention

    // Now remove the level of meta the user was asking for.
    //
    return UNMETA(v);
}


//
//  spread: native [
//
//  {Make block arguments splice}
//
//      return: "Isotope of BLOCK! or unquoted value (passthru null and void)"
//          [<opt> <void> any-value!]
//      array [<opt> <void> quoted! blank! any-array!]
//  ]
//
DECLARE_NATIVE(spread)
//
// !!! The name SPREAD is being chosen because it is more uncommon than splice,
// and there is no particular contention for its design.  SPLICE may be a more
// complex operation.
{
    INCLUDE_PARAMS_OF_SPREAD;

    Value(*) v = ARG(array);
    if (Is_Void(v))
        return VOID;
    if (Is_Nulled(v))
        return nullptr;

    if (IS_BLANK(v))
        return Init_Splice(OUT, EMPTY_ARRAY);  // treat blank as if it was []

    if (IS_QUOTED(v))
        return Unquotify(Copy_Cell(OUT, v), 1);

    mutable_HEART_BYTE(v) = REB_GROUP;
    return UNMETA(Quasify(v));
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

    if (IS_QUOTED(v))
        return Unquotify(Copy_Cell(OUT, v), 1);

    if (IS_BLOCK(v)) {
        if (rebRunThrows(OUT, Canon(MAKE), Canon(OBJECT_X), v))
            return THROWN;
    }
    else
        Copy_Cell(OUT, v);

    assert(IS_OBJECT(OUT));
    mutable_QUOTE_BYTE(OUT) = ISOTOPE_0;
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

    if (IS_QUOTED(v))
        return Unquotify(Copy_Cell(OUT, v), 1);

    if (IS_THE_BLOCK(v)) {
        Cell(const*) tail;
        Cell(const*) at = VAL_ARRAY_AT(&tail, v);
        for (; at != tail; ++at)
            Quotify(Derelativize(PUSH(), at, VAL_SPECIFIER(v)), 1);

        return Init_Pack(OUT, Pop_Stack_Values(BASELINE->stack_base));
    }

    assert(IS_BLOCK(v));
    if (rebRunThrows(SPARE,
        Canon(QUASI), Canon(COLLECT), "[", Canon(REDUCE_EACH), "^x", v,
            "[keep x]",
        "]"
    )){
        return THROWN;
    }
    return UNMETA(SPARE);
}


//
//  matches: native [
//
//  {Create isotopic pattern to signal a desire to test types non-literally}
//
//      return: "Isotope of TYPE-XXX!"
//          [<opt> any-value!]
//      types [<opt> type-word! type-group! block!]
//  ]
//
DECLARE_NATIVE(matches)
{
    INCLUDE_PARAMS_OF_MATCHES;

    Value(*) v = ARG(types);

    if (Is_Nulled(v))
        return nullptr;  // Put TRY on the FIND or whatever, not MATCHES

    if (IS_TYPE_WORD(v) or IS_TYPE_GROUP(v))
        return UNMETA(Quasify(v));

    assert(IS_BLOCK(v));
    mutable_HEART_BYTE(v) = REB_TYPE_BLOCK;

    return Meta_Unquotify(Quasify(OUT));
}


//
//  splice?: native [
//
//  "Tells you if argument is a splice (isotopic block)"
//
//      return: [logic!]
//      ^optional [<opt> <void> <fail> <pack> any-value!]
//  ]
//
DECLARE_NATIVE(splice_q)
{
    INCLUDE_PARAMS_OF_SPLICE_Q;

    return Init_Logic(OUT, Is_Meta_Of_Splice(ARG(optional)));
}


//
//  lazy?: native [
//
//  "Tells you if argument is a lazy value (isotopic object)"
//
//      return: [logic!]
//      ^optional [<opt> <void> <fail> <pack> any-value!]
//  ]
//
DECLARE_NATIVE(lazy_q)
{
    INCLUDE_PARAMS_OF_LAZY_Q;

    return Init_Logic(OUT, Is_Meta_Of_Lazy(ARG(optional)));
}


//
//  pack?: native [
//
//  "Tells you if argument is a parameter pack (isotopic block)"
//
//      return: [logic!]
//      ^optional [<opt> <void> <fail> <pack> any-value!]
//  ]
//
DECLARE_NATIVE(pack_q)
{
    INCLUDE_PARAMS_OF_PACK_Q;

    return Init_Logic(OUT, Is_Meta_Of_Pack(ARG(optional)));
}


//
//  activation?: native [
//
//  "Tells you if argument is an activation (isotopic action)"
//
//      return: [logic!]
//      ^optional [<opt> <void> <fail> <pack> any-value!]
//  ]
//
DECLARE_NATIVE(activation_q)
{
    INCLUDE_PARAMS_OF_ACTIVATION_Q;

    return Init_Logic(OUT, Is_Meta_Of_Activation(ARG(optional)));
}



//
//  runs: native [
//
//  {Make actions run when fetched through word access}
//
//      return: [~action!~]
//      action [<maybe> action! ~action!~]
//  ]
//
DECLARE_NATIVE(runs)
{
    INCLUDE_PARAMS_OF_RUNS;

    REBVAL *v = ARG(action);
    Copy_Cell(OUT, v);  // may or may not be isotope
    mutable_QUOTE_BYTE(OUT) = ISOTOPE_0;  // now it's known to be an isotope
    return OUT;
}


//
//  unrun: native [
//
//  {Make actions not run when fetched through word access}
//
//      return: [action!]
//      action [<maybe> action! ~action!~]
//  ]
//
DECLARE_NATIVE(unrun)
{
    INCLUDE_PARAMS_OF_RUNS;

    REBVAL *v = ARG(action);
    Copy_Cell(OUT, v);  // may or may not be isotope
    mutable_QUOTE_BYTE(OUT) = UNQUOTED_1;  // now it's known to not be isotopic
    return OUT;
}


//
//  maybe: native [
//
//  {If argument is null or none, make it void (also pass through voids)}
//
//      return: "Value (if it's anything other than the states being checked)"
//          [<opt> <void> any-value!]
//      ^optional [<opt> <void> any-value!]
//  ]
//
DECLARE_NATIVE(maybe)
//
// 1. !!! Should MAYBE of a parameter pack be willing to twist that parameter
//    pack, e.g. with a NULL in the first slot--into one with a void in the
//    first slot?  Currently this does not, meaning you can't say
//
//        [a b]: maybe multi-return
//
//    ...and leave the `b` element left untouched.  Review.
{
    INCLUDE_PARAMS_OF_MAYBE;

    REBVAL *v = ARG(optional);
    Meta_Unquotify(v);

    if (Is_None(v))  // !!! Should MAYBE be tolerant of NONE?
        return VOID;

    Decay_If_Unstable(v);  // question about decay, see [1]

    if (Is_Void(v))
        return VOID;  // passthru

    if (Is_Nulled(v))
        return VOID;  // main purpose of function: NULL => VOID

    if (Is_Raised(v)) {  // !!! fold in TRY behavior as well?
        ERROR_VARS *vars = ERR_VARS(VAL_CONTEXT(v));
        if (
            IS_WORD(&vars->id)
            and VAL_WORD_ID(&vars->id) == SYM_TRY_IF_NULL_MEANT
        ){
            return VOID;
        }
        return RAISE(VAL_CONTEXT(v));
    }

    return COPY(v);
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

    if (Is_Nulled(v))
        return nullptr;

    Unquotify(v, VAL_NUM_QUOTES(v));
    return COPY(v);
}
