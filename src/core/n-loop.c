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
// Copyright 2012-2019 Ren-C Open Source Contributors
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
//        for-each x [1 2 3] [if x = 3 [continue] x]  => 2  ; would be bad
//
//    But our goal is that a loop which never runs its body be distinguishable
//    from one that has CONTINUE'd each body.  Unless those are allowed to be
//    indistinguishable, loop compositions that work don't work.  So instead:
//
//        for-each x [1 2 3] [if x != 3 [x]]  =>  ~()~ isotope
//
bool Try_Catch_Break_Or_Continue(
    Sink(Value(*)) out,
    Frame(*) frame_,
    bool* breaking
){
    Value(const*) label = VAL_THROWN_LABEL(frame_);

    // Throw /NAME-s used by CONTINUE and BREAK are the actual native
    // function values of the routines themselves.
    //
    if (not IS_FRAME(label))
        return false;

    if (ACT_DISPATCHER(VAL_ACTION(label)) == &N_break) {
        CATCH_THROWN(out, frame_);
        Init_Trash(out);  // caller must interpret breaking flag
        *breaking = true;
        return true;
    }

    if (ACT_DISPATCHER(VAL_ACTION(label)) == &N_continue) {
        //
        // !!! Continue with no argument acts the same as asking
        // for CONTINUE void (the form with an argument).  This makes sense
        // in cases like MAP-EACH (one wants a continue to not add any value)
        //
        CATCH_THROWN(out, frame_);
        ASSERT_STABLE(out);  // CONTINUE doesn't take unstable /WITH
        *breaking = false;
        return true;
    }

    return false; // caller should let all other thrown values bubble up
}


//
//  break: native [
//
//  {Exit the current iteration of a loop and stop iterating further}
//
//      return: []  ; !!! notation for divergent function?
//  ]
//
DECLARE_NATIVE(break)
//
// BREAK is implemented via a thrown signal that bubbles up through the stack.
// It uses the value of its own native function as the name of the throw,
// like `throw/name null :break`.
{
    INCLUDE_PARAMS_OF_BREAK;

    return Init_Thrown_With_Label(FRAME, Lib(NULL), Lib(BREAK));
}


//
//  continue: native [
//
//  "Throws control back to top of loop for next iteration."
//
//      return: []  ; !!! notation for divergent function?
//      /with "Act as if loop body finished with this value"
//          [<void> <opt> any-value!]
//  ]
//
DECLARE_NATIVE(continue)
//
// CONTINUE is implemented via a thrown signal that bubbles up through the
// stack.  It uses the value of its own native function as the name of the
// throw, like `throw/name value :continue`.
{
    INCLUDE_PARAMS_OF_CONTINUE;

    Value(*) v = ARG(with);
    if (not REF(with))
        Init_Void(v);  // See: https://forum.rebol.info/t/1965/3

    return Init_Thrown_With_Label(FRAME, v, Lib(CONTINUE));
}


