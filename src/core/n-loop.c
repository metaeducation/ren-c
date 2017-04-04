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
    LOOP_REMOVE_EACH,
    LOOP_MAP_EACH,
    LOOP_EVERY
} LOOP_MODE;


//
//  Catching_Break_Or_Continue: C
//
// Determines if a thrown value is either a break or continue.  If so,
// modifies `val` to be the throw's argument, sets `stop` flag if it
// was a BREAK or BREAK/WITH, and returns TRUE.
//
// If FALSE is returned then the throw name `val` was not a break
// or continue, and needs to be bubbled up or handled another way.
//
REBOOL Catching_Break_Or_Continue(REBVAL *val, REBOOL *stop)
{
    assert(THROWN(val));

    // Throw /NAME-s used by CONTINUE and BREAK are the actual native
    // function values of the routines themselves.
    if (!IS_FUNCTION(val))
        return FALSE;

    if (VAL_FUNC_DISPATCHER(val) == &N_break) {
        *stop = TRUE; // was BREAK or BREAK/WITH
        CATCH_THROWN(val, val); // will be void if no /WITH was used
        return TRUE;
    }

    if (VAL_FUNC_DISPATCHER(val) == &N_continue) {
        *stop = FALSE; // was CONTINUE or CONTINUE/WITH
        CATCH_THROWN(val, val); // will be void if no /WITH was used
        return TRUE;
    }

    // Else: Let all other thrown values bubble up.
    return FALSE;
}


//
//  break: native [
//
//  {Exit the current iteration of a loop and stop iterating further.}
//
//      /with
//          {Act as if loop body finished current evaluation with a value}
//      value [any-value!]
//  ]
//
REBNATIVE(break)
//
// BREAK is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name value :break`.
{
    INCLUDE_PARAMS_OF_BREAK;

    Move_Value(D_OUT, NAT_VALUE(break));

    CONVERT_NAME_TO_THROWN(D_OUT, REF(with) ? ARG(value) : VOID_CELL);

    return R_OUT_IS_THROWN;
}


//
//  continue: native [
//
//  "Throws control back to top of loop for next iteration."
//
//      /with
//          {Act as if loop body finished current evaluation with a value}
//      value [any-value!]
//  ]
//
REBNATIVE(continue)
//
// CONTINUE is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name value :continue`.
{
    INCLUDE_PARAMS_OF_CONTINUE;

    Move_Value(D_OUT, NAT_VALUE(continue));

    CONVERT_NAME_TO_THROWN(D_OUT, REF(with) ? ARG(value) : VOID_CELL);

    return R_OUT_IS_THROWN;
}


//
//  Copy_Body_Deep_Bound_To_New_Context: C
//
// Looping constructs which are parameterized by WORD!s to set each time
// through the loop must copy the body in R3-Alpha's model.  For instance:
//
//    for-each [x y] [1 2 3] [print ["this body must be copied for" x y]]
//
// The reason is because the context in which X and Y live does not exist
// prior to the execution of the FOR-EACH.  And if the body were destructively
// rebound, then this could mutate and disrupt bindings of code that was
// intended to be reused.
//
// (Note that R3-Alpha was somewhat inconsistent on the idea of being
// sensitive about non-destructively binding arguments in this way.
// MAKE OBJECT! purposefully mutated bindings in the passed-in block.)
//
// The context is effectively an ordinary object, and outlives the loop:
//
//     x-word: none
//     for-each x [1 2 3] [x-word: 'x | break]
//     get x-word ;-- returns 1
//
// !!! Ren-C managed to avoid deep copying function bodies yet still get
// "specific binding" by means of "relative values" (RELVALs) and specifiers.
// Extending this approach is hoped to be able to avoid the deep copy.  It
// may also be that the underlying data of the
//
// !!! With stack-backed contexts in Ren-C, it may be the case that the
// chunk stack is used as backing memory for the loop, so it can be freed
// when the loop is over and word lookups will error.
//
// Note that because we are copying the block in order to rebind it, the
// ensuing loop code will `Do_At_Throws(out, body, 0);`.  Starting at
// zero is correct because the duplicate body has already had the
// items before its VAL_INDEX() omitted.
//
static REBARR *Copy_Body_Deep_Bound_To_New_Context(
    REBCTX **context_out,
    const REBVAL *spec,
    REBVAL *body
) {
    assert(IS_BLOCK(body));

    REBINT num_vars = IS_BLOCK(spec) ? VAL_LEN_AT(spec) : 1;
    if (num_vars == 0)
        fail (spec);

    REBCTX *context = Alloc_Context(REB_OBJECT, num_vars);
    TERM_ARRAY_LEN(CTX_VARLIST(context), num_vars + 1);
    TERM_ARRAY_LEN(CTX_KEYLIST(context), num_vars + 1);

    REBVAL *key = CTX_KEYS_HEAD(context);
    REBVAL *var = CTX_VARS_HEAD(context);

    const RELVAL *item;
    REBSPC *specifier;
    if (IS_BLOCK(spec)) {
        item = VAL_ARRAY_AT(spec);
        specifier = VAL_SPECIFIER(spec);
    }
    else {
        item = spec;
        specifier = SPECIFIED;
    }

    while (num_vars-- > 0) {
        if (!IS_WORD(item) && !IS_SET_WORD(item))
            fail (Error_Invalid_Arg_Core(item, specifier));

        Init_Typeset(key, ALL_64, VAL_WORD_SPELLING(item));
        key++;

        Init_Void(var);
        var++;

        ++item;
    }

    assert(IS_END(key)); // set above by TERM_ARRAY_LEN
    assert(IS_END(var)); // ...same

    REBARR *body_out = Copy_Array_At_Deep_Managed(
        VAL_ARRAY(body), VAL_INDEX(body), VAL_SPECIFIER(body)
    );
    Bind_Values_Deep(ARR_HEAD(body_out), context);

    *context_out = context;

    return body_out;
}


