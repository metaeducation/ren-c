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


// PACK piggy-backs on REDUCE's implementation.  But REDUCE ignores VOID!
// (at least by default--maybe someday a refinement will control it being able
// to see them...but it's too annoying to pass voids to predicates in the
// general case).  So it can't be just REDUCE:PREDICATE with LIFT, it has to
// slip in a flag to tell REDUCE to behave like PACK.
//
// Note: Since REDUCE and PACK are FRAME!-compatible, there could be a special
// value of the :PREDICATE refinement that cued PACK! behavior.  One might
// even say that some trick like "pass a quoted FRAME! and you get VOID!"
// could be a back-door to the functionality, vs. bloating the frame with an
// extra refinement.  For now avoiding weird magic, or exposing the feature
// via something users could trip over accidentally.
//
#define LEVEL_FLAG_REDUCE_IS_ACTUALLY_PACK  LEVEL_FLAG_MISCELLANEOUS


//
//  /reduce: native [
//
//  "Evaluates expressions, keeping each result in a block, discards voids"
//
//      return: [element?]
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
        goto reduce_step_result_in_spare;

      case ST_REDUCE_RUNNING_PREDICATE:
        goto process_out;

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
    definitely(Is_Cell_Erased(OUT));  // we are in STATE_0
    Push_Level(OUT, sub);

    Copy_Cell(Evaluator_Level_Current(sub), v);

    return DELEGATE_SUBLEVEL;

} initial_entry_list: {  /////////////////////////////////////////////////////

    require (
      Level* sub = Make_Level_At(
        &Stepper_Executor,
        v,  // TYPE_BLOCK or TYPE_GROUP
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // reused for each step
    ));
    definitely(Is_Cell_Erased(SPARE));  // we are in STATE_0
    Push_Level(SPARE, sub);

    goto next_reduce_step;

} next_reduce_step: {  ///////////////////////////////////////////////////////

  // 1. !!! Skipping BLANK! saves time here, but raises questions regarding
  //    RebindableSyntax...what if BLANK! has been rethought not to make
  //    voids?  It's actually semantically important for PACK (which reuses
  //    this code) to skip the blanks, so this needs further thought.
  //
  // 2. We want the output newline status to mirror newlines of the start
  //    of the eval positions.  But when the evaluation callback happens, we
  //    won't have the starting value anymore.  Cache the newline flag on
  //    the ARG(VALUE) cell, as newline flags on ARG()s are available.

    if (Is_Level_At_End(SUBLEVEL))
        goto finished;

    if (Is_Blank(At_Level(SUBLEVEL))) {
        Fetch_Next_In_Feed(SUBLEVEL->feed);
        goto next_reduce_step;  // PACK can't let BLANK! eval to VOID! [1]
    }

    if (Get_Cell_Flag(At_Level(SUBLEVEL), NEWLINE_BEFORE))
        Set_Cell_Flag(v, NEWLINE_BEFORE);  // cache newline flag [1]
    else
        Clear_Cell_Flag(v, NEWLINE_BEFORE);

    SUBLEVEL->executor = &Stepper_Executor;
    STATE = ST_REDUCE_EVAL_STEP;
    Reset_Stepper_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL;

} reduce_step_result_in_spare: { /////////////////////////////////////////////

  // 1. If not doing (pack [...]) semantics, we skip voids.  Consider:
  //
  //        >> reduce:predicate [1 + 2, if 3 = 4 [5], 6 + 7] negate/
  //        == [-3 -7]
  //
  //    NEGATE doesn't necessarily accept voids, but we'd still like to be
  //    able to vanish the light void that IF makes.

    if (Get_Level_Flag(LEVEL, REDUCE_IS_ACTUALLY_PACK)) {
        Copy_Lifted_Cell(PUSH(), SPARE);
        Sync_Toplevel_Baseline_After_Pushes(SUBLEVEL);
        goto next_reduce_step;
    }

    if (not predicate)  // default is no processing
        goto process_out;

    if (Is_Void(SPARE))
        goto next_reduce_step;  // don't pass voids to predicate [1]

    SUBLEVEL->executor = &Just_Use_Out_Executor;
    STATE = ST_REDUCE_RUNNING_PREDICATE;

    return CONTINUE(SPARE, unwrap predicate, SPARE);  // arg may also be output

} process_out: {  ////////////////////////////////////////////////////////////

  // 1. The sublevel that is pushed to run reduce evaluations uses the data
  //    stack position captured in BASELINE to tell things like whether a
  //    function dispatch has pushed refinements, etc.  When the REDUCE
  //    underneath it pushes a value to the data stack, that level must be
  //    informed the stack element is "not for it" before the next call.
  //
  // 2. See above section for how we remembered the newline that was on the
  //    source originally, and cache it on the input argument cell.

    if (Is_Void(SPARE))
        goto next_reduce_step;  // void results are skipped by reduce

    if (Is_Cell_A_Veto_Hot_Potato(SPARE))
        goto vetoed;  // veto means stop processing and return NULL

    require (
      Stable* spare = Decay_If_Unstable(SPARE)
    );
    if (Is_Splice(spare)) {
        const Element* tail;
        const Element* at = List_At(&tail, spare);
        bool newline = Get_Cell_Flag(v, NEWLINE_BEFORE);  // [2]
        for (; at != tail; ++at) {
            Copy_Cell(PUSH(), at);  // Note: no binding on antiform SPLICE!
            if (newline) {
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);  // [2]
                newline = false;
            }
        }
    }
    else if (Is_Antiform(spare))
        panic (Error_Bad_Antiform(spare));  // [4]
    else {
        Move_Cell(PUSH(), As_Element(spare));

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

    Element* out = Init_Any_List(OUT, Heart_Of_Builtin_Fundamental(v), a);

    dont(Tweak_Cell_Binding(out, Cell_Binding(v)));  // [1]
    UNUSED(out);

    return OUT;

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
// PACK piggy-backs on REDUCE.  See LEVEL_FLAG_REDUCE_IS_ACTUALLY_PACK.
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

            return Init_Pack(OUT, a);
        }

        assert(Is_Block(block));
        assert(Is_Null(LOCAL(PREDICATE)));
        Init_Null_Signifying_Unspecialized(LOCAL(PREDICATE));
        Set_Level_Flag(LEVEL, REDUCE_IS_ACTUALLY_PACK);
    }

    Bounce bounce = opt Irreducible_Bounce(
        LEVEL,
        Apply_Cfunc(NATIVE_CFUNC(REDUCE), LEVEL)
    );
    if (bounce)  // REDUCE wants more EVALs...final value not in OUT yet
        return bounce;

    if (Is_Null(OUT))  // VETO encountered
        return NULL_OUT_VETOING;

    if (Is_Failure(OUT))
        return OUT;  // definitional error (what choices would these be?)

    assert(Is_Possibly_Unstable_Value_Block(OUT));
    KIND_BYTE(OUT) = TYPE_GROUP;
    Unstably_Antiformize_Unbound_Fundamental(OUT);
    assert(Is_Pack(OUT));
    return OUT;
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
      case ST_REDUCE_EACH_REDUCING_STEP: goto reduce_step_result_in_spare;
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
    definitely(Is_Cell_Erased(SPARE));  // we are in STATE_0
    Push_Level(SPARE, sub);

} reduce_next: { ////////////////////////////////////////////////////////////

    if (Is_Level_At_End(SUBLEVEL))
        goto finished;

    if (Is_Blank(At_Level(SUBLEVEL))) {  // ","
        Fetch_Next_In_Feed(SUBLEVEL->feed);
        goto reduce_next;  // REDUCE skips commas, so REDUCE-EACH does too
    }

    if (Is_Pinned_Form_Of(BLOCK, block))  // undo &Just_Use_Out_Executor
        SUBLEVEL->executor = &Inert_Stepper_Executor;
    else
        SUBLEVEL->executor = &Stepper_Executor;

    STATE = ST_REDUCE_EACH_REDUCING_STEP;
    Reset_Stepper_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL;

} reduce_step_result_in_spare: {  ////////////////////////////////////////////

  // 1. See notes in REDUCE for why it just skips over VOID! results.  For
  //    compatibility, we make REDUCE-EACH do the same thing.
  //
  //    (If REDUCE ever gets a :GHOSTABLE refinement, REDUCE-EACH should too.)

    Slot* slot = Varlist_Slot(Cell_Varlist(vars), 1);

    if (Is_Void(SPARE))
        goto reduce_next;  // REDUCE-compatible semantics [1]

    trap (
      Write_Loop_Slot_May_Unbind_Or_Decay(slot, SPARE)
    );

} invoke_loop_body: {

    SUBLEVEL->executor = &Just_Use_Out_Executor;  // pass through sublevel

    STATE = ST_REDUCE_EACH_RUNNING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for break/continue
    return CONTINUE_BRANCH(OUT, body);

} body_result_in_out: { //////////////////////////////////////////////////////

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
