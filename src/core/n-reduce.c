//
//  File: %n-reduce.h
//  Summary: {REDUCE and COMPOSE natives and associated service routines}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2023 Ren-C Open Source Contributors
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
//  "Evaluates expressions, keeping each result (EVAL only gives last result)"
//
//      return: "New list or value"
//          [element?]
//      value "GROUP! and BLOCK! evaluate each item, single values evaluate"
//          [<maybe> element?]
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
// 3. There is an assert which checks to make sure that no values overwrite
//    a raised error--cells must be cleared first (see Freshen_Cell()).  This
//    makes it tough in situations where you are trying to pass a const
//    cell pointer to something like a continuation--it makes a copy of the
//    raised error, leaving it in its old location.  The aggressive assert
//    may be infeasible long-term but it has been helpful in designing
//    raised errors--workaround it for now.
//
// 4. The sublevel that is pushed to run the reduce evaluations uses the data
//    stack position captured in BASELINE to tell things like whether a
//    function dispatch has pushed refinements, etc.  So when the REDUCE level
//    underneath it pushes a value to the data stack, that level must be
//    informed the stack element is "not for it" before the next call.
{
    INCLUDE_PARAMS_OF_REDUCE;

    Element* v = cast(Element*, ARG(value));  // newline flag leveraged [2]
    Value* predicate = ARG(predicate);

    enum {
        ST_REDUCE_INITIAL_ENTRY = STATE_0,
        ST_REDUCE_EVAL_STEP,
        ST_REDUCE_RUNNING_PREDICATE
    };

    switch (STATE) {
      case ST_REDUCE_INITIAL_ENTRY:
        if (Any_List(v))
            goto initial_entry_list;
        goto initial_entry_non_list;  // semantics in question [1]

      case ST_REDUCE_EVAL_STEP:
        goto reduce_step_result_in_out;

      case ST_REDUCE_RUNNING_PREDICATE:
        goto process_out;

      default: assert(false);
    }

  initial_entry_non_list: {  /////////////////////////////////////////////////

    if (Any_Inert(v))
        return COPY(v);  // save time if it's something like a TEXT!

    Level* sub = Make_End_Level(
        &Stepper_Executor,
        FLAG_STATE_BYTE(ST_STEPPER_REEVALUATING)
    );
    Push_Level(OUT, sub);

    Copy_Cell(&sub->u.eval.current, v);
    sub->u.eval.current_gotten = nullptr;
    sub->u.eval.enfix_reevaluate = 'N';  // detect?

    return DELEGATE_SUBLEVEL(sub);

} initial_entry_list: {  /////////////////////////////////////////////////////

    Level* sub = Make_Level_At(
        &Stepper_Executor,
        v,  // REB_BLOCK or REB_GROUP
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // reused for each step
            | LEVEL_FLAG_RAISED_RESULT_OK  // predicates (like META) may handle
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

    SUBLEVEL->executor = &Stepper_Executor;
    STATE = ST_REDUCE_EVAL_STEP;
    Restart_Stepper_Level(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} reduce_step_result_in_out: {  //////////////////////////////////////////////

    if (Is_Nulled(predicate))  // default is no processing
        goto process_out;

    if (Is_Barrier(OUT))  // voids and nihils offered to predicate, not commas
        goto next_reduce_step;

    if (
        (Is_Stable(OUT) and Is_Void(OUT))  // !!! Review stability issue
        or Is_Nihil(OUT)
    ){
        const Param* param = First_Unspecialized_Param(
            nullptr,
            VAL_ACTION(predicate)
        );
        if (not Typecheck_Atom(param, OUT))
            goto next_reduce_step;
    }

    SUBLEVEL->executor = &Just_Use_Out_Executor;
    STATE = ST_REDUCE_RUNNING_PREDICATE;

    if (Is_Raised(OUT)) {  // workaround for assert about error clearing [3]
        Move_Cell(SPARE, OUT);
        return CONTINUE(OUT, predicate, SPARE);
    }

    return CONTINUE(OUT, predicate, OUT);  // arg can be same as output

} process_out: {  ////////////////////////////////////////////////////////////

    Erase_Cell(SPARE);  // in case it was used for error [3]

    if (Is_Elision(OUT))
        goto next_reduce_step;  // void results are skipped by reduce

    Decay_If_Unstable(OUT);

    if (Is_Void(OUT))
        goto next_reduce_step;

    if (Is_Nulled(OUT))
        return RAISE(Error_Need_Non_Null_Raw());  // error enables e.g. CURTAIL

    if (Is_Splice(OUT)) {
        const Element* tail;
        const Element* at = Cell_List_At(&tail, OUT);
        bool newline = Get_Cell_Flag(v, NEWLINE_BEFORE);
        for (; at != tail; ++at) {
            Derelativize(PUSH(), at, Cell_Specifier(OUT));
            SUBLEVEL->baseline.stack_base += 1;  // [4]
            if (newline) {
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);  // [2]
                newline = false;
            }
        }
    }
    else if (Is_Antiform(OUT))
        return RAISE(Error_Bad_Antiform(OUT));
    else {
        Move_Cell(PUSH(), cast(Element*, OUT));  // not void, not antiform
        SUBLEVEL->baseline.stack_base += 1;  // [4]

        if (Get_Cell_Flag(v, NEWLINE_BEFORE))  // [2]
            Set_Cell_Flag(TOP, NEWLINE_BEFORE);
    }

    goto next_reduce_step;

} finished: {  ///////////////////////////////////////////////////////////////

    Drop_Level_Unbalanced(SUBLEVEL);  // Drop_Level() asserts on accumulation

    Flags pop_flags = NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE;
    if (Get_Array_Flag(Cell_Array(v), NEWLINE_AT_TAIL))
        pop_flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;

    Init_Any_List(
        OUT,
        Cell_Heart_Ensure_Noquote(v),
        Pop_Stack_Values_Core(STACK_BASE, pop_flags)
    );
    BINDING(OUT) = BINDING(v);
    return OUT;
}}


//
//  reduce-each: native [
//
//  "Evaluates expressions, keeping each result (EVAL only gives last result)"
//
//      return: "Last body result"
//          [any-atom?]
//      ':vars "Variable to receive each reduced value (multiple TBD)"
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
// see source-level commas.  Once comma antiforms took over the barrier role,
// they were distinguishable from nihils and could be filtered separately.
// With this you can write `pack [1, ~[]~, 2]` and get a 3-element pack.
// It may be that some use case requires /COMMAS to come back, but waiting
// to see one.
//
// 1. This current REDUCE-EACH only works with one variable; it should be able
//    to take a block of variables.
{
    INCLUDE_PARAMS_OF_REDUCE_EACH;

    Value* vars = ARG(vars);
    Value* block = ARG(block);
    Value* body = ARG(body);

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

    VarList* context = Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        ARG(vars)
    );
    Init_Object(ARG(vars), context);  // keep GC safe

    if (Is_Block(body) or Is_Meta_Block(body))
        Add_Definitional_Break_Continue(body, level_);

    if (Is_The_Block(block))
        flags |= EVAL_EXECUTOR_FLAG_NO_EVALUATIONS;

    Level* sub = Make_Level_At(&Stepper_Executor, block, flags);
    Push_Level(SPARE, sub);
    goto reduce_next;

} reduce_next: {  ////////////////////////////////////////////////////////////

    if (Is_Feed_At_End(SUBLEVEL->feed))
        goto finished;

    SUBLEVEL->executor = &Stepper_Executor;  // undo &Just_Use_Out_Executor

    STATE = ST_REDUCE_EACH_REDUCING_STEP;
    Restart_Stepper_Level(SUBLEVEL);
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
        goto reduce_next;  // always cull antiform commas (barriers)
    }

    if (
        Not_Level_Flag(SUBLEVEL, META_RESULT)
        and (Is_Void(SPARE) or Is_Nihil(SPARE))
    ){
        Init_Nihil(OUT);
        goto reduce_next;  // cull voids and nihils if not ^META
    }

    Decay_If_Unstable(SPARE);

    Move_Cell(Varlist_Slot(Cell_Varlist(vars), 1), stable_SPARE);  // multiple? [1]

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


