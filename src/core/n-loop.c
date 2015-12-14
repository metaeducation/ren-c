/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  n-loop.c
**  Summary: native functions for loops
**  Section: natives
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

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
REBFLG Catching_Break_Or_Continue(REBVAL *val, REBFLG *stop)
{
    assert(THROWN(val));

    // Throw /NAME-s used by CONTINUE and BREAK are the actual native
    // function values of the routines themselves.
    if (!IS_NATIVE(val))
        return FALSE;

    if (VAL_FUNC_CODE(val) == &N_break) {
        *stop = TRUE; // was BREAK or BREAK/WITH
        CATCH_THROWN(val, val); // will be unset if no /WITH was used
        return TRUE;
    }

    if (VAL_FUNC_CODE(val) == &N_continue) {
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
// Initialize standard for loops (copy block, make frame, bind).
// Spec: WORD or [WORD ...]
// 
// Note that because we are copying the block in order to rebind it, the
// ensuing loop code will `Do_At_Throws(out, body, 0);`.  Starting at
// zero is correct because the duplicate body has already had the
// items before its VAL_INDEX() omitted.
//
static REBARR *Init_Loop(
    REBFRM **frame_out,
    const REBVAL *spec,
    REBVAL *body
) {
    REBFRM *frame;
    REBINT len;
    REBVAL *key;
    REBVAL *var;
    REBARR *body_out;

    assert(IS_BLOCK(body));

    // For :WORD format, get the var's value:
    if (IS_GET_WORD(spec)) spec = GET_VAR(spec);

    // Hand-make a FRAME (done for for speed):
    len = IS_BLOCK(spec) ? VAL_LEN_AT(spec) : 1;
    if (len == 0) fail (Error_Invalid_Arg(spec));
    frame = Alloc_Frame(len);
    SET_ARRAY_LEN(FRAME_VARLIST(frame), len + 1);
    SET_ARRAY_LEN(FRAME_KEYLIST(frame), len + 1);

    VAL_RESET_HEADER(FRAME_CONTEXT(frame), REB_OBJECT);
    FRAME_SPEC(frame) = NULL;
    FRAME_BODY(frame) = NULL;

    // Setup for loop:
    key = FRAME_KEY(frame, 1); // skip SELF
    var = FRAME_VAR(frame, 1);
    if (IS_BLOCK(spec)) spec = VAL_ARRAY_AT(spec);

    // Optimally create the FOREACH frame:
    while (len-- > 0) {
        if (!IS_WORD(spec) && !IS_SET_WORD(spec)) {
            FREE_FRAME(frame);
            fail (Error_Invalid_Arg(spec));
        }

        Val_Init_Typeset(key, ALL_64, VAL_WORD_SYM(spec));
        key++;

        // !!! This should likely use the unset-defaulting in Ren-C with the
        // legacy fallback to NONE!
        //
        SET_NONE(var);
        var++;

        spec++;
    }

    SET_END(key);
    SET_END(var);

    body_out = Copy_Array_At_Deep_Managed(
        VAL_ARRAY(body), VAL_INDEX(body)
    );
    Bind_Values_Deep(ARRAY_HEAD(body_out), frame);

    *frame_out = frame;

    return body_out;
}


//
//  Loop_Series_Throws: C
//
static REBFLG Loop_Series_Throws(
    REBVAL *out,
    REBVAL *var,
    REBARR *body,
    REBVAL *start,
    REBINT ei,
    REBINT ii
) {
    REBINT si = VAL_INDEX(start);
    REBCNT type = VAL_TYPE(start);

    *var = *start;

    if (ei >= cast(REBINT, VAL_LEN_HEAD(start)))
        ei = cast(REBINT, VAL_LEN_HEAD(start));

    if (ei < 0) ei = 0;

    SET_UNSET_UNLESS_LEGACY_NONE(out); // Default if the loop does not run

    for (; (ii > 0) ? si <= ei : si >= ei; si += ii) {
        VAL_INDEX(var) = si;

        if (Do_At_Throws(out, body, 0)) {
            REBFLG stop;
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
static REBFLG Loop_Integer_Throws(
    REBVAL *out,
    REBVAL *var,
    REBARR *body,
    REBI64 start,
    REBI64 end,
    REBI64 incr
) {
    VAL_RESET_HEADER(var, REB_INTEGER);

    SET_UNSET_UNLESS_LEGACY_NONE(out); // Default if the loop does not run

    while ((incr > 0) ? start <= end : start >= end) {
        VAL_INT64(var) = start;

        if (Do_At_Throws(out, body, 0)) {
            REBFLG stop;
            if (Catching_Break_Or_Continue(out, &stop)) {
                if (stop) break;
                goto next_iteration;
            }
            return TRUE;
        }

    next_iteration:
        if (!IS_INTEGER(var)) fail (Error_Has_Bad_Type(var));
        start = VAL_INT64(var);

        if (REB_I64_ADD_OF(start, incr, &start))
            fail (Error(RE_OVERFLOW));
    }

    return FALSE;
}


//
//  Loop_Number_Throws: C
//
static REBFLG Loop_Number_Throws(
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

    SET_UNSET_UNLESS_LEGACY_NONE(out); // Default if the loop does not run

    for (; (i > 0.0) ? s <= e : s >= e; s += i) {
        VAL_DECIMAL(var) = s;

        if (Do_At_Throws(out, body, 0)) {
            REBFLG stop;
            if (Catching_Break_Or_Continue(out, &stop)) {
                if (stop) break;
                goto next_iteration;
            }
            return TRUE;
        }

    next_iteration:
        if (!IS_DECIMAL(var)) fail (Error_Has_Bad_Type(var));
        s = VAL_DECIMAL(var);
    }

    return FALSE;
}


//
//  Loop_All: C
// 
// 0: forall
// 1: forskip
//
static REB_R Loop_All(struct Reb_Call *call_, REBINT mode)
{
    REBVAL *var;
    REBARR *body;
    REBCNT bodi;
    REBSER *dat;
    REBINT idx;
    REBINT inc = 1;
    REBCNT type;
    REBVAL *ds;

    var = GET_MUTABLE_VAR(D_ARG(1));

    SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);

    // Useful when the caller does an evaluation like `forall (any ...) [...]`
    // and wishes the code to effectively "opt-out" of the loop on an unset
    // or a none.
    if (IS_NONE(var) || IS_UNSET(var)) return R_OUT;

    // Save the starting var value:
    *D_ARG(1) = *var;

    if (mode == 1) inc = Int32(D_ARG(2));

    type = VAL_TYPE(var);
    body = VAL_ARRAY(D_ARG(mode+2));
    bodi = VAL_INDEX(D_ARG(mode+2));

    // Starting location when past end with negative skip:
    if (inc < 0 && VAL_INDEX(var) >= VAL_LEN_HEAD(var))
        VAL_INDEX(var) = VAL_LEN_HEAD(var) + inc;

    // NOTE: This math only works for index in positive ranges!

    if (ANY_SERIES(var)) {
        while (TRUE) {
            dat = VAL_SERIES(var);
            idx = VAL_INDEX(var);
            if (idx < 0) break;
            if (idx >= cast(REBINT, SERIES_LEN(dat))) {
                if (inc >= 0) break;
                idx = SERIES_LEN(dat) + inc; // negative
                if (idx < 0) break;
                VAL_INDEX(var) = idx;
            }

            if (Do_At_Throws(D_OUT, body, bodi)) {
                REBFLG stop;
                if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                    if (stop) {
                        // Return value has been set in D_OUT, but we need
                        // to reset var to its initial value
                        *var = *D_ARG(1);
                        return R_OUT;
                    }
                    goto next_iteration;
                }
                return R_OUT_IS_THROWN;
            }

        next_iteration:
            if (VAL_TYPE(var) != type) fail (Error_Invalid_Arg(var));
            VAL_INDEX(var) += inc;
        }
    }
    else
        fail (Error_Invalid_Arg(var));

    // !!!!! ???? allowed to write VAR????
    *var = *D_ARG(1);

    return R_OUT;
}


//
//  Loop_Each: C
// 
// Common implementation code of FOR-EACH, REMOVE-EACH, MAP-EACH,
// and EVERY.
//
static REB_R Loop_Each(struct Reb_Call *call_, LOOP_MODE mode)
{
    PARAM(1, vars);
    PARAM(2, data);
    PARAM(3, body);

    // `vars` frame (plus var and key for iterating over it)
    //
    REBFRM *frame;

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
    REBINT windex;  // write
    REBINT rindex;  // read
    REBVAL *ds;

    REBFLG stop = FALSE;
    REBFLG every_true = TRUE; // need due to OPTIONS_NONE_INSTEAD_OF_UNSETS
    REBOOL threw = FALSE; // did a non-BREAK or non-CONTINUE throw occur

    if (mode == LOOP_EVERY)
        SET_TRUE(D_OUT); // Default output is TRUE, to match ALL MAP-EACH
    else
        SET_UNSET_UNLESS_LEGACY_NONE(D_OUT); // Default if loop does not run

    if (IS_NONE(data_value) || IS_UNSET(data_value)) return R_OUT;

    body_copy = Init_Loop(&frame, ARG(vars), ARG(body));
    Val_Init_Object(ARG(vars), frame); // keep GC safe
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
        series = ARRAY_SERIES(FRAME_VARLIST(VAL_FRAME(data_value)));
        index = 1;
        //if (frame->tail > 3) fail (Error_Invalid_Arg(FRAME_KEY(frame, 3)));
    }
    else if (IS_MAP(data_value)) {
        series = VAL_SERIES(data_value);
        index = 0;
        //if (frame->tail > 3) fail (Error_Invalid_Arg(FRAME_KEY(frame, 3)));
    }
    else {
        series = VAL_SERIES(data_value);
        index  = VAL_INDEX(data_value);
        if (index >= cast(REBINT, SERIES_LEN(series))) {
            if (mode == LOOP_REMOVE_EACH) {
                SET_INTEGER(D_OUT, 0);
            }
            else if (mode == LOOP_MAP_EACH) {
                DROP_GUARD_ARRAY(mapped);
                Val_Init_Block(D_OUT, mapped);
            }
            return R_OUT;
        }
    }

    windex = index;

    // Iterate over each value in the data series block:
    while (index < (tail = SERIES_LEN(series))) {
        REBCNT i;
        REBCNT j = 0;

        REBVAL *key = FRAME_KEY(frame, 1);
        REBVAL *var = FRAME_VAR(frame, 1);

        rindex = index;  // remember starting spot

        // Set the FOREACH loop variables from the series:
        for (i = 1; !IS_END(key); i++, key++, var++) {

            if (index >= tail) {
                SET_NONE(var);
                continue;
            }

            if (ANY_ARRAY(data_value)) {
                *var = *ARRAY_AT(AS_ARRAY(series), index);
            }
            else if (ANY_CONTEXT(data_value)) {
                if (VAL_GET_EXT(
                    VAL_CONTEXT_KEY(data_value, index), EXT_WORD_HIDE
                )) {
                    // Do not evaluate this iteration
                    index++;
                    goto skip_hidden;
                }

                // Alternate between word and value parts of object:
                if (j == 0) {
                    Val_Init_Word(
                        var,
                        REB_WORD,
                        VAL_TYPESET_SYM(VAL_CONTEXT_KEY(data_value, index)),
                        AS_FRAME(series),
                        index
                    );
                    if (NOT_END(var + 1)) {
                        // reset index for the value part
                        index--;
                    }
                }
                else if (j == 1)
                    *var = *ARRAY_AT(AS_ARRAY(series), index);
                else {
                    // !!! Review this error (and this routine...)
                    REBVAL key_name;
                    Val_Init_Word_Unbound(
                        &key_name, REB_WORD, VAL_TYPESET_SYM(key)
                    );
                    fail (Error_Invalid_Arg(&key_name));
                }
                j++;
            }
            else if (IS_VECTOR(data_value)) {
                Set_Vector_Value(var, series, index);
            }
            else if (IS_MAP(data_value)) {
                REBVAL *val = ARRAY_AT(AS_ARRAY(series), index | 1);
                if (!IS_NONE(val)) {
                    if (j == 0) {
                        *var = *ARRAY_AT(AS_ARRAY(series), index & ~1);
                        if (IS_END(var + 1)) index++; // only words
                    }
                    else if (j == 1)
                        *var = *ARRAY_AT(AS_ARRAY(series), index);
                    else {
                        // !!! Review this error (and this routine...)
                        REBVAL key_name;
                        Val_Init_Word_Unbound(
                            &key_name, REB_WORD, VAL_TYPESET_SYM(key)
                        );
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

        if (index == rindex) {
            // the word block has only set-words: for-each [a:] [1 2 3][]
            index++;
        }

        if (Do_At_Throws(D_OUT, body_copy, 0)) {
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
            if (IS_CONDITIONAL_FALSE(D_OUT) || IS_UNSET(D_OUT)) {
                REBYTE wide = SERIES_WIDE(series);
                // memory areas may overlap, so use memmove and not memcpy!

                // !!! This seems a slow way to do it, but there's probably
                // not a lot that can be done as the series is expected to
                // be in a good state for the next iteration of the body. :-/
                memmove(
                    SERIES_DATA(series) + (windex * wide),
                    SERIES_DATA(series) + (rindex * wide),
                    (index - rindex) * wide
                );
                windex += index - rindex;
            }
            break;
        case LOOP_MAP_EACH:
            // anything that's not an UNSET! will be added to the result
            if (!IS_UNSET(D_OUT)) Append_Value(mapped, D_OUT);
            break;
        case LOOP_EVERY:
            if (IS_UNSET(D_OUT)) {
                // Unsets "opt out" of the vote, as with ANY and ALL
            }
            else
                every_true = every_true && IS_CONDITIONAL_TRUE(D_OUT);
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
        if (stop && !IS_UNSET(D_OUT))
            return R_OUT;
    }
#endif

    switch (mode) {
    case LOOP_FOR_EACH:
        // Returns last body result or /WITH of BREAK (or the /WITH of a
        // CONTINUE if it turned out to be the last iteration)
        return R_OUT;

    case LOOP_REMOVE_EACH:
        // Remove hole (updates tail):
        if (windex < index) Remove_Series(series, windex, index - windex);
        SET_INTEGER(D_OUT, index - windex);
        return R_OUT;

    case LOOP_MAP_EACH:
        Val_Init_Block(D_OUT, mapped);
        return R_OUT;

    case LOOP_EVERY:
        if (threw) return R_OUT_IS_THROWN;

        // Result is the cumulative TRUE? state of all the input (with any
        // unsets taken out of the consideration).  The last TRUE? input
        // if all valid and NONE! otherwise.  (Like ALL.)
        if (!every_true) return R_NONE;

        // We want to act like `ALL MAP-EACH ...`, hence we effectively ignore
        // unsets and return TRUE if the last evaluation leaves an unset.
        if (IS_UNSET(D_OUT)) return R_TRUE;

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

    REBARR *body_copy;
    REBFRM *frame;
    REBVAL *var;

    // Copy body block, make a frame, bind loop var to it:
    body_copy = Init_Loop(&frame, ARG(word), ARG(body));
    var = FRAME_VAR(frame, 1); // safe: not on stack
    Val_Init_Object(ARG(word), frame); // keep GC safe
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

    return R_OUT;
}


//
//  forall: native [
//  
//  "Evaluates a block for every value in a series."
//  
//      'word [word!] 
//      {Word that refers to the series, set to each position in series}
//      body [block!] "Block to evaluate each time"
//  ]
//
REBNATIVE(forall)
{
    return Loop_All(call_, 0);
}


//
//  forskip: native [
//  
//  "Evaluates a block for periodic values in a series."
//  
//      'word [word!] 
//      {Word that refers to the series, set to each position in series}
//      size [integer! decimal!] "Number of positions to skip each time"
//      body [block!] "Block to evaluate each time"
//  ]
//
REBNATIVE(forskip)
{
    return Loop_All(call_, 1);
}


//
//  forever: native [
//  
//  "Evaluates a block endlessly."
//  
//      body [block!] "Block to evaluate each time"
//  ]
//
REBNATIVE(forever)
{
    REBVAL * const block = D_ARG(1);

    do {
        if (DO_ARRAY_THROWS(D_OUT, block)) {
            REBFLG stop;
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
//      data [any-series! any-context! map! none!]
//          "The series to traverse"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(for_each)
{
    return Loop_Each(call_, LOOP_FOR_EACH);
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
    return Loop_Each(call_, LOOP_REMOVE_EACH);
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
    return Loop_Each(call_, LOOP_MAP_EACH);
}


//
//  every: native [
//  
//  {Returns last TRUE? value if evaluating a block over a series is all TRUE?}
//  
//      'word [word! block!]
//          "Word or block of words to set each time (local)"
//      data [any-series! any-context! map! none!]
//          "The series to traverse"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(every)
{
    return Loop_Each(call_, LOOP_EVERY);
}


//
//  loop: native [
//  
//  "Evaluates a block a specified number of times."
//  
//      count [any-number!] "Number of repetitions"
//      block [block!] "Block to evaluate"
//  ]
//
REBNATIVE(loop)
{
    REBI64 count = Int64(D_ARG(1));
    REBVAL * const block = D_ARG(2);
    REBVAL *ds;

    SET_UNSET_UNLESS_LEGACY_NONE(D_OUT); // Default if the loop does not run

    for (; count > 0; count--) {
        if (DO_ARRAY_THROWS(D_OUT, block)) {
            REBFLG stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop) return R_OUT;
                continue;
            }
            return R_OUT_IS_THROWN;
        }
    }

    return R_OUT;
}


//
//  repeat: native [
//  
//  {Evaluates a block a number of times or over a series.}
//  
//      'word [word!] "Word to set each time"
//      value [any-number! any-series! none!] 
//      "Maximum number or series to traverse"
//      body [block!] "Block to evaluate each time"
//  ]
//
REBNATIVE(repeat)
{
    REBARR *body;
    REBFRM *frame;
    REBVAL *var;
    REBVAL *count = D_ARG(2);

    if (IS_NONE(count)) {
        SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);
        return R_OUT;
    }

    if (IS_DECIMAL(count) || IS_PERCENT(count)) {
        VAL_INT64(count) = Int64(count);
        VAL_RESET_HEADER(count, REB_INTEGER);
    }

    body = Init_Loop(&frame, D_ARG(1), D_ARG(3));
    var = FRAME_VAR(frame, 1); // safe: not on stack
    Val_Init_Object(D_ARG(1), frame); // keep GC safe
    Val_Init_Block(D_ARG(3), body); // keep GC safe

    if (ANY_SERIES(count)) {
        if (Loop_Series_Throws(
            D_OUT, var, body, count, VAL_LEN_HEAD(count) - 1, 1
        )) {
            return R_OUT_IS_THROWN;
        }

        return R_OUT;
    }
    else if (IS_INTEGER(count)) {
        if (Loop_Integer_Throws(D_OUT, var, body, 1, VAL_INT64(count), 1))
            return R_OUT_IS_THROWN;

        return R_OUT;
    }

    SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);
    return R_OUT;
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
    REBVAL * const block = D_ARG(1);

    do {
    skip_check:
        if (DO_ARRAY_THROWS(D_OUT, block)) {
            REBFLG stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop) return R_OUT;

                // UNTIL is unique because when you get a CONTINUE/WITH, the
                // usual rule of the /WITH being "what the body would have
                // returned" becomes also the condition.  It's a very poor
                // expression of breaking an until to say CONTINUE/WITH TRUE,
                // as BREAK/WITH TRUE says it much better.
                //
                if (!IS_UNSET(D_OUT))
                    fail (Error(RE_BREAK_NOT_CONTINUE));

                goto skip_check;
            }
            return R_OUT_IS_THROWN;
        }

        if (IS_UNSET(D_OUT)) fail (Error(RE_NO_RETURN));

    } while (IS_CONDITIONAL_FALSE(D_OUT));

    return R_OUT;
}


//
//  while: native [
//  
//  {While a condition block is TRUE, evaluates another block.}
//  
//      cond-block [block!]
//      body-block [block!]
//  ]
//
REBNATIVE(while)
{
    REBVAL * const condition = D_ARG(1);
    REBVAL * const body = D_ARG(2);

    // We need to keep the condition and body safe from GC, so we can't
    // use a D_ARG slot for evaluating the condition (can't overwrite
    // D_OUT because that's the last loop's value we might return)
    REBVAL temp;

    // If the loop body never runs (and condition doesn't error or throw),
    // we want to return an UNSET!
    SET_UNSET_UNLESS_LEGACY_NONE(D_OUT);

    do {
        if (DO_ARRAY_THROWS(&temp, condition)) {
            // A while loop should only look for breaks and continues in its
            // body, not in its condition.  So `while [break] []` is a
            // request to break the enclosing loop (or error if there is
            // nothing to catch that break).  Hence we bubble up the throw.
            *D_OUT = temp;
            return R_OUT_IS_THROWN;
        }

        if (IS_UNSET(&temp))
            fail (Error(RE_NO_RETURN));

        if (IS_CONDITIONAL_FALSE(&temp)) {
            // When the condition evaluates to a LOGIC! false or a NONE!,
            // WHILE returns whatever the last value was that the body
            // evaluated to (or none if no body evaluations yet).
            return R_OUT;
        }

        if (DO_ARRAY_THROWS(D_OUT, body)) {
            REBFLG stop;
            if (Catching_Break_Or_Continue(D_OUT, &stop)) {
                if (stop) return R_OUT;
                continue;
            }
            return R_OUT_IS_THROWN;
        }
    } while (TRUE);
}
