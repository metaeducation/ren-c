//
//  file: %n-reduce.h
//  summary: "REDUCE and COMPOSE natives and associated service routines"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
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
//  reduce: native [
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
DECLARE_NATIVE(REDUCE)
{
    INCLUDE_PARAMS_OF_REDUCE;

    Element* v = Element_ARG(VALUE);  // newline flag leveraged [2]
    Value* predicate = ARG(PREDICATE);

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
        v,  // TYPE_BLOCK or TYPE_GROUP
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // reused for each step
            | LEVEL_FLAG_RAISED_RESULT_OK  // predicates (like META) may handle
    );
    Push_Level_Erase_Out_If_State_0(SPARE, sub);
    goto next_reduce_step;

} next_reduce_step: {  ///////////////////////////////////////////////////////

    // 2. We want the output newline status to mirror newlines of the start
    //    of the eval positions.  But when the evaluation callback happens,
    //    we won't have the starting value anymore.  Cache the newline flag on
    //    the ARG(VALUE) cell, as newline flags on ARG()s are available.

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

    if (Is_Ghost(SPARE))  // void offered to predicate, not commas
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

    Init_Any_List(OUT, Heart_Of_Builtin_Fundamental(v), a);
    Tweak_Cell_Binding(OUT, Cell_Binding(v));
    return OUT;
}}


//
//  reduce-each: native [
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
DECLARE_NATIVE(REDUCE_EACH)
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

    Element* vars = Element_ARG(VARS);
    Element* block = Element_ARG(BLOCK);
    Element* body = Element_ARG(BODY);

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
        body,  // may be updated, will still be GC safe
        vars
    );
    Remember_Cell_Is_Lifeguard(Init_Object(ARG(VARS), context));

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
        Is_Ghost(SPARE)
        or (
            Get_Level_Flag(SUBLEVEL, META_RESULT)
            and Is_Meta_Of_Ghost(SPARE)
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


// 1. Here the idea is that `compose [@(first [a b])]` will give `[@a]`,
//    so ANY-GROUP? will count for a group pattern.  But once you go a level
//    deeper, `compose [@(@(first [a b]))] won't match.  It would have
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
        if (not Any_Group_Type(Heart_Of(at)))
            return false;
    }
    else if (Is_Fence(pattern)) {
        if (not Any_Fence_Type(Heart_Of(at)))
            return false;
    }
    else {
        assert(Is_Block(pattern));
        if (not Any_Block_Type(Heart_Of(at)))
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
            if (Type_Of(match_1) != Type_Of(pattern_1))
                return false;
            pattern = pattern_1;
            Copy_Cell(match, match_1);
            continue;
        }
        if (not (Is_Tag(pattern_1) or Is_File(pattern_1)))
            fail ("COMPOSE non-list patterns just TAG! and FILE! atm");

        if (Type_Of(match_1) != Type_Of(pattern_1))
            return false;

        if (CT_Utf8(match_1, pattern_1, 1) != 0)
            return false;

        VAL_INDEX_RAW(match) += 1;
        break;
    }

    QUOTE_BYTE(match) = NOQUOTE_1;  // want to get rid of quasi, too
    HEART_BYTE(match) = TYPE_BLOCK;
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
    const Element* list_or_seq,  // may be quasi or quoted
    Context* context
){
    possibly(Is_Quoted(list_or_seq) or Is_Quasiform(list_or_seq));

    Heart heart = Heart_Of_Builtin(list_or_seq);

    DECLARE_ELEMENT (adjusted);
    assert(Is_Cell_Erased(adjusted));

    if (Any_Sequence_Type(heart)) {  // allow sequences [1]
        QuoteByte quote_byte = QUOTE_BYTE(list_or_seq);

        DECLARE_ELEMENT (fundamental);
        Copy_Cell(fundamental, list_or_seq);
        QUOTE_BYTE(fundamental) = NOQUOTE_1;

        Option(Error*) e = Trap_Alias_Any_Sequence_As(
            adjusted, list_or_seq, TYPE_BLOCK
        );
        assert(not e);  // all sequences can alias as block
        UNUSED(e);

        QUOTE_BYTE(adjusted) = quote_byte;  // restore quote byte
    }
    else
        assert(Any_List_Type(heart));

    Level* sub = Make_Level_At_Inherit_Const(
        &Composer_Executor,
        Is_Cell_Erased(adjusted) ? list_or_seq : adjusted,
        Derive_Binding(
            context,
            Is_Cell_Erased(adjusted) ? list_or_seq : adjusted
        ),
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // allows stack accumulation
            | LEVEL_FLAG_RAISED_RESULT_OK  // bubbles up definitional errors
    );
    Push_Level_Erase_Out_If_State_0(out, sub);  // sublevel may raise definitional failure

    sub->u.compose.main_level = main_level;   // pass options [2]
    sub->u.compose.changed = false;
}


