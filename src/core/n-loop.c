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
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"
#include "sys-int-funcs.h" //REB_I64_ADD_OF

typedef enum {
    LOOP_FOR_EACH,
    LOOP_EVERY,
    LOOP_MAP_EACH
} LOOP_MODE;


//
//  Catching_Break_Or_Continue: C
//
// Determines if a thrown value is either a break or continue.  If so, `val`
// is mutated to become the throw's argument.  Sets `broke` flag if BREAK.
//
// Returning false means the throw was neither BREAK nor CONTINUE.
//
bool Catching_Break_Or_Continue(Value* val, bool *broke)
{
    assert(THROWN(val));

    // Throw /NAME-s used by CONTINUE and BREAK are the actual native
    // function values of the routines themselves.
    if (not Is_Action(val))
        return false;

    if (VAL_ACT_DISPATCHER(val) == &N_break) {
        *broke = true;
        CATCH_THROWN(val, val);
        assert(Is_Nulled(val)); // BREAK must always return NULL
        return true;
    }

    if (VAL_ACT_DISPATCHER(val) == &N_continue) {
        //
        // !!! Currently continue with no argument acts the same as asking
        // for CONTINUE NULL (the form with an argument).  This makes sense
        // in cases like MAP-EACH (one wants a continue to not add any value,
        // as opposed to a void) but may not make sense for all cases.
        //
        *broke = false;
        CATCH_THROWN(val, val);
        return true;
    }

    return false; // caller should let all other thrown values bubble up
}


//
//  break: native [
//
//  {Exit the current iteration of a loop and stop iterating further}
//
//  ]
//
DECLARE_NATIVE(break)
//
// BREAK is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name null :break`.
{
    INCLUDE_PARAMS_OF_BREAK;

    Copy_Cell(OUT, NAT_VALUE(break));
    CONVERT_NAME_TO_THROWN(OUT, NULLED_CELL);
    return BOUNCE_THROWN;
}


//
//  continue: native [
//
//  "Throws control back to top of loop for next iteration."
//
//      /with "Act as if loop body finished with this value"
//      value [any-value!]
//  ]
//
DECLARE_NATIVE(continue)
//
// CONTINUE is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name value :continue`.
{
    INCLUDE_PARAMS_OF_CONTINUE;

    if (not REF(with))  // it's an END (should change to CONTINUE/WITH)
        Init_Void(ARG(value));

    Copy_Cell(OUT, NAT_VALUE(continue));
    CONVERT_NAME_TO_THROWN(OUT, ARG(value)); // null if e.g. `do [continue]`

    return BOUNCE_THROWN;
}


//
//  Loop_Series_Common: C
//
static Bounce Loop_Series_Common(
    Value* out,
    Value* var, // Must not be movable from context expansion, see #2274
    const Value* body,
    Value* start,
    REBINT end,
    REBINT bump
){
    Init_Void(out); // result if body never runs

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
    REBLEN *state = &VAL_INDEX(var);

    // Run only once if start is equal to end...edge case.
    //
    REBINT s = VAL_INDEX(start);
    if (s == end) {
        if (Do_Branch_Throws(out, body)) {
            bool broke;
            if (not Catching_Break_Or_Continue(out, &broke))
                return BOUNCE_THROWN;
            if (broke)
                return nullptr;
        }
        return Nothingify_Branched(out);  // null->BREAK, void->empty
    }

    // As per #1993, start relative to end determines the "direction" of the
    // FOR loop.  (R3-Alpha used the sign of the bump, which meant it did not
    // have a clear plan for what to do with 0.)
    //
    const bool counting_up = (s < end); // equal checked above
    if ((counting_up and bump <= 0) or (not counting_up and bump >= 0))
        return out; // avoid infinite loops

    while (
        counting_up
            ? cast(REBINT, *state) <= end
            : cast(REBINT, *state) >= end
    ){
        if (Do_Branch_Throws(out, body)) {
            bool broke;
            if (not Catching_Break_Or_Continue(out, &broke))
                return BOUNCE_THROWN;
            if (broke)
                return nullptr;
        }
        Nothingify_Branched(out);  // null->BREAK, void->empty
        if (
            VAL_TYPE(var) != VAL_TYPE(start)
            or Cell_Flex(var) != Cell_Flex(start)
        ){
            fail ("Can only change series index, not series to iterate");
        }

        // Note that since the array is not locked with FLEX_INFO_HOLD, it
        // can be mutated during the loop body, so the end has to be refreshed
        // on each iteration.  Review ramifications of HOLD-ing it.
        //
        if (end >= cast(REBINT, VAL_LEN_HEAD(start)))
            end = cast(REBINT, VAL_LEN_HEAD(start));

        *state += bump;
    }

    return out;
}


