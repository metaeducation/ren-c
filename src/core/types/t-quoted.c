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
//      return: [
//          element? "if not :SOFT, input value verbatim"
//          any-stable?  "if :SOFT and input is evaluated"
//      ]
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
//      return: [element?]
//      'value [element?]
//  ]
//
DECLARE_NATIVE(JUST)
//
// Note: JUST:SOFT doesn't make any sense, it cannot evaluate without binding.
{
    INCLUDE_PARAMS_OF_JUST;

    Element* quoted = Element_ARG(VALUE);
    return COPY(quoted);
}


//
//  quote: native [
//
//  "Constructs a quoted form of the evaluated argument"
//
//      return: [
//          quoted!     "will be quoted unless depth = 0"
//          element?    "if depth = 0, may give a non-quoted result"
//          <null>      "if input is void"
//      ]
//      value [<opt-out> element?]
//      :depth "Number of quoting levels to apply (default 1)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(QUOTE)
{
    INCLUDE_PARAMS_OF_QUOTE;

    Element* e = Element_ARG(VALUE);
    REBINT depth = Bool_ARG(DEPTH) ? VAL_INT32(ARG(DEPTH)) : 1;

    if (depth < 0)
        panic (PARAM(DEPTH));

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
        panic (PARAM(DEPTH));

    if (depth > Quotes_Of(v))
        panic ("Value not quoted enough for unquote depth requested");

    return Unquotify_Depth(Copy_Cell(OUT, v), depth);
}


//
//  quasi: native [
//
//  "Constructs quasiform of VALUE (if legal for type, otherwise error)"
//
//      return: [quasiform! error!]
//      value [fundamental? quasiform!]
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

    Element* elem = Element_ARG(VALUE);

    if (Is_Quasiform(elem)) {
        if (Bool_ARG(PASS))
            return COPY(elem);
        panic ("Use QUASI:PASS if QUASI argument is already a quasiform");
    }

    Element* out = Copy_Cell(OUT, elem);

    trap (  // use TRAP vs. PANIC, such that (try quasi ':foo:) is null
      Coerce_To_Quasiform(out)
    );
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
//  "antiforms -> quasiforms, adds a quote to rest"
//
//      return: [
//          quoted! quasiform! "lifted forms"
//          keyword! element? warning!  "Keywords and plain forms if :LITE"
//      ]
//      ^value [any-value?]
//      :lite "Make plain forms vs. quasi, and pass thru keywords like ~null~"
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

    if (
        Bool_ARG(LITE)  // LIFT:LITE handles quasiforms specially
        and Is_Antiform(atom)
    ){
        if (Is_Error(atom))
            panic (Cell_Error(atom));  // conservative... should it passthru?

        if (Is_Light_Null(atom) or Is_Void(atom))
            return COPY(atom);  // ^META valid [1]

        LIFT_BYTE(atom) = NOQUOTE_2;  // META:LITE gives plain for the rest
        return COPY(atom);
    }

    return COPY(Liftify(atom));
}


//
//  unlift: native:intrinsic [
//
//  "Variant of UNQUOTE that also accepts quasiforms to make antiforms"
//
//      return: [any-value?]
//      ^value "Can be plain or antiform like NULL or VOID if :LITE"
//          [<null> <void> element? quoted! quasiform!]
//      :lite "Pass thru NULL and VOID antiforms as-is"
//  ]
//
DECLARE_NATIVE(UNLIFT)
{
    INCLUDE_PARAMS_OF_UNLIFT;

    Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    if (Get_Level_Flag(LEVEL, DISPATCHING_INTRINSIC)) {  // intrinsic shortcut
        if (not Any_Lifted(atom))
            panic ("Plain UNLIFT only accepts quasiforms and quoteds");
        require (
          Unliftify_Undecayed(atom)
        );
        return COPY(atom);
    }

    if (Is_Antiform(atom)) {
        assert(Is_Void(atom) or Is_Light_Null(atom));
        if (not Bool_ARG(LITE))
            panic ("UNLIFT only accepts NULL or VOID if :LITE");
        return COPY(atom);  // pass through as-is
    }

    if (LIFT_BYTE(atom) == NOQUOTE_2) {
        if (not Bool_ARG(LITE))
            panic ("UNLIFT only takes non quoted/quasi things if :LITE");

        Copy_Cell(OUT, atom);

        require (
          Coerce_To_Antiform(OUT)
        );
        return OUT;
    }

    if (LIFT_BYTE(atom) == QUASIFORM_3 and Bool_ARG(LITE))
        panic (
            "UNLIFT:LITE does not accept quasiforms (plain forms are meta)"
        );

    require (
      Unliftify_Undecayed(atom)
    );
    return COPY(atom);  // quoted or quasi
}



