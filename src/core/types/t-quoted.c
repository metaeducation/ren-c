//
//  file: %t-quoted.c
//  summary: "QUOTED? datatype that acts as container for unquoted elements"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//  the: native [
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

    if (Bool_ARG(SOFT) and Is_Soft_Escapable_Group(v)) {
        if (Eval_Any_List_At_Throws(OUT, v, SPECIFIED))
            return THROWN;
        return OUT;
    }

    Copy_Cell(OUT, v);

    return OUT;
}


//
//  just: native [
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
//  quote: native [
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
    REBINT depth = Bool_ARG(DEPTH) ? VAL_INT32(ARG(DEPTH)) : 1;

    if (depth < 0)
        return PANIC(PARAM(DEPTH));

    Quotify_Depth(e, depth);
    return COPY(e);
}


//
//  unquote: native [
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

    Count depth = (Bool_ARG(DEPTH) ? VAL_INT32(ARG(DEPTH)) : 1);

    if (depth < 0)
        return PANIC(PARAM(DEPTH));

    if (depth > Quotes_Of(v))
        return PANIC("Value not quoted enough for unquote depth requested");

    return Unquotify_Depth(Copy_Cell(OUT, v), depth);
}


//
//  quasi: native [
//
//  "Constructs a quasi form of the evaluated argument (if legal)"
//
//      return: "Raises an error if type cannot make the quasiform"
//          [quasiform! error!]
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
        if (Bool_ARG(PASS))
            return COPY(elem);
        return PANIC("Use QUASI:PASS if QUASI argument is already a quasiform");
    }

    Element* out = Copy_Cell(OUT, elem);

    Option(Error*) e = Trap_Coerce_To_Quasiform(out);
    if (e)
        return FAIL(unwrap e);  // RAISE so (try quasi ':foo:) gives null

    return OUT;
}


//
//  unquasi: native [
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
//  lift: native:intrinsic [
//
//  "antiforms -> quasiforms, adds a quote to rest (behavior of ^^)"
//
//      return: "Keywords and plain forms if :LITE, plain ERROR! ok if :EXCEPT"
//          [quoted! quasiform! keyword! element? warning!]
//      ^atom
//      :lite "Make plain forms vs. quasi, and pass thru keywords like ~null~"
//      :except "If argument is antiform ERROR!, give back as plain ERROR!"
//  ]
//
DECLARE_NATIVE(LIFT)
//
// 1. Most code has to go through Coerce_To_Antiform()...even code that has
//    a quasiform in its hand (as not all quasiforms can be antiforms).  But
//    ^META parameters are guaranteed to be things that were validated as
//    antiforms.
{
    INCLUDE_PARAMS_OF_LIFT;

    Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    if (Get_Level_Flag(LEVEL, DISPATCHING_INTRINSIC))  // intrinsic shortcut
        return COPY(Liftify(atom));

    if (Is_Error(atom)) {
        if (not Bool_ARG(EXCEPT))
            return PANIC(Cell_Error(atom));

        LIFT_BYTE(atom) = NOQUOTE_1;
        return COPY(atom);  // plain WARNING!
    }

    if (
        Bool_ARG(LITE)  // LIFT:LITE handles quasiforms specially
        and Is_Antiform(atom)
    ){
        if (Is_Light_Null(atom) or Is_Void(atom))
            return COPY(atom);  // ^META valid [1]

        LIFT_BYTE(atom) = NOQUOTE_1;  // META:LITE gives plain for the rest
        return COPY(atom);
    }

    return COPY(Liftify(atom));
}


//
//  lift*: native:intrinsic [
//
//  "LIFT operator that works on any value (errors, packs, ghosts, etc.)"
//
//      return: [quoted! quasiform!]
//      ^atom
//  ]
//
DECLARE_NATIVE(LIFT_P)
{
    INCLUDE_PARAMS_OF_LIFT_P;

    Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    return COPY(Liftify(atom));
}