//
//  Loop_Integer_Common: C
//
static Bounce Loop_Integer_Common(
    Value* out,
    Value* var, // Must not be movable from context expansion, see #2274
    const Value* body,
    REBI64 start,
    REBI64 end,
    REBI64 bump
){
    Init_Void(out); // result if body never runs

    // A value cell exposed to the user is used to hold the state.  This means
    // if they change `var` during the loop, it affects the iteration.  Hence
    // it must be checked for changing to a non-integer form.
    //
    RESET_CELL(var, REB_INTEGER);
    REBI64 *state = &VAL_INT64(var);
    *state = start;

    // Run only once if start is equal to end...edge case.
    //
    if (start == end) {
        if (Do_Branch_Throws(out, body)) {
            bool broke;
            if (not Catching_Break_Or_Continue(out, &broke))
                return BOUNCE_THROWN;
            if (broke)
                return nullptr;
        }
        return Nothingify_Branched(out);  // null->BREAK, void->empty
    }

    // As per #1993, start relative to end determines the "direction" of the
    // FOR loop.  (R3-Alpha used the sign of the bump, which meant it did not
    // have a clear plan for what to do with 0.)
    //
    const bool counting_up = (start < end); // equal checked above
    if ((counting_up and bump <= 0) or (not counting_up and bump >= 0))
        return nullptr; // avoid infinite loops

    while (counting_up ? *state <= end : *state >= end) {
        if (Do_Branch_Throws(out, body)) {
            bool broke;
            if (not Catching_Break_Or_Continue(out, &broke))
                return BOUNCE_THROWN;
            if (broke)
                return nullptr;
        }
        Nothingify_Branched(out);  // null->BREAK, void->empty

        if (not Is_Integer(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        if (REB_I64_ADD_OF(*state, bump, state))
            fail (Error_Overflow_Raw());
    }

    return out;
}


//
//  Loop_Number_Common: C
//
static Bounce Loop_Number_Common(
    Value* out,
    Value* var, // Must not be movable from context expansion, see #2274
    const Value* body,
    Value* start,
    Value* end,
    Value* bump
){
    Init_Void(out); // result if body never runs

    REBDEC s;
    if (Is_Integer(start))
        s = cast(REBDEC, VAL_INT64(start));
    else if (Is_Decimal(start) or Is_Percent(start))
        s = VAL_DECIMAL(start);
    else
        fail (Error_Invalid(start));

    REBDEC e;
    if (Is_Integer(end))
        e = cast(REBDEC, VAL_INT64(end));
    else if (Is_Decimal(end) or Is_Percent(end))
        e = VAL_DECIMAL(end);
    else
        fail (Error_Invalid(end));

    REBDEC b;
    if (Is_Integer(bump))
        b = cast(REBDEC, VAL_INT64(bump));
    else if (Is_Decimal(bump) or Is_Percent(bump))
        b = VAL_DECIMAL(bump);
    else
        fail (Error_Invalid(bump));

    // As in Loop_Integer_Common(), the state is actually in a cell; so each
    // loop iteration it must be checked to ensure it's still a decimal...
    //
    RESET_CELL(var, REB_DECIMAL);
    REBDEC *state = &VAL_DECIMAL(var);
    *state = s;

    // Run only once if start is equal to end...edge case.
    //
    if (s == e) {
        if (Do_Branch_Throws(out, body)) {
            bool broke;
            if (not Catching_Break_Or_Continue(out, &broke))
                return BOUNCE_THROWN;
            if (broke)
                return nullptr;
        }
        return Nothingify_Branched(out);  // null->BREAK, void->empty
    }

    // As per #1993, see notes in Loop_Integer_Common()
    //
    const bool counting_up = (s < e); // equal checked above
    if ((counting_up and b <= 0) or (not counting_up and b >= 0))
        return Init_Void(out); // avoid infinite loop, void means never ran

    while (counting_up ? *state <= e : *state >= e) {
        if (Do_Branch_Throws(out, body)) {
            bool broke;
            if (not Catching_Break_Or_Continue(out, &broke))
                return BOUNCE_THROWN;
            if (broke)
                return nullptr;
        }
        Nothingify_Branched(out);  // null->BREAK, void->empty

        if (not Is_Decimal(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        *state += b;
    }

    return out;
}


// Virtual_Bind_To_New_Context() allows ISSUE! syntax to reuse an existing
// variables binding:
//
//     x: 10
//     for-each #x [20 30 40] [...]
//     ;-- The 10 will be overwritten, and x will be equal to 40, here
//
// It accomplishes this by putting a word into the "variable" slot, and having
// a flag to indicate a dereference is necessary.
//
Value* Real_Var_From_Pseudo(Value* pseudo_var) {
    if (NOT_VAL_FLAG(pseudo_var, VAR_MARKED_REUSE))
        return pseudo_var;

    // Note: these variables are fetched across running arbitrary user code.
    // So the address cannot be cached...e.g. the object it lives in might
    // expand and invalidate the location.  (The `context` for fabricated
    // variables is locked at fixed size.)
    //
    assert(Is_Issue(pseudo_var));
    return Get_Mutable_Var_May_Fail(pseudo_var, SPECIFIED);
}


struct Loop_Each_State {
    Value* out; // where to write the output data (must be GC safe)
    const Value* body; // body to run on each loop iteration
    LOOP_MODE mode; // FOR-EACH, MAP-EACH, EVERY
    REBCTX *pseudo_vars_ctx; // vars made by Virtual_Bind_To_New_Context()
    Value* data; // the data argument passed in
    Flex* data_ser; // series data being enumerated (if applicable)
    REBLEN data_idx; // index into the data for filling current variable
    REBLEN data_len; // length of the data
};

// Isolation of central logic for FOR-EACH, MAP-EACH, and EVERY so that it
// can be rebRescue()'d in case of failure (to remove FLEX_INFO_HOLD, etc.)
//
// Returns nullptr or BOUNCE_THROWN, where the relevant result is in les->out.
// (That result may be Is_Nulled() if there was a break during the loop)
//
static Bounce Loop_Each_Core(struct Loop_Each_State *les) {

    bool more_data = true;
    bool broke = false;
    bool no_falseys = true; // not "all_truthy" because body *may* not run

    do {
        // Sub-loop: set variables.  This is a loop because blocks with
        // multiple variables are allowed, e.g.
        //
        //      >> for-each [a b] [1 2 3 4] [-- a b]]
        //      -- a: 1 b: 2
        //      -- a: 3 b: 4
        //
        // ANY-CONTEXT! and MAP! allow one var (keys) or two vars (keys/vals)
        //
        Value* pseudo_var = CTX_VAR(les->pseudo_vars_ctx, 1);
        for (; NOT_END(pseudo_var); ++pseudo_var) {
            Value* var = Real_Var_From_Pseudo(pseudo_var);

            // Even if data runs out, we could still have one last loop body
            // incarnation to run...with some variables unset.  Null those
            // variables here.
            //
            //     >> for-each [x y] [1] [-- x y]
            //     -- x: 1 y: // null
            //
            if (not more_data) {
                Init_Nulled(var);
                continue;
            }

            enum Reb_Kind kind = VAL_TYPE(les->data);
            switch (kind) {
              case REB_BLOCK:
              case REB_GROUP:
              case REB_PATH:
                Derelativize(
                    var,
                    Array_At(cast_Array(les->data_ser), les->data_idx),
                    VAL_SPECIFIER(les->data)
                );
                if (++les->data_idx == les->data_len)
                    more_data = false;
                break;

              case REB_DATATYPE:
                Derelativize(
                    var,
                    Array_At(cast_Array(les->data_ser), les->data_idx),
                    SPECIFIED // array generated via data stack, all specific
                );
                if (++les->data_idx == les->data_len)
                    more_data = false;
                break;

              case REB_OBJECT:
              case REB_ERROR:
              case REB_PORT:
              case REB_MODULE:
              case REB_FRAME: {
                Value* key;
                Value* val;
                REBLEN bind_index;
                while (true) { // find next non-hidden key (if any)
                    key = VAL_CONTEXT_KEY(les->data, les->data_idx);
                    val = VAL_CONTEXT_VAR(les->data, les->data_idx);
                    bind_index = les->data_idx;
                    if (++les->data_idx == les->data_len)
                        more_data = false;
                    if (not Is_Param_Hidden(key))
                        break;
                    if (not more_data)
                        goto finished;
                }

                Init_Any_Word_Bound( // key is typeset, user wants word
                    var,
                    REB_WORD,
                    Cell_Parameter_Symbol(key),
                    VAL_CONTEXT(les->data),
                    bind_index
                );

                if (CTX_LEN(les->pseudo_vars_ctx) == 1) {
                    //
                    // Only wanted the key (`for-each key obj [...]`)
                }
                else if (CTX_LEN(les->pseudo_vars_ctx) == 2) {
                    //
                    // Want keys and values (`for-each key val obj [...]`)
                    //
                    ++pseudo_var;
                    var = Real_Var_From_Pseudo(pseudo_var);
                    Copy_Cell(var, val);
                }
                else
                    fail ("Loop enumeration of contexts must be 1 or 2 vars");
                break; }

              case REB_MAP: {
                assert(les->data_idx % 2 == 0); // should be on key slot

                Value* key;
                Value* val;
                while (true) { // pass over the unused map slots
                    key = KNOWN(Array_At(cast_Array(les->data_ser), les->data_idx));
                    ++les->data_idx;
                    val = KNOWN(Array_At(cast_Array(les->data_ser), les->data_idx));
                    ++les->data_idx;
                    if (les->data_idx == les->data_len)
                        more_data = false;
                    if (not Is_Nulled(val))
                        break;
                    if (not more_data)
                        goto finished;
                } while (Is_Nulled(val));

                Copy_Cell(var, key);

                if (CTX_LEN(les->pseudo_vars_ctx) == 1) {
                    //
                    // Only wanted the key (`for-each key map [...]`)
                }
                else if (CTX_LEN(les->pseudo_vars_ctx) == 2) {
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

              case REB_BINARY:
                Init_Integer(
                    var,
                    Binary_Head(cast(Binary*, les->data_ser))[les->data_idx]
                );
                if (++les->data_idx == les->data_len)
                    more_data = false;
                break;

              case REB_TEXT:
              case REB_TAG:
              case REB_FILE:
              case REB_EMAIL:
              case REB_URL:
                Init_Char(var, GET_ANY_CHAR(les->data_ser, les->data_idx));
                if (++les->data_idx == les->data_len)
                    more_data = false;
                break;

              case REB_ACTION: {
                Value* generated = rebValue(rebEval(les->data));
                if (generated) {
                    Copy_Cell(var, generated);
                    rebRelease(generated);
                }
                else {
                    more_data = false; // any remaining vars must be unset
                    if (pseudo_var == CTX_VARS_HEAD(les->pseudo_vars_ctx)) {
                        //
                        // If we don't have at least *some* of the variables
                        // set for this body loop run, don't run the body.
                        //
                        goto finished;
                    }
                    Init_Nulled(var);
                }
                break; }

              default:
                panic ("Unsupported type");
            }
        }

        if (Do_Branch_Throws(les->out, les->body)) {
            if (not Catching_Break_Or_Continue(les->out, &broke))
                return BOUNCE_THROWN; // non-loop-related throw

            if (broke) {
                Init_Nulled(les->out);
                return nullptr;
            }
        }

        switch (les->mode) {
          case LOOP_FOR_EACH:
            Nothingify_Branched(les->out);  // null->BREAK, void->empty
            break;

          case LOOP_EVERY:
            no_falseys = no_falseys and (
                Is_Void(les->out) or IS_TRUTHY(les->out)
            );
            break;

          case LOOP_MAP_EACH:
            if (Is_Nulled(les->out))  // null body is error now
                fail (Error_Need_Non_Null_Raw());
            if (Is_Void(les->out))  // vanish result
                Init_Nothing(les->out);  // nulled is used to signal breaking only
            else
                Copy_Cell(PUSH(), les->out);  // not void, added to the result
            break;
        }
    } while (more_data and not broke);

  finished:;

    if (les->mode == LOOP_EVERY and not no_falseys)
        Init_Logic(les->out, false);

    // We use nullptr to signal the result is in out.  If we returned les->out
    // it would be subject to the rebRescue() rules, and the loop could not
    // return an ERROR! value normally.
    //
    return nullptr;
}


//
//  Loop_Each: C
//
// Common implementation code of FOR-EACH, MAP-EACH, and EVERY.
//
// !!! This routine has been slowly clarifying since R3-Alpha, and can
// likely be factored in a better way...pushing more per-native code into the
// natives themselves.
//
static Bounce Loop_Each(Level* level_, LOOP_MODE mode)
{
    INCLUDE_PARAMS_OF_FOR_EACH; // MAP-EACH & EVERY must have same interface

    Init_Void(OUT); // result if body never runs (MAP-EACH gives [])

    struct Loop_Each_State les;
    les.mode = mode;
    les.out = OUT;
    les.data = ARG(data);
    les.body = ARG(body);

    if (Is_Blank(les.data)) {
        if (mode == LOOP_MAP_EACH)
            return Init_Block(OUT, Make_Array(0));
        return OUT;
    }

    Virtual_Bind_Deep_To_New_Context(
        ARG(body), // may be updated, will still be GC safe
        &les.pseudo_vars_ctx,
        ARG(vars)
    );
    Init_Object(ARG(vars), les.pseudo_vars_ctx); // keep GC safe

    // Currently the data stack is only used by MAP-EACH to accumulate results
    // but it's faster to just save it than test the loop mode.
    //
    StackIndex base = TOP_INDEX;

    // Extract the series and index being enumerated, based on data type

    Bounce bounce;

    bool took_hold;
    if (Is_Action(les.data)) {
        //
        // The value is generated each time by calling the data action.
        // Assign values to avoid compiler warnings.
        //
        les.data_ser = nullptr;
        les.data_idx = 0;
        les.data_len = 0;
        took_hold = false;
    }
    else {
        if (Any_Series(les.data)) {
            les.data_ser = Cell_Flex(les.data);
            les.data_idx = VAL_INDEX(les.data);
        }
        else if (Any_Context(les.data)) {
            les.data_ser = CTX_VARLIST(VAL_CONTEXT(les.data));
            les.data_idx = 1;
        }
        else if (Is_Map(les.data)) {
            les.data_ser = Cell_Flex(les.data);
            les.data_idx = 0;
        }
        else if (Is_Datatype(les.data)) {
            //
            // !!! e.g. `for-each act action! [...]` enumerating the list of
            // all actions in the system.  This is not something that it's
            // safe to expose in a general sense (subverts hidden/protected
            // information) but it's an experiment for helping with stats and
            // debugging...as well as showing a case where the enumerated
            // data has to be snapshotted and freed.
            //
            switch (VAL_TYPE_KIND(les.data)) {
              case REB_ACTION:
                les.data_ser = Snapshot_All_Actions();
                assert(Not_Node_Managed(les.data_ser));
                les.data_idx = 0;
                break;

              default:
                fail ("ACTION! is the only type with global enumeration");
            }
        }
        else
            panic ("Illegal type passed to Loop_Each()");

        took_hold = Not_Flex_Info(les.data_ser, HOLD);
        if (took_hold)
            Set_Flex_Info(les.data_ser, HOLD);

        les.data_len = Flex_Len(les.data_ser); // HOLD so length can't change
        if (les.data_idx >= les.data_len) {
            assert(Is_Void(OUT));  // result if loop body never runs
            bounce = nullptr;
            goto cleanup;
        }
    }

    // If there is a fail() and we took a FLEX_INFO_HOLD, that hold needs
    // to be released.  For this reason, the code has to trap errors.

    bounce = rebRescue(cast(REBDNG*, &Loop_Each_Core), &les);

    //=//// CLEANUPS THAT NEED TO BE DONE DESPITE ERROR, THROW, ETC. //////=//

  cleanup:;

    if (took_hold) // release read-only lock
        Clear_Flex_Info(les.data_ser, HOLD);

    if (Is_Datatype(les.data))
        Free_Unmanaged_Flex(cast_Array(les.data_ser)); // temp array of instances

    //=//// NOW FINISH UP /////////////////////////////////////////////////=//

    if (bounce == BOUNCE_THROWN) {  // THROW/RETURN/QUIT (not CONTINUE/BREAK)
        if (mode == LOOP_MAP_EACH)
            Drop_Data_Stack_To(base);
        return BOUNCE_THROWN;
    }

    if (bounce) {
        assert(Is_Error(bounce));
        if (mode == LOOP_MAP_EACH)
            Drop_Data_Stack_To(base);
        rebJumps ("FAIL", rebR(bounce));
    }

    // Otherwise, nullptr signals result in les.out (a.k.a. OUT)

    switch (mode) {
      case LOOP_FOR_EACH:
        //
        // nulled output means there was a BREAK
        // blank output means loop body never ran
        // void means the last body evaluation returned null or blank
        // any other value is the plain last body result
        //
        return OUT;

      case LOOP_EVERY:
        //
        // nulled output means there was a BREAK
        // void means body never ran (`void? every x [] [<unused>]`)
        // #[false] means loop ran, and at least one body result was "falsey"
        // any other value is the last body result, and is truthy
        // only illegal value here is trash (would cause error if body gave it)
        //
        assert(not Is_Nothing(OUT));
        return OUT;

      case LOOP_MAP_EACH:
        if (Is_Nulled(OUT)) { // e.g. there was a BREAK...*must* return null
            Drop_Data_Stack_To(base);
            return nullptr;
        }

        // !!! MAP-EACH always returns a block except in cases of BREAK, but
        // paralleling some changes to COLLECT, it may be better if the body
        // never runs it returns blank (?)
        //
        return Init_Block(OUT, Pop_Stack_Values(base));
    }

    DEAD_END; // all branches handled in enum switch
}


//
//  for: native [
//
//  {Evaluate a block over a range of values. (See also: REPEAT)}
//
//      return: [~null~ any-value!]
//      'word [word! lit-word! refinement!]
//          "Variable to hold current value"
//      start [any-series! any-number!]
//          "Starting value"
//      end [any-series! any-number!]
//          "Ending value"
//      bump [any-number!]
//          "Amount to skip each time"
//      body [block! action!]
//          "Code to evaluate"
//  ]
//
DECLARE_NATIVE(for)
{
    INCLUDE_PARAMS_OF_FOR;

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body), // may be updated, will still be GC safe
        &context,
        ARG(word)
    );
    Init_Object(ARG(word), context); // keep GC safe

    Value* var = CTX_VAR(context, 1); // not movable, see #2274

    if (
        Is_Integer(ARG(start))
        and Is_Integer(ARG(end))
        and Is_Integer(ARG(bump))
    ){
        return Loop_Integer_Common(
            OUT,
            var,
            ARG(body),
            VAL_INT64(ARG(start)),
            Is_Decimal(ARG(end))
                ? cast(REBI64, VAL_DECIMAL(ARG(end)))
                : VAL_INT64(ARG(end)),
            VAL_INT64(ARG(bump))
        );
    }

    if (Any_Series(ARG(start))) {
        if (Any_Series(ARG(end))) {
            return Loop_Series_Common(
                OUT,
                var,
                ARG(body),
                ARG(start),
                VAL_INDEX(ARG(end)),
                Int32(ARG(bump))
            );
        }
        else {
            return Loop_Series_Common(
                OUT,
                var,
                ARG(body),
                ARG(start),
                Int32s(ARG(end), 1) - 1,
                Int32(ARG(bump))
            );
        }
    }

    return Loop_Number_Common(
        OUT, var, ARG(body), ARG(start), ARG(end), ARG(bump)
    );
}


//
//  for-skip: native [
//
//  "Evaluates a block for periodic values in a series"
//
//      return: "Last body result, or null if BREAK"
//          [~null~ ~void~ any-value!]
//      'word "Variable set to each position in the series at skip distance"
//          [word! lit-word! refinement! issue! blank!]
//      series "The series to iterate over"
//          [<maybe> blank! any-series!]
//      skip "Number of positions to skip each time"
//          [<maybe> integer!]
//      body "Code to evaluate each time"
//          [block! action!]
//  ]
//
DECLARE_NATIVE(for_skip)
{
    INCLUDE_PARAMS_OF_FOR_SKIP;

    Value* series = ARG(series);

    Init_Void(OUT);  // result if body never runs, like `while [null] [...]`

    if (Is_Blank(series))
        return OUT;

    REBINT skip = Int32(ARG(skip));
    if (skip == 0) {
        //
        // !!! https://forum.rebol.info/t/infinite-loops-vs-errors/936
        //
        return OUT;  // void is loop protocol if body never ran
    }

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body), // may be updated, will still be GC safe
        &context,
        ARG(word)
    );
    Init_Object(ARG(word), context); // keep GC safe

    Value* pseudo_var = CTX_VAR(context, 1); // not movable, see #2274
    Value* var = Real_Var_From_Pseudo(pseudo_var);
    Copy_Cell(var, series);

    // Starting location when past end with negative skip:
    //
    if (skip < 0 and VAL_INDEX(var) >= VAL_LEN_HEAD(var))
        VAL_INDEX(var) = VAL_LEN_HEAD(var) + skip;

    while (true) {
        REBINT len = VAL_LEN_HEAD(var); // VAL_LEN_HEAD() always >= 0
        REBINT index = VAL_INDEX(var); // (may have been set to < 0 below)

        if (index < 0)
            break;
        if (index >= len) {
            if (skip >= 0)
                break;
            index = len + skip; // negative
            if (index < 0)
                break;
            VAL_INDEX(var) = index;
        }

        if (Do_Branch_Throws(OUT, ARG(body))) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return BOUNCE_THROWN;
            if (broke)
                return nullptr;
        }
        Nothingify_Branched(OUT);  // null->BREAK, blank->empty

        // Modifications to var are allowed, to another ANY-SERIES! value.
        //
        // If `var` is movable (e.g. specified via ISSUE!) it must be
        // refreshed each time arbitrary code runs, since the context may
        // expand and move the address, may get PROTECTed, etc.
        //
        var = Real_Var_From_Pseudo(pseudo_var);

        if (Is_Nulled(var))
            fail (Error_No_Value(ARG(word)));
        if (not Any_Series(var))
            fail (Error_Invalid(var));

        VAL_INDEX(var) += skip;
    }

    return OUT;
}


//
//  stop: native [
//
//  {End the current iteration of CYCLE and return a value (nulls allowed)}
//
//      value "If no argument is provided, assume trash"
//          [~null~ <end> any-value!]
//  ]
//
DECLARE_NATIVE(stop)
//
// Most loops are not allowed to explicitly return a value and stop looping,
// because that would make it impossible to tell from the outside whether
// they'd requested a stop or if they'd naturally completed.  It would be
// impossible to propagate a value-bearing break-like request to an aggregate
// looping construct without invasively rebinding the break.
//
// CYCLE is different because it doesn't have any loop exit condition.  Hence
// it responds to a STOP request, which lets it return any value.
//
// Coupled with the unusualness of CYCLE, NULL is allowed to come from a STOP
// request because it is given explicitly.  STOP NULL thus seems identical
// to the outside to a BREAK.
{
    INCLUDE_PARAMS_OF_STOP;

    Value* v = ARG(value);

    Copy_Cell(OUT, NAT_VALUE(stop));
    if (Is_Endish_Nulled(v))
        CONVERT_NAME_TO_THROWN(OUT, NOTHING_VALUE); // `if true [stop]`
    else
        CONVERT_NAME_TO_THROWN(OUT, v); // `if true [stop ...]`

    return BOUNCE_THROWN;
}


//
//  cycle: native [
//
//  "Evaluates a block endlessly, until a BREAK or a STOP is hit"
//
//      return: [~null~ any-value!]
//          {Null if BREAK, or non-null value passed to STOP}
//      body [block! action!]
//          "Block or action to evaluate each time"
//  ]
//
DECLARE_NATIVE(cycle)
{
    INCLUDE_PARAMS_OF_CYCLE;

    do {
        if (Do_Branch_Throws(OUT, ARG(body))) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke)) {
                if (
                    Is_Action(OUT)
                    and VAL_ACT_DISPATCHER(OUT) == &N_stop
                ){
                    // See notes on STOP for why CYCLE is unique among loop
                    // constructs, with a BREAK variant that returns a value.
                    //
                    CATCH_THROWN(OUT, OUT);
                    return OUT; // special case: null allowed (like break)
                }

                return BOUNCE_THROWN;
            }
            if (broke)
                return nullptr;
        }
        // No need to trashify result, it doesn't escape...
    } while (true);

    DEAD_END;
}


