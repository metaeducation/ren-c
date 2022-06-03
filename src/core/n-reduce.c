//
//  File: %n-reduce.h
//  Summary: {REDUCE and COMPOSE natives and associated service routines}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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


//
//  reduce: native [
//
//  {Evaluates expressions, keeping each result (DO only gives last result)}
//
//      return: "New array or value"
//          [<opt> any-value!]
//      value "GROUP! and BLOCK! evaluate each item, single values evaluate"
//          [any-value!]
//      /predicate "Applied after evaluation, default is IDENTITY"
//          [action!]
//  ]
//
REBNATIVE(reduce)
{
    INCLUDE_PARAMS_OF_REDUCE;

    REBVAL *v = ARG(value);
    REBVAL *predicate = ARG(predicate);

    // Single element REDUCE is currently limited only to certain types.
    // (R3-Alpha, would just return the input, e.g. `reduce :foo` => :foo)
    // If there are arguments required, Eval_Value_Throws() will error.
    //
    // !!! Should the error be more "reduce-specific" if args were required?
    //
    // !!! How should predicates interact with this case?
    //
    if (not ANY_ARRAY(v)) {
        if (Eval_Value_Throws(OUT, v, SPECIFIED))
            return_thrown (OUT);

        return OUT;  // let caller worry about whether to error on nulls
    }

    REBDSP dsp_orig = DSP;

    DECLARE_FEED_AT (feed, v);
    DECLARE_FRAME (f, feed, EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED);

    Push_Frame(nullptr, f);

    while (NOT_END(f_value)) {
        bool line = GET_CELL_FLAG(f_value, NEWLINE_BEFORE);

        if (Eval_Step_Maybe_Stale_Throws(SET_END(OUT), f)) {
            DS_DROP_TO(dsp_orig);
            Abort_Frame(f);
            return_thrown (OUT);
        }

        if (IS_NULLED(ARG(predicate))) {  // default processing
            if (Is_Stale(OUT))
                continue;  // reduce [<a> if false [<b>]] => [<a>]
                           // reduce [<a> comment "hi"] => [<a>]
        }
        else {
            // usermode post-processing of result if requested
            //
            // !!! Experiment with pattern for passing predicate parameters.
            // If this works, it should be generalized and reused.
            //
            REBVAL *processed;
            if (Is_Stale(OUT))
                processed = rebMeta(predicate, Init_Meta_Of_Void(OUT));
            else if (Is_Isotope(OUT))
                processed = rebMeta(predicate, Meta_Quotify(OUT));
            else if (IS_NULLED(OUT))
                processed = rebMeta(
                    predicate, Lib(THE_P), Init_Meta_Of_Null_Isotope(OUT)
                );
            else
                processed = rebMeta(predicate, rebQ(OUT));

            if (not processed) {
                Init_Nulled(OUT);
            }
            else {
                Move_Cell(OUT, processed);
                rebRelease(processed);

                if (Is_Meta_Of_Void(OUT))
                    continue;  // `reduce/predicate [null] :maybe`

                Meta_Unquotify(OUT);
            }
        }

        Decay_If_Isotope(OUT);

        if (IS_NULLED(OUT))
            fail (Error_Need_Non_Null_Raw());  // trigger error below

        if (Is_Isotope(OUT))
            fail (Error_Bad_Isotope(OUT));

        Move_Cell(DS_PUSH(), OUT);

        if (line)
            SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
    }

    Drop_Frame_Unbalanced(f);  // Drop_Frame() asserts on accumulation

    REBFLGS pop_flags = NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE;
    if (GET_SUBCLASS_FLAG(ARRAY, VAL_ARRAY(v), NEWLINE_AT_TAIL))
        pop_flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

    return Init_Any_Array(
        OUT,
        VAL_TYPE(v),
        Pop_Stack_Values_Core(FRAME->baseline.dsp, pop_flags)
    );
}


