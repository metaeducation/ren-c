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

#include "sys-core.h"
#include "sys-int-funcs.h" //Add_I64_Overflows


//
//  Try_Catch_Break_Or_Continue: C
//
// Determines if a thrown value is either a break or continue.  If so, `val`
// is mutated to become the throw's argument.  For BREAK this is NULL, and
// for continue it can be any non-NULL state (including VOID, which must be
// handled by the caller.)
//
// Returning false means the throw was neither BREAK nor CONTINUE.
//
bool Try_Catch_Break_Or_Continue(
    Sink(Atom) out,
    Level* loop_level,
    bool* breaking
){
    const Value* label = VAL_THROWN_LABEL(loop_level);

    // Throw /NAME-s used by CONTINUE and BREAK are the actual native
    // function values of the routines themselves.
    //
    if (not Is_Frame(label))
        return false;

    if (
        Frame_Phase(label) == Frame_Phase(LIB(DEFINITIONAL_BREAK))
        and Cell_Frame_Coupling(label) == Level_Varlist(loop_level)
    ){
        CATCH_THROWN(out, loop_level);
        Init_Unreadable(out);  // caller must interpret breaking flag
        *breaking = true;
        return true;
    }

    if (
        Frame_Phase(label) == Frame_Phase(LIB(DEFINITIONAL_CONTINUE))
        and Cell_Frame_Coupling(label) == Level_Varlist(loop_level)
    ){
        CATCH_THROWN(out, loop_level);
        if (not Is_Void(out))  // nihil signals no argument to CONTINUE
            Assert_Cell_Stable(out);  // CONTINUE doesn't take unstable :WITH
        *breaking = false;
        return true;
    }

    return false; // caller should let all other thrown values bubble up
}


//
//  definitional-break: native [
//
//  "Exit the current iteration of a loop and stop iterating further"
//
//      return: [<divergent>]
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_BREAK)
//
// BREAK is implemented via a thrown signal that bubbles up through the stack.
// It uses the value of its own native function as the name of the throw,
// like `throw/name null :break`.
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_BREAK;

    Level* break_level = LEVEL;  // Level of this BREAK call

    Option(VarList*) coupling = Level_Coupling(break_level);
    if (not coupling)
        panic (Error_Archetype_Invoked_Raw());

    Level* loop_level = Level_Of_Varlist_May_Panic(unwrap coupling);

    Element* label = Init_Frame(
        SPARE,
        Frame_Phase(LIB(DEFINITIONAL_BREAK)),
        CANON(BREAK),
        cast(VarList*, loop_level->varlist)
    );

    Init_Thrown_With_Label(LEVEL, LIB(NULL), label);
    return BOUNCE_THROWN;
}


//
//  definitional-continue: native [
//
//  "Throws control back to top of loop for next iteration"
//
//      return: [<divergent>]
//      :with "Act as if loop body finished with this value"
//          [any-value?]
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_CONTINUE)
//
// CONTINUE is implemented via a thrown signal that bubbles up through the
// stack.  It uses the value of its own native function as the name of the
// throw, like `throw/name value :continue`.
//
// 1. How CONTINUE with no argument acts is up to the loop construct to
//    interpret.  e.g. MAP-EACH, it acts like CONTINUE:WITH ~()~.  We throw
//    the non-valued VOID state to allow for the custom interpretation.
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_CONTINUE;

    Atom* with = SCRATCH;
    if (not Bool_ARG(WITH))
        Init_Void(SCRATCH);  // See: https://forum.rebol.info/t/1965/3 [1]
    else
        Copy_Cell(SCRATCH, ARG(WITH));

    Level* continue_level = LEVEL;  // Level of this CONTINUE call

    Option(VarList*) coupling = Level_Coupling(continue_level);
    if (not coupling)
        panic (Error_Archetype_Invoked_Raw());

    Level* loop_level = Level_Of_Varlist_May_Panic(unwrap coupling);

    Element* label = Init_Frame(
        SPARE,
        Frame_Phase(LIB(DEFINITIONAL_CONTINUE)),
        CANON(CONTINUE),
        Varlist_Of_Level_Force_Managed(loop_level)
    );

    Init_Thrown_With_Label(LEVEL, with, label);
    return BOUNCE_THROWN;
}


//
//  Add_Definitional_Break_Continue: C
//
void Add_Definitional_Break_Continue(
    Element* body,
    Level* loop_level
){
    Context* parent = List_Binding(body);
    Let* let_continue = Make_Let_Variable(CANON(CONTINUE), parent);

    Init_Action(
        Stub_Cell(let_continue),
        Frame_Phase(LIB(DEFINITIONAL_CONTINUE)),
        CANON(CONTINUE),  // relabel (the CONTINUE in lib is a dummy action)
        Varlist_Of_Level_Force_Managed(loop_level)  // what to continue
    );

    Let* let_break = Make_Let_Variable(CANON(BREAK), let_continue);
    Init_Action(
        Stub_Cell(let_break),
        Frame_Phase(LIB(DEFINITIONAL_BREAK)),
        CANON(BREAK),  // relabel (the BREAK in lib is a dummy action)
        Varlist_Of_Level_Force_Managed(loop_level)  // what to break
    );

    Tweak_Cell_Binding(body, let_break);  // extend chain
}


