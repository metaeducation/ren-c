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
//  /veto?: pure native:intrinsic [
//
//  "Detect if VALUE is the undecayable ~(veto)~ 'hot potato' PACK!"
//
//      return: [logic!]
//      ^value '[<veto> any-value?]
//  ]
//
DECLARE_NATIVE(VETO_Q)
{
    INCLUDE_PARAMS_OF_VETO_Q;

    Value* v = ARG(VALUE);

    return LOGIC_OUT(Is_Cell_A_Veto_Hot_Potato(v));
}


//
//  /drain?: native [
//
//  "Test if VALUE is the ~(_)~ bedrock representation, or $VAR holding it"
//
//      return: [logic!]
//      @value [group! $word! $tuple!]
//  ]
//
DECLARE_NATIVE(DRAIN_Q)
{
    INCLUDE_PARAMS_OF_DRAIN_Q;

    Element* v = ARG(VALUE);

    bool is_drain;

    if (Is_Tied(v)) {
        Force_Cell_Sigil(v, SIGIL_META);

        Quote_Cell(v);
        Stable* result = rebStable(CANON(TWEAK), v, "()");
        is_drain = Is_Dual_Drain(As_Dual(result));
        rebRelease(result);
    }
    else {
        if (Eval_Any_List_At_Throws(OUT, v, SPECIFIED))
            return THROWN;
        is_drain = Is_Undecayed_Drain(OUT);
    }

    return LOGIC_OUT(is_drain);
}


//
//  /hole?: native [
//
//  "Test if VALUE is PARAMETER! bedrock representation, or $VAR holding it"
//
//      return: [logic!]
//      @value [group! $word! $tuple!]
//  ]
//
DECLARE_NATIVE(HOLE_Q)
{
    INCLUDE_PARAMS_OF_HOLE_Q;

    Element* v = ARG(VALUE);

    bool is_hole;

    if (Is_Tied(v)) {
        Clear_Cell_Sigil(v);
        Add_Cell_Sigil(v, SIGIL_META);

        Quote_Cell(v);
        Stable* result = rebStable(CANON(TWEAK), v, "()");
        is_hole = Is_Dual_Hole(As_Dual(result));
        rebRelease(result);
    }
    else {
        if (Eval_Any_List_At_Throws(OUT, v, SPECIFIED))
            return THROWN;
        is_hole = Is_Undecayed_Hole(OUT);
    }

    return LOGIC_OUT(is_hole);
}


// REDUCE is intended for things like PACK, that want a 1:1 expression to
// result relationship.  This means not discarding VOID! automatically...but
// also naming-wise it would seem strange if `reduce [x]` could produce a long
// block like `[1 2 3 4 5]` if X is SPLICE!, that doesn't seem like "reducing"!
//
// Hence the default behavior with no predicate will error if an expression
// has more or less than one result.  But for maximum flexibility, a predicate
// function is allowed to return VOID! or an arbitrary SPLICE!.  We use a
// flag on the cell being processed to indicate if it came from a predicate
// evaluation or not.
//
#define CELL_FLAG_SUBOUT_NOTE_FROM_PREDICATE  CELL_FLAG_NOTE