bool Match_For_Compose(const Cell* group, const Element* label) {
    assert(Any_Group_Kind(HEART_BYTE(group)));

    assert(Is_Tag(label) or Is_File(label));

    if (Cell_Series_Len_At(group) == 0) // you have a pattern, so leave `()` as-is
        return false;

    const Element* first = Cell_List_Item_At(group);
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
    Atom* out,
    Level* main_level,
    const Value* arraylike,
    Specifier* specifier
){
    const Value* adjusted = nullptr;
    if (Any_Sequence(arraylike)) {  // allow sequences [1]
        adjusted = rebValue(Canon(AS), Canon(BLOCK_X), rebQ(arraylike));
    }

    Level* sub = Make_Level_At_Core(
        &Composer_Executor,
        adjusted ? adjusted : arraylike,
        Derive_Specifier(specifier, adjusted ? adjusted : arraylike),
        EVAL_EXECUTOR_FLAG_NO_EVALUATIONS
            | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // allows stack accumulation
            | LEVEL_FLAG_RAISED_RESULT_OK  // bubbles up definitional errors
    );
    Push_Level(out, sub);  // sublevel may raise definitional failure

    if (adjusted)
        rebRelease(adjusted);

    sub->u.compose.main_level = main_level;   // pass options [2]
    sub->u.compose.changed = false;
}