// Another helper common to the Composer_Executor() and the COMPOSE native
// itself, which pops the processed array depending on the output type.
//
//////////////////////////////////////////////////////////////////////////////
//
// 1. If you write something like `compose @ (void)/3:`, it tried to leave
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
//        >> compose:deep [a ''~[(1 + 2)]~ b]
//        == [a ''~[3]~ b]
//
static Option(Error*) Trap_Finalize_Composer_Level(
    Sink(Value) out,
    Level* L,
    const Element* composee,  // special handling if the output is a sequence
    bool conflate
){
    possibly(Is_Quoted(composee) or Is_Quasiform(composee));
    Heart heart = Heart_Of_Builtin(composee);

    if (Any_Sequence_Type(heart)) {
        Option(Error*) error = Trap_Pop_Sequence_Or_Element_Or_Nulled(
            out,
            Heart_Of_Builtin_Fundamental(composee),
            L->baseline.stack_base
        );
        if (error)
            return error;

        if (
            not Any_Sequence(out)  // so instead, things like [~/~ . ///]
            and not conflate  // don't rewrite as "sequence-looking" words
        ){
            return Error_Conflated_Sequence_Raw(Datatype_Of(out), out);
        }

        assert(QUOTE_BYTE(composee) & NONQUASI_BIT);  // no antiform/quasiform
        Count num_quotes = Quotes_Of(composee);

        if (not Is_Nulled(out))  // don't add quoting levels (?)
            Quotify_Depth(Known_Element(out), num_quotes);
        return SUCCESS;
    }

    Source* a = Pop_Source_From_Stack(L->baseline.stack_base);
    if (Get_Source_Flag(Cell_Array(composee), NEWLINE_AT_TAIL))
        Set_Source_Flag(a, NEWLINE_AT_TAIL);  // proxy newline flag [3]

    Init_Any_List(out, heart, a);

    Tweak_Cell_Binding(out, Cell_Binding(composee));  // preserve binding
    QUOTE_BYTE(out) = QUOTE_BYTE(composee);  // apply quote byte [4]
    return SUCCESS;
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
//        >> compose [thing1 (spread block-of-things)]  ; no newline flag
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
    if (Is_Throwing(L))  // no state to cleanup (just data stack, auto-cleaned)
        return Native_Thrown_Result(L);

    Level* main_level = L->u.compose.main_level;  // invoked COMPOSE native

    bool deep;
    Element* pattern;
    bool conflate;
    Value* predicate;

{ //=//// EXTRACT ARGUMENTS FROM THE ORIGINAL COMPOSE CALL ////////////////=//

    // We have levels for each "recursion" that processes the :DEEP blocks in
    // the COMPOSE.  (These don't recurse as C functions, the levels are
    // stacklessly processed by the trampoline, see %c-trampoline.c)
    //
    // But each level wants to access the arguments to the COMPOSE that
    // kicked off the process.  A pointer to the Level of the main compose is
    // tucked into each Composer_Executor() level to use.

    Level* level_ = main_level;  // level_ is L outside this scope

    INCLUDE_PARAMS_OF_COMPOSE2;

    UNUSED(ARG(TEMPLATE));  // accounted for by Level feed
    deep = Bool_ARG(DEEP);
    pattern = Element_ARG(PATTERN);
    conflate = Bool_ARG(CONFLATE);
    predicate = ARG(PREDICATE);

    assert(Is_Nulled(predicate) or Is_Frame(predicate));

} //=//////////////////////////////////////////////////////////////////////=//

    USE_LEVEL_SHORTHANDS (L);  // defines level_ as L now that args extracted

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

    Option(Heart) heart = Heart_Of(at);  // quoted groups match [1]

    if (not Any_Sequence_Or_List_Type(heart)) {  // won't substitute/recurse
        Copy_Cell(PUSH(), at);  // keep newline flag
        goto handle_next_item;
    }

    if (not Try_Match_For_Compose(SPARE, at, pattern)) {
        if (deep or Any_Sequence_Type(heart)) {  // sequences same level
            // compose:deep [does [(1 + 2)] nested] => [does [3] nested]

            Push_Composer_Level(OUT, main_level, at, L_binding);
            STATE = ST_COMPOSER_RECURSING_DEEP;
            return CONTINUE_SUBLEVEL(SUBLEVEL);
        }

        // compose [[(1 + 2)] (3 + 4)] => [[(1 + 2)] 7]  ; non-deep
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

    QuoteByte list_quote_byte = QUOTE_BYTE(At_Level(L));
    Heart list_heart = Heart_Of_Builtin(At_Level(L));

    Decay_If_Unstable(OUT);

    if (Is_Splice(OUT))
        goto push_out_spliced;

    if (Is_Nulled(OUT))
        return RAISE(Error_Need_Non_Null_Raw());  // [(null)] => error!

    if (Is_Void(OUT)) {
        if (Any_Plain_Type(list_heart) and list_quote_byte == NOQUOTE_1) {
            L->u.compose.changed = true;
            goto handle_next_item;  // compose [(void)] => []
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

    if (Any_Meta_Type(list_heart))
        Metafy(TOP);
    else if (Any_The_Type(list_heart))
        Theify(TOP);
    else
        assert(Any_Plain_Type(list_heart));

    if (list_quote_byte & NONQUASI_BIT)
        Quotify_Depth(TOP_ELEMENT, list_quote_byte / 2);  // adds to existing
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

    // compose [(spread [a b]) merges] => [a b merges]... [3]

    if (list_quote_byte != NOQUOTE_1 or not Any_Plain_Type(list_heart))
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

    assert(Is_Quasar(OUT));  // "return values" are data stack contents

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
        // compose:deep [a (void)/(void) b] => path makes null, vaporize it
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

    return Init_Quasar(OUT);  // signal finished, avoid leaking temp evaluations
}}


//
//  compose2: native [
//
//  "Evaluates only contents of GROUP!-delimited expressions in the argument"
//
//      return: "Strange types if :CONFLATE, like ('~)/('~) => ~/~ WORD!"
//      [
//          any-list? any-sequence?
//          any-word?  ; passed through as-is, or :CONFLATE can produce
//          any-utf8?
//          ~null~ quasi-word? blank! quasar?  ; :CONFLATE can produce these
//      ]
//      pattern "Use ANY-THE-LIST-TYPE? (e.g. @{{}}) to use pattern's binding"
//          [any-list?]
//      template "The template to fill in (no-op if WORD!)"
//          [<maybe> any-list? any-sequence? any-word? any-utf8?]
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
DECLARE_NATIVE(COMPOSE2)
{
    INCLUDE_PARAMS_OF_COMPOSE2;

    Element* pattern = Element_ARG(PATTERN);
    Element* input = Element_ARG(TEMPLATE);  // template is C++ keyword

    USED(ARG(PREDICATE));  // used by Composer_Executor() via main_level
    USED(ARG(DEEP));

    enum {
        ST_COMPOSE2_INITIAL_ENTRY = STATE_0,
        ST_COMPOSE2_COMPOSING_LIST,
        ST_COMPOSE2_STRING_SCAN,
        ST_COMPOSE2_STRING_EVAL
    };

    switch (STATE) {
      case ST_COMPOSE2_INITIAL_ENTRY: {
        assert(Any_List(pattern));

        if (Any_The_Value(pattern)) {  // @() means use pattern's binding
            if (Cell_Binding(pattern) == nullptr)
                return FAIL("@... patterns must have bindings");
            Heart pattern_heart = Heart_Of_Builtin_Fundamental(pattern);
            HEART_BYTE(pattern) = Plainify_Any_The_Heart(pattern_heart);
        }
        else if (Any_Plain_Value(pattern)) {
            Tweak_Cell_Binding(pattern, Level_Binding(level_));
        }
        else
            return FAIL("COMPOSE2 takes plain and @... list patterns only");

        if (Any_Word(input))
            return COPY(input);  // makes it easier to `set compose target`

        if (Any_Utf8(input))
            goto string_initial_entry;

        assert(Any_List(input) or Any_Sequence(input));
        goto list_initial_entry; }

      case ST_COMPOSE2_COMPOSING_LIST:
        goto list_compose_finished;

      case ST_COMPOSE2_STRING_SCAN:
        goto string_scan_results_on_stack;

      case ST_COMPOSE2_STRING_EVAL:
        goto string_eval_in_out;

      default: assert(false);
    }

  list_initial_entry: {  /////////////////////////////////////////////////////

    Push_Composer_Level(OUT, level_, input, Cell_List_Binding(input));

    STATE = ST_COMPOSE2_COMPOSING_LIST;
    return CONTINUE_SUBLEVEL(SUBLEVEL);

} list_compose_finished: {  //////////////////////////////////////////////////

    if (not Is_Raised(OUT)) {  // sublevel was killed
        assert(Is_Quasar(OUT));
        Option(Error*) e = Trap_Finalize_Composer_Level(
            OUT, SUBLEVEL, input, Bool_ARG(CONFLATE)
        );
        if (e)
            return FAIL(unwrap e);
    }

    Drop_Level(SUBLEVEL);
    return OUT;

} string_initial_entry: {  ///////////////////////////////////////////////////

    Utf8(const*) head = Cell_Utf8_At(input);

    TranscodeState* transcode = Try_Alloc_Memory(TranscodeState);
    Init_Handle_Cdata(SCRATCH, transcode, 1);

    const LineNumber start_line = 1;
    Init_Transcode(
        transcode,
        ANONYMOUS,  // %tmp-boot.r name in boot overwritten by this
        start_line,
        head  // we'll assign this after each pattern find
    );

    transcode->saved_levels = nullptr;  // level reuse optimization

    STATE = ST_COMPOSE2_STRING_SCAN;
    goto string_find_next_pattern;

} string_find_next_pattern: {  ///////////////////////////////////////////////

    StackIndex base = TOP_INDEX;  // base above the triples pushed so far

    TranscodeState* transcode = Cell_Handle_Pointer(TranscodeState, SCRATCH);

    Utf8(const*) head = Cell_Utf8_At(input);
    Utf8(const*) at = cast(Utf8(const*), transcode->at);

  //=//// PUSH PATTERN TERMINATORS TO DATA STACK //////////////////////////=//

    // 1. If we're matching @(([])) and we see "((some(([thing]))", then when
    //    we see the "s" that means we didn't see "(([".  So the scan has to
    //    start looking for the first paren again.
    //
    // 2. When we call into the scanner for a pattern like "({[foo]})" we
    //    start it scanning at "foo]})".  The reason we can get away with it
    //    is that we've push levels manually that account for if the scanner
    //    had seen "({[", so it expects to have consumed those tokens and
    //    knows what end delimiters it's looking for.

    Codepoint c;
    Utf8(const*) next = Utf8_Next(&c, at);

    Copy_Cell(PUSH(), pattern); // top of stack is pattern currently matching

    Byte begin_delimiter = Begin_Delimit_For_List(
        Heart_Of_Builtin_Fundamental(TOP)
    );
    Option(Byte) end_delimiter = 0;

    while (true) {
        if (c == '\0') {
            possibly(TOP_INDEX > base + 1);  // compose2 @{{}} "abc {"  ; legal
            Drop_Data_Stack_To(base);
            goto string_scan_finished;
        }

        at = next;

        if (c == begin_delimiter) {
            if (Cell_Series_Len_At(TOP) == 0)  // no more nests in pattern
                break;

            end_delimiter = End_Delimit_For_List(
                Heart_Of_Builtin_Fundamental(TOP)
            );

            const Element* pattern_at = Cell_List_Item_At(TOP);
            Copy_Cell(PUSH(), pattern_at);  // step into pattern

            if (not Any_List(TOP))
                return FAIL("COMPOSE2 pattern must be composed of lists");
            if (Cell_Series_Len_At(TOP) > 1)
                return FAIL("COMPOSE2 pattern layers must be length 1 or 0");

            begin_delimiter = Begin_Delimit_For_List(
                Heart_Of_Builtin_Fundamental(TOP)
            );
        }
        else if (end_delimiter and c == end_delimiter) {
            DROP();
            begin_delimiter = Begin_Delimit_For_List(
                Heart_Of_Builtin_Fundamental(TOP)
            );
            if (TOP_INDEX == base + 1)
                end_delimiter = 0;
            else
                end_delimiter = End_Delimit_For_List(
                    Heart_Of_Builtin_Fundamental(
                        Data_Stack_At(Element, base - 1)
                    )
            );
        }
        else if (end_delimiter) {  // back the pattern out to the start [1]
            Drop_Data_Stack_To(base + 1);
            begin_delimiter = Begin_Delimit_For_List(
                Heart_Of_Builtin_Fundamental(TOP)
            );
            end_delimiter = 0;
        }

        next = Utf8_Next(&c, at);
    }

    transcode->at = at;  // scanner needs at, e.g. "a])", not "([a])", see [2]

    Count pattern_depth = TOP_INDEX - base;  // number of pattern levels pushed
    Utf8(const*) start = at - pattern_depth;  // start replacement at "([a])"

  //=//// ALLOCATE OR PUSH LEVELS FOR EACH PATTERN END DELIMITER //////////=//

    // We don't want to allocate or push a scanner level until we are sure
    // it's necessary.  (If no patterns are found, all we need to do is COPY
    // the string if there aren't any substitutions.)

    if (not transcode->saved_levels) {  // first match... no Levels yet
        StackIndex stack_index = base;
        for (; stack_index != TOP_INDEX; ++stack_index) {
            Element* pattern_at = Data_Stack_At(Element, stack_index + 1);
            Byte terminal = End_Delimit_For_List(
                Heart_Of_Builtin_Fundamental(pattern_at)
            );

            Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
                /*| LEVEL_FLAG_RAISED_RESULT_OK*/  // definitional errors?
                | FLAG_STATE_BYTE(Scanner_State_For_Terminal(terminal));

            if (stack_index != TOP_INDEX - 1)
                flags |= SCAN_EXECUTOR_FLAG_SAVE_LEVEL_DONT_POP_ARRAY;

            Level* sub = Make_Scan_Level(transcode, TG_End_Feed, flags);
            sub->baseline.stack_base = base;  // we will drop to this

            Push_Level_Erase_Out_If_State_0(OUT, sub);

          #if RUNTIME_CHECKS
            --pattern_depth;
          #endif
        }
    }
    else {  // Subsequent scan
        Level* sub = transcode->saved_levels;
        while (sub) {
            Level* prior = sub->prior;
            transcode->saved_levels = prior;
            sub->baseline.stack_base = base;  // we drop to here before scan
            Push_Level_Erase_Out_If_State_0(OUT, sub);
            sub = prior;

          #if RUNTIME_CHECKS
            --pattern_depth;
          #endif
        }
    }

  #if RUNTIME_CHECKS
    assert(pattern_depth == 0);
  #endif

    Drop_Data_Stack_To(base);  // clear end delimiters off the stack

    Offset start_offset = start - head;
    Init_Integer(SPARE, start_offset);  // will push in a triple after scan

    assert(STATE = ST_COMPOSE2_STRING_SCAN);
    return CONTINUE_SUBLEVEL(TOP_LEVEL);

} string_scan_results_on_stack: { ////////////////////////////////////////////

    // 1. While transcoding in a general case can't assume the data is valid
    //    UTF-8, we're scanning an already validated ANY-UTF8? value here.
    //
    // 2. Each pattern found will push 3 values to the data stack: the
    //    start offset where the pattern first begins, the code that was
    //    scanned from inside the pattern, and the offset right after the
    //    end character of where the pattern matched.

    TranscodeState* transcode = Cell_Handle_Pointer(TranscodeState, SCRATCH);
    Element* elem_start_offset = Known_Element(SPARE);
    assert(Is_Integer(elem_start_offset));

    Utf8(const*) at = cast(Utf8(const*), transcode->at);  // valid UTF-8 [1]
    Utf8(const*) head = Cell_Utf8_At(input);
    Offset end_offset = at - head;

    Source* a = Pop_Managed_Source_From_Stack(SUBLEVEL->baseline.stack_base);
    if (Get_Executor_Flag(SCAN, SUBLEVEL, NEWLINE_PENDING))
        Set_Source_Flag(a, NEWLINE_AT_TAIL);

    Level* sub = SUBLEVEL;
    g_ts.top_level = sub->prior;
    sub->prior = transcode->saved_levels;
    transcode->saved_levels = sub;

    Copy_Cell(PUSH(), elem_start_offset);  // push start, code, end [2]
    Init_Block(PUSH(), a);
    Init_Integer(PUSH(), end_offset);

    if (*at != '\0')
        goto string_find_next_pattern;

    goto string_scan_finished;

} string_scan_finished: { ///////////////////////////////////////////////////

    // 1. !!! If we never found our pattern, should we validate that the
    //    pattern was legal?  Or we could just say that if you use an illegal
    //    pattern but no instances come up, that's ok?

    TranscodeState* transcode = Cell_Handle_Pointer(TranscodeState, SCRATCH);

    if (TOP_INDEX == STACK_BASE) {  // no triples pushed, so no matches [1]
        assert(not transcode->saved_levels);
        Free_Memory(TranscodeState, transcode);
        return rebValue(CANON(COPY), input);
    }

    while (transcode->saved_levels) {
        Level* sub = transcode->saved_levels;
        transcode->saved_levels = sub->prior;
        Free_Level_Internal(sub);
    }

    Free_Memory(TranscodeState, transcode);

    Init_Integer(SCRATCH, STACK_BASE + 1);  // stackindex of first triple
    goto do_string_eval_scratch_is_stackindex;

} do_string_eval_scratch_is_stackindex: { ////////////////////////////////////

    // We do all the scans first, and then the evaluations.  This means that
    // no user code is run if the string being interpolated is malformed,
    // which is preferable.  It also helps with locality.  But it means the
    // evaluations have to be done on an already built stack.

    StackIndex triples = VAL_INT32(SCRATCH);

    assert(Is_Integer(Data_Stack_At(Element, triples)));  // start offset
    OnStack(Element*) code = Data_Stack_At(Element, triples + 1);
    assert(Is_Block(code));  // code to evaluate
    assert(Is_Integer(Data_Stack_At(Element, triples + 2)));  // end offset

    Tweak_Cell_Binding(code, Cell_Binding(pattern));  // bind unbound code

    STATE = ST_COMPOSE2_STRING_EVAL;
    return CONTINUE_CORE(
        OUT,
        LEVEL_FLAG_RAISED_RESULT_OK,  // we will bubble out raised errors
        SPECIFIED,
        Copy_Cell(SPARE, code)  // pass non-stack code
    );

} string_eval_in_out: { //////////////////////////////////////////////////////

    if (Is_Raised(OUT)) {
        Drop_Data_Stack_To(STACK_BASE);
        return OUT;
    }

    Value* result = Decay_If_Unstable(OUT);

    StackIndex triples = VAL_INT32(SCRATCH);
    assert(Is_Block(Data_Stack_At(Element, triples + 1)));  // evaluated code
    Copy_Cell(Data_Stack_At(Value, triples + 1), result);  // replace w/eval

    triples += 3;  // skip to next set of 3
    if (triples > TOP_INDEX)
        goto string_evaluations_done;

    Init_Integer(SCRATCH, triples);
    goto do_string_eval_scratch_is_stackindex;

} string_evaluations_done: { /////////////////////////////////////////////////

    // 1. "File calculus" says that if we are splicing a FILE! into a FILE!,
    //    then if the splice ends in slash the template must have a slash
    //    after the splicing slot.  MORE RULES TO BE ADDED...

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    StackIndex triples = STACK_BASE + 1;  // [start_offset, code, end_offset]

    Offset at_offset = 0;

    Size size;
    Utf8(const*) head = Cell_Utf8_Size_At(&size, input);

    for (; triples < TOP_INDEX; triples += 3) {
        Offset start_offset = VAL_INT32(Data_Stack_At(Element, triples));
        Value* eval = Data_Stack_At(Value, triples + 1);
        Offset end_offset = VAL_INT32(Data_Stack_At(Element, triples + 2));

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

        if (Is_File(eval) and Is_File(input)) {  // "File calculus" [1]
            const Byte* at = c_cast(Byte*, head) + at_offset;
            bool eval_slash_tail = (
                Cell_Series_Len_At(eval) != 0
                and Cell_String_Tail(eval)[-1] == '/'
            );
            bool slash_after_splice = (at[0] == '/');

            if (eval_slash_tail) {
                if (not slash_after_splice)
                    return FAIL(
                        "FILE! spliced into FILE! must end in slash"
                        " if splice slot is followed by slash"
                    );
                ++at_offset;  // skip the slash (use the one we're forming)
            }
            else {
                if (slash_after_splice)
                    return FAIL(
                        "FILE! spliced into FILE! can't end in slash"
                        " unless splice slot followed by slash"
                    );
            }
        }

        Form_Element(mo, cast(Element*, eval));
    }
    Append_UTF8_May_Fail(
        mo->string,
        cast(const char*, head) + at_offset,
        size - at_offset,
        STRMODE_NO_CR
    );

    Drop_Data_Stack_To(STACK_BASE);

    String* str = Pop_Molded_String(mo);
    if (not Any_String(input))
        Freeze_Flex(str);

    Heart input_heart = Heart_Of_Builtin_Fundamental(input);
    return Init_Series_At_Core(OUT, input_heart, str, 0, nullptr);
}}


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
//  flatten: native [
//
//  "Flattens a block of blocks"
//
//      return: [block!]
//      block [block!]
//      :deep
//  ]
//
DECLARE_NATIVE(FLATTEN)
{
    INCLUDE_PARAMS_OF_FLATTEN;

    const Element* tail;
    Element* at = Cell_List_At_Ensure_Mutable(&tail, ARG(BLOCK));
    Flatten_Core(
        at,
        tail,
        Cell_List_Binding(ARG(BLOCK)),
        Bool_ARG(DEEP) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    return Init_Block(OUT, Pop_Source_From_Stack(STACK_BASE));
}
