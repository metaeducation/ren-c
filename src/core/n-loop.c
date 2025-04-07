//
//  File: %n-loop.c
//  Summary: "native functions for loops"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
#include "sys-int-funcs.h" //REB_I64_ADD_OF


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
// 1. On a "void" continue, it may seem tempting to drop out the last result:
//
//        for-each 'x [1 2 3] [if x = 3 [continue] x]  => 2  ; would be bad
//
//    But our goal is that a loop which never runs its body be distinguishable
//    from one that has CONTINUE'd each body.  Unless those are allowed to be
//    indistinguishable, loop compositions don't work.  So instead:
//
//        for-each 'x [1 2 3] [if x != 3 [x]]  =>  ~[~void~]~ antiform
//
bool Try_Catch_Break_Or_Continue(
    Sink(Value) out,
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
        Cell_Frame_Phase(label) == Cell_Frame_Phase(LIB(DEFINITIONAL_BREAK))
        and Cell_Frame_Coupling(label) == Level_Varlist(loop_level)
    ){
        CATCH_THROWN(out, loop_level);
        Init_Unreadable(out);  // caller must interpret breaking flag
        *breaking = true;
        return true;
    }

    if (
        Cell_Frame_Phase(label) == Cell_Frame_Phase(LIB(DEFINITIONAL_CONTINUE))
        and Cell_Frame_Coupling(label) == Level_Varlist(loop_level)
    ){
        CATCH_THROWN(out, loop_level);
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
//      return: []
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
        return FAIL(Error_Archetype_Invoked_Raw());

    Level* loop_level = Level_Of_Varlist_May_Fail(unwrap coupling);

    Element* label = Init_Frame(
        SPARE,
        Cell_Frame_Phase(LIB(DEFINITIONAL_BREAK)),
        CANON(BREAK),
        cast(VarList*, loop_level->varlist)
    );

    return Init_Thrown_With_Label(LEVEL, LIB(NULL), label);
}


//
//  definitional-continue: native [
//
//  "Throws control back to top of loop for next iteration"
//
//      return: []
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
// 1. Continue with no argument acts like CONTINUE:WITH VOID.  This makes
//    sense in cases like MAP-EACH (plain CONTINUE should not add a value).
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_CONTINUE;

    Value* with = ARG(WITH);
    if (not Bool_ARG(WITH))
        Init_Void(with);  // See: https://forum.rebol.info/t/1965/3 [1]

    Level* continue_level = LEVEL;  // Level of this CONTINUE call

    Option(VarList*) coupling = Level_Coupling(continue_level);
    if (not coupling)
        return FAIL(Error_Archetype_Invoked_Raw());

    Level* loop_level = Level_Of_Varlist_May_Fail(unwrap coupling);

    Element* label = Init_Frame(
        SPARE,
        Cell_Frame_Phase(LIB(DEFINITIONAL_CONTINUE)),
        CANON(CONTINUE),
        Varlist_Of_Level_Force_Managed(loop_level)
    );

    return Init_Thrown_With_Label(LEVEL, with, label);
}


