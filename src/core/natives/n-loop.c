//
//  file: %n-loop.c
//  summary: "native functions for loops"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2026 Ren-C Open Source Contributors
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
// A. Loop bodies are not taken as literals, the way branches are.  The body
//    is evaluated at the callsite as usual.  Hence any GROUP! is evaluated
//    to build the body regardless of whether there is data or not, and any
//    FENCE! has to be evaluated to do its wrapping prior to being received
//    by the loop.
//
//    This may seem wasteful in the case of work being done to produce or
//    wrap the body even when there is no data to enumerate.  And it also may
//    seem like a missed opportunity to take FENCE! literally and dodge the
//    creation of locals for any loop variables that match SET-WORD at the
//    top level of that fence (similar to how FUNCTION takes FENCE! literally
//    to signal gathering of locals at the top level).
//
//    But loop bodies are designed to be called multiple times, and it just
//    impacts loop wrappers too seriously.  Imagine if FOR-EACH took its body
//    literally and you tried to wrap it:
//
//        for-both: lambda [@(var) blk1 blk2 @(body)] [  ; alternate universe
//            all [
//                ^ for-each (var) blk1 (body) then lift/
//                ^ for-each (var) blk2 (body) then lift/
//            ] then unlift/
//        ]
//
//    A FENCE! that was received and analyzed by FOR-EACH would create *two*
//    separate wrappings, each with its own state.  This reality would leak
//    into the user experience by splitting identity across two separate sets
//    of variables, instead of just one.
//
//    Therefore: if you have a "computed branch" and the calculation would be
//    undesirable to do if the data turns out to be empty, don't use a GROUP!,
//    use a cache:
//
//        for-each item data-maybe-empty [
//            eval cache [some-expensive-transformation [
//                ...
//            ]]
//        ]
//

#include "sys-core.h"
#include "sys-int-funcs.h" //Add_I64_Overflows


//
//  Loop_Body_Threw_And_Cant_Catch_Continue: C
//
// Each loop structure defines its own CONTINUE* instance and binds it into
// the body.  When CONTINUE* is called it throws the passed value up to its
// associated loop--and the design is that the loop will treat that caught
// value the same as if the loop body had evaluated fully and produced it.
//
// The special states representing stopping the loop early (BREAK) and doing
// the loop body again with no increment (AGAIN) are implemented via signals
// known as "hot potatoes".  These are undecayable packs containing the words
// ~(veto)~ and ~(retry)~ respectively.  Other values are regarded as just
// being ordinary CONTINUE results (continue can take an optional parameter,
// of the value to continue with).
//
// This function returns true in the case that a situation is neither an
// ordinary value in `out` or a thrown definitional continue that could be
// caught and put into `out` as if the body had produced it.  In that case
// the caller should bubble up the throw by returning THROWN.
//
bool Loop_Body_Threw_And_Cant_Catch_Continue(
    Value* out,  // not Sink() -- leave body result if not catching CONTINUE*
    Level* loop_level
){
    dont(  // loops haven't all been made stackless--can't assert this yet
        assert(Get_Executor_Flag(ACTION, loop_level, DISPATCHER_CATCHES))
    );

    if (not Is_Throwing(loop_level))
        return false;  // not throwing, so body ran, act as if "caught"

    const Stable* label = VAL_THROWN_LABEL(loop_level);

    if (
        Is_Frame(label)
        and Frame_Phase(label) == Frame_Phase(LIB(DEFINITIONAL_CONTINUE))
        and Frame_Coupling(label) == Level_Varlist(loop_level)
    ){
        CATCH_THROWN(out, loop_level);
        return false;  // *did* catch
    }

    return true;  // *didn't* catch (caller should let other throws bubble up)
}


//
//  /definitional-continue: native [
//
//  "Per-loop native for handing control back to the loop"
//
//      return: []
//      ^value "Act as if loop body finished with this value (VETO is BREAK)"
//          [<hole> <veto> any-value?]
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_CONTINUE)
//
// Each loop construct binds an instance of this native into their body
// under the name CONTINUE*.  The loop puts its identity into the "Coupling"
// of the ACTION! Cell (much like a definitional RETURN), so that when the
// function is called it can identify which loop is being continued.
//
// 1. CONTINUE with no argument acts like the branch completed with no
//    result; and since it's being run as a branch we throw heavy void.
//
//    https://forum.rebol.info/t/1965/3
//
//    Functions like INSIST and REMOVE-EACH thus should tolerate no result
//    OR force you to CONTINUE with a value saying what you mean.
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_CONTINUE;

    Param* param = ARG(VALUE);

    Value* v;
    if (Is_Cell_A_Bedrock_Hole(param))
        v = Init_Heavy_Void(Sink_LOCAL(VALUE));  // [1]
    else
        v = As_Value(param);

    Level* continue_level = LEVEL;  // Level of this CONTINUE call

    Option(VarList*) coupling = Level_Coupling(continue_level);
    if (not coupling)
        panic (Error_Archetype_Invoked_Raw());

    Level* loop_level = Level_Of_Varlist_May_Panic(unwrap coupling);

    Element* label = Init_Frame(
        SPARE,
        Frame_Phase(LIB(DEFINITIONAL_CONTINUE)),
        CANON(CONTINUE_P),
        Varlist_Of_Level_Force_Managed(loop_level)
    );

    Init_Thrown_With_Label(LEVEL, v, label);
    return BOUNCE_THROWN;
}


//
//  /continue: native [
//
//  "Hand control back to the loop"
//
//      return: []
//      ^value "Act as if loop body finished with this value (VETO is BREAK)"
//          [<hole> <veto> any-value?]
//      {continue*}
//  ]
//
DECLARE_NATIVE(CONTINUE)
//
// CONTINUE is implemented by calling the CONTINUE* currently in scope, giving
// it whatever argument it got.
{
  default_handling: {

    INCLUDE_PARAMS_OF_CONTINUE;

    possibly(  // BREAK and AGAIN are implemented by calling this native
        Level_Label(LEVEL) == CANON(BREAK)
        or Level_Label(LEVEL) == CANON(AGAIN)
    );

    Element* continue_p = Init_Word(LOCAL(CONTINUE_P), CANON(CONTINUE_P));
    Add_Cell_Sigil(continue_p, SIGIL_META);  // !!! /CONTINUE* or CONTINUE*/
    Bind_Cell_If_Unbound(continue_p, Level_Binding(LEVEL));

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Corrupt_Cell_If_Needful(SCRATCH));

    STATE = ST_TWEAK_GETTING;

    require (
      Get_Var_To_Out_Use_Toplevel(continue_p, GROUP_EVAL_NO)
    );

    if (not Is_Action(OUT))
        panic ("found a CONTINUE* that wasn't an ACTION!");

    if (Frame_Phase(OUT) == Frame_Phase(LIB(DEFINITIONAL_CONTINUE)))
        goto optimized_builtin_continue_call;  // avoid rebDelegate overhead

    Param* param = ARG(VALUE);
    if (Is_Cell_A_Bedrock_Hole(param))  // no argument
        return rebDelegate(rebRUN(OUT));

    Value* v = As_Value(param);
    Lift_Cell(v);  // one argument, lift it (eval will unlift)
    return rebDelegate(rebRUN(OUT), v);

} optimized_builtin_continue_call: { /////////////////////////////////////////

    INCLUDE_PARAMS_OF_DEFINITIONAL_CONTINUE;

    heeded (ARG(VALUE));

    Tweak_Level_Coupling(LEVEL, Frame_Coupling(OUT));

    return Apply_Cfunc(NATIVE_CFUNC(DEFINITIONAL_CONTINUE), LEVEL);
}}


//
//  /break: native [
//
//  "Exit the current iteration of a loop and stop iterating further"
//
//      return: []
//      {value}  ; FRAME!-compatibility with CONTINUE (optimization may reuse)
//      {continue*}
//  ]
//
DECLARE_NATIVE(BREAK)
//
// The default BREAK is just a specialization of CONTINUE that uses the
// ~(veto)~ "hot potato".  This could be done with specialization, but a
// native is slightly faster.
{
    INCLUDE_PARAMS_OF_CONTINUE;  // BREAK must be frame compatible

    Copy_Cell(LOCAL(VALUE), LIB(VETO));

    return Apply_Cfunc(NATIVE_CFUNC(CONTINUE), LEVEL);
}


//
//  /again: native [
//
//  "Re-run a loop without advancing its loop variables"
//
//      return: []
//      {value}  ; FRAME!-compatibility with CONTINUE (optimization may reuse)
//      {continue*}
//  ]
//
DECLARE_NATIVE(AGAIN)
//
// The default AGAIN is just a specialization of CONTINUE that uses the
// ~(retry)~ "hot potato".  This could be done with specialization, but a
// native is slightly faster.
{
    INCLUDE_PARAMS_OF_CONTINUE;  // AGAIN must be frame compatible

    Copy_Cell(LOCAL(VALUE), LIB(RETRY));

    return Apply_Cfunc(NATIVE_CFUNC(CONTINUE), LEVEL);
}


//
//  Add_Definitional_Continue: C
//
void Add_Definitional_Continue(
    Element* body,
    Level* loop_level
){
    if (not Is_Block(body)) {
        assert(Is_Frame(body));
        return;
    }

    Context* parent = List_Binding(body);
    Let* let_continue = Make_Let_Variable(CANON(CONTINUE_P), parent);

    Init_Action(
        Stub_Cell(let_continue),
        Frame_Phase(LIB(DEFINITIONAL_CONTINUE)),
        CANON(CONTINUE_P),  // relabel as CONTINUE* (CONTINUE* in lib is trash)
        Varlist_Of_Level_Force_Managed(loop_level)  // what to continue
    );

    Tweak_Cell_Binding(body, let_continue);  // extend chain
}


