//
//  File: %t-quoted.c
//  Summary: "QUOTED? datatype that acts as container for unquoted elements"
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
// Ren-C has a generic quoting, as a container which can be arbitrarily
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
//  /the: native [
//
//  "Returns value passed in without evaluation, but with binding"
//
//      return: "Input value, verbatim--unless /SOFT and soft quoted type"
//          [any-value?]
//      @value [element?]
//      :soft "Evaluate if a GET-GROUP!, GET-WORD!, or GET-TUPLE!"
//  ]
//
DECLARE_NATIVE(THE)
//
// Note: THE is not a perfect synonym for the action assigned to @ as far as
// the API is concerned, because the evaluator has special handling for
// antiforms:
//
//   https://forum.rebol.info/t/why-isnt-a-precise-synonym-for-the/2215
{
    INCLUDE_PARAMS_OF_THE;

    Element* v = Element_ARG(VALUE);

    if (REF(SOFT) and Is_Soft_Escapable_Group(v)) {
        if (Eval_Any_List_At_Throws(OUT, v, SPECIFIED))
            return THROWN;
        return OUT;
    }

    Copy_Cell(OUT, v);

    return OUT;
}


//
//  /just: native [
//
//  "Returns value passed in without evaluation, and no additional binding"
//
//      return: "Input value, verbatim"
//          [any-value?]
//      'element [element?]
//  ]
//
DECLARE_NATIVE(JUST)
//
// Note: JUST:SOFT doesn't make any sense, it cannot evaluate without binding.
{
    INCLUDE_PARAMS_OF_JUST;

    Element* quoted = Element_ARG(ELEMENT);
    return COPY(quoted);
}


//
//  /quote: native [
//
//  "Constructs a quoted form of the evaluated argument"
//
//      return: "Quoted value (if depth = 0, may not be quoted)"
//          [element?]
//      element [element?]
//      :depth "Number of quoting levels to apply (default 1)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(QUOTE)
{
    INCLUDE_PARAMS_OF_QUOTE;

    Element* e = Element_ARG(ELEMENT);
    REBINT depth = REF(DEPTH) ? VAL_INT32(ARG(DEPTH)) : 1;

    if (depth < 0)
        return FAIL(PARAM(DEPTH));

    Quotify_Depth(e, depth);
    return COPY(e);
}


//
//  /meta: native [
//
//  "antiforms -> quasiforms, adds a quote to rest (behavior of ^^)"
//
//      return: "Keywords and plain forms if :LITE, plain ERROR! ok if :EXCEPT"
//          [quoted! quasiform! keyword! element? error!]
//      ^atom [any-atom?]
//      :lite "Make plain forms vs. quasi, and pass thru keywords like ~null~"
//      :except "If argument is antiform ERROR!, give back as plain ERROR!"
//  ]
//
DECLARE_NATIVE(META)
//
// 1. Most code has to go through Coerce_To_Antiform()...even code that has
//    a quasiform in its hand (as not all quasiforms can be antiforms).  But
//    ^META parameters are guaranteed to be things that were validated as
//    antiforms.
{
    INCLUDE_PARAMS_OF_META;

    Value* meta = ARG(ATOM); // arg already ^META, no need to Meta_Quotify()

    if (Is_Meta_Of_Raised(meta)) {
        if (not REF(EXCEPT))
            return FAIL(Cell_Error(ARG(ATOM)));

        QUOTE_BYTE(meta) = NOQUOTE_1;
        return COPY(meta);  // no longer meta, just a plain ERROR!
    }

    if (
        REF(LITE)  // META:LITE handles quasiforms specially
        and Is_Quasiform(meta)
    ){
        if (HEART_BYTE(meta) == TYPE_WORD) {  // keywords pass thru
            QUOTE_BYTE(meta) = ANTIFORM_0_COERCE_ONLY;  // ^META validated [1]
            return COPY(meta);
        }
        QUOTE_BYTE(meta) = NOQUOTE_1;  // META:LITE gives plain for the rest.
        return COPY(meta);
    }

    return COPY(meta);
}