//
//  reduce-each: native [
//
//  {Evaluates expressions, keeping each result (DO only gives last result)}
//
//      return: "Last body result"
//          [<opt> <void> any-value!]
//      :vars "Variable to receive each reduced value (multiple TBD)"
//          [word!]
//      block "Input block of expressions (@[block] acts like FOR-EACH)"
//          [block! the-block!]
//      body "Code to run on each step"
//          [block!]
//  ]
//
REBNATIVE(reduce_each)
{
    INCLUDE_PARAMS_OF_REDUCE_EACH;

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        &context,
        ARG(vars)
    );
    Init_Object(ARG(vars), context);  // keep GC safe

    REBFLGS flags = EVAL_MASK_DEFAULT;
    if (IS_THE_BLOCK(ARG(block)))
        flags |= EVAL_FLAG_NO_EVALUATIONS;

    DECLARE_FRAME_AT (f, ARG(block), flags);
    Push_Frame(nullptr, f);

    while (NOT_END(f_value)) {
        if (Eval_Step_Maybe_Stale_Throws(RESET(SPARE), f)) {
            Abort_Frame(f);
            return_thrown (SPARE);
        }
        Clear_Stale_Flag(SPARE);

        if (IS_VOID(SPARE))
            continue;

        // !!! This needs to handle the case where the vars are ^META, as well
        // as multiple vars.  Eval_Step_Throws() discards information that we
        // would get from Eval_Step_Maybe_Stale_Throws().  Review.

        Move_Cell(CTX_VAR(context, 1), SPARE);

        if (Do_Branch_Throws(OUT, ARG(body))) {
            bool broke;
            if (not Catching_Break_Or_Continue(OUT, &broke))
                return_thrown (OUT);
            if (broke)
                return nullptr;

            // The way a CONTINUE with a value works is to act as if the loop
            // body evaluated to the value.  (CONTINUE) acts as (CONTINUE VOID)
            // We don't have any special handling, just process stale normally.
        }
    } while (NOT_END(f_value));

    Drop_Frame(f);

    if (Is_Stale(OUT))
        return_void (OUT);

    return_non_void (OUT);
}