//
//  Loop_Series_Common: C
//
static Bounce Loop_Series_Common(
    Level* level_,
    Fixed(Slot*) slot,  // See #2274
    const Element* body,
    Stable* start,
    Index end,
    REBINT bump
){
    // !!! This limits incoming `end` to the array bounds.  Should it assert?
    //
    if (end >= Series_Len_Head(start))
        end = Series_Len_Head(start);
    if (end < 0)
        end = 0;

    // A value cell exposed to the user is used to hold the state.  This means
    // if they change `var` during the loop, it affects the iteration.  Hence
    // it must be checked for changing to another series, or non-series.
    //
    Stable* var = Copy_Cell(Slot_Hack(slot), start);
    Index* state = &SERIES_INDEX_UNBOUNDED(var);

    // Run only once if start is equal to end...edge case.
    //
    Index s = Series_Index(start);
    if (s == end) {
        attempt {
            if (Eval_Branch_Throws(OUT, body)) {
                if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
                    return THROWN;
            }
            if (Is_Hot_Potato(OUT)) {
                if (Is_Cell_A_Veto_Hot_Potato(OUT))
                    return NULL_OUT_VETOING;

                if (Is_Cell_A_Retry_Hot_Potato(OUT))
                    again;
            }
        }
        require (
          Ensure_No_Failures_Including_In_Packs(OUT)
        );
        return OUT;
    }

    // As per #1993, start relative to end determines the "direction" of the
    // FOR loop.  (R3-Alpha used the sign of the bump, which meant it did not
    // have a clear plan for what to do with 0.)
    //
    const bool counting_up = (s < end); // equal checked above
    if ((counting_up and bump <= 0) or (not counting_up and bump >= 0))
        return VOID_OUT_UNBRANCHED;  // avoid infinite loops

    while (
        counting_up
            ? *state <= end
            : *state >= end
    ){
        if (Eval_Branch_Throws(OUT, body)) {
            if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
                return THROWN;
        }

        if (Is_Hot_Potato(OUT)) {
            if (Is_Cell_A_Veto_Hot_Potato(OUT))
                return NULL_OUT_VETOING;

            if (Is_Cell_A_Retry_Hot_Potato(OUT))
                continue;  // re-run without advancing index
        }

        require (
          Ensure_No_Failures_Including_In_Packs(OUT)
        );

        if (
            Type_Of(var) != Type_Of(start)
            or Cell_Flex(var) != Cell_Flex(start)
        ){
            panic ("Can only change series index, not series to iterate");
        }

        // Note that since the array is not locked with FLEX_INFO_HOLD, it
        // can be mutated during the loop body, so the end has to be refreshed
        // on each iteration.  Review ramifications of HOLD-ing it.
        //
        if (end >= Series_Len_Head(start))
            end = Series_Len_Head(start);

        *state += bump;
    }

    if (Is_Cell_Erased(OUT))
        return VOID_OUT_UNBRANCHED;

    return OUT_BRANCHED;
}


//
//  Loop_Integer_Common: C
//
static Bounce Loop_Integer_Common(
    Level* level_,
    Fixed(Slot*) slot,  // See #2274
    const Element* body,
    REBI64 start,
    REBI64 end,
    REBI64 bump
){
    // A value cell exposed to the user is used to hold the state.  This means
    // if they change `slot` during the loop, it affects the iteration.  Hence
    // it must be checked for changing to a non-integer form.
    //
    Stable* var = Init_Integer(Slot_Hack(slot), start);
    REBI64* state = &mutable_VAL_INT64(var);

    // Run only once if start is equal to end...edge case.
    //
    if (start == end) {
        attempt {
            if (Eval_Branch_Throws(OUT, body)) {
                if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
                    return THROWN;
            }

            if (Is_Hot_Potato(OUT)) {
                if (Is_Cell_A_Veto_Hot_Potato(OUT))
                    return NULL_OUT_VETOING;

                if (Is_Cell_A_Retry_Hot_Potato(OUT))
                    again;
            }
        }
        require (
          Ensure_No_Failures_Including_In_Packs(OUT)
        );
        return OUT_BRANCHED;
    }

    // As per #1993, start relative to end determines the "direction" of the
    // FOR loop.  (R3-Alpha used the sign of the bump, which meant it did not
    // have a clear plan for what to do with 0.)
    //
    const bool counting_up = (start < end);  // equal checked above
    if ((counting_up and bump <= 0) or (not counting_up and bump >= 0))
        return VOID_OUT_UNBRANCHED;  // avoid infinite loops !!!

    while (counting_up ? *state <= end : *state >= end) {
        attempt {
            if (Eval_Branch_Throws(OUT, body)) {
                if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
                    return THROWN;
            }
            if (Is_Hot_Potato(OUT)) {
                if (Is_Cell_A_Veto_Hot_Potato(OUT))
                    return NULL_OUT_VETOING;

                if (Is_Cell_A_Retry_Hot_Potato(OUT))
                    again;
            }
        }

        require (
          Ensure_No_Failures_Including_In_Packs(OUT)
        );

        if (not Is_Integer(var))
            panic (Error_Invalid_Type_Raw(Datatype_Of(var)));

        if (Add_I64_Overflows(state, *state, bump))
            panic (Error_Overflow_Raw());
    }

    return OUT_BRANCHED;
}


//
//  Loop_Number_Common: C
//
static Bounce Loop_Number_Common(
    Level* level_,
    Fixed(Slot*) slot,  // See #2274
    const Element* body,
    Stable* start,
    Stable* end,
    Stable* bump
){
    REBDEC s;
    if (Is_Integer(start))
        s = cast(REBDEC, VAL_INT64(start));
    else if (Is_Decimal(start) or Is_Percent(start))
        s = VAL_DECIMAL(start);
    else
        panic (start);

    REBDEC e;
    if (Is_Integer(end))
        e = cast(REBDEC, VAL_INT64(end));
    else if (Is_Decimal(end) or Is_Percent(end))
        e = VAL_DECIMAL(end);
    else
        panic (end);

    REBDEC b;
    if (Is_Integer(bump))
        b = cast(REBDEC, VAL_INT64(bump));
    else if (Is_Decimal(bump) or Is_Percent(bump))
        b = VAL_DECIMAL(bump);
    else
        panic (bump);

    // As in Loop_Integer_Common(), the state is actually in a cell; so each
    // loop iteration it must be checked to ensure it's still a decimal...
    //
    Stable* var = Init_Decimal(Slot_Hack(slot), s);
    REBDEC *state = &VAL_DECIMAL(var);

    // Run only once if start is equal to end...edge case.
    //
    if (s == e) {
        attempt {
            if (Eval_Branch_Throws(OUT, body)) {
                if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
                    return THROWN;
            }
            if (Is_Hot_Potato(OUT)) {
                if (Is_Cell_A_Veto_Hot_Potato(OUT))
                    return NULL_OUT_VETOING;

                if (Is_Cell_A_Retry_Hot_Potato(OUT))
                    again;
            }
        }
        require (
          Ensure_No_Failures_Including_In_Packs(OUT)
        );
        return OUT_BRANCHED;
    }

    // As per #1993, see notes in Loop_Integer_Common()
    //
    const bool counting_up = (s < e); // equal checked above
    if ((counting_up and b <= 0) or (not counting_up and b >= 0))
        return VOID_OUT_UNBRANCHED;  // avoid inf. loop

    while (counting_up ? *state <= e : *state >= e) {
        if (Eval_Branch_Throws(OUT, body)) {
            if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
                return THROWN;
        }
        if (Is_Hot_Potato(OUT)) {
            if (Is_Cell_A_Veto_Hot_Potato(OUT))
                return NULL_OUT_VETOING;

            if (Is_Cell_A_Retry_Hot_Potato(OUT))
                continue;  // re-run without advancing index
        }

        require (
          Ensure_No_Failures_Including_In_Packs(OUT)
        );

        if (not Is_Decimal(var))
            panic (Error_Invalid_Type_Raw(Datatype_Of(var)));

        *state += b;
    }

    if (Is_Cell_Erased(OUT))
        return VOID_OUT_UNBRANCHED;

    return OUT_BRANCHED;
}


//
//  /cfor: native [
//
//  "Evaluate a block over a range of values (See also: REPEAT)"
//
//      return: [any-value?]
//      @(word) [_ word! 'word! $word! '$word!]
//      start [any-series? any-number?]
//      end [any-series? any-number?]
//      bump [any-number?]
//      body [<const> block! frame!]  ; [A]
//  ]
//
DECLARE_NATIVE(CFOR)
{
    INCLUDE_PARAMS_OF_CFOR;

    Element* word = Element_ARG(WORD);
    Element* body = ARG(BODY);

    require (
      VarList* varlist = Create_Loop_Context_May_Bind_Body(body, word)
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(WORD), varlist));

    Add_Definitional_Continue(body, level_);

    Fixed(Slot*) slot = Varlist_Fixed_Slot(varlist, 1);

    if (
        Is_Integer(ARG(START))
        and Is_Integer(ARG(END))
        and Is_Integer(ARG(BUMP))
    ){
        return Loop_Integer_Common(
            level_,
            slot,
            ARG(BODY),
            VAL_INT64(ARG(START)),
            Is_Decimal(ARG(END))
                ? cast(REBI64, VAL_DECIMAL(ARG(END)))
                : VAL_INT64(ARG(END)),
            VAL_INT64(ARG(BUMP))
        );
    }

    if (Any_Series(ARG(START))) {
        if (Any_Series(ARG(END))) {
            return Loop_Series_Common(
                level_,
                slot,
                ARG(BODY),
                ARG(START),
                Series_Index(ARG(END)),
                Int32(ARG(BUMP))
            );
        }
        else {
            return Loop_Series_Common(
                level_,
                slot,
                ARG(BODY),
                ARG(START),
                Int32s(ARG(END), 1) - 1,
                Int32(ARG(BUMP))
            );
        }
    }

    return Loop_Number_Common(
        level_, slot, ARG(BODY), ARG(START), ARG(END), ARG(BUMP)
    );
}


