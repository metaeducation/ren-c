//
//  file: %n-ghost.c
//  summary: "Native Functions for VOID! Datatype (COMMENT, ELIDE, etc.)"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// For a long time, vanishing functions were not implemented as natives, due
// to the desire to prove that they could be implemented in usermode.  But
// now that VOID! is well understood and simple to use (vs. being esoteric
// evaluator tricks on special infix functions), there's no reason not to
// just implement them as fast intrinsics.
//

#include "sys-core.h"


//
//  /ghost: vanishable native [  ; not non-discardable
//
//  "Generate VOID! (arity-0 COMMENT)"
//
//      return: [void!]
//  ]
//
DECLARE_NATIVE(GHOST)
{
    INCLUDE_PARAMS_OF_GHOST;

    return VOID_OUT;
}


//
//  /any-void?: native:intrinsic [
//
//  "Is VALUE a VOID! (antiform comma) or HEAVY VOID (empty pack!)"
//
//      return: [logic! failure!]
//      ^value '[any-value? failure!]
//  ]
//
DECLARE_NATIVE(ANY_VOID_Q)
{
    INCLUDE_PARAMS_OF_ANY_VOID_Q;

    Value* v = ARG(VALUE);

    if (Is_Failure(v))
        return COPY_TO_OUT(v);  // typical behavior for type tests on FAILURE!

    require (
      Ensure_No_Failures_Including_In_Packs(v)
    );

    return LOGIC_OUT(Any_Void(v));
}


//
//  /light-void?: native:intrinsic [
//
//  "Is VALUE specifically LIGHT VOID (blank! antiform)"
//
//      return: [logic!]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(LIGHT_VOID_Q)
{
    INCLUDE_PARAMS_OF_LIGHT_VOID_Q;

    Value* v = ARG(VALUE);

    // !!! what about FAILURE!?  Should this panic?

    return LOGIC_OUT(Is_Void(v));
}


//
//  /heavy-void?: native:intrinsic [
//
//  "Is VALUE specifically HEAVY VOID (empty pack!)"
//
//      return: [logic!]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(HEAVY_VOID_Q)
{
    INCLUDE_PARAMS_OF_HEAVY_VOID_Q;

    Value* v = ARG(VALUE);

    // !!! what about FAILURE!?  Should this panic?

    return LOGIC_OUT(Is_Heavy_Void(v));
}


//
//  /comment: vanishable native:intrinsic [
//
//  "Skip one element ahead, doing no evaluation (see also ELIDE)"
//
//      return: [void!]
//      @value "Literal to skip, (comment print -[x]-) disallowed"
//          '[any-list? any-utf8? blob! any-scalar?]
//  ]
//
DECLARE_NATIVE(COMMENT)
{
    INCLUDE_PARAMS_OF_COMMENT;

    Element* v = Unchecked_ARG(VALUE);

    if (not (Any_List(v) or Any_Utf8(v) or Is_Blob(v) or Any_Scalar(v)))
       panic (Error_Bad_Intrinsic_Arg_1(LEVEL));

    return VOID_OUT;
}


//
//  /elide: vanishable native:intrinsic [
//
//  "Argument evaluated, result discarded (not FAILURE!, or packs w/FAILURE!s)"
//
//      return: [void!]
//      ^value '[any-stable? pack! void!]
//  ]
//
DECLARE_NATIVE(ELIDE)
{
    INCLUDE_PARAMS_OF_ELIDE;

    Value* v = Possibly_Unstable(Unchecked_ARG(VALUE));

    require (
      Ensure_No_Failures_Including_In_Packs(v)
    );

    return VOID_OUT;
}


//
//  /ghostly: vanishable native:intrinsic [
//
//  "Argument is evaluative, but discarded if ANY-VOID?"
//
//      return: [any-value?]
//      ^value '[any-value?]  ; void! is passed through
//  ]
//
DECLARE_NATIVE(GHOSTLY)
{
    INCLUDE_PARAMS_OF_GHOSTLY;

    Value* v = ARG(VALUE);

    if (Any_Void(v))
        return VOID_OUT;

    return COPY_TO_OUT(v);  // trust piped FAILURE! etc. behaves appropriately
}


//
//  /ignore: native:intrinsic [  ; !!! should it be vanishable?
//
//  "Argument evaluated and discarded (even FAILURE! and undecayable packs)"
//
//      return: [void!]  ; TRASH! would cause errors in ANY/ALL
//      ^discarded '[any-value?]
//  ]
//
DECLARE_NATIVE(IGNORE)
{
    INCLUDE_PARAMS_OF_IGNORE;  // no ARG(DISCARDED), parameter is intrinsic

    return VOID_OUT;
}


//
//  /unvoid: native:intrinsic [  ; !!! Better name?
//
//  "If the argument is a VOID!, convert it to a HEAVY VOID, else passthru"
//
//      return: [any-value?]
//      ^value '[any-value?]
//  ]
//
DECLARE_NATIVE(UNVOID)
//
// Functions should be cautious about "leaking voids", as we want to limit
// the cases where expressions vanish some of the time and not others.
{
    INCLUDE_PARAMS_OF_UNVOID;

    Value* v = ARG(VALUE);

    if (Is_Void(v)) {
        Init_Heavy_Void(OUT);
        return BOUNCE_OUT;
    }

    return COPY_TO_OUT(v);
}
