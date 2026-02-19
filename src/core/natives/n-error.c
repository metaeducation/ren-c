//
//  file: %n-error.c
//  summary: "native functions for raising and trapping errors"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2022 Ren-C Open Source Contributors
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


#include "sys-core.h"


//
//  /try: native:intrinsic [
//
//  "Suppress PANIC escalation from PANIC!s and 'hot potatoes', return NULL"
//
//      return: [<null> any-value?]
//      ^value '[<veto> any-value? failure! hot-potato?]
//  ]
//
DECLARE_NATIVE(TRY)
//
// 1. The current idea is that you use DID if you are just looking for a
//    LOGIC! result to know something wasn't VOID!, NULL, FAILURE!, or a
//    hot potato.  So write (if did ...) instead of (if try ...).
//
//    This means there's less of a compelling case for TRY to turn voids into
//    nulls (although maybe it still should--waiting to see if there is a
//    truly motivating example for or against it).  For now it just passes
//    them through.
{
    INCLUDE_PARAMS_OF_TRY;

    Value* v = ARG(VALUE);

    if (Is_Failure(v) or Is_Hot_Potato(v))  // not counting Any_Void() [1]
        return NULL_OUT;

    return COPY_TO_OUT(v);  // !!! also tolerates other antiforms, should it?
}


//
//  /enrecover: native [
//
//  "Sandbox to intercept failures; FAILURE! -> ERROR! else lifted result"
//
//      return: [error! quoted! quasiform!]
//      code "Code to sandbox, intercept errors at any depth (including typos)"
//          [frame! any-list?]
//      :relax "Allow non-erroring premature exits (THROW, RETURN, etc.)"
//  ]
//
DECLARE_NATIVE(ENRECOVER)
//
// Note: During boot, this operation is removed from LIB and moved to the
// system utilities, so it is typically called as SYS.UTIL/ENRECOVER.  Reason
// is to help raise awareness of the risks involved with using this function,
// because it's dangerous to react to these errors (or suppress them) due to
// how little you know about what actually happened.
{
    INCLUDE_PARAMS_OF_ENRECOVER;

    Element* code = Element_ARG(CODE);

    enum {
        ST_ENRECOVER_INITIAL_ENTRY = STATE_0,
        ST_ENRECOVER_EVALUATING
    };

    switch (STATE) {
      case ST_ENRECOVER_INITIAL_ENTRY: goto initial_entry;
      case ST_ENRECOVER_EVALUATING: goto eval_result_in_out;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    require (
      Level* L = Make_Level_At(
        &Evaluator_Executor,
        code,
        LEVEL_FLAG_VANISHABLE_VOIDS_ONLY  // EVAL-BLOCK!-like semantics?
    ));
    definitely(Is_Cell_Erased(OUT));  // we are in STATE_0
    Push_Level(OUT, L);

    STATE = ST_ENRECOVER_EVALUATING;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // fail not caught by default
    return CONTINUE_SUBLEVEL;

} eval_result_in_out: {  /////////////////////////////////////////////////////

    if (not THROWING) {  // successful result
        if (Is_Failure(OUT)) {
            LIFT_BYTE(OUT) = NOQUOTE_3;  // turn it into normal error
            return OUT;
        }
        return Lift_Cell(OUT);
    }

    if (not Is_Throwing_Panic(LEVEL)) {  // non-ERROR! throws
        if (ARG(RELAX))
            return BOUNCE_THROWN;  // e.g. RETURN, THROW

        return fail (Error_No_Catch_For_Throw(LEVEL));
    }

    Copy_Cell(OUT, VAL_THROWN_LABEL(LEVEL));
    CATCH_THROWN(SPARE, LEVEL);
    assert(Is_Light_Null(SPARE));  // all error throws are null-valued

    return OUT;
  }
}