//
//  /for-skip: native [
//
//  "Evaluates a block for periodic values in a series"
//
//      return: [
//          any-stable?     "last body result"
//          <null>          "if BREAK encountered"
//          ~(<null>)~      "if body result was NULL"
//      ]
//      @(word) "Word to set each time, no new var if $word"
//          [_ word! ^word! $word! 'word! '^word! '$word!]
//      series [<opt> any-series?]
//      skip [integer!]
//      body [<const> block! frame!]  ; [A]
//  ]
//
DECLARE_NATIVE(FOR_SKIP)
{
    INCLUDE_PARAMS_OF_FOR_SKIP;

    if (not ARG(SERIES))
        return Init_Heavy_Void(OUT);

    Element* series = unwrap Element_ARG(SERIES);

    Element* word = Element_ARG(WORD);
    Element* body = ARG(BODY);

    REBINT skip = Int32(ARG(SKIP));
    if (skip == 0)  // https://forum.rebol.info/t/infinite-loop-vs-error/936
        return VOID_OUT_UNBRANCHED;

    require (
      VarList* varlist = Create_Loop_Context_May_Bind_Body(body, word)
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(WORD), varlist));

    Add_Definitional_Continue(body, level_);

    Fixed(Slot*) slot = Varlist_Fixed_Slot(varlist, 1);

    Stable* spare = Copy_Cell(SPARE, series);

    // Starting location when past end with negative skip:
    //
    if (
        skip < 0
        and SERIES_INDEX_UNBOUNDED(spare) >= Series_Len_Head(spare)
    ){
        SERIES_INDEX_UNBOUNDED(spare) = Series_Len_Head(spare) + skip;
    }

    while (true) {
        REBINT len = Series_Len_Head(spare);  // always >= 0
        REBINT index = SERIES_INDEX_UNBOUNDED(spare);  // maybe set < 0 below

        if (index < 0)
            break;
        if (index >= len) {
            if (skip >= 0)
                break;
            index = len + skip;  // negative
            if (index < 0)
                break;
            SERIES_INDEX_UNBOUNDED(spare) = index;
        }

        trap (
          Write_Loop_Slot_May_Unbind_Or_Decay(slot, spare)
        );

        attempt {
            if (Eval_Branch_Throws(OUT, ARG(BODY))) {
                if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
                    return THROWN;
            }
            if (Is_Hot_Potato(OUT)) {
                if (Is_Cell_A_Veto_Hot_Potato(OUT))
                    return NULL_OUT_VETOING;

                if (Is_Cell_A_Retry_Hot_Potato(OUT))
                    again;
            }
        }

        require (
          Ensure_No_Failures_Including_In_Packs(OUT)
        );

        // Modifications to var are allowed, to another ANY-SERIES? value.
        //
        require (
          Read_Slot(spare, slot)
        );
        if (not Any_Series(spare))
            panic (spare);

        // Increment via skip, which may go before 0 or after the tail of
        // the series.
        //
        // !!! Should also check for overflows of Index range.
        //
        SERIES_INDEX_UNBOUNDED(spare) += skip;
    }

    if (Is_Cell_Erased(OUT))
        return VOID_OUT_UNBRANCHED;

    return OUT_BRANCHED;
}


//
//  /definitional-stop: native [
//
//  "End the current iteration of CYCLE, optionally returning a value"
//
//      return: []
//      :with "Act as if loop body finished with this value"
//          [any-stable?]
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_STOP)  // See CYCLE for notes about STOP
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_STOP;

    Value* with = SCRATCH;
    if (not ARG(WITH))
        Init_Void(SCRATCH);  // See: https://forum.rebol.info/t/1965/3 [1]
    else
        Copy_Cell(SCRATCH, unwrap ARG(WITH));

    Level* stop_level = LEVEL;  // Level of this STOP call

    Option(VarList*) coupling = Level_Coupling(stop_level);
    if (not coupling)
        panic (Error_Archetype_Invoked_Raw());

    Level* loop_level = Level_Of_Varlist_May_Panic(unwrap coupling);

    Element* label = Init_Frame(
        SPARE,
        Frame_Phase(LIB(DEFINITIONAL_STOP)),
        CANON(STOP),
        loop_level->varlist
    );

    Init_Thrown_With_Label(LEVEL, with, label);
    return BOUNCE_THROWN;
}


//
//  Add_Definitional_Stop: C
//
void Add_Definitional_Stop(
    Element* body,
    Level* loop_level
){
    if (not Is_Block(body)) {
        assert(Is_Frame(body));
        return;
    }

    Context* parent = List_Binding(body);

    Force_Level_Varlist_Managed(loop_level);

    Let* let_stop = Make_Let_Variable(CANON(STOP), parent);
    Init_Action(
        Stub_Cell(let_stop),
        Frame_Phase(LIB(DEFINITIONAL_STOP)),
        CANON(STOP),  // relabel (the STOP in lib is a dummy action)
        loop_level->varlist  // what to stop
    );

    Tweak_Cell_Binding(body, let_stop);  // extend chain
}


//
//  /cycle: native [
//
//  "Evaluate branch endlessly until BREAK gives NULL or a STOP gives a result"
//
//      return: [<null> any-stable?]
//      body [<const> block! frame!]  ; [A]
//  ]
//
DECLARE_NATIVE(CYCLE)
{
    INCLUDE_PARAMS_OF_CYCLE;

    Element* body = ARG(BODY);

    enum {
        ST_CYCLE_INITIAL_ENTRY = STATE_0,
        ST_CYCLE_EVALUATING_BODY
    };

    switch (STATE) {
      case ST_CYCLE_INITIAL_ENTRY: goto initial_entry;
      case ST_CYCLE_EVALUATING_BODY: goto body_result_or_thrown_in_out;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Add_Definitional_Continue(body, level_);
    Add_Definitional_Stop(body, level_);

    STATE = ST_CYCLE_EVALUATING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);
    return CONTINUE(OUT, body);

} body_result_or_thrown_in_out: {  ///////////////////////////////////////////

  // 1. Most loops can't explicitly return a value and stop looping, since
  //    that would make it impossible to tell from the outside whether they
  //    requested a stop or if they'd naturally completed.  It would be
  //    impossible to propagate a value-bearing break request to aggregate
  //    looping constructs without invasively rebinding the break.
  //
  //    CYCLE is different because it doesn't have any loop exit condition.
  //    Hence it responds to a STOP request, which lets it return any value.
  //
  // 2. Technically, we know CYCLE's body will always run.  We could make an
  //    exception to having it return void from STOP, or pure NULL.  There's
  //    probably no good reason to do that, so right now we stick with the
  //    usual branch policies.  Review if a good use case shows up.

    if (THROWING) {
        const Stable* label = VAL_THROWN_LABEL(LEVEL);
        if (
            Is_Frame(label)
            and Frame_Phase(label) == Frame_Phase(LIB(DEFINITIONAL_STOP))
            and Frame_Coupling(label) == Level_Varlist(LEVEL)
        ){
            CATCH_THROWN(OUT, LEVEL);  // Unlike BREAK, STOP takes an arg--[1]

            if (Is_Light_Null(OUT))
                return Init_Heavy_Null(OUT);  // NULL usually for BREAK [2]

            return OUT;
        }
    }

    if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
        return THROWN;

    if (Is_Hot_Potato(OUT)) {
        if (Is_Cell_A_Veto_Hot_Potato(OUT))
            return NULL_OUT_VETOING;

        if (Is_Cell_A_Retry_Hot_Potato(OUT))
           goto continue_cycling;
    }

    require (
      Ensure_No_Failures_Including_In_Packs(OUT)
    );

    goto continue_cycling;

} continue_cycling: { ////////////////////////////////////////////////////////

    return CONTINUE(OUT, body);
}}


// !!! The point of accepting QUOTED in the XXX-EACH iterators is for the
// efficiency of not creating blocks.  But work needs to be done on the
// internals to realize that efficiency.  For now, be slow and make the block
// on the insides of the iteration, with an eye to improving in the future.
//
static void Morph_Quoted_To_Block(Element* data)
{
    assert(Is_Quoted(data));
    Source* a = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
    Unquote_Cell(Copy_Cell(Stub_Cell(a), data));
    Init_Block(data, a);
}


struct Reb_Enum_Series {
    Index index;  // index into the data for filling current variable
    REBLEN len;  // length of the data
};

typedef struct Reb_Enum_Series ESER;

typedef struct {
    Element* data;  // possibly API handle if converted from sequence
    Value* generator;  // Action to generate data (if applicable)

    const Flex* flex;  // Flex being enumerated (if applicable)
    union {
        EVARS evars;
        ESER eser;
    } u;
    bool took_hold;
    bool more_data;
    bool want_dual;  // GET:DUAL semantics
} LoopEachState;

//
//  Init_Loop_Each_May_Alias_Data: C
//
Element* Init_Loop_Each_May_Alias_Data(
    Sink(Element) iterator,
    Value* data_arg
){
    assert(not Is_Api_Value(data_arg));  // used to be cue to free, but not now

    require (
      LoopEachState *les = Alloc_On_Heap(LoopEachState)
    );

    Element* data = nullptr;

  handle_action: {

  // Enumerating actions used to mean calling them repeatedly, but it became
  // more useful to enumerate their PARAMETER! values.  This is consistent
  // with the idea that things like APPEND.DUP report back a PARAMETER!, vs.
  // panic on trying to pick out of an ACTION!.

    if (Is_Action(data_arg)) {
        Deactivate_Action(data_arg);
        les->want_dual = true;
    }
    else  // dual enumeration should be some kind of ITERATOR!, generically
        les->want_dual = false;

    data = As_Element(data_arg);
    les->generator = nullptr;

} handle_lifted_action: {

  // !!! As an interim workaround for the lack of ITERATOR!, we use quasi
  // frames to signify a desire to call an action repeatedly
  //
  // The value is generated each time by calling the data action.  Actions
  // are antiform FRAME!, and frame enumeration is distinct from this, so
  // the unstable antiform status has to be distinguished.  We don't want
  // handling `les->data` to be awkward due to being possibly unstable, so
  // put the generator in a separate field.

    if (Is_Lifted_Action(data)) {
        Copy_Cell(data_arg, data);
        Unstably_Antiformize_Unbound_Fundamental(data_arg);

        les->took_hold = false;
        les->more_data = true;  // !!! Needs to do first call
        les->flex = nullptr;
        les->generator = data_arg;

        data = nullptr;
        goto return_iterator;
    }

} convert_quoted_to_block: {

    if (Is_Quoted(data))
        Morph_Quoted_To_Block(data);

} convert_sequence_to_block: {

  // Iterating sequences is currently rare, so rather than trying to figure
  // out how to iterate the various optimized forms just turn them into
  // a BLOCK!.  This builds on top of the AS aliasing code, which may be
  // able to reuse an Array if the sequence is uncompressed.  Note that
  // each iteration of the same optimized series would create a new block,
  // so it may be that AS aliasing should deoptimize the sequences (though
  // this would break the invariant that sequences that could be optimized
  // are optimized).

    if (Any_Sequence(data)) {  // alias paths, chains, tuples as BLOCK!
        DECLARE_ELEMENT (temp);
        assume (  // all sequences can alias as block
            Alias_Any_Sequence_As(temp, cast(Element*, data), TYPE_BLOCK)
        );
        Copy_Cell(data, temp);
    }

} handle_any_series: {

    if (Any_Series(data)) {
        les->flex = Cell_Flex(data);
        les->u.eser.index = Series_Index(data);
        les->u.eser.len = Series_Len_Head(data);  // has HOLD, won't change
        goto take_hold_and_continue;
    }

} handle_module: {

    if (Is_Module(data)) {
        les->flex = EMPTY_ARRAY;  // !!! workaround, not a Flex
        Init_Evars(&les->u.evars, data);
        goto take_hold_and_continue;
    }

} handle_any_context: {

    if (Any_Context(data)) {
        les->flex = Varlist_Array(Cell_Varlist(data));
        Init_Evars(&les->u.evars, data);
        goto take_hold_and_continue;
    }

} handle_map: {

    if (Is_Map(data)) {
        les->flex = MAP_PAIRLIST(VAL_MAP(data));
        les->u.eser.index = 0;
        les->u.eser.len = Flex_Used(les->flex);  // immutable--has HOLD
        goto take_hold_and_continue;
    }

    crash ("Illegal type passed to Loop_Each()");

} take_hold_and_continue: {

  // HOLD so length can't change

    les->took_hold = Not_Flex_Flag(les->flex, FIXED_SIZE);
    if (les->took_hold)
        Set_Flex_Flag(les->flex, FIXED_SIZE);

    if (Any_Context(data)) {
        les->more_data = Try_Advance_Evars(&les->u.evars);
    }
    else {
        les->more_data = (les->u.eser.index < les->u.eser.len);
    }

    goto return_iterator;

  return_iterator: { /////////////////////////////////////////////////////////

    les->data = data;  // shorter to use plain `data` above

    return Init_Handle_Cdata(iterator, les, sizeof(les));
}}}


