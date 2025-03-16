//
//  File: %n-reduce.h
//  Summary: "REDUCE and COMPOSE natives and associated service routines"
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


#define L_binding         Level_Binding(L)


//
//  /reduce: native [
//
//  "Evaluates expressions, keeping each result (EVAL only gives last result)"
//
//      return: "New list or value"
//          [element?]
//      value "GROUP! and BLOCK! evaluate each item, single values evaluate"
//          [<maybe> element?]
//      :predicate "Applied after evaluation, default is IDENTITY"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(reduce)
{
    INCLUDE_PARAMS_OF_REDUCE;

    Element* v = Element_ARG(value);  // newline flag leveraged [2]
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
        goto reduce_step_result_in_spare;

      case ST_REDUCE_RUNNING_PREDICATE:
        goto process_out;

      default: assert(false);
    }

  initial_entry_non_list: {  /////////////////////////////////////////////////

    // It's not completely clear what the semantics of non-block REDUCE should
    // be, but right now single value REDUCE does a REEVALUATE with no
    // arguments.  This is a variant of REEVAL with an END feed.
    //
    // (R3-Alpha, would return the input, e.g. `reduce ':foo` => :foo)

    if (Any_Inert(v))
        return COPY(v);  // save time if it's something like a TEXT!

    Level* sub = Make_End_Level(
        &Stepper_Executor,
        FLAG_STATE_BYTE(ST_STEPPER_REEVALUATING)
    );
    Push_Level_Erase_Out_If_State_0(OUT, sub);

    Copy_Cell(Evaluator_Level_Current(sub), v);
    sub->u.eval.current_gotten = nullptr;

    return DELEGATE_SUBLEVEL(sub);

} initial_entry_list: {  /////////////////////////////////////////////////////

    Level* sub = Make_Level_At(
        &Stepper_Executor,
        v,  // REB_BLOCK or REB_GROUP
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // reused for each step
            | LEVEL_FLAG_RAISED_RESULT_OK  // predicates (like META) may handle
    );
    Push_Level_Erase_Out_If_State_0(SPARE, sub);
    goto next_reduce_step;

} next_reduce_step: {  ///////////////////////////////////////////////////////

    // 2. We want the output newline status to mirror newlines of the start
    //    of the eval positions.  But when the evaluation callback happens,
    //    we won't have the starting value anymore.  Cache the newline flag on
    //    the ARG(value) cell, as newline flags on ARG()s are available.

    if (Is_Feed_At_End(SUBLEVEL->feed))
        goto finished;

    if (Get_Cell_Flag(At_Feed(SUBLEVEL->feed), NEWLINE_BEFORE))
        Set_Cell_Flag(v, NEWLINE_BEFORE);  // cache newline flag [2]
    else
        Clear_Cell_Flag(v, NEWLINE_BEFORE);

    SUBLEVEL->executor = &Stepper_Executor;
    STATE = ST_REDUCE_EVAL_STEP;
    Reset_Evaluator_Erase_Out(SUBLEVEL);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} reduce_step_result_in_spare: {  ////////////////////////////////////////////

    if (Is_Nulled(predicate))  // default is no processing
        goto process_out;

    if (Is_Barrier(SPARE))  // void and nihil offered to predicate, not commas
        goto next_reduce_step;

    if (
        (Is_Stable(SPARE) and Is_Void(SPARE))  // !!! Review stability issue
        or Is_Nihil(SPARE)
    ){
        const Param* param = First_Unspecialized_Param(
            nullptr,
            Cell_Frame_Phase(predicate)
        );
        if (not Typecheck_Atom_In_Spare_Uses_Scratch(LEVEL, param, SPECIFIED))
            goto next_reduce_step;
    }

    SUBLEVEL->executor = &Just_Use_Out_Executor;
    STATE = ST_REDUCE_RUNNING_PREDICATE;

    return CONTINUE(SPARE, predicate, SPARE);  // arg can be same as output

} process_out: {  ////////////////////////////////////////////////////////////

    // 3. The sublevel that is pushed to run reduce evaluations uses the data
    //    stack position captured in BASELINE to tell things like whether a
    //    function dispatch has pushed refinements, etc.  When the REDUCE
    //    underneath it pushes a value to the data stack, that level must be
    //    informed the stack element is "not for it" before the next call.

    if (Is_Elision(SPARE))
        goto next_reduce_step;  // void results are skipped by reduce

    Decay_If_Unstable(SPARE);

    if (Is_Void(SPARE))
        goto next_reduce_step;

    if (Is_Nulled(SPARE))
        return RAISE(Error_Need_Non_Null_Raw());  // enables e.g. CURTAIL

    if (Is_Splice(SPARE)) {
        const Element* tail;
        const Element* at = Cell_List_At(&tail, SPARE);
        bool newline = Get_Cell_Flag(v, NEWLINE_BEFORE);
        for (; at != tail; ++at) {
            Derelativize(PUSH(), at, Cell_List_Binding(SPARE));
            SUBLEVEL->baseline.stack_base += 1;  // [3]
            if (newline) {
                Set_Cell_Flag(TOP, NEWLINE_BEFORE);  // [2]
                newline = false;
            }
        }
    }
    else if (Is_Antiform(SPARE))
        return RAISE(Error_Bad_Antiform(SPARE));
    else {
        Move_Cell(PUSH(), cast(Element*, SPARE));  // not void, not antiform
        SUBLEVEL->baseline.stack_base += 1;  // [3]

        if (Get_Cell_Flag(v, NEWLINE_BEFORE))  // [2]
            Set_Cell_Flag(TOP, NEWLINE_BEFORE);
    }

    goto next_reduce_step;

} finished: {  ///////////////////////////////////////////////////////////////

    Drop_Level_Unbalanced(SUBLEVEL);  // Drop_Level() asserts on accumulation

    Source* a = Pop_Source_From_Stack(STACK_BASE);
    if (Get_Source_Flag(Cell_Array(v), NEWLINE_AT_TAIL))
        Set_Source_Flag(a, NEWLINE_AT_TAIL);

    Init_Any_List(OUT, Cell_Heart_Ensure_Noquote(v), a);
    Tweak_Cell_Binding(OUT, Cell_Binding(v));
    return OUT;
}}