//
//  Loop_Series_Common: C
//
static Bounce Loop_Series_Common(
    Frame(*) frame_,
    REBVAL *var, // Must not be movable from context expansion, see #2274
    const REBVAL *body,
    REBVAL *start,
    REBINT end,
    REBINT bump
){
    // !!! This bounds incoming `end` inside the array.  Should it assert?
    //
    if (end >= cast(REBINT, VAL_LEN_HEAD(start)))
        end = cast(REBINT, VAL_LEN_HEAD(start));
    if (end < 0)
        end = 0;

    // A value cell exposed to the user is used to hold the state.  This means
    // if they change `var` during the loop, it affects the iteration.  Hence
    // it must be checked for changing to another series, or non-series.
    //
    Copy_Cell(var, start);
    REBIDX *state = &VAL_INDEX_UNBOUNDED(var);

    // Run only once if start is equal to end...edge case.
    //
    REBINT s = VAL_INDEX(start);
    if (s == end) {
        if (Do_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
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
            ? cast(REBINT, *state) <= end
            : cast(REBINT, *state) >= end
    ){
        if (Do_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
                return THROWN;

            if (breaking)
                return nullptr;
        }

        if (
            VAL_TYPE(var) != VAL_TYPE(start)
            or VAL_SERIES(var) != VAL_SERIES(start)
        ){
            fail ("Can only change series index, not series to iterate");
        }

        // Note that since the array is not locked with SERIES_INFO_HOLD, it
        // can be mutated during the loop body, so the end has to be refreshed
        // on each iteration.  Review ramifications of HOLD-ing it.
        //
        if (end >= cast(REBINT, VAL_LEN_HEAD(start)))
            end = cast(REBINT, VAL_LEN_HEAD(start));

        *state += bump;
    }

    if (Is_Fresh(OUT))
        return VOID;

    return BRANCHED(OUT);
}


//
//  Loop_Integer_Common: C
//
static Bounce Loop_Integer_Common(
    Frame(*) frame_,
    REBVAL *var,  // Must not be movable from context expansion, see #2274
    const REBVAL *body,
    REBI64 start,
    REBI64 end,
    REBI64 bump
){
    // A value cell exposed to the user is used to hold the state.  This means
    // if they change `var` during the loop, it affects the iteration.  Hence
    // it must be checked for changing to a non-integer form.
    //
    Reset_Unquoted_Header_Untracked(TRACK(var), CELL_MASK_INTEGER);
    REBI64* state = &mutable_VAL_INT64(var);
    *state = start;

    // Run only once if start is equal to end...edge case.
    //
    if (start == end) {
        if (Do_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
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
        if (Do_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
                return THROWN;

            if (breaking)
                return nullptr;
        }

        if (not IS_INTEGER(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        if (REB_I64_ADD_OF(*state, bump, state))
            fail (Error_Overflow_Raw());
    }

    return BRANCHED(OUT);
}


//
//  Loop_Number_Common: C
//
static Bounce Loop_Number_Common(
    Frame(*) frame_,
    REBVAL *var,  // Must not be movable from context expansion, see #2274
    const REBVAL *body,
    REBVAL *start,
    REBVAL *end,
    REBVAL *bump
){
    REBDEC s;
    if (IS_INTEGER(start))
        s = cast(REBDEC, VAL_INT64(start));
    else if (IS_DECIMAL(start) or IS_PERCENT(start))
        s = VAL_DECIMAL(start);
    else
        fail (start);

    REBDEC e;
    if (IS_INTEGER(end))
        e = cast(REBDEC, VAL_INT64(end));
    else if (IS_DECIMAL(end) or IS_PERCENT(end))
        e = VAL_DECIMAL(end);
    else
        fail (end);

    REBDEC b;
    if (IS_INTEGER(bump))
        b = cast(REBDEC, VAL_INT64(bump));
    else if (IS_DECIMAL(bump) or IS_PERCENT(bump))
        b = VAL_DECIMAL(bump);
    else
        fail (bump);

    // As in Loop_Integer_Common(), the state is actually in a cell; so each
    // loop iteration it must be checked to ensure it's still a decimal...
    //
    Reset_Unquoted_Header_Untracked(TRACK(var), CELL_MASK_DECIMAL);
    REBDEC *state = &VAL_DECIMAL(var);
    *state = s;

    // Run only once if start is equal to end...edge case.
    //
    if (s == e) {
        if (Do_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
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
        if (Do_Branch_Throws(OUT, body)) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
                return THROWN;

            if (breaking)
                return nullptr;
        }

        if (not IS_DECIMAL(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        *state += b;
    }

    if (Is_Fresh(OUT))
        return VOID;

    return BRANCHED(OUT);
}


// Virtual_Bind_To_New_Context() allows LIT-WORD! syntax to reuse an existing
// variables binding:
//
//     x: 10
//     for-each 'x [20 30 40] [...]
//     ; The 10 will be overwritten, and x will be equal to 40, here
//
// It accomplishes this by putting a word into the "variable" slot, and having
// a flag to indicate a dereference is necessary.
//
REBVAL *Real_Var_From_Pseudo(REBVAL *pseudo_var) {
    if (Not_Cell_Flag(pseudo_var, BIND_NOTE_REUSE))
        return pseudo_var;
    if (IS_BLANK(pseudo_var))  // e.g. `for-each _ [1 2 3] [...]`
        return nullptr;  // signal to throw generated quantity away

    // Note: these variables are fetched across running arbitrary user code.
    // So the address cannot be cached...e.g. the object it lives in might
    // expand and invalidate the location.  (The `context` for fabricated
    // variables is locked at fixed size.)
    //
    assert(IS_QUOTED_WORD(pseudo_var));
    return Lookup_Mutable_Word_May_Fail(pseudo_var, SPECIFIED);
}


//
//  cfor: native [
//
//  {Evaluate a block over a range of values. (See also: REPEAT)}
//
//      return: [<opt> <void> any-value!]
//      :word [word!]
//          "Variable to hold current value"
//      start [any-series! any-number!]
//          "Starting value"
//      end [any-series! any-number!]
//          "Ending value"
//      bump [any-number!]
//          "Amount to skip each time"
//      body [<const> any-branch!]
//          "Code to evaluate"
//  ]
//
DECLARE_NATIVE(cfor)
{
    INCLUDE_PARAMS_OF_CFOR;

    Context(*) context = Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        ARG(word)
    );
    Init_Object(ARG(word), context);  // keep GC safe

    REBVAL *var = CTX_VAR(context, 1);  // not movable, see #2274

    if (
        IS_INTEGER(ARG(start))
        and IS_INTEGER(ARG(end))
        and IS_INTEGER(ARG(bump))
    ){
        return Loop_Integer_Common(
            frame_,
            var,
            ARG(body),
            VAL_INT64(ARG(start)),
            IS_DECIMAL(ARG(end))
                ? cast(REBI64, VAL_DECIMAL(ARG(end)))
                : VAL_INT64(ARG(end)),
            VAL_INT64(ARG(bump))
        );
    }

    if (ANY_SERIES(ARG(start))) {
        if (ANY_SERIES(ARG(end))) {
            return Loop_Series_Common(
                frame_,
                var,
                ARG(body),
                ARG(start),
                VAL_INDEX(ARG(end)),
                Int32(ARG(bump))
            );
        }
        else {
            return Loop_Series_Common(
                frame_,
                var,
                ARG(body),
                ARG(start),
                Int32s(ARG(end), 1) - 1,
                Int32(ARG(bump))
            );
        }
    }

    return Loop_Number_Common(
        frame_, var, ARG(body), ARG(start), ARG(end), ARG(bump)
    );
}


//
//  for-skip: native [
//
//  "Evaluates a block for periodic values in a series"
//
//      return: "Last body result, or null if BREAK"
//          [<opt> <void> any-value!]
//      'word "Variable set to each position in the series at skip distance"
//          [word! lit-word! blank!]
//      series "The series to iterate over"
//          [<maybe> any-series!]
//      skip "Number of positions to skip each time"
//          [<maybe> integer!]
//      body "Code to evaluate each time"
//          [<const> any-branch!]
//  ]
//
DECLARE_NATIVE(for_skip)
{
    INCLUDE_PARAMS_OF_FOR_SKIP;

    REBVAL *series = ARG(series);

    REBINT skip = Int32(ARG(skip));
    if (skip == 0) {
        //
        // !!! https://forum.rebol.info/t/infinite-loops-vs-errors/936
        //
        return VOID;
    }

    Context(*) context = Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        ARG(word)
    );
    Init_Object(ARG(word), context);  // keep GC safe

    REBVAL *pseudo_var = CTX_VAR(context, 1); // not movable, see #2274
    REBVAL *var = Real_Var_From_Pseudo(pseudo_var);
    Copy_Cell(var, series);

    // Starting location when past end with negative skip:
    //
    if (
        skip < 0
        and VAL_INDEX_UNBOUNDED(var) >= cast(REBIDX, VAL_LEN_HEAD(var))
    ){
        VAL_INDEX_UNBOUNDED(var) = VAL_LEN_HEAD(var) + skip;
    }

    while (true) {
        REBINT len = VAL_LEN_HEAD(var);  // VAL_LEN_HEAD() always >= 0
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

        if (Do_Branch_Throws(OUT, ARG(body))) {
            bool breaking;
            if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
                return THROWN;

            if (breaking)
                return nullptr;
        }

        // Modifications to var are allowed, to another ANY-SERIES! value.
        //
        // If `var` is movable (e.g. specified via LIT-WORD!) it must be
        // refreshed each time arbitrary code runs, since the context may
        // expand and move the address, may get PROTECTed, etc.
        //
        var = Real_Var_From_Pseudo(pseudo_var);

        if (Is_Nulled(var))
            fail (PARAM(word));
        if (not ANY_SERIES(var))
            fail (var);

        // Increment via skip, which may go before 0 or after the tail of
        // the series.
        //
        // !!! Should also check for overflows of REBIDX range.
        //
        VAL_INDEX_UNBOUNDED(var) += skip;
    }

    if (Is_Fresh(OUT))
        return VOID;

    return BRANCHED(OUT);
}


//
//  stop: native [
//
//  {End the current iteration of CYCLE, optionally returning a value}
//
//      return: []  ; !!! Notation for divergent functions?s
//      /with "Act as if loop body finished with this value"
//          [<void> any-value!]
//  ]
//
DECLARE_NATIVE(stop)  // See CYCLE for notes about STOP
{
    INCLUDE_PARAMS_OF_STOP;

    Value(*) v = ARG(with);
    if (not REF(with))
        Init_Void(v);  // See: https://forum.rebol.info/t/1965/3

    return Init_Thrown_With_Label(FRAME, v, Lib(STOP));
}


//
//  cycle: native [
//
//  "Evaluates a block endlessly, until a BREAK or a STOP is hit"
//
//      return: [<opt> any-value!]
//          {Null if BREAK, or non-null value passed to STOP}
//      body [<const> any-branch!]
//          "Block or action to evaluate each time"
//  ]
//
DECLARE_NATIVE(cycle)
//
// 1. Most loops are not allowed to explicitly return a value and stop looping,
//    because that would make it impossible to tell from the outside whether
//    they'd requested a stop or if they'd naturally completed.  It would be
//    impossible to propagate a value-bearing break request to an aggregate
//    looping construct without invasively rebinding the break.
//
//    CYCLE is different because it doesn't have any loop exit condition.
//    Hence it responds to a STOP request, which lets it return any value.
//
// 2. Technically, we know CYCLE's body will always run.  So we could make an
//    exception to having it return void from STOP (or pure NULL).  There's
//    probably no good reason to do that, so right now we stick with the usual
//    branch policies.  Review if a good use case shows up.
{
    INCLUDE_PARAMS_OF_CYCLE;

    Value(*) body = ARG(body);

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

    STATE = ST_CYCLE_EVALUATING_BODY;
    return CATCH_CONTINUE(OUT, body);

} body_was_evaluated: {  /////////////////////////////////////////////////////

    if (THROWING)
        goto handle_thrown;

    return CATCH_CONTINUE(OUT, body);  // no break or stop, so keep going

} handle_thrown: {  /////////////////////////////////////////////////////////

    bool breaking;
    if (Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking)) {
        if (breaking)
            return nullptr;

        return CATCH_CONTINUE(OUT, body);  // plain continue
    }

    const REBVAL *label = VAL_THROWN_LABEL(FRAME);
    if (
        IS_FRAME(label)
        and ACT_DISPATCHER(VAL_ACTION(label)) == &N_stop
    ){
        CATCH_THROWN(OUT, FRAME);  // Unlike BREAK, STOP takes an arg--see [1]

        if (Is_Void(OUT))  // STOP with no arg, void usually reserved, see [2]
            return Init_Heavy_Void(OUT);

        if (Is_Nulled(OUT))
            return Init_Heavy_Null(OUT);  // NULL usually for BREAK, see [2]

        return OUT;
    }

    return THROWN;
}}


struct Reb_Enum_Series {
    REBLEN index;  // index into the data for filling current variable
    REBLEN len;  // length of the data
};

typedef struct Reb_Enum_Series ESER;

struct Loop_Each_State {
    REBVAL *data;  // possibly API handle if converted from sequence
    const REBSER *series;  // series data being enumerated (if applicable)
    union {
        EVARS evars;
        ESER eser;
    } u;
    bool took_hold;
    bool more_data;
    REBSPC *specifier;  // specifier (if applicable)
};

//
//  Init_Loop_Each: C
//
void Init_Loop_Each(Value(*) iterator, Value(*) data)
{
    struct Loop_Each_State *les = TRY_ALLOC(struct Loop_Each_State);

    // !!! Temporarily turn any sequences into a BLOCK!, rather than worry
    // about figuring out how to iterate optimized series.  Review as part of
    // an overall vetting of "generic iteration" (is a poor substitute for).
    //
    assert(not Is_Api_Value(data));  // we will free API handles
    if (ANY_SEQUENCE(data)) {
        data = rebValue(Canon(AS), Canon(BLOCK_X), rebQ(data));
        rebUnmanage(data);
    }

    les->specifier = SPECIFIED;

    if (Is_Activation(data)) {
        //
        // The value is generated each time by calling the data action.
        // Assign values to avoid compiler warnings.
        //
        les->took_hold = false;
        les->more_data = true;  // !!! Needs to do first call
        les->series = nullptr;
    }
    else {
        if (ANY_SERIES(data)) {
            les->series = VAL_SERIES(data);
            les->u.eser.index = VAL_INDEX(data);
            les->u.eser.len = VAL_LEN_HEAD(data);  // has HOLD, won't change

            if (ANY_ARRAY(data))
                les->specifier = VAL_SPECIFIER(data);
        }
        else if (ANY_CONTEXT(data)) {
            les->series = CTX_VARLIST(VAL_CONTEXT(data));
            Init_Evars(&les->u.evars, data);
        }
        else if (IS_MAP(data)) {
            les->series = MAP_PAIRLIST(VAL_MAP(data));
            les->u.eser.index = 0;
            les->u.eser.len = SER_USED(les->series);  // has HOLD, won't change
        }
        else
            panic ("Illegal type passed to Loop_Each()");

        // HOLD so length can't change

        les->took_hold = NOT_SERIES_FLAG(les->series, FIXED_SIZE);
        if (les->took_hold)
            SET_SERIES_FLAG(m_cast(REBSER*, les->series), FIXED_SIZE);

        if (ANY_CONTEXT(data)) {
            les->more_data = Did_Advance_Evars(&les->u.evars);
        }
        else {
            les->more_data = (les->u.eser.index < les->u.eser.len);
        }
    }

    les->data = data;  // shorter to use plain `data` above

    Init_Handle_Cdata(iterator, les, sizeof(les));
}


//
//  Loop_Each_Throws: C
//
// Common to FOR-EACH, MAP-EACH, and EVERY.  This takes an enumeration state
// and fills variables in a context with as much of that state as possible.
// The context containing the variables is created from a block:
//
//      >> for-each [a b] [1 2 3 4] [-- a b]]
//      -- a: 1 b: 2
//      -- a: 3 b: 4
//
// ANY-CONTEXT! and MAP! allow one var (keys) or two vars (keys/vals).
//
// It's possible to opt out of variable slots using BLANK!.
//
static bool Try_Loop_Each_Next(Value(const*) iterator, Context(*) vars_ctx)
{
    struct Loop_Each_State *les;
    les = VAL_HANDLE_POINTER(struct Loop_Each_State, iterator);

    if (not les->more_data)
        return false;

    const REBVAR *pseudo_tail;
    REBVAL *pseudo_var = CTX_VARS(&pseudo_tail, vars_ctx);
    for (; pseudo_var != pseudo_tail; ++pseudo_var) {
        REBVAL *var = Real_Var_From_Pseudo(pseudo_var);

        if (not les->more_data) {
            Init_Nulled(var);  // Y is null in `for-each [x y] [1] ...`
            continue;  // the `for` variable acquisition loop
        }

        enum Reb_Kind kind = VAL_TYPE(les->data);

        if (Is_Activation(les->data)) {
            REBVAL *generated = rebValue(rebRUN(les->data));
            if (generated) {
                if (var)
                    Copy_Cell(var, generated);
                rebRelease(generated);
            }
            else {
                les->more_data = false;  // any remaining vars must be unset
                if (pseudo_var == CTX_VARS_HEAD(vars_ctx)) {
                    //
                    // If we don't have at least *some* of the variables
                    // set for this body loop run, don't run the body.
                    //
                    return false;
                }
                if (var)
                    Init_Nulled(var);
            }
        }
        else switch (kind) {
          case REB_BLOCK:
          case REB_SET_BLOCK:
          case REB_GET_BLOCK:
          case REB_META_BLOCK:
          case REB_THE_BLOCK:
          case REB_GROUP:
          case REB_SET_GROUP:
          case REB_GET_GROUP:
          case REB_META_GROUP:
          case REB_THE_GROUP:
          case REB_PATH:
          case REB_SET_PATH:
          case REB_GET_PATH:
          case REB_META_PATH:
          case REB_THE_PATH:
          case REB_TUPLE:
          case REB_SET_TUPLE:
          case REB_GET_TUPLE:
          case REB_META_TUPLE:
          case REB_THE_TUPLE:
            if (var)
                Derelativize(
                    var,
                    ARR_AT(ARR(les->series), les->u.eser.index),
                    les->specifier
                );
            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;
            break;

          case REB_OBJECT:
          case REB_ERROR:
          case REB_PORT:
          case REB_MODULE:
          case REB_FRAME: {
            if (var) {
                assert(les->u.evars.index != 0);
                Init_Any_Word_Bound(
                    var,
                    REB_WORD,
                    KEY_SYMBOL(les->u.evars.key),
                    VAL_CONTEXT(les->data),
                    les->u.evars.index
                );
            }

            if (CTX_LEN(vars_ctx) == 1) {
                //
                // Only wanted the key (`for-each key obj [...]`)
            }
            else if (CTX_LEN(vars_ctx) == 2) {
                //
                // Want keys and values (`for-each key val obj [...]`)
                //
                ++pseudo_var;
                var = Real_Var_From_Pseudo(pseudo_var);
                Copy_Cell(var, les->u.evars.var);
            }
            else
                fail ("Loop enumeration of contexts must be 1 or 2 vars");

            les->more_data = Did_Advance_Evars(&les->u.evars);
            break; }

          case REB_MAP: {
            assert(les->u.eser.index % 2 == 0);  // should be on key slot

            const REBVAL *key;
            const REBVAL *val;
            while (true) {  // pass over the unused map slots
                key = SPECIFIC(ARR_AT(ARR(les->series), les->u.eser.index));
                ++les->u.eser.index;
                val = SPECIFIC(ARR_AT(ARR(les->series), les->u.eser.index));
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

            if (CTX_LEN(vars_ctx) == 1) {
                //
                // Only wanted the key (`for-each key map [...]`)
            }
            else if (CTX_LEN(vars_ctx) == 2) {
                //
                // Want keys and values (`for-each key val map [...]`)
                //
                ++pseudo_var;
                var = Real_Var_From_Pseudo(pseudo_var);
                Copy_Cell(var, val);
            }
            else
                fail ("Loop enumeration of contexts must be 1 or 2 vars");

            break; }

          case REB_BINARY: {
            Binary(const*) bin = BIN(les->series);
            if (var)
                Init_Integer(var, BIN_HEAD(bin)[les->u.eser.index]);
            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;
            break; }

          case REB_TEXT:
          case REB_TAG:
          case REB_FILE:
          case REB_EMAIL:
          case REB_URL:
            if (var)
                Init_Char_Unchecked(
                    var,
                    GET_CHAR_AT(STR(les->series), les->u.eser.index)
                );
            if (++les->u.eser.index == les->u.eser.len)
                les->more_data = false;
            break;

          default:
            panic ("Unsupported type");
        }
    }

    return true;
}

//
//  Shutdown_Loop_Each: C
//
// Cleanups that need to be done despite error, throw, etc.
//
void Shutdown_Loop_Each(Value(*) iterator)
{
    struct Loop_Each_State *les;
    les = VAL_HANDLE_POINTER(struct Loop_Each_State, iterator);

    if (les->took_hold)  // release read-only lock
        CLEAR_SERIES_FLAG(m_cast(REBSER*, les->series), FIXED_SIZE);

    if (ANY_CONTEXT(les->data))
        Shutdown_Evars(&les->u.evars);

    if (Is_Api_Value(les->data))  // free data last (used above)
        rebRelease(les->data);

    FREE(struct Loop_Each_State, les);
    Init_Trash(iterator);
}


//
//  for-each: native [
//
//  {Evaluates a block for each value(s) in a series.}
//
//      return: "Last body result, or null if BREAK"
//          [<opt> <void> any-value!]
//      :vars "Word or block of words to set each time, no new var if quoted"
//          [blank! word! lit-word! block! group!]
//      data "The series to traverse"
//          [<maybe> blank! any-series! any-context! map! any-sequence!
//           activation?]  ; activation support experimental, e.g. generators
//      body "Block to evaluate each time"
//          [<const> block! meta-block!]
//  ]
//
DECLARE_NATIVE(for_each)
{
    INCLUDE_PARAMS_OF_FOR_EACH;

    Value(*) vars = ARG(vars);  // transformed to context on initial_entry
    Value(*) data = ARG(data);
    Value(*) body = ARG(body);  // bound to vars context on initial_entry

    Value(*) iterator = ARG(return);  // reuse to hold Loop_Each_State

    bool breaking = false;

    enum {
        ST_FOR_EACH_INITIAL_ENTRY = STATE_0,
        ST_FOR_EACH_RUNNING_BODY
    };

    if (Get_Frame_Flag(frame_, ABRUPT_FAILURE))  // a fail() in this dispatcher
        goto finalize_for_each;

    switch (STATE) {
      case ST_FOR_EACH_INITIAL_ENTRY : goto initial_entry;
      case ST_FOR_EACH_RUNNING_BODY : goto body_result_in_spare_or_threw;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (IS_BLANK(data))  // same response as to empty series
        return VOID;

    Context(*) pseudo_vars_ctx = Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        ARG(vars)
    );
    Init_Object(ARG(vars), pseudo_vars_ctx);  // keep GC safe

    Init_Loop_Each(iterator, data);
    Set_Frame_Flag(frame_, NOTIFY_ON_ABRUPT_FAILURE);  // to clean up iterator

    goto next_iteration;

} next_iteration: {  /////////////////////////////////////////////////////////

    if (not Try_Loop_Each_Next(iterator, VAL_CONTEXT(vars)))
        goto finalize_for_each;

    STATE = ST_FOR_EACH_RUNNING_BODY;
    return CATCH_CONTINUE_BRANCH(OUT, body);

} body_result_in_spare_or_threw: {  //////////////////////////////////////////

    if (THROWING) {
        if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
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

    if (Is_Fresh(OUT))
        return VOID;

    return BRANCHED(OUT);
}}


//
//  every: native [
//
//  {Iterate and return null if any previous body evaluations were falsey}
//
//      return: [<opt> <void> any-value!]
//          {null on BREAK, blank on empty, false or the last truthy value}
//      :vars "Word or block of words to set each time, no new var if quoted"
//          [blank! word! lit-word! block! group!]
//      data "The series to traverse"
//          [<maybe> any-series! any-context! map! activation?]
//      body [<const> block! meta-block!]
//          "Block to evaluate each time"
//  ]
//
DECLARE_NATIVE(every)
//
// 1. In light of other tolerances in the system for voids in logic tests
//    (see ALL & ANY), EVERY treats a void as "no vote".
//
//        every x [1 2 3 4] [if odd? x [x]]  =>  [1 3]
//
//        every x [1 2 3 4] [maybe if odd? x [x]]  => none (~) isotope
//
//        every x [1 2 3 4] [comment "heavy"]  => none (~) isotope
//
//    But it returns a none isotope (~) on the skipped bodies, as loop
//    composition breaks down if we try to keep old values.
//
// 2. We don't decay isotopes, for the reason we don't decay them in ALL etc:
//
//        every x [~false~ ~true~] [match logic! decay x]
//
//    The ~false~ isotope here catches the misunderstanding by erroring.
{
    INCLUDE_PARAMS_OF_EVERY;

    Value(*) vars = ARG(vars);  // transformed to context on initial_entry
    Value(*) data = ARG(data);
    Value(*) body = ARG(body);  // bound to vars context on initial_entry

    Value(*) iterator = ARG(return);  // place to store iteration state

    enum {
        ST_EVERY_INITIAL_ENTRY = STATE_0,
        ST_EVERY_RUNNING_BODY
    };

    if (Get_Frame_Flag(frame_, ABRUPT_FAILURE))  // a fail() in this dispatcher
        goto finalize_every;

    switch (STATE) {
      case ST_EVERY_INITIAL_ENTRY : goto initial_entry;
      case ST_EVERY_RUNNING_BODY : goto body_result_in_spare;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (IS_BLANK(data))  // same response as to empty series
        return VOID;

    Context(*) pseudo_vars_ctx = Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        ARG(vars)
    );
    Init_Object(ARG(vars), pseudo_vars_ctx);  // keep GC safe

    Init_Loop_Each(iterator, data);
    Set_Frame_Flag(frame_, NOTIFY_ON_ABRUPT_FAILURE);  // to clean up iterator

    goto next_iteration;

} next_iteration: {  /////////////////////////////////////////////////////////

    if (not Try_Loop_Each_Next(iterator, VAL_CONTEXT(vars)))
        goto finalize_every;

    STATE = ST_EVERY_RUNNING_BODY;
    return CATCH_CONTINUE(SPARE, body);

} body_result_in_spare: {  ///////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(SPARE, FRAME, &breaking))
            goto finalize_every;

        if (breaking) {
            Init_Nulled(OUT);
            goto finalize_every;
        }
    }

    if (
        Is_Void(SPARE)
        or Is_Heavy_Void(SPARE)
        or (IS_META_BLOCK(body) and Is_Meta_Of_Void(SPARE))
    ){
        Init_Heavy_Void(OUT);  // forget OUT for loop composition, see [1]
        goto next_iteration;  // ...but void does not NULL-lock output
    }

    if (Is_Falsey(SPARE)) {
        Init_Nulled(OUT);
    }
    else if (Is_Fresh(OUT) or not Is_Nulled(OUT)) {
        Move_Cell(OUT, SPARE);
    }

    goto next_iteration;

} finalize_every: {  /////////////////////////////////////////////////////////

    Shutdown_Loop_Each(iterator);

    if (THROWING)
        return THROWN;

    if (Is_Fresh(OUT))
        return VOID;

    return OUT;
}}


//
//  remove-each: native [
//
//  {Removes values for each block that returns true}
//
//      return: "Number of removed series items, or null if BREAK"
//          [<opt> integer!]
//      :vars "Word or block of words to set each time, no new var if quoted"
//          [blank! word! lit-word! block! group!]
//      data "The series to traverse (modified)"
//          [<maybe> any-series!]
//      body "Block to evaluate (return TRUE to remove)"
//          [<const> block!]
//  ]
//
DECLARE_NATIVE(remove_each)
//
// 1. For reasons of semantics and performance, REMOVE-EACH does not actually
//    perform removals "as it goes".  It could run afoul of any number of
//    problems, including the mutable series becoming locked during iteration.
//    Hence the series is locked, and removals aren't applied until the end.
//    However, this means that there's state which must be finalized on every
//    possible exit path.  (Errors, throws, completion)
//
// 2. Updating arrays in place may not be better than pushing kept values to
//    the data stack and then creating a precisely-sized output blob to swap as
//    underlying memory for the array.  (Imagine a large array from which there
//    are many removals, and the ensuing wasted space being left behind).  We
//    use the method anyway, to test novel techniques and error handling.
//
// 3. For binaries and strings, we push new data as the loop runs.  Then at
//    the end of the enumeration, the identity of the incoming series is kept
//    but the new underlying data is poked into it--and the old data free.
//
// 4. When a BREAK happens there is no change applied to the series.  It is
//    conceivable that might not be what people want--and that if they did
//    want that, they would likely use a MAP-EACH or something to generate
//    a new series.  However, NULL is reserved for when loops break, so there
//    would not be a way to get the number of removals in this case.  Hence
//    it is semantically easiest to say BREAK goes along with "no effect".
//
//    UPDATE: With multi-returns, the number of removals could be a secondary
//    result.  It still feels a bit uncomfortable to have the result usually
//    be the modified series, but then NULL if there's a BREAK.
//
// 5. We do not want to decay isotopes, e.g. if someone tried to say:
//
//        remove-each x [...] [n: _, ..., match [logic! integer!] false]
//
//    The ~false~ isotope protects from having a condition you thought should
//    be truthy come back #[false] and be falsey.
//
// 6. The only signals allowed are LOGIC! and void.  This is believed to be
//    more coherent, and likely would catch more errors than just allowing any
//    truthy value to mean "remove".
//
// 7. We are reusing the mold buffer for BINARY!, but *not putting UTF-8 data*
//    into it.  Revisit if this inhibits cool UTF-8 based tricks the mold
//    buffer might do otherwise.
{
    INCLUDE_PARAMS_OF_REMOVE_EACH;

    Value(*) data = ARG(data);
    Value(*) body = ARG(body);

    REBSER *series = VAL_SERIES_ENSURE_MUTABLE(data);  // check even if empty

    if (VAL_INDEX(data) >= VAL_LEN_AT(data))  // past series end
        return Init_Integer(OUT, 0);

    Context(*) context = Virtual_Bind_Deep_To_New_Context(
        body,  // may be updated, will still be GC safe
        ARG(vars)
    );
    Init_Object(ARG(vars), context);  // keep GC safe

    REBLEN start = VAL_INDEX(data);

    DECLARE_MOLD (mo);
    if (ANY_ARRAY(data)) {
        //
        // We're going to use NODE_FLAG_MARKED on the elements of data's
        // array for those items we wish to remove later.  See [2]
        //
        TRASH_POINTER_IF_DEBUG(mo);
    }
    else {
        // Generate a new data allocation, but then swap its underlying content
        // to back the series we were given.  See [3]
        //
        Push_Mold(mo);
    }

    SET_SERIES_INFO(series, HOLD);  // disallow mutations until finalize

    REBLEN len = ANY_STRING(data)
        ? STR_LEN(STR(series))
        : SER_USED(series);  // temp read-only, this won't change

    bool threw = false;
    bool breaking = false;

    REBLEN index = start;
    while (index < len) {
        assert(start == index);

        const REBVAR *var_tail;
        REBVAL *var = CTX_VARS(&var_tail, context);  // fixed (#2274)
        for (; var != var_tail; ++var) {
            if (index == len) {
                Init_Nulled(var);  // Y on 2nd step of remove-each [x y] "abc"
                continue;  // the `for` loop setting variables
            }

            if (ANY_ARRAY(data))
                Derelativize(
                    var,
                    ARR_AT(VAL_ARRAY(data), index),
                    VAL_SPECIFIER(data)
                );
            else if (IS_BINARY(data)) {
                Binary(*) bin = BIN(series);
                Init_Integer(var, cast(REBI64, BIN_HEAD(bin)[index]));
            }
            else {
                assert(ANY_STRING(data));
                Init_Char_Unchecked(
                    var,
                    GET_CHAR_AT(STR(series), index)
                );
            }
            ++index;
        }

        if (Do_Any_Array_At_Throws(OUT, body, SPECIFIED)) {
            if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking)) {
                threw = true;
                goto finalize_remove_each;
            }

            if (breaking) {  // break semantics are no-op, see [4]
                assert(start < len);
                goto finalize_remove_each;
            }
        }

        bool keep;

        if (Is_Void(OUT)) {
            keep = true;  // treat same as logic false (e.g. don't remove)
        }
        else if (IS_LOGIC(OUT)) {  // pure logic required, see [6]
            keep = not VAL_LOGIC(OUT);
        }
        else if (Is_Nulled(OUT)) {  // don't remove
            keep = true;
            Init_Heavy_Null(OUT);  // NULL reserved for BREAK signal
        }
        else if (Is_Isotope(OUT)) {  // don't decay isotopes, see [5]
            threw = true;
            Init_Error(SPARE, Error_Bad_Isotope(OUT));
            Init_Thrown_With_Label(FRAME, Lib(NULL), stable_SPARE);
            goto finalize_remove_each;
        }
        else if (Is_Blackhole(OUT)) {  // do remove
            keep = false;
        }
        else {
            threw = true;
            Init_Error(
                SPARE,
                Error_User("Use [LOGIC! NULL BLACKHOLE VOID] with REMOVE-EACH")
            );
            Init_Thrown_With_Label(FRAME, Lib(NULL), stable_SPARE);
            goto finalize_remove_each;
        }

        if (ANY_ARRAY(data)) {
            if (keep) {
                start = index;
                continue;  // keeping, don't mark for culling
            }

            do {
                assert(start <= len);
                Set_Cell_Flag(  // v-- okay to mark despite read only
                    m_cast(Cell(*), ARR_AT(VAL_ARRAY(data), start)),
                    NOTE_REMOVE
                );
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
                if (IS_BINARY(data)) {
                    Binary(*) bin = BIN(series);
                    Append_Ascii_Len(
                        mo->series,
                        cs_cast(BIN_AT(bin, start)),
                        1
                    );
                }
                else {
                    Append_Codepoint(
                        mo->series,
                        GET_CHAR_AT(STR(series), start)
                    );
                }
                ++start;
            } while (start != index);
        }

        Isotopify_If_Nulled(OUT);  // reserve NULL for BREAK
    }

    assert(start == len);  // normal completion (otherwise a `goto` happened)

  finalize_remove_each: {  ///////////////////////////////////////////////////

    REBLEN removals = 0;

    assert(GET_SERIES_INFO(series, HOLD));
    CLEAR_SERIES_INFO(series, HOLD);

    if (ANY_ARRAY(data)) {
        if (not threw and breaking) {  // clean marks, don't remove
            Cell(const*) tail;
            Cell(*) temp = VAL_ARRAY_KNOWN_MUTABLE_AT(&tail, data);
            for (; temp != tail; ++temp) {
                if (Get_Cell_Flag(temp, NOTE_REMOVE))
                    Clear_Cell_Flag(temp, NOTE_REMOVE);
            }
            goto done_finalizing;
        }

        Cell(const*) tail;
        Cell(*) dest = VAL_ARRAY_KNOWN_MUTABLE_AT(&tail, data);
        Cell(*) src = dest;

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
                SET_SERIES_LEN(VAL_ARRAY_KNOWN_MUTABLE(data), len);
                goto done_finalizing;
            }
            Copy_Cell(dest, src);  // same array, so we can do this
        }

        assert(removals == 0);  // didn't goto, so no removals
    }
    else if (IS_BINARY(data)) {
        if (not threw and breaking) {  // leave data unchanged
            Drop_Mold(mo);
            goto done_finalizing;
        }

        Binary(*) bin = BIN(series);

        // If there was a THROW, or fail() we need the remaining data
        //
        REBLEN orig_len = VAL_LEN_HEAD(data);
        assert(start <= orig_len);
        Append_Ascii_Len(
            mo->series,
            cs_cast(BIN_AT(bin, start)),
            orig_len - start
        );

        Binary(*) popped = Pop_Molded_Binary(mo);  // not UTF-8 if binary, see [7]

        assert(BIN_LEN(popped) <= VAL_LEN_HEAD(data));
        removals = VAL_LEN_HEAD(data) - BIN_LEN(popped);

        Swap_Series_Content(popped, series);  // swap series identity, see [3]

        Free_Unmanaged_Series(popped);  // now frees incoming series's data
    }
    else {
        assert(ANY_STRING(data));
        if (not threw and breaking) {  // leave data unchanged
            Drop_Mold(mo);
            goto done_finalizing;
        }

        // If there was a THROW, or fail() we need the remaining data
        //
        REBLEN orig_len = VAL_LEN_HEAD(data);
        assert(start <= orig_len);

        for (; start != orig_len; ++start) {
            Append_Codepoint(
                mo->series,
                GET_CHAR_AT(STR(series), start)
            );
        }

        Raw_String* popped = Pop_Molded_String(mo);

        assert(STR_LEN(popped) <= VAL_LEN_HEAD(data));
        removals = VAL_LEN_HEAD(data) - STR_LEN(popped);

        Swap_Series_Content(popped, series);  // swap series identity, see [3]

        Free_Unmanaged_Series(popped);  // frees incoming series's data
    }

  done_finalizing:

    if (threw)
        return THROWN;

    if (breaking)
        return nullptr;

    return Init_Integer(OUT, removals);
}}


