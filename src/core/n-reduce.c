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


#define L_specifier         Level_Specifier(L)


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
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(reduce)
//
// 1. It's not completely clear what the semantics of non-block REDUCE should
//    be, but right now single value REDUCE does a REEVALUATE where it does
//    not allow arguments.  This is a variant of REEVAL with an END feed.
//
//    (R3-Alpha, would return the input, e.g. `reduce ':foo` => :foo)
//
// 2. We want the output newline status to mirror the newlines of the start
//    of the eval positions.  But when the evaluation callback happens, we
//    won't have the starting value anymore.  Cache the newline flag on the
//    ARG(value) cell, as newline flags on ARG()s are available.
//
// 3. The sublevel that is pushed to run the reduce evaluations uses the data
//    stack position captured in BASELINE to tell things like whether a
//    function dispatch has pushed refinements, etc.  So when the REDUCE level
//    underneath it pushes a value to the data stack, that level must be
//    informed the stack element is "not for it" before the next call.
{
    INCLUDE_PARAMS_OF_REDUCE;

    Value(*) v = ARG(value);  // newline flag on `v` cell is leveraged [2]
    Value(*) predicate = ARG(predicate);

    enum {
        ST_REDUCE_INITIAL_ENTRY = STATE_0,
        ST_REDUCE_EVAL_STEP,
        ST_REDUCE_RUNNING_PREDICATE
    };

    switch (STATE) {
      case ST_REDUCE_INITIAL_ENTRY:
        if (Any_Array(v))
            goto initial_entry_any_array;
        goto initial_entry_non_array;  // semantics in question [1]

      case ST_REDUCE_EVAL_STEP:
        goto reduce_step_result_in_out;

      case ST_REDUCE_RUNNING_PREDICATE:
        goto process_out;

      default: assert(false);
    }

  initial_entry_non_array: {  /////////////////////////////////////////////////

    if (Any_Inert(v))
        return COPY(v);  // save time if it's something like a TEXT!

    Level* sub = Make_End_Level(
        FLAG_STATE_BYTE(ST_EVALUATOR_REEVALUATING)
    );
    Push_Level(OUT, sub);

    sub->u.eval.current = v;
    sub->u.eval.current_gotten = nullptr;
    sub->u.eval.enfix_reevaluate = 'N';  // detect?

    return DELEGATE_SUBLEVEL(sub);

} initial_entry_any_array: {  ////////////////////////////////////////////////

    Level* sub = Make_Level_At(
        v,  // REB_BLOCK or REB_GROUP
        LEVEL_FLAG_ALLOCATED_FEED
            | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // reused for each step
    );
    Push_Level(OUT, sub);
    goto next_reduce_step;

} next_reduce_step: {  ///////////////////////////////////////////////////////

    if (Is_Feed_At_End(SUBLEVEL->feed))
        goto finished;

    if (Get_Cell_Flag(At_Feed(SUBLEVEL->feed), NEWLINE_BEFORE))
        Set_Cell_Flag(v, NEWLINE_BEFORE);  // cache newline flag [2]
    else
        Clear_Cell_Flag(v, NEWLINE_BEFORE);

    SUBLEVEL->executor = &Evaluator_Executor;
    STATE = ST_REDUCE_EVAL_STEP;
    Restart_Evaluator_Level(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} reduce_step_result_in_out: {  //////////////////////////////////////////////

    if (Is_Nulled(predicate))  // default is no processing
        goto process_out;

    if (Is_Barrier(OUT))  // voids and nihils offered to predicate, not commas
        goto next_reduce_step;

    if (Is_Void(OUT) or Is_Nihil(OUT)) {
        const Param* param = First_Unspecialized_Param(
            nullptr,
            VAL_ACTION(predicate)
        );
        if (not TYPE_CHECK(param, OUT))
            goto next_reduce_step;
    }

    SUBLEVEL->executor = &Just_Use_Out_Executor;
    STATE = ST_REDUCE_RUNNING_PREDICATE;
    return CONTINUE(OUT, predicate, OUT);  // arg can be same as output

} process_out: {  ////////////////////////////////////////////////////////////

    if (Is_Elision(OUT) or Is_Void(OUT))
        goto next_reduce_step;  // void results are skipped by reduce

    Decay_If_Unstable(OUT);

    if (Is_Nulled(OUT))
        return RAISE(Error_Need_Non_Null_Raw());  // error enables e.g. CURTAIL

    if (Is_Splice(OUT)) {
        const Cell* tail;
        const Cell* at = VAL_ARRAY_AT(&tail, OUT);
        bool newline = Get_Cell_Flag(v, NEWLINE_BEFORE);
        for (; at != tail; ++at) {
            Derelativize(PUSH(), at, VAL_SPECIFIER(OUT));
            SUBLEVEL->baseline.stack_base += 1;  // [3]
            if (newline) {
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);  // [2]
                newline = false;
            }
        }
    }
    else if (Is_Isotope(OUT))
        return RAISE(Error_Bad_Isotope(OUT));
    else {
        Move_Cell(PUSH(), OUT);
        SUBLEVEL->baseline.stack_base += 1;  // [3]

        if (Get_Cell_Flag(v, NEWLINE_BEFORE))  // [2]
            Set_Cell_Flag(TOP, NEWLINE_BEFORE);
    }

    goto next_reduce_step;

} finished: {  ///////////////////////////////////////////////////////////////

    Drop_Level_Unbalanced(SUBLEVEL);  // Drop_Level() asserts on accumulation

    Flags pop_flags = NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE;
    if (Get_Array_Flag(VAL_ARRAY(v), NEWLINE_AT_TAIL))
        pop_flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

    return Init_Array_Cell(
        OUT,
        VAL_TYPE(v),
        Pop_Stack_Values_Core(STACK_BASE, pop_flags)
    );
}}


