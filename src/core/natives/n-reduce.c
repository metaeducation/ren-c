//
//  file: %n-reduce.h
//  summary: "REDUCE and REDUCE-EACH natives"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
//  Startup_Reduce_Errors: C
//
void Startup_Reduce_Errors(void)
{
    ensure_nullptr(g_error_veto) = Init_Warning(
        Alloc_Value(),
        Error_Veto_Raw()
    );
}


//
//  Shutdown_Reduce_Errors: C
//
void Shutdown_Reduce_Errors(void)
{
    rebReleaseAndNull(&g_error_veto);
}


//
//  veto: native [
//
//  "Give back an error with (id = 'veto), used to cancel an operation"
//
//      return: [error!]
//  ]
//
DECLARE_NATIVE(VETO)
{
    INCLUDE_PARAMS_OF_VETO;

    Copy_Cell(OUT, g_error_veto);
    return Failify(OUT);
}


//
//  veto?: native:intrinsic [
//
//  "Detect whether argument is an error with (id = 'veto)"
//
//      return: [logic?]
//      ^value [any-value?]
//  ]
//
DECLARE_NATIVE(VETO_Q)
{
    INCLUDE_PARAMS_OF_VETO_Q;

    const Atom* atom = Intrinsic_Typechecker_Atom_ARG(LEVEL);

    if (not Is_Error(atom))
        return LOGIC(false);

    return LOGIC(Is_Error_Veto_Signal(Cell_Error(atom)));
}


