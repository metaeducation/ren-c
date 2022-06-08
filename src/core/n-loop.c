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
bool Catching_Break_Or_Continue(REBVAL *val, bool *broke)
{
    const REBVAL *label = VAL_THROWN_LABEL(val);

    // Throw /NAME-s used by CONTINUE and BREAK are the actual native
    // function values of the routines themselves.
    //
    if (not IS_ACTION(label))
        return false;

    if (ACT_DISPATCHER(VAL_ACTION(label)) == &N_break) {
        *broke = true;
        CATCH_THROWN_META(val, val);
        assert(IS_NULLED(val)); // BREAK must always return NULL
        return true;
    }

    if (ACT_DISPATCHER(VAL_ACTION(label)) == &N_continue) {
        //
        // !!! Continue with no argument acts the same as asking
        // for CONTINUE void (the form with an argument).  This makes sense
        // in cases like MAP-EACH (one wants a continue to not add any value,
        // as opposed to some ~void~ BAD-WORD!)  Other loops may vary.
        //
        *broke = false;
        CATCH_THROWN_META(val, val);
        if (Is_Meta_Of_Void(val)) {
            //
            // For instance, `continue if false [<a>]`.  The rules here may
            // be different...a MAP-EACH might want to tolerate it, but a
            // REMOVE-EACH might not.
            //
            RESET(val);
        }
        else
            Meta_Unquotify(val);

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
REBNATIVE(break)
//
// BREAK is implemented via a thrown signal that bubbles up through the stack.
// It uses the value of its own native function as the name of the throw,
// like `throw/name null :break`.
{
    INCLUDE_PARAMS_OF_BREAK;

    return_thrown (Init_Thrown_With_Label(OUT, Lib(NULL), Lib(BREAK)));
}


//
//  continue: native [
//
//  "Throws control back to top of loop for next iteration."
//
//      return: []  ; !!! notation for divergent function?
//      ^value "If provided, act as if loop body finished with this value"
//          [<end> <opt> any-value!]
//  ]
//
REBNATIVE(continue)
//
// CONTINUE is implemented via a thrown signal that bubbles up through the
// stack.  It uses the value of its own native function as the name of the
// throw, like `throw/name value :continue`.
{
    INCLUDE_PARAMS_OF_CONTINUE;

    REBVAL *v = ARG(value);

    if (Is_Meta_Of_End(v))
        Init_Meta_Of_Void(v);  // Treat CONTINUE same as CONTINUE VOID

    return_thrown (Init_Thrown_With_Label_Meta(OUT, v, Lib(CONTINUE)));
}


//
//  Loop_Series_Common: C
//
static REB_R Loop_Series_Common(
    REBFRM *frame_,
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
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return R_THROWN;
            if (broke)
                return nullptr;  // BREAK -> NULL

            if (Is_Void(OUT))  // CONTINUE w/no argument
                Init_None(OUT);
        }
        return OUT;  // guaranteed not stale
    }

    // As per #1993, start relative to end determines the "direction" of the
    // FOR loop.  (R3-Alpha used the sign of the bump, which meant it did not
    // have a clear plan for what to do with 0.)
    //
    const bool counting_up = (s < end); // equal checked above
    if ((counting_up and bump <= 0) or (not counting_up and bump >= 0))
        return_void (OUT);  // avoid infinite loops

    while (
        counting_up
            ? cast(REBINT, *state) <= end
            : cast(REBINT, *state) >= end
    ){
        if (Do_Branch_Throws(OUT, body)) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return R_THROWN;
            if (broke)
                return nullptr;

            if (Is_Void(OUT))  // CONTINUE w/no argument
                Init_None(OUT);
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

    if (Is_Stale(OUT))
        return_void (OUT);

    return_branched (OUT);
}


//
//  Loop_Integer_Common: C
//
static REB_R Loop_Integer_Common(
    REBFRM *frame_,
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
    Reset_Cell_Header_Untracked(TRACK(var), REB_INTEGER, CELL_MASK_NONE);
    REBI64 *state = &VAL_INT64(var);
    *state = start;

    // Run only once if start is equal to end...edge case.
    //
    if (start == end) {
        if (Do_Branch_Throws(OUT, body)) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return R_THROWN;
            if (broke)
                return nullptr;

            if (Is_Void(OUT))  // CONTINUE w/no argument
                Init_None(OUT);
        }
        return_branched (OUT);  // BREAK -> NULL
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
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return R_THROWN;
            if (broke)
                return nullptr;

            if (Is_Void(OUT))  // CONTINUE w/no argument
                Init_None(OUT);
        }

        if (not IS_INTEGER(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        if (REB_I64_ADD_OF(*state, bump, state))
            fail (Error_Overflow_Raw());
    }

    return_branched (OUT);
}


//
//  Loop_Number_Common: C
//
static REB_R Loop_Number_Common(
    REBFRM *frame_,
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
    Reset_Cell_Header_Untracked(TRACK(var), REB_DECIMAL, CELL_MASK_NONE);
    REBDEC *state = &VAL_DECIMAL(var);
    *state = s;

    // Run only once if start is equal to end...edge case.
    //
    if (s == e) {
        if (Do_Branch_Throws(OUT, body)) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return R_THROWN;
            if (broke)
                return nullptr;

            if (Is_Void(OUT))  // CONTINUE w/no argument
                Init_None(OUT);
        }
        return_branched (OUT);  // BREAK -> NULL
    }

    // As per #1993, see notes in Loop_Integer_Common()
    //
    const bool counting_up = (s < e); // equal checked above
    if ((counting_up and b <= 0) or (not counting_up and b >= 0))
        return_void (OUT);  // avoid inf. loop, means never ran

    while (counting_up ? *state <= e : *state >= e) {
        if (Do_Branch_Throws(OUT, body)) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return R_THROWN;
            if (broke)
                return nullptr;

            if (Is_Void(OUT))  // CONTINUE w/no argument
                Init_None(OUT);
        }

        if (not IS_DECIMAL(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        *state += b;
    }

    if (Is_Stale(OUT))
        return_void (OUT);

    return_branched (OUT);
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
    if (NOT_CELL_FLAG(pseudo_var, BIND_NOTE_REUSE))
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


struct Loop_Each_State {
    REBVAL *out;  // where to write the output data (must be GC safe)
    const REBVAL *body;  // body to run on each loop iteration
    LOOP_MODE mode;  // FOR-EACH, MAP-EACH, EVERY
    REBCTX *pseudo_vars_ctx;  // vars made by Virtual_Bind_To_New_Context()
    REBVAL *data;  // the data argument passed in
    const REBSER *data_ser;  // series data being enumerated (if applicable)
    EVARS evars;
    REBSPC *specifier;  // specifier (if applicable)
    REBLEN data_idx;  // index into the data for filling current variable
    REBLEN data_len;  // length of the data
};

// Isolation of central logic for FOR-EACH, MAP-EACH, and EVERY so that it
// can be rebRescue()'d in case of failure (to remove SERIES_INFO_HOLD, etc.)
//
// Returns nullptr or R_THROWN, where the relevant result is in les->out.
// (That result may be IS_NULLED() if there was a break during the loop)
//
static REB_R Loop_Each_Core(struct Loop_Each_State *les) {

    assert(IS_BLOCK(les->body) or IS_META_BLOCK(les->body));

    bool more_data = true;
    bool broke = false;

    assert(Is_Stale(les->out));  // return_void requires stale to work

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
        const REBVAR *pseudo_tail;
        REBVAL *pseudo_var = CTX_VARS(&pseudo_tail, les->pseudo_vars_ctx);
        for (; pseudo_var != pseudo_tail; ++pseudo_var) {
            REBVAL *var = Real_Var_From_Pseudo(pseudo_var);

            // Even if data runs out, we could still have one last loop body
            // incarnation to run...with some variables unset.  Null those
            // variables here.
            //
            //     >> for-each [x y] [1] [-- x y]
            //     -- x: 1 y: \null\  ; Seems like an okay rendering
            //
            if (not more_data) {
                Init_Nulled(var);
                continue;
            }

            enum Reb_Kind kind = VAL_TYPE(les->data);
            switch (kind) {
              case REB_BLOCK:
              case REB_SET_BLOCK:
              case REB_GET_BLOCK:
              case REB_META_BLOCK:
              case REB_GROUP:
              case REB_SET_GROUP:
              case REB_GET_GROUP:
              case REB_META_GROUP:
              case REB_PATH:
              case REB_SET_PATH:
              case REB_GET_PATH:
              case REB_META_PATH:
              case REB_TUPLE:
              case REB_SET_TUPLE:
              case REB_GET_TUPLE:
              case REB_META_TUPLE:
                if (var)
                    Derelativize(
                        var,
                        ARR_AT(ARR(les->data_ser), les->data_idx),
                        les->specifier
                    );
                if (++les->data_idx == les->data_len)
                    more_data = false;
                break;

              case REB_OBJECT:
              case REB_ERROR:
              case REB_PORT:
              case REB_MODULE:
              case REB_FRAME: {
                if (var)
                    Init_Any_Word_Bound(
                        var,
                        REB_WORD,
                        VAL_CONTEXT(les->data),
                        KEY_SYMBOL(les->evars.key),
                        les->evars.index
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
                    Copy_Cell(var, les->evars.var);
                }
                else
                    fail ("Loop enumeration of contexts must be 1 or 2 vars");

                more_data = Did_Advance_Evars(&les->evars);

                break; }

              case REB_MAP: {
                assert(les->data_idx % 2 == 0);  // should be on key slot

                const REBVAL *key;
                const REBVAL *val;
                while (true) {  // pass over the unused map slots
                    key = SPECIFIC(ARR_AT(ARR(les->data_ser), les->data_idx));
                    ++les->data_idx;
                    val = SPECIFIC(ARR_AT(ARR(les->data_ser), les->data_idx));
                    ++les->data_idx;
                    if (les->data_idx == les->data_len)
                        more_data = false;
                    if (not IS_NULLED(val))
                        break;
                    if (not more_data)
                        goto finished;
                } while (IS_NULLED(val));

                if (var)
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

              case REB_BINARY: {
                const REBBIN *bin = BIN(les->data_ser);
                if (var)
                    Init_Integer(var, BIN_HEAD(bin)[les->data_idx]);
                if (++les->data_idx == les->data_len)
                    more_data = false;
                break; }

              case REB_TEXT:
              case REB_TAG:
              case REB_FILE:
              case REB_EMAIL:
              case REB_URL:
                if (var)
                    Init_Char_Unchecked(
                        var,
                        GET_CHAR_AT(STR(les->data_ser), les->data_idx)
                    );
                if (++les->data_idx == les->data_len)
                    more_data = false;
                break;

              case REB_ACTION: {
                REBVAL *generated = rebValue(les->data);
                if (generated) {
                    if (var)
                        Copy_Cell(var, generated);
                    rebRelease(generated);
                }
                else {
                    more_data = false;  // any remaining vars must be unset
                    if (pseudo_var == CTX_VARS_HEAD(les->pseudo_vars_ctx)) {
                        //
                        // If we don't have at least *some* of the variables
                        // set for this body loop run, don't run the body.
                        //
                        goto finished;
                    }
                    if (var)
                        Init_Nulled(var);
                }
                break; }

              default:
                panic ("Unsupported type");
            }
        }

        // !!! Throwing e.g. CONTINUE can overwrite the output, but we don't
        // want that.  This is a good argument for making throws "out of band"
        // so that processing mechanics that want to take advantage of "stale"
        // cells don't need to make copies.  Review.

        DECLARE_LOCAL (temp);
        if (Do_Any_Array_At_Throws(temp, les->body, SPECIFIED)) {
            if (not Catching_Break_Or_Continue(temp, &broke)) {
                Move_Cell(les->out, temp);
                return R_THROWN;  // non-loop-related throw
            }

            if (broke) {
                Init_Nulled(les->out);
                return nullptr;
            }
        }

        switch (les->mode) {
          case LOOP_FOR_EACH:
            if (Is_Void(temp)) {
                //
                //    for-each x [1 2 3] [if x != 3 [x]]  =>  none (~) isotope
                //
                // It may seem tempting to drop out the last result:
                //
                //     for-each x [1 2 3] [if x = 3 [continue] x]  => 2
                //
                // But this is too difficult to deal with in loop compositions.
                // We want to tell from the outside if a loop ran or not...
                // that's more important than this esoteric feature.  And if
                // you write something like FOR-BOTH with two FOR-EACH loops,
                // the fact that the second loop would have to use some signal
                // if all branches are erased would make it too difficult to
                // drop out the prior result.
                //
                Init_None(les->out);
            }
            else {
                Move_Cell(les->out, temp);
                Isotopify_If_Nulled(les->out);
                if (IS_META_BLOCK(les->body))
                    Meta_Quotify(les->out);
            }
            break;

          case LOOP_EVERY:
            if (Is_Void(temp)) {
                //
                // In light of other tolerances in the system for voids, EVERY
                // treats a void as "no vote", whether MAYBE is used or not.
                //
                // every x [1 2 3 4] [if odd? x [x]]  =>  [1 3]
                //
                // every x [1 2 3 4] [maybe if odd? x [x]]  => none (~) isotope
                //
                // every x [1 2 3 4] [comment "heavy"]  => none (~) isotope
                //
                // But it returns a none isotope (~) on the skipped bodies,
                // as loop composition breaks down if we try to keep old values.
                //
                Init_None(les->out);
            }
            else {
                // We don't decay isotopes here, for the reason we don't decay
                // them in ALL/ANY etc:
                //
                //    every x [#[false] #[true]] [match logic! x]
                //
                // The ~false~ isotope here catches the misunderstanding by
                // giving an error.  Conditional testing makes us care!
                //
                if (Is_Isotope(temp))
                    fail (Error_Bad_Isotope(temp));

                if (IS_FALSEY(temp)) {
                    Init_Nulled(les->out);
                }
                else if (
                    Is_Stale(les->out)
                    or not IS_NULLED(les->out)  // null means we saw false
                ){
                    Move_Cell(les->out, temp);
                    if (IS_META_BLOCK(les->body))
                        Meta_Quotify(les->out);
                }
            }
            break;

          case LOOP_MAP_EACH:  // don't worry about stale les->out (unused)
            //
            // Here is where we would run a predicate to process the block
            // result before appending.
            //
            /* Predicate(temp) */

            if (Is_Void(temp)) {
                //
                // MAP rules are different because we aren't conditionally
                // testing, so void scenarios should be okay to skip.
                //
                // map x each [1 2 3] [if even? x [x * 10]] => [20]
                //
                // map x each [1 2] [maybe if odd? x [x]] => [1]
                //
                continue;
            }

            Decay_If_Isotope(temp);
            if (IS_META_BLOCK(les->body))
                Meta_Quotify(temp);

            if (IS_NULLED(temp))
                Init_Isotope(temp, Canon(NULL));
            if (Is_Isotope(temp))
                fail (Error_Bad_Isotope(temp));

            // !!! We use APPEND semantics on les->out; whatever APPEND does,
            // we should do here.  Unify logic.

            if (IS_BLANK(temp))
                continue;

            if (IS_QUOTED(temp)) {
                Unquotify(temp, 1);
                if (IS_NULLED(temp))
                    Init_Bad_Word(DS_PUSH(), Canon(NULL));  // APPEND semantics
                else
                    Copy_Cell(DS_PUSH(), temp);
            }
            else if (ANY_THE_KIND(VAL_TYPE(temp))) {
                Plainify(Copy_Cell(DS_PUSH(), temp));
            }
            else if (IS_BLOCK(temp)) {
                const Cell *tail;
                const Cell *v = VAL_ARRAY_AT(&tail, temp);
                for (; v != tail; ++v)
                    Derelativize(DS_PUSH(), v, VAL_SPECIFIER(temp));
            }
            else if (ANY_INERT(temp)) {
                Copy_Cell(DS_PUSH(), temp);  // non nulls added to result
            }
            else
                fail ("Cannot MAP evaluative values w/o QUOTE");

            // MAP-EACH only changes les->out if BREAK
            //
            assert(Is_Stale(les->out));
            break;
        }
    } while (more_data and not broke);

  finished:;

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
static REB_R Loop_Each(REBFRM *frame_, LOOP_MODE mode)
{
    INCLUDE_PARAMS_OF_FOR_EACH;  // MAP-EACH & EVERY must subset interface

    if (ANY_SEQUENCE(ARG(data))) {
        //
        // !!! Temporarily turn any sequences into a BLOCK!, rather than
        // worry about figuring out how to iterate optimized series.  Review
        // as part of an overall vetting of "generic iteration" (which this
        // is a poor substitute for).
        //
        REBVAL *block = rebValue("as block! @", ARG(data));
        Copy_Cell(ARG(data), block);
        rebRelease(block);
    }

    struct Loop_Each_State les;
    les.mode = mode;
    les.out = OUT;
    les.data = ARG(data);
    les.body = ARG(body);

    Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        &les.pseudo_vars_ctx,
        ARG(vars)
    );
    Init_Object(ARG(vars), les.pseudo_vars_ctx);  // keep GC safe

    // Currently the data stack is only used by MAP-EACH to accumulate results
    // but it's faster to just save it than test the loop mode.
    //
    REBDSP dsp_orig = DSP;

    // Extract the series and index being enumerated, based on data type

    REB_R r;

    bool took_hold;
    if (IS_ACTION(les.data)) {
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
        if (ANY_SERIES(les.data)) {
            les.data_ser = VAL_SERIES(les.data);
            les.data_idx = VAL_INDEX(les.data);
            if (ANY_ARRAY(les.data))
                les.specifier = VAL_SPECIFIER(les.data);
            les.data_len = VAL_LEN_HEAD(les.data);  // has HOLD, won't change
        }
        else if (ANY_CONTEXT(les.data)) {
            les.data_ser = CTX_VARLIST(VAL_CONTEXT(les.data));
            Init_Evars(&les.evars, les.data);
        }
        else if (IS_MAP(les.data)) {
            les.data_ser = MAP_PAIRLIST(VAL_MAP(les.data));
            les.data_idx = 0;
            les.data_len = SER_USED(les.data_ser);  // has HOLD, won't change
        }
        else
            panic ("Illegal type passed to Loop_Each()");

        // HOLD so length can't change

        took_hold = NOT_SERIES_FLAG(les.data_ser, FIXED_SIZE);
        if (took_hold)
            SET_SERIES_FLAG(m_cast(REBSER*, les.data_ser), FIXED_SIZE);

        if (ANY_CONTEXT(les.data)) {
            if (not Did_Advance_Evars(&les.evars)) {
                assert(Is_Stale(OUT));  // body never ran
                r = nullptr;
                goto cleanup;
            }
        }
        else if (les.data_idx >= les.data_len) {
            assert(Is_Stale(OUT));  // body never ran
            r = nullptr;
            goto cleanup;
        }
    }

    // If there is a fail() and we took a SERIES_INFO_FIXED_SIZE, that hold
    // needs to be released.  For this reason, the code has to trap errors.

    r = rebRescue(cast(REBDNG*, &Loop_Each_Core), &les);

    //=//// CLEANUPS THAT NEED TO BE DONE DESPITE ERROR, THROW, ETC. //////=//

  cleanup:;

    if (took_hold)  // release read-only lock
        CLEAR_SERIES_FLAG(m_cast(REBSER*, les.data_ser), FIXED_SIZE);

    if (IS_DATATYPE(les.data))  // must free temp array of instances
        Free_Unmanaged_Series(m_cast(REBARR*, ARR(les.data_ser)));

    if (ANY_CONTEXT(les.data))
        Shutdown_Evars(&les.evars);

    //=//// NOW FINISH UP /////////////////////////////////////////////////=//

    if (r == R_THROWN) {  // generic THROW/RETURN/QUIT (not BREAK/CONTINUE)
        if (mode == LOOP_MAP_EACH)
            DS_DROP_TO(dsp_orig);
        return R_THROWN;
    }

    if (r) {
        assert(IS_ERROR(r));
        if (mode == LOOP_MAP_EACH)
            DS_DROP_TO(dsp_orig);
        rebJumps ("fail", rebR(m_cast(REBVAL*, r)));
    }

    // Otherwise, nullptr signals result in les.out (a.k.a. OUT)

    switch (mode) {
      case LOOP_FOR_EACH:
        //
        // pure NULL output means there was a BREAK
        // stale means body never ran: `10 = (10 maybe for-each x [] [<skip>])`
        // ~null~ isotope means body made NULL or ~null~ isotope
        // ~none~ isotope means body made ~none~ isotope or ~void~ isotope
        // any other value is the plain last body result
        //
        if (Is_Stale(OUT))
            return_void (OUT);
        return OUT;

      case LOOP_EVERY:
        //
        // pure NULL output means there was a BREAK
        // stale means body never ran: `10 = (10 maybe every x [] [<skip>])`
        // pure null means loop ran, and at least one body result was "falsey"
        // any other value is the last body result, and is truthy
        //
        if (Is_Stale(OUT))
            return_void (OUT);
        return OUT;

      case LOOP_MAP_EACH:
        if (not Is_Stale(OUT)) {  // only modifies on break
            assert(IS_NULLED(OUT));  // BREAK, so *must* return null
            DS_DROP_TO(dsp_orig);
            return nullptr;
        }

        // !!! MAP-EACH always returns a block except in cases of BREAK, e.g.
        // there's no way to detect from the outside if the body never ran.
        // Review if variants would be useful.
        //
        return Init_Block(OUT, Pop_Stack_Values(dsp_orig));
    }

    DEAD_END;  // all branches handled in enum switch
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
REBNATIVE(cfor)
{
    INCLUDE_PARAMS_OF_CFOR;

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        &context,
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
//          [<blank> any-series!]
//      skip "Number of positions to skip each time"
//          [<blank> integer!]
//      body "Code to evaluate each time"
//          [<const> any-branch!]
//  ]
//
REBNATIVE(for_skip)
{
    INCLUDE_PARAMS_OF_FOR_SKIP;

    REBVAL *series = ARG(series);

    REBINT skip = Int32(ARG(skip));
    if (skip == 0) {
        //
        // !!! https://forum.rebol.info/t/infinite-loops-vs-errors/936
        //
        return_void (OUT);
    }

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        &context,
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
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return_thrown (OUT);
            if (broke)
                return nullptr;

            if (Is_Void(OUT))  // CONTINUE w/no argument
                Init_None(OUT);
        }

        // Modifications to var are allowed, to another ANY-SERIES! value.
        //
        // If `var` is movable (e.g. specified via LIT-WORD!) it must be
        // refreshed each time arbitrary code runs, since the context may
        // expand and move the address, may get PROTECTed, etc.
        //
        var = Real_Var_From_Pseudo(pseudo_var);

        if (IS_NULLED(var))
            fail (PAR(word));
        if (not ANY_SERIES(var))
            fail (var);

        // Increment via skip, which may go before 0 or after the tail of
        // the series.
        //
        // !!! Should also check for overflows of REBIDX range.
        //
        VAL_INDEX_UNBOUNDED(var) += skip;
    }

    if (Is_Stale(OUT))
        return_void (OUT);

    return_branched (OUT);
}


//
//  stop: native [
//
//  {End the current iteration of CYCLE and return a value (nulls allowed)}
//
//      return: []  ; !!! Notation for divergent functions?s
//      ^value "If no argument is provided, assume ~void~ isotope"
//          [<opt> <end> any-value!]
//  ]
//
REBNATIVE(stop)
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

    REBVAL *v = ARG(value);

    if (Is_Meta_Of_End(v))
        Init_Meta_Of_Void(v);  // Treat STOP the same as STOP VOID

    return_thrown (Init_Thrown_With_Label_Meta(OUT, ARG(value), Lib(STOP)));
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
REBNATIVE(cycle)
{
    INCLUDE_PARAMS_OF_CYCLE;

    while (true) {
        if (Do_Branch_Throws(OUT, ARG(body))) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke)) {
                const REBVAL *label = VAL_THROWN_LABEL(OUT);
                if (
                    IS_ACTION(label)
                    and ACT_DISPATCHER(VAL_ACTION(label)) == &N_stop
                ){
                    // See notes on STOP for why CYCLE is unique among loop
                    // constructs, with a BREAK variant that returns a value.
                    //
                    CATCH_THROWN_META(OUT, OUT);

                    // !!! Technically, we know CYCLE's body will always run,
                    // so we could make an exception to having it return
                    // voids from STOP (or pure NULL).  There's probably no
                    // good reason to do that, so going with the usual branch
                    // rules unless there's a good demonstrated case otherwise.

                    if (Is_Meta_Of_Void(OUT))
                        return Init_None(OUT);

                    Meta_Unquotify(OUT);
                    Isotopify_If_Nulled(OUT);  // NULL reserved for BREAK
                    return OUT;
                }

                return R_THROWN;
            }
            if (broke)
                return nullptr;
        }
        // No need to isotopify result, it doesn't escape...
    }

    DEAD_END;
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
//          [<blank> any-series! any-context! map! any-sequence!
//           action!]  ; experimental
//      body "Block to evaluate each time"
//          [<const> block! meta-block!]
//  ]
//
REBNATIVE(for_each)
{
    return Loop_Each(frame_, LOOP_FOR_EACH);
}


