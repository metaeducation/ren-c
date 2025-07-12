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
//  try: native:intrinsic [
//
//  "Suppress escalation to PANIC from ERROR!s, by returning NULL"
//
//      return: [any-stable?]
//      ^atom  ; e.g. TRY on a pack returns the pack
//  ]
//
DECLARE_NATIVE(TRY)
{
    INCLUDE_PARAMS_OF_TRY;

    Atom* atom = Intrinsic_Atom_ARG(LEVEL);

    if (Is_Void(atom) or Is_Light_Null(atom))
        return NULLED;

    if (Is_Error(atom))
        return NULLED;

    return COPY(atom);  // !!! also tolerates other antiforms, should it?
}


//
//  enrecover: native [
//
//  "Sandbox code to intercept failures at ANY depth (including typos)"
//
//      return: "WARNING! if result is ERROR!, else ^META of the result"
//          [warning! quoted! quasiform!]
//      code "Code to sandbox and monitor"
//          [<unrun> frame! any-list?]
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

    // 1. We prime the evaluator with nihil so (enrecover [comment "hi"]) and
    //    (enrecover []) will return a ~[]~ empty block antiform.  This is
    //    because a key early use of ENRESCUE is in the console, and the
    //    console wishes to give the user the clearest feedback on what
    //    is going on.  It may be that there should be an option that decays
    //    that to void, and maybe even that should be the default, but at
    //    time of writing serving the needs of the console is most important.

    require (
      Level* L = Make_Level_At(
        &Evaluator_Executor,
        code,
        LEVEL_MASK_NONE
    ));
    Init_Void(Evaluator_Primed_Cell(L));  // able to produce nihil [1]

    Push_Level_Erase_Out_If_State_0(OUT, L);

    STATE = ST_ENRECOVER_EVALUATING;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // fail not caught by default
    return CONTINUE_SUBLEVEL(L);

} eval_result_in_out: {  /////////////////////////////////////////////////////

    if (not THROWING) {  // successful result
        if (Is_Error(OUT)) {
            LIFT_BYTE(OUT) = NOQUOTE_2;  // turn it into normal error
            return OUT;
        }
        return Liftify(OUT);
    }

    if (not Is_Throwing_Panic(LEVEL)) {  // non-ERROR! throws
        if (Bool_ARG(RELAX))
            return BOUNCE_THROWN;  // e.g. RETURN, THROW
        return Init_Warning(OUT, Error_No_Catch_For_Throw(LEVEL));
    }

    Copy_Cell(OUT, VAL_THROWN_LABEL(LEVEL));
    CATCH_THROWN(SPARE, LEVEL);
    assert(Is_Light_Null(SPARE));  // all error throws are null-valued

    return OUT;
  }
}


//
//  enrescue: native [
//
//  "Tries to EVAL a block, trapping error antiforms"
//
//      return: "WARNING! if antiform error, else the ^META of the result"
//          [warning! quasiform! quoted!]
//      code "Code to execute and monitor"
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
      case ST_ENRESCUE_EVAL_STEPPING: goto eval_step_dual_in_spare;
      case ST_ENRESCUE_RUNNING_FRAME: goto eval_result_in_spare;
      default: assert(false);
    }

  initial_entry: {  /////////////////////////////////////////////////////////

  // 1. We aren't catching throws or panics, only cooperative ERROR! results.

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
        Push_Level_Erase_Out_If_State_0(SPARE, sub);
        STATE = ST_ENRESCUE_EVAL_STEPPING;
        unnecessary(Enable_Dispatcher_Catching_Of_Throws(LEVEL));  // [1]
        return CONTINUE_SUBLEVEL(sub);
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
    return CONTINUE_SUBLEVEL(sub);

} eval_step_dual_in_spare: {  ////////////////////////////////////////////////

    if (Is_Endlike_Unset(SPARE))
        goto finished;

} eval_result_in_spare: {  ///////////////////////////////////////////////////

    if (Is_Error(SPARE)) {
        Drop_Level(SUBLEVEL);
        Move_Atom(OUT, SPARE);
        LIFT_BYTE(OUT) = NOQUOTE_2;  // change antiform error to plain
        return BRANCHED(OUT);
    }

    if (STATE == ST_ENRESCUE_RUNNING_FRAME) {
        Copy_Cell(OUT, SPARE);
        goto finished;
    }

    if (not Is_Ghost_Or_Void(SPARE))
        Move_Atom(OUT, SPARE);

    if (Try_Is_Level_At_End_Optimization(SUBLEVEL))
        goto finished;

    Reset_Evaluator_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} finished: {  ///////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);
    return Liftify(OUT);  // ^META result, may be initial void state
}}


