//
//  File: %n-reduce.h
//  Summary: {REDUCE and COMPOSE natives and associated service routines}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"

//
//  Reduce_To_Stack_Throws: C
//
// Reduce array from the index position specified in the value.
//
bool Reduce_To_Stack_Throws(
    REBVAL *out,
    const RELVAL *any_array,
    REBSPC *specifier
){
    REBDSP dsp_orig = DSP;

    DECLARE_ARRAY_FEED (feed,
        VAL_ARRAY(any_array),
        VAL_INDEX(any_array),
        specifier
    );

    DECLARE_FRAME (f, feed, EVAL_MASK_DEFAULT);
    SHORTHAND (v, f->feed->value, NEVERNULL(const RELVAL*));

    Push_Frame(nullptr, f);

    while (NOT_END(*v)) {
        bool line = GET_CELL_FLAG(*v, NEWLINE_BEFORE);

        if (Eval_Step_Throws(out, f)) {
            DS_DROP_TO(dsp_orig);
            Abort_Frame(f);
            return true;
        }

        if (IS_END(out)) { // e.g. `reduce [comment "hi"]`
            assert(IS_END(*v));
            break;
        }

        // We can't put nulls into array cells, so we put BLANK!.  This is
        // compatible with historical behavior of `reduce [if 1 = 2 [<x>]]`
        // which produced `[#[none]]`, and is generally more useful than
        // putting VOID!, as more operations skip blanks vs. erroring.
        //
        if (IS_NULLED(out))
            Init_Blank(DS_PUSH());
        else
            Move_Value(DS_PUSH(), out);

        if (line)
            SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
    }

    Drop_Frame_Unbalanced(f); // Drop_Frame() asserts on accumulation
    return false;
}


//
//  reduce: native [
//
//  {Evaluates expressions, keeping each result (DO only gives last result)}
//
//      return: "New array or value"
//          [<opt> any-value!]
//      value "GROUP! and BLOCK! evaluate each item, single values evaluate"
//          [any-value!]
//  ]
//
REBNATIVE(reduce)
{
    INCLUDE_PARAMS_OF_REDUCE;

    REBVAL *v = ARG(value);

    if (IS_BLOCK(v) or IS_GROUP(v)) {
        REBDSP dsp_orig = DSP;

        if (Reduce_To_Stack_Throws(D_OUT, v, VAL_SPECIFIER(v)))
            return R_THROWN;

        REBFLGS pop_flags = NODE_FLAG_MANAGED | ARRAY_FLAG_HAS_FILE_LINE;
        if (GET_ARRAY_FLAG(VAL_ARRAY(v), NEWLINE_AT_TAIL))
            pop_flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

        return Init_Any_Array(
            D_OUT,
            VAL_TYPE(v),
            Pop_Stack_Values_Core(dsp_orig, pop_flags)
        );
    }

    // Single element REDUCE does an EVAL, but doesn't allow arguments.
    // (R3-Alpha, would just return the input, e.g. `reduce :foo` => :foo)
    // If there are arguments required, Eval_Value_Throws() will error.
    //
    // !!! Should the error be more "reduce-specific" if args were required?

    if (Eval_Value_Throws(D_OUT, v, SPECIFIED))
        return R_THROWN;

    return D_OUT; // let caller worry about whether to error on nulls
}


bool Match_For_Compose(const RELVAL *group, const REBVAL *label) {
    if (IS_NULLED(label))
        return true;

    assert(IS_TAG(label) or IS_FILE(label));

    if (VAL_LEN_AT(group) == 0) // you have a pattern, so leave `()` as-is
        return false;

    RELVAL *first = VAL_ARRAY_AT(group);
    if (VAL_TYPE(first) != VAL_TYPE(label))
        return false;

    return (CT_String(label, first, 1) > 0);
}