//
//  /meta*: native:intrinsic [
//
//  "META operator that works on any value (errors, packs, barriers, etc.)"
//
//      return: [quoted! quasiform!]
//      ^atom
//  ]
//
DECLARE_NATIVE(META_P)
{
    INCLUDE_PARAMS_OF_META_P;

    Get_Meta_Atom_Intrinsic(OUT, LEVEL);

    return OUT;  // argument was ^META, so no need to Meta_Quotify()
}


//
//  /unquote: native [
//
//  "Remove quoting levels from the evaluated argument"
//
//      return: [element?]
//      value [element?]
//      :depth "Number of quoting levels to remove (default 1)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(UNQUOTE)
{
    INCLUDE_PARAMS_OF_UNQUOTE;

    Element* v = Element_ARG(VALUE);

    Count depth = (REF(DEPTH) ? VAL_INT32(ARG(DEPTH)) : 1);

    if (depth < 0)
        return FAIL(PARAM(DEPTH));

    if (depth > Element_Num_Quotes(v))
        return FAIL("Value not quoted enough for unquote depth requested");

    return Unquotify_Depth(Copy_Cell(OUT, v), depth);
}


//
//  /quasi: native [
//
//  "Constructs a quasi form of the evaluated argument (if legal)"
//
//      return: "Raises an error if type cannot make the quasiform"
//          [quasiform! raised!]
//      element "Any non-QUOTED! value for which quasiforms are legal"
//          [fundamental? quasiform!]
//      :pass "If input is already a quasiform, then pass it trhough"
//  ]
//
DECLARE_NATIVE(QUASI)
//
// Not all datatypes have quasiforms.  For example:  ~:foo:~ is interpreted
// as a 3-element CHAIN! with quasi-blanks in the first and last spots.  We
// choose that interpretation because it is more useful, and also goes along
// with being able to have ~/home/whoever as a PATH!.
{
    INCLUDE_PARAMS_OF_QUASI;

    Element* elem = Element_ARG(ELEMENT);

    if (Is_Quasiform(elem)) {
        if (REF(PASS))
            return COPY(elem);
        return FAIL("Use QUASI:PASS if QUASI argument is already a quasiform");
    }

    Copy_Cell(OUT, elem);

    Option(Error*) e = Trap_Coerce_To_Quasiform(OUT);
    if (e)
        return RAISE(unwrap e);  // RAISE so (try quasi ':foo:) gives null

    return OUT;
}


//
//  /unquasi: native [
//
//  "Turn quasiforms into fundamental forms"
//
//      return: [fundamental?]
//      quasiform [quasiform!]
//  ]
//
DECLARE_NATIVE(UNQUASI)
{
    INCLUDE_PARAMS_OF_UNQUASI;

    Element* quasi = Element_ARG(QUASIFORM);
    return COPY(Unquasify(quasi));
}


//
//  /antiform?: native:intrinsic [
//
//  "Tells you whether argument is a stable or unstable antiform"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(ANTIFORM_Q)
//
// !!! This can be deceptive, in the sense that you could ask if something
// like an antiform pack is an antiform, and it will say yes...but then
// another routine like integer? might say it's an integer.  Be aware.
{
    INCLUDE_PARAMS_OF_ANTIFORM_Q;

    Heart heart;
    QuoteByte quote_byte;
    Get_Heart_And_Quote_Of_Atom_Intrinsic(&heart, &quote_byte, LEVEL);

    return LOGIC(quote_byte == ANTIFORM_0);
}


//
//  /anti: native [
//
//  "Give the antiform of the plain argument (like UNMETA QUASI)"
//
//      return: "Antiform of input (will be unbound)"
//          [antiform?]
//      element "Any non-QUOTED!, non-QUASI value"
//          [element?]  ; there isn't an any-nonquoted! typeset
//  ]
//
DECLARE_NATIVE(ANTI)
{
    INCLUDE_PARAMS_OF_ANTI;

    Element* elem = Element_ARG(ELEMENT);

    if (Is_Quoted(elem))
        return FAIL("QUOTED! values have no antiform");

    if (Is_Quasiform(elem))  // Review: Allow this?
        return FAIL("QUASIFORM! values can be made into antiforms with UNMETA");

    Copy_Cell(OUT, elem);
    Option(Error*) e = Trap_Coerce_To_Antiform(OUT);
    if (e)
        return FAIL(unwrap e);

    return OUT;
}