//
//  reduce: native [
//
//  "Evaluates expressions, keeping each result (EVAL only gives last result)"
//
//      return: "New list or value (or null if VETO encountered)"
//          [null? element?]
//      value "GROUP! and BLOCK! evaluate each item, single values evaluate"
//          [<opt-out> element?]
//      :predicate "Applied after evaluation, default is IDENTITY"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(REDUCE)
{
    INCLUDE_PARAMS_OF_REDUCE;

    Element* v = Element_ARG(VALUE);  // newline flag leveraged [2]
    Value* predicate = ARG(PREDICATE);

    enum {
        ST_REDUCE_INITIAL_ENTRY = STATE_0,
        ST_REDUCE_EVAL_STEP,
        ST_REDUCE_RUNNING_PREDICATE
    };

    switch (STATE) {
      case ST_REDUCE_INITIAL_ENTRY:
        if (Any_List(v))
            goto initial_entry_list;
        goto initial_entry_non_list;  // semantics in question [1]

      case ST_REDUCE_EVAL_STEP:
        if (Is_Endlike_Unset(SPARE))
            goto finished;
        goto reduce_step_dual_in_spare;

      case ST_REDUCE_RUNNING_PREDICATE:
        goto process_out;

      default: assert(false);
    }

  initial_entry_non_list: {  /////////////////////////////////////////////////

    // It's not completely clear what the semantics of non-block REDUCE should
    // be, but right now single value REDUCE does a REEVALUATE with no
    // arguments.  This is a variant of REEVAL with an END feed.
    //
    // (R3-Alpha, would return the input, e.g. `reduce ':foo` => :foo)

    if (Any_Inert(v))
        return COPY(v);  // save time if it's something like a TEXT!

    require (
      Level* sub = Make_End_Level(
        &Stepper_Executor,
        FLAG_STATE_BYTE(ST_STEPPER_REEVALUATING)
    ));
    Push_Level_Erase_Out_If_State_0(OUT, sub);

    Copy_Cell(Evaluator_Level_Current(sub), v);
    Force_Invalidate_Gotten(&sub->u.eval.current_gotten);

    return DELEGATE_SUBLEVEL(sub);

} initial_entry_list: {  /////////////////////////////////////////////////////

    require (
      Level* sub = Make_Level_At(
        &Stepper_Executor,
        v,  // TYPE_BLOCK or TYPE_GROUP
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // reused for each step
    ));
    Push_Level_Erase_Out_If_State_0(SPARE, sub);
    goto next_reduce_step;

} next_reduce_step: {  ///////////////////////////////////////////////////////

    // 1. We want the output newline status to mirror newlines of the start
    //    of the eval positions.  But when the evaluation callback happens,
    //    we won't have the starting value anymore.  Cache the newline flag on
    //    the ARG(VALUE) cell, as newline flags on ARG()s are available.

    if (Try_Is_Level_At_End_Optimization(SUBLEVEL))
        goto finished;

    if (not Is_Feed_At_End(SUBLEVEL->feed)) {
        if (Get_Cell_Flag(At_Feed(SUBLEVEL->feed), NEWLINE_BEFORE))
            Set_Cell_Flag(v, NEWLINE_BEFORE);  // cache newline flag [1]
        else
            Clear_Cell_Flag(v, NEWLINE_BEFORE);
    }

    SUBLEVEL->executor = &Stepper_Executor;
    STATE = ST_REDUCE_EVAL_STEP;
    Reset_Evaluator_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} reduce_step_dual_in_spare: { ///////////////////////////////////////////////

    if (Is_Nulled(predicate))  // default is no processing
        goto process_out;

    if (Is_Ghost_Or_Void(SPARE)) {  // vaporize unless accepted by predicate
        const Param* param = First_Unspecialized_Param(
            nullptr,
            Frame_Phase(predicate)
        );
        if (not Typecheck_Atom_In_Spare_Uses_Scratch(LEVEL, param, SPECIFIED))
            goto next_reduce_step;  // not accepted, so skip it
    }

    SUBLEVEL->executor = &Just_Use_Out_Executor;
    STATE = ST_REDUCE_RUNNING_PREDICATE;

    return CONTINUE(SPARE, predicate, SPARE);  // arg can be same as output

} process_out: {  ////////////////////////////////////////////////////////////

    // 3. The sublevel that is pushed to run reduce evaluations uses the data
    //    stack position captured in BASELINE to tell things like whether a
    //    function dispatch has pushed refinements, etc.  When the REDUCE
    //    underneath it pushes a value to the data stack, that level must be
    //    informed the stack element is "not for it" before the next call.

    if (Is_Ghost_Or_Void(SPARE))
        goto next_reduce_step;  // void results are skipped by reduce

    if (Is_Error(SPARE) and Is_Error_Veto_Signal(Cell_Error(SPARE)))
        goto vetoed;

    require (
      Value* spare = Decay_If_Unstable(SPARE)
    );
    if (Is_Splice(spare)) {
        const Element* tail;
        const Element* at = List_At(&tail, spare);
        bool newline = Get_Cell_Flag(v, NEWLINE_BEFORE);
        for (; at != tail; ++at) {
            Copy_Cell(PUSH(), at);  // Note: no binding on antiform SPLICE!
            SUBLEVEL->baseline.stack_base += 1;  // [3]
            if (newline) {
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);  // [2]
                newline = false;
            }
        }
    }
    else if (Is_Antiform(spare))
        return fail (Error_Bad_Antiform(spare));
    else {
        Move_Cell(PUSH(), Known_Element(spare));  // not void, not antiform
        SUBLEVEL->baseline.stack_base += 1;  // [3]

        if (Get_Cell_Flag(v, NEWLINE_BEFORE))  // [2]
            Set_Cell_Flag(TOP, NEWLINE_BEFORE);
    }

    goto next_reduce_step;

} finished: {  ///////////////////////////////////////////////////////////////

    Drop_Level_Unbalanced(SUBLEVEL);  // Drop_Level() asserts on accumulation

    Source* a = Pop_Source_From_Stack(STACK_BASE);
    if (Get_Source_Flag(Cell_Array(v), NEWLINE_AT_TAIL))
        Set_Source_Flag(a, NEWLINE_AT_TAIL);

    Element* out = Init_Any_List(OUT, Heart_Of_Builtin_Fundamental(v), a);
    Tweak_Cell_Binding(out, Cell_Binding(v));
    return OUT;

} vetoed: {  ///////////////////////////////////////////////////////////////

    Drop_Data_Stack_To(STACK_BASE);
    Drop_Level(SUBLEVEL);
    return NULLED;
}}


