//
//  File: %n-error.c
//  Summary: "native functions for raising and trapping errors"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//  enrescue: native [
//
//  "Sandbox code to intercept failures at ANY depth (including typos)"
//
//      return: "ERROR! if raised, else ^META of the result"
//          [error! quoted? quasi? blank!]
//      code "Code to sandbox and monitor"
//          [<unrun> frame! any-list?]
//      /relax "Allow non-erroring premature exits (THROW, RETURN, etc.)"
//  ]
//
DECLARE_NATIVE(enrescue)
//
// Note: During boot, this operation is removed from LIB and moved to the
// system utilities, so it is typically called as SYS.UTIL/ENRESCUE.  Reason
// is to help raise awareness of the risks involved with using this function,
// because it's dangerous to react to these errors (or suppress them) due to
// how little you know about what actually happened.
{
    INCLUDE_PARAMS_OF_ENRESCUE;

    Value* code = ARG(code);

    enum {
        ST_ENRESCUE_INITIAL_ENTRY = STATE_0,
        ST_ENRESCUE_EVALUATING
    };

    switch (STATE) {
      case ST_ENRESCUE_INITIAL_ENTRY: goto initial_entry;
      case ST_ENRESCUE_EVALUATING: goto evaluation_finished;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. We prime the evaluator with nihil so (enrescue [comment "hi"]) and
    //    (enrescue []) will return a ~[]~ empty block antiform.  This is
    //    because a key early use of ENRESCUE is in the console, and the
    //    console wishes to give the user the clearest feedback on what
    //    is going on.  It may be that there should be an option that decays
    //    that to void, and maybe even that should be the default, but at
    //    time of writing serving the needs of the console is most important.

    if (Not_Cell_Flag(code, CONST))
        Set_Cell_Flag(code, EXPLICITLY_MUTABLE);  // read EVAL source for why

    Init_Nihil(Alloc_Evaluator_Primed_Result());  // able to produce nihil [1]
    Level* L = Make_Level_At(
        &Evaluator_Executor,
        code,
        LEVEL_FLAG_META_RESULT | LEVEL_FLAG_RAISED_RESULT_OK
    );

    Push_Level(OUT, L);

    STATE = ST_ENRESCUE_EVALUATING;
    return CATCH_CONTINUE_SUBLEVEL(L);

} evaluation_finished: {  ////////////////////////////////////////////////////

    if (not THROWING) {  // successful result
        if (Is_Meta_Of_Raised(OUT))  // was definitional error, got META'd
            QUOTE_BYTE(OUT) = NOQUOTE_1;  // turn it into normal error

        return OUT;  // META'd by LEVEL_FLAG_META_RESULT
    }

    if (not Is_Throwing_Failure(LEVEL)) {  // non-ERROR! throws
        if (REF(relax))
            return BOUNCE_THROWN;  // e.g. RETURN, THROW
        return Init_Error(OUT, Error_No_Catch_For_Throw(LEVEL));
    }

    Copy_Cell(OUT, VAL_THROWN_LABEL(LEVEL));
    CATCH_THROWN(SPARE, LEVEL);
    assert(Is_Nulled(SPARE));  // all error throws are null-valued

    return OUT;
  }
}