//
//  Loop_Series_Common: C
//
static REB_R Loop_Series_Common(
    REBVAL *out,
    REBVAL *var,
    REBARR *body,
    REBVAL *start,
    REBINT ei,
    REBINT ii
) {
    assert(IS_END(out));

    REBINT si = VAL_INDEX(start);
    enum Reb_Kind type = VAL_TYPE(start);

    Move_Value(var, start);

    if (ei >= cast(REBINT, VAL_LEN_HEAD(start)))
        ei = cast(REBINT, VAL_LEN_HEAD(start));

    if (ei < 0) ei = 0;

    for (; (ii > 0) ? si <= ei : si >= ei; si += ii) {
        VAL_INDEX(var) = si;

        // loop bodies are copies at the moment, so fully specified; there
        // may be a point to making it more efficient by not always copying
        //
        if (Do_At_Throws(out, body, 0, SPECIFIED)) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(out, &stop)) {
                if (stop)
                    return R_BLANK;
                goto next_iteration;
            }
            return R_OUT_IS_THROWN;
        }

    next_iteration:
        if (VAL_TYPE(var) != type) fail (Error_Invalid_Type(VAL_TYPE(var)));
        si = VAL_INDEX(var);
    }

    return R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY;
}


//
//  Loop_Integer_Common: C
//
static REB_R Loop_Integer_Common(
    REBVAL *out,
    REBVAL *var,
    REBARR *body,
    REBI64 start,
    REBI64 end,
    REBI64 incr
) {
    assert(IS_END(out));

    Reset_Val_Header(var, REB_INTEGER);

    while ((incr > 0) ? start <= end : start >= end) {
        VAL_INT64(var) = start;

        if (Do_At_Throws(out, body, 0, SPECIFIED)) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(out, &stop)) {
                if (stop)
                    return R_BLANK;
                goto next_iteration;
            }
            return R_OUT_IS_THROWN;
        }

    next_iteration:
        if (!IS_INTEGER(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        start = VAL_INT64(var);

        if (REB_I64_ADD_OF(start, incr, &start))
            fail (Error_Overflow_Raw());
    }

    return R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY;
}