//
//  reduce-each: native [
//
//  {Evaluates expressions, keeping each result (DO only gives last result)}
//
//      return: "Last body result"
//          [<opt> <void> any-value!]
//      :vars "Variable to receive each reduced value (multiple TBD)"
//          [word! meta-word!]
//      block "Input block of expressions (@[block] acts like FOR-EACH)"
//          [block! the-block!]
//      body "Code to run on each step"
//          [block!]
//  ]
//
DECLARE_NATIVE(reduce_each)
//
// !!! There used to be a /COMMAS refinement on this, which allowed you to
// see source-level commas.  Once comma isotopes took over the barrier role,
// they were distinguishable from nihils and could be filtered separately.
// With this you can write `pack [1, nihil, 2]` and get a 3-element pack.
// It may be that some use case requires /COMMAS to come back, but waiting
// to see one.
//
// 1. This current REDUCE-EACH only works with one variable; it should be able
//    to take a block of variables.
{
    INCLUDE_PARAMS_OF_REDUCE_EACH;

    Value(*) vars = ARG(vars);
    Value(*) block = ARG(block);
    Value(*) body = ARG(body);

    bool breaking = false;

    enum {
        ST_REDUCE_EACH_INITIAL_ENTRY = STATE_0,
        ST_REDUCE_EACH_REDUCING_STEP,
        ST_REDUCE_EACH_RUNNING_BODY
    };

    switch (STATE) {
      case ST_REDUCE_EACH_INITIAL_ENTRY : goto initial_entry;
      case ST_REDUCE_EACH_REDUCING_STEP : goto reduce_step_output_in_spare;
      case ST_REDUCE_EACH_RUNNING_BODY : goto body_result_in_out;
      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;

    if (Is_Meta_Word(vars)) {  // Note: gets converted to object in next step
        flags |= LEVEL_FLAG_META_RESULT | LEVEL_FLAG_RAISED_RESULT_OK;
    }

    Context* context = Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        ARG(vars)
    );
    Init_Object(ARG(vars), context);  // keep GC safe

    if (Is_The_Block(block))
        flags |= EVAL_EXECUTOR_FLAG_NO_EVALUATIONS;

    Level* sub = Make_Level_At(block, flags);
    Push_Level(SPARE, sub);
    goto reduce_next;

} reduce_next: {  ////////////////////////////////////////////////////////////

    if (Is_Feed_At_End(SUBLEVEL->feed))
        goto finished;

    SUBLEVEL->executor = &Evaluator_Executor;  // restore from pass through

    STATE = ST_REDUCE_EACH_REDUCING_STEP;
    Restart_Evaluator_Level(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} reduce_step_output_in_spare: {  ////////////////////////////////////////////

    if (
        Is_Barrier(SPARE)
        or (
            Get_Level_Flag(SUBLEVEL, META_RESULT)
            and Is_Meta_Of_Barrier(SPARE)
        )
    ){
        Init_Nihil(OUT);
        goto reduce_next;  // always cull isotopic commas (barriers)
    }

    if (
        Not_Level_Flag(SUBLEVEL, META_RESULT)
        and (Is_Void(SPARE) or Is_Nihil(SPARE))
    ){
        Init_Nihil(OUT);
        goto reduce_next;  // cull voids and nihils if not ^META
    }

    Move_Cell(CTX_VAR(VAL_CONTEXT(vars), 1), SPARE);  // do multiple? [1]

    SUBLEVEL->executor = &Just_Use_Out_Executor;  // pass through sublevel

    STATE = ST_REDUCE_EACH_RUNNING_BODY;
    return CATCH_CONTINUE_BRANCH(OUT, body);

} body_result_in_out: {  /////////////////////////////////////////////////////

    if (THROWING) {
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            goto finished;

        if (breaking)
            goto finished;
    }

    goto reduce_next;

} finished: {  ///////////////////////////////////////////////////////////////

    if (THROWING)  // sublevel has already been dropped if thrown
        return THROWN;

    Drop_Level(SUBLEVEL);

    if (Is_Fresh(OUT))  // body never ran
        return VOID;

    if (breaking)
        return nullptr;  // BREAK encountered

    return BRANCHED(OUT);
}}