//
//  entrap: native [
//
//  "Tries to EVAL a block, trapping raised errors"
//
//      return: "ERROR! if raised, else the ^META of the result"
//          [error! quasi? quoted? blank!]
//      code "Code to execute and monitor"
//          [block! frame!]
//  ]
//
DECLARE_NATIVE(entrap)  // wrapped as TRAP and ATTEMPT
//
// Unlike SYS.UTIL/RESCUE, the ENTRAP function only reacts to errors from the
// functions it directly calls via LEVEL_FLAG_RAISED_RESULT_OK.  Hence it
// does not intercept thrown "failures", making it much safer to react to the
// errors one gets back from it.
{
    INCLUDE_PARAMS_OF_ENTRAP;

    Value* code = ARG(code);

    enum {
        ST_ENTRAP_INITIAL_ENTRY = STATE_0,
        ST_ENTRAP_EVALUATING
    };

    switch (STATE) {
      case ST_ENTRAP_INITIAL_ENTRY: goto initial_entry;
      case ST_ENTRAP_EVALUATING: goto eval_step_result_in_out;
      default: assert(false);
    }

  initial_entry: {  /////////////////////////////////////////////////////////

    Init_Void(OUT);  // default if all evaluations produce void

    Tweak_Non_Const_To_Explicitly_Mutable(code);  // see comments on function

    Flags flags =
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // reused for each step
        | LEVEL_FLAG_RAISED_RESULT_OK;  // we're trapping it

    Level* sub;
    if (Is_Block(code)) {
        sub = Make_Level_At(
            &Stepper_Executor,
            code,  // REB_BLOCK or REB_GROUP
            flags
        );
        Push_Level(SPARE, sub);
    }
    else {
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
    }

    STATE = ST_ENTRAP_EVALUATING;
    return CONTINUE_SUBLEVEL(sub);  // not a CATCH_CONTINUE; throws passthru

} eval_step_result_in_out: {  ////////////////////////////////////////////////

    if (Is_Raised(SPARE)) {
        Drop_Level(SUBLEVEL);
        Move_Cell(OUT, SPARE);
        QUOTE_BYTE(OUT) = NOQUOTE_1;  // change antiform error to plain
        return BRANCHED(OUT);
    }

    if (not Is_Elision(SPARE))
        Move_Cell(OUT, SPARE);

    if (Is_Level_At_End(SUBLEVEL))
        goto finished;

    Restart_Stepper_Level(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} finished: {  ///////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);
    return Meta_Quotify(OUT);  // ^META result, may be initial void state
}}


//
//  except: enfix native [
//
//  "Analogue to something like a THEN which traps definitional errors"
//
//      return: "Non-failure input, or product of processing failure"
//          [any-atom?]  ; [1]
//      ^atom "<deferred argument> Run branch if this is definitional fail"
//          [any-atom?]
//      @(branch) "If arity-1 ACTION!, receives value that triggered branch"
//          [<unrun> any-branch?]
//  ]
//
DECLARE_NATIVE(except)
//
// 1. Although THEN and ELSE will not operate on invisible input, it is legal
//    to trap a definitional error coming from a function that evaluates to
//    nihil.  Consider this case:
//
//        let result': ^ eval f except e -> [...]
//
//    If you intend this to work with arbitrary code and store a meta-NIHIL
//    in non-erroring cases, then EXCEPT must tolerate the NIHIL, since the
//    enfix defer rules mean this acts as ^ (eval f except e -> [...]).  If
//    you couldn't do that, this gets laborious to where you have to write
//    something like:
//
//        let result': ^ eval f
//        if failure? unmeta result' [let e: unquasi reify unmeta result ...]
{
    INCLUDE_PARAMS_OF_EXCEPT;

    Element* meta_atom = cast(Element*, ARG(atom));
    Value* branch = ARG(branch);

    if (not Is_Meta_Of_Raised(meta_atom))
        return UNMETA(meta_atom);  // pass thru anything not a raised error

    return DELEGATE_BRANCH(
        OUT,
        branch,  // if branch is an action, wants plain ERROR! as argument...
        Unquasify(meta_atom)  // ...meta_atom is ~QUASI-ERROR!~, unquasify it
    );
}


//
//  raised?: native:intrinsic [
//
//  "Tells you if argument is an ERROR! antiform, doesn't fail if it is"
//
//      return: [logic?]
//      ^optional
//  ]
//
DECLARE_INTRINSIC(raised_q)
{
    UNUSED(phase);

    Init_Logic(out, Is_Meta_Of_Raised(arg));
}


//
//  unraised?: native:intrinsic [
//
//  "Tells you if argument is not an ERROR! antiform, doesn't fail if it is"
//
//      return: [logic?]
//      ^optional
//  ]
//
DECLARE_INTRINSIC(unraised_q)
{
    UNUSED(phase);

    Init_Logic(out, not Is_Meta_Of_Raised(arg));
}


//
//  set-location-of-error: native [
//
//  "Sets the WHERE, NEAR, FILE, and LINE fields of an error"
//
//      return: [~null~]
//      error [error!]
//      location [frame! any-word?]
//  ]
//
DECLARE_NATIVE(set_location_of_error)
{
    INCLUDE_PARAMS_OF_SET_LOCATION_OF_ERROR;

    Value* location = ARG(location);

    VarList* context;
    if (Is_Word(location)) {
        if (not IS_WORD_BOUND(location))
            fail ("SET-LOCATION-OF-ERROR requires bound WORD!");
        context = VAL_WORD_CONTEXT(location);
    }
    else {
        assert(Is_Frame(location));
        context = Cell_Varlist(location);
    }

    Level* where = Level_Of_Varlist_May_Fail(context);

    Error* error = Cell_Error(ARG(error));
    Set_Location_Of_Error(error, where);

    return nullptr;
}