//
//  Loop_Number_Common: C
//
static REB_R Loop_Number_Common(
    REBVAL *out,
    REBVAL *var,
    REBARR *body,
    REBVAL *start,
    REBVAL *end,
    REBVAL *incr
) {
    assert(IS_END(out));

    REBDEC s;
    if (IS_INTEGER(start))
        s = cast(REBDEC, VAL_INT64(start));
    else if (IS_DECIMAL(start) || IS_PERCENT(start))
        s = VAL_DECIMAL(start);
    else
        fail (start);

    REBDEC e;
    if (IS_INTEGER(end))
        e = cast(REBDEC, VAL_INT64(end));
    else if (IS_DECIMAL(end) || IS_PERCENT(end))
        e = VAL_DECIMAL(end);
    else
        fail (end);

    REBDEC i;
    if (IS_INTEGER(incr))
        i = cast(REBDEC, VAL_INT64(incr));
    else if (IS_DECIMAL(incr) || IS_PERCENT(incr))
        i = VAL_DECIMAL(incr);
    else
        fail (incr);

    Reset_Val_Header(var, REB_DECIMAL);

    for (; (i > 0.0) ? s <= e : s >= e; s += i) {
        VAL_DECIMAL(var) = s;

        if (Do_At_Throws(out, body, 0, SPECIFIED)) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(out, &stop)) {
                if (stop)
                    return R_BLANK;
                goto next_iteration;
            }
            return R_OUT_IS_THROWN;
        }

    next_iteration:
        if (!IS_DECIMAL(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        s = VAL_DECIMAL(var);
    }

    return R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY;
}