//
//  /unmeta: native [
//
//  "Variant of UNQUOTE that also accepts quasiforms to make antiforms"
//
//      return: [any-atom?]
//      value "Can be plain or antiform like ~null~ or ~void~ if :LITE"
//          [keyword! element? quoted! quasiform!]
//      :lite "Pass thru ~null~ and ~void~ antiforms as-is"
//  ]
//
DECLARE_NATIVE(UNMETA)
{
    INCLUDE_PARAMS_OF_UNMETA;

    Value* meta = ARG(VALUE);

    if (QUOTE_BYTE(meta) == ANTIFORM_0) {
        if (not REF(LITE) or not Is_Keyword(meta))
            return FAIL("UNMETA only keyword antiforms (e.g. ~null~) if :LITE");
        return COPY(meta);
    }

    if (QUOTE_BYTE(meta) == NOQUOTE_1) {
        if (not REF(LITE))
            return FAIL("UNMETA only takes non quoted/quasi things if :LITE");
        Copy_Cell(OUT, meta);

        Option(Error*) e = Trap_Coerce_To_Antiform(OUT);
        if (e)
            return FAIL(unwrap e);

        return OUT;
    }

    if (QUOTE_BYTE(meta) == QUASIFORM_2 and REF(LITE))
        return FAIL(
            "UNMETA:LITE does not accept quasiforms (plain forms are meta)"
        );

    return UNMETA(cast(Element*, meta));  // quoted or quasi
}


//
//  /unmeta*: native [
//
//  "Variant of UNMETA that can synthesize any atom (raised, pack, barrier...)"
//
//      return: [any-atom?]
//      metaform [quoted! quasiform?]
//  ]
//
DECLARE_NATIVE(UNMETA_P)
{
    INCLUDE_PARAMS_OF_UNMETA_P;

    Copy_Cell(OUT, ARG(METAFORM));
    return Meta_Unquotify_Undecayed(OUT);
}


//
//  /spread: native [
//
//  "Make block arguments splice"
//
//      return: "Antiform of GROUP! or unquoted value (pass null and void)"
//          [~null~ ~void~ element? splice!]
//      value [~null~ ~void~ blank! any-list? quasiform!]  ; see [1] [2] [3]
//  ]
//
DECLARE_NATIVE(SPREAD)
//
// !!! The name SPREAD is being chosen because it is more uncommon than splice,
// and there is no particular contention for its design.  SPLICE may be a more
// complex operation.
//
// 1. The current thinking on SPREAD is that it acts as passthru for null and
//    for void, and whatever you were going to pass the result of spread to
//    is responsible for raising errors or MAYBE'ing it.  Seems to work out.
//
// 2. Generally speaking, functions are not supposed to conflate quasiforms
//    with their antiforms.  But it seems like being willing to DEGRADE a
//    ~void~ or a ~null~ here instead of erroring helps more than it hurts.
//    Should it turn out to be bad for some reason, this might be dropped.
//
// 3. BLANK! is considered EMPTY? and hence legal to use with spread.  It
//    could return an empty splice...but that would then wind up having to
//    make a decision on using a "cheap" shared read-only array, or making
//    a new empty array to use.  Different usage situations would warrant
//    one vs. the other, e.g. GLOM expects splices to be mutable.  Void is
//    cheap and agnostic, so it's the logical choice here.
{
    INCLUDE_PARAMS_OF_SPREAD;

    Value* v = ARG(VALUE);

    if (Any_List(v)) {  // most common case
        Copy_Cell(OUT, v);
        HEART_BYTE(OUT) = TYPE_GROUP;  // throws away original heart

        Option(Error*) e = Trap_Coerce_To_Antiform(OUT);
        assert(not e);
        UNUSED(e);
        assert(Is_Splice(OUT));
        return OUT;
    }

    if (Is_Blank(v))
        return Init_Void(OUT);  // empty array makes problems for GLOM [3]

    if (Is_Void(v) or Is_Quasi_Void(v))  // quasi ok [2]
        return Init_Void(OUT);  // pass through [1]

    if (Is_Nulled(v) or Is_Quasi_Null(v))  // quasi ok [2]
        return Init_Nulled(OUT);  // pass through [1]

    return FAIL(v);
}