//
//  unlift: native:intrinsic [
//
//  "Variant of UNQUOTE that also accepts quasiforms to make antiforms"
//
//      return: [any-atom?]
//      ^value "Can be plain or antiform like NULL or VOID if :LITE"
//          [null? void? element? quoted! quasiform!]
//      :lite "Pass thru NULL and VOID antiforms as-is"
//  ]
//
DECLARE_NATIVE(UNLIFT)
{
    INCLUDE_PARAMS_OF_UNLIFT;

    Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    if (Get_Level_Flag(LEVEL, DISPATCHING_INTRINSIC)) {  // intrinsic shortcut
        if (not Any_Lifted(atom))
            return PANIC("Plain UNLIFT only accepts quasiforms and quoteds");
        return COPY(Unliftify_Undecayed(atom));
    }

    if (Is_Antiform(atom)) {
        assert(Is_Void(atom) or Is_Light_Null(atom));
        if (not Bool_ARG(LITE))
            return PANIC("UNLIFT only accepts NULL or VOID if :LITE");
        return COPY(atom);  // pass through as-is
    }

    if (LIFT_BYTE(atom) == NOQUOTE_1) {
        if (not Bool_ARG(LITE))
            return PANIC("UNLIFT only takes non quoted/quasi things if :LITE");

        Copy_Cell(OUT, atom);

        Option(Error*) e = Trap_Coerce_To_Antiform(OUT);
        if (e)
            return PANIC(unwrap e);

        return OUT;
    }

    if (LIFT_BYTE(atom) == QUASIFORM_2 and Bool_ARG(LITE))
        return PANIC(
            "UNLIFT:LITE does not accept quasiforms (plain forms are meta)"
        );

    return COPY(Unliftify_Undecayed(atom));  // quoted or quasi
}


//
//  unlift*: native [
//
//  "Variant of UNLIFT that can synthesize any atom (error, pack, ghost...)"
//
//      return: [any-atom?]
//      lifted [quoted! quasiform?]
//  ]
//
DECLARE_NATIVE(UNLIFT_P)
{
    INCLUDE_PARAMS_OF_UNLIFT_P;

    Copy_Cell(OUT, ARG(LIFTED));
    return Unliftify_Undecayed(OUT);
}


//
//  antiform?: native:intrinsic [
//
//  "Tells you whether argument is a stable or unstable antiform"
//
//      return: [logic?]
//      ^atom
//      :type
//  ]
//
DECLARE_NATIVE(ANTIFORM_Q)
//
// !!! This can be deceptive, in the sense that you could ask if something
// like an antiform pack is an antiform, and it will say yes...but then
// another routine like integer? might say it's an integer.  Be aware.
//
// 1. If you're not running as an intrinsic, then the rules for immutable
//    arguments don't apply...the frame got its own copy of the thing being
//    typechecked so it can be modified.
{
    INCLUDE_PARAMS_OF_ANTIFORM_Q;

    const Atom* atom = Intrinsic_Typechecker_Atom_ARG(LEVEL);

    if (Get_Level_Flag(LEVEL, DISPATCHING_INTRINSIC))  // intrinsic shortcut
        return LOGIC(Is_Antiform(atom));

    if (not Bool_ARG(TYPE))
        return LOGIC(Is_Antiform(atom));

    Value* datatype = Decay_If_Unstable(m_cast(Atom*, atom));  // mutable [1]

    if (not Is_Datatype(datatype))
        return PANIC("ANTIFORM?:TYPE only accepts DATATYPE!");

    Option(Type) type = Cell_Datatype_Type(datatype);

    if (u_cast(Byte, type) > u_cast(Byte, MAX_TYPE_ELEMENT))
        return LOGIC(true);

    return LOGIC(false);
}


//
//  anti: native [
//
//  "Give the antiform of the plain argument (like UNMETA QUASI)"
//
//      return: [antiform?]
//      element "Any non-QUOTED!, non-QUASI value"
//          [fundamental?]
//  ]
//
DECLARE_NATIVE(ANTI)
{
    INCLUDE_PARAMS_OF_ANTI;

    Element* elem = Element_ARG(ELEMENT);

    Copy_Cell(OUT, elem);
    Option(Error*) e = Trap_Coerce_To_Antiform(OUT);
    if (e)
        return PANIC(unwrap e);

    return OUT;
}


//
//  unanti: native:intrinsic [
//
//  "Give the plain form of the antiform argument"
//
//      return: [plain?]
//      ^antiform [antiform?]
//  ]
//
DECLARE_NATIVE(UNANTI)
{
    INCLUDE_PARAMS_OF_UNANTI;

    Atom* atom = Intrinsic_Atom_ARG(LEVEL);
    LIFT_BYTE(atom) = NOQUOTE_1;  // turn to plain form

    return COPY(Known_Element(atom));
}