//
//  antiform?: native:intrinsic [
//
//  "Tells you whether argument is a stable or unstable antiform"
//
//      return: [logic?]
//      ^value [any-value?]
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

    require (  // mutable [1]
      Value* datatype = Decay_If_Unstable(m_cast(Atom*, atom))
    );

    if (not Is_Datatype(datatype))
        panic ("ANTIFORM?:TYPE only accepts DATATYPE!");

    Option(Type) type = Datatype_Type(datatype);

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
//      value "Any non-QUOTED!, non-QUASI value"
//          [fundamental?]
//  ]
//
DECLARE_NATIVE(ANTI)
{
    INCLUDE_PARAMS_OF_ANTI;

    Element* elem = Element_ARG(VALUE);

    Copy_Cell(OUT, elem);
    require (
      Coerce_To_Antiform(OUT)
    );
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
    LIFT_BYTE(atom) = NOQUOTE_2;  // turn to plain form

    return COPY(Known_Element(atom));
}


//
//  spread: native [
//
//  "Turn lists into SPLICE! antiforms"
//
//      return: [
//          splice! "note that splices carry no bindings"
//          <void> <null> "void and null pass through"
//      ]
//      ^value [
//          any-list? "plain lists become splices"
//          blank? "empty splices pass through as empty splice"  ; [1]
//          quasiform! "automatic DEGRADE quasiform lists to splice"  ; [2]
//          <void> <null> "void and null pass through"
//      ]
//  ]
//
DECLARE_NATIVE(SPREAD)
//
// SPREAD is chosen as the verb instead of SPLICE, because SPLICE! is the
// "noun" for a group antiform representing a splice.
//
// 1. BLANK? is considered EMPTY? and hence legal to use with spread, though
//    it is already a splice.  This may suggest in general that spreading a
//    splice should be a no-op, but more investigation is needed.
//
// 2. Generally speaking, functions are not supposed to conflate quasiforms
//    with their antiforms.  But it seems like being willing to DEGRADE a
//    ~[]~ or a ~null~ here instead of erroring helps more than it hurts.
//    Should it turn out to be bad for some reason, this might be dropped.
{
    INCLUDE_PARAMS_OF_SPREAD;

    if (Is_Void(Atom_ARG(VALUE)))
        return VOID;  // void is a no-op, so just pass it through

    Value* v = ARG(VALUE);

    if (Is_Nulled(v))
        return NULLED;

    if (Any_List(v))  // most common case
        return COPY(Splicify(v));

    if (Is_Blank(v))
        return VOID;  // immutable empty array makes problems for GLOM [3]

    if (Is_Nulled(v) or Is_Quasi_Null(v))  // quasi ok [2]
        return Init_Nulled(OUT);  // pass through [1]

    panic (PARAM(VALUE));
}