//
//  for-each: native [
//
//  "Evaluates a block for each value(s) in a series."
//
//      return: [~null~ ~void~ any-value!]
//          {Last body result, or null if BREAK}
//      'vars [word! lit-word! refinement! issue! block!]
//          "Word or block of words to set each time, no new var if LIT-WORD!"
//      data [<maybe> blank! any-series! any-context! map! datatype! action!]
//          "The series to traverse"
//      body [block! action!]
//          "Block to evaluate each time"
//  ]
//
DECLARE_NATIVE(for_each)
{
    return Loop_Each(level_, LOOP_FOR_EACH);
}


//
//  every: native [
//
//  {Iterate and return false if any previous body evaluations were false}
//
//      return: [~null~ ~void~ any-value!]
//          {null on BREAK, blank on empty, false or the last truthy value}
//      'vars [word! lit-word! refinement! issue! block!]
//          "Word or block of words to set each time (local)"
//      data [<maybe> blank! any-series! any-context! map! datatype! action!]
//          "The series to traverse"
//      body [block! action!]
//          "Block to evaluate each time"
//  ]
//
DECLARE_NATIVE(every)
{
    return Loop_Each(level_, LOOP_EVERY);
}


// For important reasons of semantics and performance, the REMOVE-EACH native
// does not actually perform removals "as it goes".  It could run afoul of
// any number of problems, including the mutable series becoming locked during
// the iteration.  Hence the iterated series is locked, and the removals are
// applied all at once atomically.
//
// However, this means that there's state which must be finalized on every
// possible exit path...be that BREAK, THROW, FAIL, or just ordinary finishing
// of the loop.  That finalization is done by this routine, which will clean
// up the state and remove any indicated items.  (It is assumed that all
// forms of exit, including raising an error, would like to apply any
// removals indicated thus far.)
//
// Because it's necessary to intercept, finalize, and then re-throw any
// fail() exceptions, rebRescue() must be used with a state structure.
//
struct Remove_Each_State {
    Value* out;
    Value* data;
    Flex* series;
    bool broke; // e.g. a BREAK ran
    const Value* body;
    REBCTX *context;
    REBLEN start;
    REB_MOLD *mo;
};


