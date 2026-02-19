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
//  /the: native [
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

    Element* v = ARG(VALUE);

    if (ARG(SOFT) and Is_Soft_Escapable_Group(v)) {
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
//      return: [element?]
//      '@value [element?]
//  ]
//
DECLARE_NATIVE(JUST)
//
// Note: JUST:SOFT doesn't make any sense, it cannot evaluate without binding.
{
    INCLUDE_PARAMS_OF_JUST;

    Element* v = ARG(VALUE);

    return COPY_TO_OUT(v);
}


//
//  /quote: native [
//
//  "Constructs a quoted form of the evaluated argument"
//
//      return: [
//          quoted!     "will be quoted unless depth = 0"
//          element?    "if depth = 0, may give a non-quoted result"
//          <null>      "if input is void"
//      ]
//      value [element?]
//      :depth "Number of quoting levels to apply (default 1)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(QUOTE)
{
    INCLUDE_PARAMS_OF_QUOTE;

    Element* e = ARG(VALUE);
    Count depth = ARG(DEPTH) ? VAL_INT32(unwrap ARG(DEPTH)) : 1;

    if (depth < 0)
        panic (PARAM(DEPTH));

    Quotify_Depth(e, depth);
    return COPY_TO_OUT(e);
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

    Element* v = ARG(VALUE);

    Count depth = ARG(DEPTH) ? VAL_INT32(unwrap ARG(DEPTH)) : 1;

    if (depth < 0)
        panic (PARAM(DEPTH));

    if (depth > Quotes_Of(v))
        panic ("Value not quoted enough for unquote depth requested");

    return Unquotify_Depth(Copy_Cell(OUT, v), depth);
}


//
//  /quasi: native [
//
//  "Constructs quasiform of VALUE (if legal for type, otherwise failure)"
//
//      return: [quasiform! failure!]
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
        if (ARG(PASS))
            return COPY_TO_OUT(elem);
        panic ("Use QUASI:PASS if QUASI argument is already a quasiform");
    }

    Element* out = Copy_Cell(OUT, elem);

    trap (  // use TRAP vs. PANIC, such that (try quasi ':foo:) is null
      Coerce_To_Quasiform(out)
    );
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
    return COPY_TO_OUT(Unquasify(quasi));
}


//
//  /lift: pure native:intrinsic [
//
//  "antiforms -> quasiforms, adds a quote to rest"
//
//      return: [quoted! quasiform!]
//      ^value '[<veto> any-value?]  ; LIFT lifts ~(veto)~, too
//  ]
//
DECLARE_NATIVE(LIFT)
{
    INCLUDE_PARAMS_OF_LIFT;

    Value* v = ARG(VALUE);

    return Copy_Lifted_Cell(OUT, v);
}


//
//  /lift*: pure native:intrinsic [
//
//  "Variant of LIFT that only lifts THEN-reactive values"
//
//      return: [
//          quoted! quasiform! "lifted forms"
//          void! <null> failure! hot-potato? "passed through"
//      ]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(LIFT_P)
{
    INCLUDE_PARAMS_OF_LIFT_P;

    Value* v = ARG(VALUE);

    if (Is_Failure(v))
        return COPY_TO_OUT(v);

    if (Is_Void(v))
        return VOID_OUT;

    if (Is_Light_Null(v))
        return NULL_OUT;

    return Copy_Lifted_Cell(OUT, v);
}


//
//  /unlift: pure native:intrinsic [
//
//  "Variant of UNQUOTE that also accepts quasiforms to make antiforms"
//
//      return: [any-value?]
//      ^value '[quoted! quasiform!]  ; REVIEW: decay PACK!?
//  ]
//
DECLARE_NATIVE(UNLIFT)
{
    INCLUDE_PARAMS_OF_UNLIFT;

    Value* v = Possibly_Unstable(Unchecked_ARG(VALUE));

    if (not Any_Lifted(v))
        panic (Error_Bad_Intrinsic_Arg_1(LEVEL));

    return UNLIFT_TO_OUT(As_Element(v));  // quoted or quasi
}


//
//  /unlift*: pure native:intrinsic [
//
//  "Variant of UNLIFT that only unlifts THEN-reactive values"
//
//      return: [any-value?]
//      ^value '[<null> void! quoted! quasiform!]
//  ]
//
DECLARE_NATIVE(UNLIFT_P)
//
// 1. While the implementation of UNLIFT is trivial, avoiding the duplication
//    of logic for the intrinsic still seems worthwhile (?)
{
    INCLUDE_PARAMS_OF_UNLIFT_P;

    Value* v = Possibly_Unstable(Unchecked_ARG(VALUE));

    if (Is_Void(v))
        return VOID_OUT;

    if (Is_Light_Null(v))
        return NULL_OUT;

    return Apply_Cfunc(NATIVE_CFUNC(UNLIFT), LEVEL);
}


//
//  /antiform?: pure native:intrinsic [
//
//  "Tells you whether argument is a stable or unstable antiform"
//
//      return: [logic!]
//      ^value '[<veto> any-value?]  ; want to report ~(veto)~ antiform
//      :type
//  ]
//
DECLARE_NATIVE(ANTIFORM_Q)
//
// !!! This can be deceptive, in the sense that you could ask if something
// like an antiform pack is an antiform, and it will say yes...but then
// another routine like integer? might say it's an integer.  Be aware.
{
    INCLUDE_PARAMS_OF_ANTIFORM_Q;

    Value* v = ARG(VALUE);

    if (Get_Level_Flag(LEVEL, DISPATCHING_INTRINSIC))  // intrinsic shortcut
        return LOGIC_OUT(Is_Antiform(v));

    if (not ARG(TYPE))
        return LOGIC_OUT(Is_Antiform(v));

    require (
      Stable* datatype = Decay_If_Unstable(v)
    );

    if (not Is_Datatype(datatype))
        panic ("ANTIFORM?:TYPE only accepts DATATYPE!");

    Option(Type) type = Datatype_Type(datatype);

    if (i_cast(TypeByte, type) > MAX_TYPEBYTE_ELEMENT)
        return LOGIC_OUT(true);

    return LOGIC_OUT(false);
}