//
//  Add_Definitional_Break_Continue: C
//
void Add_Definitional_Break_Continue(
    Value* body,
    Level* loop_level
){
    Context* parent = Cell_List_Binding(body);
    Let* let_continue = Make_Let_Variable(CANON(CONTINUE), parent);

    Init_Action(
        Stub_Cell(let_continue),
        Cell_Frame_Phase(LIB(DEFINITIONAL_CONTINUE)),
        CANON(CONTINUE),  // relabel (the CONTINUE in lib is a dummy action)
        Varlist_Of_Level_Force_Managed(loop_level)  // what to continue
    );

    Let* let_break = Make_Let_Variable(CANON(BREAK), let_continue);
    Init_Action(
        Stub_Cell(let_break),
        Cell_Frame_Phase(LIB(DEFINITIONAL_BREAK)),
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
    if (end >= Cell_Series_Len_Head(start))
        end = Cell_Series_Len_Head(start);
    if (end < 0)
        end = 0;

    // A value cell exposed to the user is used to hold the state.  This means
    // if they change `var` during the loop, it affects the iteration.  Hence
    // it must be checked for changing to another series, or non-series.
    //
    Copy_Cell(var, start);
    REBIDX* state = &VAL_INDEX_UNBOUNDED(var);

    // Run only once if start is equal to end...edge case.
    //
    REBINT s = VAL_INDEX(start);
    if (s == end) {
        if (Eval_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
                return THROWN;

            if (breaking)
                return nullptr;
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
                return nullptr;
        }

        if (
            Type_Of(var) != Type_Of(start)
            or Cell_Flex(var) != Cell_Flex(start)
        ){
            return FAIL("Can only change series index, not series to iterate");
        }

        // Note that since the array is not locked with FLEX_INFO_HOLD, it
        // can be mutated during the loop body, so the end has to be refreshed
        // on each iteration.  Review ramifications of HOLD-ing it.
        //
        if (end >= Cell_Series_Len_Head(start))
            end = Cell_Series_Len_Head(start);

        *state += bump;
    }

    if (Is_Cell_Erased(OUT))
        return VOID;

    return BRANCHED(OUT);
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
    // if they change `var` during the loop, it affects the iteration.  Hence
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
                return nullptr;
        }
        return BRANCHED(OUT);
    }

    // As per #1993, start relative to end determines the "direction" of the
    // FOR loop.  (R3-Alpha used the sign of the bump, which meant it did not
    // have a clear plan for what to do with 0.)
    //
    const bool counting_up = (start < end);  // equal checked above
    if ((counting_up and bump <= 0) or (not counting_up and bump >= 0))
        return nullptr;  // avoid infinite loops

    while (counting_up ? *state <= end : *state >= end) {
        if (Eval_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
                return THROWN;

            if (breaking)
                return nullptr;
        }

        if (not Is_Integer(var))
            return FAIL(Error_Invalid_Type_Raw(Datatype_Of(var)));

        if (REB_I64_ADD_OF(*state, bump, state))
            return FAIL(Error_Overflow_Raw());
    }

    return BRANCHED(OUT);
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
        return FAIL(start);

    REBDEC e;
    if (Is_Integer(end))
        e = cast(REBDEC, VAL_INT64(end));
    else if (Is_Decimal(end) or Is_Percent(end))
        e = VAL_DECIMAL(end);
    else
        return FAIL(end);

    REBDEC b;
    if (Is_Integer(bump))
        b = cast(REBDEC, VAL_INT64(bump));
    else if (Is_Decimal(bump) or Is_Percent(bump))
        b = VAL_DECIMAL(bump);
    else
        return FAIL(bump);

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
                return nullptr;
        }
        return BRANCHED(OUT);
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
                return nullptr;
        }

        if (not Is_Decimal(var))
            return FAIL(Error_Invalid_Type_Raw(Datatype_Of(var)));

        *state += b;
    }

    if (Is_Cell_Erased(OUT))
        return VOID;

    return BRANCHED(OUT);
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

    VarList* context = Virtual_Bind_Deep_To_New_Context(
        body,  // may be updated, will still be GC safe
        word
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(WORD), context));

    if (Is_Block(body) or Is_Meta_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    Value* var = Varlist_Slot(context, 1);  // not movable, see #2274

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
                VAL_INDEX(ARG(END)),
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
//          [word! the-word? blank!]
//      series "The series to iterate over"
//          [<maybe> blank! any-series?]
//      skip "Number of positions to skip each time"
//          [<maybe> integer!]
//      body "Code to evaluate each time"
//          [<const> any-branch?]
//  ]
//
DECLARE_NATIVE(FOR_SKIP)
{
    INCLUDE_PARAMS_OF_FOR_SKIP;

    Element* word = Element_ARG(WORD);
    Element* series = Element_ARG(SERIES);
    Element* body = Element_ARG(BODY);

    if (Is_Blank(series))
        return VOID;

    REBINT skip = Int32(ARG(SKIP));
    if (skip == 0) {
        //
        // !!! https://forum.rebol.info/t/infinite-loops-vs-errors/936
        //
        return VOID;
    }

    VarList* context = Virtual_Bind_Deep_To_New_Context(
        body,  // may be updated, will still be GC safe
        word
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(WORD), context));

    if (Is_Block(body) or Is_Meta_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    Value* pseudo_var = Varlist_Slot(context, 1); // not movable, see #2274
    Value* var = Real_Var_From_Pseudo(pseudo_var);
    Copy_Cell(var, series);

    // Starting location when past end with negative skip:
    //
    if (
        skip < 0
        and VAL_INDEX_UNBOUNDED(var) >= Cell_Series_Len_Head(var)
    ){
        VAL_INDEX_UNBOUNDED(var) = Cell_Series_Len_Head(var) + skip;
    }

    while (true) {
        REBINT len = Cell_Series_Len_Head(var);  // always >= 0
        REBINT index = VAL_INDEX_RAW(var);  // may have been set to < 0 below

        if (index < 0)
            break;
        if (index >= len) {
            if (skip >= 0)
                break;
            index = len + skip;  // negative
            if (index < 0)
                break;
            VAL_INDEX_UNBOUNDED(var) = index;
        }

        if (Eval_Branch_Throws(OUT, ARG(BODY))) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
                return THROWN;

            if (breaking)
                return nullptr;
        }

        // Modifications to var are allowed, to another ANY-SERIES? value.
        //
        // If `var` is movable (e.g. specified via THE-WORD!) it must be
        // refreshed each time arbitrary code runs, since the context may
        // expand and move the address, may get PROTECTed, etc.
        //
        var = Real_Var_From_Pseudo(pseudo_var);

        if (Is_Nulled(var))
            return FAIL(PARAM(WORD));
        if (not Any_Series(var))
            return FAIL(var);

        // Increment via skip, which may go before 0 or after the tail of
        // the series.
        //
        // !!! Should also check for overflows of REBIDX range.
        //
        VAL_INDEX_UNBOUNDED(var) += skip;
    }

    if (Is_Cell_Erased(OUT))
        return VOID;

    return BRANCHED(OUT);
}


//
//  definitional-stop: native [
//
//  "End the current iteration of CYCLE, optionally returning a value"
//
//      return: []
//      :with "Act as if loop body finished with this value"
//          [any-value?]
//  ]
//
DECLARE_NATIVE(DEFINITIONAL_STOP)  // See CYCLE for notes about STOP
{
    INCLUDE_PARAMS_OF_DEFINITIONAL_STOP;

    Value* with = ARG(WITH);
    if (not Bool_ARG(WITH))
        Init_Void(with);  // See: https://forum.rebol.info/t/1965/3

    Level* stop_level = LEVEL;  // Level of this STOP call

    Option(VarList*) coupling = Level_Coupling(stop_level);
    if (not coupling)
        return FAIL(Error_Archetype_Invoked_Raw());

    Level* loop_level = Level_Of_Varlist_May_Fail(unwrap coupling);

    Element* label = Init_Frame(
        SPARE,
        Cell_Frame_Phase(LIB(DEFINITIONAL_STOP)),
        CANON(STOP),
        cast(VarList*, loop_level->varlist)
    );

    return Init_Thrown_With_Label(LEVEL, with, label);
}