//
//  Loop_Series_Common: C
//
static Bounce Loop_Series_Common(
    Level* level_,
    Value* var, // Must not be movable from context expansion, see #2274
    const Value* body,
    Value* start,
    REBINT end,
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
    Copy_Cell(var, start);
    REBIDX* state = &SERIES_INDEX_UNBOUNDED(var);

    // Run only once if start is equal to end...edge case.
    //
    REBINT s = Series_Index(start);
    if (s == end) {
        if (Eval_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
                return THROWN;

            if (breaking)
                return NULLED;
        }
        return OUT;
    }

    // As per #1993, start relative to end determines the "direction" of the
    // FOR loop.  (R3-Alpha used the sign of the bump, which meant it did not
    // have a clear plan for what to do with 0.)
    //
    const bool counting_up = (s < end); // equal checked above
    if ((counting_up and bump <= 0) or (not counting_up and bump >= 0))
        return VOID;  // avoid infinite loops

    while (
        counting_up
            ? *state <= end
            : *state >= end
    ){
        if (Eval_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
                return THROWN;

            if (breaking)
                return BREAKING_NULL;
        }

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
        return VOID;

    return LOOPED(OUT);
}


//
//  Loop_Integer_Common: C
//
static Bounce Loop_Integer_Common(
    Level* level_,
    Value* var,  // Must not be movable from context expansion, see #2274
    const Value* body,
    REBI64 start,
    REBI64 end,
    REBI64 bump
){
    // A value cell exposed to the user is used to hold the state.  This means
    // if they change `slot` during the loop, it affects the iteration.  Hence
    // it must be checked for changing to a non-integer form.
    //
    Reset_Cell_Header_Noquote(TRACK(var), CELL_MASK_INTEGER);
    REBI64* state = &mutable_VAL_INT64(var);
    *state = start;

    // Run only once if start is equal to end...edge case.
    //
    if (start == end) {
        if (Eval_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
                return THROWN;

            if (breaking)
                return BREAKING_NULL;
        }
        return LOOPED(OUT);
    }

    // As per #1993, start relative to end determines the "direction" of the
    // FOR loop.  (R3-Alpha used the sign of the bump, which meant it did not
    // have a clear plan for what to do with 0.)
    //
    const bool counting_up = (start < end);  // equal checked above
    if ((counting_up and bump <= 0) or (not counting_up and bump >= 0))
        return BREAKING_NULL;  // avoid infinite loops !!! void, or null?

    while (counting_up ? *state <= end : *state >= end) {
        if (Eval_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
                return THROWN;

            if (breaking)
                return BREAKING_NULL;
        }

        if (not Is_Integer(var))
            panic (Error_Invalid_Type_Raw(Datatype_Of(var)));

        if (Add_I64_Overflows(state, *state, bump))
            panic (Error_Overflow_Raw());
    }

    return LOOPED(OUT);
}


//
//  Loop_Number_Common: C
//
static Bounce Loop_Number_Common(
    Level* level_,
    Value* var,  // Must not be movable from context expansion, see #2274
    const Value* body,
    Value* start,
    Value* end,
    Value* bump
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
    Reset_Cell_Header_Noquote(TRACK(var), CELL_MASK_DECIMAL);
    REBDEC *state = &VAL_DECIMAL(var);
    *state = s;

    // Run only once if start is equal to end...edge case.
    //
    if (s == e) {
        if (Eval_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
                return THROWN;

            if (breaking)
                return BREAKING_NULL;
        }
        return LOOPED(OUT);
    }

    // As per #1993, see notes in Loop_Integer_Common()
    //
    const bool counting_up = (s < e); // equal checked above
    if ((counting_up and b <= 0) or (not counting_up and b >= 0))
        return VOID;  // avoid inf. loop, means never ran

    while (counting_up ? *state <= e : *state >= e) {
        if (Eval_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
                return THROWN;

            if (breaking)
                return BREAKING_NULL;
        }

        if (not Is_Decimal(var))
            panic (Error_Invalid_Type_Raw(Datatype_Of(var)));

        *state += b;
    }

    if (Is_Cell_Erased(OUT))
        return VOID;

    return LOOPED(OUT);
}


//
//  cfor: native [
//
//  "Evaluate a block over a range of values (See also: REPEAT)"
//
//      return: [any-value?]
//      word [word!]
//          "Variable to hold current value"
//      start [any-series? any-number?]
//          "Starting value"
//      end [any-series? any-number?]
//          "Ending value"
//      bump [any-number?]
//          "Amount to skip each time"
//      body [<const> any-branch?]
//          "Code to evaluate"
//  ]
//
DECLARE_NATIVE(CFOR)
{
    INCLUDE_PARAMS_OF_CFOR;

    Element* word = Element_ARG(WORD);
    Element* body = Element_ARG(BODY);

    VarList* varlist = require (Create_Loop_Context_May_Bind_Body(body, word));

    Remember_Cell_Is_Lifeguard(Init_Object(ARG(WORD), varlist));

    if (Is_Block(body) or Is_Meta_Form_Of(BLOCK, body))
        Add_Definitional_Break_Continue(body, level_);

    Fixed(Slot*) slot = Varlist_Fixed_Slot(varlist, 1);
    Value* var = Slot_Hack(slot);

    if (
        Is_Integer(ARG(START))
        and Is_Integer(ARG(END))
        and Is_Integer(ARG(BUMP))
    ){
        return Loop_Integer_Common(
            level_,
            var,
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
                var,
                ARG(BODY),
                ARG(START),
                Series_Index(ARG(END)),
                Int32(ARG(BUMP))
            );
        }
        else {
            return Loop_Series_Common(
                level_,
                var,
                ARG(BODY),
                ARG(START),
                Int32s(ARG(END), 1) - 1,
                Int32(ARG(BUMP))
            );
        }
    }

    return Loop_Number_Common(
        level_, var, ARG(BODY), ARG(START), ARG(END), ARG(BUMP)
    );
}


//
//  for-skip: native [
//
//  "Evaluates a block for periodic values in a series"
//
//      return: "Last body result, or null if BREAK"
//          [any-value?]
//      word "Variable set to each position in the series at skip distance"
//          [word! @word? _]
//      series "The series to iterate over"
//          [<opt-out> blank? any-series?]
//      skip "Number of positions to skip each time"
//          [<opt-out> integer!]
//      body "Code to evaluate each time"
//          [<const> any-branch?]
//  ]
//
DECLARE_NATIVE(FOR_SKIP)
{
    INCLUDE_PARAMS_OF_FOR_SKIP;

    if (Is_Blank(ARG(SERIES)))
        return VOID;

    Element* word = Element_ARG(WORD);
    Element* series = Element_ARG(SERIES);
    Element* body = Element_ARG(BODY);

    REBINT skip = Int32(ARG(SKIP));
    if (skip == 0)
        return VOID;  // https://forum.rebol.info/t/infinite-loop-vs-error/936

    VarList* varlist = require (Create_Loop_Context_May_Bind_Body(body, word));

    Remember_Cell_Is_Lifeguard(Init_Object(ARG(WORD), varlist));

    if (Is_Block(body) or Is_Meta_Form_Of(BLOCK, body))
        Add_Definitional_Break_Continue(body, level_);

    Fixed(Slot*) slot = Varlist_Fixed_Slot(varlist, 1);

    Value* spare = Copy_Cell(SPARE, series);

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
        REBINT index = SERIES_INDEX_UNBOUNDED(spare);  // may have been set to < 0 below

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

        required (Write_Loop_Slot_May_Bind(slot, spare, body));

        if (Eval_Branch_Throws(OUT, ARG(BODY))) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
                return THROWN;

            if (breaking)
                return BREAKING_NULL;
        }

        // Modifications to var are allowed, to another ANY-SERIES? value.
        //
        required (Read_Slot(spare, slot));

        if (not Any_Series(spare))
            panic (spare);

        // Increment via skip, which may go before 0 or after the tail of
        // the series.
        //
        // !!! Should also check for overflows of REBIDX range.
        //
        SERIES_INDEX_UNBOUNDED(spare) += skip;
    }

    if (Is_Cell_Erased(OUT))
        return VOID;

    return LOOPED(OUT);
}