//
//  Compose_To_Stack_Core: C
//
// Use rules of composition to do template substitutions on values matching
// `pattern` by evaluating those slots, leaving all other slots as is.
//
// Values are pushed to the stack because it is a "hot" preallocated large
// memory range, and the number of values can be calculated in order to
// accurately size the result when it needs to be allocated.  Not returning
// an array also offers more options for avoiding that intermediate if the
// caller wants to add part or all of the popped data to an existing array.
//
// Returns R_UNHANDLED if the composed series is identical to the input, or
// nullptr if there were compositions.  R_THROWN if there was a throw.  It
// leaves the accumulated values for the current stack level, so the caller
// can decide if it wants them or not, regardless of if any composes happened.
//
REB_R Compose_To_Stack_Core(
    REBVAL *out, // if return result is R_THROWN, will hold the thrown value
    const RELVAL *any_array, // the template
    REBSPC *specifier, // specifier for relative any_array value
    const REBVAL *label, // e.g. if <*>, only match `(<*> ...)`
    bool deep, // recurse into sub-blocks
    bool splice_all,  // all matches that return arrays insert their values
    bool only  // do not use (( )) to cue splicing, run normally
){
    REBDSP dsp_orig = DSP;

    bool changed = false;

    DECLARE_FEED_AT_CORE (feed, any_array, specifier);

    DECLARE_FRAME (f, feed, EVAL_MASK_DEFAULT);
    SHORTHAND (v, f->feed->value, NEVERNULL(const RELVAL*));

    Push_Frame(nullptr, f);

    for (; NOT_END(*v); Fetch_Next_Forget_Lookback(f)) {
        const REBCEL *cell = VAL_UNESCAPED(*v);
        enum Reb_Kind kind = CELL_KIND(cell); // notice `''(...)`

        if (not ANY_ARRAY_OR_PATH_KIND(kind)) { // won't substitute/recurse
            Derelativize(DS_PUSH(), *v, specifier); // keep newline flag
            continue;
        }

        REBCNT quotes = VAL_NUM_QUOTES(*v);

        bool splicing = splice_all;  // can override splice with ((...))

        REBSPC *match_specifier = nullptr;
        const RELVAL *match = nullptr;

        if (not ANY_GROUP_KIND(kind)) {
            //
            // Don't compose at this level, but may need to walk deeply to
            // find compositions inside it if /DEEP and it's an array
        }
        else if (not only and Is_Any_Doubled_Group(*v)) {  // splice override
            RELVAL *inner = VAL_ARRAY_AT(*v);
            if (Match_For_Compose(inner, label)) {
                splicing = true;
                match = inner;
                match_specifier = Derive_Specifier(specifier, inner);
            }
        }
        else {  // plain compose, if match
            if (Match_For_Compose(*v, label)) {
                match = *v;
                match_specifier = specifier;
            }
        }

        if (match) {
            //
            // If <*> is the label and (<*> 1 + 2) is found, run just (1 + 2).
            // Using feed interface vs plain Do_XXX to skip cheaply.
            //
            DECLARE_FEED_AT_CORE (subfeed, match, match_specifier);
            if (not IS_NULLED(label))
                Fetch_Next_In_Feed(subfeed, false);  // wasn't possibly at END

            Init_Nulled(out);  // want empty `()` to vanish as a null would
            if (Do_Feed_To_End_Maybe_Stale_Throws(out, subfeed)) {
                DS_DROP_TO(dsp_orig);
                Abort_Frame(f);
                return R_THROWN;
            }

            // We do not have to clear the stale flag, because the `out`
            // cell's pointer is not passed to any clients who read it.
            // (Move_Value() explicitly does not copy NODE_FLAG_MARKED)

            if (IS_NULLED(out) and kind == REB_GROUP and quotes == 0) {
                //
                // compose [(unquoted "nulls *vanish*!" null)] => []
                // compose [(elide "so do 'empty' composes")] => []
            }
            else if (splicing and ANY_ARRAY(out)) {
                //
                // compose [(([a b])) merges] => [a b merges]

                if (quotes != 0 or kind != REB_GROUP)
                    fail ("Currently can only splice plain unquoted GROUP!s");

                RELVAL *push = VAL_ARRAY_AT(out);
                if (NOT_END(push)) {
                    //
                    // Only proxy newline flag from the template on *first*
                    // value spliced in (it may have its own newline flag)
                    //
                    Derelativize(DS_PUSH(), push, VAL_SPECIFIER(out));
                    if (GET_CELL_FLAG(*v, NEWLINE_BEFORE))
                        SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);

                    while (++push, NOT_END(push))
                        Derelativize(DS_PUSH(), push, VAL_SPECIFIER(out));
                }
            }
            else {
                // !!! What about VOID!s?  REDUCE and other routines have
                // become more lenient, and let you worry about it later.

                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose [([a b c]) unmerged] => [[a b c] unmerged]

                Move_Value(DS_PUSH(), out);  // can't eval to stack directly!

                if (kind == REB_SET_GROUP)
                    Setify(DS_TOP);
                else if (kind == REB_GET_GROUP)
                    Getify(DS_TOP);
                else
                    assert(kind == REB_GROUP);

                Quotify(DS_TOP, quotes);  // match original quotes
                if (GET_CELL_FLAG(*v, NEWLINE_BEFORE))
                    SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
            }

          #ifdef DEBUG_UNREADABLE_BLANKS
            Init_Unreadable_Blank(out);  // shouldn't leak temp eval to caller
          #endif

            changed = true;
        }
        else if (deep) {
            // compose/deep [does [(1 + 2)] nested] => [does [3] nested]

            REBDSP dsp_deep = DSP;
            REB_R r = Compose_To_Stack_Core(
                out,
                cast(const RELVAL*, cell),  // unescaped array (w/no QUOTEs)
                specifier,
                label,
                true,  // deep (guaranteed true if we get here)
                splice_all,
                only
            );

            if (r == R_THROWN) {
                DS_DROP_TO(dsp_orig);  // drop to outer DSP (@ function start)
                Abort_Frame(f);
                return R_THROWN;
            }

            if (r == R_UNHANDLED) {
                //
                // To save on memory usage, Ren-C does not make copies of
                // arrays that don't have some substitution under them.  This
                // may be controlled by a switch if it turns out to be needed.
                //
                DS_DROP_TO(dsp_deep);
                Derelativize(DS_PUSH(), *v, specifier);
                continue;
            }

            REBFLGS pop_flags = NODE_FLAG_MANAGED | ARRAY_FLAG_HAS_FILE_LINE;
            if (GET_ARRAY_FLAG(VAL_ARRAY(cell), NEWLINE_AT_TAIL))
                pop_flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

            REBARR *popped = Pop_Stack_Values_Core(dsp_deep, pop_flags);
            Init_Any_Array(
                DS_PUSH(),
                kind,
                popped  // can't push and pop in same step, need this variable
            );

            Quotify(DS_TOP, quotes);  // match original quoting

            if (GET_CELL_FLAG(*v, NEWLINE_BEFORE))
                SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);

            changed = true;
        }
        else {
            // compose [[(1 + 2)] (3 + 4)] => [[(1 + 2)] 7]  ; non-deep
            //
            Derelativize(DS_PUSH(), *v, specifier);  // keep newline flag
        }
    }

    Drop_Frame_Unbalanced(f);  // Drop_Frame() asserts on stack accumulation
    return changed ? nullptr : R_UNHANDLED;
}