//
//  map-each: native [
//
//  {Evaluate a block for each value(s) in a series and collect as a block.}
//
//      return: "Collected block"
//          [<opt> block!]
//      :vars "Word or block of words to set each time, no new var if quoted"
//          [blank! word! lit-word! block! group!]
//      data "The series to traverse"
//          [<maybe> blank! any-series! any-sequence! any-context!
//           activation?]
//      body "Block to evaluate each time (result will be kept literally)"
//          [<const> block!]
//  ]
//
DECLARE_NATIVE(map_each)
//
// MAP-EACH lacks the planned flexibility of MAP.  The syntax of FOR and MAP
// are intended to be generic to work with generators or a dialect.
{
    INCLUDE_PARAMS_OF_MAP_EACH;

    UNUSED(PARAM(vars));
    UNUSED(PARAM(body));

    // The theory is that MAP would use a dialect on BLOCK! arguments for data
    // by default, like [1 thru 10].  But you could give it an arbitrary
    // enumerating action and it would iteratively call it.  Since such an
    // iterator does not exist yet (and would not be cheap) a QUOTED! BLOCK!
    // is used temporarily as a substitute for passing a block iterator.
    //
    if (not IS_BLANK(ARG(data)))
        Quotify(ARG(data), 1);

    INIT_FRM_PHASE(frame_, ACT_IDENTITY(VAL_ACTION(Lib(MAP))));
    // INIT_FRM_BINDING ?

    Dispatcher* dispatcher = ACT_DISPATCHER(VAL_ACTION(Lib(MAP)));
    return dispatcher(frame_);
}


