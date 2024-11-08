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
//  CT_Quoted: C
//
// !!! Currently, in order to have a GENERIC dispatcher (e.g. DECLARE_GENERICS())
// then one also must implement a comparison function.  However, compare
// functions specifically take noquote cells, so you can't pass REB_QUOTED to
// them.  The handling for QUOTED? is in the comparison dispatch itself.
//
REBINT CT_Quoted(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(a); UNUSED(b); UNUSED(strict);
    assert(!"CT_Quoted should never be called");
    return 0;
}


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
DECLARE_NATIVE(the)
//
// Note: THE is not a perfect synonym for the action assigned to @ as far as
// the API is concerned, because the evaluator has special handling for
// antiforms:
//
//   https://forum.rebol.info/t/why-isnt-a-precise-synonym-for-the/2215
{
    INCLUDE_PARAMS_OF_THE;

    Element* v = cast(Element*, ARG(value));

    if (REF(soft) and Is_Soft_Escapable_Group(v)) {
        if (Eval_Any_List_At_Throws(OUT, v, SPECIFIED))
            return THROWN;
        return OUT;
    }

    Copy_Cell(OUT, v);

    return OUT;
}


//
//  /just: native:intrinsic [
//
//  "Returns value passed in without evaluation, and no additional binding"
//
//      return: "Input value, verbatim"
//          [any-value?]
//      'element [element?]
//  ]
//
DECLARE_NATIVE(just)
//
// Note: JUST:SOFT doesn't make any sense, it cannot evaluate without binding.
{
    INCLUDE_PARAMS_OF_JUST;

    Element* quoted = cast(Element*, ARG_1);
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
DECLARE_NATIVE(quote)
{
    INCLUDE_PARAMS_OF_QUOTE;

    Element* e = cast(Element*, ARG(element));
    REBINT depth = REF(depth) ? VAL_INT32(ARG(depth)) : 1;

    if (depth < 0)
        return FAIL(PARAM(depth));

    Quotify(e, depth);
    return COPY(e);
}


//
//  /meta: native [
//
//  "antiforms -> quasiforms, adds a quote to rest (behavior of ^^)"
//
//      return: "Keywords and plain forms if :LITE, plain ERROR! ok if :EXCEPT"
//          [quoted? quasi? keyword? element? error!]
//      ^atom [any-atom?]
//      :lite "Make plain forms vs. quasi, and pass thru keywords like ~null~"
//      :except "If argument is antiform ERROR!, give back as plain ERROR!"
//  ]
//
DECLARE_NATIVE(meta)
//
// 1. Most code has to go through Coerce_To_Antiform()...even code that has
//    a quasiform in its hand (as not all quasiforms can be antiforms).  But
//    ^META parameters are guaranteed to be things that were validated as
//    antiforms.
{
    INCLUDE_PARAMS_OF_META;

    Value* meta = ARG(atom); // arg already ^META, no need to Meta_Quotify()

    if (Is_Meta_Of_Raised(meta)) {
        if (not REF(except))
            return FAIL(Cell_Error(ARG(atom)));

        QUOTE_BYTE(meta) = NOQUOTE_1;
        return COPY(meta);  // no longer meta, just a plain ERROR!
    }

    if (
        REF(lite)  // META:LITE handles quasiforms specially
        and Is_Quasiform(meta)
    ){
        if (HEART_BYTE(meta) == REB_WORD) {  // keywords pass thru
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
//      return: [quoted! quasi?]
//      ^atom [any-atom?]
//  ]
//
DECLARE_NATIVE(meta_p)
{
    INCLUDE_PARAMS_OF_META_P;

    Element* meta = cast(Element*, ARG_1);
    return COPY(meta);  // argument was ^META, so no need to Meta_Quotify()
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
DECLARE_NATIVE(unquote)
{
    INCLUDE_PARAMS_OF_UNQUOTE;

    Value* v = ARG(value);

    Count depth = (REF(depth) ? VAL_INT32(ARG(depth)) : 1);

    if (depth < 0)
        return FAIL(PARAM(depth));

    if (depth > Cell_Num_Quotes(v))
        return FAIL("Value not quoted enough for unquote depth requested");

    Unquotify(Copy_Cell(OUT, v), depth);
    return OUT;
}


//
//  /quasi: native [
//
//  "Constructs a quasi form of the evaluated argument"
//
//      return: [quasi?]
//      value "Any non-QUOTED! value for which quasiforms are legal"
//          [any-isotopic?]
//  ]
//
DECLARE_NATIVE(quasi)
{
    INCLUDE_PARAMS_OF_QUASI;

    Value* v = ARG(value);

    if (Is_Quoted(v))
        return FAIL("Quoted values do not have quasiforms");

    return COPY(Quasify(v));
}


//
//  /unquasi: native:intrinsic [
//
//  "Turn quasiforms into common forms"
//
//      return: [any-isotopic?]  ; a non-quasi, non-quoted element
//      quasiform [quasi?]
//  ]
//
DECLARE_NATIVE(unquasi)
{
    INCLUDE_PARAMS_OF_UNQUASI;

    Element* quasi = cast(Element*, ARG_1);
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
DECLARE_NATIVE(antiform_q)
//
// !!! This can be deceptive, in the sense that you could ask if something
// like an antiform pack is an antiform, and it will say yes...but then
// another routine like integer? might say it's an integer.  Be aware.
{
    INCLUDE_PARAMS_OF_ANTIFORM_Q;

    Element* meta = cast(Element*, ARG_1);
    return Init_Logic(OUT, Is_Quasiform(meta));
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
DECLARE_NATIVE(anti)
{
    INCLUDE_PARAMS_OF_ANTI;

    Element* e = cast(Element*, ARG(element));

    if (Is_Quoted(e))
        return FAIL("QUOTED! values have no antiform");

    if (Is_Quasiform(e))  // Review: Allow this?
        return FAIL("QUASIFORM! values can be made into antiforms with UNMETA");

    Copy_Cell(OUT, e);
    return Coerce_To_Antiform(OUT);
}


//
//  /unmeta: native [
//
//  "Variant of UNQUOTE that also accepts quasiforms to make antiforms"
//
//      return: [any-atom?]
//      value "Can be plain or antiform like ~null~ or ~void~ if :LITE"
//          [keyword? element? quoted? quasi?]
//      :lite "Pass thru ~null~ and ~void~ antiforms as-is"
//  ]
//
DECLARE_NATIVE(unmeta)
{
    INCLUDE_PARAMS_OF_UNMETA;

    Value* meta = ARG(value);

    if (QUOTE_BYTE(meta) == ANTIFORM_0) {
        if (not REF(lite) or not Is_Keyword(meta))
            return FAIL("UNMETA only keyword antiforms (e.g. ~null~) if :LITE");
        return COPY(meta);
    }

    if (QUOTE_BYTE(meta) == NOQUOTE_1) {
        if (not REF(lite))
            return FAIL("UNMETA only takes non quoted/quasi things if :LITE");
        Copy_Cell(OUT, meta);
        return Coerce_To_Antiform(OUT);
    }

    if (QUOTE_BYTE(meta) == QUASIFORM_2 and REF(lite))
        return FAIL(
            "UNMETA:LITE does not accept quasiforms (plain forms are meta)"
        );

    return UNMETA(cast(Element*, meta));  // quoted or quasi
}


//
//  /unmeta*: native:intrinsic [
//
//  "Variant of UNMETA that can synthesize any atom (raised, pack, barrier...)"
//
//      return: [any-atom?]
//      value [quoted? quasi?]
//  ]
//
DECLARE_NATIVE(unmeta_p)
{
    INCLUDE_PARAMS_OF_UNMETA_P;

    Copy_Cell(OUT, ARG_1);
    return Meta_Unquotify_Undecayed(OUT);
}


//
//  /spread: native:intrinsic [
//
//  "Make block arguments splice"
//
//      return: "Antiform of GROUP! or unquoted value (pass null and void)"
//          [~null~ ~void~ element? splice?]
//      value [~null~ ~void~ blank! any-list? quasi?]  ; see [1] [2] [3]
//  ]
//
DECLARE_NATIVE(spread)
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

    Value* v = ARG_1;

    if (Any_List(v)) {  // most common case
        HEART_BYTE(v) = REB_GROUP;  // throws away original heart
        Coerce_To_Stable_Antiform(v);
        return COPY(v);
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
//          [~null~ ~void~ quoted? object! block!]
//  ]
//
DECLARE_NATIVE(lazy)
{
    INCLUDE_PARAMS_OF_LAZY;

    Value* v = ARG(object);
    if (Is_Void(v))
        return VOID;
    if (Is_Nulled(v))
        return nullptr;

    if (Is_Quoted(v))
        return Unquotify(Copy_Cell(OUT, v), 1);

    if (Is_Block(v)) {
        if (rebRunThrows(cast(Value*, OUT), Canon(MAKE), Canon(OBJECT_X), v))
            return THROWN;
    }
    else
        Copy_Cell(OUT, v);

    assert(Is_Object(OUT));
    return Coerce_To_Unstable_Antiform(OUT);;
}


// 1. In REDUCE, :PREDICATE functions are offered things like nihil and void
//    if they can accept them (which META can).  But COMMA! antiforms that
//    result from evaluating commas are -not- offered to any predicates.  This
//    is by design, so we get:
//
//        >> pack [1 + 2, comment "hi", if null [1020]]
//        == ~[3 ~[]~ ']
//
INLINE bool Pack_Native_Core_Throws(
    Sink(Atom) out,
    const Value* block,
    const Value* predicate
){
    if (Is_The_Block(block)) {
        StackIndex base = TOP_INDEX;

        const Element* tail;
        const Element* at = Cell_List_At(&tail, block);
        for (; at != tail; ++at)
            Copy_Meta_Cell(PUSH(), at);

        Init_Pack(out, Pop_Source_From_Stack(base));
        return false;
    }

    assert(Is_Block(block));

    if (rebRunThrows(
        cast(Value*, out),  // output cell
        Canon(QUASI), "reduce:predicate",  // commas excluded by :PREDICATE [1]
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
//          [pack?]
//      block "Reduce if plain BLOCK!, not if THE-BLOCK!"
//          [<maybe> the-block! block!]
//  ]
//
DECLARE_NATIVE(pack)
//
// 1. Using the predicate META means that raised errors aren't tolerated in
//    the main pack routine.  You have to use PACK*, which uses META* instead.
//
//        https://forum.rebol.info/t/2206
{
    INCLUDE_PARAMS_OF_PACK;

    Element* block = cast(Element*, ARG(block));

    if (Pack_Native_Core_Throws(OUT, block, Lib(META)))  // no raised [1]
        return THROWN;
    return OUT;
}


//
//  /pack*: native [
//
//  "Create a pack of arguments from a list, raised errors okay (or see PACK)"
//
//      return: "Antiform of BLOCK!"
//          [pack?]
//      block "Reduce if plain BLOCK!, not if THE-BLOCK!"
//          [<maybe> the-block! block!]
//  ]
//
DECLARE_NATIVE(pack_p)
//
// 1. Using the predicate META* means that raised errors will be tolerated
//    by PACK*, whereas PACK does not.
//
//        https://forum.rebol.info/t/2206
{
    INCLUDE_PARAMS_OF_PACK_P;

    Element* block = cast(Element*, ARG(block));

    if (Pack_Native_Core_Throws(OUT, block, Lib(META_P)))  // raise ok [1]
        return THROWN;
    return OUT;
}


//
//  /splice?: native:intrinsic [
//
//  "Tells you if argument is a splice (antiform group)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(splice_q)
{
    INCLUDE_PARAMS_OF_SPLICE_Q;

    return Init_Logic(OUT, Is_Splice(ARG_1));
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
DECLARE_NATIVE(lazy_q)
{
    INCLUDE_PARAMS_OF_LAZY_Q;

    Element* meta = cast(Element*, ARG_1);
    return Init_Logic(OUT, Is_Meta_Of_Lazy(meta));
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
DECLARE_NATIVE(pack_q)
{
    INCLUDE_PARAMS_OF_PACK_Q;

    Element* meta = cast(Element*, ARG_1);
    return Init_Logic(OUT, Is_Meta_Of_Pack(meta));
}


//
//  /keyword?: native:intrinsic [
//
//  "Tells you if argument is an antiform word, reserved for special purposes"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(keyword_q)
{
    INCLUDE_PARAMS_OF_KEYWORD_Q;

    return Init_Logic(OUT, Is_Keyword(ARG_1));
}


//
//  /action?: native:intrinsic [
//
//  "Tells you if argument is an action (antiform frame)"
//
//      return: [logic?]
//      value
//  ]
//
DECLARE_NATIVE(action_q)
{
    INCLUDE_PARAMS_OF_ACTION_Q;

    return Init_Logic(OUT, Is_Action(ARG_1));
}


//
//  /runs: native:intrinsic [
//
//  "Make frames run when fetched through word access"
//
//      return: [action?]
//      frame [<unrun> frame! action?]
//  ]
//
DECLARE_NATIVE(runs)
//
// 1. In order for a frame to properly report answers to things like
//    `parameters of`, the specializations have to be committed to...because
//    until that commitment, the frame hasn't hidden the parameters on its
//    public interface that are specialized.  At the moment, this means
//    making a copy.
{
    INCLUDE_PARAMS_OF_RUNS;

    Value* frame = ARG_1;

    if (Is_Frame_Details(frame)) {
        Coerce_To_Stable_Antiform(frame);
        return COPY(frame);
    }

    Phase* specialized = Make_Action(
        Varlist_Array(Cell_Varlist(frame)),
        nullptr,
        &Specializer_Dispatcher,
        IDX_SPECIALIZER_MAX  // details array capacity
    );

    return Init_Action(OUT, specialized, VAL_FRAME_LABEL(frame), UNBOUND);
}


//
//  /unrun: native:intrinsic [
//
//  "Give back a frame! for action? input"
//
//      return: [frame!]
//      action [<maybe> frame! action?]
//  ]
//
DECLARE_NATIVE(unrun)
{
    INCLUDE_PARAMS_OF_UNRUN;

    Value* action = ARG_1;  // may or may not be antiform
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
DECLARE_NATIVE(maybe)
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

    Value* v = ARG_1;

    if (Is_Void(v))
        return Init_Void(OUT);  // passthru

    if (Is_Nulled(v))
        return Init_Void(OUT);  // main purpose of function: NULL => VOID

    return COPY(v);  // passthru
}


//
//  /noquote: native:intrinsic [
//
//  "Removes all levels of quoting from a quoted value"
//
//      return: [element?]
//      element [<maybe> element?]
//  ]
//
DECLARE_NATIVE(noquote)
{
    INCLUDE_PARAMS_OF_NOQUOTE;

    Element* e = cast(Element*, ARG_1);
    QUOTE_BYTE(e) = NOQUOTE_1;
    return COPY(e);
}