// Common to FOR-EACH, MAP-EACH, and EVERY.  This takes an enumeration state
// and fills variables in a context with as much of that state as possible.
// The context containing the variables is created from a block:
//
//      >> for-each [a b] [1 2 3 4] [-- a b]]
//      -- a: 1 b: 2
//      -- a: 3 b: 4
//
// ANY-CONTEXT? and MAP! allow one var (keys) or two vars (keys/vals).
//
// It's possible to opt out of variable slots using SPACE.
//
static Result(bool) Loop_Each_Next_Maybe_Done(Level* level_)
{
    INCLUDE_PARAMS_OF_FOR_EACH;  // must be frame-compatible

  #if NEEDFUL_DOES_CORRUPTIONS
    assert(Not_Cell_Readable(SPARE));
    assert(Not_Cell_Readable(SCRATCH));
  #endif

    Element* vars = Element_ARG(VARS);  // becomes context on initial_entry
    UNUSED(ARG(DATA));  // les->data is used, may be API handle (?)
    UNUSED(ARG(BODY));

    Element* iterator = Element_LOCAL(ITERATOR);  // holds Loop_Each_State

    VarList* vars_ctx = Cell_Varlist(vars);
    LoopEachState *les = Cell_Handle_Pointer(LoopEachState, iterator);

    if (not les->more_data)
        return true;  // no more data

    const Slot* slot_tail;
    Slot* slot = Varlist_Slots(&slot_tail, vars_ctx);
    for (; slot != slot_tail; ++slot) {
        if (not les->more_data) {  // Y is void in `for-each [x y] [1] ...`
            Init_Void_Signifying_Unset(SPARE);
            trap (
              Write_Loop_Slot_May_Unbind_Or_Decay(slot, SPARE)
            );
            goto maybe_lift_and_continue;
        }

        if (les->generator) {
            assert(not les->data);
            Value* generated = rebUndecayed(rebRUN(les->generator));
            if (not generated)
                Init_Null(SPARE);
            else
                Copy_Cell(SPARE, generated);  // need mutable, non-nulled cell
            rebRelease(generated);

            if (not Is_Cell_A_Done_Hot_Potato(SPARE)) {
                trap (
                  Write_Loop_Slot_May_Unbind_Or_Decay(slot, SPARE)
                );
            }
            else {
                les->more_data = false;  // any remaining vars must be unset
                if (slot == Varlist_Slots_Head(vars_ctx)) {
                    //
                    // If we don't have at least *some* of the variables
                    // set for this body loop run, don't run the body.
                    //
                    return true;
                }
                Init_Void_Signifying_Unset(SPARE);
                trap (
                  Write_Loop_Slot_May_Unbind_Or_Decay(slot, SPARE)
                );
            }

            goto maybe_lift_and_continue;
        }

      switch_on_heart: {

        Heart heart = Heart_Of_Builtin_Fundamental(les->data);

        if (Any_List_Type(heart)) {
            Element* spare_element = Copy_Cell_May_Bind(
                SPARE,
                Array_At(cast(Array*, les->flex), les->u.eser.index),
                List_Binding(les->data)
            );
            trap (
              Write_Loop_Slot_May_Unbind_Or_Decay(slot, spare_element)
            );
            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;

            goto maybe_lift_and_continue;
        }

        if (Any_Context_Type(heart)) {
            Element* spare_key = Init_Word(
                SPARE, Key_Symbol(les->u.evars.key)
            );

            if (heart == TYPE_MODULE) {
                Tweak_Word_Index(spare_key, INDEX_PATCHED);
                Tweak_Cell_Binding(spare_key, Cell_Module_Sea(les->data));
            }
            else {
                Tweak_Cell_Binding(spare_key, Cell_Varlist(les->data));
                Tweak_Word_Index(spare_key, les->u.evars.n);
            }
            trap (
              Write_Loop_Slot_May_Unbind_Or_Decay(slot, spare_key)
            );

            if (Varlist_Len(vars_ctx) == 1) {
                //
                // Only wanted the key (`for-each 'key obj [...]`)
            }
            else if (Varlist_Len(vars_ctx) == 2) {
                ++slot;

                // Want keys and values (`for-each [key val] obj [...]`)
                //
                if (les->want_dual)
                    Read_Slot_Dual(SPARE, les->u.evars.slot);
                else {
                    trap (
                        Read_Slot_Meta(SPARE, les->u.evars.slot)
                    );
                }
                trap (  // heeds LOOP_SLOT_ROOT_META, errors if unstable w/o
                    Write_Loop_Slot_May_Unbind_Or_Decay(slot, SPARE)
                );
            }
            else
                panic ("Loop enumeration of contexts must be 1 or 2 vars");

            les->more_data = Try_Advance_Evars(&les->u.evars);

            goto maybe_lift_and_continue;
        }

        if (heart == TYPE_MAP) {
            assert(les->u.eser.index % 2 == 0);  // should be on key slot

            const Stable* key;
            const Stable* val;
            while (true) {  // pass over the unused map slots
                key = (
                    Array_At(cast(Array*, les->flex), les->u.eser.index)
                );
                ++les->u.eser.index;
                val = (
                    Array_At(cast(Array*, les->flex), les->u.eser.index)
                );
                ++les->u.eser.index;
                if (les->u.eser.index == les->u.eser.len)
                    les->more_data = false;
                if (not Is_Zombie(val))
                    break;
                if (not les->more_data)
                    return true;  // done
            } while (Is_Zombie(val));

            Stable* spare_key = Copy_Cell(SPARE, key);
            trap (
              Write_Loop_Slot_May_Unbind_Or_Decay(slot, spare_key)
            );

            if (Varlist_Len(vars_ctx) == 1) {
                //
                // Only wanted the key (`for-each 'key map [...]`)
            }
            else if (Varlist_Len(vars_ctx) == 2) {
                //
                // Want keys and values (`for-each 'key val map [...]`)
                //
                ++slot;
                Stable* spare_val = Copy_Cell(SPARE, val);
                trap (
                  Write_Loop_Slot_May_Unbind_Or_Decay(slot, spare_val)
                );
            }
            else
                panic ("Loop enumeration of contexts must be 1 or 2 vars");

            goto maybe_lift_and_continue;
        }

        if (Any_String_Type(heart)) {
            Element* spare_rune = Init_Char_Unchecked(
                SPARE,
                Get_Strand_Char_At(cast(Strand*, les->flex), les->u.eser.index)
            );

            trap (
              Write_Loop_Slot_May_Unbind_Or_Decay(slot, spare_rune)
            );

            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;

            goto maybe_lift_and_continue;
        }

        if (heart == TYPE_BLOB) {
            const Binary* b = cast(Binary*, les->flex);

            Element* spare_integer = Init_Integer(
                SPARE, Binary_Head(b)[les->u.eser.index]
            );
            trap (
              Write_Loop_Slot_May_Unbind_Or_Decay(slot, spare_integer)
            );

            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;

            goto maybe_lift_and_continue;
        }

        crash (les->data);

    } maybe_lift_and_continue: { /////////////////////////////////////////////

        // LIFTING NOW HANDLED BY WRITE (but other post-processing?)
    }}

    return false;  // more data to process
}

//
//  Shutdown_Loop_Each: C
//
// Cleanups that need to be done despite error, throw, etc.
//
void Shutdown_Loop_Each(Stable* iterator)
{
    LoopEachState *les = Cell_Handle_Pointer(LoopEachState, iterator);

    if (les->took_hold)  // release read-only lock
        Clear_Flex_Flag(les->flex, FIXED_SIZE);

    if (les->generator)
        assert(not les->data);
    else if (Any_Context(les->data))
        Shutdown_Evars(&les->u.evars);

    Free_Memory(LoopEachState, les);
    Init_Null(iterator);
}