//
//  Add_Definitional_Stop: C
//
void Add_Definitional_Stop(
    Value* body,
    Level* loop_level
){
    Context* parent = Cell_List_Binding(body);

    Force_Level_Varlist_Managed(loop_level);

    Let* let_stop = Make_Let_Variable(CANON(STOP), parent);
    Init_Action(
        Stub_Cell(let_stop),
        Cell_Frame_Phase(LIB(DEFINITIONAL_STOP)),
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

    Value* body = ARG(BODY);

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

    if (Is_Block(body) or Is_Meta_Block(body)) {
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
            return nullptr;

        return CONTINUE(OUT, body);  // plain continue
    }

    const Value* label = VAL_THROWN_LABEL(LEVEL);
    if (
        Is_Frame(label)
        and Cell_Frame_Phase(label) == Cell_Frame_Phase(LIB(DEFINITIONAL_STOP))
        and Cell_Frame_Coupling(label) == Level_Varlist(LEVEL)
    ){
        CATCH_THROWN(OUT, LEVEL);  // Unlike BREAK, STOP takes an arg--[1]

        if (Is_Void(OUT))  // STOP with no arg, void usually reserved [2]
            return Init_Heavy_Void(OUT);

        if (Is_Nulled(OUT))
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
void Init_Loop_Each_May_Alias_Data(Value* iterator, Value* data)
{
    assert(not Is_Api_Value(data));  // used to be cue to free, but not now

    LoopEachState *les = Try_Alloc_Memory(LoopEachState);

    if (Any_Sequence(data)) {  // alias paths, chains, tuples as BLOCK!
        DECLARE_ELEMENT (temp);
        Option(Error*) e = Trap_Alias_Any_Sequence_As(
            temp, cast(Element*, data), TYPE_BLOCK
        );
        assert(not e);
        UNUSED(e);
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
            les->u.eser.index = VAL_INDEX(data);
            les->u.eser.len = Cell_Series_Len_Head(data);  // has HOLD, won't change
        }
        else if (Is_Module(data)) {
            les->flex = EMPTY_ARRAY;  // !!! workaround, not a Flex
            Init_Evars(&les->u.evars, data);
        }
        else if (Any_Context(data)) {
            les->flex = Varlist_Array(Cell_Varlist(data));
            Init_Evars(&les->u.evars, data);
        }
        else if (Is_Map(data)) {
            les->flex = MAP_PAIRLIST(VAL_MAP(data));
            les->u.eser.index = 0;
            les->u.eser.len = Flex_Used(les->flex);  // immutable--has HOLD
        }
        else
            panic ("Illegal type passed to Loop_Each()");

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

    Init_Handle_Cdata(iterator, les, sizeof(les));
}


//
//  Try_Loop_Each_Next: C
//
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
// It's possible to opt out of variable slots using BLANK!.
//
static bool Try_Loop_Each_Next(const Value* iterator, VarList* vars_ctx)
{
    LoopEachState *les = Cell_Handle_Pointer(LoopEachState, iterator);

    if (not les->more_data)
        return false;

    const Value* pseudo_tail;
    Value* pseudo_var = Varlist_Slots(&pseudo_tail, vars_ctx);
    for (; pseudo_var != pseudo_tail; ++pseudo_var) {
        Value* var = Real_Var_From_Pseudo(pseudo_var);

        if (not les->more_data) {
            Init_Nulled(var);  // Y is null in `for-each [x y] [1] ...`
            continue;  // the `for` variable acquisition loop
        }

        if (Is_Action(les->data)) {
            Value* generated = rebValue(rebRUN(les->data));
            if (generated) {
                if (var)
                    Copy_Cell(var, generated);
                rebRelease(generated);
            }
            else {
                les->more_data = false;  // any remaining vars must be unset
                if (pseudo_var == Varlist_Slots_Head(vars_ctx)) {
                    //
                    // If we don't have at least *some* of the variables
                    // set for this body loop run, don't run the body.
                    //
                    return false;
                }
                if (var)
                    Init_Nulled(var);
            }
            continue;
        }

        Heart heart = Heart_Of_Builtin_Fundamental(les->data);

        if (Any_List_Type(heart)) {
            if (var)
                Copy_Cell(
                    var,
                    Array_At(
                        c_cast(Array*, les->flex),
                        les->u.eser.index
                    )
                );
            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;
            continue;
        }

        if (Any_Context_Type(heart)) {
            if (var) {
                assert(les->u.evars.index != 0);
                Init_Any_Word(
                    var,
                    TYPE_WORD,
                    Key_Symbol(les->u.evars.key)
                );

                if (heart == TYPE_MODULE) {
                    Tweak_Cell_Word_Index(var, INDEX_PATCHED);
                    Tweak_Cell_Binding(var, Sea_Patch(
                        Cell_Module_Sea(les->data),
                        Key_Symbol(les->u.evars.key),
                        true
                    ));
                }
                else {
                    Tweak_Cell_Word_Index(var, les->u.evars.index);
                    Tweak_Cell_Binding(var, Cell_Varlist(les->data));
                }
            }

            if (Varlist_Len(vars_ctx) == 1) {
                //
                // Only wanted the key (`for-each 'key obj [...]`)
            }
            else if (Varlist_Len(vars_ctx) == 2) {
                //
                // Want keys and values (`for-each 'key val obj [...]`)
                //
                ++pseudo_var;
                var = Real_Var_From_Pseudo(pseudo_var);
                Copy_Cell(var, les->u.evars.var);
            }
            else
                fail ("Loop enumeration of contexts must be 1 or 2 vars");

            les->more_data = Try_Advance_Evars(&les->u.evars);
            continue;
        }

        if (heart == TYPE_MAP) {
            assert(les->u.eser.index % 2 == 0);  // should be on key slot

            const Value* key;
            const Value* val;
            while (true) {  // pass over the unused map slots
                key = (
                    Array_At(c_cast(Array*, les->flex), les->u.eser.index)
                );
                ++les->u.eser.index;
                val = (
                    Array_At(c_cast(Array*, les->flex), les->u.eser.index)
                );
                ++les->u.eser.index;
                if (les->u.eser.index == les->u.eser.len)
                    les->more_data = false;
                if (not Is_Nulled(val))
                    break;
                if (not les->more_data)
                    return false;
            } while (Is_Nulled(val));

            if (var)
                Copy_Cell(var, key);

            if (Varlist_Len(vars_ctx) == 1) {
                //
                // Only wanted the key (`for-each 'key map [...]`)
            }
            else if (Varlist_Len(vars_ctx) == 2) {
                //
                // Want keys and values (`for-each 'key val map [...]`)
                //
                ++pseudo_var;
                var = Real_Var_From_Pseudo(pseudo_var);
                Copy_Cell(var, val);
            }
            else
                fail ("Loop enumeration of contexts must be 1 or 2 vars");

            continue;
        }

        if (Any_String_Type(heart)) {
            if (var)
                Init_Char_Unchecked(
                    var,
                    Get_Char_At(c_cast(String*, les->flex), les->u.eser.index)
                );
            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;
            continue;
        }

        if (heart == TYPE_BLOB) {
            const Binary* b = c_cast(Binary*, les->flex);
            if (var)
                Init_Integer(var, Binary_Head(b)[les->u.eser.index]);
            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;
            continue;
        }

        panic (les->data);
    }

    return true;
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
//          [blank! word! the-word? block!]
//      data "The series to traverse"
//          [<maybe> blank! any-series? any-context? map! any-sequence?
//           action!]  ; action support experimental, e.g. generators
//      body "Block to evaluate each time"
//          [<const> block! meta-block!]
//      <local> iterator
//  ]
//
DECLARE_NATIVE(FOR_EACH)
{
    INCLUDE_PARAMS_OF_FOR_EACH;

    Element* vars = Element_ARG(VARS);  // becomes context on initial_entry
    Value* data = ARG(DATA);
    Element* body = Element_ARG(BODY);  // bound to vars on initial_entry

    Value* iterator = LOCAL(ITERATOR);  // reuse to hold Loop_Each_State

    bool breaking = false;

    enum {
        ST_FOR_EACH_INITIAL_ENTRY = STATE_0,
        ST_FOR_EACH_INITIALIZED_ITERATOR,
        ST_FOR_EACH_RUNNING_BODY
    };

    switch (STATE) {
      case ST_FOR_EACH_INITIAL_ENTRY:
        goto initial_entry;

      case ST_FOR_EACH_INITIALIZED_ITERATOR:
        assert(Is_Throwing_Failure(LEVEL));  // this dispatcher fail()'d
        goto finalize_for_each;

      case ST_FOR_EACH_RUNNING_BODY:
        goto body_result_in_spare_or_threw;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. If there is an abrupt failure, e.g. a `fail()` that could happen
    //    even in the code of this dispatcher, we need to clean up the
    //    iterator state.

    if (Is_Blank(data))  // same response as to empty series
        return VOID;

    VarList* pseudo_vars_ctx = Virtual_Bind_Deep_To_New_Context(
        body,  // may be updated, will still be GC safe
        vars
    );
    Remember_Cell_Is_Lifeguard(Init_Object(vars, pseudo_vars_ctx));

    if (Is_Block(body) or Is_Meta_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    Init_Loop_Each_May_Alias_Data(iterator, data);  // all paths must cleanup
    STATE = ST_FOR_EACH_INITIALIZED_ITERATOR;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // need to finalize_for_each

    goto next_iteration;

} next_iteration: {  /////////////////////////////////////////////////////////

    if (not Try_Loop_Each_Next(iterator, Cell_Varlist(vars)))
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
        return nullptr;

    if (Is_Cell_Erased(OUT))
        return VOID;

    return BRANCHED(OUT);
}}


//
//  every: native [
//
//  "Iterate and return null if any previous body evaluations were falsey"
//
//      return: "null on BREAK, void on empty, null or the last non-null value"
//          [any-value?]
//      vars "Word or block of words to set each time, no new var if @word"
//          [blank! word! the-word! block!]
//      data "The series to traverse"
//          [<maybe> blank! any-series? any-context? map! action!]
//      body [<const> block! meta-block!]
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

    Value* iterator = LOCAL(ITERATOR);  // place to store iteration state

    enum {
        ST_EVERY_INITIAL_ENTRY = STATE_0,
        ST_EVERY_INITIALIZED_ITERATOR,
        ST_EVERY_RUNNING_BODY
    };

    switch (STATE) {
      case ST_EVERY_INITIAL_ENTRY:
        goto initial_entry;

      case ST_EVERY_INITIALIZED_ITERATOR:
        assert(Is_Throwing_Failure(LEVEL));  // this dispatcher fail()'d
        goto finalize_every;

      case ST_EVERY_RUNNING_BODY:
        goto body_result_in_spare;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Blank(data))  // same response as to empty series
        return VOID;

    VarList* pseudo_vars_ctx = Virtual_Bind_Deep_To_New_Context(
        body,  // may be updated, will still be GC safe
        vars
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), pseudo_vars_ctx));

    if (Is_Block(body) or Is_Meta_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    Init_Loop_Each_May_Alias_Data(iterator, data);  // all paths must cleanup
    STATE = ST_EVERY_INITIALIZED_ITERATOR;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // need to finalize_every

    goto next_iteration;

} next_iteration: {  /////////////////////////////////////////////////////////

    if (not Try_Loop_Each_Next(iterator, Cell_Varlist(vars)))
        goto finalize_every;

    STATE = ST_EVERY_RUNNING_BODY;
    return CONTINUE(SPARE, body);

} body_result_in_spare: {  ///////////////////////////////////////////////////

    // 1. In light of other tolerances in the system for voids in logic tests
    //    (see ALL & ANY), EVERY treats a void as "no vote".
    //
    //        every 'x [1 2 3 4] [if even? x [x]]  =>  4
    //
    //        every 'x [1 2 3 4] [if odd? x [x]]  => ~[~void~]~ antiform
    //
    //    It returns heavy void on skipped bodies, as loop composition breaks
    //    down if we try to keep old values.

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(SPARE, LEVEL, &breaking))
            goto finalize_every;

        if (breaking) {
            Init_Nulled(OUT);
            goto finalize_every;
        }
    }

    if (
        Is_Void(SPARE)
        or Is_Heavy_Void(SPARE)
        or (Is_Meta_Block(body) and Is_Meta_Of_Void(SPARE))
    ){
        Init_Heavy_Void(OUT);  // forget OUT for loop composition [1]
        goto next_iteration;  // ...but void does not NULL-lock output
    }

    if (Is_Inhibitor(stable_SPARE)) {
        Init_Nulled(OUT);
    }
    else if (Is_Cell_Erased(OUT) or not Is_Nulled(OUT)) {
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
//          [~null~ ~[[blank! any-series?] integer!]~]
//      vars "Word or block of words to set each time, no new var if @word"
//          [blank! word! the-word! block!]
//      data "The series to traverse (modified)"
//          [<maybe> blank! any-series?]
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
    Element* data = Element_ARG(DATA);
    Element* body = Element_ARG(BODY);

    Count removals = 0;

    if (Is_Blank(data)) {
        Init_Blank(OUT);
        goto return_pack;
    }

  process_non_blank: { ////////////////////////////////////////////////////=//

    // 1. Updating arrays in place may not be better than pushingvalues to
    //    the data stack and creating a precisely-sized output Flex to swap as
    //    underlying memory for the array.  (Imagine a large array with many
    //    removals, and the ensuing wasted space being left behind).  We use
    //    the method anyway, to test novel techniques and error handling.
    //
    // 2. For blobs and strings, we push new data as the loop runs.  Then at
    //    the end of the enumeration, the identity of the incoming series is
    //    kept but new underlying data is poked into it, old data is freed.
    //
    // 3. When a BREAK happens there is no change applied to the series.  It's
    //    conceivable that might not be what people want--and that if they did
    //    want that, they would likely use a MAP-EACH or something to generate
    //    a new series.  But NULL is reserved for when loops break, so there
    //    would not be a way to get the removal count in this case.  Hence it
    //    is semantically easiest to say BREAK goes along with "no effect".
    //
    // 4. The only signals allowed are ~okay~, ~null~ and ~void~.  This seems
    //    more coherent, and likely would catch more errors than allowing any
    //    Is_Trigger() value to mean "remove".

    Flex* flex = Cell_Flex_Ensure_Mutable(data);  // check even if empty

    if (VAL_INDEX(data) >= Cell_Series_Len_At(data))  // past series end
        return nullptr;

    VarList* context = Virtual_Bind_Deep_To_New_Context(
        body,  // may be updated, will still be GC safe
        vars
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), context));

    if (Is_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    REBLEN start = VAL_INDEX(data);

    DECLARE_MOLDER (mo);
    if (Any_List(data)) {  // use NODE_FLAG_MARKED to mark for removal [1]
        Corrupt_Pointer_If_Debug(mo);
    }
    else {  // generate new data allocation and swap content in the Flex [2]
        Push_Mold(mo);
    }

    Set_Flex_Info(flex, HOLD);  // disallow mutations until finalize

    REBLEN len = Any_String(data)
        ? String_Len(cast(String*, flex))
        : Flex_Used(flex);  // temp read-only, this won't change

    bool threw = false;
    bool breaking = false;

    REBLEN index = start;
    while (index < len) {
        assert(start == index);

        const Value* var_tail;
        Value* var = Varlist_Slots(&var_tail, context);  // fixed (#2274)
        for (; var != var_tail; ++var) {
            if (index == len) {
                Init_Nulled(var);  // Y on 2nd step of remove-each [x y] "abc"
                continue;  // the `for` loop setting variables
            }

            if (Any_List(data))
                Derelativize(
                    var,
                    Array_At(Cell_Array(data), index),
                    Cell_List_Binding(data)
                );
            else if (Is_Blob(data)) {
                Binary* b = cast(Binary*, flex);
                Init_Integer(var, cast(REBI64, Binary_Head(b)[index]));
            }
            else {
                assert(Any_String(data));
                Init_Char_Unchecked(
                    var,
                    Get_Char_At(cast(String*, flex), index)
                );
            }
            ++index;
        }

        if (Eval_Any_List_At_Throws(OUT, body, SPECIFIED)) {
            if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking)) {
                threw = true;
                goto finalize_remove_each;
            }

            if (breaking) {  // break semantics are no-op [3]
                assert(start < len);
                goto finalize_remove_each;
            }
        }

        bool keep;

        Decay_If_Unstable(OUT);

        if (Is_Void(OUT)) {
            keep = true;  // treat same as logic false (e.g. don't remove)
        }
        else if (Is_Okay(OUT)) {  // pure logic required [4]
            keep = false;  // okay is remove
        }
        else if (Is_Nulled(OUT)) {  // don't remove
            keep = true;
            Init_Heavy_Null(OUT);  // NULL reserved for BREAK signal
        }
        else {
            threw = true;
            Init_Error(
                SPARE,
                Error_User("Use [~null~ ~okay~ ~void~] with REMOVE-EACH")
            );
            Init_Thrown_With_Label(LEVEL, LIB(NULL), stable_SPARE);
            goto finalize_remove_each;
        }

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
                    Append_Ascii_Len(
                        mo->string,
                        cs_cast(Binary_At(b, start)),
                        1
                    );
                }
                else {
                    Append_Codepoint(
                        mo->string,
                        Get_Char_At(cast(String*, flex), start)
                    );
                }
                ++start;
            } while (start != index);
        }

        if (Is_Nulled(OUT))
            Init_Heavy_Null(OUT);  // reserve pure NULL for BREAK
    }

    assert(start == len);  // normal completion (otherwise a `goto` happened)

  finalize_remove_each: {  ///////////////////////////////////////////////////

    // 7. We are reusing the mold buffer for BLOB!, but *not putting UTF-8
    //    data* into it.  Revisit if this inhibits cool UTF-8 based tricks
    //    the mold buffer might do otherwise.

    assert(Get_Flex_Info(flex, HOLD));
    Clear_Flex_Info(flex, HOLD);

    if (Any_List(data)) {
        if (not threw and breaking) {  // clean marks, don't remove
            const Element* tail;
            Element* temp = Cell_List_At_Known_Mutable(&tail, data);
            for (; temp != tail; ++temp) {
                if (Get_Cell_Flag(temp, NOTE_REMOVE))
                    Clear_Cell_Flag(temp, NOTE_REMOVE);
            }
            goto done_finalizing;
        }

        Copy_Cell(OUT, data);  // going to be the same series

        const Element* tail;
        Element* dest = Cell_List_At_Known_Mutable(&tail, data);
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

        // If there was a THROW, or fail() we need the remaining data
        //
        REBLEN orig_len = Cell_Series_Len_Head(data);
        assert(start <= orig_len);
        Append_Ascii_Len(
            mo->string,
            cs_cast(Binary_At(b, start)),
            orig_len - start
        );

        Binary* popped = Pop_Molded_Binary(mo);  // not UTF-8 if binary [7]

        assert(Binary_Len(popped) <= Cell_Series_Len_Head(data));
        removals = Cell_Series_Len_Head(data) - Binary_Len(popped);

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

        // If there was a THROW, or fail() we need the remaining data
        //
        REBLEN orig_len = Cell_Series_Len_Head(data);
        assert(start <= orig_len);

        String* s = cast(String*, flex);

        for (; start != orig_len; ++start)
            Append_Codepoint(mo->string, Get_Char_At(s, start));

        String* popped = Pop_Molded_String(mo);

        assert(String_Len(popped) <= Cell_Series_Len_Head(data));
        removals = Cell_Series_Len_Head(data) - String_Len(popped);

        Swap_Flex_Content(popped, s);  // swap Flex identity [3]

        Free_Unmanaged_Flex(popped);  // frees incoming Flex's data
        Init_Any_String(OUT, Heart_Of_Builtin_Fundamental(data), s);
    }

  done_finalizing:

    if (threw)
        return THROWN;

    if (breaking)
        return nullptr;

    assert(Type_Of(OUT) == Type_Of(data));

}} return_pack: { //////////////////////////////////////////////////////////=//

    Source* pack = Make_Source(2);
    Set_Flex_Len(pack, 2);

    Copy_Meta_Cell(Array_At(pack, 0), OUT);
    Meta_Quotify(Init_Integer(Array_At(pack, 1), removals));

    return Init_Pack(OUT, pack);
}}