bool Match_For_Compose(NoQuote(const Cell*) group, const REBVAL *label) {
    assert(Any_Group_Kind(HEART_BYTE(group)));

    if (Is_Nulled(label))
        return true;

    assert(Is_Tag(label) or Is_File(label));

    if (VAL_LEN_AT(group) == 0) // you have a pattern, so leave `()` as-is
        return false;

    const Cell* first = VAL_ARRAY_ITEM_AT(group);
    if (VAL_TYPE(first) != VAL_TYPE(label))
        return false;

    return (CT_String(label, first, 1) == 0);
}


// This is a helper common to the Composer_Executor() and the COMPOSE native
// which will push a level that does composing to the trampoline stack.
//
/////////////////////////////////////////////////////////////////////////////
//
// 1. COMPOSE relies on feed enumeration...and feeds are only willing to
//    enumerate arrays.  Paths and tuples may be in a more compressed form.
//    While this is being rethought, we just reuse the logic of AS so it's in
//    one place and gets tested more, to turn sequences into arrays.
//
// 2. The easiest way to pass along options to the composing sublevels is by
//    passing the frame of the COMPOSE to it.  Though Composer_Executor() has
//    no varlist of its own, it can read the frame variables of the native
//    so long as it is passed in the `main_level` member.
//
static void Push_Composer_Level(
    Atom(*) out,
    Level* main_level,
    const Cell* arraylike,
    Specifier* specifier
){
    Value(const*) adjusted = nullptr;
    if (Any_Sequence(arraylike)) {  // allow sequences [1]
        Derelativize(out, arraylike, specifier);
        adjusted = rebValue(Canon(AS), Canon(BLOCK_X), rebQ(out));
    }

    Level* sub = Make_Level_At_Core(
        adjusted ? adjusted : arraylike,
        adjusted ? SPECIFIED : specifier,
        EVAL_EXECUTOR_FLAG_NO_EVALUATIONS
            | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // allows stack accumulation
            | LEVEL_FLAG_RAISED_RESULT_OK  // bubbles up definitional errors
    );
    Push_Level(out, sub);  // sublevel may raise definitional failure

    if (adjusted)
        rebRelease(adjusted);

    sub->executor = &Composer_Executor;

    sub->u.compose.main_level = main_level;   // pass options [2]
    sub->u.compose.changed = false;
}