//
//  /enrescue: native [
//
//  "Catch top-level EVAL step errors, FAILURE! -> ERROR! else lifted result"
//
//      return: [error! quasiform! quoted!]
//      code "Code to execute in steps, returning ERROR! if step is FAILURE!"
//          [block! frame!]
//  ]
//
DECLARE_NATIVE(ENRESCUE)  // wrapped as RESCUE
//
// Unlike SYS.UTIL/ENRECOVER, the ENRESCUE function only reacts to errors from
// the expressions it directly evaluates.  Hence it doesn't intercept panics,
// making it much safer to react to the errors one gets back from it.
{
    INCLUDE_PARAMS_OF_ENRESCUE;

    Element* code = Element_ARG(CODE);

    enum {
        ST_ENRESCUE_INITIAL_ENTRY = STATE_0,
        ST_ENRESCUE_EVAL_STEPPING,
        ST_ENRESCUE_RUNNING_FRAME
    };

    switch (STATE) {
      case ST_ENRESCUE_INITIAL_ENTRY: goto initial_entry;
      case ST_ENRESCUE_EVAL_STEPPING: goto eval_result_in_spare;
      case ST_ENRESCUE_RUNNING_FRAME: goto eval_result_in_spare;
      default: assert(false);
    }

  initial_entry: {  /////////////////////////////////////////////////////////

  // 1. We aren't catching throws or panics, only cooperative FAILURE! results.

    Init_Void(OUT);  // default if all evaluations produce void

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;  // reused for each step

    Level* sub;
    if (Is_Block(code)) {
        require (
          sub = Make_Level_At(
            &Stepper_Executor,
            code,  // TYPE_BLOCK or TYPE_GROUP
            flags
        ));
        definitely(Is_Cell_Erased(SPARE));  // we are in STATE_0
        Push_Level(SPARE, sub);

        if (Is_Level_At_End(sub))
            goto finished;

        STATE = ST_ENRESCUE_EVAL_STEPPING;
        unnecessary(Enable_Dispatcher_Catching_Of_Throws(LEVEL));  // [1]
        return CONTINUE_SUBLEVEL;
    }

    bool pushed = Pushed_Continuation(
        SPARE,
        flags,
        SPECIFIED,
        code,
        nullptr
    );
    assert(pushed);
    UNUSED(pushed);
    sub = TOP_LEVEL;

    STATE = ST_ENRESCUE_RUNNING_FRAME;
    unnecessary(Enable_Dispatcher_Catching_Of_Throws(LEVEL));  // [1]
    return CONTINUE_SUBLEVEL;

} eval_result_in_spare: {  ///////////////////////////////////////////////////

    if (Is_Failure(SPARE)) {
        Drop_Level(SUBLEVEL);
        Move_Cell(OUT, SPARE);
        LIFT_BYTE(OUT) = NOQUOTE_3;  // change antiform error to plain
        return OUT_BRANCHED;
    }

    if (STATE == ST_ENRESCUE_RUNNING_FRAME) {
        Copy_Cell(OUT, SPARE);
        goto finished;
    }

    if (not Any_Void(SPARE))
        Move_Cell(OUT, SPARE);

    if (Is_Level_At_End(SUBLEVEL))
        goto finished;

    Reset_Stepper_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL;

} finished: {  ///////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);
    return Lift_Cell(OUT);  // ^META result, may be initial void state
}}


//
//  /except: infix:defer native [
//
//  "If LEFT is FAILURE! then return BRANCH evaluation, else return LEFT"
//
//      return: [any-value?]  ; [1]
//      ^left [any-value?]
//      @branch [any-branch?]
//  ]
//
DECLARE_NATIVE(EXCEPT)
//
// 1. EXCEPT branches generally don't want antiforms, though once I thought
//    that (... except ^e -> [...]) might use ^e in the branch, with things
//    like (^e.id) being permissive and letting you read the field.  That's
//    not how that works... it acts as ^($e.id) not (^e).id - so basically
//    we have to disarm before passing.
//
// 2. "Hot Potatoes" are light substitutes for FAILURE!, so they pretty much
//    need to be handled by EXCEPT.  But unlike FAILURE!s they have little to
//    extract from them, and would become (confusingly) just WORD!s if they
//    were to lose their wrapping PACK! antiform.  By leaving them in the
//    dual state "as-is" this means that if a branch wants to accept a
//    hot potato it has to take its argument ^META, but once it does it
//    can use ordinary functions like VETO? or DONE? to recognize them.
{
    INCLUDE_PARAMS_OF_EXCEPT;

    Value* left = Possibly_Unstable(ARG(LEFT));
    Element* branch = ARG(BRANCH);

    if (Is_Failure(left)) {
        LIFT_BYTE(left) = NOQUOTE_3;  // turn FAILURE! to plain ERROR! [1]
    }
    else if (Is_Hot_Potato(left)) {
        // leave as-is [2]
    }
    else
        return COPY_TO_OUT(left);  // pass through non-error/non-hot-potato

    return DELEGATE_BRANCH(OUT, branch, left);
}