//
//  /reduce: native [
//
//  "Evaluates N expressions to produce N results (but discards BLANK!)"
//
//      return: [<null> element?]
//      value "Lists evaluate each item, single values evaluate"
//          [element?]
//      :predicate "Applied after evaluation, default is IDENTITY"
//          [frame!]
//  ]
//
DECLARE_NATIVE(REDUCE)
{
    INCLUDE_PARAMS_OF_REDUCE;

    Element* v = ARG(VALUE);  // newline flag leveraged [2]
    Option(Element*) predicate = ARG(PREDICATE);

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
        goto reduce_step_result_in_subout;

      case ST_REDUCE_RUNNING_PREDICATE:
        goto predicate_finished;

      default: assert(false);
    }

  initial_entry_non_list: {  /////////////////////////////////////////////////

  // It's not completely clear what the semantics of non-block REDUCE should
  // be, single value REDUCE currently does a REEVALUATE with no arguments.
  // This is a variant of REEVAL with an END feed.
  //
  // (R3-Alpha, would return the input, e.g. `reduce ':foo` => :foo)

    if (Any_Inert(v))
        return COPY_TO_OUT(v);  // save time if it's something like a TEXT!

    require (
      Level* sub = Make_End_Level(
        &Stepper_Executor,
        FLAG_STATE_BYTE(ST_STEPPER_REEVALUATING)
    ));
    Push_Level(sub);

    Copy_Cell(Evaluator_Level_Current(sub), v);

    return DELEGATE_SUBLEVEL;

} initial_entry_list: {  /////////////////////////////////////////////////////

    require (
      Level* sub = Make_Level_At(  // reused for each step
        &Stepper_Executor,
        v,  // TYPE_BLOCK or TYPE_GROUP
        LEVEL_MASK_NONE
    ));
    Push_Level(sub);

    goto next_reduce_step;

} next_reduce_step: {  ///////////////////////////////////////////////////////

  // 1. When you write e.g. `pack [1 + 2, 3 + 4]` that should be a 2-element
  //    PACK!, not a 3-element one with a lifted VOID! in the middle.  So
  //    we don't want an evaluation step on the BLANK!.
  //
  // 2. We want the output newline status to mirror newlines of the start
  //    of the eval positions.  But when the evaluation callback happens, we
  //    won't have the starting value anymore.  Cache the newline flag on
  //    the ARG(VALUE) cell, as newline flags on ARG()s are available.

    if (Is_Level_At_End(SUBLEVEL))
        goto finished;

    if (Is_Blank(At_Level(SUBLEVEL))) {
        Fetch_Next_In_Feed(SUBLEVEL->feed);
        goto next_reduce_step;  // don't want BLANK! to eval to VOID! [1]
    }

    if (Get_Cell_Flag(At_Level(SUBLEVEL), NEWLINE_BEFORE))
        Set_Cell_Flag(v, NEWLINE_BEFORE);  // cache newline flag [2]
    else
        Clear_Cell_Flag(v, NEWLINE_BEFORE);

    SUBLEVEL->executor = &Stepper_Executor;
    STATE = ST_REDUCE_EVAL_STEP;
    Reset_Stepper_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL;

} reduce_step_result_in_subout: { ////////////////////////////////////////////

    if (Is_Cell_A_Veto_Hot_Potato(SUBOUT))  // predicates are not offered veto
        goto vetoed;  // veto means stop processing and return NULL

    if (not predicate)
        goto process_subout;

    if (Frame_Phase(unwrap predicate) == Frame_Phase(LIB(LIFT))) {
        Lift_Cell(SUBOUT);  // optimize this case (used by PACK)
        goto process_subout;  // (likely better to optimize all intrinsics!)
    }

    SUBLEVEL->executor = &Skip_Me_Executor;
    STATE = ST_REDUCE_RUNNING_PREDICATE;

    return CONTINUE(unwrap predicate, SUBOUT);  // arg may also be output

} predicate_finished: {  /////////////////////////////////////////////////////

    Copy_Cell(Level_Out(TOP_LEVEL->prior), Level_Out(TOP_LEVEL));
    Drop_Level(TOP_LEVEL);

    if (Is_Cell_A_Veto_Hot_Potato(SUBOUT))
        goto vetoed;  // veto means stop processing and return NULL

    Set_Cell_Flag(SUBOUT, SUBOUT_NOTE_FROM_PREDICATE);

    goto process_subout;

} process_subout: {  /////////////////////////////////////////////////////////

  // 1. See SUBOUT_NOTE_FROM_PREDICATE about why we only allow expressoins
  //    to be more or less than 1 result if they come from predicate eval.
  //
  // 2. See above section for how we remembered the newline that was on the
  //    source originally, and cache it on the input argument cell.

    if (
        Any_Void(SUBOUT)
        and Get_Cell_Flag(SUBOUT, SUBOUT_NOTE_FROM_PREDICATE)  // [1]
    ){
        goto next_reduce_step;
    }

    require (
      Stable* subout = Decay_If_Unstable(SUBOUT)
    );
    if (Is_Splice(subout)) {
        const Element* tail;
        const Element* at = List_At(&tail, subout);
        if (
            at + 1 != tail
            and Not_Cell_Flag(SUBOUT, SUBOUT_NOTE_FROM_PREDICATE)  // [1]
        ){
            panic ("SPLICE! used in REDUCE w/no predicate must be 1 item");
        }
        bool newline = Get_Cell_Flag(v, NEWLINE_BEFORE);  // [2]
        for (; at != tail; ++at) {
            Copy_Cell(PUSH(), at);  // Note: no binding on antiform SPLICE!
            if (newline) {
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);  // [2]
                newline = false;
            }
        }
        Sync_Toplevel_Baseline_After_Pushes(SUBLEVEL);
        goto next_reduce_step;
    }
    else if (Is_Antiform(subout))
        panic (Error_Bad_Antiform(subout));
    else {
        Move_Cell(PUSH(), As_Element(subout));

        if (Get_Cell_Flag(v, NEWLINE_BEFORE))  // [2]
            Set_Cell_Flag(TOP, NEWLINE_BEFORE);
    }
    Sync_Toplevel_Baseline_After_Pushes(SUBLEVEL);

    goto next_reduce_step;

} finished: {  ///////////////////////////////////////////////////////////////

  // 1. As with COMPOSE, we don't bind the output list.  It's the caller's
  //    responsibility to bind it if desired.

    Drop_Level_Unbalanced(SUBLEVEL);  // Drop_Level() asserts on accumulation

    Source* a = Pop_Source_From_Stack(STACK_BASE);
    if (Get_Source_Flag(Cell_Array(v), NEWLINE_AT_TAIL))
        Set_Source_Flag(a, NEWLINE_AT_TAIL);

    Element* out = Init_List(OUT, Heart_Of_Builtin_Fundamental(v), a);

    dont(Tweak_Cell_Binding(out, Cell_Binding(v)));  // [1]
    UNUSED(out);

    return BOUNCE_OUT;

} vetoed: {  ///////////////////////////////////////////////////////////////

    Drop_Data_Stack_To(STACK_BASE);
    Drop_Level(SUBLEVEL);
    return NULL_OUT_VETOING;
}}