//
//  /lazy: native [
//
//  "Make objects lazy"
//
//      return: "Antiform of OBJECT! or unquoted value (pass null and void)"
//          [~null~ ~void~ element? lazy?]
//      object "Will do MAKE OBJECT! on BLOCK!"
//          [~null~ ~void~ quoted! object! block!]
//  ]
//
DECLARE_NATIVE(LAZY)
{
    INCLUDE_PARAMS_OF_LAZY;

    Value* v = ARG(OBJECT);
    if (Is_Void(v))
        return VOID;
    if (Is_Nulled(v))
        return nullptr;

    if (Is_Quoted(v))
        return Unquotify(Copy_Cell(OUT, cast(Element*, v)));

    if (Is_Block(v)) {
        if (rebRunThrows(cast(Value*, OUT), CANON(MAKE), CANON(OBJECT_X), v))
            return THROWN;
    }
    else
        Copy_Cell(OUT, v);

    assert(Is_Object(OUT));
    return Destabilize_Unbound_Fundamental(OUT);
}


// 1. In REDUCE, :PREDICATE functions are offered things like nihil and void
//    if they can accept them (which META can).  But COMMA! antiforms that
//    result from evaluating commas are -not- offered to any predicates.  This
//    is by design, so we get:
//
//        >> pack [1 + 2, comment "hi", if null [1020]]
//        == ~[3 ~[]~ ']~
//
INLINE bool Pack_Native_Core_Throws(
    Sink(Atom) out,
    const Value* block,
    const Value* predicate
){
    if (Is_The_Block(block)) {  // as-is: pack @[1 + 2] -> ~['1 '+ '2']~ anti
        const Element* tail;
        const Element* at = Cell_List_At(&tail, block);

        Length len = tail - at;
        Source* a = Make_Source_Managed(len);  // same size array
        Set_Flex_Len(a, len);
        Element *dest = Array_Head(a);

        for (; at != tail; ++at, ++dest)
            Copy_Meta_Cell(dest, at);

        Init_Pack(out, a);
        return false;
    }

    assert(Is_Block(block));

    if (rebRunThrows(
        cast(Value*, out),  // output cell
        CANON(QUASI), "reduce:predicate",  // commas excluded by :PREDICATE [1]
            rebQ(block), rebQ(predicate)
    )){
        return true;
    }

    Meta_Unquotify_Undecayed(out);
    return false;
}


//
//  /pack: native [
//
//  "Create a pack of arguments from a list, no raised errors (or see PACK*)"
//
//      return: "Antiform of BLOCK!"
//          [pack!]
//      block "Reduce if plain BLOCK!, not if THE-BLOCK!"
//          [<maybe> the-block! block!]
//  ]
//
DECLARE_NATIVE(PACK)
//
// 1. Using the predicate META means that raised errors aren't tolerated in
//    the main pack routine.  You have to use PACK*, which uses META* instead.
//
//        https://forum.rebol.info/t/2206
{
    INCLUDE_PARAMS_OF_PACK;

    Element* block = Element_ARG(BLOCK);

    if (Pack_Native_Core_Throws(OUT, block, LIB(META)))  // no raised [1]
        return THROWN;
    return OUT;
}