//
//  except: infix:defer native [
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
DECLARE_NATIVE(EXCEPT)
//
// 1. While it was once "obvious" that an EXCEPT branch wouldn't want to get
//    an unstable antiform, it's now not as clear... since they could do
//    (... except ^e -> [...]) and use ^e in the branch, which might permit
//    things like (^e.id) and not give an error.  This would make it easier
//    to propagate the error without having the complexity or cost of doing
//    another call to FAIL.
//
//    This is of course contingent on the behavior of (^e.id) and such, so
//    we'll see how that shapes up.
{
    INCLUDE_PARAMS_OF_EXCEPT;

    Atom* atom = Atom_ARG(ATOM);
    Value* branch = ARG(BRANCH);

    if (not Is_Error(atom))
        return COPY(atom);  // pass thru any non-errors

    LIFT_BYTE(atom) = NOQUOTE_2;  // turn antiform error into plain warning
    Element* warning = Known_Element(atom);

    return DELEGATE_BRANCH(OUT, branch, warning);  // !!! pass antiform? [1]
}


//
//  trap: native [  ; performs arbitrary evaluation, can't be :intrinsic ATM
//
//  "If passed an ERROR! antiform, tunnel it to RETURN in scope, else passthru"
//
//      return: "Anything that wasn't an ERROR! antiform"
//          [any-atom?]  ; [1]
//      ^atom [any-atom?]
//  ]
//
DECLARE_NATIVE(TRAP)
{
    INCLUDE_PARAMS_OF_TRAP;

    Atom* atom = Atom_ARG(ATOM);

    if (not Is_Error(atom))
        return COPY(atom);  // pass thru any non-errors

    Element* return_word = Init_Word(SCRATCH, CANON(RETURN));

    require (
      Value* spare_action = Get_Word(
        SPARE,
        return_word,
        Feed_Binding(LEVEL->feed)
    ));

    if (not Is_Action(spare_action))
        panic ("TRAP can't find RETURN in scope to tunnel ERROR! to");

    Element* lifted_atom = Liftify(atom);

    return rebDelegate(rebRUN(spare_action), lifted_atom);
}


//
//  require: native [
//
//  "If passed an ERROR! antiform, panic on it, otherwise passthru"
//
//      return: "Anything that wasn't an ERROR! antiform"
//          [any-atom?]
//      ^atom [any-atom?]
//  ]
//
DECLARE_NATIVE(REQUIRE)
{
    INCLUDE_PARAMS_OF_REQUIRE;

    Atom* atom = Atom_ARG(ATOM);

    if (not Is_Error(atom))
        return COPY(atom);  // pass thru any non-errors

    panic (Cell_Error(atom));
}


//
//  error?: native:intrinsic [
//
//  "Tells you if argument is an ERROR! antiform, doesn't panic if it is"
//
//      return: [logic?]
//      ^atom
//  ]
//
DECLARE_NATIVE(ERROR_Q)
{
    INCLUDE_PARAMS_OF_ERROR_Q;

    const Atom* atom = Intrinsic_Typechecker_Atom_ARG(LEVEL);

    return LOGIC(Is_Error(atom));
}


//
//  set-location-of-error: native [
//
//  "Sets the WHERE, NEAR, FILE, and LINE fields of an error"
//
//      return: [null?]
//      error [warning!]
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
            not Try_Get_Binding_Of(spare_context, location)
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

    return NULLED;
}