//
//  every: native [
//
//  {Iterate and return false if any previous body evaluations were false}
//
//      return: [<opt> <void> any-value!]
//          {null on BREAK, blank on empty, false or the last truthy value}
//      :vars "Word or block of words to set each time, no new var if quoted"
//          [blank! word! lit-word! block! group!]
//      data [<blank> any-series! any-context! map! datatype! action!]
//          "The series to traverse"
//      body [<const> block! meta-block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(every)
{
    return Loop_Each(frame_, LOOP_EVERY);
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
    REBVAL *out;
    REBVAL *data;
    REBSER *series;
    bool broke;  // e.g. a BREAK ran
    const REBVAL *body;
    REBCTX *context;
    REBLEN start;
    REB_MOLD *mo;
};


// See notes on Remove_Each_State
//
inline static REBLEN Finalize_Remove_Each(struct Remove_Each_State *res)
{
    assert(GET_SERIES_INFO(res->series, HOLD));
    CLEAR_SERIES_INFO(res->series, HOLD);

    // If there was a BREAK, we return NULL to indicate that as part of
    // the loop protocol.  This prevents giving back a return value of
    // how many removals there were, so we don't do the removals.

    REBLEN count = 0;
    if (ANY_ARRAY(res->data)) {
        if (res->broke) {  // cleanup markers, don't do removals
            const Cell *tail;
            Cell *temp = VAL_ARRAY_KNOWN_MUTABLE_AT(&tail, res->data);
            for (; temp != tail; ++temp) {
                if (GET_CELL_FLAG(temp, NOTE_REMOVE))
                    CLEAR_CELL_FLAG(temp, NOTE_REMOVE);
            }
            return 0;
        }

        REBLEN len = VAL_LEN_HEAD(res->data);

        const Cell *tail;
        Cell *dest = VAL_ARRAY_KNOWN_MUTABLE_AT(&tail, res->data);
        Cell *src = dest;

        // avoid blitting cells onto themselves by making the first thing we
        // do is to pass up all the unmarked (kept) cells.
        //
        while (src != tail and NOT_CELL_FLAG(src, NOTE_REMOVE)) {
            ++src;
            ++dest;
        }

        // If we get here, we're either at the end, or all the cells from here
        // on are going to be moving to somewhere besides the original spot
        //
        for (; dest != tail; ++dest, ++src) {
            while (src != tail and GET_CELL_FLAG(src, NOTE_REMOVE)) {
                ++src;
                --len;
                ++count;
            }
            if (src == tail) {
                SET_SERIES_LEN(VAL_ARRAY_KNOWN_MUTABLE(res->data), len);
                return count;
            }
            Copy_Cell(dest, src);  // same array, so we can do this
        }

        // If we get here, there were no removals, and length is unchanged.
        //
        assert(count == 0);
        assert(len == VAL_LEN_HEAD(res->data));
    }
    else if (IS_BINARY(res->data)) {
        if (res->broke) { // leave data unchanged
            Drop_Mold(res->mo);
            return 0;
        }

        REBBIN *bin = BIN(res->series);

        // If there was a THROW, or fail() we need the remaining data
        //
        REBLEN orig_len = VAL_LEN_HEAD(res->data);
        assert(res->start <= orig_len);
        Append_Ascii_Len(
            res->mo->series,
            cs_cast(BIN_AT(bin, res->start)),
            orig_len - res->start
        );

        // !!! We are reusing the mold buffer, but *not putting UTF-8 data*
        // into it.  Revisit if this inhibits cool UTF-8 based tricks the
        // mold buffer might do otherwise.
        //
        REBBIN *popped = Pop_Molded_Binary(res->mo);

        assert(BIN_LEN(popped) <= VAL_LEN_HEAD(res->data));
        count = VAL_LEN_HEAD(res->data) - BIN_LEN(popped);

        // We want to swap out the data properties of the series, so the
        // identity of the incoming series is kept but now with different
        // underlying data.
        //
        Swap_Series_Content(popped, res->series);

        Free_Unmanaged_Series(popped);  // now frees incoming series's data
    }
    else {
        assert(ANY_STRING(res->data));
        if (res->broke) { // leave data unchanged
            Drop_Mold(res->mo);
            return 0;
        }

        // If there was a BREAK, THROW, or fail() we need the remaining data
        //
        REBLEN orig_len = VAL_LEN_HEAD(res->data);
        assert(res->start <= orig_len);

        for (; res->start != orig_len; ++res->start) {
            Append_Codepoint(
                res->mo->series,
                GET_CHAR_AT(STR(res->series), res->start)
            );
        }

        REBSTR *popped = Pop_Molded_String(res->mo);

        assert(STR_LEN(popped) <= VAL_LEN_HEAD(res->data));
        count = VAL_LEN_HEAD(res->data) - STR_LEN(popped);

        // We want to swap out the data properties of the series, so the
        // identity of the incoming series is kept but now with different
        // underlying data.
        //
        Swap_Series_Content(popped, res->series);

        Free_Unmanaged_Series(popped);  // frees incoming series's data
    }

    return count;
}