//
//  /for-each: native [
//
//  "Evaluates a block for each value(s) in a series"
//
//      return: [
//          any-stable?     "last body result (if not NULL)"
//          ~(<null>)~      "if last body result was NULL"
//          <null>          "if BREAK encountered"
//          void!           "if body never ran"
//      ]
//      @(vars) "Word or block of words to set each time, no new var if $word"
//          [_ word! ^word! $word! 'word! '^word! '$word! block!]
//      ^data "The series to traverse"
//          [<opt> any-series? any-context? map! any-sequence?
//           action! quoted!]  ; action support experimental, e.g. generators
//      body [<const> block! frame!]  ; [A]
//      {iterator}
//  ]
//
DECLARE_NATIVE(FOR_EACH)
{
    INCLUDE_PARAMS_OF_FOR_EACH;

    Element* vars = Element_ARG(VARS);  // becomes context on initial_entry
    Option(Value*) data = ARG(DATA);
    Element* body = ARG(BODY);  // bound to vars on initial_entry

    Element* iterator;  // holds Loop_Each_State, all paths must cleanup!

    bool breaking = false;

    enum {
        ST_FOR_EACH_INITIAL_ENTRY = STATE_0,
        ST_FOR_EACH_INITIALIZED_ITERATOR,
        ST_FOR_EACH_RUNNING_BODY
    };

    if (STATE != ST_FOR_EACH_INITIAL_ENTRY)
        goto not_initial_entry;

  initial_entry: {

    // 1. If there is an abrupt panic, e.g. a `panic()` that could happen
    //    even in the code of this dispatcher, we need to clean up the
    //    iterator state.

    if (not data)  // same response as to empty series
        return Init_Heavy_Void(OUT);

    require (
      VarList* varlist = Create_Loop_Context_May_Bind_Body(body, vars)
    );

    Remember_Cell_Is_Lifeguard(Init_Object(vars, varlist));

    Add_Definitional_Continue(body, level_);

    iterator = Init_Loop_Each_May_Alias_Data(LOCAL(ITERATOR), unwrap data);
    STATE = ST_FOR_EACH_INITIALIZED_ITERATOR;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // need to finalize_for_each

    goto next_iteration;

} not_initial_entry: {  //////////////////////////////////////////////////////

    iterator = Element_LOCAL(ITERATOR);

    switch (STATE) {
      case ST_FOR_EACH_INITIALIZED_ITERATOR:
        assert(Is_Throwing_Panic(LEVEL));  // this dispatcher panic()'d
        goto finalize_for_each;

      case ST_FOR_EACH_RUNNING_BODY:
        goto body_result_in_spare_or_threw;

      default: assert(false);
    }

} next_iteration: {  /////////////////////////////////////////////////////////

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Corrupt_Cell_If_Needful(SCRATCH));

    require (
      bool done = Loop_Each_Next_Maybe_Done(LEVEL)
    );
    if (done)
        goto finalize_for_each;

} invoke_body: {

    STATE = ST_FOR_EACH_RUNNING_BODY;
    return CONTINUE_BRANCH(OUT, body);

} body_result_in_spare_or_threw: {  //////////////////////////////////////////

    if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
        goto finalize_for_each;

    if (Is_Hot_Potato(OUT)) {
        if (Is_Cell_A_Veto_Hot_Potato(OUT)) {
            breaking = true;
            goto finalize_for_each;
        }

        if (Is_Cell_A_Retry_Hot_Potato(OUT))
            goto invoke_body;
    }

    require (
      Ensure_No_Failures_Including_In_Packs(OUT)
    );

    goto next_iteration;

} finalize_for_each: {  //////////////////////////////////////////////////////

    Shutdown_Loop_Each(iterator);

    if (THROWING)
        return THROWN;

    if (breaking)
        return NULL_OUT_VETOING;

    if (Is_Cell_Erased(OUT))
        return VOID_OUT_UNBRANCHED;

    return OUT_BRANCHED;
}}


//
//  /every: native [
//
//  "Iterate and return null if any previous body evaluations were falsey"
//
//      return: [
//          any-stable?     "last body result (if not NULL)"
//          ~(<null>)~      "if last body result was NULL"
//          <null>          "if any body eval was NULL, or BREAK encountered"
//          void!           "if body never ran"
//      ]
//      @(vars) "Word or block of words to set each time, no new var if $word"
//          [_ word! ^word! $word! 'word! '^word! '$word! block!]
//      ^data [<opt> any-series? any-context? map! action!]
//      body [<const> block! frame!]  ; [A]
//      {iterator}
//  ]
//
DECLARE_NATIVE(EVERY)
{
    INCLUDE_PARAMS_OF_EVERY;

    Element* vars = Element_ARG(VARS);  // becomes context on initial_entry
    Option(Value*) data = ARG(DATA);
    Element* body = ARG(BODY);  // bound to vars on initial_entry

    Stable* iterator;  // holds Loop_Each_State, all paths must cleanup!

    enum {
        ST_EVERY_INITIAL_ENTRY = STATE_0,
        ST_EVERY_INITIALIZED_ITERATOR,
        ST_EVERY_RUNNING_BODY
    };

    if (STATE != ST_EVERY_INITIAL_ENTRY)
        goto not_initial_entry;

  initial_entry: {

    if (not data)  // same response as to empty series
        return Init_Heavy_Void(OUT);

    require (
      VarList* varlist = Create_Loop_Context_May_Bind_Body(body, vars)
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), varlist));

    Add_Definitional_Continue(body, level_);

    iterator = Init_Loop_Each_May_Alias_Data(LOCAL(ITERATOR), unwrap data);
    STATE = ST_EVERY_INITIALIZED_ITERATOR;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // need to finalize_every

    goto next_iteration;

} not_initial_entry: {  //////////////////////////////////////////////////////

    iterator = Stable_LOCAL(ITERATOR);

    switch (STATE) {
      case ST_EVERY_INITIALIZED_ITERATOR:
        assert(Is_Throwing_Panic(LEVEL));  // this dispatcher panic()'d
        goto finalize_every;

      case ST_EVERY_RUNNING_BODY:
        goto body_result_in_spare;

      default: assert(false);
    }

} next_iteration: {

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Corrupt_Cell_If_Needful(SCRATCH));

    require (
      bool done = Loop_Each_Next_Maybe_Done(LEVEL)
    );
    if (done)
        goto finalize_every;

} invoke_body: {

    STATE = ST_EVERY_RUNNING_BODY;
    return CONTINUE(SPARE, body);

} body_result_in_spare: {  ///////////////////////////////////////////////////

    // 1. In light of other tolerances in the system for voids in logic tests
    //    (see ALL & ANY), EVERY treats a void as "no vote".
    //
    //        every 'x [1 2 3 4] [if even? x [x]]  =>  4
    //
    //        every 'x [1 2 3 4] [opt if odd? x [x]]  =>  ~<?>~ antiform
    //
    //    It returns trash on skipped bodies, as loop composition breaks
    //    down if we try to keep old values, or return void.

    if (Loop_Body_Threw_And_Cant_Catch_Continue(SPARE, LEVEL))
        goto finalize_every;

    if (Is_Hot_Potato(SPARE)) {
        if (Is_Cell_A_Veto_Hot_Potato(SPARE)) {
            Init_Null_Signifying_Vetoed(OUT);
            goto finalize_every;
        }

        if (Is_Cell_A_Retry_Hot_Potato(SPARE))
            goto invoke_body;
    }

    if (Any_Void(SPARE)) {
        Init_Tripwire(OUT);  // forget OUT for loop composition [1]
        goto next_iteration;  // ...but void does not NULL-lock output
    }

    require (
      Stable* spare = Decay_If_Unstable(SPARE)
    );
    if (not Logical_Test(spare)) {
        Init_Null(OUT);
    }
    else if (Is_Cell_Erased(OUT) or not Is_Light_Null(OUT)) {
        Move_Cell(OUT, SPARE);
    }

    goto next_iteration;

} finalize_every: {  /////////////////////////////////////////////////////////

    Shutdown_Loop_Each(iterator);

    if (THROWING)
        return THROWN;

    if (Is_Cell_Erased(OUT))
        return VOID_OUT_UNBRANCHED;

    return OUT;
}}


