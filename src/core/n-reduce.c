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
//      'predicate "Applied after evaluation, default is .IDENTITY"
//          [<skip> predicate! action!]
//      value "GROUP! and BLOCK! evaluate each item, single values evaluate"
//          [any-value!]
//  ]
//
REBNATIVE(reduce)
{
    INCLUDE_PARAMS_OF_REDUCE;

    REBVAL *v = ARG(value);

    REBVAL *predicate = ARG(predicate);
    if (Cache_Predicate_Throws(D_OUT, predicate))
        return R_THROWN;

    // Single element REDUCE does an EVAL, but doesn't allow arguments.
    // (R3-Alpha, would just return the input, e.g. `reduce :foo` => :foo)
    // If there are arguments required, Eval_Value_Throws() will error.
    //
    // !!! Should the error be more "reduce-specific" if args were required?
    //
    // !!! How should predicates interact with this case?
    //
    if (not IS_BLOCK(v) and not IS_GROUP(v)) {
        if (Eval_Value_Throws(D_OUT, v, SPECIFIED))
            return R_THROWN;

        return D_OUT;  // let caller worry about whether to error on nulls
    }

    REBDSP dsp_orig = DSP;

    DECLARE_FEED_AT (feed, v);
    DECLARE_FRAME (f, feed, EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED);

    Push_Frame(nullptr, f);

    do {
        bool line = IS_END(f_value)
            ? false
            : GET_CELL_FLAG(f_value, NEWLINE_BEFORE);

        if (Eval_Step_Throws(D_OUT, f)) {
            DS_DROP_TO(dsp_orig);
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_END(D_OUT)) {
            if (IS_END(f_value))
                break;  // `reduce []`
            continue;  // `reduce [comment "hi"]`
        }

        if (IS_NULLED(ARG(predicate))) {
            //
            // Ren-C breaks with historical precedent in making the default
            // for REDUCE to not strictly output a number of results equal
            // to the number of input expressions, as NULL is "non-valued":
            //
            //     >> append [<a> <b>] reduce [<c> if false [<d>]]
            //     == [<a> <b> <c>]  ; two expressions added just one result
            //
            // A predicate like .NON.NULL could error on NULLs, or they could
            // be converted to blanks/etc.  See rationale for the change:
            //
            // https://forum.rebol.info/t/what-should-do-do/1426
            //
            if (not IS_NULLED(D_OUT))
                Move_Value(DS_PUSH(), D_OUT);
        }
        else {
            REBVAL *processed = rebValue(
                rebINLINE(predicate), rebQ(D_OUT),
            rebEND);
            if (processed)
                Move_Value(DS_PUSH(), processed);
            rebRelease(processed);
        }

        if (line)
            SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
    } while (NOT_END(f_value));

    Drop_Frame_Unbalanced(f);  // Drop_Frame() asserts on accumulation

    REBFLGS pop_flags = NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE;
    if (GET_ARRAY_FLAG(VAL_ARRAY(v), NEWLINE_AT_TAIL))
        pop_flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

    return Init_Any_Array(
        D_OUT,
        VAL_TYPE(v),
        Pop_Stack_Values_Core(dsp_orig, pop_flags)
    );
}