//
//  definitional-stop: native [
//
//  "End the current iteration of CYCLE, optionally returning a value"
//
//      return: [<divergent>]
//      :with "Act as if loop body finished with this value"
//          [any-value?]
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_STOP)  // See CYCLE for notes about STOP
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_STOP;

    Atom* with = SCRATCH;
    if (not Bool_ARG(WITH))
        Init_Void(SCRATCH);  // See: https://forum.rebol.info/t/1965/3 [1]
    else
        Copy_Cell(SCRATCH, ARG(WITH));

    Level* stop_level = LEVEL;  // Level of this STOP call

    Option(VarList*) coupling = Level_Coupling(stop_level);
    if (not coupling)
        panic (Error_Archetype_Invoked_Raw());

    Level* loop_level = Level_Of_Varlist_May_Panic(unwrap coupling);

    Element* label = Init_Frame(
        SPARE,
        Frame_Phase(LIB(DEFINITIONAL_STOP)),
        CANON(STOP),
        cast(VarList*, loop_level->varlist)
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
    Context* parent = List_Binding(body);

    Force_Level_Varlist_Managed(loop_level);

    Let* let_stop = Make_Let_Variable(CANON(STOP), parent);
    Init_Action(
        Stub_Cell(let_stop),
        Frame_Phase(LIB(DEFINITIONAL_STOP)),
        CANON(STOP),  // relabel (the STOP in lib is a dummy action)
        cast(VarList*, loop_level->varlist)  // what to stop
    );

    Tweak_Cell_Binding(body, let_stop);  // extend chain
}


//
//  cycle: native [
//
//  "Evaluates a block endlessly, until a BREAK or a STOP is hit"
//
//      return: "Null if BREAK, or non-null value passed to STOP"
//          [any-value?]
//      body "Block or action to evaluate each time"
//          [<const> any-branch?]
//  ]
//
DECLARE_NATIVE(CYCLE)
{
    INCLUDE_PARAMS_OF_CYCLE;

    Element* body = Element_ARG(BODY);

    enum {
        ST_CYCLE_INITIAL_ENTRY = STATE_0,
        ST_CYCLE_EVALUATING_BODY
    };

    switch (STATE) {
      case ST_CYCLE_INITIAL_ENTRY: goto initial_entry;
      case ST_CYCLE_EVALUATING_BODY: goto body_was_evaluated;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Block(body) or Is_Meta_Form_Of(BLOCK, body)) {
        Add_Definitional_Break_Continue(body, level_);
        Add_Definitional_Stop(body, level_);
    }

    STATE = ST_CYCLE_EVALUATING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);
    return CONTINUE(OUT, body);

} body_was_evaluated: {  /////////////////////////////////////////////////////

    if (THROWING)
        goto handle_thrown;

    return CONTINUE(OUT, body);  // no break or stop, so keep going

} handle_thrown: {  /////////////////////////////////////////////////////////

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

    bool breaking;
    if (Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking)) {
        if (breaking)
            return BREAKING_NULL;

        return CONTINUE(OUT, body);  // plain continue
    }

    const Value* label = VAL_THROWN_LABEL(LEVEL);
    if (
        Is_Frame(label)
        and Frame_Phase(label) == Frame_Phase(LIB(DEFINITIONAL_STOP))
        and Cell_Frame_Coupling(label) == Level_Varlist(LEVEL)
    ){
        CATCH_THROWN(OUT, LEVEL);  // Unlike BREAK, STOP takes an arg--[1]

        if (Is_Light_Null(OUT))
            return Init_Heavy_Null(OUT);  // NULL usually for BREAK [2]

        return OUT;
    }

    return THROWN;
}}


struct Reb_Enum_Series {
    REBLEN index;  // index into the data for filling current variable
    REBLEN len;  // length of the data
};

typedef struct Reb_Enum_Series ESER;

typedef struct {
    Value* data;  // possibly API handle if converted from sequence
    const Flex* flex;  // Flex being enumerated (if applicable)
    union {
        EVARS evars;
        ESER eser;
    } u;
    bool took_hold;
    bool more_data;
} LoopEachState;