//
//  /reduce-each: native [
//
//  "Evaluates expressions, keeping each result (EVAL only gives last result)"
//
//      return: "Last body result"
//          [any-atom?]
//      @(vars) "Variable to receive each reduced value (multiple TBD)"
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

    if (Is_Meta_Word(vars))  // Note: gets converted to object in next step
        flags |= LEVEL_FLAG_META_RESULT | LEVEL_FLAG_RAISED_RESULT_OK;

    VarList* context = Virtual_Bind_Deep_To_New_Context(
        ARG(body),  // may be updated, will still be GC safe
        ARG(vars)
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(vars), context));

    assert(Is_Block(body));
    Add_Definitional_Break_Continue(body, level_);

    Executor* executor;
    if (Is_The_Block(block))
        executor = &Inert_Stepper_Executor;
    else {
        assert(Is_Block(block));
        executor = &Stepper_Executor;
    }

    Level* sub = Make_Level_At(executor, block, flags);
    Push_Level_Erase_Out_If_State_0(SPARE, sub);
    goto reduce_next;

} reduce_next: {  ////////////////////////////////////////////////////////////

    if (Is_Feed_At_End(SUBLEVEL->feed))
        goto finished;

    SUBLEVEL->executor = &Stepper_Executor;  // undo &Just_Use_Out_Executor

    STATE = ST_REDUCE_EACH_REDUCING_STEP;
    Reset_Evaluator_Erase_Out(SUBLEVEL);
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
    Enable_Dispatcher_Catching_Of_Throws(LEVEL);  // for break/continue
    return CONTINUE_BRANCH(OUT, body);

} body_result_in_out: {  /////////////////////////////////////////////////////

    if (THROWING) {
        if (not Try_Catch_Break_Or_Continue(OUT, LEVEL, &breaking))
            goto finished;

        if (breaking)
            goto finished;
    }

    Disable_Dispatcher_Catching_Of_Throws(LEVEL);
    goto reduce_next;

} finished: {  ///////////////////////////////////////////////////////////////

    Drop_Level(SUBLEVEL);

    if (THROWING)
        return THROWN;

    if (Is_Cell_Erased(OUT))  // body never ran
        return VOID;

    if (breaking)
        return nullptr;  // BREAK encountered

    return BRANCHED(OUT);
}}