//
//  map: native [
//
//  {Evaluate a block for each value(s) in a series and collect as a block}
//
//      return: "Collected block"
//          [<opt> block!]
//      :vars "Word or block of words to set each time, no new var if quoted"
//          [blank! word! lit-word! block! group!]
//      data "The series to traverse (only QUOTED! BLOCK! at the moment...)"
//          [<maybe> blank! quoted! activation?]
//      :body "Block to evaluate each time"
//          [<const> block! meta-block!]
//  ]
//
DECLARE_NATIVE(map)
//
// 1. Void is allowed for skipping map elements:
//
//        map x each [1 2 3] [if even? x [x * 10]] => [20]
//
// 2. We use APPEND semantics on the body result; whatever APPEND would do with
//    the value, we do the same.  (Ideally the logic could be unified.)
//
// 3. MAP and MAP-EACH always return blocks except in cases of BREAK, e.g.
//    there's no way to detect from the outside if the body never ran.  Review
//    if variants would be useful (e.g. COLLECT* is NULL if nothing collected)
{
    INCLUDE_PARAMS_OF_MAP;

    Value(*) vars = ARG(vars);  // transformed to context on initial_entry
    Value(*) data = ARG(data);
    Value(*) body = ARG(body);  // bound to vars context on initial_entry

    Value(*) iterator = ARG(return);  // reuse to hold Loop_Each_State

    enum {
        ST_MAP_INITIAL_ENTRY = STATE_0,
        ST_MAP_RUNNING_BODY
    };

    if (Get_Frame_Flag(frame_, ABRUPT_FAILURE))  // a fail() in this dispatcher
        goto finalize_map;

    switch (STATE) {
      case ST_MAP_INITIAL_ENTRY : goto initial_entry;
      case ST_MAP_RUNNING_BODY : goto body_result_in_spare;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    assert(Is_Fresh(OUT));  // output only written during MAP if BREAK hit

    if (IS_BLANK(data))  // same response as to empty series
        return Init_Block(OUT, Make_Array(0));

    if (Is_Activation(data)) {
        // treat as a generator
    }
    else if (
        not IS_QUOTED(data)
        or VAL_QUOTED_DEPTH(data) != 1
        or not (
            ANY_SERIES(Unquotify(data, 1))
            or ANY_PATH(data)  // has been unquoted
            or ANY_CONTEXT(data)
        )
    ){
        fail ("MAP only supports one-level QUOTED! series/path/context ATM");
    }

    Context(*) pseudo_vars_ctx = Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        ARG(vars)
    );
    Init_Object(ARG(vars), pseudo_vars_ctx);  // keep GC safe

    Init_Loop_Each(iterator, data);
    Set_Frame_Flag(frame_, NOTIFY_ON_ABRUPT_FAILURE);  // to clean up iterator

    goto next_iteration;

} next_iteration: {  /////////////////////////////////////////////////////////

    if (not Try_Loop_Each_Next(iterator, VAL_CONTEXT(vars)))
        goto finalize_map;

    STATE = ST_MAP_RUNNING_BODY;
    return CATCH_CONTINUE(SPARE, body);  // body may be META-BLOCK!

} body_result_in_spare: {  ///////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(SPARE, FRAME, &breaking))
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
        Quasify_Isotope(SPARE);
        Cell(const*) tail;
        Cell(const*) v = VAL_ARRAY_AT(&tail, SPARE);
        for (; v != tail; ++v)
            Derelativize(PUSH(), v, VAL_SPECIFIER(SPARE));
    }
    else if (Is_Isotope(SPARE)) {
        Init_Error(SPARE, Error_Bad_Isotope(SPARE));
        Init_Thrown_Error(FRAME, stable_SPARE);
        goto finalize_map;
    }
    else if (Is_Nulled(SPARE)) {
        fail (Error_Need_Non_Null_Raw());
    }
    else
        Copy_Cell(PUSH(), SPARE);  // non nulls added to result

    goto next_iteration;

} finalize_map: {  ///////////////////////////////////////////////////////////

    Shutdown_Loop_Each(iterator);

    if (THROWING)
        return THROWN;  // automatically drops to baseline

    if (not Is_Fresh(OUT)) {  // only modifies on break
        assert(Is_Nulled(OUT));  // BREAK, so *must* return null
        Drop_Data_Stack_To(STACK_BASE);
        return nullptr;
    }

    return Init_Block(  // always returns block unless break, see [3]
        OUT,
        Pop_Stack_Values(STACK_BASE)
    );
}}