//
//  map-each: native [
//
//  "Evaluate a block for each value(s) in a series and collect as a block"
//
//      return: "Collected block"
//          [~null~ block!]
//      vars "Word or block of words to set each time, no new var if @word"
//          [blank! word! the-word! block!]
//      data "The series to traverse"
//          [<maybe> blank! any-series? any-sequence? any-context?
//           action!]
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
//          [~null~ block!]
//      vars "Word or block of words to set each time, no new var if @word"
//          [blank! word! the-word! block!]
//      data "The series to traverse (only QUOTED? BLOCK! at the moment...)"
//          [<maybe> blank! quoted! action!]
//      @(body) "Block to evaluate each time"
//          [<const> block! meta-block!]
//      <local> iterator
//  ]
//
DECLARE_NATIVE(MAP)
{
    INCLUDE_PARAMS_OF_MAP;

    bool invoke_frame = false;
    if (Is_Action(ARG(DATA))) {
        invoke_frame = true;
        QUOTE_BYTE(ARG(DATA)) = NOQUOTE_1;
    }

    Element* vars = Element_ARG(VARS);  // becomes context on initial_entry
    Element* data = Element_ARG(DATA);  // action invokes, frame enumerates
    Element* body = Element_ARG(BODY);  // bound to vars on initial_entry

    Value* iterator = LOCAL(ITERATOR);  // reuse to hold Loop_Each_State

    enum {
        ST_MAP_INITIAL_ENTRY = STATE_0,
        ST_MAP_INITIALIZED_ITERATOR,
        ST_MAP_RUNNING_BODY
    };

    switch (STATE) {
      case ST_MAP_INITIAL_ENTRY:
        goto initial_entry;

      case ST_MAP_INITIALIZED_ITERATOR:
        assert(Is_Throwing_Failure(LEVEL));  // this dispatcher fail()'d
        goto finalize_map;

      case ST_MAP_RUNNING_BODY:
        goto body_result_in_spare;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    assert(Is_Cell_Erased(OUT));  // output only written in MAP if BREAK hit

    if (Is_Blank(data))  // same response as to empty series
        return Init_Block(OUT, Make_Source(0));

    if (Is_Block(body) or Is_Meta_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    if (invoke_frame) {
        // treat as a generator
    }
    else if (
        not Is_Quoted(data)
        or Quotes_Of(data) != 1
        or not (
            Any_Series(Unquotify(data))
            or Any_Path(data)  // has been unquoted
            or Any_Context(data)
            or Any_Sequence(data)
        )
    ){
        return FAIL(
            "MAP only supports one-level QUOTED? series/path/context ATM"
        );
    }

    VarList* pseudo_vars_ctx = Virtual_Bind_Deep_To_New_Context(
        body,  // may be updated, will still be GC safe
        vars
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), pseudo_vars_ctx));

    Init_Loop_Each_May_Alias_Data(iterator, data);  // all paths must cleanup
    STATE = ST_MAP_INITIALIZED_ITERATOR;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // need to finalize_map

    goto next_iteration;

} next_iteration: {  /////////////////////////////////////////////////////////

    if (not Try_Loop_Each_Next(iterator, Cell_Varlist(vars)))
        goto finalize_map;

    STATE = ST_MAP_RUNNING_BODY;
    return CONTINUE(SPARE, body);  // body may be META-BLOCK!

} body_result_in_spare: {  ///////////////////////////////////////////////////

    // Use APPEND semantics on the body result; whatever APPEND would do with
    // the value, we do the same.  (Ideally the code could be unified.)
    //
    // e.g. void is allowed for skipping map elements:
    //
    //        map-each 'x [1 2 3] [if even? x [x * 10]] => [20]

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(SPARE, LEVEL, &breaking))
            goto finalize_map;

        if (breaking) {
            Init_Nulled(OUT);
            goto finalize_map;
        }
    }

    Decay_If_Unstable(SPARE);

    if (Is_Void(SPARE))
        goto next_iteration;  // okay to skip

    if (Is_Splice(SPARE)) {
        Quasify_Antiform(SPARE);
        const Element* tail;
        const Element* v = Cell_List_At(&tail, SPARE);
        for (; v != tail; ++v)
            Derelativize(PUSH(), v, Cell_List_Binding(SPARE));
    }
    else if (Is_Antiform(SPARE)) {
        Init_Thrown_Failure(LEVEL, Error_Bad_Antiform(SPARE));
        goto finalize_map;
    }
    else if (Is_Nulled(SPARE)) {
        return FAIL(Error_Need_Non_Null_Raw());
    }
    else
        Copy_Cell(PUSH(), stable_SPARE);  // non nulls added to result

    goto next_iteration;

} finalize_map: {  ///////////////////////////////////////////////////////////

    // 1. MAP and MAP-EACH always return blocks except in cases of BREAK, e.g.
    //    there's no way to detect from the outside if the body never ran.
    //    Are variants useful? (e.g. COLLECT* is NULL if nothing collected)

    Shutdown_Loop_Each(iterator);

    if (THROWING)
        return THROWN;  // automatically drops to baseline

    if (Not_Cell_Erased(OUT)) {  // only modifies on break
        assert(Is_Nulled(OUT));  // BREAK, so *must* return null
        Drop_Data_Stack_To(STACK_BASE);
        return nullptr;
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
//          [<maybe> any-number? logic?]
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
    Value* body = ARG(BODY);

    Value* index = cast(Value*, SPARE);  // spare cell holds current index

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
            return nullptr;
    }

    if (Is_Logic(count)) {
        assert(Cell_Logic(count) == true);  // false already returned
        return CONTINUE_BRANCH(OUT, body);  // true infinite loops
    }

    if (VAL_INT64(count) == VAL_INT64(index))  // reached the desired count
        return BRANCHED(OUT);

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
//          [blank! word! the-word! block!]
//      value "Maximum number or series to traverse"
//          [<maybe> any-number? any-sequence? quoted! block! action!]
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
            return FAIL(PARAM(VALUE));

        // Delegate to FOR-EACH (note: in the future this will be the other
        // way around, with FOR-EACH delegating to FOR).
        //
        rebPushContinuation_internal(
            cast(Value*, OUT),  // <-- output cell
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

    VarList* context = Virtual_Bind_Deep_To_New_Context(body, vars);
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), context));

    assert(Varlist_Len(context) == 1);

    Value* var = Varlist_Slot(Cell_Varlist(vars), 1);  // not movable, see #2274
    Init_Integer(var, 1);

    STATE = ST_FOR_RUNNING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for break/continue
    return CONTINUE_BRANCH(OUT, body, var);

} body_result_in_out: {  /////////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            return THROWN;

        if (breaking)
            return nullptr;
    }

    Value* var = Varlist_Slot(Cell_Varlist(vars), 1);  // not movable, see #2274

    if (not Is_Integer(var))
        return FAIL(Error_Invalid_Type_Raw(Datatype_Of(var)));

    if (VAL_INT64(var) == VAL_INT64(value))
        return BRANCHED(OUT);

    if (REB_I64_ADD_OF(VAL_INT64(var), 1, &mutable_VAL_INT64(var)))
        return FAIL(Error_Overflow_Raw());

    assert(STATE == ST_FOR_RUNNING_BODY);
    assert(Get_Executor_Flag(ACTION, LEVEL, DISPATCHER_CATCHES));
    return CONTINUE_BRANCH(OUT, body, var);
}}