//
//  /pack: native [
//
//  "Create a pack of arguments from a list"
//
//      return: [pack!]
//      block "Reduce if plain BLOCK!, don't if @BLOCK!"
//          [block! @block!]
//      {predicate}  ; for FRAME!-compatibility with REDUCE
//  ]
//
DECLARE_NATIVE(PACK)
//
// PACK piggy-backs on REDUCE (it's REDUCE with PREDICATE of LIFT)
//
// 1. A PINNED! pack will just lifts the items as-is:
//
//       >> pack @[1 + 2]
//       == \~['1 '+ '2']~\  ; antiform (pack!)
//
// 2. Using LIFT as a predicate means FAILURE! antiforms are tolerated; it is
//    expected that you IGNORE (vs. ELIDE) a PACK which contains errors, as
//    ordinary elisions (such as in multi-step evaluations) will complain:
//
//        https://rebol.metaeducation.com/t/2206
{
    INCLUDE_PARAMS_OF_PACK;

    if (STATE == STATE_0) { // PACK is VOID!-aware REDUCE with LIFT predicate
        Element* block = Element_ARG(BLOCK);

        if (Is_Pinned_Form_Of(BLOCK, block)) {  // lift elements literally [1]
            const Element* tail;
            const Element* at = List_At(&tail, block);

            Length len = tail - at;
            Source* a = Make_Source_Managed(len);  // same size array
            Set_Flex_Len(a, len);
            Element *dest = Array_Head(a);

            for (; at != tail; ++at, ++dest)
                Copy_Lifted_Cell(dest, at);

            Init_Pack(OUT, a);
            return BOUNCE_OUT;
        }

        assert(Is_Block(block));
        assert(Is_Light_Null(LOCAL(PREDICATE)));
        Copy_Cell(LOCAL(PREDICATE), LIB(LIFT));
    }

    Bounce b = Irreducible_Bounce(
        Apply_Cfunc(NATIVE_CFUNC(REDUCE), LEVEL)
    );
    if (b != BOUNCE_TOPLEVEL_OUT)  // REDUCE wants more EVALs...
        return b;  // ...final value not in OUT yet

    if (Is_Light_Null(OUT))  // VETO encountered
        return NULL_OUT_VETOING;

    if (Is_Failure(OUT))
        return BOUNCE_OUT;  // definitional error (what choices would these be?)

    assert(Is_Possibly_Unstable_Value_Block(OUT));
    Tweak_Cell_Type_Matching_Heart(As_Element(OUT), HEART_GROUP);
    Tweak_Cell_Lift_Byte(OUT, TYPE_PACK);
    return BOUNCE_OUT;
}


