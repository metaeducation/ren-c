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
// Copyright 2012-2016 Rebol Open Source Contributors
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
        CATCH_THROWN(val, val); // will be unset if no /WITH was used
        return TRUE;
    }

    if (VAL_FUNC_DISPATCHER(val) == &N_continue) {
        *stop = FALSE; // was CONTINUE or CONTINUE/WITH
        CATCH_THROWN(val, val); // will be unset if no /WITH was used
        return TRUE;
    }

    // Else: Let all other thrown values bubble up.
    return FALSE;
}


//
//  Init_Loop: C
// 
// Initialize standard for loops (copy block, make context, bind).
// Spec: WORD or [WORD ...]
// 
// Note that because we are copying the block in order to rebind it, the
// ensuing loop code will `Do_At_Throws(out, body, 0);`.  Starting at
// zero is correct because the duplicate body has already had the
// items before its VAL_INDEX() omitted.
//
static REBARR *Init_Loop(
    REBCTX **context_out,
    const REBVAL *spec,
    REBVAL *body
) {
    REBCTX *context;
    REBINT len;
    REBVAL *key;
    REBVAL *var;
    REBARR *body_out;

    const RELVAL *item;
    REBCTX *specifier;

    assert(IS_BLOCK(body));

    // For :WORD format, get the var's value:
    if (IS_GET_WORD(spec))
        spec = GET_OPT_VAR_MAY_FAIL(spec, SPECIFIED);

    // Hand-make a CONTEXT (done for for speed):
    len = IS_BLOCK(spec) ? VAL_LEN_AT(spec) : 1;
    if (len == 0) fail (Error_Invalid_Arg(spec));

    context = Alloc_Context(len);
    SET_ARRAY_LEN(CTX_VARLIST(context), len + 1);
    SET_ARRAY_LEN(CTX_KEYLIST(context), len + 1);

    VAL_RESET_HEADER(CTX_VALUE(context), REB_OBJECT);
    CTX_VALUE(context)->extra.binding = NULL;

    // Setup for loop:
    key = CTX_KEYS_HEAD(context);
    var = CTX_VARS_HEAD(context);

    if (IS_BLOCK(spec)) {
        item = VAL_ARRAY_AT(spec);
        specifier = VAL_SPECIFIER(spec);
    }
    else {
        item = spec;
        specifier = SPECIFIED;
    }

    // Optimally create the FOREACH context:
    while (len-- > 0) {
        if (!IS_WORD(item) && !IS_SET_WORD(item)) {
            FREE_CONTEXT(context);
            fail (Error_Invalid_Arg_Core(item, specifier));
        }

        Val_Init_Typeset(key, ALL_64, VAL_WORD_CASED(item));
        key++;

        // !!! This should likely use the unset-defaulting in Ren-C with the
        // legacy fallback to NONE!
        //
        SET_BLANK(var);
        var++;

        ++item;
    }

    SET_END(key);
    SET_END(var);

    body_out = Copy_Array_At_Deep_Managed(
        VAL_ARRAY(body), VAL_INDEX(body), VAL_SPECIFIER(body)
    );
    Bind_Values_Deep(ARR_HEAD(body_out), context);

    *context_out = context;

    return body_out;
}


//
//  Loop_Series_Throws: C
//
static REBOOL Loop_Series_Throws(
    REBVAL *out,
    REBVAL *var,
    REBARR *body,
    REBVAL *start,
    REBINT ei,
    REBINT ii
) {
    REBINT si = VAL_INDEX(start);
    enum Reb_Kind type = VAL_TYPE(start);

    *var = *start;

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
                if (stop) break;
                goto next_iteration;
            }
            return TRUE;
        }

    next_iteration:
        if (VAL_TYPE(var) != type) fail (Error(RE_INVALID_TYPE, var));
        si = VAL_INDEX(var);
    }

    return FALSE;
}


//
//  Loop_Integer_Throws: C
//
static REBOOL Loop_Integer_Throws(
    REBVAL *out,
    REBVAL *var,
    REBARR *body,
    REBI64 start,
    REBI64 end,
    REBI64 incr
) {
    VAL_RESET_HEADER(var, REB_INTEGER);

    while ((incr > 0) ? start <= end : start >= end) {
        VAL_INT64(var) = start;

        if (Do_At_Throws(out, body, 0, SPECIFIED)) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(out, &stop)) {
                if (stop) break;
                goto next_iteration;
            }
            return TRUE;
        }

    next_iteration:
        if (!IS_INTEGER(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        start = VAL_INT64(var);

        if (REB_I64_ADD_OF(start, incr, &start))
            fail (Error(RE_OVERFLOW));
    }

    return FALSE;
}