bool Match_For_Compose(REBCEL(const*) group, const REBVAL *label) {
    assert(ANY_GROUP_KIND(CELL_KIND(group)));

    if (IS_NULLED(label))
        return true;

    assert(IS_TAG(label) or IS_FILE(label));

    if (VAL_LEN_AT(group) == 0) // you have a pattern, so leave `()` as-is
        return false;

    const RELVAL *first = VAL_ARRAY_ITEM_AT(group);
    if (VAL_TYPE(first) != VAL_TYPE(label))
        return false;

    return (CT_String(label, first, 1) == 0);
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
    const RELVAL *composee, // the template
    REBSPC *specifier, // specifier for relative any_array value
    const REBVAL *label, // e.g. if <*>, only match `(<*> ...)`
    bool deep, // recurse into sub-blocks
    const REBVAL *predicate,  // function to run on each spliced slot
    bool only  // do not exempt (( )) from splicing
){
    assert(predicate == nullptr or IS_ACTION(predicate));

    REBDSP dsp_orig = DSP;

    bool changed = false;

    // !!! At the moment, COMPOSE is written to use frame enumeration...and
    // frames are only willing to enumerate arrays.  But the path may be in
    // a more compressed form.  While this is being rethought, we just reuse
    // the logic of AS so it's in one place and gets tested more.
    //
    const RELVAL *any_array;
    if (ANY_PATH(composee)) {
        DECLARE_LOCAL (temp);
        Derelativize(temp, composee, specifier);
        PUSH_GC_GUARD(temp);
        any_array = rebValue("as block! @", temp);
        DROP_GC_GUARD(temp);
    }
    else
        any_array = composee;

    DECLARE_FEED_AT_CORE (feed, any_array, specifier);

    if (ANY_PATH(composee))
        rebRelease(cast(REBVAL*, m_cast(RELVAL*, any_array)));

    DECLARE_FRAME (f, feed, EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED);

    Push_Frame(nullptr, f);

  #if DEBUG_ENSURE_FRAME_EVALUATES
    f->was_eval_called = true;  // lie since we're using frame for enumeration
  #endif

    for (; NOT_END(f_value); Fetch_Next_Forget_Lookback(f)) {
        REBCEL(const*) cell = VAL_UNESCAPED(f_value);
        enum Reb_Kind heart = CELL_HEART(cell); // notice `''(...)`

        if (not ANY_ARRAY_KIND(heart)) { // won't substitute/recurse
            Derelativize(DS_PUSH(), f_value, specifier); // keep newline flag
            continue;
        }

        REBLEN quotes = VAL_NUM_QUOTES(f_value);

        bool doubled_group = false;  // override predicate with ((...))

        REBSPC *match_specifier = nullptr;
        REBCEL(const*) match = nullptr;

        if (not ANY_GROUP_KIND(heart)) {
            //
            // Don't compose at this level, but may need to walk deeply to
            // find compositions inside it if /DEEP and it's an array
        }
        else if (not only and Is_Any_Doubled_Group(cell)) {
            const RELVAL *inner = VAL_ARRAY_ITEM_AT(cell);  // 1 item
            assert(IS_GROUP(inner));
            if (Match_For_Compose(inner, label)) {
                doubled_group = true;
                match = inner;
                match_specifier = Derive_Specifier(specifier, inner);
            }
        }
        else {  // plain compose, if match
            if (Match_For_Compose(cell, label)) {
                match = cell;
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
                Fetch_Next_In_Feed(subfeed);  // wasn't possibly at END

            SET_END(out);  // want empty `()` or `(comment "hi")` to vanish
            if (Do_Feed_To_End_Maybe_Stale_Throws(
                out,
                subfeed,
                EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED
            )){
                DS_DROP_TO(dsp_orig);
                Abort_Frame(f);
                return R_THROWN;
            }

            Clear_Stale_Flag(out);

            if (predicate and not doubled_group) {
                REBVAL *processed;
                if (IS_VOID(out))
                    processed = rebMeta(predicate, Init_Meta_Of_Void(out));
                else if (IS_NULLED(out))
                    processed = rebMeta(predicate, Init_Meta_Of_Null_Isotope(out));
                else if (Is_Isotope(out))
                    processed = rebMeta(predicate, Meta_Quotify(out));
                else
                    processed = rebMeta(predicate, rebQ(out));

                if (processed == nullptr)
                    Init_Nulled(out);
                else if (Is_Meta_Of_Void(processed)) {
                    SET_END(out);
                    rebRelease(processed);
                }
                else {
                    Meta_Unquotify(Move_Cell(out, processed));
                    rebRelease(processed);
                }
            }

            if (IS_VOID(out)) {
                //
                // compose [(void)] => []
                //
                if (heart == REB_GROUP and quotes == 0)
                    continue;

                Init_Nulled(out);
            }
            else
                Decay_If_Isotope(out);

            if (
                IS_NULLED(out)
                and (heart != REB_GROUP or quotes == 0)  // [''(null)] => ['']
            ){
                // With voids, NULL is no longer tolerated in COMPOSE.  You
                // have to use MAYBE.  Set as isotope to trigger error below.
                //
                fail (Error_Need_Non_Null_Raw());
            }

            if (Is_Isotope(out))
                fail (Error_Bad_Isotope(out));

            if (predicate or doubled_group) {
                //
                // We use splicing semantics if the result was produced by a
                // predicate application, or if (( )) was used.  Splicing
                // semantics match the rules for APPEND/etc.  (The difference
                // here is that the (( )) has shown that a "one or many"
                // intent is understood; APPEND lacks that, so it's best to
                // err on the side of caution regarding NULL.  COMPOSE errs
                // on that side by splicing ~null~ in the ( ) splices.

                // compose [(([a b])) merges] => [a b merges]

                if (quotes != 0 or heart != REB_GROUP)
                    fail ("Currently can only splice plain unquoted GROUP!s");

                if (IS_BLANK(out)) {
                    //
                    // BLANK! does nothing in APPEND so do nothing.
                    //
                }
                else if (IS_QUOTED(out)) {
                    //
                    // Quoted items lose a quote level and get pushed.
                    //
                    Unquotify(Copy_Cell(DS_PUSH(), out), 1);
                }
                else if (IS_BLOCK(out)) {
                    //
                    // The only splice type is BLOCK!...

                    const RELVAL *push_tail;
                    const RELVAL *push = VAL_ARRAY_AT(&push_tail, out);
                    if (push != push_tail) {
                        //
                        // Only proxy newline flag from the template on *first*
                        // value spliced in (it may have its own newline flag)
                        //
                        // !!! These rules aren't necessarily obvious.  If you
                        // say `compose [thing ((block-of-things))]` did you
                        // want that block to fit on one line?
                        //
                        Derelativize(DS_PUSH(), push, VAL_SPECIFIER(out));
                        if (GET_CELL_FLAG(f_value, NEWLINE_BEFORE))
                            SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
                        else
                            CLEAR_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);

                        while (++push, push != push_tail)
                            Derelativize(DS_PUSH(), push, VAL_SPECIFIER(out));
                    }
                }
                else if (ANY_THE_KIND(VAL_TYPE(out))) {
                    //
                    // the @ types splice as is without the @
                    //
                    Plainify(Copy_Cell(DS_PUSH(), out));
                }
                else if (not ANY_INERT(out)) {
                    fail ("COMPOSE slots that are (( )) can't be evaluative");
                }
                else {
                    assert(not ANY_ARRAY(out));
                    Copy_Cell(DS_PUSH(), out);
                }
            }
            else {
                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose [([a b c]) unmerged] => [[a b c] unmerged]

                if (IS_NULLED(out)) {
                    assert(quotes != 0);  // handled above
                    Init_Nulled(DS_PUSH());
                }
                else
                    Copy_Cell(DS_PUSH(), out);  // can't stack eval direct

                if (heart == REB_SET_GROUP)
                    Setify(DS_TOP);
                else if (heart == REB_GET_GROUP)
                    Getify(DS_TOP);
                else if (heart == REB_META_GROUP)
                    Metafy(DS_TOP);
                else if (heart == REB_THE_GROUP)
                    Theify(DS_TOP);
                else
                    assert(heart == REB_GROUP);

                Quotify(DS_TOP, quotes);  // match original quotes

                // Use newline intent from the GROUP! in the compose pattern
                //
                if (GET_CELL_FLAG(f_value, NEWLINE_BEFORE))
                    SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
                else
                    CLEAR_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
            }

            SET_END(out);  // shouldn't leak temp eval to caller

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
                predicate,
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
                Derelativize(DS_PUSH(), f_value, specifier);
                continue;
            }

            enum Reb_Kind kind = CELL_KIND(cell);
            if (ANY_SEQUENCE_KIND(kind)) {
                DECLARE_LOCAL (temp);
                if (not Try_Pop_Sequence_Or_Element_Or_Nulled(
                    temp,
                    kind,
                    dsp_deep
                )){
                    if (Is_Valid_Sequence_Element(kind, temp)) {
                        //
                        // `compose '(null)/1:` would leave beind 1:
                        //
                        fail (Error_Cant_Decorate_Type_Raw(temp));
                    }

                    fail (Error_Bad_Sequence_Init(DS_TOP));
                }
                Copy_Cell(DS_PUSH(), temp);
            }
            else {
                REBFLGS pop_flags
                    = NODE_FLAG_MANAGED
                    | ARRAY_MASK_HAS_FILE_LINE;

                if (GET_SUBCLASS_FLAG(ARRAY, VAL_ARRAY(cell), NEWLINE_AT_TAIL))
                    pop_flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

                REBARR *popped = Pop_Stack_Values_Core(dsp_deep, pop_flags);
                Init_Any_Array(
                    DS_PUSH(),
                    kind,
                    popped  // can't push and pop in same step, need variable
                );
            }

            Quotify(DS_TOP, quotes);  // match original quoting

            if (GET_CELL_FLAG(f_value, NEWLINE_BEFORE))
                SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);

            changed = true;
        }
        else {
            // compose [[(1 + 2)] (3 + 4)] => [[(1 + 2)] 7]  ; non-deep
            //
            Derelativize(DS_PUSH(), f_value, specifier);  // keep newline flag
        }
    }

    Drop_Frame_Unbalanced(f);  // Drop_Frame() asserts on stack accumulation

    if (changed)
        return nullptr;

    return R_UNHANDLED;
}