//
//  Init_Loop_Each_May_Alias_Data: C
//
// 1. Iterating sequences is currently rare, so rather than trying to figure
//    out how to iterate the various optimized forms just turn them into
//    a BLOCK!.  This builds on top of the AS aliasing code, which may be
//    able to reuse an Array if the sequence is uncompressed.  Note that
//    each iteration of the same optimized series would create a new block,
//    so it may be that AS aliasing should deoptimize the sequences (though
//    this would break the invariant that sequences that could be optimized
//    are optimized).
//
Element* Init_Loop_Each_May_Alias_Data(Sink(Element) iterator, Value* data)
{
    assert(not Is_Api_Value(data));  // used to be cue to free, but not now

    LoopEachState *les = require (Alloc_On_Heap(LoopEachState));

    if (Any_Sequence(data)) {  // alias paths, chains, tuples as BLOCK!
        DECLARE_ELEMENT (temp);
        guaranteed (  // all sequences can alias as block
            Alias_Any_Sequence_As(temp, cast(Element*, data), TYPE_BLOCK)
        );
        Copy_Cell(data, temp);
    }

    if (Is_Action(data)) {
        //
        // The value is generated each time by calling the data action.
        // Assign values to avoid compiler warnings.
        //
        les->took_hold = false;
        les->more_data = true;  // !!! Needs to do first call
        les->flex = nullptr;
    }
    else {
        if (Any_Series(data)) {
            les->flex = Cell_Flex(data);
            les->u.eser.index = Series_Index(data);
            les->u.eser.len = Series_Len_Head(data);  // has HOLD, won't change
        }
        else if (Is_Module(data)) {
            les->flex = g_empty_array;  // !!! workaround, not a Flex
            Init_Evars(&les->u.evars, Known_Element(data));
        }
        else if (Any_Context(data)) {
            les->flex = Varlist_Array(Cell_Varlist(data));
            Init_Evars(&les->u.evars, Known_Element(data));
        }
        else if (Is_Map(data)) {
            les->flex = MAP_PAIRLIST(VAL_MAP(data));
            les->u.eser.index = 0;
            les->u.eser.len = Flex_Used(les->flex);  // immutable--has HOLD
        }
        else
            crash ("Illegal type passed to Loop_Each()");

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
    }

    les->data = data;  // shorter to use plain `data` above

    return Init_Handle_Cdata(iterator, les, sizeof(les));
}


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
        if (not les->more_data) {  // Y is null in `for-each [x y] [1] ...`
            trapped (Write_Loop_Slot_May_Bind(slot, nullptr, les->data));

            goto maybe_lift_and_continue;
        }

        if (Is_Action(les->data)) {
            Value* generated = rebLift(rebRUN(les->data));
            if (not (
                Is_Lifted_Error(generated)
                and Is_Error_Done_Signal(Cell_Error(generated))
            )) {
                Unliftify_Decayed(generated) excepted (Error* e) {
                    rebRelease(generated);
                    return fail (e);
                };
                Write_Loop_Slot_May_Bind(
                    slot, generated, les->data
                ) excepted (Error* e) {
                    rebRelease(generated);
                    return fail (e);
                }
                rebRelease(generated);
            }
            else {
                rebRelease(generated);
                les->more_data = false;  // any remaining vars must be unset
                if (slot == Varlist_Slots_Head(vars_ctx)) {
                    //
                    // If we don't have at least *some* of the variables
                    // set for this body loop run, don't run the body.
                    //
                    return true;
                }
                trapped (Write_Loop_Slot_May_Bind(slot, nullptr, les->data));
            }

            goto maybe_lift_and_continue;
        }

      switch_on_heart: {

        Heart heart = Heart_Of_Builtin_Fundamental(Known_Element(les->data));

        if (Any_List_Type(heart)) {
            Element* spare_element = Copy_Cell(
                SPARE,
                Array_At(cast(Array*, les->flex), les->u.eser.index)
            );
            trapped (Write_Loop_Slot_May_Bind(slot, spare_element, les->data));
            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;

            goto maybe_lift_and_continue;
        }

        if (Any_Context_Type(heart)) {
            assert(les->u.evars.index != 0);

            Element* spare_key = Init_Word(
                SPARE, Key_Symbol(les->u.evars.key)
            );

            if (heart == TYPE_MODULE) {
                Tweak_Word_Index(spare_key, INDEX_PATCHED);
                Tweak_Cell_Binding(spare_key, Cell_Module_Sea(les->data));
            }
            else {
                Tweak_Cell_Binding(spare_key, Cell_Varlist(les->data));
                Tweak_Word_Index(spare_key, les->u.evars.index);
            }
            trapped (Write_Loop_Slot_May_Bind(slot, spare_key, les->data));

            if (Varlist_Len(vars_ctx) == 1) {
                //
                // Only wanted the key (`for-each 'key obj [...]`)
            }
            else if (Varlist_Len(vars_ctx) == 2) {
                ++slot;

                // Want keys and values (`for-each 'key val obj [...]`)
                //
                if (Is_Dual_Unset(les->u.evars.slot)) {
                    Flags persist = (
                        slot->header.bits & CELL_MASK_PERSIST_SLOT
                    );
                    Init_Dual_Unset(slot);  // !!! hack!
                    slot->header.bits |= persist;  // preserve persist flags
                }
                else {
                    Sink(Value) spare_val = SPARE;
                    trapped (Read_Slot(spare_val, les->u.evars.slot));

                    trapped (
                        Write_Loop_Slot_May_Bind(slot, spare_val, les->data)
                    );
                }
            }
            else
                panic ("Loop enumeration of contexts must be 1 or 2 vars");

            les->more_data = Try_Advance_Evars(&les->u.evars);

            goto maybe_lift_and_continue;
        }

        if (heart == TYPE_MAP) {
            assert(les->u.eser.index % 2 == 0);  // should be on key slot

            const Value* key;
            const Value* val;
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

            Value* spare_key = Copy_Cell(SPARE, key);
            trapped (Write_Loop_Slot_May_Bind(slot, spare_key, les->data));

            if (Varlist_Len(vars_ctx) == 1) {
                //
                // Only wanted the key (`for-each 'key map [...]`)
            }
            else if (Varlist_Len(vars_ctx) == 2) {
                //
                // Want keys and values (`for-each 'key val map [...]`)
                //
                ++slot;
                Value* spare_val = Copy_Cell(SPARE, val);
                trapped (Write_Loop_Slot_May_Bind(slot, spare_val, les->data));
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

            trapped (Write_Loop_Slot_May_Bind(slot, spare_rune, les->data));

            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;

            goto maybe_lift_and_continue;
        }

        if (heart == TYPE_BLOB) {
            const Binary* b = cast(Binary*, les->flex);

            Element* spare_integer = Init_Integer(
                SPARE, Binary_Head(b)[les->u.eser.index]
            );
            trapped (Write_Loop_Slot_May_Bind(slot, spare_integer, les->data));

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
void Shutdown_Loop_Each(Value* iterator)
{
    LoopEachState *les = Cell_Handle_Pointer(LoopEachState, iterator);

    if (les->took_hold)  // release read-only lock
        Clear_Flex_Flag(les->flex, FIXED_SIZE);

    if (Any_Context(les->data))
        Shutdown_Evars(&les->u.evars);

    Free_Memory(LoopEachState, les);
    Init_Unreadable(iterator);
}


//
//  for-each: native [
//
//  "Evaluates a block for each value(s) in a series"
//
//      return: "Last body result, or null if BREAK"
//          [any-value?]
//      vars "Word or block of words to set each time, no new var if @word"
//          [_ word! @word! block!]
//      data "The series to traverse"
//          [<opt-out> blank? any-series? any-context? map! any-sequence?
//           action!]  ; action support experimental, e.g. generators
//      body "Block to evaluate each time"
//          [<const> block! ^block!]
//      <local> iterator
//  ]
//
DECLARE_NATIVE(FOR_EACH)
{
    INCLUDE_PARAMS_OF_FOR_EACH;

    Element* vars = Element_ARG(VARS);  // becomes context on initial_entry
    Value* data = ARG(DATA);
    Element* body = Element_ARG(BODY);  // bound to vars on initial_entry

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

    if (Is_Blank(data))  // same response as to empty series
        return VOID;

    VarList* varlist = require (Create_Loop_Context_May_Bind_Body(body, vars));

    Remember_Cell_Is_Lifeguard(Init_Object(vars, varlist));

    if (Is_Block(body) or Is_Meta_Form_Of(BLOCK, body))
        Add_Definitional_Break_Continue(body, level_);

    iterator = Init_Loop_Each_May_Alias_Data(LOCAL(ITERATOR), data);
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

    bool done = require (Loop_Each_Next_Maybe_Done(LEVEL));
    if (done)
        goto finalize_for_each;

    STATE = ST_FOR_EACH_RUNNING_BODY;
    return CONTINUE_BRANCH(OUT, body);

} body_result_in_spare_or_threw: {  //////////////////////////////////////////

    if (THROWING) {
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            goto finalize_for_each;

        if (breaking)
            goto finalize_for_each;
    }

    goto next_iteration;

} finalize_for_each: {  //////////////////////////////////////////////////////

    Shutdown_Loop_Each(iterator);

    if (THROWING)
        return THROWN;

    if (breaking)
        return BREAKING_NULL;

    if (Is_Cell_Erased(OUT))
        return VOID;

    return LOOPED(OUT);
}}


//
//  every: native [
//
//  "Iterate and return null if any previous body evaluations were falsey"
//
//      return: "null on BREAK, void on empty, null or the last non-null value"
//          [any-value?]
//      vars "Word or block of words to set each time, no new var if @word"
//          [_ word! @word! block!]
//      data "The series to traverse"
//          [<opt-out> blank? any-series? any-context? map! action!]
//      body [<const> block! ^block!]
//          "Block to evaluate each time"
//      <local> iterator
//  ]
//
DECLARE_NATIVE(EVERY)
{
    INCLUDE_PARAMS_OF_EVERY;

    Element* vars = Element_ARG(VARS);  // becomes context on initial_entry
    Value* data = ARG(DATA);
    Element* body = Element_ARG(BODY);  // bound to vars on initial_entry

    Element* iterator;  // holds Loop_Each_State, all paths must cleanup!

    enum {
        ST_EVERY_INITIAL_ENTRY = STATE_0,
        ST_EVERY_INITIALIZED_ITERATOR,
        ST_EVERY_RUNNING_BODY
    };

    if (STATE != ST_EVERY_INITIAL_ENTRY)
        goto not_initial_entry;

  initial_entry: {

    if (Is_Blank(data))  // same response as to empty series
        return VOID;

    VarList* varlist = require (Create_Loop_Context_May_Bind_Body(body, vars));

    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), varlist));

    if (Is_Block(body) or Is_Meta_Form_Of(BLOCK, body))
        Add_Definitional_Break_Continue(body, level_);

    iterator = Init_Loop_Each_May_Alias_Data(LOCAL(ITERATOR), data);
    STATE = ST_EVERY_INITIALIZED_ITERATOR;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // need to finalize_every

    goto next_iteration;

} not_initial_entry: {  //////////////////////////////////////////////////////

    iterator = Element_LOCAL(ITERATOR);

    switch (STATE) {
      case ST_EVERY_INITIALIZED_ITERATOR:
        assert(Is_Throwing_Panic(LEVEL));  // this dispatcher panic()'d
        goto finalize_every;

      case ST_EVERY_RUNNING_BODY:
        goto body_result_in_spare;

      default: assert(false);
    }

} next_iteration: {  /////////////////////////////////////////////////////////

    heeded (Corrupt_Cell_If_Needful(SPARE));
    heeded (Corrupt_Cell_If_Needful(SCRATCH));

    bool done = require (Loop_Each_Next_Maybe_Done(LEVEL));
    if (done)
        goto finalize_every;

    STATE = ST_EVERY_RUNNING_BODY;
    return CONTINUE(SPARE, body);

} body_result_in_spare: {  ///////////////////////////////////////////////////

    // 1. In light of other tolerances in the system for voids in logic tests
    //    (see ALL & ANY), EVERY treats a void as "no vote".
    //
    //        every 'x [1 2 3 4] [if even? x [x]]  =>  4
    //
    //        every 'x [1 2 3 4] [opt if odd? x [x]]  =>  ~ antiform
    //
    //    It returns trash on skipped bodies, as loop composition breaks
    //    down if we try to keep old values, or return void.

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(SPARE, LEVEL, &breaking))
            goto finalize_every;

        if (breaking) {
            Init_Nulled(OUT);
            goto finalize_every;
        }
    }

    if (Is_Ghost_Or_Void(SPARE)) {
        Init_Tripwire(OUT);  // forget OUT for loop composition [1]
        goto next_iteration;  // ...but void does not NULL-lock output
    }

    Value* spare = require (Decay_If_Unstable(SPARE));

    bool cond = require (Test_Conditional(spare));
    if (not cond) {
        Init_Nulled(OUT);
    }
    else if (Is_Cell_Erased(OUT) or not Is_Light_Null(OUT)) {
        Move_Atom(OUT, SPARE);
    }

    goto next_iteration;

} finalize_every: {  /////////////////////////////////////////////////////////

    Shutdown_Loop_Each(iterator);

    if (THROWING)
        return THROWN;

    if (Is_Cell_Erased(OUT))
        return VOID;

    return OUT;
}}