// See notes on Remove_Each_State
//
INLINE REBLEN Finalize_Remove_Each(struct Remove_Each_State *res)
{
    assert(Get_Flex_Info(res->series, HOLD));
    Clear_Flex_Info(res->series, HOLD);

    // If there was a BREAK, we return NULL to indicate that as part of
    // the loop protocol.  This prevents giving back a return value of
    // how many removals there were, so we don't do the removals.

    REBLEN count = 0;
    if (Any_List(res->data)) {
        if (res->broke) { // cleanup markers, don't do removals
            Cell* temp = Cell_List_At(res->data);
            for (; NOT_END(temp); ++temp) {
                if (GET_VAL_FLAG(temp, NODE_FLAG_MARKED))
                    CLEAR_VAL_FLAG(temp, NODE_FLAG_MARKED);
            }
            return 0;
        }

        REBLEN len = VAL_LEN_HEAD(res->data);

        Cell* dest = Cell_List_At(res->data);
        Cell* src = dest;

        // avoid blitting cells onto themselves by making the first thing we
        // do is to pass up all the unmarked (kept) cells.
        //
        while (NOT_END(src) and not (src->header.bits & NODE_FLAG_MARKED)) {
            ++src;
            ++dest;
        }

        // If we get here, we're either at the end, or all the cells from here
        // on are going to be moving to somewhere besides the original spot
        //
        for (; NOT_END(dest); ++dest, ++src) {
            while (NOT_END(src) and (src->header.bits & NODE_FLAG_MARKED)) {
                ++src;
                --len;
                ++count;
            }
            if (IS_END(src)) {
                Term_Array_Len(Cell_Array(res->data), len);
                return count;
            }
            Blit_Cell(dest, src); // same array--rare place we can do this
        }

        // If we get here, there were no removals, and length is unchanged.
        //
        assert(count == 0);
        assert(len == VAL_LEN_HEAD(res->data));
    }
    else if (Is_Binary(res->data)) {
        if (res->broke) { // leave data unchanged
            Drop_Mold(res->mo);
            return 0;
        }

        // If there was a THROW, or fail() we need the remaining data
        //
        REBLEN orig_len = VAL_LEN_HEAD(res->data);
        assert(res->start <= orig_len);
        Append_Unencoded_Len(
            res->mo->series,
            cs_cast(Binary_At(cast(Binary*, res->series), res->start)),
            orig_len - res->start
        );

        // !!! We are reusing the mold buffer, but *not putting UTF-8 data*
        // into it.  Revisit if this inhibits cool UTF-8 based tricks the
        // mold buffer might do otherwise.
        //
        Flex* popped = Pop_Molded_Binary(res->mo);

        assert(Flex_Len(popped) <= VAL_LEN_HEAD(res->data));
        count = VAL_LEN_HEAD(res->data) - Flex_Len(popped);

        // We want to swap out the data properties of the series, so the
        // identity of the incoming series is kept but now with different
        // underlying data.
        //
        Swap_Flex_Content(popped, Cell_Flex(res->data));

        Free_Unmanaged_Flex(popped); // now frees incoming series's data
    }
    else {
        assert(Any_String(res->data));
        if (res->broke) { // leave data unchanged
            Drop_Mold(res->mo);
            return 0;
        }

        // If there was a BREAK, THROW, or fail() we need the remaining data
        //
        REBLEN orig_len = VAL_LEN_HEAD(res->data);
        assert(res->start <= orig_len);

        for (; res->start != orig_len; ++res->start) {
            Append_Utf8_Codepoint(
                res->mo->series,
                GET_ANY_CHAR(res->series, res->start)
            );
        }

        Flex* popped = Pop_Molded_String(res->mo);

        assert(Flex_Len(popped) <= VAL_LEN_HEAD(res->data));
        count = VAL_LEN_HEAD(res->data) - Flex_Len(popped);

        // We want to swap out the data properties of the series, so the
        // identity of the incoming series is kept but now with different
        // underlying data.
        //
        Swap_Flex_Content(popped, Cell_Flex(res->data));

        Free_Unmanaged_Flex(popped); // now frees incoming series's data
    }

    return count;
}