// See notes on Remove_Each_State
//
static REB_R Remove_Each_Core(struct Remove_Each_State *res)
{
    // Set a bit saying we are iterating the series, which will disallow
    // mutations (including a nested REMOVE-EACH) until completion or failure.
    // This flag will be cleaned up by Finalize_Remove_Each(), which is run
    // even if there is a fail().
    //
    SET_SERIES_INFO(res->series, HOLD);

    REBLEN index = res->start;  // up here to avoid longjmp clobber warnings

    REBLEN len = ANY_STRING(res->data)
        ? STR_LEN(STR(res->series))
        : SER_USED(res->series);  // temp read-only, this won't change

    while (index < len) {
        assert(res->start == index);

        const REBVAR *var_tail;
        REBVAL *var = CTX_VARS(&var_tail, res->context);  // fixed (#2274)
        for (; var != var_tail; ++var) {
            if (index == len) {
                //
                // The second iteration here needs x = #"c" and y as void.
                //
                //     data: copy "abc"
                //     remove-each [x y] data [...]
                //
                Init_Nulled(var);
                continue;  // the `for` loop setting variables
            }

            if (ANY_ARRAY(res->data))
                Derelativize(
                    var,
                    VAL_ARRAY_AT_HEAD(res->data, index),
                    VAL_SPECIFIER(res->data)
                );
            else if (IS_BINARY(res->data)) {
                REBBIN *bin = BIN(res->series);
                Init_Integer(var, cast(REBI64, BIN_HEAD(bin)[index]));
            }
            else {
                assert(ANY_STRING(res->data));
                Init_Char_Unchecked(
                    var,
                    GET_CHAR_AT(STR(res->series), index)
                );
            }
            ++index;
        }

        if (Do_Any_Array_At_Throws(res->out, res->body, SPECIFIED)) {
            if (not Catching_Break_Or_Continue(
                res->out,
                &res->broke
            )){
                REBLEN removals = Finalize_Remove_Each(res);
                UNUSED(removals);

                return R_THROWN; // we'll bubble it up, but will also finalize
            }

            if (res->broke) {
                //
                // BREAK; this means we will return nullptr and not run any
                // removals (we couldn't report how many if we did)
                //
                assert(res->start < len);
                REBLEN removals = Finalize_Remove_Each(res);
                UNUSED(removals);

                Init_Nulled(res->out);
                return nullptr;
            }
        }

        if (Is_Void(res->out)) {
            res->start = index;
            Init_None(res->out);  // void reserved for "loop never ran"
            continue;  // keep requested, don't mark for culling
        }

        Reify_Eval_Out_Plain(res->out);

        // We do not want to decay isotopes, e.g. if someone tried to say:
        //
        //    remove-each x [...] [n: null, ..., match [<opt> integer!] n]
        //
        // The ~null~ isotope protects from having a condition you thought
        // should be truthy come back NULL and be falsey.
        //
        // !!! To be more safe it could be limited to logic!, but at bare
        // minimum it needs to fail on isotopes.
        //
        if (Is_Isotope(res->out))
            fail (Error_Bad_Isotope(res->out));

        if (ANY_ARRAY(res->data)) {
            if (IS_FALSEY(res->out)) {
                res->start = index;
                continue;  // keep requested, don't mark for culling
            }

            do {
                assert(res->start <= len);
                SET_CELL_FLAG(  // v-- okay to mark despite read only
                    m_cast(Cell*, ARR_AT(VAL_ARRAY(res->data), res->start)),
                    NOTE_REMOVE
                );
                ++res->start;
            } while (res->start != index);
        }
        else {
            if (
                not Is_Null_Isotope(res->out)
                and IS_TRUTHY(res->out)
            ){
                res->start = index;
                continue;  // remove requested, don't save to buffer
            }

            do {
                assert(res->start <= len);
                if (IS_BINARY(res->data)) {
                    REBBIN *bin = BIN(res->series);
                    Append_Ascii_Len(
                        res->mo->series,
                        cs_cast(BIN_AT(bin, res->start)),
                        1
                    );
                }
                else {
                    Append_Codepoint(
                        res->mo->series,
                        GET_CHAR_AT(STR(res->series), res->start)
                    );
                }
                ++res->start;
            } while (res->start != index);
        }
    }

    // We get here on normal completion (THROW and BREAK will return above)

    assert(not res->broke and res->start == len);

    REBLEN removals = Finalize_Remove_Each(res);
    Init_Integer(res->out, removals);

    return nullptr;
}