//
//  repeat: native [
//
//  {Evaluates a block a specified number of times}
//
//      return: "Last body result, or null if BREAK"
//          [<opt> <void> any-value!]
//      count "Repetitions (true loops infinitely, false doesn't run)"
//          [<maybe> any-number! logic!]
//      body "Block to evaluate or action to run"
//          [<unrun> <const> block! frame!]
//  ]
//
DECLARE_NATIVE(repeat)
//
// 1. We pass the index into the body if it's an ACTION! as we count.  But if
//    it's a LOGIC! TRUE no index is passed, because we don't count.  If we
//    were using arbitrary precision arithmetic, the count could have a
//    non-trivial cost to upkeep in large loops.
{
    INCLUDE_PARAMS_OF_REPEAT;

    Value(*) count = ARG(count);
    Value(*) body = ARG(body);

    Value(*) index = cast(Value(*), SPARE);  // spare cell holds current index

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

    if (IS_LOGIC(count)) {
        if (VAL_LOGIC(count) == false)
            return VOID;  // treat false as "don't run"

        STATE = ST_REPEAT_EVALUATING_BODY;  // true is "infinite loop"
        return CATCH_CONTINUE_BRANCH(OUT, body);  // no index, see [1]
    }

    if (VAL_INT64(count) <= 0)
        return VOID;  // negative means "don't run" (vs. error)

    Init_Integer(index, 1);

    STATE = ST_REPEAT_EVALUATING_BODY;
    return CATCH_CONTINUE_BRANCH(OUT, body, index);

} body_result_in_out: {  /////////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
            return THROWN;

        if (breaking)
            return nullptr;
    }

    if (IS_LOGIC(count)) {
        assert(VAL_LOGIC(count) == true);  // false already returned
        return CATCH_CONTINUE_BRANCH(OUT, body);  // true infinite loops
    }

    if (VAL_INT64(count) == VAL_INT64(index))  // reached the desired count
        return BRANCHED(OUT);

    mutable_VAL_INT64(index) += 1;

    return CATCH_CONTINUE_BRANCH(OUT, body, index);  // keep looping
}}