//
//  until: native [
//
//  "Evaluates the body until it produces a conditionally true value"
//
//      return: "Last body result, or null if a BREAK occurred"
//          [any-value?]
//      body [<const> block!]
//      :predicate "Function to apply to body result"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(UNTIL)
{
    INCLUDE_PARAMS_OF_UNTIL;

    Value* body = ARG(BODY);
    Value* predicate = ARG(PREDICATE);

    Atom* condition;  // can point to OUT or SPARE

    enum {
        ST_UNTIL_INITIAL_ENTRY = STATE_0,
        ST_UNTIL_EVALUATING_BODY,
        ST_UNTIL_RUNNING_PREDICATE
    };

    switch (STATE) {
      case ST_UNTIL_INITIAL_ENTRY : goto initial_entry;
      case ST_UNTIL_EVALUATING_BODY : goto body_result_in_out;
      case ST_UNTIL_RUNNING_PREDICATE : goto predicate_result_in_spare;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    STATE = ST_UNTIL_EVALUATING_BODY;
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);
    return CONTINUE(OUT, body);

} body_result_in_out: {  /////////////////////////////////////////////////////

    // 1. When CONTINUE has an argument, it acts like the loop body evaluated
    //    to that argument.  But UNTIL's condition and body are the same, so
    //    CONTINUE:WITH OKAY will stop the UNTIL and return OKAY, while
    //    CONTINUE:WITH 10 will stop and return 10, etc.

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            return THROWN;

        if (breaking)
            return nullptr;

        // continue acts like body evaluated to its argument [1]
    }

    if (Is_Nulled(predicate)) {
        condition = OUT;  // default is just test truthiness of body product
        goto test_condition;
    }

    STATE = ST_UNTIL_RUNNING_PREDICATE;
    Disable_Dispatcher_Catching_Of_Throws(LEVEL);
    return CONTINUE(SPARE, predicate, OUT);

} predicate_result_in_spare: {  //////////////////////////////////////////////

    Enable_Dispatcher_Catching_Of_Throws(LEVEL);
    condition = SPARE;
    goto test_condition;

} test_condition: {  /////////////////////////////////////////////////////////

    // 1. Today we don't test undecayed values for truthiness or falseyness.
    //    Hence UNTIL cannot return something like a pack...it must be META'd
    //    and the result UNMETA'd.  That would mean all pack quasiforms would
    //    be considered truthy.
    //
    // 2. Due to body_result_in_out:[1], we want CONTINUE (or CONTINUE VOID)
    //    to keep the loop running.  For parity between what continue does
    //    with an argument and what the loop does if the body evaluates to
    //    that argument, it suggests tolerating a void body result as intent
    //    to continue the loop also.

    Decay_If_Unstable(condition);  // must decay for truth test [1]

    if (not Is_Void(condition)) {  // skip voids [2]
        if (Is_Trigger(Stable_Unchecked(condition)))
            return BRANCHED(OUT);  // truthy result, return value!
    }

    STATE = ST_UNTIL_EVALUATING_BODY;
    assert(Get_Executor_Flag(ACTION, LEVEL, DISPATCHER_CATCHES));
    return CONTINUE(OUT, body);  // not truthy, keep going
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

    Value* condition = ARG(CONDITION);
    Value* body = ARG(BODY);

    enum {
        ST_WHILE_INITIAL_ENTRY = STATE_0,
        ST_WHILE_EVALUATING_CONDITION,
        ST_WHILE_EVALUATING_BODY
    };

    switch (STATE) {
      case ST_WHILE_INITIAL_ENTRY : goto initial_entry;
      case ST_WHILE_EVALUATING_CONDITION : goto condition_eval_in_spare;
      case ST_WHILE_EVALUATING_BODY : goto body_eval_in_out;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. We could make it so CONTINUE in the condition of a WHILE skipped
    //    the execution of the body of that loop, and run the condition again.
    //    That *may* be interesting for some stylized usage that puts complex
    //    branching code in a condition.  But it adds some cost, and would
    //    override the default meaning of CONTINUE continuing some enclosing
    //    loop...which is free, and enables other strange stylized usages.

    STATE = ST_WHILE_EVALUATING_CONDITION;  // have to set before catching

    if (Is_Block(body))
        Add_Definitional_Break_Continue(body, LEVEL);  // no condition bind [1]
    else
        assert(Is_Frame(body));

} evaluate_condition: {  /////////////////////////////////////////////////////

    STATE = ST_WHILE_EVALUATING_CONDITION;
    return CONTINUE_CORE(
        SPARE,
        LEVEL_FLAG_RAISED_RESULT_OK,  // want to catch DONE error
        SPECIFIED,
        condition
    );

} condition_eval_in_spare: {  ////////////////////////////////////////////////

    if (Is_Raised(SPARE) and Is_Error_Done_Signal(SPARE))
        goto return_out;

    Decay_If_Unstable(SPARE);

    if (Is_Inhibitor(stable_SPARE))  // falsey condition => last body result
        goto return_out;

    STATE = ST_WHILE_EVALUATING_BODY;  // body result => OUT
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for break/continue
    return CONTINUE_BRANCH(OUT, body, SPARE);

} body_eval_in_out: { ////////////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            return THROWN;

        if (breaking)
            return nullptr;
    }

    Disable_Dispatcher_Catching_Of_Throws(LEVEL);
    goto evaluate_condition;

} return_out: {  /////////////////////////////////////////////////////////////

    // 1. If someone writes:
    //
    //        flag: 'true
    //        while [true? flag] [flag: 'false, null]
    //
    //    We don't want that to evaluate to NULL--because NULL is reserved for
    //    signaling BREAK.  And VOID results are reserved for when the body
    //    never runs.  BRANCHED() encloses these in single-element packs.

    if (Is_Cell_Erased(OUT))
        return VOID;  // body never ran, so no result to return!

    return BRANCHED(OUT);  // put void and null in packs [1]
}}