// See notes on Remove_Each_State
//
static Bounce Remove_Each_Core(struct Remove_Each_State *res)
{
    // Set a bit saying we are iterating the series, which will disallow
    // mutations (including a nested REMOVE-EACH) until completion or failure.
    // This flag will be cleaned up by Finalize_Remove_Each(), which is run
    // even if there is a fail().
    //
    Set_Flex_Info(res->series, HOLD);

    REBLEN index = res->start; // declare here, avoid longjmp clobber warnings

    REBLEN len = Flex_Len(res->series); // temp read-only, this won't change
    while (index < len) {
        assert(res->start == index);

        Value* var = CTX_VAR(res->context, 1); // not movable, see #2274
        for (; NOT_END(var); ++var) {
            if (index == len) {
                //
                // The second iteration here needs x = #"c" and y as void.
                //
                //     data: copy "abc"
                //     remove-each [x y] data [...]
                //
                Init_Nulled(var);
                continue; // the `for` loop setting variables
            }

            if (Any_List(res->data))
                Derelativize(
                    var,
                    Cell_List_At_Head(res->data, index),
                    VAL_SPECIFIER(res->data)
                );
            else if (Is_Binary(res->data))
                Init_Integer(
                    var,
                    cast(REBI64, Flex_Head(Byte, res->series)[index])
                );
            else {
                assert(Any_String(res->data));
                Init_Char(var, GET_ANY_CHAR(res->series, index));
            }
            ++index;
        }

        if (Do_Branch_Throws(res->out, res->body)) {
            if (not Catching_Break_Or_Continue(res->out, &res->broke))
                return BOUNCE_THROWN;  // bubble it up, but we'll also finalize

            if (res->broke) {
                //
                // BREAK; this means we will return nullptr and not run any
                // removals (we couldn't report how many if we did)
                //
                assert(res->start < len);
                return nullptr;
            }
            else {
                // CONTINUE - res->out may not be void if /WITH refinement used
            }
        }

        if (Any_List(res->data)) {
            if (
                Is_Nulled(res->out)
                or Is_Void(res->out)
                or IS_FALSEY(res->out)
            ){
                res->start = index;
                continue; // keep requested, don't mark for culling
            }

            do {
                assert(res->start <= len);
                Cell_List_At_Head(res->data, res->start)->header.bits
                    |= NODE_FLAG_MARKED;
                ++res->start;
            } while (res->start != index);
        }
        else {
            if (
                not Is_Nulled(res->out)
                and not Is_Void(res->out)
                and IS_TRUTHY(res->out)
            ){
                res->start = index;
                continue; // remove requested, don't save to buffer
            }

            do {
                assert(res->start <= len);
                if (Is_Binary(res->data)) {
                    Append_Unencoded_Len(
                        res->mo->series,
                        cs_cast(
                            Binary_At(cast(Binary*, res->series), res->start)
                        ),
                        1
                    );
                }
                else {
                    Append_Utf8_Codepoint(
                        res->mo->series, GET_ANY_CHAR(res->series, res->start)
                    );
                }
                ++res->start;
            } while (res->start != index);
        }
    }

    // We get here on normal completion
    // THROW and BREAK will return above

    assert(not res->broke and res->start == len);

    return nullptr;
}