//
//  compose: native [
//
//  {Evaluates only contents of GROUP!-delimited expressions in an array}
//
//      return: [blackhole! any-array! any-sequence! any-word! action!]
//      'label "Distinguish compose groups, e.g. [(plain) (<*> composed)]"
//          [<skip> tag! file!]
//      value "The template to fill in (no-op if WORD!, ACTION! or SPACE!)"
//          [blackhole! any-array! any-sequence! any-word! action!]
//      /deep "Compose deeply into nested arrays"
//      /only "Do not exempt ((...)) from predicate application"
//      /predicate "Function to run on composed slots (default: META)"
//          [action!]
//  ]
//
REBNATIVE(compose)
//
// Note: /INTO is intentionally no longer supported
// https://forum.rebol.info/t/stopping-the-into-virus/705
{
    INCLUDE_PARAMS_OF_COMPOSE;

    if (Is_Blackhole(ARG(value)))
        return ARG(value);  // sink locations composed to avoid double eval

    if (ANY_WORD(ARG(value)) or IS_ACTION(ARG(value)))
        return ARG(value);  // makes it easier to `set/hard compose target`

    REBDSP dsp_orig = DSP;

    REB_R r = Compose_To_Stack_Core(
        OUT,
        ARG(value),
        VAL_SPECIFIER(ARG(value)),
        ARG(label),
        did REF(deep),
        REF(predicate),
        did REF(only)
    );

    if (r == R_THROWN)
        return_thrown (OUT);

    if (r == R_UNHANDLED) {
        //
        // This is the signal that stack levels use to say nothing under them
        // needed compose, so you can just use a copy (if you want).  COMPOSE
        // always copies at least the outermost array, though.
    }
    else
        assert(r == nullptr); // normal result, changed

    if (ANY_SEQUENCE(ARG(value))) {
        if (not Try_Pop_Sequence_Or_Element_Or_Nulled(
            OUT,
            VAL_TYPE(ARG(value)),
            dsp_orig
        )){
            if (Is_Valid_Sequence_Element(VAL_TYPE(ARG(value)), OUT)) {
                //
                // `compose '(null)/1:` would leave behind 1:
                //
                fail (Error_Cant_Decorate_Type_Raw(OUT));
            }

            fail (Error_Bad_Sequence_Init(OUT));
        }
        return OUT;  // note: may not be an ANY-PATH!  See Try_Pop_Path...
    }

    // The stack values contain N NEWLINE_BEFORE flags, and we need N + 1
    // flags.  Borrow the one for the tail directly from the input REBARR.
    //
    REBFLGS flags = NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE;
    if (GET_SUBCLASS_FLAG(ARRAY, VAL_ARRAY(ARG(value)), NEWLINE_AT_TAIL))
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

    REBARR *popped = Pop_Stack_Values_Core(dsp_orig, flags);

    return Init_Any_Array(OUT, VAL_TYPE(ARG(value)), popped);
}


enum FLATTEN_LEVEL {
    FLATTEN_NOT,
    FLATTEN_ONCE,
    FLATTEN_DEEP
};


static void Flatten_Core(
    RELVAL *head,
    const RELVAL *tail,
    REBSPC *specifier,
    enum FLATTEN_LEVEL level
) {
    RELVAL *item = head;
    for (; item != tail; ++item) {
        if (IS_BLOCK(item) and level != FLATTEN_NOT) {
            REBSPC *derived = Derive_Specifier(specifier, item);

            const RELVAL *sub_tail;
            RELVAL *sub = VAL_ARRAY_AT_ENSURE_MUTABLE(&sub_tail, item);
            Flatten_Core(
                sub,
                sub_tail,
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

    const RELVAL *tail;
    RELVAL *at = VAL_ARRAY_AT_ENSURE_MUTABLE(&tail, ARG(block));
    Flatten_Core(
        at,
        tail,
        VAL_SPECIFIER(ARG(block)),
        REF(deep) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(OUT, Pop_Stack_Values(dsp_orig));
}