//
//  /anti: native [
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
//  /unanti: native:intrinsic [
//
//  "Give the plain form of the antiform argument"
//
//      return: [plain?]
//      ^value '[antiform?]
//  ]
//
DECLARE_NATIVE(UNANTI)
{
    INCLUDE_PARAMS_OF_UNANTI;

    Value* v = Unchecked_ARG(VALUE);
    LIFT_BYTE(v) = NOQUOTE_3;  // turn to plain form

    return COPY_TO_OUT(As_Element(v));
}


//
//  /spread: native [
//
//  "Turn lists into SPLICE! antiforms"
//
//      return: [
//          splice! "note that splices carry no bindings"
//          <null>
//      ]
//      value [
//          any-list? "plain lists become splices"
//          <opt> "void gives empty splice"
//      ]
//  ]
//
DECLARE_NATIVE(SPREAD)
//
// SPREAD is chosen as the verb instead of SPLICE, because SPLICE! is the
// "noun" for a group antiform representing a splice.
{
    INCLUDE_PARAMS_OF_SPREAD;

    if (not ARG(VALUE))
        return Init_None(OUT);

    Stable* v = unwrap ARG(VALUE);

    if (Any_List(v))  // most common case
        return COPY_TO_OUT(Splicify(v));

    panic (PARAM(VALUE));
}


//
//  /bedrock?: native:intrinsic [
//
//  "Is VALUE 'unlifted bedrock' (antiform PACK! containing one unlifted item)"
//
//      return: [logic!]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(BEDROCK_Q)
{
    INCLUDE_PARAMS_OF_BEDROCK_Q;

    Value* v = ARG(VALUE);

    return LOGIC_OUT(Is_Undecayed_Bedrock(v));
}


//
//  /hot-potato?: pure native:intrinsic [
//
//  "Tells you if argument is an undecayable PACK!, with one unlifted WORD!"
//
//      return: [logic!]
//      ^value '[<veto> any-value?]
//  ]
//
DECLARE_NATIVE(HOT_POTATO_Q)
{
    INCLUDE_PARAMS_OF_HOT_POTATO_Q;

    Value* v = ARG(VALUE);

    return LOGIC_OUT(Is_Hot_Potato(v));
}


//
//  /alias: native [
//
//  "Make a dual state for proxying SET and GET to another variable"
//
//      return: [pack!]  ; !!! actually a dual state, not a "normal pack!"
//      var [word! tuple!]
//  ]
//
DECLARE_NATIVE(ALIAS)
{
    INCLUDE_PARAMS_OF_ALIAS;

    Element* var = Element_ARG(VAR);
    assert(Is_Word(var) or Is_Tuple(var));
    KIND_BYTE(var) = Kind_From_Sigil_And_Heart(
        SIGIL_META, unwrap Heart_Of(var)
    );

    Source* a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
    Copy_Cell(Array_Head(a), var);

    return Init_Pack(OUT, a);
}


//
//  /runs: native [
//
//  "Turn frame or action into antiform in PACK!, allows SET-WORD! assignment"
//
//      return: [action!]
//      ^value [frame! action!]
//  ]
//
DECLARE_NATIVE(RUNS)
{
    INCLUDE_PARAMS_OF_RUNS;

    Copy_Cell(OUT, ARG(VALUE));

    if (Is_Action(OUT))
        return OUT;

    return Activate_Frame(OUT);
}


//
//  /unrun: native [
//
//  "Give back a frame! for action! input"
//
//      return: [frame!]
//      action [frame!]
//  ]
//
DECLARE_NATIVE(UNRUN)
{
    INCLUDE_PARAMS_OF_UNRUN;

    Stable* action = ARG(ACTION);  // decayed on input
    return COPY_TO_OUT(action);
}


//
//  /disarm: native [
//
//  "Give back an ERROR! for FAILURE! input"
//
//      return: [error!]
//      ^error [failure!]
//  ]
//
DECLARE_NATIVE(DISARM)
{
    INCLUDE_PARAMS_OF_DISARM;

    Value* v = ARG(ERROR);

    Copy_Cell(OUT, v);
    return Disarm_Failure(OUT);
}


//
//  /unsplice: native [
//
//  "Give back a block! for splice! input"
//
//      return: [block!]  ; BLOCK! seems more generically desired than GROUP!
//      splice [splice!]
//  ]
//
DECLARE_NATIVE(UNSPLICE)
{
    INCLUDE_PARAMS_OF_UNSPLICE;

    Stable* splice = ARG(SPLICE);
    LIFT_BYTE(splice) = NOQUOTE_3;
    KIND_BYTE(splice) = TYPE_BLOCK;
    return COPY_TO_OUT(splice);
}


//
//  /noquote: native:intrinsic [
//
//  "Removes all levels of quoting from a (potentially) quoted element"
//
//      return: [fundamental?]
//      value '[element?]
//  ]
//
DECLARE_NATIVE(NOQUOTE)
{
    INCLUDE_PARAMS_OF_NOQUOTE;

    require (
      Element* v = opt Typecheck_Element_Intrinsic_Arg(LEVEL)
    );
    if (not v)
        return NULL_OUT;

    Copy_Cell(OUT, v);
    LIFT_BYTE(OUT) = NOQUOTE_3;
    return OUT;
}