//
//  /remove-each: native [
//
//  "Removes values for each body evaluation that's not null, modifies input"
//
//      return: [<null> ~([none? any-series?] integer!)~]
//      @(vars) "Word or block of words to set each time, no new var if $word"
//          [_ word! ^word! $word! 'word! '^word! '$word! block!]
//      data "The series to traverse (modified)"
//          [<opt> any-series?]  ; !!! can't do QUOTED!
//      body [<const> block! frame!]  ; [A]
//  ]
//
DECLARE_NATIVE(REMOVE_EACH)
//
// Note: For semantics and performance, REMOVE-EACH doesn't actually perform
// removals "as it goes".  It could run afoul of any number of problems,
// including the mutable series becoming locked during iteration.  Hence the
// series is locked, and removals aren't applied until the end.  However, this
// means that there's state which must be finalized on every possible exit
// path.  (Errors, throws, completion)
{
    INCLUDE_PARAMS_OF_REMOVE_EACH;

    Element* vars = Element_ARG(VARS);
    Element* body = ARG(BODY);

    Count removals = 0;

    if (not ARG(DATA)) {
        Init_None(OUT);
        goto return_pack;
    }

  process_non_blank: { ////////////////////////////////////////////////////=//

    Element* data = unwrap Element_ARG(DATA);

    // 1. Updating arrays in place may not be better than pushing values to
    //    the data stack and creating a precisely-sized output Flex to swap as
    //    underlying memory for the array.  (Imagine a large array with many
    //    removals, and the ensuing wasted space being left behind).  We use
    //    the method anyway, to test novel techniques and error handling.
    //
    // 2. For blobs and strings, we push new data as the loop runs.  Then at
    //    the end of the enumeration, the identity of the incoming series is
    //    kept but new underlying data is poked into it, old data is freed.

    Flex* flex = Cell_Flex_Ensure_Mutable(data);  // check even if empty

    if (Series_Index(data) >= Series_Len_At(data))  // past series end
        return VOID_OUT_UNBRANCHED;

    require (
      VarList* varlist = Create_Loop_Context_May_Bind_Body(body, vars)
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), varlist));

    Add_Definitional_Continue(body, level_);

    Index start = Series_Index(data);

    DECLARE_MOLDER (mo);
    if (Any_List(data)) {  // use BASE_FLAG_MARKED to mark for removal [1]
        Corrupt_If_Needful(mo);
    }
    else {  // generate new data allocation and swap content in the Flex [2]
        Push_Mold(mo);
    }

    Set_Flex_Info(flex, HOLD);  // disallow mutations until finalize

    Length len = Any_String(data)
        ? Strand_Len(cast(Strand*, flex))
        : Flex_Used(flex);  // temp read-only, this won't change

    bool threw = false;
    bool breaking = false;

    Index index = start;
    while (index < len) {
        assert(start == index);

        const Slot* slot_tail;
        Fixed(Slot*) slot = Varlist_Fixed_Slots(&slot_tail, varlist);
        for (; slot != slot_tail; ++slot) {
            if (index == len) {  // Y on 2nd step of remove-each [x y] "abc"
                Init_Null(SPARE);
                require (
                  Write_Loop_Slot_May_Unbind_Or_Decay(slot, SPARE)
                );
                continue;  // the `for` loop setting variables
            }

            if (Any_List(data)) {
                Copy_Cell_May_Bind(
                    SPARE,
                    Array_At(Cell_Array(data), index),
                    List_Binding(data)
                );
            }
            else if (Is_Blob(data)) {
                Binary* b = cast(Binary*, flex);
                Init_Integer(SPARE, cast(REBI64, Binary_Head(b)[index]));
            }
            else {
                assert(Any_String(data));
                Init_Char_Unchecked(
                    SPARE,
                    Get_Strand_Char_At(cast(Strand*, flex), index)
                );
            }
            require (
              Write_Loop_Slot_May_Unbind_Or_Decay(slot, SPARE)
            );

            ++index;
        }

    invoke_loop_body: {  /////////////////////////////////////////////////////

        if (Eval_Any_List_At_Throws(OUT, body, SPECIFIED)) {
            if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL)) {
                threw = true;
                goto finalize_remove_each;
            }
        }

  } process_body_result: {

    // The only signals allowed are OKAY, NULL, and VOID.  This likely catches
    // more errors than allowing any Logical_Test() value to mean "remove"
    // (e.g. use DID MATCH or NOT MATCH instead of just MATCH).
    //
    // 1. BREAK/VETO means there is no change applied to the series.  It's
    //    conceivable that might not be what people want--and that if they did
    //    want that, they would likely use a MAP-EACH or something to generate
    //    a new series.  But NULL is reserved for when loops break, so there
    //    would not be a way to get the removal count in this case.  Hence it
    //    is semantically easiest to say BREAK goes along with "no effect".
    //
    // 2. The reason VOID is tolerated is because CONTINUE with no argument
    //    acts as if the body returned VOID.  This is a general behavioral
    //    rule for loops, and it's most useful if that doesn't remove.

        bool keep;

        if (Is_Hot_Potato(OUT)) {
            if (Is_Cell_A_Veto_Hot_Potato(OUT)) {
                breaking = true;  // break semantics are no-op [1]
                assert(start < len);
                goto finalize_remove_each;
            }

            if (Is_Cell_A_Retry_Hot_Potato(OUT))
                goto invoke_loop_body;
        }

        if (Any_Void(OUT)) {
            keep = true;  // treat same as logic false (e.g. don't remove) [2]
            goto handle_keep_or_no_keep;
        }

      decay_out: {

        require (
          Stable* out = Decay_If_Unstable(OUT)
        );
        if (Is_Okay(out)) {  // pure logic required [1]
            keep = false;  // okay is remove
        }
        else if (Is_Null(out)) {  // don't remove
            keep = true;
            Init_Heavy_Null(OUT);  // NULL reserved for BREAK signal
        }
        else {
            threw = true;
            Element* spare = Init_Error_Cell(
                SPARE,
                Error_User("Use [NULL OKAY VOID] with REMOVE-EACH")
            );
            Init_Thrown_With_Label(LEVEL, LIB(NULL), spare);
            goto finalize_remove_each;
        }

    } handle_keep_or_no_keep: ////////////////////////////////////////////////

        if (Any_List(data)) {
            if (keep) {
                start = index;
                continue;  // keeping, don't mark for culling
            }

            do {
                assert(start <= len);
                Set_Cell_Flag(Array_At(Cell_Array(data), start), NOTE_REMOVE);
                ++start;
            } while (start != index);
        }
        else {
            if (not keep) {
                start = index;
                continue;  // not keeping, don't save to buffer
            }

            do {
                assert(start <= len);
                if (Is_Blob(data)) {
                    Binary* b = cast(Binary*, flex);
                    require (
                      Append_Ascii_Len(
                        mo->strand,
                        s_cast(Binary_At(b, start)),
                        1
                    ));
                }
                else {
                    Append_Codepoint(
                        mo->strand,
                        Get_Strand_Char_At(cast(Strand*, flex), start)
                    );
                }
                ++start;
            } while (start != index);
        }

        if (Is_Light_Null(OUT))
            Init_Heavy_Null(OUT);  // reserve pure NULL for BREAK
    }

} finalize_remove_each: {  ///////////////////////////////////////////////////

    // 7. We are reusing the mold buffer for BLOB!, but *not putting UTF-8
    //    data* into it.  Revisit if this inhibits cool UTF-8 based tricks
    //    the mold buffer might do otherwise.

    if (not threw and not breaking)
        assert(start == len);  // normal completion

    assert(Get_Flex_Info(flex, HOLD));
    Clear_Flex_Info(flex, HOLD);

    if (Any_List(data)) {
        if (not threw and breaking) {  // clean marks, don't remove
            const Element* tail;
            Element* temp = List_At_Known_Mutable(&tail, data);
            for (; temp != tail; ++temp) {
                if (Get_Cell_Flag(temp, NOTE_REMOVE))
                    Clear_Cell_Flag(temp, NOTE_REMOVE);
            }
            goto done_finalizing;
        }

        Copy_Cell(OUT, data);  // going to be the same series

        const Element* tail;
        Element* dest = List_At_Known_Mutable(&tail, data);
        Element* src = dest;

        // avoid blitting cells onto themselves by making the first thing we
        // do is to pass up all the unmarked (kept) cells.
        //
        while (src != tail and Not_Cell_Flag(src, NOTE_REMOVE)) {
            ++src;
            ++dest;
        }

        // If we get here, we're either at the end, or all the cells from here
        // on are going to be moving to somewhere besides the original spot
        //
        for (; dest != tail; ++dest, ++src) {
            while (src != tail and Get_Cell_Flag(src, NOTE_REMOVE)) {
                ++src;
                --len;
                ++removals;
            }
            if (src == tail) {
                Set_Flex_Len(Cell_Array_Known_Mutable(data), len);
                goto done_finalizing;
            }
            Copy_Cell(dest, src);  // same array, so we can do this
        }

        assert(removals == 0);  // didn't goto, so no removals
    }
    else if (Is_Blob(data)) {
        if (not threw and breaking) {  // leave data unchanged
            Drop_Mold(mo);
            goto done_finalizing;
        }

        Binary* b = cast(Binary*, flex);

        // If there was a THROW, or panic() we need the remaining data
        //
        REBLEN orig_len = Series_Len_Head(data);
        assert(start <= orig_len);
        require (
          Append_Ascii_Len(
            mo->strand,
            s_cast(Binary_At(b, start)),
            orig_len - start
        ));

        Binary* popped = Pop_Molded_Binary(mo);  // not UTF-8 if binary [7]

        assert(Binary_Len(popped) <= Series_Len_Head(data));
        removals = Series_Len_Head(data) - Binary_Len(popped);

        Swap_Stub_Content(popped, b);  // swap identity, process_non_blank:[1]

        Free_Unmanaged_Flex(popped);  // now frees incoming Flex's data
        Init_Blob(OUT, b);
    }
    else {
        assert(Any_String(data));
        if (not threw and breaking) {  // leave data unchanged
            Drop_Mold(mo);
            goto done_finalizing;
        }

        // If there was a THROW, or panic() we need the remaining data
        //
        REBLEN orig_len = Series_Len_Head(data);
        assert(start <= orig_len);

        Strand* s = cast(Strand*, flex);

        for (; start != orig_len; ++start)
            Append_Codepoint(mo->strand, Get_Strand_Char_At(s, start));

        Strand* popped = Pop_Molded_Strand(mo);

        assert(Strand_Len(popped) <= Series_Len_Head(data));
        removals = Series_Len_Head(data) - Strand_Len(popped);

        Swap_Stub_Content(popped, s);  // swap Flex identity [3]

        Free_Unmanaged_Flex(popped);  // frees incoming Flex's data
        Init_Any_String(OUT, Heart_Of_Builtin_Fundamental(data), s);
    }

  done_finalizing:

    if (threw)
        return THROWN;

    if (breaking)
        return NULL_OUT_VETOING;

    assert(Type_Of(As_Stable(OUT)) == Type_Of(data));

}} return_pack: { //////////////////////////////////////////////////////////=//

    Source* pack = Make_Source(2);
    Set_Flex_Len(pack, 2);

    Copy_Lifted_Cell(Array_At(pack, 0), OUT);
    Lift_Cell(Init_Integer(Array_At(pack, 1), removals));

    return Init_Pack(OUT, pack);
}}


//
//  /map-each: native [
//
//  "Evaluate a block for each value(s) in a series and collect as a block"
//
//      return: [<null> block!]
//      @(vars) "Word or block of words to set each time, no new var if $word"
//          [_ word! ^word! $word! 'word! '^word! '$word! block!]
//      ^data "The series to traverse"
//          [
//              <opt>
//              quoted!
//              any-series? any-sequence? any-context?
//              action!
//          ]
//      body [<const> block! frame!]  ; [A]
//      {iterator}
//  ]
//
DECLARE_NATIVE(MAP_EACH)
//
// MAP-EACH lacks the planned flexibility of MAP.  The syntax of FOR and MAP
// are intended to be generic to work with generators or a dialect.
//
// 1. The theory is that MAP would use a dialect on BLOCK! arguments for data
//    by default, like [1 thru 10].  But you could give it an arbitrary
//    enumerating action and it would iteratively call it.  Since such an
//    iterator does not exist yet (and would not be cheap) a QUOTED? BLOCK!
//    is used temporarily as a substitute for passing a block iterator.
{
    INCLUDE_PARAMS_OF_MAP_EACH;

    if (not ARG(DATA))  // same as empty list
        return Init_Block(OUT, Make_Source_Managed(0));

    Value* data = unwrap ARG(DATA);
    if (not Is_Action(data))
        Quote_Cell(As_Element(data));  // dialect, in theory [1]

    const Value* map_action = LIB(MAP);
    Details* details = Ensure_Frame_Details(map_action);

  require (
    Tweak_Level_Phase(LEVEL, details)
  );

    Tweak_Level_Coupling(LEVEL, Frame_Coupling(map_action));

    Dispatcher* dispatcher = Details_Dispatcher(details);
    return Apply_Cfunc(dispatcher, LEVEL);
}