//
//  /reduce-each: native [
//
//  "Evaluates expressions, passing each result to body (antiforms handled)"
//
//      return: [
//          any-value?      "last body result (if not NULL)"
//          ~(<null>)~      "if last body result was NULL"
//          <null>          "if BREAK encountered"
//          void!           "if body never ran"
//      ]
//      @vars "Variable to receive each reduced value (multiple TBD)"
//          [_ word! 'word! ^word!]
//      block "Input block of expressions (@[...] acts like FOR-EACH)"
//          [block! @block!]
//      body [block!]
//  ]
//
DECLARE_NATIVE(REDUCE_EACH)
{
    INCLUDE_PARAMS_OF_REDUCE_EACH;

    Element* vars = ARG(VARS);
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
      case ST_REDUCE_EACH_REDUCING_STEP: goto reduce_step_result_in_subout;
      case ST_REDUCE_EACH_RUNNING_BODY: goto body_result_in_topout;
      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

  // 1. This current REDUCE-EACH only works with one variable; it should be
  //    able to take a block of variables.

    Flags flags = LEVEL_MASK_NONE;

    require (
      VarList* varlist = Create_Loop_Context_May_Bind_Body(body, vars)
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), varlist));

    if (Varlist_Len(varlist) != 1)  // current limitation [1]
        panic ("REDUCE-EACH only supports one variable for now");

    assert(Is_Block(body));
    Add_Definitional_Continue(body, level_);

    Executor* executor;
    if (Is_Pinned_Form_Of(BLOCK, block))
        executor = &Inert_Stepper_Executor;
    else {
        assert(Is_Block(block));
        executor = &Stepper_Executor;
    }

    trap (
      Level* sub = Make_Level_At(executor, block, flags)
    );
    Push_Level(sub);

} reduce_next: { ////////////////////////////////////////////////////////////

    if (Is_Level_At_End(SUBLEVEL))
        goto finished;

    if (Is_Blank(At_Level(SUBLEVEL))) {  // ","
        Fetch_Next_In_Feed(SUBLEVEL->feed);
        goto reduce_next;  // REDUCE skips commas, so REDUCE-EACH does too
    }

    if (Is_Pinned_Form_Of(BLOCK, block))  // undo &Skip_Me_Executor
        SUBLEVEL->executor = &Inert_Stepper_Executor;
    else
        SUBLEVEL->executor = &Stepper_Executor;

    STATE = ST_REDUCE_EACH_REDUCING_STEP;
    Reset_Stepper_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL;

} reduce_step_result_in_subout: {  ////////////////////////////////////////////

  // 1. See notes in REDUCE for why it just skips over VOID! results.  For
  //    compatibility, we make REDUCE-EACH do the same thing.
  //
  //    (If REDUCE ever gets a :GHOSTABLE refinement, REDUCE-EACH should too.)

    Slot* slot = Varlist_Slot(Cell_Varlist(vars), 1);

    if (Is_Void(SUBOUT))
        goto reduce_next;  // REDUCE-compatible semantics [1]

    trap (
      Write_Loop_Slot_May_Unbind_Or_Decay(slot, SUBOUT)
    );

} invoke_loop_body: {

    SUBLEVEL->executor = &Skip_Me_Executor;  // pass through sublevel

    STATE = ST_REDUCE_EACH_RUNNING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for break/continue
    return CONTINUE_BRANCH(body);

} body_result_in_topout: { ///////////////////////////////////////////////////

    if (not THROWING) {
        Copy_Cell(OUT, Level_Out(TOP_LEVEL));
        Drop_Level(TOP_LEVEL);
    }

    if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
        goto finished;

    if (Is_Hot_Potato(OUT)) {
        if (Is_Cell_A_Veto_Hot_Potato(OUT)) {
            breaking = true;
            goto finished;
        }

        if (Is_Cell_A_Retry_Hot_Potato(OUT))
            goto invoke_loop_body;
    }

    require (
      Ensure_No_Failures_Including_In_Packs(OUT)
    );

    Disable_Dispatcher_Catching_Of_Throws(LEVEL);
    goto reduce_next;

} finished: { ////////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);

    if (THROWING)
        return THROWN;

    if (Is_Cell_Erased(OUT))  // body never ran
        return VOID_OUT_UNBRANCHED;

    if (breaking)
        return NULL_OUT_VETOING;

    return OUT_BRANCHED;
}}