//
//  for: native [
//
//  {Evaluates a branch a number of times or over a series, return last result}
//
//      return: "Last body result, or NULL if BREAK"
//          [<opt> <void> any-value!]
//      :vars "Word or block of words to set each time, no new var if quoted"
//          [blank! word! lit-word! block! group!]
//      value "Maximum number or series to traverse"
//          [<maybe> any-number! any-sequence! quoted! block! activation?]
//      'body "!!! actually just BLOCK!, but quoted to catch legacy uses"
//          [<const> any-value!]
//  ]
//
DECLARE_NATIVE(for)
{
    INCLUDE_PARAMS_OF_FOR;

    REBVAL *vars = ARG(vars);
    REBVAL *value = ARG(value);
    REBVAL *body = ARG(body);

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

    if (IS_GROUP(body)) {
        if (Eval_Value_Throws(SPARE, body, SPECIFIED))
            return THROWN;
        Move_Cell(body, SPARE);
    }

    if (not IS_BLOCK(body))
        fail ("FOR has a new syntax, use CFOR for old arity-5 behavior.");

    if (IS_QUOTED(value)) {
        Unquotify(value, 1);

        if (not (ANY_SERIES(value) or ANY_SEQUENCE(value)))
            fail (PARAM(value));

        // Delegate to FOR-EACH (note: in the future this will be the other
        // way around, with FOR-EACH delegating to FOR).
        //
        rebPushContinuation(
            cast(REBVAL*, OUT),  // <-- output cell
            FRAME_MASK_NONE,
            Canon(FOR_EACH), ARG(vars), rebQ(value), body
        );
        return BOUNCE_DELEGATE;
    }

    if (IS_DECIMAL(value) or IS_PERCENT(value))
        Init_Integer(value, Int64(value));

    REBI64 n = VAL_INT64(value);
    if (n < 1)  // Loop_Integer from 1 to 0 with bump of 1 is infinite
        return VOID;

    Context(*) context = Virtual_Bind_Deep_To_New_Context(body, vars);
    Init_Object(ARG(vars), context);  // keep GC safe

    assert(CTX_LEN(context) == 1);

    REBVAL *var = CTX_VAR(VAL_CONTEXT(vars), 1);  // not movable, see #2274
    Init_Integer(var, 1);

    STATE = ST_FOR_RUNNING_BODY;
    return CATCH_CONTINUE_BRANCH(OUT, body, var);

} body_result_in_out: {  /////////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
            return THROWN;

        if (breaking)
            return nullptr;
    }

    REBVAL *var = CTX_VAR(VAL_CONTEXT(vars), 1);  // not movable, see #2274

    if (not IS_INTEGER(var))
        fail (Error_Invalid_Type(VAL_TYPE(var)));

    if (VAL_INT64(var) == VAL_INT64(value))
        return BRANCHED(OUT);

    if (REB_I64_ADD_OF(VAL_INT64(var), 1, &mutable_VAL_INT64(var)))
        fail (Error_Overflow_Raw());

    STATE = ST_FOR_RUNNING_BODY;
    return CATCH_CONTINUE_BRANCH(OUT, body, var);
}}