//
//  /map: native [
//
//  "Evaluate a block for each value(s) in a series and collect as a block"
//
//      return: [<null> block!]
//      @(vars) "Word or block of words to set each time, no new var if $word"
//          [_ word! ^word! $word! 'word! '^word! '$word! block!]
//      ^data "The series to traverse (only QUOTED? BLOCK! at the moment...)"
//          [<opt> quoted! action!]
//      body [<const> block! frame!]  ; [A]
//      {iterator}
//  ]
//
DECLARE_NATIVE(MAP)
{
    INCLUDE_PARAMS_OF_MAP;

    Element* vars = Element_ARG(VARS);  // becomes context on initial_entry
    Option(Value*) data_arg = ARG(DATA);  // action invokes, frame enumerates
    Element* body = ARG(BODY);  // bound to vars on initial_entry

    Stable* iterator;  // holds Loop_Each_State, all paths must cleanup!

    enum {
        ST_MAP_INITIAL_ENTRY = STATE_0,
        ST_MAP_INITIALIZED_ITERATOR,
        ST_MAP_RUNNING_BODY
    };

    if (STATE != ST_MAP_INITIAL_ENTRY)
        goto not_initial_entry;

  initial_entry: {

    assert(Is_Cell_Erased(OUT));  // output only written in MAP if BREAK hit

    if (not data_arg)  // same response as empty
        return Init_Block(OUT, Make_Source(0));

    Add_Definitional_Continue(body, level_);

    if (Is_Action(unwrap data_arg)) {
        // treat as a generator
    }
    else {
        Element* data = As_Element(unwrap data_arg);
        if (
            not Is_Quoted(data)
            or Quotes_Of(data) != 1
            or not (
                Any_Series(Unquote_Cell(data))  // <= UNQUOTIFY here!
                or Is_Path(data)  // has been unquoted
                or Any_Context(data)
                or Any_Sequence(data)
            )
        ){
            panic (
                "MAP only supports one-level QUOTED? series/path/context ATM"
            );
        }
    }

    require (
      VarList* varlist = Create_Loop_Context_May_Bind_Body(body, vars)
    );

    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), varlist));

    iterator = Init_Loop_Each_May_Alias_Data(LOCAL(ITERATOR), unwrap data_arg);
    STATE = ST_MAP_INITIALIZED_ITERATOR;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // need to finalize_map

    goto next_iteration;

} not_initial_entry: {  //////////////////////////////////////////////////////

    iterator = Stable_LOCAL(ITERATOR);

    switch (STATE) {
      case ST_MAP_INITIALIZED_ITERATOR:
        assert(Is_Throwing_Panic(LEVEL));  // this dispatcher panic()'d
        goto finalize_map;

      case ST_MAP_RUNNING_BODY:
        goto body_result_in_spare;

      default: assert(false);
    }

} next_iteration: {  /////////////////////////////////////////////////////////

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Corrupt_Cell_If_Needful(SCRATCH));

    require (
      bool done = Loop_Each_Next_Maybe_Done(LEVEL)
    );
    if (done)
        goto finalize_map;

} invoke_loop_body: {

    STATE = ST_MAP_RUNNING_BODY;
    return CONTINUE(SPARE, body);  // body may be ^BLOCK!

} body_result_in_spare: {  ///////////////////////////////////////////////////

    // Use APPEND semantics on the body result; whatever APPEND would do with
    // the value, we do the same.  (Ideally the code could be unified.)
    //
    // e.g. void is allowed for skipping map elements:
    //
    //        map-each 'x [1 2 3] [opt if even? x [x * 10]] => [20]

    if (Loop_Body_Threw_And_Cant_Catch_Continue(SPARE, LEVEL))
        goto finalize_map;

    if (Is_Hot_Potato(SPARE)) {
        if (Is_Cell_A_Veto_Hot_Potato(SPARE)) {
            Init_Null_Signifying_Vetoed(OUT);
            goto finalize_map;
        }
        if (Is_Cell_A_Retry_Hot_Potato(SPARE))
            goto invoke_loop_body;
    }

    if (Any_Void(SPARE))
        goto next_iteration;  // okay to skip

    if (Is_Cell_A_Veto_Hot_Potato(SPARE)) {
        Init_Null_Signifying_Vetoed(OUT);
        goto finalize_map;
    }

    require (
      Stable* spare = Decay_If_Unstable(SPARE)
    );
    if (Is_Splice(spare)) {
        const Element* tail;
        const Element* v = List_At(&tail, spare);
        for (; v != tail; ++v)
            Copy_Cell(PUSH(), v);  // Note: no binding on antiform SPLICE!
    }
    else if (Is_Antiform(spare)) {
        Init_Thrown_Panic(LEVEL, Error_Bad_Antiform(spare));
        goto finalize_map;
    }
    else
        Copy_Cell(PUSH(), spare);  // non nulls added to result

    goto next_iteration;

} finalize_map: {  ///////////////////////////////////////////////////////////

    // 1. MAP and MAP-EACH always return blocks except in cases of BREAK, e.g.
    //    there's no way to detect from the outside if the body never ran.
    //    Are variants useful? (e.g. COLLECT* is NULL if nothing collected)

    Shutdown_Loop_Each(iterator);

    if (THROWING)
        return THROWN;  // automatically drops to baseline

    if (Not_Cell_Erased(OUT)) {  // only modifies on break or veto
        assert(Is_Light_Null(OUT));  // BREAK or VETO, so *must* return null
        Drop_Data_Stack_To(STACK_BASE);
        return NULL_OUT_VETOING;
    }

    return Init_Block(  // always returns block unless break [1]
        OUT,
        Pop_Source_From_Stack(STACK_BASE)
    );
}}


//
//  /repeat: native [
//
//  "Evaluates a block a specified number of times"
//
//      return: [
//          any-stable?     "last body result (if not NULL)"
//          ~(<null>)~      "if last body result was NULL"
//          <null>          "if BREAK encountered"
//          void!           "if body never ran"
//      ]
//      count "Repetitions (true loops infinitely, false doesn't run)"
//          [<opt> any-number? logic!]
//      body [<const> block! frame!]  ; [A]
//  ]
//
DECLARE_NATIVE(REPEAT)
//
// 1. We pass the index into the body if it's an ACTION! as we count.  But if
//    it's a LOGIC! TRUE no index is passed, because we don't count.  If we
//    were using arbitrary precision arithmetic, the count could have a
//    non-trivial cost to upkeep in large loops.
{
    INCLUDE_PARAMS_OF_REPEAT;

    Stable* count = opt ARG(COUNT);  // nullptr checked in initial_entry
    Element* body = ARG(BODY);

    Stable* index = u_cast(Stable*, SPARE);  // current index, erased on entry

    enum {
        ST_REPEAT_INITIAL_ENTRY = STATE_0,
        ST_REPEAT_EVALUATING_BODY
    };

    switch (STATE) {
      case ST_REPEAT_INITIAL_ENTRY : goto initial_entry;
      case ST_REPEAT_EVALUATING_BODY : goto body_result_in_out;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (not count)
        return VOID_OUT_UNBRANCHED;  // treat <opt> void input as "don't run"

    if (Is_Logic(count)) {
        if (Cell_Logic(count) == false)
            return VOID_OUT_UNBRANCHED;  // treat false as "don't run"
        Init_True(index);
    }
    else if (VAL_INT64(count) <= 0)
        return VOID_OUT_UNBRANCHED;  // negative means "don't run" (vs. error)
    else {
        assert(Any_Number(count));
        Init_Integer(index, 1);
    }

    Add_Definitional_Continue(body, level_);

    STATE = ST_REPEAT_EVALUATING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // catch break/continue
    return CONTINUE_BRANCH(OUT, body, index);

} body_result_in_out: {  /////////////////////////////////////////////////////

    if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
        return THROWN;

    if (Is_Hot_Potato(OUT)) {
        if (Is_Cell_A_Veto_Hot_Potato(OUT))
            return NULL_OUT_VETOING;

        if (Is_Cell_A_Retry_Hot_Potato(OUT))
            goto invoke_loop_body;
    }

    require (
      Ensure_No_Failures_Including_In_Packs(OUT)
    );

    if (Is_Logic(count)) {
        assert(Cell_Logic(count) == true);  // false already returned
        return CONTINUE_BRANCH(OUT, body);  // true infinite loops
    }

    if (VAL_INT64(count) == VAL_INT64(index))  // reached the desired count
        return OUT_BRANCHED;

    mutable_VAL_INT64(index) += 1;

} invoke_loop_body: { ////////////////////////////////////////////////////////

    assert(STATE == ST_REPEAT_EVALUATING_BODY);
    assert(Get_Executor_Flag(ACTION, LEVEL, DISPATCHER_CATCHES));
    return CONTINUE_BRANCH(OUT, body, index);  // keep looping
}}