//
//  Loop_Each: C
//
// Common implementation code of FOR-EACH, REMOVE-EACH, MAP-EACH, and EVERY.
//
// !!! This routine has been slowly clarifying since R3-Alpha, and can
// likely be factored in a better way...pushing more per-native code into the
// natives themselves.
//
static REB_R Loop_Each(REBFRM *frame_, LOOP_MODE mode)
{
    INCLUDE_PARAMS_OF_FOR_EACH;

    REBVAL *data = ARG(data);
    assert(!IS_VOID(data));

    if (IS_BLANK(data))
        return R_VOID;

    REBOOL stop = FALSE;
    REBOOL threw = FALSE; // did a non-BREAK or non-CONTINUE throw occur

    assert(IS_END(D_OUT));
    if (mode == LOOP_EVERY)
        Init_End(D_CELL); // Final result is in D_CELL (last TRUE? or a BLANK!)

    REBCTX *context;
    REBARR *body_copy = Copy_Body_Deep_Bound_To_New_Context(
        &context,
        ARG(vars),
        ARG(body)
    );
    Init_Object(ARG(vars), context); // keep GC safe
    Init_Block(ARG(body), body_copy); // keep GC safe

    // Currently the data stack is only used by MAP-EACH to accumulate results
    // but it's faster to just save it than test the loop mode.
    //
    REBDSP dsp_orig = DSP;

    // Extract the series and index being enumerated, based on data type

    REBSER *series;
    REBCNT index;
    if (ANY_CONTEXT(data)) {
        series = AS_SERIES(CTX_VARLIST(VAL_CONTEXT(data)));
        index = 1;
    }
    else if (IS_MAP(data)) {
        series = VAL_SERIES(data);
        index = 0;
    }
    else if (IS_DATATYPE(data)) {
        //
        // !!! Snapshotting the state is not particularly efficient.  However,
        // bulletproofing an enumeration of the system against possible GC
        // would be difficult.  And this is really just a debug/instrumentation
        // feature anyway.
        //
        switch (VAL_TYPE_KIND(data)) {
        case REB_FUNCTION:
            series = AS_SERIES(Snapshot_All_Functions());
            index = 0;
            PUSH_GUARD_ARRAY_CONTENTS(AS_ARRAY(series));
            break;

        default:
            fail ("FUNCTION! is the only datatype with global enumeration");
        }
    }
    else {
        series = VAL_SERIES(data);
        index = VAL_INDEX(data);
        if (index >= SER_LEN(series)) {
            if (mode == LOOP_REMOVE_EACH) {
                Init_Integer(D_OUT, 0);
                return R_OUT;
            }
            else if (mode == LOOP_MAP_EACH) {
                Init_Block(D_OUT, Make_Array(0));
                return R_OUT;
            }
            return R_VOID;
        }
    }

    REBCNT write_index = index;

    // Iterate over each value in the data series block:

    REBCNT tail;
    while (index < (tail = SER_LEN(series))) {
        REBCNT i;
        REBCNT j = 0;

        REBVAL *key = CTX_KEY(context, 1);
        REBVAL *var = CTX_VAR(context, 1);

        REBCNT read_index;

        read_index = index;  // remember starting spot

        // Set the FOREACH loop variables from the series:
        for (i = 1; NOT_END(key); i++, key++, var++) {

            if (index >= tail) {
                Init_Blank(var);
                continue;
            }

            if (ANY_ARRAY(data)) {
                Derelativize(
                    var,
                    ARR_AT(AS_ARRAY(series), index),
                    VAL_SPECIFIER(data) // !!! always matches series?
                );
            }
            else if (IS_DATATYPE(data)) {
                Derelativize(
                    var,
                    ARR_AT(AS_ARRAY(series), index),
                    SPECIFIED // array generated via data stack, all specific
                );
            }
            else if (ANY_CONTEXT(data)) {
                if (Get_Val_Flag(
                    VAL_CONTEXT_KEY(data, index), TYPESET_FLAG_HIDDEN
                )) {
                    // Do not evaluate this iteration
                    index++;
                    goto skip_hidden;
                }

                // Alternate between word and value parts of object:
                if (j == 0) {
                    Init_Any_Word_Bound(
                        var,
                        REB_WORD,
                        CTX_KEY_SPELLING(VAL_CONTEXT(data), index),
                        AS_CONTEXT(series),
                        index
                    );
                    if (NOT_END(var + 1)) {
                        // reset index for the value part
                        index--;
                    }
                }
                else if (j == 1) {
                    Derelativize(
                        var,
                        ARR_AT(AS_ARRAY(series), index),
                        SPECIFIED // !!! it's a varlist
                    );
                }
                else {
                    // !!! Review this error (and this routine...)
                    DECLARE_LOCAL (key_name);
                    Init_Word(key_name, VAL_KEY_SPELLING(key));

                    fail (key_name);
                }
                j++;
            }
            else if (IS_VECTOR(data)) {
                Set_Vector_Value(var, series, index);
            }
            else if (IS_MAP(data)) {
                //
                // MAP! does not store RELVALs
                //
                REBVAL *val = KNOWN(ARR_AT(AS_ARRAY(series), index | 1));
                if (!IS_VOID(val)) {
                    if (j == 0) {
                        Derelativize(
                            var,
                            ARR_AT(AS_ARRAY(series), index & ~1),
                            SPECIFIED // maps always specified
                        );

                        if (IS_END(var + 1)) index++; // only words
                    }
                    else if (j == 1) {
                        Derelativize(
                            var,
                            ARR_AT(AS_ARRAY(series), index),
                            SPECIFIED // maps always specified
                        );
                    }
                    else {
                        // !!! Review this error (and this routine...)
                        DECLARE_LOCAL (key_name);
                        Init_Word(key_name, VAL_KEY_SPELLING(key));

                        fail (key_name);
                    }
                    j++;
                }
                else {
                    index += 2;
                    goto skip_hidden;
                }
            }
            else if (IS_BINARY(data)) {
                Init_Integer(var, (REBI64)(BIN_HEAD(series)[index]));
            }
            else if (IS_IMAGE(data)) {
                Set_Tuple_Pixel(BIN_AT(series, index), var);
            }
            else {
                assert(IS_STRING(data));
                Reset_Val_Header(var, REB_CHAR);
                VAL_CHAR(var) = GET_ANY_CHAR(series, index);
            }
            index++;
        }

        assert(IS_END(key) && IS_END(var));

        if (index == read_index) {
            // the word block has only set-words: for-each [a:] [1 2 3][]
            index++;
        }

        if (Do_At_Throws(D_OUT, body_copy, 0, SPECIFIED)) { // copy, specified
            if (!Catching_Break_Or_Continue(D_OUT, &stop)) {
                // A non-loop throw, we should be bubbling up
                threw = TRUE;
                break;
            }

            // Fall through and process the D_OUT (unset if no /WITH) for
            // this iteration.  `stop` flag will be checked ater that.
        }

        switch (mode) {
        case LOOP_FOR_EACH:
            // no action needed after body is run
            break;

        case LOOP_REMOVE_EACH:
            //
            // If body evaluates to FALSE, preserve the slot.  Do the same
            // for a void body, since that should have the same behavior as
            // a CONTINUE with no /WITH (which most sensibly does not do
            // a removal.)
            //
            if (IS_VOID(D_OUT) || IS_CONDITIONAL_FALSE(D_OUT)) {
                //
                // memory areas may overlap, so use memmove and not memcpy!
                //
                // !!! This seems a slow way to do it, but there's probably
                // not a lot that can be done as the series is expected to
                // be in a good state for the next iteration of the body. :-/
                //
                memmove(
                    SER_AT_RAW(SER_WIDE(series), series, write_index),
                    SER_AT_RAW(SER_WIDE(series), series, read_index),
                    (index - read_index) * SER_WIDE(series)
                );
                write_index += index - read_index;
            }
            break;

        case LOOP_MAP_EACH:
            // anything that's not void will be added to the result
            if (!IS_VOID(D_OUT))
                DS_PUSH(D_OUT);
            break;

        case LOOP_EVERY:
            if (IS_VOID(D_OUT)) {
                // Unsets "opt out" of the vote, as with ANY and ALL
            }
            else if (IS_CONDITIONAL_FALSE(D_OUT))
                Init_Blank(D_CELL); // at least one false means blank result
            else if (IS_END(D_CELL) || !IS_BLANK(D_CELL))
                Move_Value(D_CELL, D_OUT);
            break;
        default:
            assert(FALSE);
        }

        if (stop) {
            Init_Blank(D_OUT);
            break;
        }

skip_hidden: ;
    }

    if (IS_DATATYPE(data)) {
        //
        // If asked to enumerate a datatype, we allocated a temporary array
        // of all instances of that datatype.  It has to be freed.
        //
        DROP_GUARD_ARRAY_CONTENTS(AS_ARRAY(series));
        Free_Array(AS_ARRAY(series));
    }

    if (threw) {
        // a non-BREAK and non-CONTINUE throw overrides any other return
        // result we might give (generic THROW, RETURN, QUIT, etc.)

        if (mode == LOOP_MAP_EACH)
            DS_DROP_TO(dsp_orig);

        return R_OUT_IS_THROWN;
    }

    // Note: This finalization will be run by finished loops as well as
    // interrupted ones.  So:
    //
    //    map-each x [1 2 3 4] [if x = 3 [break]] => [1 2]
    //
    //    map-each x [1 2 3 4] [if x = 3 [break/with "A"]] => [1 2 "A"]
    //
    //    every x [1 3 6 12] [if x = 6 [break/with 7] even? x] => 7
    //
    // This provides the most flexibility in the loop's processing, because
    // "override" logic already exists in the form of CATCH & THROW.

#if !defined(NDEBUG)
    if (LEGACY(OPTIONS_BREAK_WITH_OVERRIDES)) {
        // In legacy R3-ALPHA, BREAK without a provided value did *not*
        // override the result.  It returned the partial results.
        if (stop && NOT_END(D_OUT))
            return R_OUT;
    }
#endif

    if (stop)
        return R_BLANK;

    switch (mode) {
    case LOOP_FOR_EACH:
        return R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY;

    case LOOP_REMOVE_EACH:
        // Remove hole (updates tail):
        if (write_index < index)
            Remove_Series(series, write_index, index - write_index);
        Init_Integer(D_OUT, index - write_index);
        return R_OUT;

    case LOOP_MAP_EACH:
        Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
        return R_OUT;

    case LOOP_EVERY:
        if (threw)
            return R_OUT_IS_THROWN;

        if (IS_END(D_CELL))
            return R_VOID; // all evaluations opted out

        Move_Value(D_OUT, D_CELL);
        return R_OUT; // should it be like R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY?

    default:
        assert(FALSE);
    }

    DEAD_END;
}