//
//  remove-each: native [
//
//  {Removes values for each block that returns true.}
//
//      return: [~null~ integer!]
//          {Number of removed series items, or null if BREAK}
//      'vars [word! lit-word! refinement! issue! block!]
//          "Word or block of words to set each time (local)"
//      data [<maybe> blank! any-series!]
//          "The series to traverse (modified)" ; should BLANK! opt-out?
//      body [block! action!]
//          "Block to evaluate (return TRUE to remove)"
//  ]
//
DECLARE_NATIVE(remove_each)
{
    INCLUDE_PARAMS_OF_REMOVE_EACH;

    struct Remove_Each_State res;
    res.data = ARG(data);

    if (Is_Blank(res.data))
        return Init_Integer(OUT, 0);

    if (not (
        Any_List(res.data) or Any_String(res.data) or Is_Binary(res.data)
    )){
        fail (Error_Invalid(res.data));
    }

    // Check the series for whether it is read only, in which case we should
    // not be running a REMOVE-EACH on it.  This check for permissions applies
    // even if the REMOVE-EACH turns out to be a no-op.
    //
    res.series = Cell_Flex(res.data);
    Fail_If_Read_Only_Flex(res.series);

    if (VAL_INDEX(res.data) >= Flex_Len(res.series)) {
        //
        // If index is past the series end, then there's nothing removable.
        //
        // !!! Should REMOVE-EACH follow the "loop conventions" where if the
        // body never gets a chance to run, the return value is nothing?
        //
        return Init_Integer(OUT, 0);
    }

    // Create a context for the loop variables, and bind the body to it.
    // Do this before PUSH_TRAP, so that if there is any failure related to
    // memory or a poorly formed ARG(vars) that it doesn't try to finalize
    // the REMOVE-EACH, as `res` is not ready yet.
    //
    Virtual_Bind_Deep_To_New_Context(
        ARG(body), // may be updated, will still be GC safe
        &res.context,
        ARG(vars)
    );
    Init_Object(ARG(vars), res.context); // keep GC safe
    res.body = ARG(body);

    res.start = VAL_INDEX(res.data);

    REB_MOLD mold_struct;
    if (Any_List(res.data)) {
        //
        // We're going to use NODE_FLAG_MARKED on the elements of data's
        // array for those items we wish to remove later.
        //
        // !!! This may not be better than pushing kept values to the data
        // stack and then creating a precisely-sized output binary to swap as
        // the underlying memory for the array.  (Imagine a large array from
        // which there are many removals, and the ensuing wasted space being
        // left behind).  But worth testing the technique of marking in case
        // it's ever required for other scenarios.
        //
        Corrupt_Pointer_If_Debug(res.mo);
    }
    else {
        // We're going to generate a new data allocation, but then swap its
        // underlying content to back the series we were given.  (See notes
        // above on how this might be the better way to deal with arrays too.)
        //
        // !!! Uses the mold buffer even for binaries, and since we know
        // we're never going to be pushing a value bigger than 0xFF it will
        // not require a wide string.  So the series we pull off should be
        // byte-sized.  In a sense this is wasteful and there should be a
        // byte-buffer-backed parallel to mold, but the logic for nesting mold
        // stacks already exists and the mold buffer is "hot", so it's not
        // necessarily *that* wasteful in the scheme of things.
        //
        CLEARS(&mold_struct);
        res.mo = &mold_struct;
        Push_Mold(res.mo);
    }

    SET_END(OUT); // will be tested for THROWN() to signal a throw happened
    res.out = OUT;

    res.broke = false; // will be set to true if there is a BREAK

    Bounce bounce = rebRescue(cast(REBDNG*, &Remove_Each_Core), &res);

    // Currently, if a fail() happens during the iteration, any removals
    // which were indicated will be enacted before propagating failure.
    //
    REBLEN removals = Finalize_Remove_Each(&res);

    if (bounce == BOUNCE_THROWN)
        return BOUNCE_THROWN;

    if (bounce) {
        assert(Is_Error(bounce));
        rebJumps("FAIL", rebR(bounce));
    }

    if (res.broke)
        return nullptr;

    return Init_Integer(OUT, removals);
}