//
//  compose: native [
//
//  {Evaluates only contents of GROUP!-delimited expressions in an array}
//
//      return: [any-array! any-path!]
//      :label "Distinguish compose groups, e.g. [(plain) (<*> composed)]"
//          [<skip> tag! file!]
//      value "Array to use as the template for substitution"
//          [any-array! any-path!]
//      /deep "Compose deeply into nested arrays"
//      /splice "Insert contents of arrays by default, even if not ((...))"
//      /only "Do not treat ((...)) as an instruction to splice"
//  ]
//
REBNATIVE(compose)
//
// Note: /INTO is intentionally no longer supported
// https://forum.rebol.info/t/stopping-the-into-virus/705
{
    INCLUDE_PARAMS_OF_COMPOSE;

    REBDSP dsp_orig = DSP;

    REB_R r = Compose_To_Stack_Core(
        D_OUT,
        ARG(value),
        VAL_SPECIFIER(ARG(value)),
        ARG(label),
        REF(deep),
        REF(splice),
        REF(only)
    );

    if (r == R_THROWN)
        return R_THROWN;

    if (r == R_UNHANDLED) {
        //
        // This is the signal that stack levels use to say nothing under them
        // needed compose, so you can just use a copy (if you want).  COMPOSE
        // always copies at least the outermost array, though.
    }
    else
        assert(r == nullptr); // normal result, changed

    // The stack values contain N NEWLINE_BEFORE flags, and we need N + 1
    // flags.  Borrow the one for the tail directly from the input REBARR.
    //
    REBFLGS flags = NODE_FLAG_MANAGED | ARRAY_FLAG_HAS_FILE_LINE;
    if (GET_ARRAY_FLAG(VAL_ARRAY(ARG(value)), NEWLINE_AT_TAIL))
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

    REBARR *popped = Pop_Stack_Values_Core(dsp_orig, flags);
    if (ANY_PATH(ARG(value)))
        return Init_Any_Path(D_OUT, VAL_TYPE(ARG(value)), popped);

    return Init_Any_Array(D_OUT, VAL_TYPE(ARG(value)), popped);
}


enum FLATTEN_LEVEL {
    FLATTEN_NOT,
    FLATTEN_ONCE,
    FLATTEN_DEEP
};


static void Flatten_Core(
    RELVAL *head,
    REBSPC *specifier,
    enum FLATTEN_LEVEL level
) {
    RELVAL *item = head;
    for (; NOT_END(item); ++item) {
        if (IS_BLOCK(item) and level != FLATTEN_NOT) {
            REBSPC *derived = Derive_Specifier(specifier, item);
            Flatten_Core(
                VAL_ARRAY_AT(item),
                derived,
                level == FLATTEN_ONCE ? FLATTEN_NOT : FLATTEN_DEEP
            );
        }
        else
            Derelativize(DS_PUSH(), item, specifier);
    }
}


//
//  flatten: native [
//
//  {Flattens a block of blocks.}
//
//      return: [block!]
//          {The flattened result block}
//      block [block!]
//          {The nested source block}
//      /deep
//  ]
//
REBNATIVE(flatten)
{
    INCLUDE_PARAMS_OF_FLATTEN;

    REBDSP dsp_orig = DSP;

    Flatten_Core(
        VAL_ARRAY_AT(ARG(block)),
        VAL_SPECIFIER(ARG(block)),
        REF(deep) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
}