//
//  /pack*: native [
//
//  "Create a pack of arguments from a list, raised errors okay (or see PACK)"
//
//      return: "Antiform of BLOCK!"
//          [pack!]
//      block "Reduce if plain BLOCK!, not if THE-BLOCK!"
//          [<maybe> the-block! block!]
//  ]
//
DECLARE_NATIVE(PACK_P)
//
// 1. Using the predicate META* means that raised errors will be tolerated
//    by PACK*, whereas PACK does not.
//
//        https://forum.rebol.info/t/2206
{
    INCLUDE_PARAMS_OF_PACK_P;

    Element* block = Element_ARG(BLOCK);

    if (Pack_Native_Core_Throws(OUT, block, LIB(META_P)))  // raise ok [1]
        return THROWN;
    return OUT;
}


//
//  /lazy?: native:intrinsic [
//
//  "Tells you if argument is a lazy value (antiform object)"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(LAZY_Q)
{
    INCLUDE_PARAMS_OF_LAZY_Q;

    Heart heart;
    QuoteByte quote_byte;
    Get_Heart_And_Quote_Of_Atom_Intrinsic(&heart, &quote_byte, LEVEL);

    return LOGIC(quote_byte == ANTIFORM_0 and heart == TYPE_OBJECT);
}


//
//  /pack?: native:intrinsic [
//
//  "Tells you if argument is a parameter pack (antiform block)"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(PACK_Q)
{
    INCLUDE_PARAMS_OF_PACK_Q;

    Heart heart;
    QuoteByte quote_byte;
    Get_Heart_And_Quote_Of_Atom_Intrinsic(&heart, &quote_byte, LEVEL);

    return LOGIC(quote_byte == ANTIFORM_0 and heart == TYPE_BLOCK);
}


//
//  /runs: native [
//
//  "Make frames run when fetched through word access"
//
//      return: [action!]
//      frame [frame! action!]
//  ]
//
DECLARE_NATIVE(RUNS)
//
// 1. Is allowing things that are already antiforms a good idea?
//
// 2. This is designed to be a type checked synonym for `anti`...all the
//    actual work would be done regardless of using this routine.
{
    INCLUDE_PARAMS_OF_RUNS;

    Value* frame = ARG(FRAME);
    if (Is_Action(frame))  // already antiform, no need to pay for coercion [1]
        return COPY(frame);

    Copy_Cell(OUT, frame);

    Option(Error*) e = Trap_Coerce_To_Antiform(OUT);  // same code as anti [2]
    if (e)
        return FAIL(unwrap e);

    return OUT;
}


//
//  /unrun: native [
//
//  "Give back a frame! for action! input"
//
//      return: [frame!]
//      action [<maybe> frame! action!]
//  ]
//
DECLARE_NATIVE(UNRUN)
{
    INCLUDE_PARAMS_OF_UNRUN;

    Value* action = ARG(ACTION);  // may or may not be antiform
    QUOTE_BYTE(action) = NOQUOTE_1;  // now it's known to not be antiform
    return COPY(action);
}


//
//  /maybe: native:intrinsic [
//
//  "If argument is null, make it void (also pass through voids)"
//
//      return: "Void if input value was null"
//      value
//  ]
//
DECLARE_NATIVE(MAYBE)
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
    INCLUDE_PARAMS_OF_MAYBE;

    DECLARE_VALUE (v);
    Option(Bounce) bounce = Trap_Bounce_Decay_Value_Intrinsic(v, LEVEL);
    if (bounce)
        return unwrap bounce;

    if (Is_Void(v))
        return Init_Void(OUT);  // passthru

    if (Is_Nulled(v))
        return Init_Void(OUT);  // main purpose of function: NULL => VOID

    return COPY(v);  // passthru
}


//
//  /noquote: native:intrinsic [
//
//  "Removes all levels of quoting from a (potentially) quoted element"
//
//      return: [fundamental?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(NOQUOTE)
{
    INCLUDE_PARAMS_OF_NOQUOTE;

    Option(Bounce) b = Trap_Bounce_Maybe_Element_Intrinsic(OUT, LEVEL);
    if (b)
        return unwrap b;

    QUOTE_BYTE(OUT) = NOQUOTE_1;
    return OUT;
}