//
//  reduce-each: native [
//
//  "Evaluates expressions, keeping each result (EVAL only gives last result)"
//
//      return: "Last body result"
//          [any-value?]
//      vars "Variable to receive each reduced value (multiple TBD)"
//          [_ word! @word! block!]
//      block "Input block of expressions (@[...] acts like FOR-EACH)"
//          [block! @block!]
//      body "Code to run on each step"
//          [block!]
//  ]
//
DECLARE_NATIVE(REDUCE_EACH)
{
    INCLUDE_PARAMS_OF_REDUCE_EACH;

    Element* vars = Element_ARG(VARS);
    Element* block = Element_ARG(BLOCK);
    Element* body = Element_ARG(BODY);

    bool breaking = false;

    enum {
        ST_REDUCE_EACH_INITIAL_ENTRY = STATE_0,
        ST_REDUCE_EACH_REDUCING_STEP,
        ST_REDUCE_EACH_RUNNING_BODY
    };

    switch (STATE) {
      case ST_REDUCE_EACH_INITIAL_ENTRY: goto initial_entry;
      case ST_REDUCE_EACH_REDUCING_STEP: goto reduce_step_dual_in_spare;
      case ST_REDUCE_EACH_RUNNING_BODY: goto body_result_in_out;
      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. This current REDUCE-EACH only works with one variable; it should be
    //    able to take a block of variables.

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;

    require (
      VarList* varlist = Create_Loop_Context_May_Bind_Body(body, vars)
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), varlist));

    if (Varlist_Len(varlist) != 1)  // current limitation [1]
        panic ("REDUCE-EACH only supports one variable for now");

    assert(Is_Block(body));
    Add_Definitional_Break_Continue(body, level_);

    Executor* executor;
    if (Is_Pinned_Form_Of(BLOCK, block))
        executor = &Inert_Stepper_Executor;
    else {
        assert(Is_Block(block));
        executor = &Stepper_Executor;
    }

    require (
      Level* sub = Make_Level_At(executor, block, flags)
    );
    Push_Level_Erase_Out_If_State_0(SPARE, sub);

} reduce_next: { ////////////////////////////////////////////////////////////

    if (Is_Feed_At_End(SUBLEVEL->feed))
        goto finished;

    if (Is_Pinned_Form_Of(BLOCK, block))  // undo &Just_Use_Out_Executor
        SUBLEVEL->executor = &Inert_Stepper_Executor;
    else
        SUBLEVEL->executor = &Stepper_Executor;

    STATE = ST_REDUCE_EACH_REDUCING_STEP;
    Reset_Evaluator_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} reduce_step_dual_in_spare: {  //////////////////////////////////////////////

    Slot* slot = Varlist_Slot(Cell_Varlist(vars), 1);

    if (Is_Endlike_Unset(SPARE))
        goto finished;

    if (Is_Ghost(SPARE) and Not_Cell_Flag(slot, LOOP_SLOT_ROOT_META))
        goto reduce_next;  // skip ghost unless meta?

    require (
      Write_Loop_Slot_May_Bind_Or_Decay(slot, SPARE, block)
    );

} next_reduce_each: {

    SUBLEVEL->executor = &Just_Use_Out_Executor;  // pass through sublevel

    STATE = ST_REDUCE_EACH_RUNNING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for break/continue
    return CONTINUE_BRANCH(OUT, body);

} body_result_in_out: { //////////////////////////////////////////////////////

    if (THROWING) {
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            goto finished;

        if (breaking)
            goto finished;
    }

    Disable_Dispatcher_Catching_Of_Throws(LEVEL);
    goto reduce_next;

} finished: { ////////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);

    if (THROWING)
        return THROWN;

    if (Is_Cell_Erased(OUT))  // body never ran
        return VOID;

    if (breaking)
        return BREAKING_NULL;

    return BRANCHED(OUT);
}}



enum FLATTEN_LEVEL {
    FLATTEN_NOT,
    FLATTEN_ONCE,
    FLATTEN_DEEP
};


static void Flatten_Core(
    Element* head,
    const Element* tail,
    Context* binding,
    enum FLATTEN_LEVEL level
) {
    Element* item = head;
    for (; item != tail; ++item) {
        if (Is_Block(item) and level != FLATTEN_NOT) {
            Context* derived = Derive_Binding(binding, item);

            const Element* sub_tail;
            Element* sub = List_At_Ensure_Mutable(&sub_tail, item);
            Flatten_Core(
                sub,
                sub_tail,
                derived,
                level == FLATTEN_ONCE ? FLATTEN_NOT : FLATTEN_DEEP
            );
        }
        else
            Derelativize(PUSH(), item, binding);
    }
}


//
//  flatten: native [
//
//  "Flattens a block of blocks"
//
//      return: [block!]
//      block [block!]
//      :deep
//  ]
//
DECLARE_NATIVE(FLATTEN)
{
    INCLUDE_PARAMS_OF_FLATTEN;

    Element* block = Element_ARG(BLOCK);

    const Element* tail;
    Element* at = List_At_Ensure_Mutable(&tail, block);
    Flatten_Core(
        at,
        tail,
        List_Binding(block),
        Bool_ARG(DEEP) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(OUT, Pop_Source_From_Stack(STACK_BASE));
}