bool Match_For_Compose(unstable const RELVAL *group, const REBVAL *label) {
    if (IS_NULLED(label))
        return true;

    assert(IS_TAG(label) or IS_FILE(label));

    if (VAL_LEN_AT(group) == 0) // you have a pattern, so leave `()` as-is
        return false;

    unstable const RELVAL *first = VAL_ARRAY_AT(group);
    if (VAL_TYPE(first) != VAL_TYPE(label))
        return false;

    return (CT_String(label, STABLE_HACK(first), 1) == 0);
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
        any_array = rebValueQ("as block!", temp, rebEND);
        DROP_GC_GUARD(temp);
    }
    else
        any_array = composee;

    DECLARE_FEED_AT_CORE (feed, any_array, specifier);

    if (ANY_PATH(composee))
        rebRelease(cast(REBVAL*, m_cast(RELVAL*, any_array)));

    DECLARE_FRAME (f, feed, EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED);

    Push_Frame(nullptr, f);

  #if defined(DEBUG_ENSURE_FRAME_EVALUATES)
    f->was_eval_called = true;  // lie since we're using frame for enumeration
  #endif

    for (; NOT_END(f_value); Fetch_Next_Forget_Lookback(f)) {
        REBCEL(const*) cell = VAL_UNESCAPED(f_value);
        enum Reb_Kind heart = CELL_HEART(cell); // notice `''(...)`

        if (not ANY_ARRAY_OR_PATH_KIND(heart)) { // won't substitute/recurse
            Derelativize(DS_PUSH(), f_value, specifier); // keep newline flag
            continue;
        }

        REBLEN quotes = VAL_NUM_QUOTES(f_value);

        bool doubled_group = false;  // override predicate with ((...))

        REBSPC *match_specifier = nullptr;
        unstable const RELVAL *match = nullptr;

        if (not ANY_GROUP_KIND(heart)) {
            //
            // Don't compose at this level, but may need to walk deeply to
            // find compositions inside it if /DEEP and it's an array
        }
        else if (not only and Is_Any_Doubled_Group(f_value)) {
            unstable const RELVAL *inner = VAL_ARRAY_AT(f_value);
            if (Match_For_Compose(inner, label)) {
                doubled_group = true;
                match = inner;
                match_specifier = Derive_Specifier(specifier, inner);
            }
        }
        else {  // plain compose, if match
            if (Match_For_Compose(f_value, label)) {
                match = f_value;
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

            Init_Nulled(out);  // want empty `()` to vanish as a null would
            if (Do_Feed_To_End_Maybe_Stale_Throws(
                out,
                subfeed,
                EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED
            )){
                DS_DROP_TO(dsp_orig);
                Abort_Frame(f);
                return R_THROWN;
            }
            CLEAR_CELL_FLAG(out, OUT_MARKED_STALE);

            REBVAL *insert;
            if (
                predicate
                and not doubled_group
            ){
                insert = rebValue(rebINLINE(predicate), rebQ(out), rebEND);
            } else
                insert = IS_NULLED(out) ? nullptr : out;

            if (insert == nullptr and heart == REB_GROUP and quotes == 0) {
                //
                // compose [(unquoted "nulls *vanish*!" null)] => []
                // compose [(elide "so do 'empty' composes")] => []
            }
            else if (
                insert
                and Splices_Without_Only(insert)
                and (predicate or doubled_group)
            ){
                // We splice blocks if they were produced by a predicate
                // application, or if (( )) was used.

                // compose [(([a b])) merges] => [a b merges]

                if (quotes != 0 or heart != REB_GROUP)
                    fail ("Currently can only splice plain unquoted GROUP!s");

                unstable const RELVAL *push = VAL_ARRAY_AT(insert);
                if (NOT_END(push)) {
                    //
                    // Only proxy newline flag from the template on *first*
                    // value spliced in (it may have its own newline flag)
                    //
                    // !!! These rules aren't necessarily obvious.  If you
                    // say `compose [thing ((block-of-things))]` did you want
                    // that block to fit on one line?
                    //
                    Derelativize(DS_PUSH(), push, VAL_SPECIFIER(insert));
                    if (GET_CELL_FLAG(f_value, NEWLINE_BEFORE))
                        SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
                    else
                        CLEAR_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);

                    while (++push, NOT_END(push))
                        Derelativize(DS_PUSH(), push, VAL_SPECIFIER(insert));
                }
            }
            else {
                // !!! What about VOID!s?  REDUCE and other routines have
                // become more lenient, and let you worry about it later.

                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose [([a b c]) unmerged] => [[a b c] unmerged]

                if (insert == nullptr)
                    Init_Nulled(DS_PUSH());
                else
                    Move_Value(DS_PUSH(), insert);  // can't stack eval direct

                if (heart == REB_SET_GROUP)
                    Setify(DS_TOP);
                else if (heart == REB_GET_GROUP)
                    Getify(DS_TOP);
                else if (heart == REB_SYM_GROUP)
                    Symify(DS_TOP);
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

            if (insert != out)
                rebRelease(insert);

          #ifdef DEBUG_UNREADABLE_VOIDS
            Init_Unreadable_Void(out);  // shouldn't leak temp eval to caller
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
                Move_Value(DS_PUSH(), temp);
            }
            else {
                REBFLGS pop_flags
                    = NODE_FLAG_MANAGED
                    | ARRAY_MASK_HAS_FILE_LINE;

                if (GET_ARRAY_FLAG(VAL_ARRAY(cell), NEWLINE_AT_TAIL))
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
    return changed ? nullptr : R_UNHANDLED;
}


//
//  compose: native [
//
//  {Evaluates only contents of GROUP!-delimited expressions in an array}
//
//      return: [blackhole! any-array! any-path! any-word! action!]
//      'predicate [<skip> action!]  ; !!! PATH! may be meant as value (!)
//          "Function to run on composed slots (default: ENBLOCK)"
//      'label "Distinguish compose groups, e.g. [(plain) (<*> composed)]"
//          [<skip> tag! file!]
//      value "The template to fill in (no-op if WORD!, ACTION! or SPACE!)"
//          [blackhole! any-array! any-path! any-word! action!]
//      /deep "Compose deeply into nested arrays"
//      /only "Do not exempt ((...)) from predicate application"
//  ]
//
REBNATIVE(compose)
//
// Note: /INTO is intentionally no longer supported
// https://forum.rebol.info/t/stopping-the-into-virus/705
{
    INCLUDE_PARAMS_OF_COMPOSE;

    REBVAL *predicate = ARG(predicate);
    if (Cache_Predicate_Throws(D_OUT, predicate))
        return R_THROWN;

    if (Is_Blackhole(ARG(value)))
        RETURN (ARG(value));  // sink locations composed to avoid double eval

    if (ANY_WORD(ARG(value)) or IS_ACTION(ARG(value)))
        RETURN (ARG(value));  // makes it easier to `set/hard compose target`

    REBDSP dsp_orig = DSP;

    REB_R r = Compose_To_Stack_Core(
        D_OUT,
        ARG(value),
        VAL_SPECIFIER(ARG(value)),
        ARG(label),
        did REF(deep),
        REF(predicate),
        did REF(only)
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

    if (ANY_SEQUENCE(ARG(value))) {
        if (not Try_Pop_Sequence_Or_Element_Or_Nulled(
            D_OUT,
            VAL_TYPE(ARG(value)),
            dsp_orig
        )){
            if (Is_Valid_Sequence_Element(VAL_TYPE(ARG(value)), D_OUT)) {
                //
                // `compose '(null)/1:` would leave behind 1:
                //
                fail (Error_Cant_Decorate_Type_Raw(D_OUT));
            }

            fail (Error_Bad_Sequence_Init(D_OUT));
        }
        return D_OUT;  // note: may not be an ANY-PATH!  See Try_Pop_Path...
    }

    // The stack values contain N NEWLINE_BEFORE flags, and we need N + 1
    // flags.  Borrow the one for the tail directly from the input REBARR.
    //
    REBFLGS flags = NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE;
    if (GET_ARRAY_FLAG(VAL_ARRAY(ARG(value)), NEWLINE_AT_TAIL))
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

    REBARR *popped = Pop_Stack_Values_Core(dsp_orig, flags);

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
                VAL_ARRAY_AT_ENSURE_MUTABLE(item),
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
        VAL_ARRAY_AT_ENSURE_MUTABLE(ARG(block)),
        VAL_SPECIFIER(ARG(block)),
        REF(deep) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
}