//
//  /trap: native [  ; performs arbitrary evaluation, can't be :intrinsic ATM
//
//  "If passed FAILURE! antiform, tunnel it to RETURN in scope, else passthru"
//
//      return: [any-value?]
//      ^value [any-value? failure!]
//      {return*}
//  ]
//
DECLARE_NATIVE(TRAP)
//
// 1. TRAP makes an API call to RETURN*.  It would be broken if it called the
//    context-sensing RETURN in the API call, because it would have to run
//    that code in the calling context of the TRAP callsite for the RETURN
//    to find it.  This means that if you hooked RETURN to do something
//    special with FAILURE! then the TRAP won't see that... it goes direct
//    to the RETURN*, but that is *probably* better anyway.
{
    INCLUDE_PARAMS_OF_TRAP;

    Value* v = ARG(VALUE);

    if (not Is_Failure(v))
        return COPY_TO_OUT(v);  // pass thru any non-failures

    Element* return_p = Init_Word(LOCAL(RETURN_P), CANON(RETURN_P));  // [1]
    Bind_Cell_If_Unbound(return_p, Level_Binding(LEVEL));
    Add_Cell_Sigil(return_p, SIGIL_META);  // !!! use /RETURN* or RETURN*/

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Corrupt_Cell_If_Needful(SCRATCH));

    STATE = ST_TWEAK_GETTING;

    require (
      Get_Var_To_Out_Use_Toplevel(return_p, GROUP_EVAL_NO)
    );

    if (not Is_Action(OUT))
        panic ("TRAP can't find RETURN in scope to tunnel FAILURE! to");

    Element* lifted_error = Lift_Cell(v);

    return rebDelegate(rebRUN(OUT), lifted_error);
}


//
//  /require: native [
//
//  "If passed a FAILURE! antiform, panic on it, otherwise passthru"
//
//      return: [any-value?]
//      ^value [any-value? failure! hot-potato?]
//  ]
//
DECLARE_NATIVE(REQUIRE)
{
    INCLUDE_PARAMS_OF_REQUIRE;

    Value* v = ARG(VALUE);

    if (Is_Failure(v) or Is_Undecayed_Bedrock(v)) {
      require (
        Decay_If_Unstable(v)
      );
    }

    return COPY_TO_OUT(v);  // pass thru any non-errors
}


//
//  /set-location-of-error: native [
//
//  "Sets the WHERE, NEAR, FILE, and LINE fields of an error"
//
//      return: ~
//      error [error!]
//      location [frame! any-word?]
//  ]
//
DECLARE_NATIVE(SET_LOCATION_OF_ERROR)
{
    INCLUDE_PARAMS_OF_SET_LOCATION_OF_ERROR;

    Element* location = Element_ARG(LOCATION);

    VarList* varlist;
    if (Is_Word(location)) {
        Sink(Element) spare_context = SPARE;
        if (
            not Try_Get_Binding_Of(spare_context, location, SPECIFIED)
            or not Is_Frame(spare_context)
        ){
            panic ("SET-LOCATION-OF-ERROR requires FRAME!-bound WORD!");
        }
        varlist = Cell_Varlist(spare_context);
    }
    else {
        assert(Is_Frame(location));
        varlist = Cell_Varlist(location);
    }

    Level* where = Level_Of_Varlist_May_Panic(varlist);

    Error* error = Cell_Error(ARG(ERROR));
    Set_Location_Of_Error(error, where);

    return TRASH_OUT;
}
