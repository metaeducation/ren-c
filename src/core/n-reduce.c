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

        if (not IS_NULLED(ARG(predicate))) {  // post-process result if needed
            REBVAL *processed = rebValue(
                META_VALUE, rebINLINE(predicate), rebQ(D_OUT)
            );  // !!! "^" instead of META_VALUE causes crash, investigate
            if (processed) {
                if (Is_Void(processed)) {
                    rebRelease(processed);
                    continue;
                }
                Copy_Cell(D_OUT, processed);
                Meta_Unquotify(D_OUT);
                rebRelease(processed);
            }
            else
                Init_Nulled(D_OUT);
        }

        // Ren-C sticks with historical precedent in making the default
        // for REDUCE strictly output a number of results equal to the number
        // of input expressions.  Hence NULL is converted to ~null~::
        //
        //     >> append [<a> <b>] reduce [<c> if false [<d>]]
        //     == [<a> <b> <c> ~null~]  ; two expressions adding two results
        //
        // There are invisibles to subvert this if one really wants to, in
        // particular DENULL.
        //
        // https://forum.rebol.info/t/1665
        //
        if (IS_NULLED(D_OUT))
            Init_Bad_Word(DS_PUSH(), SYM_NULL);
        else {
            Copy_Cell(DS_PUSH(), D_OUT);
            if (IS_BAD_WORD(DS_TOP))
                CLEAR_CELL_FLAG(DS_TOP, ISOTOPE);  // must be block-safe
        }
        if (line)
            SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
    } while (NOT_END(f_value));

    Drop_Frame_Unbalanced(f);  // Drop_Frame() asserts on accumulation

    REBFLGS pop_flags = NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE;
    if (GET_SUBCLASS_FLAG(ARRAY, VAL_ARRAY(v), NEWLINE_AT_TAIL))
        pop_flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

    return Init_Any_Array(
        D_OUT,
        VAL_TYPE(v),
        Pop_Stack_Values_Core(dsp_orig, pop_flags)
    );
}


//
//  reduce-each: native [
//
//  {Evaluates expressions, keeping each result (DO only gives last result)}
//
//      return: "Last body result"
//          [<opt> any-value!]
//      :vars "Variable to receive each reduced value (multiple TBD)"
//          [word!]
//      block "Input block of expressions (quoted block acts like FOR-EACH)"
//          [block! quoted!]
//      body "Code to run on each step"
//          [block!]
//  ]
//
REBNATIVE(reduce_each)
{
    INCLUDE_PARAMS_OF_REDUCE_EACH;

    // `reduce-each x ^[1 + 2] [...]` gives x as 1, +, and 2.
    //
    REBVAL *block = ARG(block);
    if (IS_QUOTED(block)) {
        Unquotify(block, 1);
        if (not IS_BLOCK(block))
            fail ("Only BLOCK! and QUOTED!-BLOCK! supported by REDUCE-EACH");

        return rebValue(Native(FOR_EACH), ARG(vars), block, ARG(body));
    }

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        &context,
        ARG(vars)
    );
    Init_Object(ARG(vars), context);  // keep GC safe

    DECLARE_FEED_AT (feed, ARG(block));
    DECLARE_FRAME (f, feed, EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED);

    Push_Frame(nullptr, f);

    do {
        if (Eval_Step_Throws(D_SPARE, f)) {
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_END(D_SPARE)) {
            if (IS_END(f_value))
                break;  // `reduce []`
            continue;  // `reduce [comment "hi"]`
        }

        Move_Cell(CTX_VAR(context, 1), D_SPARE);

        if (Do_Branch_Throws(D_OUT, ARG(body))) {
            bool broke;
            if (not Catching_Break_Or_Continue(D_OUT, &broke))
                return R_THROWN;
            if (broke)
                return Init_Nulled(D_OUT);

            // The way a CONTINUE with a value works is to act as if the loop
            // body evaluated to the value.  (CONTINUE) acts as (CONTINUE NULL)
        }
    } while (NOT_END(f_value));

    Drop_Frame(f);

    return D_OUT;
}