//
//  remove-each: native [
//
//  {Removes values for each block that returns true.}
//
//      return: [<opt> integer!]
//          {Number of removed series items, or null if BREAK}
//      :vars "Word or block of words to set each time, no new var if quoted"
//          [blank! word! lit-word! block! group!]
//      data [<blank> any-series! any-sequence! action!]
//          "The series to traverse (modified)" ; should BLANK! opt-out?
//      body [<const> block!]
//          "Block to evaluate (return TRUE to remove)"
//  ]
//
REBNATIVE(remove_each)
{
    INCLUDE_PARAMS_OF_REMOVE_EACH;

    struct Remove_Each_State res;
    res.data = ARG(data);

    // !!! Currently there is no support for VECTOR!, or IMAGE! (what would
    // that even *mean*?) yet these are in the ANY-SERIES! typeset.
    //
    if (not (
        ANY_ARRAY(res.data) or ANY_STRING(res.data) or IS_BINARY(res.data)
    )){
        fail (res.data);
    }

    // Check the series for whether it is read only, in which case we should
    // not be running a REMOVE-EACH on it.  This check for permissions applies
    // even if the REMOVE-EACH turns out to be a no-op.
    //
    res.series = VAL_SERIES_ENSURE_MUTABLE(res.data);

    if (VAL_INDEX(res.data) >= SER_USED(res.series)) {
        //
        // If index is past the series end, then there's nothing removable.
        //
        // !!! Should REMOVE-EACH follow the "loop conventions" where if the
        // body never gets a chance to run, the return value is bad-word?
        //
        return Init_Integer(OUT, 0);
    }

    // Create a context for the loop variables, and bind the body to it.
    // Do this before PUSH_TRAP, so that if there is any failure related to
    // memory or a poorly formed ARG(vars) that it doesn't try to finalize
    // the REMOVE-EACH, as `res` is not ready yet.
    //
    Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        &res.context,
        ARG(vars)
    );
    Init_Object(ARG(vars), res.context);  // keep GC safe
    res.body = ARG(body);

    res.start = VAL_INDEX(res.data);

    REB_MOLD mold_struct;
    if (ANY_ARRAY(res.data)) {
        //
        // We're going to use NODE_FLAG_MARKED on the elements of data's
        // array for those items we wish to remove later.
        //
        // !!! This may not be better than pushing kept values to the data
        // stack and then creating a precisely-sized output blob to swap as
        // the underlying memory for the array.  (Imagine a large array from
        // which there are many removals, and the ensuing wasted space being
        // left behind).  But worth testing the technique of marking in case
        // it's ever required for other scenarios.
        //
        TRASH_POINTER_IF_DEBUG(res.mo);
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
        memset(&mold_struct, 0, sizeof(mold_struct));
        res.mo = &mold_struct;
        Push_Mold(res.mo);
    }

    res.out = OUT;

    res.broke = false;  // will be set to true if there is a BREAK

    REB_R r = rebRescue(cast(REBDNG*, &Remove_Each_Core), &res);

    if (r == R_THROWN)
        return_thrown (OUT);

    if (r) {  // Remove_Each_Core() couldn't finalize in this case due to fail
        assert(IS_ERROR(r));

        // !!! Because we use the mold buffer to achieve removals from strings
        // and the mold buffer has to equalize at the end of rebRescue(), we
        // cannot mutate the string here to account for the removals.  So
        // FAIL means no removals--but we need to get in and take out the
        // marks on the array cells.
        //
        REBLEN removals = Finalize_Remove_Each(&res);
        UNUSED(removals);

        rebJumps("fail", rebR(m_cast(REBVAL*, r)));
    }

    if (res.broke)
        assert(IS_NULLED(OUT));  // BREAK in loop
    else
        assert(IS_INTEGER(OUT));  // no break--plain removal count

    return OUT;
}


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
//          [<blank> any-series! any-sequence! action!]
//      body "Block to evaluate each time (result will be kept literally)"
//          [<const> block!]
//  ]
//
REBNATIVE(map_each)
//
// !!! MAP-EACH is a legacy construct that lacks the planned flexibility of
// MAP, as it presumes a 1:1 mapping vs. being able to splice.  The syntax of
// FOR and MAP are intended to be generic to work with generators or a dialect.
{
    INCLUDE_PARAMS_OF_MAP_EACH;

    UNUSED(PAR(vars));
    UNUSED(PAR(data));

    Metafy(ARG(body));  // want the body to effectively quote the argument

    return Loop_Each(
        frame_,
        LOOP_MAP_EACH  // will transition to MAP_EACH_SPLICED as default
    );
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
//          [<blank> quoted! action!]
//      :body "Block to evaluate each time"
//          [<const> block! meta-block!]
//  ]
//
REBNATIVE(map)
{
    INCLUDE_PARAMS_OF_MAP;

    UNUSED(PAR(vars));
    UNUSED(PAR(body));

    REBVAL *data = ARG(data);

    if (IS_ACTION(data)) {
        // treat as a generator
    }
    else if (
        not IS_QUOTED(data)
        or VAL_QUOTED_DEPTH(data) != 1
        or not (
            ANY_SERIES(Unquotify(data, 1))
            or ANY_PATH(data)  // has been unquoted
        )
    ){
        fail ("MAP only supports one-level QUOTED! series/path for the moment");
    }

    return Loop_Each(
        frame_,
        LOOP_MAP_EACH  // will transition to MAP_EACH_SPLICED as default
    );
}


//
//  repeat: native [
//
//  {Evaluates a block a specified number of times}
//
//      return: "Last body result, or null if BREAK"
//          [<opt> <void> any-value!]
//      count "Repetitions (true loops infinitely, false doesn't run)"
//          [<blank> any-number! logic!]
//      body "Block to evaluate or action to run"
//          [<const> block! action!]
//  ]
//
REBNATIVE(repeat)
{
    INCLUDE_PARAMS_OF_REPEAT;

    if (IS_FALSEY(ARG(count))) {
        assert(IS_LOGIC(ARG(count)));  // is false (opposite of infinite loop)
        return_void (OUT);
    }

    REBI64 count;

    if (IS_LOGIC(ARG(count))) {
        assert(VAL_LOGIC(ARG(count)) == true);

        // Run forever, and as a micro-optimization don't handle specially
        // in the loop, just seed with a very large integer.  In the off
        // chance that we exhaust it, jump here to re-seed and loop again.
        //
      restart:
        count = INT64_MAX;
    }
    else
        count = Int64(ARG(count));

    for (; count > 0; count--) {
        if (Do_Branch_Throws(OUT, ARG(body))) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return_thrown (OUT);
            if (broke)
                return nullptr;

            if (Is_Void(OUT))  // CONTINUE w/no argument
                Init_None(OUT);
        }
    }

    if (IS_LOGIC(ARG(count)))
        goto restart;  // "infinite" loop exhausted MAX_I64 steps (rare case)

    if (Is_Stale(OUT))
        return_void (OUT);

    return_branched (OUT);  // asserts no pure NULL or isotope ~void~
}


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
//          [<blank> any-number! any-sequence! quoted! block! action!]
//      'body "!!! actually just BLOCK!, but quoted to catch legacy uses"
//          [<const> any-value!]
//  ]
//
REBNATIVE(for)
{
    INCLUDE_PARAMS_OF_FOR;

    REBVAL *body = ARG(body);

    if (IS_GROUP(body)) {
        if (Eval_Value_Throws(SPARE, body, SPECIFIED))
            return_thrown (SPARE);
        Move_Cell(body, SPARE);
    }

    if (not IS_BLOCK(body))
        fail ("FOR has a new syntax, use CFOR for old arity-5 behavior.");

    REBVAL *value = ARG(value);

    if (IS_QUOTED(value)) {
        Unquotify(value, 1);

        if (not (ANY_SERIES(value) or ANY_SEQUENCE(value)))
            fail (PAR(value));

        // Delegate to FOR-EACH (note: in the future this will be the other
        // way around, with FOR-EACH delegating to FOR).
        //
        if (rebRunThrows(
            OUT,  // <-- output cell
            Lib(FOR_EACH), ARG(vars), rebQ(value), body
        )){
            return_thrown (OUT);
        }
        return OUT;
    }

    if (IS_DECIMAL(value) or IS_PERCENT(value))
        Init_Integer(value, Int64(value));

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        body,
        &context,
        ARG(vars)
    );
    Init_Object(ARG(vars), context);  // keep GC safe

    assert(CTX_LEN(context) == 1);

    REBVAL *var = CTX_VAR(context, 1);  // not movable, see #2274

    REBI64 n = VAL_INT64(value);
    if (n < 1)  // Loop_Integer from 1 to 0 with bump of 1 is infinite
        return_void (OUT);

    return Loop_Integer_Common(
        frame_, var, body, 1, VAL_INT64(value), 1
    );
}