//
//  until: native [
//
//  {Evaluates the body until it produces a conditionally true value}
//
//      return: [<opt> any-value!]
//          {Last body result, or null if a BREAK occurred}
//      body [<const> block!]
//      /predicate "Function to apply to body result"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(until)
//
// 1. When CONTINUE has an argument, it acts as if the loop body evaluated to
//    that argument.  But UNTIL's condition and body are the same.  That means
//    CONTINUE/WITH TRUE will stop the UNTIL and return TRUE, CONTINUE/WITH 10
//    will stop and return 10, etc.
//
// 2. Testing the body result for truthiness or falseyness means that the
//    evaluated-to-value must be decayed.  Hence UNTIL cannot return something
//    like a pack...it must be META'd and the result UNMETA'd...with all
//    packs quasiforms being considered truthy.
//
// 3. Purusant to [1], we want CONTINUE (or CONTINUE VOID) to keep the loop
//    running.  For parity between what continue does with an argument and
//    what the loop does if the body evaluates to that argument, it suggests
//    tolerating a void body result as intent to continue the loop also.
//
{
    INCLUDE_PARAMS_OF_UNTIL;

    Value(*) body = ARG(body);
    Value(*) predicate = ARG(predicate);

    Atom(*) condition;  // can point to OUT or SPARE

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

    STATE = ST_UNTIL_EVALUATING_BODY;
    return CATCH_CONTINUE(OUT, body);

} body_result_in_out: {  /////////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
            return THROWN;

        if (breaking)
            return nullptr;

        // continue acts like body evaluated to its argument, see [1]
    }

    if (Is_Nulled(predicate)) {
        condition = OUT;  // default is just test truthiness of body product
        goto test_condition;
    }

    STATE = ST_UNTIL_RUNNING_PREDICATE;
    return CONTINUE(SPARE, predicate, OUT);

} predicate_result_in_spare: {  //////////////////////////////////////////////

    condition = SPARE;
    goto test_condition;

} test_condition: {  /////////////////////////////////////////////////////////

    Decay_If_Unstable(condition);  // must decay for truth test, see [2]

    if (not Is_Void(condition)) {  // skip voids, see [3]
        if (Is_Truthy(condition))
            return BRANCHED(OUT);  // truthy result, return value!
    }

    STATE = ST_UNTIL_EVALUATING_BODY;
    return CATCH_CONTINUE(OUT, body);  // not truthy, keep going
}}