// Another helper common to the Composer_Executor() and the COMPOSE native
// itself, which pops the processed array depending on the output type.
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. If you write something like `compose '(void)/3:`, it would try to leave
//    behind something like the "SET-INTEGER!" of `3:`.  Currently that is
//    not allowed, though it could be a WORD! (like |3|:) ?
//
// 2. See Try_Pop_Sequence_Or_Element_Or_Nulled() for how reduced cases like
//    `(void).1` will turn into just INTEGER!, not `.1` -- this is in contrast
//    to `(blank).1` which does turn into `.1`
//
// 3. There are N instances of the NEWLINE_BEFORE flags on the pushed items,
//    and we need N + 1 flags.  Borrow the tail flag from the input array.
//
static Atom(*) Finalize_Composer_Level(
    Atom(*) out,
    Level* L,
    const Cell* composee  // special handling if the output kind is a sequence
){
    if (Is_Raised(out)) {
        Drop_Data_Stack_To(L->baseline.stack_base);
        return out;
    }

    enum Reb_Kind heart = Cell_Heart(composee);
    REBLEN quotes = VAL_NUM_QUOTES(composee);

    if (Any_Sequence_Kind(heart)) {
        if (not Try_Pop_Sequence_Or_Element_Or_Nulled(
            out,
            Cell_Heart(composee),
            L->baseline.stack_base
        )){
            if (Is_Valid_Sequence_Element(heart, out))
                fail (Error_Cant_Decorate_Type_Raw(out));  // no `3:` [1]

            fail (Error_Bad_Sequence_Init(Stable_Unchecked(out)));
        }

        Quotify(Stable_Unchecked(out), quotes);  // may not be sequence [2]
        return out;
    }

    Flags flags = NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE;
    if (Get_Array_Flag(VAL_ARRAY(composee), NEWLINE_AT_TAIL))
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;  // proxy newline flag [3]

    Init_Array_Cell(
        out,
        heart,
        Pop_Stack_Values_Core(L->baseline.stack_base, flags)
    );

    return Quotify(Stable_Unchecked(out), quotes);
}