//
//  for: native [
//
//  {Evaluate a block over a range of values. (See also: REPEAT)}
//
//      return: [<opt> any-value!]
//      'word [word!]
//          "Variable to hold current value"
//      start [any-series! any-number!]
//          "Starting value"
//      end [any-series! any-number!]
//          "Ending value"
//      bump [any-number!]
//          "Amount to skip each time"
//      body [block!]
//          "Block to evaluate"
//  ]
//
REBNATIVE(for)
{
    INCLUDE_PARAMS_OF_FOR;

    REBCTX *context;
    REBARR *body_copy = Copy_Body_Deep_Bound_To_New_Context(
        &context,
        ARG(word),
        ARG(body)
    );
    Init_Object(ARG(word), context); // keep GC safe
    Init_Block(ARG(body), body_copy); // keep GC safe

    REBVAL *var = CTX_VAR(context, 1);

    if (
        IS_INTEGER(ARG(start))
        && IS_INTEGER(ARG(end))
        && IS_INTEGER(ARG(bump))
    ) {
        return Loop_Integer_Common(
            D_OUT,
            var,
            body_copy,
            VAL_INT64(ARG(start)),
            IS_DECIMAL(ARG(end))
                ? (REBI64)VAL_DECIMAL(ARG(end))
                : VAL_INT64(ARG(end)),
            VAL_INT64(ARG(bump))
        );
    }
    else if (ANY_SERIES(ARG(start))) {
        if (ANY_SERIES(ARG(end))) {
            return Loop_Series_Common(
                D_OUT,
                var,
                body_copy,
                ARG(start),
                VAL_INDEX(ARG(end)),
                Int32(ARG(bump))
            );
        }
        else {
            return Loop_Series_Common(
                D_OUT,
                var,
                body_copy,
                ARG(start),
                Int32s(ARG(end), 1) - 1,
                Int32(ARG(bump))
            );
        }
    }

    return Loop_Number_Common(
        D_OUT, var, body_copy, ARG(start), ARG(end), ARG(bump)
    );

}