//
//  remove-each: native [
//
//  "Removes values for each block that returns true"
//
//      return: "Modified Input"
//          [null? ~[[blank? any-series?] integer!]~]
//      vars "Word or block of words to set each time, no new var if @word"
//          [_ word! @word! block!]
//      data "The series to traverse (modified)"
//          [<opt-out> blank? any-series?]
//      body "Block to evaluate (return TRUE to remove)"
//          [<const> block!]
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
    Element* body = Element_ARG(BODY);

    Count removals = 0;

    if (Is_Blank(ARG(DATA))) {
        Init_Blank(OUT);
        goto return_pack;
    }

  process_non_blank: { ////////////////////////////////////////////////////=//

    Element* data = Element_ARG(DATA);

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
        return NULLED;

    VarList* varlist = require (Create_Loop_Context_May_Bind_Body(body, vars));

    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), varlist));

    if (Is_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    REBLEN start = Series_Index(data);

    DECLARE_MOLDER (mo);
    if (Any_List(data)) {  // use BASE_FLAG_MARKED to mark for removal [1]
        Corrupt_If_Needful(mo);
    }
    else {  // generate new data allocation and swap content in the Flex [2]
        Push_Mold(mo);
    }

    Set_Flex_Info(flex, HOLD);  // disallow mutations until finalize

    REBLEN len = Any_String(data)
        ? Strand_Len(cast(Strand*, flex))
        : Flex_Used(flex);  // temp read-only, this won't change

    bool threw = false;
    bool breaking = false;

    REBLEN index = start;
    while (index < len) {
        assert(start == index);

        const Slot* slot_tail;
        Fixed(Slot*) slot = Varlist_Fixed_Slots(&slot_tail, varlist);
        for (; slot != slot_tail; ++slot) {
            Value* var = Slot_Hack(slot);
            if (index == len) {
                Init_Nulled(var);  // Y on 2nd step of remove-each [x y] "abc"
                continue;  // the `for` loop setting variables
            }

            if (Any_List(data))
                Derelativize(
                    var,
                    Array_At(Cell_Array(data), index),
                    List_Binding(data)
                );
            else if (Is_Blob(data)) {
                Binary* b = cast(Binary*, flex);
                Init_Integer(var, cast(REBI64, Binary_Head(b)[index]));
            }
            else {
                assert(Any_String(data));
                Init_Char_Unchecked(
                    var,
                    Get_Strand_Char_At(cast(Strand*, flex), index)
                );
            }
            ++index;
        }

    invoke_loop_body: {  /////////////////////////////////////////////////////

    // 1. When a BREAK happens there is no change applied to the series.  It's
    //    conceivable that might not be what people want--and that if they did
    //    want that, they would likely use a MAP-EACH or something to generate
    //    a new series.  But NULL is reserved for when loops break, so there
    //    would not be a way to get the removal count in this case.  Hence it
    //    is semantically easiest to say BREAK goes along with "no effect".

        if (Eval_Any_List_At_Throws(OUT, body, SPECIFIED)) {
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking)) {
                threw = true;
                goto finalize_remove_each;
            }

            if (breaking) {  // break semantics are no-op [1]
                assert(start < len);
                goto finalize_remove_each;
            }
        }

  } process_body_result: {  //////////////////////////////////////////////////

    // The only signals allowed are OKAY, NULL, and VOID.  This likely catches
    // more errors than allowing any Test_Conditional() value to mean "remove"
    // (e.g. use DID MATCH or NOT MATCH instead of just MATCH).
    //
    // 1. The reason VOID is tolerated is because CONTINUE with no argument
    //    acts as if the body returned VOID.  This is a general behavioral
    //    rule for loops, and it's most useful if that doesn't remove.

        bool keep;

        if (Is_Void(OUT)) {
            keep = true;  // treat same as logic false (e.g. don't remove) [1]
            goto handle_keep_or_no_keep;
        }

      decay_out: {

        Value* out = require (Decay_If_Unstable(OUT));

        if (Is_Okay(out)) {  // pure logic required [1]
            keep = false;  // okay is remove
        }
        else if (Is_Nulled(out)) {  // don't remove
            keep = true;
            Init_Heavy_Null(OUT);  // NULL reserved for BREAK signal
        }
        else {
            threw = true;
            Element* spare = Init_Warning(
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
                    required (Append_Ascii_Len(
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
        required (Append_Ascii_Len(
            mo->strand,
            s_cast(Binary_At(b, start)),
            orig_len - start
        ));

        Binary* popped = Pop_Molded_Binary(mo);  // not UTF-8 if binary [7]

        assert(Binary_Len(popped) <= Series_Len_Head(data));
        removals = Series_Len_Head(data) - Binary_Len(popped);

        Swap_Flex_Content(popped, b);  // swap identity, process_non_blank:[1]

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

        Swap_Flex_Content(popped, s);  // swap Flex identity [3]

        Free_Unmanaged_Flex(popped);  // frees incoming Flex's data
        Init_Any_String(OUT, Heart_Of_Builtin_Fundamental(data), s);
    }

  done_finalizing:

    if (threw)
        return THROWN;

    if (breaking)
        return BREAKING_NULL;

    assert(Type_Of(OUT) == Type_Of(data));

}} return_pack: { //////////////////////////////////////////////////////////=//

    Source* pack = Make_Source(2);
    Set_Flex_Len(pack, 2);

    Copy_Lifted_Cell(Array_At(pack, 0), OUT);
    Liftify(Init_Integer(Array_At(pack, 1), removals));

    return Init_Pack(OUT, pack);
}}


//
//  map-each: native [
//
//  "Evaluate a block for each value(s) in a series and collect as a block"
//
//      return: "Collected block"
//          [null? block!]
//      vars "Word or block of words to set each time, no new var if @word"
//          [_ word! @word! block!]
//      data "The series to traverse"
//          [<opt-out> blank? any-series? any-sequence? any-context?]
//      body "Block to evaluate each time (result will be kept literally)"
//          [<const> block!]
//      <local> iterator
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

    UNUSED(PARAM(VARS));
    UNUSED(PARAM(BODY));
    UNUSED(LOCAL(ITERATOR));

    if (Is_Blank(ARG(DATA)))  // should have same result as empty list
        return Init_Block(OUT, Make_Source_Managed(0));

    Quotify(Element_ARG(DATA));  // dialect, in theory [1]

    const Value* map_action = LIB(MAP);
    Details* details = Ensure_Cell_Frame_Details(map_action);

    Tweak_Level_Phase(LEVEL, details);
    Tweak_Level_Coupling(LEVEL, Cell_Frame_Coupling(map_action));

    Dispatcher* dispatcher = Details_Dispatcher(details);
    return Apply_Cfunc(dispatcher, LEVEL);
}


//
//  map: native [
//
//  "Evaluate a block for each value(s) in a series and collect as a block"
//
//      return: "Collected block"
//          [null? block!]
//      vars "Word or block of words to set each time, no new var if @word"
//          [_ word! @word! block!]
//      data "The series to traverse (only QUOTED? BLOCK! at the moment...)"
//          [<opt-out> blank? quoted! action!]
//      @(body) "Block to evaluate each time"
//          [<const> block! ^block!]
//      <local> iterator
//  ]
//
DECLARE_NATIVE(MAP)
{
    INCLUDE_PARAMS_OF_MAP;

    Element* vars = Element_ARG(VARS);  // becomes context on initial_entry
    Value* data = ARG(DATA);  // action invokes, frame enumerates
    Element* body = Element_ARG(BODY);  // bound to vars on initial_entry

    Element* iterator;  // holds Loop_Each_State, all paths must cleanup!

    enum {
        ST_MAP_INITIAL_ENTRY = STATE_0,
        ST_MAP_INITIALIZED_ITERATOR,
        ST_MAP_RUNNING_BODY
    };

    if (STATE != ST_MAP_INITIAL_ENTRY)
        goto not_initial_entry;

  initial_entry: {

    assert(Is_Cell_Erased(OUT));  // output only written in MAP if BREAK hit

    if (Is_Blank(data))  // same response as to empty series
        return Init_Block(OUT, Make_Source(0));

    if (Is_Block(body) or Is_Meta_Form_Of(BLOCK, body))
        Add_Definitional_Break_Continue(body, level_);

    if (Is_Action(data)) {
        // treat as a generator
    }
    else if (
        not Is_Quoted(data)
        or Quotes_Of(Known_Element(data)) != 1
        or not (
            Any_Series(Unquotify(Known_Element(data)))  // <= UNQUOTIFY here!
            or Is_Path(data)  // has been unquoted
            or Any_Context(data)
            or Any_Sequence(data)
        )
    ){
        panic (
            "MAP only supports one-level QUOTED? series/path/context ATM"
        );
    }

    VarList* varlist = require (Create_Loop_Context_May_Bind_Body(body, vars));

    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), varlist));

    iterator = Init_Loop_Each_May_Alias_Data(LOCAL(ITERATOR), data);
    STATE = ST_MAP_INITIALIZED_ITERATOR;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // need to finalize_map

    goto next_iteration;

} not_initial_entry: {  //////////////////////////////////////////////////////

    iterator = Element_LOCAL(ITERATOR);

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

    bool done = require (Loop_Each_Next_Maybe_Done(LEVEL));
    if (done)
        goto finalize_map;

    STATE = ST_MAP_RUNNING_BODY;
    return CONTINUE(SPARE, body);  // body may be ^BLOCK!

} body_result_in_spare: {  ///////////////////////////////////////////////////

    // Use APPEND semantics on the body result; whatever APPEND would do with
    // the value, we do the same.  (Ideally the code could be unified.)
    //
    // e.g. void is allowed for skipping map elements:
    //
    //        map-each 'x [1 2 3] [opt if even? x [x * 10]] => [20]

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(SPARE, LEVEL, &breaking))
            goto finalize_map;

        if (breaking) {
            Init_Nulled(OUT);
            goto finalize_map;
        }
    }

    if (Is_Void(SPARE))
        goto next_iteration;  // okay to skip

    if (Is_Error(SPARE) and Is_Error_Veto_Signal(Cell_Error(SPARE))) {
        Init_Nulled(OUT);
        goto finalize_map;
    }

    Value* spare = require (Decay_If_Unstable(SPARE));

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
        return NULLED;
    }

    return Init_Block(  // always returns block unless break [1]
        OUT,
        Pop_Source_From_Stack(STACK_BASE)
    );
}}