//
//  Composer_Executor: C
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
// At the end of the process, `L->u.compose.changed` will be false if the
// composed series is identical to the input, true if there were compositions.
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. label -> e.g. if <*>, only match `(<*> ...)`
//    deep -> recurse into sub-blocks
//    predicate -> function to run on each spliced slot
//
// 2. HEART byte is used as a GROUP! matches regardless of quoting, so:
//
//        >> compose [a ''(1 + 2) b]
//        == [a ''3 b]
//
// 3. Splicing semantics match the rules for APPEND/etc.
//
// 4. Only proxy newline flag from the template on *first* value spliced in,
//    where it may have its own newline flag.  Not necessarily obvious; e.g.
//    would you want the composed block below to all fit on one line?
//
//        >> block-of-things: [
//               thing2  ; newline flag on thing1
//               thing3
//           ]
//
//        >> compose [thing1 (spread block-of-things)]  ; no newline flag ()
//        == [thing1
//               thing2  ; we proxy the flag, but is this what you wanted?
//               thing3
//           ]
//
// 5. At the end of the composer, we do not Drop_Data_Stack_To() and the level
//    will still be alive for the caller.  This lets them have access to this
//    level's BASELINE->stack_base, so it knows what all was pushed...and it
//    also means the caller can decide if they want the accrued items or not
//    depending on the `changed` field in the level.
//
Bounce Composer_Executor(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    if (THROWING)
        return THROWN;  // no state to cleanup (just data stack, auto-cleaned)

    DECLARE_PARAM(1, return);
    DECLARE_PARAM(2, label);
    DECLARE_PARAM(3, value);
    DECLARE_PARAM(4, deep);
    DECLARE_PARAM(5, predicate);

    Level* main_level = L->u.compose.main_level;  // the invoked COMPOSE native

    UNUSED(Level_Arg(main_level, p_return_));
    Value(*) label = Level_Arg(main_level, p_label_);
    UNUSED(Level_Arg(main_level, p_value_));
    bool deep = not Is_Nulled(Level_Arg(main_level, p_deep_));
    Value(*) predicate = Level_Arg(main_level, p_predicate_);

    assert(Is_Nulled(predicate) or Is_Frame(predicate));

    enum {
        ST_COMPOSER_INITIAL_ENTRY = STATE_0,
        ST_COMPOSER_EVAL_GROUP,
        ST_COMPOSER_RUNNING_PREDICATE,
        ST_COMPOSER_RECURSING_DEEP
    };

    switch (STATE) {
      case ST_COMPOSER_INITIAL_ENTRY :
        goto handle_current_item;

      case ST_COMPOSER_EVAL_GROUP :
      case ST_COMPOSER_RUNNING_PREDICATE :
        goto process_out;

      case ST_COMPOSER_RECURSING_DEEP :
        goto composer_finished_recursion;

      default : assert(false);
    }

  handle_next_item: {  ///////////////////////////////////////////////////////

   Fetch_Next_Forget_Lookback(L);
   goto handle_current_item;

} handle_current_item: {  ////////////////////////////////////////////////////

    if (Is_Level_At_End(L))
        goto finished;

    const Cell* at = At_Level(L);

    if (not Any_Arraylike(at)) {  // won't substitute/recurse
        Derelativize(PUSH(), at, L_specifier);  // keep newline flag
        goto handle_next_item;
    }

    enum Reb_Kind heart = Cell_Heart(at);  // quoted groups match [1]

    Specifier* match_specifier = nullptr;
    const Cell* match = nullptr;

    if (not Any_Group_Kind(heart)) {
        //
        // Don't compose at this level, but may need to walk deeply to
        // find compositions inside it if /DEEP and it's an array
    }
    else {  // plain compose, if match
        if (Match_For_Compose(at, label)) {
            match = at;
            match_specifier = L_specifier;
        }
    }

    if (not match) {
        if (deep) {
            // compose/deep [does [(1 + 2)] nested] => [does [3] nested]

            Push_Composer_Level(OUT, main_level, at, L_specifier);
            STATE = ST_COMPOSER_RECURSING_DEEP;
            return CATCH_CONTINUE_SUBLEVEL(SUBLEVEL);
        }

        // compose [[(1 + 2)] (3 + 4)] => [[(1 + 2)] 7]  ; non-deep
        //
        Derelativize(PUSH(), at, L_specifier);  // keep newline flag
        goto handle_next_item;
    }

    if (Is_Nulled(predicate))
        goto evaluate_group;

    Derelativize(SPARE, match, match_specifier);
    Dequotify(SPARE);  // cast was needed because there may have been quotes
    HEART_BYTE(SPARE) = REB_GROUP;  // don't confuse with decoration
    if (not Is_Nulled(label))
        VAL_INDEX_RAW(SPARE) += 1;  // wasn't possibly at END

    STATE = ST_COMPOSER_RUNNING_PREDICATE;
    return CONTINUE(OUT, predicate, SPARE);

  evaluate_group: { //////////////////////////////////////////////////////////

    // If <*> is the label and (<*> 1 + 2) is found, run just (1 + 2).
    //
    Feed* subfeed = Make_At_Feed_Core(match, match_specifier);
    if (not Is_Nulled(label))
        Fetch_Next_In_Feed(subfeed);  // wasn't possibly at END

    Level* sublevel = Make_Level(
        subfeed,  // used subfeed so we could skip the label if there was one
        LEVEL_FLAG_ALLOCATED_FEED
    );
    sublevel->executor = &Array_Executor;

    Push_Level(OUT, sublevel);

    STATE = ST_COMPOSER_EVAL_GROUP;
    return CATCH_CONTINUE_SUBLEVEL(sublevel);

}} process_out: {  ///////////////////////////////////////////////////////////

    assert(
        STATE == ST_COMPOSER_EVAL_GROUP
        or STATE == ST_COMPOSER_RUNNING_PREDICATE
    );

    enum Reb_Kind group_heart = Cell_Heart(At_Level(L));
    Byte group_quote_byte = QUOTE_BYTE(At_Level(L));

    if (Is_Splice(OUT))
        goto push_out_spliced;

    if (Is_Nulled(OUT))
        return RAISE(Error_Need_Non_Null_Raw());  // [(null)] => error!

    if (Is_Void(OUT)) {
        if (group_heart == REB_GROUP and group_quote_byte == UNQUOTED_1)
            goto handle_next_item;  // compose [(void)] => []

        // [''(void)] => ['']
    }
    else
        Decay_If_Unstable(OUT);

    if (Is_Isotope(OUT))
        return RAISE(Error_Bad_Isotope(OUT));

    goto push_out_as_is;

  push_out_as_is:  ///////////////////////////////////////////////////////////

    // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
    // compose [([a b c]) unmerged] => [[a b c] unmerged]

    if (Is_Void(OUT)) {
        assert(group_quote_byte != UNQUOTED_1);  // handled above
        Init_Void(PUSH());
    }
    else
        Copy_Cell(PUSH(), OUT);  // can't stack eval direct

    if (group_heart == REB_SET_GROUP)
        Setify(TOP);
    else if (group_heart == REB_GET_GROUP)
        Getify(TOP);
    else if (group_heart == REB_META_GROUP)
        Metafy(TOP);
    else if (group_heart == REB_THE_GROUP)
        Theify(TOP);
    else
        assert(group_heart == REB_GROUP);

    if (group_quote_byte & NONQUASI_BIT)
        Quotify(TOP, group_quote_byte / 2);  // add to existing quotes
    else {
        if (QUOTE_BYTE(TOP) != UNQUOTED_1)
            fail ("COMPOSE cannot quasify items not at quote level 0");
        QUOTE_BYTE(TOP) = group_quote_byte;
    }

    // Use newline intent from the GROUP! in the compose pattern
    //
    if (Get_Cell_Flag(At_Level(L), NEWLINE_BEFORE))
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);
    else
        Clear_Cell_Flag(TOP, NEWLINE_BEFORE);

    L->u.compose.changed = true;
    goto handle_next_item;

  push_out_spliced:  /////////////////////////////////////////////////////////

    // compose [(spread [a b]) merges] => [a b merges]... [3]

    if (group_quote_byte != UNQUOTED_1 or group_heart != REB_GROUP)
        return RAISE("Currently can only splice plain unquoted GROUP!s");

    if (Is_Splice(OUT)) {  // GROUP! at "quoting level -1" means splice
        Quasify_Isotope(OUT);

        const Cell* push_tail;
        const Cell* push = VAL_ARRAY_AT(&push_tail, OUT);
        if (push != push_tail) {
            Derelativize(PUSH(), push, VAL_SPECIFIER(OUT));
            if (Get_Cell_Flag(At_Level(L), NEWLINE_BEFORE))
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);  // first [4]
            else
                Clear_Cell_Flag(TOP, NEWLINE_BEFORE);

            while (++push, push != push_tail)
                Derelativize(PUSH(), push, VAL_SPECIFIER(OUT));
        }
    }
    else {
        assert(not Any_Array(OUT));
        Copy_Cell(PUSH(), OUT);
    }

    L->u.compose.changed = true;
    goto handle_next_item;

} composer_finished_recursion: {  ////////////////////////////////////////////

    // The compose stack of the nested compose is relative to *its* baseline.

    if (Is_Raised(OUT)) {
        Drop_Data_Stack_To(SUBLEVEL->baseline.stack_base);
        Drop_Level(SUBLEVEL);
        return OUT;
    }

    assert(Is_Void(OUT));

    if (not SUBLEVEL->u.compose.changed) {
        //
        // To save on memory usage, Ren-C does not make copies of
        // arrays that don't have some substitution under them.  This
        // may be controlled by a switch if it turns out to be needed.
        //
        Drop_Data_Stack_To(SUBLEVEL->baseline.stack_base);
        Drop_Level(SUBLEVEL);

        Derelativize(PUSH(), At_Level(L), L_specifier);
        // Constify(TOP);
        goto handle_next_item;
    }

    Finalize_Composer_Level(OUT, SUBLEVEL, At_Level(L));
    Drop_Level(SUBLEVEL);
    Move_Cell(PUSH(), OUT);

    if (Get_Cell_Flag(At_Level(L), NEWLINE_BEFORE))
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);

    L->u.compose.changed = true;
    goto handle_next_item;

} finished: {  ///////////////////////////////////////////////////////////////

    assert(Get_Level_Flag(L, TRAMPOLINE_KEEPALIVE));  // caller needs [5]

    return Init_Void(OUT);  // signal finished, avoid leaking temp evaluations
}}