bool Match_For_Compose(const RELVAL *group, const REBVAL *label) {
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

  #if defined(DEBUG_ENSURE_FRAME_EVALUATES)
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
        const RELVAL *match = nullptr;

        if (not ANY_GROUP_KIND(heart)) {
            //
            // Don't compose at this level, but may need to walk deeply to
            // find compositions inside it if /DEEP and it's an array
        }
        else if (not only and Is_Any_Doubled_Group(f_value)) {
            const RELVAL *inner = VAL_ARRAY_ITEM_AT(f_value);  // 1 item
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

            SET_END(out);  // want empty `()` to vanish
            if (Do_Feed_To_End_Maybe_Stale_Throws(
                out,
                subfeed,
                EVAL_MASK_DEFAULT | EVAL_FLAG_ALLOCATED_FEED
            )){
                DS_DROP_TO(dsp_orig);
                Abort_Frame(f);
                return R_THROWN;
            }
            CLEAR_CELL_FLAG(out, OUT_NOTE_STALE);

            if (IS_END(out))
                continue;

            REBVAL *insert;
            if (
                predicate
                and not doubled_group
            ){
                insert = rebValue(META_VALUE, rebINLINE(predicate), rebQ(out));

                if (insert == nullptr) {
                    // leave alone
                }
                else if (Is_Nulled_Isotope(insert)) {
                    insert = nullptr;
                    rebRelease(insert);
                }
                else if (Is_Void(insert)) {
                    //
                    // compose [(elide "invisible composes vanish")] => []
                    //
                    rebRelease(insert);
                    continue;
                }
                else
                    Meta_Unquotify(insert);
            }
            else {
                if (Is_Void(out) and doubled_group) {
                    //
                    // The idea about ~void~ isotopes (vs. actual invisiblity)
                    // is that they represent void intent across places where
                    // it may not be safe to vaporize.  But we have permission
                    // to vaporize due to the (( )) intent...it's only the
                    // ( ) slots that need to be conservative.
                    //
                    continue;
                }

                insert = (IS_NULLED(out) or Is_Nulled_Isotope(out))
                    ? nullptr
                    : out;
            }

            // Note: Keep isotopic nulls as isotopic, in case predicate takes
            // its parameter as ^META.

            if (
                insert
                and IS_BAD_WORD(insert)
                and GET_CELL_FLAG(insert, ISOTOPE)
            ){
                fail (Error_Bad_Isotope(insert));
            }

            if (insert == nullptr and heart == REB_GROUP and quotes == 0) {
                //
                // compose [(null)] => [~null~]
                // compose [((null))] => []
                //
                if (not doubled_group)
                    Init_Bad_Word(DS_PUSH(), SYM_NULL);
            }
            else if (insert and (predicate or doubled_group)) {
                //
                // We use splicing semantics if the result was produced by a
                // predicate application, or if (( )) was used.  Splicing
                // semantics match the rules for APPEND/etc, with the minor
                // twist that NULL is accepted as vaporizing.  (The difference
                // here is that the (( )) has shown that a "one or many"
                // intent is understood; APPEND lacks that, so it's best to
                // err on the side of caution regarding NULL.  COMPOSE errs
                // on that side by splicing ~null~ in the ( ) splices.

                // compose [(([a b])) merges] => [a b merges]

                if (quotes != 0 or heart != REB_GROUP)
                    fail ("Currently can only splice plain unquoted GROUP!s");

                if (IS_BLANK(insert)) {
                    //
                    // BLANK! does nothing in APPEND so do nothing.
                    //
                }
                else if (IS_QUOTED(insert)) {
                    //
                    // Quoted items lose a quote level and get pushed.
                    //
                    Unquotify(Copy_Cell(DS_PUSH(), insert), 1);
                }
                else if (IS_BLOCK(insert)) {
                    //
                    // The only splice type is BLOCK!...

                    const RELVAL *push_tail;
                    const RELVAL *push = VAL_ARRAY_AT(&push_tail, insert);
                    if (push != push_tail) {
                        //
                        // Only proxy newline flag from the template on *first*
                        // value spliced in (it may have its own newline flag)
                        //
                        // !!! These rules aren't necessarily obvious.  If you
                        // say `compose [thing ((block-of-things))]` did you
                        // want that block to fit on one line?
                        //
                        Derelativize(DS_PUSH(), push, VAL_SPECIFIER(insert));
                        if (GET_CELL_FLAG(f_value, NEWLINE_BEFORE))
                            SET_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);
                        else
                            CLEAR_CELL_FLAG(DS_TOP, NEWLINE_BEFORE);

                        while (++push, push != push_tail)
                            Derelativize(DS_PUSH(), push, VAL_SPECIFIER(insert));
                    }
                }
                else if (ANY_THE_KIND(VAL_TYPE(insert))) {
                    //
                    // the @ types splice as is without the @
                    //
                    Plainify(Copy_Cell(DS_PUSH(), insert));
                }
                else if (not ANY_INERT(insert)) {
                    fail ("COMPOSE slots that are (( )) can't be evaluative");
                }
                else {
                    assert(not ANY_ARRAY(insert));
                    Copy_Cell(DS_PUSH(), insert);
                }
            }
            else {
                // !!! What about BAD-WORD!s?  REDUCE and other routines have
                // become more lenient, and let you worry about it later.

                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose [([a b c]) unmerged] => [[a b c] unmerged]

                if (insert == nullptr)
                    Init_Nulled(DS_PUSH());
                else
                    Copy_Cell(DS_PUSH(), insert);  // can't stack eval direct

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

            if (insert != out)
                rebRelease(insert);

          #ifdef DEBUG_UNREADABLE_TRASH
            Init_Trash(out);  // shouldn't leak temp eval to caller
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
    return changed ? nullptr : R_UNHANDLED;
}


//
//  compose: native [
//
//  {Evaluates only contents of GROUP!-delimited expressions in an array}
//
//      return: [blackhole! any-array! any-sequence! any-word! action!]
//      'predicate [<skip> action!]  ; !!! PATH! may be meant as value (!)
//          "Function to run on composed slots (default: ENBLOCK)"
//      'label "Distinguish compose groups, e.g. [(plain) (<*> composed)]"
//          [<skip> tag! file!]
//      value "The template to fill in (no-op if WORD!, ACTION! or SPACE!)"
//          [blackhole! any-array! any-sequence! any-word! action!]
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
    if (GET_SUBCLASS_FLAG(ARRAY, VAL_ARRAY(ARG(value)), NEWLINE_AT_TAIL))
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

    return Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
}