//
//  for-skip: native [
//
//  "Evaluates a block for periodic values in a series"
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value, will also be void if never run}
//      'word [word! blank!]
//          "Word that refers to the series, set to positions in the series"
//      skip [integer!]
//          "Number of positions to skip each time"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(for_skip)
//
// !!! Should this fail on 0?  It could be that the loop will break for some
// other reason, and the author didn't wish to special case to rule out zero...
// generality may dictate allowing it.
{
    INCLUDE_PARAMS_OF_FOR_SKIP;

    REBVAL *word = ARG(word);

    // Though we can only iterate on a series, BLANK! is used as a way of
    // opting out.  This could be useful, e.g. `for-next x (any ...) [...]`
    //
    if (IS_BLANK(word))
        return R_VOID;

    REBVAL *var = Get_Mutable_Var_May_Fail(word, SPECIFIED);

    if (NOT(ANY_SERIES(var)))
        fail (var);

    REBINT skip = Int32(ARG(skip));

    // Save the starting var value, assume `word` is a GC protected slot
    //
    Move_Value(word, var);

    // Starting location when past end with negative skip:
    //
    if (skip < 0 && VAL_INDEX(var) >= VAL_LEN_HEAD(var))
        VAL_INDEX(var) = VAL_LEN_HEAD(var) + skip;

    while (TRUE) {
        REBINT len = VAL_LEN_HEAD(var); // VAL_LEN_HEAD() always >= 0
        REBINT index = VAL_INDEX(var); // (may have been set to < 0 below)

        if (index < 0) break;
        if (index >= len) {
            if (skip >= 0) break;
            index = len + skip; // negative
            if (index < 0) break;
            VAL_INDEX(var) = index;
        }

        if (Do_Any_Array_At_Throws(D_OUT, ARG(body))) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop) {
                    Move_Value(var, word);
                    return R_BLANK;
                }
                goto next_iteration;
            }
            return R_OUT_IS_THROWN;
        }

    next_iteration:
        //
        // !!! The code in the body is allowed to modify the var.  However,
        // R3-Alpha checked to make sure that the type of the var did not
        // change.  This seemed like an arbitrary limitation and Ren-C
        // removed it, only checking that it's a series.
        //
        if (IS_BLANK(var))
            return R_OUT;

        if (NOT(ANY_SERIES(var)))
            fail (var);

        VAL_INDEX(var) += skip;
    }

    Move_Value(var, word);
    return R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY;
}


//
//  forever: native [
//
//  "Evaluates a block endlessly, until an interrupting throw/error/break."
//
//      return: [<opt> any-value!]
//          {Void if plain BREAK, or arbitrary value using BREAK/WITH}
//      body [block! function!]
//          "Block or function to evaluate each time"
//  ]
//
REBNATIVE(forever)
{
    INCLUDE_PARAMS_OF_FOREVER;

    do {
        const REBOOL only = FALSE;
        if (Run_Branch_Throws(D_OUT, ARG(body), only)) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop)
                    return R_BLANK;
                continue;
            }
            return R_OUT_IS_THROWN;
        }
    } while (TRUE);

    DEAD_END;
}


//
//  for-each: native [
//
//  "Evaluates a block for each value(s) in a series."
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value, will also be void if never run}
//      'vars [word! block!]
//          "Word or block of words to set each time (local)"
//      data [any-series! any-context! map! blank! datatype!]
//          "The series to traverse"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(for_each)
{
    return Loop_Each(frame_, LOOP_FOR_EACH);
}