//
//  while: native [
//
//  {So long as a condition is truthy, evaluate the body}
//
//      return: "Void if body never run, else last body result, null if BREAK"
//          [<opt> <void> any-value!]
//      condition [<const> block!]
//      body [<unrun> <const> block! frame!]
//  ]
//
DECLARE_NATIVE(while)
//
// 1. It was considered if `while true [...]` should infinite loop, and then
//    `while false [...]` never ran.  However, that could lead to accidents
//    like `while x > 10 [...]` instead of `while [x > 10] [...]`.  It is
//    probably safer to require a BLOCK! vs. falling back on such behaviors.
//
//    (It's now easy for people to make their own weird polymorphic loops.)
//
// 2. We have to pick a meaning when someone writes:
//
//        while [print "condition" continue] [print "body"]
//
//    R3-Alpha would alternate printing out "condition"/"body", which isn't
//    useful.  Here the decision is to assume a BREAK or CONTINUE in the
//    condition targets an enclosing loop--not this WHILE loop.
//
// 3. A weird idea being tried here is that if your condition vanishes, it
//    doesn't run the body, and it doesn't end the loop.  It acts as a continue
//    and re-runs the condition again.  This offers a feature that resembles
//    what someone might want for the semantics of [2], so it's being tried.
//
// 4. If someone writes:
//
//        flag: true, while [flag] [flag: false, null]
//
//    We don't want that to evaluate to NULL--because NULL is reserved for
//    signaling BREAK.  Similarly, void results are reserved for when the body
//    never runs--so they're turned into none (~)
{
    INCLUDE_PARAMS_OF_WHILE;

    Value(*) condition = ARG(condition);  // condition is BLOCK! only, see [1]
    Value(*) body = ARG(body);

    enum {
        ST_WHILE_INITIAL_ENTRY = STATE_0,
        ST_WHILE_EVALUATING_CONDITION,
        ST_WHILE_EVALUATING_BODY
    };

    switch (STATE) {
      case ST_WHILE_INITIAL_ENTRY : goto evaluate_condition;
      case ST_WHILE_EVALUATING_CONDITION : goto condition_was_evaluated;
      case ST_WHILE_EVALUATING_BODY : goto body_was_evaluated;
      default: assert(false);
    }

  evaluate_condition: {  /////////////////////////////////////////////////////

    STATE = ST_WHILE_EVALUATING_CONDITION;
    return CONTINUE(SPARE, condition);  // ignore BREAKs, see [2]

} condition_was_evaluated: {  ////////////////////////////////////////////////

    if (Is_Void(SPARE))
        goto evaluate_condition;  // skip body, see [3]

    if (Is_Falsey(SPARE)) {  // falsey condition => return last body result
        if (Is_Fresh(OUT))
            return VOID;  // body never ran, so no result to return!

        return BRANCHED(OUT);  // see [4]
    }

    STATE = ST_WHILE_EVALUATING_BODY;  // body result => OUT
    return CATCH_CONTINUE_BRANCH(OUT, body, SPARE);  // catch break & continue

} body_was_evaluated: {  /////////////////////////////////////////////////////

    if (THROWING) {
        bool breaking;
        if (not Try_Catch_Break_Or_Continue(OUT, FRAME, &breaking))
            return THROWN;

        if (breaking)
            return nullptr;
    }

    goto evaluate_condition;
}}