//
//  until: native [
//
//  {Evaluates the body until it produces a conditionally true value}
//
//      return: [<opt> any-value!]
//          {Last body result, or null if a BREAK occurred}
//      body [<const> block!]
//      /predicate "Function to apply to body result (default is DID)"
//          [action!]
//  ]
//
REBNATIVE(until)
//
// 1. When CONTINUE has an argument, it acts as if the loop body evaluated to
//    that argument.  UNTIL's condition and body are the same.  That means that
//    CONTINUE TRUE will stop the UNTIL and return TRUE, CONTINUE 10 will stop
//    the loop and return 10, etc.
//
// 2. Purusant to [1], we want CONTINUE (or CONTINUE VOID) to keep the loop
//    running.  For parity between what continue does with an argument and
//    what the loop does if the body evaluates to that argument, it suggests
//    tolerating a void body result as intent to continue the loop also.
//
// 3. While some cases may want to decay isotopes for convenience, this is
//    not such a case.  Imagine:
//
//        until [match blank! get-queue-item]
//
//    The likely intent is that BLANK! is supposed to stop the loop, and return
//    a BLANK! value.  Erroring on the ~blank~ isotope draw attention to the
//    problem, so they know to use DID MATCH or CATCH/THROW the blank.
{
    INCLUDE_PARAMS_OF_UNTIL;

    REBVAL *body = ARG(body);
    REBVAL *predicate = ARG(predicate);

    do {
        if (Do_Any_Array_At_Throws(OUT, body, SPECIFIED)) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return_thrown (OUT);
            if (broke)
                return nullptr;

            // continue acts like body evaluated to its argument, see [1]
        }

        if (Is_Void(OUT))
            continue;  // skip void results, see [2]

        if (IS_NULLED(predicate)) {
            if (Is_Isotope(OUT))
                fail (Error_Bad_Isotope(OUT));  // all isotopes fail, see [3]

            if (IS_TRUTHY(OUT))
                return OUT;  // truthy body result--return value
        }
        else {
            if (rebUnboxLogic(predicate, rebQ(OUT)))  // rebTruthy() ?
                return OUT;
        }
    } while (true);
}