//
//  repeat: native [
//
//  "Evaluates a block a specified number of times"
//
//      return: "Last body result, or null if BREAK"
//          [any-value?]
//      count "Repetitions (true loops infinitely, false doesn't run)"
//          [<opt-out> any-number? logic?]
//      body "Block to evaluate or action to run"
//          [<unrun> <const> block! frame!]
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

    Value* count = ARG(COUNT);
    Element* body = Element_ARG(BODY);

    Value* index = u_cast(Value*, SPARE);  // current index, erased on entry

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

    if (Is_Logic(count)) {
        if (Cell_Logic(count) == false)
            return VOID;  // treat false as "don't run"

        Init_True(index);
    }
    else if (VAL_INT64(count) <= 0)
        return VOID;  // negative means "don't run" (vs. error)
    else {
        assert(Any_Number(count));
        Init_Integer(index, 1);
    }

    if (Is_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    STATE = ST_REPEAT_EVALUATING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // catch break/continue
    return CONTINUE_BRANCH(OUT, body, index);

} body_result_in_out: {  /////////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            return THROWN;

        if (breaking)
            return BREAKING_NULL;
    }

    if (Is_Logic(count)) {
        assert(Cell_Logic(count) == true);  // false already returned
        return CONTINUE_BRANCH(OUT, body);  // true infinite loops
    }

    if (VAL_INT64(count) == VAL_INT64(index))  // reached the desired count
        return LOOPED(OUT);

    mutable_VAL_INT64(index) += 1;

    assert(STATE == ST_REPEAT_EVALUATING_BODY);
    assert(Get_Executor_Flag(ACTION, LEVEL, DISPATCHER_CATCHES));
    return CONTINUE_BRANCH(OUT, body, index);  // keep looping
}}