//
//  remove-each: native [
//
//  {Removes values for each block that returns true; returns removal count.}
//
//      'vars [word! block!]
//          "Word or block of words to set each time (local)"
//      data [any-series!]
//          "The series to traverse (modified)"
//      body [block!]
//          "Block to evaluate (return TRUE to remove)"
//  ]
//
REBNATIVE(remove_each)
{
    return Loop_Each(frame_, LOOP_REMOVE_EACH);
}


//
//  map-each: native [
//
//  {Evaluate a block for each value(s) in a series and collect as a block.}
//
//      return: [block!]
//          {Collected block (BREAK/WITH can add a final result to block)}
//      'vars [word! block!]
//          "Word or block of words to set each time (local)"
//      data [block! vector!]
//          "The series to traverse"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(map_each)
{
    return Loop_Each(frame_, LOOP_MAP_EACH);
}


//
//  every: native [
//
//  {Returns last TRUE? value if evaluating a block over a series is all TRUE?}
//
//      return: [<opt> any-value!]
//          {TRUE or BLANK! collected, or BREAK value, TRUE if never run.}
//      'vars [word! block!]
//          "Word or block of words to set each time (local)"
//      data [any-series! any-context! map! blank! datatype!]
//          "The series to traverse"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(every)
{
    return Loop_Each(frame_, LOOP_EVERY);
}


//
//  loop: native [
//
//  "Evaluates a block a specified number of times."
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value, will also be void if never run}
//      count [any-number! logic! blank!]
//          "Repetitions (true loops infinitely, FALSE? doesn't run)"
//      body [block! function!]
//          "Block to evaluate or function to run (may be a BRANCHER)."
//  ]
//
REBNATIVE(loop)
{
    INCLUDE_PARAMS_OF_LOOP;

    REBI64 count;

    if (IS_CONDITIONAL_FALSE(ARG(count))) {
        //
        // A NONE! or LOGIC! FALSE means don't run the loop at all.
        //
        return R_VOID;
    }

    if (IS_LOGIC(ARG(count))) {
        //
        // (Must be TRUE).  Run forever.  As a micro-optimization we don't
        // complicate the condition checking in the loop, but seed with a
        // *very* large integer.  In the off chance that we exhaust it, the
        // code jumps up here, re-seeds it, and loops again.
        //
    restart:
        count = MAX_I64;
    }
    else
        count = Int64(ARG(count));

    for (; count > 0; count--) {
        const REBOOL only = FALSE;
        if (Run_Branch_Throws(D_OUT, ARG(body), only)) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop)
                    return R_BLANK;
                continue;
            }
            return R_OUT_IS_THROWN;
        }
    }

    if (IS_LOGIC(ARG(count))) {
        //
        // Rare case, "infinite" loop exhausted MAX_I64 steps...
        //
        goto restart;
    }

    return R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY;
}


//
//  repeat: native [
//
//  {Evaluates a block a number of times or over a series.}
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value, will also be void if never run}
//      'word [word!]
//          "Word to set each time"
//      value [any-number! any-series! blank!]
//          "Maximum number or series to traverse"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(repeat)
{
    INCLUDE_PARAMS_OF_REPEAT;

    REBVAL *value = ARG(value);

    if (IS_BLANK(value))
        return R_VOID;

    if (IS_DECIMAL(value) || IS_PERCENT(value))
        Init_Integer(value, Int64(value));

    REBCTX *context;
    REBARR *copy = Copy_Body_Deep_Bound_To_New_Context(
        &context,
        ARG(word),
        ARG(body)
    );

    REBVAL *var = CTX_VAR(context, 1);

    Init_Object(ARG(word), context); // keep GC safe
    Init_Block(ARG(body), copy); // keep GC safe

    if (ANY_SERIES(value)) {
        return Loop_Series_Common(
            D_OUT, var, copy, value, VAL_LEN_HEAD(value) - 1, 1
        );
    }

    assert(IS_INTEGER(value));

    return Loop_Integer_Common(D_OUT, var, copy, 1, VAL_INT64(value), 1);
}