//
//  /for: native [
//
//  "Evaluates a branch a number of times or over a series, return last result"
//
//      return: [
//          any-stable?     "last body result (if not NULL)"
//          ~(<null>)~      "if last body result was NULL"
//          <null>          "if BREAK encountered"
//          void!           "if body never ran"
//      ]
//      @(vars) "Word or block of words to set each time, no new var if $word"
//          [_ word! ^word! $word! 'word! '^word! '$word! block!]
//      value "Maximum number or series to traverse"
//          [any-number? any-sequence? quoted! block!]
//      body [<const> block! frame!]  ; [A]
//  ]
//
DECLARE_NATIVE(FOR)
{
    INCLUDE_PARAMS_OF_FOR;

    Element* vars = Element_ARG(VARS);
    Element* value = Element_ARG(VALUE);
    Element* body = ARG(BODY);

    enum {
        ST_FOR_INITIAL_ENTRY = STATE_0,
        ST_FOR_RUNNING_BODY
    };

    switch (STATE) {
      case ST_FOR_INITIAL_ENTRY :
        goto initial_entry;

      case ST_FOR_RUNNING_BODY :
        goto body_result_in_out;

      default : break;
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Quoted(value)) {
        Unquote_Cell(value);

        if (not (Any_Series(value) or Any_Sequence(value)))
            panic (PARAM(VALUE));

        // Delegate to FOR-EACH (note: in the future this will be the other
        // way around, with FOR-EACH delegating to FOR).
        //
        rebPushContinuation_internal(
            u_cast(RebolValue*, OUT),  // <-- output cell (can be in valist)
            LEVEL_MASK_NONE,
            CANON(FOR_EACH), rebQ(ARG(VARS)), rebQ(value), body
        );
        return BOUNCE_DELEGATE;
    }

    if (Is_Decimal(value) or Is_Percent(value))
        Init_Integer(value, Int64(value));

    REBI64 n = VAL_INT64(value);
    if (n < 1)  // Loop_Integer from 1 to 0 with bump of 1 is infinite
        return VOID_OUT_UNBRANCHED;

    Add_Definitional_Continue(body, level_);

    trap (
      VarList* varlist = Create_Loop_Context_May_Bind_Body(body, vars)
    );
    assert(Varlist_Len(varlist) == 1);
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), varlist));

    Stable* spare_one = Init_Integer(SPARE, 1);

    Fixed(Slot*) slot = Varlist_Fixed_Slot(varlist, 1);
    trap (
      Write_Loop_Slot_May_Unbind_Or_Decay(slot, spare_one)
    );

    STATE = ST_FOR_RUNNING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for break/continue
    return CONTINUE_BRANCH(OUT, body, Slot_Hack(slot));

} body_result_in_out: {  /////////////////////////////////////////////////////

    if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
        return THROWN;

    if (Is_Hot_Potato(OUT)) {
        if (Is_Cell_A_Veto_Hot_Potato(OUT))
            return NULL_OUT_VETOING;

        if (Is_Cell_A_Retry_Hot_Potato(OUT))
            goto invoke_loop_body;
    }

    require (
      Ensure_No_Failures_Including_In_Packs(OUT)
    );

    Fixed(Slot*) slot = Varlist_Fixed_Slot(Cell_Varlist(vars), 1);

    Sink(Stable) spare = SPARE;
    trap (
      Read_Slot(spare, slot)
    );

    if (not Is_Integer(spare))
        panic (Error_Invalid_Type_Raw(Datatype_Of(spare)));

    if (VAL_INT64(spare) == VAL_INT64(value))
        return OUT_BRANCHED;

    if (Add_I64_Overflows(&mutable_VAL_INT64(spare), VAL_INT64(spare), 1))
        panic (Error_Overflow_Raw());

    trap (
      Write_Loop_Slot_May_Unbind_Or_Decay(slot, spare)
    );

    goto invoke_loop_body;

} invoke_loop_body: { ////////////////////////////////////////////////////////

    assert(STATE == ST_FOR_RUNNING_BODY);
    assert(Get_Executor_Flag(ACTION, LEVEL, DISPATCHER_CATCHES));
    return CONTINUE_BRANCH(OUT, body, SPARE);
}}


//
//  /insist: native [
//
//  "Evaluates the body until it produces a non-NULL (and non-VOID) value"
//
//      return: [any-stable?]
//      body [<const> block! frame!]  ; [A]
//  ]
//
DECLARE_NATIVE(INSIST)
{
    INCLUDE_PARAMS_OF_INSIST;

    Element* body = ARG(BODY);

    enum {
        ST_INSIST_INITIAL_ENTRY = STATE_0,
        ST_INSIST_EVALUATING_BODY
    };

    switch (STATE) {
      case ST_INSIST_INITIAL_ENTRY: goto initial_entry;
      case ST_INSIST_EVALUATING_BODY: goto body_result_in_out;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Add_Definitional_Continue(body, level_);

    STATE = ST_INSIST_EVALUATING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for BREAK, CONTINUE, etc.
    goto loop_again;

} loop_again: { /////////////////////////////////////////////////////////////

    assert(Get_Executor_Flag(ACTION, LEVEL, DISPATCHER_CATCHES));
    assert(STATE == ST_INSIST_EVALUATING_BODY);
    return CONTINUE(OUT, body);

} body_result_in_out: {  /////////////////////////////////////////////////////

  // 1. When CONTINUE has an argument, it acts like the loop body evaluated
  //    to that argument.  But INSIST's condition and body are the same, so
  //    CONTINUE:WITH OKAY will stop the INSIST and return okay, while
  //    CONTINUE:WITH 10 will stop and return 10, etc.
  //
  // 2. Due to body_result_in_out:[1], we want CONTINUE (or CONTINUE:WITH ())
  //    to keep the loop running.  For parity between what continue does with
  //    an argument and what the loop does if the body evaluates to that
  //    argument, it suggests a void body result be intent to continue.
  //
  // 3. Today we don't test undecayed values for truthiness or falseyness.
  //    Hence INSIST cannot return something like a pack...it must be META'd
  //    and the result UNMETA'd.  That would mean all pack quasiforms would
  //    be considered truthy.

    if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
        return THROWN;

    if (Is_Hot_Potato(OUT)) {
        if (Is_Cell_A_Veto_Hot_Potato(OUT))
            return NULL_OUT_VETOING;

        if (Is_Cell_A_Retry_Hot_Potato(OUT))
            goto loop_again;
    }

    if (Any_Void(OUT))
        goto loop_again;  // skip voids [2]

    require (
      Stable* stable_out = Decay_If_Unstable(OUT)
    );
    if (Logical_Test(stable_out))
        return OUT_BRANCHED;

    goto loop_again;  // not truthy, keep going
}}


// The WHILE native is used to implement UNTIL as well, with just a simple
// flag to control if the exit condition for the loop is truthy or falsey.
//
#define LEVEL_FLAG_WHILE_IS_ACTUALLY_UNTIL  LEVEL_FLAG_MISCELLANEOUS


//
//  /while: native [
//
//  "So long as a condition is not NULL, evaluate the body"
//
//      return: [<null> void! any-stable?]
//      condition [<const> block! frame!]
//      body [<const> block! frame!]  ; [A]
//  ]
//
DECLARE_NATIVE(WHILE)  // note: UNTIL shares this implementation
{
    INCLUDE_PARAMS_OF_WHILE;

    Element* condition = ARG(CONDITION);
    Element* body = ARG(BODY);

    enum {
        ST_WHILE_INITIAL_ENTRY = STATE_0,
        ST_WHILE_EVALUATING_CONDITION,
        ST_WHILE_EVALUATING_BODY
    };

    switch (STATE) {
      case ST_WHILE_INITIAL_ENTRY: goto initial_entry;
      case ST_WHILE_EVALUATING_CONDITION: goto condition_result_in_spare;
      case ST_WHILE_EVALUATING_BODY: goto body_result_in_out_or_thrown;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

  // 1. The condition can already signal to end *this* loop by being falsey.
  //    It can "break" the loop and return NULL by evaluating to ~(veto)~,
  //    or skip the body and re-run itself with ~(retry)~.  So it's better if
  //    CONTINUE* in the condition stays bound to enclosing loops.

    Add_Definitional_Continue(body, LEVEL);  // just body, not condition [1]
    goto evaluate_condition;

} evaluate_condition: {  /////////////////////////////////////////////////////

    STATE = ST_WHILE_EVALUATING_CONDITION;

    assert(Not_Executor_Flag(ACTION, LEVEL, DISPATCHER_CATCHES));
    return CONTINUE(SPARE, condition);

} condition_result_in_spare: {  //////////////////////////////////////////////

    if (Is_Hot_Potato(SPARE)) {
        if (Is_Cell_A_Done_Hot_Potato(SPARE))
            goto return_out;  // treat DONE as iterator exhausted

        if (Is_Cell_A_Retry_Hot_Potato(SPARE))
            goto evaluate_condition;  // skip body and run condition again

        if (Is_Cell_A_Veto_Hot_Potato(SPARE))
            return NULL_OUT_VETOING;  // break loop and return null
    }

    require (
      Stable* spare = Decay_If_Unstable(SPARE)
    );
    bool logic = Logical_Test(spare);

    if (Get_Level_Flag(LEVEL, WHILE_IS_ACTUALLY_UNTIL)) {  // until loop
        if (logic)
            goto return_out;  // truthy condition => last body result
    }
    else {  // while loop
        if (not logic)
            goto return_out;  // falsey condition => last body result
    }

} invoke_loop_body: {

    STATE = ST_WHILE_EVALUATING_BODY;  // body result => OUT

    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for break/continue
    return CONTINUE_BRANCH(OUT, body, SPARE);

} body_result_in_out_or_thrown: { ////////////////////////////////////////////

    if (Loop_Body_Threw_And_Cant_Catch_Continue(OUT, LEVEL))
        return THROWN;

    if (Is_Hot_Potato(OUT)) {
        if (Is_Cell_A_Veto_Hot_Potato(OUT))
            return NULL_OUT_VETOING;

        if (Is_Cell_A_Retry_Hot_Potato(OUT))
            goto invoke_loop_body;
    }

    require (
      Ensure_No_Failures_Including_In_Packs(OUT)
    );

    Disable_Dispatcher_Catching_Of_Throws(LEVEL);
    goto evaluate_condition;

} return_out: {  /////////////////////////////////////////////////////////////

    if (Is_Cell_Erased(OUT))  // body never ran
        return VOID_OUT_UNBRANCHED;  // no result to return!

    return OUT_BRANCHED;
}}


//
//  /until: native [
//
//  "So long as a condition is NULL, evaluate the body"
//
//      return: [<null> void! any-stable?]
//      condition [<const> block! frame!]
//      body [<const> block! frame!]  ; [A]
//  ]
//
DECLARE_NATIVE(UNTIL)
{
    INCLUDE_PARAMS_OF_WHILE;  // UNTIL must have same parameters as WHILE

    if (STATE == STATE_0)
        Set_Level_Flag(LEVEL, WHILE_IS_ACTUALLY_UNTIL);  // invert condition

    return Apply_Cfunc(NATIVE_CFUNC(WHILE), LEVEL);
}