//
//  for: native [
//
//  "Evaluates a branch a number of times or over a series, return last result"
//
//      return: "Last body result, or NULL if BREAK"
//          [any-value?]
//      vars "Word or block of words to set each time, no new var if @word"
//          [_ word! @word! block!]
//      value "Maximum number or series to traverse"
//          [<opt-out> any-number? any-sequence? quoted! block! action!]
//      body [<const> block!]
//  ]
//
DECLARE_NATIVE(FOR)
{
    INCLUDE_PARAMS_OF_FOR;

    Element* vars = Element_ARG(VARS);
    Element* value = Element_ARG(VALUE);
    Element* body = Element_ARG(BODY);

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
        Unquotify(value);

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
        return VOID;

    if (Is_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    VarList* varlist = require (Create_Loop_Context_May_Bind_Body(body, vars));

    assert(Varlist_Len(varlist) == 1);
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), varlist));

    Value* spare_one = Init_Integer(SPARE, 1);

    Fixed(Slot*) slot = Varlist_Fixed_Slot(varlist, 1);
    required (Write_Loop_Slot_May_Bind(slot, spare_one, body));

    STATE = ST_FOR_RUNNING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for break/continue
    return CONTINUE_BRANCH(OUT, body, Slot_Hack(slot));

} body_result_in_out: {  /////////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            return THROWN;

        if (breaking)
            return BREAKING_NULL;
    }

    Fixed(Slot*) slot = Varlist_Fixed_Slot(Cell_Varlist(vars), 1);

    Sink(Value) spare = SPARE;
    required (Read_Slot(spare, slot));

    if (not Is_Integer(spare))
        panic (Error_Invalid_Type_Raw(Datatype_Of(spare)));

    if (VAL_INT64(spare) == VAL_INT64(value))
        return LOOPED(OUT);

    if (Add_I64_Overflows(&mutable_VAL_INT64(spare), VAL_INT64(spare), 1))
        panic (Error_Overflow_Raw());

    required (Write_Loop_Slot_May_Bind(slot, spare, body));

    assert(STATE == ST_FOR_RUNNING_BODY);
    assert(Get_Executor_Flag(ACTION, LEVEL, DISPATCHER_CATCHES));
    return CONTINUE_BRANCH(OUT, body, spare);
}}