// Common code for LOOP-WHILE & LOOP-UNTIL (same frame param layout)
//
inline static REB_R Loop_While_Until_Core(REBFRM *frame_, REBOOL trigger)
{
    INCLUDE_PARAMS_OF_LOOP_WHILE;

    do {
    skip_check:;

        const REBOOL only = FALSE;
        if (Run_Branch_Throws(D_OUT, ARG(body), only)) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop)
                    return R_BLANK;

                // LOOP-UNTIL and LOOP-WITH follow the precedent that the way
                // a CONTINUE/WITH works is to act as if the loop body
                // returned the value passed to the WITH...and that a CONTINUE
                // lacking a WITH acts as if the body returned a void.
                //
                // Since the condition and body are the same in this case,
                // the implications are a little strange (though logical).
                // CONTINUE/WITH FALSE will break a LOOP-WHILE, and
                // CONTINUE/WITH TRUE breaks a LOOP-UNTIL.
                //
                if (IS_VOID(D_OUT))
                    goto skip_check;

                goto perform_check;
            }
            return R_OUT_IS_THROWN;
        }

        // Since CONTINUE acts like reaching the end of the loop body with a
        // void, the logical consequence is that reaching the end of *either*
        // a LOOP-WHILE or a LOOP-UNTIL with a void just keeps going.  This
        // means that `loop-until [print "hi"]` and `loop-while [print "hi"]`
        // are both infinite loops.
        //
        if (IS_VOID(D_OUT))
            goto skip_check;

    perform_check:;
    } while (IS_CONDITIONAL_TRUE(D_OUT) == trigger);

    // Though LOOP-UNTIL will always have a truthy result, LOOP-WHILE never
    // will, and needs to have the result overwritten with something TRUE?
    // so BAR! is used.
    //
    if (trigger == TRUE)
        return R_BAR;

    assert(IS_CONDITIONAL_TRUE(D_OUT));
    return R_OUT;
}


//
//  loop-while: native [
//
//  "Evaluates a block while it is TRUE?"
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value.}
//      body [block! function!]
//  ]
//
REBNATIVE(loop_while)
{
    return Loop_While_Until_Core(frame_, TRUE);
}


//
//  loop-until: native [
//
//  "Evaluates a block until it is TRUE?"
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value.}
//      body [block! function!]
//  ]
//
REBNATIVE(loop_until)
//
// !!! This function used to be called just UNTIL, but Ren-C retakes that for
// the arity-2 complement to WHILE.
{
    return Loop_While_Until_Core(frame_, FALSE);
}


// Common code for WHILE & UNTIL (same frame param layout)
//
inline static REB_R While_Until_Core(REBFRM *frame_, REBOOL trigger)
{
    INCLUDE_PARAMS_OF_WHILE;

    const REBOOL only = FALSE; // while/only [cond] [body] is meaningless

    assert(IS_END(D_OUT)); // guaranteed by the evaluator

    do {
        if (Run_Branch_Throws(D_CELL, ARG(condition), only)) {
            //
            // A while loop should only look for breaks and continues in its
            // body, not in its condition.  So `while [break] []` is a
            // request to break the enclosing loop (or error if there is
            // nothing to catch that break).  Hence we bubble up the throw.
            //
            Move_Value(D_OUT, D_CELL);
            return R_OUT_IS_THROWN;
        }

        if (IS_VOID(D_CELL))
            fail (Error_No_Return_Raw());

        if (IS_CONDITIONAL_TRUE(D_CELL) != trigger) {
            //
            // Successfully completed loops aren't allowed to return a
            // FALSE? value, so they get BAR! as a truthy-result if the
            // loop body ever ran... or void if it never did.
            //
            return R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY;
        }

        if (Run_Branch_Throws(D_OUT, ARG(body), only)) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop)
                    return R_BLANK;

                continue;
            }
            return R_OUT_IS_THROWN;
        }

    } while (TRUE);
}


//
//  while: native [
//
//  {While a condition block is TRUE?, evaluates another block.}
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value, will also be void if never run}
//      condition [block! function!]
//      body [block! function!]
//  ]
//
REBNATIVE(while)
{
    return While_Until_Core(frame_, TRUE);
}


//
//  until: native [
//
//  {Until a condition block is TRUE?, evaluates another block.}
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value, will also be void if never run}
//      condition [block! function!]
//      body [block! function!]
//  ]
//
REBNATIVE(until)
//
// !!! This arity-2 form of UNTIL is aliased to UNTIL-2 in the bootstrap, and
// UNTIL is left undefined.
{
    return While_Until_Core(frame_, FALSE);
}