//
//  map-each: native [
//
//  {Evaluate a block for each value(s) in a series and collect as a block.}
//
//      return: [~null~ ~void~ block!]
//          {Collected block (BREAK/WITH can add a final result to block)}
//      'vars [word! lit-word! refinement! issue! block!]
//          "Word or block of words to set each time (local)"
//      data [<maybe> blank! any-series! action!]
//          "The series to traverse"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
DECLARE_NATIVE(map_each)
{
    return Loop_Each(level_, LOOP_MAP_EACH);
}


//
//  repeat: native [
//
//  "Evaluates a block a specified number of times."
//
//      return: [~null~ any-value!]
//          {Last body result, or null if BREAK}
//      count [<maybe> any-number! logic!]
//          "Repetitions (true loops infinitely, false doesn't run)"
//      body [block! action!]
//          "Block to evaluate or action to run."
//  ]
//
DECLARE_NATIVE(repeat)
{
    INCLUDE_PARAMS_OF_REPEAT;

    Init_Void(OUT);  // result if body never runs, like `while [null] [...]`

    if (IS_FALSEY(ARG(count))) {
        assert(Is_Logic(ARG(count))); // is false...opposite of infinite loop
        return OUT;
    }

    REBI64 count;

    if (Is_Logic(ARG(count))) {
        assert(VAL_LOGIC(ARG(count)) == true);

        // Run forever, and as a micro-optimization don't handle specially
        // in the loop, just seed with a very large integer.  In the off
        // chance that is exhaust it, jump here to re-seed and loop again.
    restart:
        count = INT64_MAX;
    }
    else
        count = Int64(ARG(count));

    for (; count > 0; count--) {
        if (Do_Branch_Throws(OUT, ARG(body))) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return BOUNCE_THROWN;
            if (broke)
                return nullptr;
        }
        Nothingify_Branched(OUT);  // null->BREAK, blank->empty
    }

    if (Is_Logic(ARG(count)))
        goto restart; // "infinite" loop exhausted MAX_I64 steps (rare case)

    return OUT;
}