//
//  while: native [
//
//  {So long as a condition is truthy, evaluate the body}
//
//      return: "Last body result, or null if BREAK"
//          [<opt> <void> any-value!]
//      condition [<const> block!]
//      body [<const> block! action!]
//  ]
//
REBNATIVE(while)
//
// 1. It was considered if `while true [...]` should infinite loop, and then
//    `while false [...]` never ran.  However, that could lead to accidents of
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
//        flag: true, while [flag] [flag: false, continue null]
//
//    We don't want that to evaluate to NULL--because NULL is reserved for
//    signaling BREAK.  So the result of a body with a CONTINUE in it will be
//    turned from null to a ~null~ isotope.  Similarly, void results are
//    reserved for when the body never runs--so they're  turned into none (~)
{
    INCLUDE_PARAMS_OF_WHILE;

    REBVAL *condition = ARG(condition);  // condition is BLOCK! only, see [1]
    REBVAL *body = ARG(body);

    assert(Is_Void(SPARE));

    while (true) {
        if (Do_Any_Array_At_Throws(SPARE, condition, SPECIFIED))
            return_thrown (SPARE);  // break/continue in body only, see [2]

        if (Is_Void(SPARE))
            continue;  // restart loop when condition vanishes, see [3]

        if (IS_FALSEY(SPARE)) {  // falsey condition => return last body result
            if (Is_Stale(OUT))
                return_void (OUT);  // body never ran, so no result to return!

            return_branched (OUT);
        }

        if (Do_Branch_With_Throws(OUT, body, SPARE)) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return_thrown (OUT);

            if (broke)
                return nullptr;

            if (Is_Void(OUT))  // CONTINUE w/no argument
                Init_None(OUT);
        }
    }

    // ~unreachable~
}