//
//  spread: native [
//
//  "Make block arguments splice"
//
//      return: "Antiform of GROUP! or unquoted value (pass null and void)"
//          [null? element? splice!]
//      value [<opt-out> blank? any-list? quasiform!]  ; see [1] [2]
//  ]
//
DECLARE_NATIVE(SPREAD)
//
// !!! The name SPREAD is being chosen because it is more uncommon than splice,
// and there is no particular contention for its design.  SPLICE may be a more
// complex operation.
//
// 1. Generally speaking, functions are not supposed to conflate quasiforms
//    with their antiforms.  But it seems like being willing to DEGRADE a
//    ~[]~ or a ~null~ here instead of erroring helps more than it hurts.
//    Should it turn out to be bad for some reason, this might be dropped.
//
// 2. BLANK? is considered EMPTY? and hence legal to use with spread, though
//    it is already a splice.  This may suggest in general that spreading a
//    splice should be a no-op, but more investigation is needed.
{
    INCLUDE_PARAMS_OF_SPREAD;

    Value* v = ARG(VALUE);

    if (Any_List(v)) {  // most common case
        Copy_Cell(OUT, v);
        HEART_BYTE(OUT) = TYPE_GROUP;  // throws away original heart

        Option(Error*) e = Trap_Coerce_To_Antiform(OUT);
        assert(not e);
        UNUSED(e);
        assert(Is_Atom_Splice(OUT));
        return OUT;
    }

    if (Is_Blank(v))
        return VOID;  // immutable empty array makes problems for GLOM [3]

    if (Is_Nulled(v) or Is_Quasi_Null(v))  // quasi ok [2]
        return Init_Nulled(OUT);  // pass through [1]

    return PANIC(PARAM(VALUE));
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
    if (Is_Pinned_Form_Of(BLOCK, block)) {  // pack @[1 + 2] -> ~['1 '+ '2']~
        const Element* tail;
        const Element* at = Cell_List_At(&tail, block);

        Length len = tail - at;
        Source* a = Make_Source_Managed(len);  // same size array
        Set_Flex_Len(a, len);
        Element *dest = Array_Head(a);

        for (; at != tail; ++at, ++dest)
            Copy_Lifted_Cell(dest, at);

        Init_Pack(out, a);
        return false;
    }

    assert(Is_Block(block));

    if (rebRunThrows(
        u_cast(Init(Value), out),
        CANON(QUASI), "reduce:predicate",  // commas excluded by :PREDICATE [1]
            rebQ(block), rebQ(predicate)
    )){
        return true;
    }

    Unliftify_Undecayed(out);
    return false;
}


//
//  pack: native [
//
//  "Create a pack of arguments from a list, no errors (see PACK*)"
//
//      return: "Antiform of BLOCK!"
//          [pack!]
//      block "Reduce if plain BLOCK!, don't if @BLOCK!"
//          [<opt-out> block! @block!]
//  ]
//
DECLARE_NATIVE(PACK)
//
// 1. Using the predicate META means that error antiforms aren't tolerated in
//    the main pack routine.  You have to use PACK*, which uses META* instead.
//
//        https://forum.rebol.info/t/2206
{
    INCLUDE_PARAMS_OF_PACK;

    Element* block = Element_ARG(BLOCK);

    if (Pack_Native_Core_Throws(OUT, block, LIB(LIFT)))  // no errors [1]
        return THROWN;
    return OUT;
}


//
//  pack*: native [
//
//  "Create a pack of arguments from a list, error antiforms okay"
//
//      return: "Antiform of BLOCK!"
//          [pack!]
//      block "Reduce if plain BLOCK!, don't if @BLOCK!"
//          [<opt-out> block! @block!]
//  ]
//
DECLARE_NATIVE(PACK_P)
//
// 1. Using the predicate LIFT* means that errors will be tolerated by PACK*,
//    whereas PACK does not.
//
//        https://forum.rebol.info/t/2206
{
    INCLUDE_PARAMS_OF_PACK_P;

    Element* block = Element_ARG(BLOCK);

    if (Pack_Native_Core_Throws(OUT, block, LIB(LIFT_P)))  // fail ok [1]
        return THROWN;
    return OUT;
}