//
//  for-next: native [
//
//  {Evaluates a block over a series.}
//
//      return: [~null~ any-value!]
//          {Last body result or BREAK value}
//      'word [word! lit-word! refinement!]
//          "Word to set each time"
//      value [<maybe> any-number! any-series!]
//          "Maximum number or series to traverse"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
DECLARE_NATIVE(for_next)
{
    INCLUDE_PARAMS_OF_FOR_NEXT;

    Value* value = ARG(value);

    if (Is_Decimal(value) or Is_Percent(value))
        Init_Integer(value, Int64(value));

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body),
        &context,
        ARG(word)
    );
    Init_Object(ARG(word), context); // keep GC safe

    assert(CTX_LEN(context) == 1);

    Value* var = CTX_VAR(context, 1); // not movable, see #2274
    return Loop_Series_Common(
        OUT, var, ARG(body), value, VAL_LEN_HEAD(value) - 1, 1
    );
}


// Common code for UNTIL & UNTIL-NOT (same frame param layout)
//
INLINE Bounce Until_Core(
    Level* level_,
    bool trigger // body keeps running so until evaluation matches this
){
    INCLUDE_PARAMS_OF_UNTIL;

    do {

    skip_check:;

        if (Do_Branch_Throws(OUT, ARG(body))) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return BOUNCE_THROWN;
            if (broke)
                return Init_Nulled(OUT);

            if (Is_Void(OUT))  // e.g. CONTINUE and no /WITH
                goto skip_check;
        }

        if (not Is_Void(OUT) and IS_TRUTHY(OUT) == trigger)
            return OUT;

    } while (true);
}


//
//  until: native [
//
//  "Evaluates the body until it evaluates to a conditionally true value"
//
//      return: [~null~ any-value!]
//          {Last body result or BREAK value.}
//      body [block! action!]
//  ]
//
DECLARE_NATIVE(until)
{
    return Until_Core(level_, true); // run loop until result IS_TRUTHY()
}


//
//  until-not: native [
//
//  "Evaluates the body until it evaluates to a conditionally false value"
//
//      return: [~null~ any-value!]
//          {Last body result or BREAK value.}
//      body [block! action!]
//  ]
//
DECLARE_NATIVE(until_not)
//
// Faster than running NOT, and doesn't need groups for `until [...not (x =`
{
    return Until_Core(level_, false); // run loop until result IS_FALSEY()
}


// Common code for WHILE & WHILE-NOT
//
INLINE Bounce While_Core(
    Level* level_,
    bool trigger // body keeps running so long as condition matches
){
    INCLUDE_PARAMS_OF_WHILE;

    DECLARE_VALUE (cell); // unsafe to use ARG() slots as frame output cells
    SET_END(cell);
    Push_GC_Guard(cell);

    Init_Void(OUT);  // result if body never runs

    do {
        if (Do_Branch_Throws(cell, ARG(condition))) {
            Copy_Cell(OUT, cell);
            Drop_GC_Guard(cell);
            return BOUNCE_THROWN; // don't see BREAK/CONTINUE in the *condition*
        }

        if (IS_TRUTHY(cell) != trigger) {
            Drop_GC_Guard(cell);
            return OUT; // trigger didn't match, return last body result
        }

        if (Do_Branch_With_Throws(OUT, ARG(body), cell)) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke)) {
                Drop_GC_Guard(cell);
                return BOUNCE_THROWN;
            }
            if (broke) {
                Drop_GC_Guard(cell);
                return Init_Nulled(OUT);
            }
        }
        Nothingify_Branched(OUT);  // null->BREAK, blank->empty
    } while (true);

    DEAD_END;
}


//
//  while: native [
//
//  {While a condition is conditionally true, evaluates the body.}
//
//      return: [~null~ any-value!]
//          "Last body result, or null if BREAK"
//      condition [block! action!]
//      body [block! action!]
//  ]
//
DECLARE_NATIVE(while)
{
    return While_Core(level_, true); // run loop while condition IS_TRUTHY()
}


//
//  while-not: native [
//
//  {While a condition is conditionally false, evaluate the body.}
//
//      return: [~null~ any-value!]
//          "Last body result, or null if BREAK"
//      condition [block! action!]
//      body [block! action!]
//  ]
//
DECLARE_NATIVE(while_not)
//
// Faster than running NOT, and doesn't need groups for `while [not (x =`
{
    return While_Core(level_, false); // run loop while condition IS_FALSEY()
}