//
//  insist: native [
//
//  "Evaluates the body until it produces a conditionally true value"
//
//      return: "Last body result, or null if a BREAK occurred"
//          [any-value?]
//      body [<const> block!]
//  ]
//
DECLARE_NATIVE(INSIST)
{
    INCLUDE_PARAMS_OF_INSIST;

    Element* body = Element_ARG(BODY);

    enum {
        ST_INSIST_INITIAL_ENTRY = STATE_0,
        ST_INSIST_EVALUATING_BODY
    };

    switch (STATE) {
      case ST_INSIST_INITIAL_ENTRY:
        goto initial_entry;

      case ST_INSIST_EVALUATING_BODY:
        goto body_result_in_out;

      default:
        assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    STATE = ST_INSIST_EVALUATING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for BREAK, CONTINUE, etc.

} loop_again: { /////////////////////////////////////////////////////////////

    assert(Get_Executor_Flag(ACTION, LEVEL, DISPATCHER_CATCHES));
    assert(STATE == ST_INSIST_EVALUATING_BODY);
    return CONTINUE(OUT, body);

} body_result_in_out: {  /////////////////////////////////////////////////////

    // 1. When CONTINUE has an argument, it acts like the loop body evaluated
    //    to that argument.  But INSIST's condition and body are the same, so
    //    CONTINUE:WITH OKAY will stop the INSIST and return OKAY, while
    //    CONTINUE:WITH 10 will stop and return 10, etc.
    //
    // 2. Due to body_result_in_out:[1], we want CONTINUE (or CONTINUE VOID)
    //    to keep the loop running.  For parity between what continue does
    //    with an argument and what the loop does if the body evaluates to
    //    that argument, it suggests tolerating a void body result as intent
    //    to continue the loop also.
    //
    // 3. Being willing to tolerate a GHOST is a little more questionable.
    //    For now, don't allow it...though it may wind up being useful.
    //
    // 4. Today we don't test undecayed values for truthiness or falseyness.
    //    Hence INSIST cannot return something like a pack...it must be META'd
    //    and the result UNMETA'd.  That would mean all pack quasiforms would
    //    be considered truthy.

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            return THROWN;

        if (breaking)
            return BREAKING_NULL;

        // continue acts like body evaluated to its argument [1]
    }

    if (Is_Void(OUT))
        goto loop_again;  // skip voids [2]

    if (Is_Ghost(OUT))
        panic ("Body of INSIST must not return GHOST");  // tolerate? [3]

    Value* out = require (Decay_If_Unstable(OUT));  // decay for truth test [4]

    bool cond = require (Test_Conditional(out));
    if (cond)
        return LOOPED(OUT);

    goto loop_again;  // not truthy, keep going
}}


static Bounce While_Or_Until_Native_Core(Level* level_, bool is_while)
{
    INCLUDE_PARAMS_OF_WHILE;  // must have same parameters as UNTIL

    Element* condition = Element_ARG(CONDITION);
    Element* body = Element_ARG(BODY);

    enum {
        ST_WHILE_OR_UNTIL_INITIAL_ENTRY = STATE_0,
        ST_WHILE_OR_UNTIL_EVALUATING_CONDITION,
        ST_WHILE_OR_UNTIL_EVALUATING_BODY
    };

    switch (STATE) {
      case ST_WHILE_OR_UNTIL_INITIAL_ENTRY:
        goto initial_entry;

      case ST_WHILE_OR_UNTIL_EVALUATING_CONDITION:
        goto condition_eval_in_spare;

      case ST_WHILE_OR_UNTIL_EVALUATING_BODY:
        goto body_eval_in_out;

      default:
        assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. We *could* have CONTINUE in the *condition* as well as the body of a
    //    WHILE/UNTIL skip the execution of the body of that loop, and run the
    //    condition again.  :-/
    //
    //    That *may* be interesting for some stylized usage that puts complex
    //    branching code in a condition.  But it adds some cost, and would
    //    override the default meaning of CONTINUE continuing some enclosing
    //    loop...which is free, and enables other strange stylized usages.

    STATE = ST_WHILE_OR_UNTIL_EVALUATING_CONDITION;  // set before catching

    if (Is_Block(body))
        Add_Definitional_Break_Continue(body, LEVEL);  // no condition bind [1]
    else
        assert(Is_Frame(body));

} evaluate_condition: {  /////////////////////////////////////////////////////

    STATE = ST_WHILE_OR_UNTIL_EVALUATING_CONDITION;
    return CONTINUE(SPARE, condition);

} condition_eval_in_spare: {  ////////////////////////////////////////////////

    if (Is_Error(SPARE) and Is_Error_Done_Signal(Cell_Error(SPARE)))
        goto return_out;

    Value* spare = require (Decay_If_Unstable(SPARE));

    bool cond = require (Test_Conditional(spare));

    if (is_while) {
        if (not cond)
            goto return_out;  // falsey condition => last body result
    }
    else {  // is_until
        if (cond)
            goto return_out;  // truthy condition => last body result
    }

    STATE = ST_WHILE_OR_UNTIL_EVALUATING_BODY;  // body result => OUT
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for break/continue
    return CONTINUE_BRANCH(OUT, body, SPARE);

} body_eval_in_out: { ////////////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            return THROWN;

        if (breaking)
            return BREAKING_NULL;
    }

    Disable_Dispatcher_Catching_Of_Throws(LEVEL);
    goto evaluate_condition;

} return_out: {  /////////////////////////////////////////////////////////////

    if (Is_Cell_Erased(OUT))
        return VOID;  // body never ran, so no result to return!

    return LOOPED(OUT);  // VOID => TRASH, NULL => HEAVY NULL
}}


//
//  while: native [
//
//  "So long as a condition is truthy, evaluate the body"
//
//      return: "VOID if body never run, NULL if BREAK, else last body result"
//          [any-value?]
//      condition [<unrun> <const> block! frame!]  ; literals not allowed, [1]
//      body [<unrun> <const> block! frame!]
//  ]
//
DECLARE_NATIVE(WHILE)
//
// 1. It was considered if `while true [...]` should infinite loop, and then
//    `while false [...]` never ran.  However, that could lead to accidents
//    like `while x > 10 [...]` instead of `while [x > 10] [...]`.  It is
//    safer to require a BLOCK! vs. falling back on such behaviors.
//
//    (It's now easy for people to make their own weird polymorphic loops.)
{
    INCLUDE_PARAMS_OF_WHILE;

    USED(ARG(CONDITION));
    USED(ARG(BODY));

    bool is_while = true;
    return While_Or_Until_Native_Core(LEVEL, is_while);
}


//
//  until: native [
//
//  "So long as a condition is falsey, evaluate the body"
//
//      return: "VOID if body never run, NULL if BREAK, else last body result"
//          [any-value?]
//      condition [<unrun> <const> block! frame!]  ; literals not allowed, [1]
//      body [<unrun> <const> block! frame!]
//  ]
//
DECLARE_NATIVE(UNTIL)
//
// 1. See WHILE:1
{
    INCLUDE_PARAMS_OF_UNTIL;

    USED(ARG(CONDITION));
    USED(ARG(BODY));

    bool is_while = false;
    return While_Or_Until_Native_Core(LEVEL, is_while);
}