// 1. Here the idea is that `compose $() [@(first [a b])]` will give `[@a]`,
//    so ANY-GROUP? will count for a group pattern.  But once you go a level
//    deeper, `compose $(()) [@(@(first [a b]))] won't match.  It would have
//    to be `[@((first [a b]))]`
//
bool Try_Match_For_Compose(
    Sink(Element) match,  // returns a BLOCK! for use with CONTINUE(...)
    const Element* at,
    const Element* pattern
){
    assert(Any_List(pattern));
    Context* binding = Cell_Binding(pattern);

    if (Is_Group(pattern)) {  // top level only has to match plain heart [1]
        if (not Any_Group_Kind(Cell_Heart(at)))
            return false;
    }
    else if (Is_Fence(pattern)) {
        if (not Any_Fence_Kind(Cell_Heart(at)))
            return false;
    }
    else {
        assert(Is_Block(pattern));
        if (not Any_Block_Kind(Cell_Heart(at)))
            return false;
    }

    Copy_Cell(match, at);

    while (Cell_Series_Len_At(pattern) != 0) {
        if (Cell_Series_Len_At(pattern) != 1)
            fail ("COMPOSE patterns only nested length 1 or 0 right now");

        if (Cell_Series_Len_At(match) == 0)
            return false;  // no nested list or item to match

        const Element* match_1 = Cell_List_Item_At(match);
        const Element* pattern_1 = Cell_List_Item_At(pattern);

        if (Any_List(pattern_1)) {
            if (VAL_TYPE(match_1) != VAL_TYPE(pattern_1))
                return false;
            pattern = pattern_1;
            Copy_Cell(match, match_1);
            continue;
        }
        if (not (Is_Tag(pattern_1) or Is_File(pattern_1)))
            fail ("COMPOSE non-list patterns just TAG! and FILE! atm");

        if (VAL_TYPE(match_1) != VAL_TYPE(pattern_1))
            return false;

        if (CT_Utf8(match_1, pattern_1, 1) != 0)
            return false;

        VAL_INDEX_RAW(match) += 1;
        break;
    }

    QUOTE_BYTE(match) = NOQUOTE_1;  // want to get rid of quasi, too
    HEART_BYTE(match) = REB_BLOCK;
    Tweak_Cell_Binding(match, binding);  // override? combine?
    return true;
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
    const Element* e,  // list or sequence, may be quasi or quoted
    Context* context
){
    Heart heart = Cell_Heart(e);
    const Value* adjusted = nullptr;
    if (Any_Sequence_Kind(heart))  // allow sequences [1]
        adjusted = rebValue(CANON(AS), CANON(BLOCK_X), rebQ(e));
    else
        assert(Any_List_Kind(heart));

    Level* sub = Make_Level_At_Inherit_Const(
        &Composer_Executor,
        adjusted ? adjusted : e,
        Derive_Binding(context, adjusted ? adjusted : e),
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // allows stack accumulation
            | LEVEL_FLAG_RAISED_RESULT_OK  // bubbles up definitional errors
    );
    Push_Level_Erase_Out_If_State_0(out, sub);  // sublevel may raise definitional failure

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
// 1. If you write something like `compose $() '(void)/3:`, it tried to leave
//    behind something like the "SET-INTEGER!" of `3:`.
//
// 2. See Try_Pop_Sequence_Or_Element_Or_Nulled() for how reduced cases like
//    `(void).1` will turn into just INTEGER!, not `.1` -- this is in contrast
//    to `(blank).1` which does turn into `.1`
//
// 3. There are N instances of the NEWLINE_BEFORE flags on the pushed items,
//    and we need N + 1 flags.  Borrow the tail flag from the input array.
//
// 4. It is legal to COMPOSE:DEEP into lists that are antiforms or quoted
//    (or potentially both).  So we transfer the QUOTE_BYTE.
//
//        >> compose:deep $() [a ''~[(1 + 2)]~ b]
//        == [a ''~[3]~ b]
//
static Option(Error*) Trap_Finalize_Composer_Level(
    Sink(Value) out,
    Level* L,
    const Element* composee,  // special handling if the output is a sequence
    bool conflate
){
    Heart heart = Cell_Heart(composee);

    if (Any_Sequence_Kind(heart)) {
        Option(Error*) error = Trap_Pop_Sequence_Or_Element_Or_Nulled(
            out,
            Cell_Heart(composee),
            L->baseline.stack_base
        );
        if (error)
            return error;

        if (
            not Any_Sequence(out)  // so instead, things like [~/~ . ///]
            and not conflate  // do not allow decay to "sequence-looking" words
        ){
            const Element* type = Datatype_From_Kind(VAL_TYPE(out));
            return Error_Conflated_Sequence_Raw(type, out);
        }

        assert(QUOTE_BYTE(composee) & NONQUASI_BIT);  // no antiform/quasiform
        Count num_quotes = Cell_Num_Quotes(composee);

        if (not Is_Nulled(out))  // don't add quoting levels (?)
            Quotify_Depth(out, num_quotes);
        return nullptr;
    }

    Source* a = Pop_Source_From_Stack(L->baseline.stack_base);
    if (Get_Source_Flag(Cell_Array(composee), NEWLINE_AT_TAIL))
        Set_Source_Flag(a, NEWLINE_AT_TAIL);  // proxy newline flag [3]

    Init_Any_List(out, heart, a);

    Tweak_Cell_Binding(out, Cell_Binding(composee));  // preserve binding
    QUOTE_BYTE(out) = QUOTE_BYTE(composee);  // apply quote byte [4]
    return nullptr;
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
//        >> compose $() [a ''(1 + 2) b]
//        == [a ''3 b]
//
// 3. Splicing semantics match the rules for APPEND etc.
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
//        >> compose $() [thing1 (spread block-of-things)]  ; no newline flag
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

  //=//// EXTRACT ARGUMENTS FROM THE ORIGINAL COMPOSE CALL ////////////////=//

    // We have levels for each "recursion" that processes the :DEEP blocks in
    // the COMPOSE.  (These don't recurse as C functions, the levels are
    // stacklessly processed by the trampoline, see %c-trampoline.c)
    //
    // But each level wants to access the arguments to the COMPOSE that
    // kicked off the process.  A pointer to the Level of the main compose is
    // tucked into each Composer_Executor() level to use.
    //
    // !!! IF YOU REARRANGE THESE, YOU HAVE TO UPDATE THE NUMBERING ALSO !!!

    DECLARE_PARAM(1, pattern);
    DECLARE_PARAM(2, template);
    DECLARE_PARAM(3, deep);
    DECLARE_PARAM(4, conflate);
    DECLARE_PARAM(5, predicate);

    Level* main_level = L->u.compose.main_level;  // invoked COMPOSE native

    UNUSED(Level_Arg(main_level, p_template_));  // accounted for by Level feed

    bool deep = not Is_Nulled(Level_Arg(main_level, p_deep_));

    Element* pattern = cast(Element*, Level_Arg(main_level, p_pattern_));

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

    Heart heart = Cell_Heart(at);  // quoted groups match [1]

    if (not Any_Sequence_Or_List_Kind(heart)) {  // won't substitute/recurse
        Copy_Cell(PUSH(), at);  // keep newline flag
        goto handle_next_item;
    }

    if (not Try_Match_For_Compose(SPARE, at, pattern)) {
        if (deep or Any_Sequence_Kind(heart)) {  // sequences same level
            // compose:deep $() [does [(1 + 2)] nested] => [does [3] nested]

            Push_Composer_Level(OUT, main_level, at, L_binding);
            STATE = ST_COMPOSER_RECURSING_DEEP;
            return CONTINUE_SUBLEVEL(SUBLEVEL);
        }

        // compose $() [[(1 + 2)] (3 + 4)] => [[(1 + 2)] 7]  ; non-deep
        //
        Copy_Cell(PUSH(), at);  // keep newline flag
        goto handle_next_item;
    }

    if (Is_Nulled(predicate)) {
        STATE = ST_COMPOSER_EVAL_GROUP;
        return CONTINUE(OUT, cast(Element*, SPARE));
    }

    STATE = ST_COMPOSER_RUNNING_PREDICATE;
    return CONTINUE(OUT, predicate, SPARE);

} process_out: {  ///////////////////////////////////////////////////////////

    assert(
        STATE == ST_COMPOSER_EVAL_GROUP
        or STATE == ST_COMPOSER_RUNNING_PREDICATE
    );

    Heart list_heart = Cell_Heart(At_Level(L));
    Byte list_quote_byte = QUOTE_BYTE(At_Level(L));

    Decay_If_Unstable(OUT);

    if (Is_Splice(OUT))
        goto push_out_spliced;

    if (Is_Nulled(OUT))
        return RAISE(Error_Need_Non_Null_Raw());  // [(null)] => error!

    if (Is_Void(OUT)) {
        if (Any_Plain_Kind(list_heart) and list_quote_byte == NOQUOTE_1) {
            L->u.compose.changed = true;
            goto handle_next_item;  // compose $() [(void)] => []
        }

        // We can actually handle e.g. [''(void)] now as being some levels of
        // quotedness of the apostrophe SIGIL! (e.g. that would be '' which
        // is a single-quoted apostrophe).  Probably not meaningful??
        //
        return FAIL(
            "COMPOSE of quoted VOIDs as quoted apostrophe SIGIL! disabled"
        );
    }
    else if (Is_Antiform(OUT))
        return RAISE(Error_Bad_Antiform(OUT));
    else
        Copy_Cell(PUSH(), cast(Element*, OUT));

    if (Any_Meta_Kind(list_heart))
        Metafy(TOP);
    else if (Any_The_Kind(list_heart))
        Theify(TOP);
    else
        assert(Any_Plain_Kind(list_heart));

    if (list_quote_byte & NONQUASI_BIT)
        Quotify_Depth(TOP, list_quote_byte / 2);  // add to existing quotes
    else {
        if (QUOTE_BYTE(TOP) != NOQUOTE_1)
            return FAIL(
                "COMPOSE cannot quasify items not at quote level 0"
            );
        QUOTE_BYTE(TOP) = list_quote_byte;
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

    // compose $() [(spread [a b]) merges] => [a b merges]... [3]

    if (list_quote_byte != NOQUOTE_1 or not Any_Plain_Kind(list_heart))
        return RAISE("Currently can only splice plain unquoted ANY-LIST?s");

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

    Option(Error*) e = Trap_Finalize_Composer_Level(
        OUT, SUBLEVEL, At_Level(L), conflate
    );
    Drop_Level(SUBLEVEL);

    if (e)
        return FAIL(unwrap e);

    if (Is_Nulled(OUT)) {
        // compose:deep $() [a (void)/(void) b] => path makes null, vaporize it
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
//  /compose: native [  ; !!! IMPORTANT! IF YOU REARRANGE ARGS, SEE [1] !!!
//
//  "Evaluates only contents of GROUP!-delimited expressions in the argument"
//
//      return: "Strange types if :CONFLATE, like ('~)/('~) => ~/~ WORD!"
//      [
//          any-list? any-sequence?
//          any-word?  ; passed through as-is, or :CONFLATE can produce
//          any-string?
//          ~null~ quasi-word? blank! trash?  ; :CONFLATE can produce these
//      ]
//      pattern "Supplies the pattern as well as the binding for composing"
//          [any-list?]
//      template "The template to fill in (no-op if WORD!)"
//          [<maybe> any-list? any-sequence? any-word? any-string?]
//      :deep "Compose deeply into nested lists and sequences"
//      :conflate "Let illegal sequence compositions produce lookalike WORD!s"
//      :predicate "Function to run on composed slots"
//          [<unrun> frame!]
//  ]
//
//  ; Note: :INTO is intentionally no longer supported
//  ; https://forum.rebol.info/t/stopping-the-into-virus/705
//
//  ; Note: :ONLY is intentionally no longer supported
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

    Element* pattern = Element_ARG(pattern);
    Element* t = Element_ARG(template);

    USED(ARG(predicate));  // used by Composer_Executor() via main_level
    USED(ARG(deep));

    enum {
        ST_COMPOSE_INITIAL_ENTRY = STATE_0,
        ST_COMPOSE_COMPOSING_LIST,
        ST_COMPOSE_STRING_SCAN,
        ST_COMPOSE_STRING_EVAL
    };

    switch (STATE) {
      case ST_COMPOSE_INITIAL_ENTRY: {
        if (Any_Word(t))
            return COPY(t);  // makes it easier to `set compose target`

        if (Any_String(t))
            goto string_initial_entry;

        assert(Any_List(t) or Any_Sequence(t));
        goto list_initial_entry; }

      case ST_COMPOSE_COMPOSING_LIST:
        goto list_compose_finished;

      case ST_COMPOSE_STRING_SCAN:
        goto string_scan_result_in_top_scratch_is_transcode_state;

      case ST_COMPOSE_STRING_EVAL:
        goto string_eval_in_out;

      default: assert(false);
    }

  list_initial_entry: {  /////////////////////////////////////////////////////

    Push_Composer_Level(OUT, level_, t, Cell_List_Binding(t));

    STATE = ST_COMPOSE_COMPOSING_LIST;
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} list_compose_finished: {  //////////////////////////////////////////////////

    if (not Is_Raised(OUT)) {  // sublevel was killed
        assert(Is_Trash(OUT));
        Option(Error*) e = Trap_Finalize_Composer_Level(
            OUT, SUBLEVEL, t, REF(conflate)
        );
        if (e)
            return FAIL(unwrap e);
    }

    Drop_Level(SUBLEVEL);
    return OUT;

} string_initial_entry: {  ///////////////////////////////////////////////////

    if (not (Is_Group(pattern) and Cell_Series_Len_At(pattern) == 0))
        return FAIL("Preliminary string compose only works with $()");

    Utf8(const*) head = Cell_Utf8_At(t);
    Utf8(const*) at = head;

    Codepoint c;
    Utf8(const*) next = Utf8_Next(&c, at);

    while (c != '\0') {
        if (c == '(')
            goto found_first_string_pattern;
        at = next;
        next = Utf8_Next(&c, at);
    }

    return rebValue(CANON(COPY), t);  // didn't find anything to substitute.

  found_first_string_pattern: { ///////////////////////////////////////////////

    TranscodeState* ss = Try_Alloc_Memory(TranscodeState);
    Init_Handle_Cdata(SCRATCH, ss, 1);

    const LineNumber start_line = 1;
    Init_Transcode(
        ss,
        ANONYMOUS,  // %tmp-boot.r name in boot overwritten currently by this
        start_line,
        at  // we'll reset this on each pattern find
    );

    Flags flags =
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // query pending newline
        /*| LEVEL_FLAG_RAISED_RESULT_OK*/  // definitional errors?
        | FLAG_STATE_BYTE(ST_SCANNER_OUTERMOST_SCAN)
        | SCAN_EXECUTOR_FLAG_JUST_ONCE;

    Level* sub = Make_Scan_Level(ss, TG_End_Feed, flags);

    Push_Level_Erase_Out_If_State_0(OUT, sub);

    Offset start_offset = at - head;
    Init_Integer(PUSH(), start_offset);
    STATE = ST_COMPOSE_STRING_SCAN;
    return CONTINUE_SUBLEVEL(sub);

}} string_scan_result_in_top_scratch_is_transcode_state: { ///////////////////

    // 1. While transcoding in a general case can't assume the data is valid
    //    UTF-8, we're scanning an already validated UTF-8 TEXT! here.

    TranscodeState* ss = Cell_Handle_Pointer(TranscodeState, SCRATCH);

    Utf8(const*) head = Cell_Utf8_At(t);
    Offset end_offset = ss->at - head;
    Init_Integer(PUSH(), end_offset);

    Utf8(const*) at = cast(Utf8(const*), ss->at);  // validated UTF-8 [1]

    Codepoint c;
    Utf8(const*) next = Utf8_Next(&c, at);

    while (c != '\0') {
        if (c == '(') {
            ss->at = at;
            goto found_subsequent_string_pattern;
        }
        at = next;
        next = Utf8_Next(&c, cast(Utf8(const*), at));
    }

    Drop_Level(SUBLEVEL);
    Free_Memory(TranscodeState, ss);
    goto string_start_evaluations;

  found_subsequent_string_pattern: { /////////////////////////////////////////

    Offset start_offset = ss->at - head;
    Init_Integer(PUSH(), start_offset);
    assert(STATE == ST_COMPOSE_STRING_SCAN);
    return CONTINUE_SUBLEVEL(SUBLEVEL);

}} string_start_evaluations: { ///////////////////////////////////////////////

    // We do all the scans first, and then the evaluations.  This means that
    // no user code is run if the string being interpolated is malformed,
    // which is preferable.  It also helps with locality.  But it means the
    // evaluations have to be done on an already built stack.

    Init_Integer(SCRATCH, 2);
    goto do_string_eval_from_stack_scratch_is_offset;

} string_eval_in_out: { //////////////////////////////////////////////////////

    Decay_If_Unstable(OUT);

    StackIndex index = VAL_INT32(SCRATCH) + STACK_BASE;
    Copy_Cell(Data_Stack_At(Value, index), stable_OUT);

    index += 3;

    if (index > TOP_INDEX)
        goto string_evaluations_done;

    Init_Integer(SCRATCH, index - STACK_BASE);
    goto do_string_eval_from_stack_scratch_is_offset;

} do_string_eval_from_stack_scratch_is_offset: { /////////////////////////////

    StackIndex index = VAL_INT32(SCRATCH) + STACK_BASE;

    assert(Is_Integer(Data_Stack_At(Element, index - 1)));  // start offset
    assert(VAL_TYPE(Data_Stack_At(Element, index)) == VAL_TYPE(pattern));
    assert(Is_Integer(Data_Stack_At(Element, index + 1)));  // end offset

    Copy_Cell(SPARE, Data_Stack_At(Element, index));
    HEART_BYTE(SPARE) = REB_BLOCK;
    Tweak_Cell_Binding(SPARE, Cell_Binding(pattern));

    Init_Integer(SCRATCH, index - STACK_BASE);

    STATE = ST_COMPOSE_STRING_EVAL;
    return CONTINUE(OUT, stable_SPARE);

} string_evaluations_done: { /////////////////////////////////////////////////

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    StackIndex index = STACK_BASE + 2;

    Offset at_offset = 0;

    Size size;
    Utf8(const*) head = Cell_Utf8_Size_At(&size, t);

    for (; index < TOP_INDEX; index += 3) {
        Offset start_offset = VAL_INT32(Data_Stack_At(Element, index - 1));
        Value* eval = Data_Stack_At(Value, index);
        Offset end_offset = VAL_INT32(Data_Stack_At(Element, index + 1));

        Append_UTF8_May_Fail(
            mo->string,
            cast(const char*, head) + at_offset,
            start_offset - at_offset,
            STRMODE_NO_CR
        );

        at_offset = end_offset;

        if (Is_Nulled(eval))
            return RAISE(Error_Need_Non_Null_Raw());

        if (Is_Void(eval))
            continue;

        if (QUOTE_BYTE(eval) != NOQUOTE_1)
            return FAIL("For the moment, COMPOSE string only does NOQUOTE_1");

        Form_Element(mo, cast(Element*, eval));
    }
    Append_UTF8_May_Fail(
        mo->string,
        cast(const char*, head) + at_offset,
        size - at_offset,
        STRMODE_NO_CR
    );

    Drop_Data_Stack_To(STACK_BASE);

    return Init_Text(OUT, Pop_Molded_String(mo));
}}


//
//  /print*: native [
//
//  "Sneaky capturing PRINT with interpolation, native to be sneaky for now"
//
//      return: [~]
//      line [text!]
//  ]
//
DECLARE_NATIVE(print_p)
{
    INCLUDE_PARAMS_OF_PRINT_P;

    Init_Group(SPARE, EMPTY_ARRAY);
    Tweak_Cell_Binding(SPARE, Level_Binding(level_));

    return rebDelegate(CANON(PRINT), CANON(COMPOSE), rebQ(SPARE), ARG(line));
}


enum FLATTEN_LEVEL {
    FLATTEN_NOT,
    FLATTEN_ONCE,
    FLATTEN_DEEP
};


static void Flatten_Core(
    Element* head,
    const Element* tail,
    Context* binding,
    enum FLATTEN_LEVEL level
) {
    Element* item = head;
    for (; item != tail; ++item) {
        if (Is_Block(item) and level != FLATTEN_NOT) {
            Context* derived = Derive_Binding(binding, item);

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
            Derelativize(PUSH(), item, binding);
    }
}


//
//  /flatten: native [
//
//  "Flattens a block of blocks"
//
//      return: [block!]
//      block [block!]
//      :deep
//  ]
//
DECLARE_NATIVE(flatten)
{
    INCLUDE_PARAMS_OF_FLATTEN;

    const Element* tail;
    Element* at = Cell_List_At_Ensure_Mutable(&tail, ARG(block));
    Flatten_Core(
        at,
        tail,
        Cell_List_Binding(ARG(block)),
        REF(deep) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(OUT, Pop_Source_From_Stack(STACK_BASE));
}