//
//  pack?: native:intrinsic [
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

    const Atom* atom = Intrinsic_Typechecker_Atom_ARG(LEVEL);

    return LOGIC(Is_Pack(atom));
}


//
//  runs: native [
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
// 2. This is mostly a type checked synonym for `anti`, with the exception
//    that it sets the "unsurprising" flag on the result.  This means you
//    can directly assign the result of RUNS e.g. (x: runs frame) and you will
//    not be required to say (x: ^ runs frame)
{
    INCLUDE_PARAMS_OF_RUNS;

    Value* frame = ARG(FRAME);
    if (Is_Action(frame))  // already antiform, no need to pay for coercion [1]
        return COPY(frame);

    Copy_Cell(OUT, frame);

    Option(Error*) e = Trap_Coerce_To_Antiform(OUT);  // same code as anti [2]
    if (e)
        return PANIC(unwrap e);

    return UNSURPRISING(OUT);
}


//
//  unrun: native [
//
//  "Give back a frame! for action! input"
//
//      return: [frame!]
//      action [<opt-out> frame! action!]
//  ]
//
DECLARE_NATIVE(UNRUN)
{
    INCLUDE_PARAMS_OF_UNRUN;

    Value* action = ARG(ACTION);  // may or may not be antiform
    LIFT_BYTE(action) = NOQUOTE_1;  // now it's known to not be antiform
    return COPY(action);
}


// We want OPT and ? to be intrinsics, so the strictness is not controlled
// with a refinement.  Share the code.
//
static Bounce Optional_Intrinsic_Native_Core(Level* level_, bool veto) {
    Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    if (Is_Error(atom))
        return COPY(atom);  // will pass thru vetos, and other errors

    if (Is_Void(atom))
        goto opting_out;  // void => void in OPT, or void => veto in OPT:VETO

    if (Is_Ghost(atom))
        return PANIC("Cannot OPT a GHOST!");  // !!! Should we opt out ghosts?

  decay_if_unstable: {

    Copy_Cell(OUT, atom);
    Value* out = Decay_If_Unstable(OUT);

    if (Is_Nulled(out))
        goto opting_out;

    return out;

} opting_out: { //////////////////////////////////////////////////////////////

    return veto ? FAIL(Cell_Error(g_error_veto)) : VOID;
}}


//
//  optional: native:intrinsic [
//
//  "If argument is null, make it VOID (or VETO), else passthru"
//
//      return: [any-atom?]
//      ^atom "Decayed if pack"
//      :veto "If true, then return VETO instead of VOID"
//  ]
//
DECLARE_NATIVE(OPTIONAL)  // ususally used via its aliases of OPT or ?
{
    INCLUDE_PARAMS_OF_OPTIONAL;

    bool veto;
    if (Get_Level_Flag(LEVEL, DISPATCHING_INTRINSIC))
        veto = false;  // default in intrinsic dispatch to not light
    else
        veto = Bool_ARG(VETO);  // slower dispatch with frame + refinement

    return Optional_Intrinsic_Native_Core(LEVEL, veto);
}


//
//  optional-veto: native:intrinsic [
//
//  "If argument is null or error antiform make it VETO, else passthru"
//
//      return: [any-atom?]
//      ^atom "Decayed if pack"
//  ]
//
DECLARE_NATIVE(OPTIONAL_VETO)  // usually used via its alias of ?!
//
// This is functionally equivalent to OPTIONAL:VETO, but much faster to run
// because it's dispatched intrinsically.  (Plain OPT with no refinements
// is also dispatched intrinsically, but adding the refinement slows it down
// with CHAIN! calculations and requires building a FRAME!)
{
    INCLUDE_PARAMS_OF_OPTIONAL_VETO;

    bool veto = true;
    return Optional_Intrinsic_Native_Core(LEVEL, veto);
}


//
//  noquote: native:intrinsic [
//
//  "Removes all levels of quoting from a (potentially) quoted element"
//
//      return: [fundamental?]
//      element [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(NOQUOTE)
{
    INCLUDE_PARAMS_OF_NOQUOTE;

    Option(Bounce) b = Trap_Bounce_Opt_Out_Element_Intrinsic(OUT, LEVEL);
    if (b)
        return unwrap b;

    LIFT_BYTE(OUT) = NOQUOTE_1;
    return OUT;
}