// Another helper common to the Composer_Executor() and the COMPOSE native
// itself, which pops the processed array depending on the output type.
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. If you write something like `compose @ (void)/3:`, it would try to leave
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
// 4. It is legal to COMPOSE/DEEP into lists that are antiforms or quoted
//    (or potentially both).  So we transfer the QUOTE_BYTE.
//
//        >> compose/deep [a ''~[(1 + 2)]~ b]
//        == [a ''~[3]~ b]
//
static void Finalize_Composer_Level(
    Sink(Value*) out,
    Level* L,
    const Element* composee,  // special handling if the output is a sequence
    bool conflate
){
    Heart heart = Cell_Heart(composee);

    if (Any_Sequence_Kind(heart)) {
        Option(VarList*) error = Trap_Pop_Sequence_Or_Element_Or_Nulled(
            out,
            Cell_Heart(composee),
            L->baseline.stack_base
        );
        if (error)
            fail (unwrap error);

        if (
            not Any_Sequence(out)  // so instead, things like [~/~ . ///]
            and not conflate  // do not allow decay to "sequence-looking" words
        ){
            fail (Error_Conflated_Sequence_Raw(out));
        }

        assert(QUOTE_BYTE(composee) & NONQUASI_BIT);  // no antiform/quasiform
        Count num_quotes = Cell_Num_Quotes(composee);

        if (not Is_Nulled(out))  // don't add quoting levels (?)
            Quotify(out, num_quotes);
        return;
    }

    Flags flags = NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE;
    if (Get_Array_Flag(Cell_Array(composee), NEWLINE_AT_TAIL))
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;  // proxy newline flag [3]

    Init_Any_List(
        out,
        heart,
        Pop_Stack_Values_Core(L->baseline.stack_base, flags)
    );

    BINDING(out) = BINDING(composee);  // preserve binding
    QUOTE_BYTE(out) = QUOTE_BYTE(composee);  // apply quote byte [4]
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

  //=//// EXTRACT REFINEMENTS FROM THE COMPOSE CALL ///////////////////////=//

    // We have levels for each "recursion" that processes the /DEEP blocks in
    // the COMPOSE.  (These don't recurse as C functions, the levels are
    // stacklessly processed by the trampoline, see %c-trampoline.c)
    //
    // But each level wants to access the refinements to the COMPOSE that
    // kicked off the process.  A pointer to the Level of the main compose is
    // tucked into each Composer_Executor() level to use.
    //
    // !!! IF YOU REARRANGE THESE, YOU HAVE TO UPDATE THE NUMBERING ALSO !!!

    DECLARE_PARAM(1, return);
    DECLARE_PARAM(2, value);
    DECLARE_PARAM(3, deep);
    DECLARE_PARAM(4, label);
    DECLARE_PARAM(5, conflate);
    DECLARE_PARAM(6, predicate);

    Level* main_level = L->u.compose.main_level;  // invoked COMPOSE native

    UNUSED(Level_Arg(main_level, p_return_));
    UNUSED(Level_Arg(main_level, p_value_));

    bool deep = not Is_Nulled(Level_Arg(main_level, p_deep_));

    Option(Element*) label;
    if (Is_Nulled(Level_Arg(main_level, p_label_)))
        label = nullptr;
    else
        label = cast(Element*, Level_Arg(main_level, p_label_));

    bool conflate;
    if (Is_Nulled(Level_Arg(main_level, p_conflate_)))
        conflate = false;
    else {
        assert(Is_Okay(Level_Arg(main_level, p_conflate_)));
        conflate = true;
    }

    Value* predicate = Level_Arg(main_level, p_predicate_);
    assert(Is_Nulled(predicate) or Is_Frame(predicate));

  //=//////////////////////////////////////////////////////////////////////=//

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

   Fetch_Next_In_Feed(L->feed);
   goto handle_current_item;

} handle_current_item: {  ////////////////////////////////////////////////////

    if (Is_Level_At_End(L))
        goto finished;

    const Element* at = At_Level(L);

    if (not Any_Listlike(at)) {  // won't substitute/recurse
        Copy_Cell(PUSH(), at);  // keep newline flag
        goto handle_next_item;
    }

    Heart heart = Cell_Heart(at);  // quoted groups match [1]

    Specifier* match_specifier = nullptr;
    const Element* match = nullptr;

    if (not Any_Group_Kind(heart)) {
        //
        // Don't compose at this level, but may need to walk deeply to
        // find compositions if /DEEP and it's an array
    }
    else {  // plain compose, if match
        if (not label or Match_For_Compose(at, unwrap label)) {
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
        Copy_Cell(PUSH(), at);  // keep newline flag
        goto handle_next_item;
    }

    if (Is_Nulled(predicate))
        goto evaluate_group;

    Derelativize(SPARE, match, L_specifier);
    Dequotify(SPARE);  // cast was needed because there may have been quotes
    HEART_BYTE(SPARE) = REB_GROUP;  // don't confuse with decoration
    if (label)
        VAL_INDEX_RAW(SPARE) += 1;  // wasn't possibly at END

    STATE = ST_COMPOSER_RUNNING_PREDICATE;
    return CONTINUE(OUT, predicate, SPARE);

  evaluate_group: { //////////////////////////////////////////////////////////

    // If <*> is the label and (<*> 1 + 2) is found, run just (1 + 2).
    //
    Feed* subfeed = Make_At_Feed_Core(match, match_specifier);
    if (label)
        Fetch_Next_In_Feed(subfeed);  // wasn't possibly at END

    Init_Void(Alloc_Evaluator_Primed_Result());
    Level* sublevel = Make_Level(
        &Evaluator_Executor,
        subfeed,  // used subfeed so we could skip the label if there was one
        LEVEL_MASK_NONE
    );

    Push_Level(OUT, sublevel);

    STATE = ST_COMPOSER_EVAL_GROUP;
    return CATCH_CONTINUE_SUBLEVEL(sublevel);

}} process_out: {  ///////////////////////////////////////////////////////////

    assert(
        STATE == ST_COMPOSER_EVAL_GROUP
        or STATE == ST_COMPOSER_RUNNING_PREDICATE
    );

    Heart group_heart = Cell_Heart(At_Level(L));
    Byte group_quote_byte = QUOTE_BYTE(At_Level(L));

    Decay_If_Unstable(OUT);

    if (Is_Splice(OUT))
        goto push_out_spliced;

    if (Is_Nulled(OUT))
        return RAISE(Error_Need_Non_Null_Raw());  // [(null)] => error!

    if (Is_Void(OUT)) {
        if (group_heart == REB_GROUP and group_quote_byte == NOQUOTE_1) {
            L->u.compose.changed = true;
            goto handle_next_item;  // compose [(void)] => []
        }

        // We can actually handle e.g. [''(void)] now as being some levels of
        // quotedness of the apostrophe SIGIL! (e.g. that would be '' which
        // is a single-quoted apostrophe).  Probably not meaningful??
        //
        fail ("COMPOSE of quoted VOIDs as quoted apostrophe SIGIL! disabled");
    }
    else if (Is_Antiform(OUT))
        return RAISE(Error_Bad_Antiform(OUT));
    else
        Copy_Cell(PUSH(), cast(Element*, OUT));

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
        if (QUOTE_BYTE(TOP) != NOQUOTE_1)
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

    if (group_quote_byte != NOQUOTE_1 or group_heart != REB_GROUP)
        return RAISE("Currently can only splice plain unquoted GROUP!s");

    assert(Is_Splice(OUT));  // GROUP! at "quoting level -1" means splice

    const Element* push_tail;
    const Element* push = Cell_List_At(&push_tail, OUT);
    if (push != push_tail) {
        Copy_Cell(PUSH(), push);
        if (Get_Cell_Flag(At_Level(L), NEWLINE_BEFORE))
            Set_Cell_Flag(TOP, NEWLINE_BEFORE);  // first [4]
        else
            Clear_Cell_Flag(TOP, NEWLINE_BEFORE);

        while (++push, push != push_tail)
            Copy_Cell(PUSH(), push);
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

    assert(Is_Trash(OUT));  // "return values" are data stack contents

    if (not SUBLEVEL->u.compose.changed) {
        //
        // To save on memory usage, Ren-C does not make copies of
        // arrays that don't have some substitution under them.  This
        // may be controlled by a switch if it turns out to be needed.
        //
        Drop_Data_Stack_To(SUBLEVEL->baseline.stack_base);
        Drop_Level(SUBLEVEL);

        Copy_Cell(PUSH(), At_Level(L));
        // Constify(TOP);
        goto handle_next_item;
    }

    Finalize_Composer_Level(OUT, SUBLEVEL, At_Level(L), conflate);
    Drop_Level(SUBLEVEL);

    if (Is_Nulled(OUT)) {
        // compose/deep [a (void)/(void) b] => path makes null, vaporize it
    }
    else {
        assert(not Is_Antiform(OUT));
        Move_Cell(PUSH(), stable_OUT);
    }

    if (Get_Cell_Flag(At_Level(L), NEWLINE_BEFORE))
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);

    L->u.compose.changed = true;
    goto handle_next_item;

} finished: {  ///////////////////////////////////////////////////////////////

    assert(Get_Level_Flag(L, TRAMPOLINE_KEEPALIVE));  // caller needs [5]

    return Init_Trash(OUT);  // signal finished, avoid leaking temp evaluations
}}