//
//  compose: native [
//
//  {Evaluates only contents of GROUP!-delimited expressions in an array}
//
//      return: [blackhole! any-array! any-sequence! any-word! activation?]
//      'label "Distinguish compose groups, e.g. [(plain) (<*> composed)]"
//          [<skip> tag! file!]
//      value "The template to fill in (no-op if WORD!, ACTIVATION?, BLACKHOLE!)"
//          [blackhole! any-array! any-sequence! any-word! activation?]
//      /deep "Compose deeply into nested arrays"
//      /predicate "Function to run on composed slots (default: META)"
//          [<unrun> frame!]
//  ]
//
//  ; Note: /INTO is intentionally no longer supported
//  ; https://forum.rebol.info/t/stopping-the-into-virus/705
//
//  ; Note: /ONLY is intentionally no longer supported
//  https://forum.rebol.info/t/the-superpowers-of-ren-cs-revamped-compose/979/7
//
DECLARE_NATIVE(compose)
{
    INCLUDE_PARAMS_OF_COMPOSE;

    Value(*) v = ARG(value);

    USED(ARG(predicate));  // used by Composer_Executor() via main_level
    USED(ARG(label));
    USED(ARG(deep));

    enum {
        ST_COMPOSE_INITIAL_ENTRY = STATE_0,
        ST_COMPOSE_COMPOSING
    };

    switch (STATE) {
      case ST_COMPOSE_INITIAL_ENTRY: goto initial_entry;
      case ST_COMPOSE_COMPOSING: goto composer_finished;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    if (Is_Blackhole(v))
        return COPY(v);  // sink locations composed to avoid double eval

    if (Any_Word(v) or Is_Activation(v))
        return COPY(v);  // makes it easier to `set compose target`

    Push_Composer_Level(OUT, level_, v, VAL_SPECIFIER(v));

    STATE = ST_COMPOSE_COMPOSING;
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} composer_finished: {  //////////////////////////////////////////////////////

    Finalize_Composer_Level(OUT, SUBLEVEL, v);
    Drop_Level(SUBLEVEL);

    if (Is_Raised(OUT))  // sublevel was killed
        return OUT;

    return OUT;
}}