//
//  pack: native [
//
//  "Create a pack of arguments from a list"
//
//      return: [pack!]
//      block "Reduce if plain BLOCK!, don't if @BLOCK!"
//          [<opt-out> block! @block!]
//  ]
//
DECLARE_NATIVE(PACK)
//
// 1. In REDUCE, :PREDICATE functions are offered things like ghost and void
//    if they can accept them (which LIFT can).  But source-level COMMA! are
//    -not- offered to any predicates.  This is by design, so we get:
//
//        >> pack [1 + 2, comment "hi", opt null]
//        == \~['3 ~,~ ~[]~]~\  ; antiform (pack!)
//
// 2. Using LIFT as a predicate means error antiforms are tolerated; it is
//    expected that you IGNORE (vs. ELIDE) a PACK which contains errors, as
//    ordinary elisions (such as in multi-step evaluations) will complain:
//
//        https://rebol.metaeducation.com/t/2206
{
    INCLUDE_PARAMS_OF_PACK;

    Element* block = Element_ARG(BLOCK);

    if (Is_Pinned_Form_Of(BLOCK, block)) {  // pack @[1 + 2] -> ~['1 '+ '2']~
        const Element* tail;
        const Element* at = List_At(&tail, block);

        Length len = tail - at;
        Source* a = Make_Source_Managed(len);  // same size array
        Set_Flex_Len(a, len);
        Element *dest = Array_Head(a);

        for (; at != tail; ++at, ++dest)
            Copy_Lifted_Cell(dest, at);

        return Init_Pack(OUT, a);
    }

    assert(Is_Block(block));

    if (rebRunThrows(
        u_cast(Init(Value), SPARE),
        "reduce:predicate",  // commas excluded by :PREDICATE [1]
            rebQ(block), rebQ(LIB(LIFT))  // fail ok [2]
    )){
        return THROWN;
    }

    return Init_Pack(OUT, Cell_Array(Known_Stable(SPARE)));
}


//
//  pack?: native:intrinsic [
//
//  "Tells you if argument is a parameter pack (antiform block)"
//
//      return: [logic?]
//      ^value [any-value?]
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
//      frame [frame! action!]  ; !!! is allowing already action! a good idea?
//  ]
//
DECLARE_NATIVE(RUNS)
//
// This is mostly a type-checked synonym for `anti`, with the exception that
// the result is made "UNSURPRISING".
{
    INCLUDE_PARAMS_OF_RUNS;

    Value* frame = ARG(FRAME);

    if (Is_Action(frame))
        return UNSURPRISING(Copy_Cell(OUT, frame));

    Stably_Antiformize_Unbound_Fundamental(frame);
    assert(Is_Action(frame));

    return UNSURPRISING(Copy_Cell(OUT, frame));
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
    LIFT_BYTE(action) = NOQUOTE_2;  // now it's known to not be antiform
    return COPY(action);
}


//
//  disarm: native [
//
//  "Give back a warning! for error! input"
//
//      return: [warning!]
//      ^error [<opt-out> error!]
//  ]
//
DECLARE_NATIVE(DISARM)
{
    INCLUDE_PARAMS_OF_DISARM;

    Atom* error = Atom_ARG(ERROR);
    LIFT_BYTE(error) = NOQUOTE_2;
    return COPY(error);
}


//
//  unsplice: native [
//
//  "Give back a block! for splice! input"
//
//      return: [block!]  ; BLOCK! seems more generically desired than GROUP!
//      splice [<opt-out> splice!]
//  ]
//
DECLARE_NATIVE(UNSPLICE)
{
    INCLUDE_PARAMS_OF_UNSPLICE;

    Value* splice = ARG(SPLICE);
    LIFT_BYTE(splice) = NOQUOTE_2;
    KIND_BYTE(splice) = TYPE_BLOCK;
    return COPY(splice);
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
        panic ("Cannot OPT a GHOST!");  // !!! Should we opt out ghosts?

  decay_if_unstable: {

    Copy_Cell(OUT, atom);
    require (
      Value* out = Decay_If_Unstable(OUT)
    );
    if (Is_Nulled(out))
        goto opting_out;

    return out;

} opting_out: { //////////////////////////////////////////////////////////////

    if (veto)
        return fail (Cell_Error(g_error_veto));  // OPT:VETO

    return VOID;
}}


//
//  optional: native:intrinsic [
//
//  "If argument is null, make it VOID (or VETO), else passthru"
//
//      return: [any-value?]
//      ^value "Decayed if pack"
//          [<opt-out> any-value?]
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
//      return: [any-value?]
//      ^value "Decayed if pack"
//          [any-value?]
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
//      value [<opt-out> element?]
//  ]
//
DECLARE_NATIVE(NOQUOTE)
{
    INCLUDE_PARAMS_OF_NOQUOTE;

    require (
      Bounce b = Bounce_Opt_Out_Element_Intrinsic(OUT, LEVEL)
    );
    if (b != BOUNCE_GOOD_INTRINSIC_ARG)
        return b;

    LIFT_BYTE(OUT) = NOQUOTE_2;
    return OUT;
}