//
//  Loop_Number_Throws: C
//
static REBOOL Loop_Number_Throws(
    REBVAL *out,
    REBVAL *var,
    REBARR *body,
    REBVAL *start,
    REBVAL *end,
    REBVAL *incr
) {
    REBDEC s;
    REBDEC e;
    REBDEC i;

    if (IS_INTEGER(start))
        s = cast(REBDEC, VAL_INT64(start));
    else if (IS_DECIMAL(start) || IS_PERCENT(start))
        s = VAL_DECIMAL(start);
    else
        fail (Error_Invalid_Arg(start));

    if (IS_INTEGER(end))
        e = cast(REBDEC, VAL_INT64(end));
    else if (IS_DECIMAL(end) || IS_PERCENT(end))
        e = VAL_DECIMAL(end);
    else
        fail (Error_Invalid_Arg(end));

    if (IS_INTEGER(incr))
        i = cast(REBDEC, VAL_INT64(incr));
    else if (IS_DECIMAL(incr) || IS_PERCENT(incr))
        i = VAL_DECIMAL(incr);
    else
        fail (Error_Invalid_Arg(incr));

    VAL_RESET_HEADER(var, REB_DECIMAL);

    for (; (i > 0.0) ? s <= e : s >= e; s += i) {
        VAL_DECIMAL(var) = s;

        if (Do_At_Throws(out, body, 0, SPECIFIED)) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(out, &stop)) {
                if (stop) break;
                goto next_iteration;
            }
            return TRUE;
        }

    next_iteration:
        if (!IS_DECIMAL(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        s = VAL_DECIMAL(var);
    }

    return FALSE;
}


//
//  Loop_Skip: C
//
// Provides the core implementation behind FOR-NEXT, FOR-BACK, and FOR-SKIP
//
static REB_R Loop_Skip(
    REBVAL *out,
    REBVAL *word, // MODIFIED - Must be GC safe!
    REBINT skip,
    REBVAL *body // Must be GC safe!
) {
    REBOOL q = FALSE; // !!! currently /? option not passed in

    REBVAL *var = GET_MUTABLE_VAR_MAY_FAIL(word, SPECIFIED);

    // Though we can only iterate on a series, BLANK! is used as a way of
    // opting out.  This could be useful, e.g. `for-next x (any ...) [...]`
    //
    if (IS_BLANK(var))
        return R_OUT_Q(q);

    if (!ANY_SERIES(var))
        fail (Error_Invalid_Arg(var));

    // Save the starting var value, assume `word` is a GC protected slot
    //
    *word = *var;

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

        if (DO_VAL_ARRAY_AT_THROWS(out, body)) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(out, &stop)) {
                if (stop) goto restore_var_and_return;

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
            return R_OUT_Q(q);

        if (!ANY_SERIES(var))
            fail (Error_Invalid_Arg(var));

        VAL_INDEX(var) += skip;
    }

restore_var_and_return:
    *var = *word;
    return R_OUT_Q(q);
}