//
//  compose: native [  ; !!! IMPORTANT! IF YOU REARRANGE ARGS, SEE [1] !!!
//
//  "Evaluates only contents of GROUP!-delimited expressions in the argument"
//
//      return: "Strange types if /CONFLATE, like ('~)/('~) => ~/~ WORD!"
//      [
//          any-list? any-sequence?
//          any-word?  ; passed through as-is, or /CONFLATE can produce
//          ~null~ quasi-word? blank! trash?  ; /CONFLATE can produce these
//      ]
//      template "The template to fill in (no-op if WORD!)"
//          [<maybe> any-list? any-sequence? any-word? action?]
//      /deep "Compose deeply into nested lists and sequences"
//      /label "Distinguish compose groups, e.g. [(plain) (<*> composed)]"
//          [tag! file!]
//      /conflate "Let illegal sequence compositions produce lookalike WORD!s"
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
//
// 1. Composer_Executor() accesses the arguments of the COMPOSE that spawned
//    it by index.  The trick used to name arguments and pick up `level_->`
//    does not work there because level_ is the level of an executor with
//    no varlist.  There's diminishing returns to coming up with a super
//    clever way to work around this, so instead heed this warning and go
//    update Composer_Executor() if these arguments are reordered.
{
    INCLUDE_PARAMS_OF_COMPOSE;

    Element* t = cast(Element*, ARG(template));

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

    if (Any_Word(t))
        return COPY(t);  // makes it easier to `set compose target`

    Push_Composer_Level(OUT, level_, t, Cell_Specifier(t));

    STATE = ST_COMPOSE_COMPOSING;
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} composer_finished: {  //////////////////////////////////////////////////////

    if (not Is_Raised(OUT)) {  // sublevel was killed
        assert(Is_Trash(OUT));
        Finalize_Composer_Level(OUT, SUBLEVEL, t, REF(conflate));
    }

    Drop_Level(SUBLEVEL);
    return OUT;
}}


enum FLATTEN_LEVEL {
    FLATTEN_NOT,
    FLATTEN_ONCE,
    FLATTEN_DEEP
};


static void Flatten_Core(
    Element* head,
    const Element* tail,
    Specifier* specifier,
    enum FLATTEN_LEVEL level
) {
    Element* item = head;
    for (; item != tail; ++item) {
        if (Is_Block(item) and level != FLATTEN_NOT) {
            Specifier* derived = Derive_Specifier(specifier, item);

            const Element* sub_tail;
            Element* sub = Cell_List_At_Ensure_Mutable(&sub_tail, item);
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
//  "Flattens a block of blocks"
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

    const Element* tail;
    Element* at = Cell_List_At_Ensure_Mutable(&tail, ARG(block));
    Flatten_Core(
        at,
        tail,
        Cell_Specifier(ARG(block)),
        REF(deep) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(OUT, Pop_Stack_Values(base));
}