enum FLATTEN_LEVEL {
    FLATTEN_NOT,
    FLATTEN_ONCE,
    FLATTEN_DEEP
};


static void Flatten_Core(
    Cell* head,
    const Cell* tail,
    Specifier* specifier,
    enum FLATTEN_LEVEL level
) {
    Cell* item = head;
    for (; item != tail; ++item) {
        if (Is_Block(item) and level != FLATTEN_NOT) {
            Specifier* derived = Derive_Specifier(specifier, item);

            const Cell* sub_tail;
            Cell* sub = VAL_ARRAY_AT_Ensure_Mutable(&sub_tail, item);
            Flatten_Core(
                sub,
                sub_tail,
                derived,
                level == FLATTEN_ONCE ? FLATTEN_NOT : FLATTEN_DEEP
            );
        }
        else
            Derelativize(PUSH(), item, specifier);
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
DECLARE_NATIVE(flatten)
{
    INCLUDE_PARAMS_OF_FLATTEN;

    StackIndex base = TOP_INDEX;

    const Cell* tail;
    Cell* at = VAL_ARRAY_AT_Ensure_Mutable(&tail, ARG(block));
    Flatten_Core(
        at,
        tail,
        VAL_SPECIFIER(ARG(block)),
        REF(deep) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(OUT, Pop_Stack_Values(base));
}