//
//  Loop_Each: C
// 
// Common implementation code of FOR-EACH, REMOVE-EACH, MAP-EACH,
// and EVERY.
//
static REB_R Loop_Each(struct Reb_Frame *frame_, LOOP_MODE mode)
{
    PARAM(1, vars);
    PARAM(2, data);
    PARAM(3, body);

    // `vars` context (plus var and key for iterating over it)
    //
    REBCTX *context;

    // `data` series and index (where data is the series/object/map/etc. that
    // the loop is iterating over)
    //
    REBVAL *data_value = ARG(data);
    REBSER *series;
    REBINT index;   // !!!! should this be REBCNT?

    // The body block must be bound to the loop variables, and the loops do
    // not mutate them directly.
    //
    REBARR *body_copy;

    REBARR *mapped; // output block of mapped-to values (needed for MAP-EACH)

    REBINT tail;
    REBINT write_index;
    REBINT read_index;
    REBVAL *ds;

    REBOOL stop = FALSE;
    REBOOL every_true = TRUE; // need due to OPTIONS_NONE_INSTEAD_OF_VOIDS
    REBOOL threw = FALSE; // did a non-BREAK or non-CONTINUE throw occur

    REBOOL q = FALSE; // !!! Currently, /? option is not passed in

    if (mode == LOOP_EVERY)
        SET_TRUE(D_OUT); // Default output is TRUE, to match ALL MAP-EACH

    if (IS_BLANK(data_value) || IS_VOID(data_value)) return R_OUT;

    body_copy = Init_Loop(&context, ARG(vars), ARG(body));
    Val_Init_Object(ARG(vars), context); // keep GC safe
    Val_Init_Block(ARG(body), body_copy); // keep GC safe

    if (mode == LOOP_MAP_EACH) {
        // Must be managed *and* saved...because we are accumulating results
        // into it, and those results must be protected from GC

        // !!! This means we cannot Free_Series in case of a BREAK, we
        // have to leave it to the GC.  Is there a safe and efficient way
        // to allow inserting the managed values into a single-deep
        // unmanaged series if we *promise* not to go deeper?

        mapped = Make_Array(VAL_LEN_AT(data_value));
        MANAGE_ARRAY(mapped);
        PUSH_GUARD_ARRAY(mapped);
    }

    // Get series info:
    if (ANY_CONTEXT(data_value)) {
        series = ARR_SERIES(CTX_VARLIST(VAL_CONTEXT(data_value)));
        index = 1;
        //if (context->tail > 3)
        //  fail (Error_Invalid_Arg(CTX_KEY(context, 3)));
    }
    else if (IS_MAP(data_value)) {
        series = VAL_SERIES(data_value);
        index = 0;
        //if (context->tail > 3)
        //  fail (Error_Invalid_Arg(CTX_KEY(context, 3)));
    }
    else {
        series = VAL_SERIES(data_value);
        index  = VAL_INDEX(data_value);
        if (index >= cast(REBINT, SER_LEN(series))) {
            if (mode == LOOP_REMOVE_EACH) {
                SET_INTEGER(D_OUT, 0);
            }
            else if (mode == LOOP_MAP_EACH) {
                DROP_GUARD_ARRAY(mapped);
                Val_Init_Block(D_OUT, mapped);
            }
            return R_OUT_Q(q);
        }
    }

    write_index = index;

    // Iterate over each value in the data series block:
    while (index < (tail = SER_LEN(series))) {
        REBCNT i;
        REBCNT j = 0;

        REBVAL *key = CTX_KEY(context, 1);
        REBVAL *var = CTX_VAR(context, 1);

        read_index = index;  // remember starting spot

        // Set the FOREACH loop variables from the series:
        for (i = 1; !IS_END(key); i++, key++, var++) {

            if (index >= tail) {
                SET_BLANK(var);
                continue;
            }

            if (ANY_ARRAY(data_value)) {
                COPY_VALUE(
                    var,
                    ARR_AT(AS_ARRAY(series), index),
                    VAL_SPECIFIER(data_value) // !!! always matches series?
                );
            }
            else if (ANY_CONTEXT(data_value)) {
                if (GET_VAL_FLAG(
                    VAL_CONTEXT_KEY(data_value, index), TYPESET_FLAG_HIDDEN
                )) {
                    // Do not evaluate this iteration
                    index++;
                    goto skip_hidden;
                }

                // Alternate between word and value parts of object:
                if (j == 0) {
                    Val_Init_Word_Bound(
                        var,
                        REB_WORD,
                        CTX_KEY_CASED(VAL_CONTEXT(data_value), index),
                        AS_CONTEXT(series),
                        index
                    );
                    if (NOT_END(var + 1)) {
                        // reset index for the value part
                        index--;
                    }
                }
                else if (j == 1) {
                    COPY_VALUE(
                        var,
                        ARR_AT(AS_ARRAY(series), index),
                        SPECIFIED // !!! it's a varlist
                    );
                }
                else {
                    // !!! Review this error (and this routine...)
                    REBVAL key_name;
                    Val_Init_Word(&key_name, REB_WORD, VAL_KEY_CASED(key));

                    fail (Error_Invalid_Arg(&key_name));
                }
                j++;
            }
            else if (IS_VECTOR(data_value)) {
                Set_Vector_Value(var, series, index);
            }
            else if (IS_MAP(data_value)) {
                //
                // MAP! does not store RELVALs
                //
                REBVAL *val = KNOWN(ARR_AT(AS_ARRAY(series), index | 1));
                if (!IS_VOID(val)) {
                    if (j == 0) {
                        COPY_VALUE(
                            var,
                            ARR_AT(AS_ARRAY(series), index & ~1),
                            SPECIFIED // maps always specified
                        );

                        if (IS_END(var + 1)) index++; // only words
                    }
                    else if (j == 1) {
                        COPY_VALUE(
                            var,
                            ARR_AT(AS_ARRAY(series), index),
                            SPECIFIED // maps always specified
                        );
                    }
                    else {
                        // !!! Review this error (and this routine...)
                        REBVAL key_name;
                        Val_Init_Word(&key_name, REB_WORD, VAL_KEY_CASED(key));

                        fail (Error_Invalid_Arg(&key_name));
                    }
                    j++;
                }
                else {
                    index += 2;
                    goto skip_hidden;
                }
            }
            else { // A string or binary
                if (IS_BINARY(data_value)) {
                    SET_INTEGER(var, (REBI64)(BIN_HEAD(series)[index]));
                }
                else if (IS_IMAGE(data_value)) {
                    Set_Tuple_Pixel(BIN_AT(series, index), var);
                }
                else {
                    VAL_RESET_HEADER(var, REB_CHAR);
                    VAL_CHAR(var) = GET_ANY_CHAR(series, index);
                }
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
            // If FALSE return (or unset), copy values to the write location
            if (IS_CONDITIONAL_FALSE(D_OUT) || IS_VOID(D_OUT)) {
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
            if (!IS_VOID(D_OUT)) Append_Value(mapped, D_OUT);
            break;
        case LOOP_EVERY:
            if (IS_VOID(D_OUT)) {
                // Unsets "opt out" of the vote, as with ANY and ALL
            }
            else
                every_true = LOGICAL(every_true && IS_CONDITIONAL_TRUE(D_OUT));
            break;
        default:
            assert(FALSE);
        }

        if (stop) break;

skip_hidden: ;
    }

    if (mode == LOOP_MAP_EACH) DROP_GUARD_ARRAY(mapped);

    if (threw) {
        // a non-BREAK and non-CONTINUE throw overrides any other return
        // result we might give (generic THROW, RETURN, QUIT, etc.)

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
        if (stop && !IS_END(D_OUT))
            return R_OUT;
    }
#endif

    switch (mode) {
    case LOOP_FOR_EACH:
        // Returns last body result or /WITH of BREAK (or the /WITH of a
        // CONTINUE if it turned out to be the last iteration)
        return R_OUT_Q(q);

    case LOOP_REMOVE_EACH:
        // Remove hole (updates tail):
        if (write_index < index)
            Remove_Series(series, write_index, index - write_index);
        SET_INTEGER(D_OUT, index - write_index);
        return R_OUT_Q(q);

    case LOOP_MAP_EACH:
        Val_Init_Block(D_OUT, mapped);
        return R_OUT_Q(q);

    case LOOP_EVERY:
        if (threw) return R_OUT_IS_THROWN;

        // Result is the cumulative TRUE? state of all the input (with any
        // unsets taken out of the consideration).  The last TRUE? input
        // if all valid and NONE! otherwise.  (Like ALL.)
        if (!every_true) return R_BLANK;

        // We want to act like `ALL MAP-EACH ...`, hence we effectively ignore
        // unsets and return TRUE if the last evaluation leaves an unset.
        if (IS_VOID(D_OUT)) return R_TRUE;

        return R_OUT;

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
//      'word [word!] "Variable to hold current value"
//      start [any-series! any-number!] "Starting value"
//      end [any-series! any-number!] "Ending value"
//      bump [any-number!] "Amount to skip each time"
//      body [block!] "Block to evaluate"
//  ]
//
REBNATIVE(for)
{
    PARAM(1, word);
    PARAM(2, start);
    PARAM(3, end);
    PARAM(4, bump);
    PARAM(5, body);

    REBOOL q = FALSE; // !!! No /q refinement yet, and FOR may be going away

    REBARR *body_copy;
    REBCTX *context;
    REBVAL *var;

    // Copy body block, make a context, bind loop var to it:
    body_copy = Init_Loop(&context, ARG(word), ARG(body));
    var = CTX_VAR(context, 1); // safe: not on stack
    Val_Init_Object(ARG(word), context); // keep GC safe
    Val_Init_Block(ARG(body), body_copy); // keep GC safe

    if (
        IS_INTEGER(ARG(start))
        && IS_INTEGER(ARG(end))
        && IS_INTEGER(ARG(bump))
    ) {
        if (Loop_Integer_Throws(
            D_OUT,
            var,
            body_copy,
            VAL_INT64(ARG(start)),
            IS_DECIMAL(ARG(end))
                ? (REBI64)VAL_DECIMAL(ARG(end))
                : VAL_INT64(ARG(end)),
            VAL_INT64(ARG(bump))
        )) {
            return R_OUT_IS_THROWN;
        }
    }
    else if (ANY_SERIES(ARG(start))) {
        if (ANY_SERIES(ARG(end))) {
            if (Loop_Series_Throws(
                D_OUT,
                var,
                body_copy,
                ARG(start),
                VAL_INDEX(ARG(end)),
                Int32(ARG(bump))
            )) {
                return R_OUT_IS_THROWN;
            }
        }
        else {
            if (Loop_Series_Throws(
                D_OUT,
                var,
                body_copy,
                ARG(start),
                Int32s(ARG(end), 1) - 1,
                Int32(ARG(bump))
            )) {
                return R_OUT_IS_THROWN;
            }
        }
    }
    else {
        if (Loop_Number_Throws(
            D_OUT, var, body_copy, ARG(start), ARG(end), ARG(bump)
        )) {
            return R_OUT_IS_THROWN;
        }
    }

    return R_OUT_Q(q);
}


//
//  for-next: native [
//  
//  "Evaluates a block for each position until the end, using NEXT to skip"
//  
//      'word [word!] 
//          "Word that refers to the series, set to positions in the series"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(for_next)
{
    PARAM(1, word);
    PARAM(2, body);

    return Loop_Skip(D_OUT, ARG(word), 1, ARG(body));
}


//
//  for-back: native [
//
//  "Evaluates a block for each position until the start, using BACK to skip"
//
//      'word [word!]
//          "Word that refers to the series, set to positions in the series"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(for_back)
{
    PARAM(1, word);
    PARAM(2, body);

    return Loop_Skip(D_OUT, ARG(word), -1, ARG(body));
}


//
//  for-skip: native [
//  
//  "Evaluates a block for periodic values in a series"
//  
//      'word [word!] 
//          "Word that refers to the series, set to positions in the series"
//      skip [integer!]
//          "Number of positions to skip each time"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(for_skip)
{
    PARAM(1, word);
    PARAM(2, skip);
    PARAM(3, body);

    // !!! Should this fail on 0?  It could be that the loop will break for
    // some other reason, and the author didn't wish to special case to
    // rule out zero... generality may dictate allowing it.

    return Loop_Skip(D_OUT, ARG(word), Int32(ARG(skip)), ARG(body));
}


//
//  forever: native [
//  
//  "Evaluates a block endlessly."
//  
//      block [block!] "Block to evaluate each time"
//  ]
//
REBNATIVE(forever)
{
    PARAM(1, block);

    do {
        if (DO_VAL_ARRAY_AT_THROWS(D_OUT, ARG(block))) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop) return R_OUT;
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
//      'word [word! block!]
//          "Word or block of words to set each time (local)"
//      data [any-series! any-context! map! blank!]
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
//      'word [word! block!] "Word or block of words to set each time (local)"
//      data [any-series!] "The series to traverse (modified)"
//      body [block!] "Block to evaluate (return TRUE to remove)"
//  ]
//
REBNATIVE(remove_each)
{
    return Loop_Each(frame_, LOOP_REMOVE_EACH);
}


//
//  map-each: native [
//  
//  {Evaluates a block for each value(s) in a series and returns them as a block.}
//  
//      'word [word! block!] "Word or block of words to set each time (local)"
//      data [block! vector!] "The series to traverse"
//      body [block!] "Block to evaluate each time"
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
//      'word [word! block!]
//          "Word or block of words to set each time (local)"
//      data [any-series! any-context! map! blank!]
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
//      count [any-number! logic! blank!]
//          "Repetitions (true loops infinitely, FALSE? doesn't run)"
//      block [block!]
//          "Block to evaluate"
//  ]
//
REBNATIVE(loop)
{
    PARAM(1, count);
    PARAM(2, block);

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
        if (DO_VAL_ARRAY_AT_THROWS(D_OUT, ARG(block))) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop) return R_OUT;
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

    if (IS_END(D_OUT))
        return R_VOID;

    return R_OUT;
}


//
//  repeat: native [
//  
//  {Evaluates a block a number of times or over a series.}
//  
//      'word [word!] "Word to set each time"
//      value [any-number! any-series! blank!]
//      "Maximum number or series to traverse"
//      body [block!] "Block to evaluate each time"
//  ]
//
REBNATIVE(repeat)
{
    REBARR *body;
    REBCTX *context;
    REBVAL *var;
    REBVAL *count = D_ARG(2);

    REBOOL q = FALSE; // !!! No /? passed in at the moment

    if (IS_BLANK(count))
        return q ? R_FALSE : R_VOID;

    if (IS_DECIMAL(count) || IS_PERCENT(count)) {
        REBI64 i64 = Int64(count);
        SET_INTEGER(count, i64); // macro! don't get-and-set in same line!
    }

    body = Init_Loop(&context, D_ARG(1), D_ARG(3));
    var = CTX_VAR(context, 1); // safe: not on stack
    Val_Init_Object(D_ARG(1), context); // keep GC safe
    Val_Init_Block(D_ARG(3), body); // keep GC safe

    if (ANY_SERIES(count)) {
        if (Loop_Series_Throws(
            D_OUT, var, body, count, VAL_LEN_HEAD(count) - 1, 1
        )) {
            return R_OUT_IS_THROWN;
        }

        return R_OUT_Q(q);
    }
    else if (IS_INTEGER(count)) {
        if (Loop_Integer_Throws(D_OUT, var, body, 1, VAL_INT64(count), 1))
            return R_OUT_IS_THROWN;

        return R_OUT_Q(q);
    }

    return q ? R_FALSE : R_VOID;
}


//
//  until: native [
//  
//  "Evaluates a block until it is TRUE. "
//  
//      block [block!]
//  ]
//
REBNATIVE(until)
{
    PARAM(1, block);

    do {
    skip_check:
        if (DO_VAL_ARRAY_AT_THROWS(D_OUT, ARG(block))) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop) return R_OUT;

                // UNTIL is unique because when you get a CONTINUE/WITH, the
                // usual rule of the /WITH being "what the body would have
                // returned" becomes also the condition.  It's a very poor
                // expression of breaking an until to say CONTINUE/WITH TRUE,
                // as BREAK/WITH TRUE says it much better.
                //
                if (!IS_VOID(D_OUT))
                    fail (Error(RE_BREAK_NOT_CONTINUE));

                goto skip_check;
            }
            return R_OUT_IS_THROWN;
        }

        if (IS_VOID(D_OUT)) fail (Error(RE_NO_RETURN));

    } while (IS_CONDITIONAL_FALSE(D_OUT));

    return R_OUT;
}


//
//  while: native [
//  
//  {While a condition block is TRUE?, evaluates another block.}
//  
//      condition [block!]
//      body [block!]
//      /?
//          "Instead of last body result, return LOGIC! of if body ever ran"
//  ]
//
REBNATIVE(while)
{
    PARAM(1, condition);
    PARAM(2, body);
    REFINE(3, q_reused);

    REBOOL q = VAL_LOGIC(ARG(q_reused));
    REBVAL *temp = ARG(q_reused); // cell-sized slot that is also GC-safe

    do {
        if (DO_VAL_ARRAY_AT_THROWS(temp, ARG(condition))) {
            //
            // A while loop should only look for breaks and continues in its
            // body, not in its condition.  So `while [break] []` is a
            // request to break the enclosing loop (or error if there is
            // nothing to catch that break).  Hence we bubble up the throw.
            //
            *D_OUT = *temp;
            return R_OUT_IS_THROWN;
        }

        if (IS_VOID(temp))
            fail (Error(RE_NO_RETURN));

        if (IS_CONDITIONAL_FALSE(temp))
            return R_OUT_Q(q);

        // If this line runs, it will put a non-END marker in D_OUT, which
        // will signal R_OUT_Q() to return TRUE if /? (and D_OUT otherwise)
        //
        if (DO_VAL_ARRAY_AT_THROWS(D_OUT, ARG(body))) {
            REBOOL stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop) {
                    if (q) return R_TRUE;
                    return R_OUT;
                }
                continue;
            }
            return R_OUT_IS_THROWN;
        }

    } while (TRUE);
}
