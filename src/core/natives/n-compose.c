//
//  file: %n-compose.h
//  summary: "COMPOSE native for lists, sequences, and strings"
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
// Ren-C's COMPOSE has greatly expanded capabilities compared to traditional
// Redbol.  There's too many features to list, but a few are:
//
// * It supports customized patterns for what slots to match, based on
//   arbitrary nesting of list types and signals, using COMPOSE2:
//
//      >> compose2 '{{}} [(1 + 2) {3 + 4} {{5 + 6}} [7 + 8]]
//      == [(1 + 2) {3 + 4} 11 [7 + 8]]
//
// * It's able to transfer decorations onto composed items, e.g.:
//
//       >> word: 'foo, compose [(word): 1020]
//       == [foo: 1020]
//
// * It can interpolate strings:
//
//       >> x: 1000, compose "Hello (x + 20), World"
//       == "Hello 1020, World"
//

#include "sys-core.h"

#define L_binding         Level_Binding(L)


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
        if (Heart_Of(at) != HEART_GROUP)
            return false;
    }
    else if (Is_Fence(pattern)) {
        if (Heart_Of(at) != HEART_FENCE)
            return false;
    }
    else {
        assert(Is_Block(pattern));
        if (Heart_Of(at) != HEART_BLOCK)
            return false;
    }

    Copy_Cell(match, at);

    while (Series_Len_At(pattern) != 0) {
        if (Series_Len_At(pattern) != 1)
            panic ("COMPOSE patterns only nested length 1 or 0 right now");

        if (Series_Len_At(match) == 0)
            return false;  // no nested list or item to match

        const Element* match_1 = List_Item_At(match);
        const Element* pattern_1 = List_Item_At(pattern);

        if (Any_List(pattern_1)) {
            if (not Have_Same_Type(match_1, pattern_1))
                return false;
            pattern = pattern_1;
            Copy_Cell(match, match_1);
            continue;
        }
        if (not (Is_Tag(pattern_1) or Is_File(pattern_1)))
            panic ("COMPOSE non-list patterns just TAG! and FILE! atm");

        if (not Have_Same_Type(match_1, pattern_1))
            return false;

        if (CT_Utf8(match_1, pattern_1, 1) != 0)
            return false;

        SERIES_INDEX_UNBOUNDED(match) += 1;
        break;
    }

    Clear_Cell_Quotes_And_Quasi(match);  // want to get rid of quasi, too
    Tweak_Cell_Type_Matching_Heart(match, HEART_BLOCK);
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
    Value* out,
    Level* main_level,
    const Stable* list_or_seq,  // may be quasi or quoted (SPLICE! if toplevel)
    Context* context
){
    possibly(
        Is_Splice(list_or_seq)
        or Is_Cell_Quoted(list_or_seq)
        or Is_Quasiform(list_or_seq)
    );

    Heart heart = Heart_Of_Builtin(list_or_seq);

    DECLARE_ELEMENT (adjusted);
    assert(Is_Cell_Erased(adjusted));

    if (Any_Sequence_Heart(heart)) {  // allow sequences [1]
        TypeEnum lift = Type_Of_Raw(list_or_seq);

        assume (  // all sequences alias as block
          Alias_Any_Sequence_As(adjusted, As_Element(list_or_seq), HEART_BLOCK)
        );

        if (Type_Of_Raw(list_or_seq) > MAX_TYPE_NOQUOTE_NOQUASI)
            Tweak_Cell_Lift_Byte(adjusted, lift);  // restore
    }
    else if (Is_Splice(list_or_seq))
        Copy_Lifted_Cell(adjusted, list_or_seq);  // make it a quasiform
    else
        assert(Any_List_Heart(heart));

    require (
      Level* sub = Make_Level_At_Inherit_Const(
        &Composer_Executor,
        Is_Cell_Erased(adjusted) ? As_Element(list_or_seq) : adjusted,
        Derive_Binding(
            context,
            Is_Cell_Erased(adjusted) ? As_Element(list_or_seq) : adjusted
        ),
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE  // allows stack accumulation
    ));
    Push_Level(Erase_Cell(out), sub);  // sublevel may fail

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
// 2. See Pop_Sequence_Or_Element_Or_Null() for how reduced cases like
//    `(void).1` will turn into just INTEGER!, not `.1` -- this is in contrast
//    to `(space).1` which does turn into `.1`
//
// 3. There are N instances of the NEWLINE_BEFORE flags on the pushed items,
//    and we need N + 1 flags.  Borrow the tail flag from the input array.
//
// 4. It is legal to COMPOSE:DEEP into lists that are antiforms or quoted
//    (or potentially both).  So we transfer the TYPE_BYTE.
//
//        >> compose:deep [a ''~[(1 + 2)]~ b]
//        == [a ''~[3]~ b]
//
static Result(Stable*) Finalize_Composer_Level(
    Level* L,
    const Stable* composee,  // special handling if the output is a sequence
    bool conflate
){
    Stable* out = As_Stable(L->out);

    if (Is_Null(out)) {  // a composed slot evaluated to VETO error antiform
        Drop_Data_Stack_To(L->baseline.stack_base);
        return out;
    }

    assert(Is_Okay(out));  // finished normally

    possibly(
        Is_Splice(composee)
        or Is_Cell_Quoted(composee)
        or Is_Quasiform(composee)
    );
    Heart heart = Heart_Of_Builtin(composee);

    if (Any_Sequence_Heart(heart)) {
        trap (
          Pop_Sequence_Or_Element_Or_Null(
            out,
            Heart_Of_Builtin_Fundamental(As_Element(composee)),
            L->baseline.stack_base
        ));

        if (
            not Any_Sequence(out)  // so instead, things like [~/~ . ///]
            and not conflate  // don't rewrite as "sequence-looking" words
        ){
            return fail (Error_Conflated_Sequence_Raw(Datatype_Of(out), out));
        }

        assert(  // no anti/quasi forms
            (Type_Of_Raw(composee) <= MAX_TYPE_NOQUOTE_NOQUASI)
            or (Byte_From_Type(Type_Of_Raw(composee)) & NONQUASI_BIT)
        );
        Count num_quotes = Quotes_Of(As_Element(composee));

        if (not Is_Null(out))  // don't add quoting levels (?)
            Quotify_Depth(As_Element(out), num_quotes);
        return out;
    }

    Source* a = Pop_Source_From_Stack(L->baseline.stack_base);
    if (Get_Source_Flag(Cell_Array(composee), NEWLINE_AT_TAIL))
        Set_Source_Flag(a, NEWLINE_AT_TAIL);  // proxy newline flag [3]

    Element* out_list = Init_List(out, heart, a);

    if (Is_Splice(composee)) {
        Spread_Cell(out);  // no binding/sigil on output
    }
    else {  // preserve binding if not splice
        Tweak_Cell_Binding(out_list, Cell_Binding(As_Element(composee)));

        Option(Sigil) sigil = Cell_Underlying_Sigil(composee);
        if (sigil)
            Add_Cell_Sigil(out_list, unwrap sigil);
        Tweak_Cell_Lift_Byte(out_list, Type_Of_Raw(composee));  // add lift [4]
    }

    return out;
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
Bounce Composer_Executor(Level* const L)
{
    if (Is_Throwing(L))  // no state to cleanup (just data stack, auto-cleaned)
        return Native_Thrown_Result(L);

    Level* main_level = L->u.compose.main_level;  // invoked COMPOSE native

    bool deep;
    Element* pattern;
    bool conflate;
    Option(Element*) predicate;

  extract_arguments_from_original_compose_call: {

  // There's a Level for each "recursion" that processes the :DEEP blocks in a
  // COMPOSE.  (These don't recurse as C functions, the levels are stacklessly
  // processed by the trampoline, see %c-trampoline.c)
  //
  // But each level wants to access the arguments to the COMPOSE that kicked
  // off the process.  A pointer to the Level of the main compose is tucked
  // into each Composer_Executor() level to use.

    Level* level_ = main_level;  // level_ aliases L when outside this scope

    INCLUDE_PARAMS_OF_COMPOSE2;

    USED(ARG(TEMPLATE));  // accounted for by Level feed
    deep = ARG(DEEP);
    pattern = Element_ARG(PATTERN);
    conflate = ARG(CONFLATE);
    predicate = ARG(PREDICATE);

    assert(not predicate or Is_Frame(unwrap predicate));

} jump_to_label_for_state: {

    USE_LEVEL_SHORTHANDS (L);  // defines level_ as L now that args extracted

    enum {
        ST_COMPOSER_INITIAL_ENTRY = STATE_0,
        ST_COMPOSER_EVAL_GROUP,
        ST_COMPOSER_RUNNING_PREDICATE,
        ST_COMPOSER_RECURSING_DEEP
    };

    switch (STATE) {
      case ST_COMPOSER_INITIAL_ENTRY:
        goto handle_current_item;

      case ST_COMPOSER_EVAL_GROUP:
      case ST_COMPOSER_RUNNING_PREDICATE:
        goto process_slot_evaluation_result_in_out;

      case ST_COMPOSER_RECURSING_DEEP:
        goto composer_finished_recursion;

      default: assert(false);
    }

  handle_next_item: {  ///////////////////////////////////////////////////////

   Fetch_Next_In_Feed(L->feed);
   goto handle_current_item;

} handle_current_item: {  ////////////////////////////////////////////////////

    if (Is_Level_At_End(L)) {
        Init_Okay(OUT);
        goto finished_out_is_null_if_veto;
    }

    const Element* at = At_Level(L);

    Option(Heart) heart = Heart_Of(at);  // quoted groups match [1]

    if (not Any_Sequence_Or_List_Heart(heart)) {  // won't substitute/recurse
        Copy_Cell(PUSH(), at);  // keep newline flag
        goto handle_next_item;
    }

    if (not Try_Match_For_Compose(SPARE, at, pattern)) {
        if (deep or Any_Sequence_Heart(heart)) {  // sequences "same level"
            Push_Composer_Level(OUT, main_level, at, L_binding);
            STATE = ST_COMPOSER_RECURSING_DEEP;
            return CONTINUE_SUBLEVEL;
        }

        Copy_Cell(PUSH(), at);  // keep newline flag
        goto handle_next_item;
    }

    if (not predicate) {
        STATE = ST_COMPOSER_EVAL_GROUP;
        return CONTINUE(OUT, As_Element(SPARE));
    }

    STATE = ST_COMPOSER_RUNNING_PREDICATE;
    return CONTINUE(OUT, unwrap predicate, SPARE);

} process_slot_evaluation_result_in_out: {  //////////////////////////////////

    assert(
        STATE == ST_COMPOSER_EVAL_GROUP
        or STATE == ST_COMPOSER_RUNNING_PREDICATE
    );

    TypeEnum list_lift = Type_Of_Raw(At_Level(L));
    Option(Sigil) sigil = Cell_Underlying_Sigil(At_Level(L));

    if (Any_Void(OUT)) {
        if (not sigil and list_lift <= MAX_TYPE_NOQUOTE_NOQUASI) {
            L->u.compose.changed = true;
            goto handle_next_item;  // compose [(void)] => []
        }

        Init_Blank(PUSH());
        if (sigil)
            Add_Cell_Sigil(TOP_ELEMENT, unwrap sigil);  // ^ or @ or $
        Tweak_Cell_Lift_Byte(TOP_ELEMENT, list_lift);

        goto handle_next_item;
    }

    if (Is_Failure(OUT))
        return OUT;  // !!! Good idea?  Bad idea?

    if (Is_Cell_A_Veto_Hot_Potato(OUT)) {
        Init_Null_Signifying_Vetoed(OUT);
        goto finished_out_is_null_if_veto;  // compose [a (veto) b] => null
    }

    require (
      Stable* out = Decay_If_Unstable(OUT)
    );

    if (Is_Antiform(out)) {
        if (sigil)
            panic ("Cannot apply sigils to antiforms in COMPOSE'd slots");

        if (list_lift > MAX_TYPE_NOQUOTE_NOQUASI) {
            if (not (Byte_From_Type(list_lift) & NONQUASI_BIT))
                panic ("Can't COMPOSE antiforms into ~(...)~ slots");

            Copy_Lifted_Cell(PUSH(), OUT);
            Tweak_Cell_Lift_Byte(TOP, list_lift);
            goto handle_next_item;
        }

        if (Is_Splice(out))
            goto push_out_spliced;

        panic (Error_Bad_Antiform(out));
    }

  push_single_element_in_out: {

  // 1. When composing a single element, we use the newline intent from the
  //    GROUP! in the compose pattern...because there is no meaning to the
  //    newline flag of an evaluative product:
  //
  //        >> block: [foo
  //               bar]
  //
  //        >> compose [a (block.2) b]
  //        == [a bar b]
  //
  //        >> compose [a
  //               (block.2) b]
  //        == [a
  //               bar b]

    Copy_Cell(PUSH(), As_Element(out));

    if (sigil) {
        if (Sigil_Of(TOP_ELEMENT))
            panic ("COMPOSE cannot sigilize items already sigilized");

        Add_Cell_Sigil(TOP_ELEMENT, unwrap sigil);  // ^ or @ or $
    }

    if (
        (list_lift <= MAX_TYPE_NOQUOTE_NOQUASI)
        or (Byte_From_Type(list_lift) & NONQUASI_BIT)
    ){
        Quotify_Depth(
            TOP_ELEMENT,
            Quotes_From_Lift_Byte(list_lift)  // adds to existing
        );
    } else {
        if (Type_Of_Raw(TOP) > MAX_TYPE_NOQUOTE_NOQUASI)
            panic (
                "COMPOSE cannot quasify items not at quote level 0"
            );
        Tweak_Cell_Lift_Byte(TOP, list_lift);
    }

    if (Get_Cell_Flag(At_Level(L), NEWLINE_BEFORE))  // newline from group [1]
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);
    else
        Clear_Cell_Flag(TOP, NEWLINE_BEFORE);

    L->u.compose.changed = true;
    goto handle_next_item;

} push_out_spliced:  /////////////////////////////////////////////////////////

  // Splices are merged itemwise:
  //
  //    >> compose [(spread [a b]) merges]
  //    == [a b merges]
  //
  // 1. Although QUOTE typically doesn't work with antiforms, the power user
  //    feature of quoting composition slots lifts the decayed result:
  //
  //        >> compose [a: ~null~ b: '(spread [c 'd])]
  //        == [a: ~null~ b: ~[c 'd]~]
  //
  //    This is far more useful than something like distributing the quotes
  //    across the spliced items, which would be very odd:
  //
  //        >> compose [a: ~null~ b: '(spread [c 'd])]
  //        == [a: ~null~ b: 'c ''d]
  //
  // 2. Only proxy newline flag from template on *first* value spliced in,
  //    where it may have its own newline flag.  Not necessarily obvious,
  //    e.g. would you want the composed block below to all fit on one line?
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

    assert(Is_Splice(out));  // v-- quotes make quasiforms above [1]
    assert(list_lift <= MAX_TYPE_NOQUOTE_NOQUASI);
    assert(not sigil);  // should've errored on any non-VOID antiform

    const Element* push_tail;
    const Element* push = List_At(&push_tail, out);
    if (push != push_tail) {
        Copy_Cell(PUSH(), push);

        if (Get_Cell_Flag(At_Level(L), NEWLINE_BEFORE))
            Set_Cell_Flag(TOP, NEWLINE_BEFORE);  // proxy on first item [2]
        else
            Clear_Cell_Flag(TOP, NEWLINE_BEFORE);

        while (++push, push != push_tail)
            Copy_Cell(PUSH(), push);
    }

    L->u.compose.changed = true;
    goto handle_next_item;

} composer_finished_recursion: {  ////////////////////////////////////////////

  // 1. Compose stack of the nested compose is relative to *its* baseline.
  //
  // 2. To save on memory usage, Rebol historically does not make copies of
  //    arrays that don't have some substitution under them.  This may need
  //    to be controlled by a refinement.

    if (Is_Light_Null(OUT)) {  // VETO encountered
        Drop_Data_Stack_To(SUBLEVEL->baseline.stack_base);  // [1]
        Drop_Level(SUBLEVEL);
        return OUT;
    }

    assert(Is_Okay(As_Stable(OUT)));  // "return values" are on data stack

    if (not SUBLEVEL->u.compose.changed) {  // optimize on no substitutions [2]
        Drop_Data_Stack_To(SUBLEVEL->baseline.stack_base);  // [1]
        Drop_Level(SUBLEVEL);

        Copy_Cell(PUSH(), At_Level(L));
        // Constify(TOP);
        goto handle_next_item;
    }

    Stable* out = Finalize_Composer_Level(
        SUBLEVEL, At_Level(L), conflate
    ) except (Error* e) {
        Drop_Level(SUBLEVEL);  // have to drop level before panic !!! (why?)
        panic (e);
    }

    Drop_Level(SUBLEVEL);

    if (Is_Null(out)) {
        // compose:deep [a (void)/(void) b] => path makes null, vaporize it
    }
    else {
        assert(not Is_Antiform(out));
        Move_Cell(PUSH(), out);
    }

    if (Get_Cell_Flag(At_Level(L), NEWLINE_BEFORE))
        Set_Cell_Flag(TOP, NEWLINE_BEFORE);

    L->u.compose.changed = true;
    goto handle_next_item;

} finished_out_is_null_if_veto: {  ///////////////////////////////////////////

  // 1. At the end of the composer, we do not Drop_Data_Stack_To() and the
  //    level will still be alive for the caller.  This lets them have access
  //    to this level's BASELINE->stack_base, so it knows what all was pushed,
  //    and also means the caller can decide if they want the accrued items or
  //    not depending on the `changed` field in the level.

    assert(Get_Level_Flag(L, TRAMPOLINE_KEEPALIVE));  // caller needs [1]

    assert(Is_Logic(As_Stable(OUT)));  // null if veto

    return OUT;
}}}


//
//  /compose2: native [
//
//  "Evaluates only contents of GROUP!-delimited expressions in the argument"
//
//      return: [
//          any-list?
//          splice!
//          any-sequence?
//          any-utf8?
//          any-word?       "passed through as-is, or :CONFLATE can produce"
//          space?          ":CONFLATE can make, e.g. ().(_) => _"
//          quasar?         ":CONFLATE can make, e.g. ('~):() => ~"
//          ~word!~         ":CONFLATE can make, e.g. ('~)/('~) => ~/~"
//          <null>          "if ~(veto)~ encountered"
//      ]
//      pattern "Pass @ANY-LIST? (e.g. @{{}}) to use the pattern's binding"
//          [any-list? @any-list?]
//      template "The template to fill in (no-op if WORD!)"
//          [any-list? splice! any-sequence? any-word? any-utf8?]
//      :deep "Compose deeply into nested lists and sequences"
//      :conflate "Let illegal sequence compositions produce lookalike WORD!s"
//      :predicate "Function to run on slots (default is EVALUATE AS GROUP!)"
//          [frame!]
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
    Stable* input = ARG(TEMPLATE);  // template is C++ keyword

    enum {
        ST_COMPOSE2_INITIAL_ENTRY = STATE_0,
        ST_COMPOSE2_COMPOSING_LIST,
        ST_COMPOSE2_STRING_SCAN,
        ST_COMPOSE2_STRING_EVAL
    };

    switch (STATE) {
      case ST_COMPOSE2_INITIAL_ENTRY:
        goto initial_entry;

      case ST_COMPOSE2_COMPOSING_LIST:
        goto list_compose_finished_out_is_null_if_vetoed;

      case ST_COMPOSE2_STRING_SCAN:
        goto string_scan_results_on_stack;

      case ST_COMPOSE2_STRING_EVAL:
        goto string_eval_in_out;

      default: assert(false);
    }

  initial_entry: { ///////////////////////////////////////////////////////////

    if (Is_Pinned(pattern)) {  // @() means use pattern's binding
        Clear_Cell_Sigil(pattern);  // drop the @ from the pattern for processing
        if (Cell_Binding(pattern) == nullptr)
            panic ("@... patterns must have bindings");
    }
    else if (not Sigil_Of(pattern)) {
        Tweak_Cell_Binding(pattern, Level_Binding(level_));
    }
    else
        panic ("COMPOSE2 takes plain and @... list patterns only");

    assert(Any_List(pattern));

    if (Any_Word(input))
        return COPY_TO_OUT(input);  // makes it easier to `set compose target`

    if (Any_Utf8(input))
        goto string_initial_entry;

    assert(Any_List(input) or Is_Splice(input) or Any_Sequence(input));
    goto list_initial_entry;

} list_initial_entry: { //////////////////////////////////////////////////////

    Context* binding =
        Is_Splice(input) ? UNBOUND : List_Binding(As_Element(input));

    Push_Composer_Level(OUT, level_, input, binding);

    STATE = ST_COMPOSE2_COMPOSING_LIST;
    return CONTINUE_SUBLEVEL;

} list_compose_finished_out_is_null_if_vetoed: {  ////////////////////////////

    assert(Is_Logic(As_Stable(OUT)));

    require (
      Finalize_Composer_Level(SUBLEVEL, input, ARG(CONFLATE))
    );
    Drop_Level(SUBLEVEL);
    return OUT;

} string_initial_entry: {  ///////////////////////////////////////////////////

    Utf8(const*) head = Cell_Utf8_At(input);

    require (
      TranscodeState* transcode = Alloc_On_Heap(TranscodeState)
    );
    Init_Handle_Cdata(SCRATCH, transcode, 1);

    const LineNumber start_line = 1;
    Init_Transcode(
        transcode,
        NO_FILENAME,  // %tmp-boot.r name in boot overwritten by this
        start_line,
        cast(Byte*, head)
    );

    transcode->saved_levels = nullptr;  // level reuse optimization

    STATE = ST_COMPOSE2_STRING_SCAN;
    goto string_find_next_pattern;

} string_find_next_pattern: {  ///////////////////////////////////////////////

    StackIndex base = TOP_INDEX;  // base above the triples pushed so far

    Element* handle = As_Element(SCRATCH);
    TranscodeState* transcode = Cell_Handle_Pointer(TranscodeState, handle);

    Utf8(const*) head = Cell_Utf8_At(input);
    Utf8(const*) at = cast(Utf8(const*), transcode->at);

  push_pattern_terminators_to_data_stack: {

  // 1. If we're matching @(([])) and we see "((some(([thing]))", then when we
  //    see the "s" that means we didn't see "(([".  So the scan has to start
  //    looking for the first paren again.
  //
  // 2. When we call into the scanner for a pattern like "({[foo]})" we start
  //    it scanning at "foo]})".  The reason we can get away with it is that
  //    we've pushed levels manually that account for if the scanner had seen
  //    "({[", so it expects to have consumed those tokens and knows what end
  //    delimiters it's looking for.

    Codepoint c;
    Utf8(const*) next = Utf8_Next(&c, at);

    Copy_Cell(PUSH(), pattern); // top of stack is pattern currently matching

    Byte begin_delimiter = Begin_Delimit_For_List(
        Heart_Of_Builtin_Fundamental(TOP_ELEMENT)
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
            if (Series_Len_At(TOP) == 0)  // no more nests in pattern
                break;

            end_delimiter = End_Delimit_For_List(
                Heart_Of_Builtin_Fundamental(TOP_ELEMENT)
            );

            const Element* pattern_at = List_Item_At(TOP_ELEMENT);
            Copy_Cell(PUSH(), pattern_at);  // step into pattern

            if (not Any_List(TOP_ELEMENT))
                panic ("COMPOSE2 pattern must be composed of lists");
            if (Series_Len_At(TOP_ELEMENT) > 1)
                panic ("COMPOSE2 pattern layers must be length 1 or 0");

            begin_delimiter = Begin_Delimit_For_List(
                Heart_Of_Builtin_Fundamental(TOP_ELEMENT)
            );
        }
        else if (end_delimiter and c == end_delimiter) {
            DROP();
            begin_delimiter = Begin_Delimit_For_List(
                Heart_Of_Builtin_Fundamental(TOP_ELEMENT)
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
                Heart_Of_Builtin_Fundamental(TOP_ELEMENT)
            );
            end_delimiter = 0;
            continue;  // don't advance scan, try to match pattern start
        }

        next = Utf8_Next(&c, at);
    }

    transcode->at = at;  // scanner needs at, e.g. "a])", not "([a])", see [2]

    Count pattern_depth = TOP_INDEX - base;  // number of pattern levels pushed
    Utf8(const*) start = cast(Utf8(*),
        u_cast(Byte*, at) - pattern_depth  // start replacement at "([a])"
    );

  allocate_or_push_levels_for_each_pattern_end_delimiter: {

  // We don't want to allocate or push a scanner level until we are sure it's
  // necessary.  (If no patterns are found, all we need to do is COPY the
  // string if there aren't any substitutions.)

    if (not transcode->saved_levels) {  // first match... no Levels yet
        StackIndex stack_index = base;
        for (; stack_index != TOP_INDEX; ++stack_index) {
            Element* pattern_at = Data_Stack_At(Element, stack_index + 1);
            Byte terminal = End_Delimit_For_List(
                Heart_Of_Builtin_Fundamental(pattern_at)
            );

            Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
                | FLAG_STATE_BYTE(Scanner_State_For_Terminal(terminal));

            if (stack_index != TOP_INDEX - 1)
                flags |= SCAN_EXECUTOR_FLAG_SAVE_LEVEL_DONT_POP_ARRAY;

            Level* sub = Make_Scan_Level(transcode, g_end_feed, flags);
            sub->baseline.stack_base = base;  // we will drop to this

            unnecessary(Erase_Cell(OUT));  // LEVEL_STATE_BYTE is not STATE_0
            Push_Level(OUT, sub);

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
            unnecessary(Erase_Cell(OUT));  // LEVEL_STATE_BYTE is not STATE_0
            sub->prior = nullptr;  // expected by Push_Level()
            Push_Level(OUT, sub);
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
    possibly(TOP_LEVEL->prior != level_);  // deeper if {{nested}} delimiters
    return BOUNCE_CONTINUE;

}}} string_scan_results_on_stack: { //////////////////////////////////////////

  // 1. While transcoding in a general case can't assume the data is valid
  //    UTF-8, we're scanning an already validated ANY-UTF8? value here.
  //
  // 2. Each pattern found will push 3 values to the data stack:
  //
  //    * the start offset where the pattern first begins
  //    * the code that was scanned from inside the pattern
  //    * the offset right after the end character where the pattern matched

    if (Is_Failure(OUT))  // transcode had a problem
        panic (OUT);

    Element* handle = As_Element(SCRATCH);
    TranscodeState* transcode = Cell_Handle_Pointer(TranscodeState, handle);
    Element* elem_start_offset = As_Element(SPARE);
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

    if (not Codepoint_At_Is_NUL_0(at))
        goto string_find_next_pattern;

} string_scan_finished: {

  // 1. !!! If we never found our pattern, should we validate the pattern was
  //    legal?  Or we could just say that if you use an illegal pattern but
  //    no instances come up, that's ok?

    Element* handle = As_Element(SCRATCH);
    TranscodeState* transcode = Cell_Handle_Pointer(TranscodeState, handle);

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

  // We do all the scans first, and then the evaluations.  This means that no
  // user code is run if the string being interpolated is malformed, which is
  // preferable.  It also helps with locality.  But it means the evaluations
  // have to be done on an already built stack.

    StackIndex triples = VAL_INT32(As_Element(SCRATCH));

    assert(Is_Integer(Data_Stack_At(Element, triples)));  // start offset
    OnStack(Element*) code = Data_Stack_At(Element, triples + 1);
    assert(Is_Block(code));  // code to evaluate
    assert(Is_Integer(Data_Stack_At(Element, triples + 2)));  // end offset

    Tweak_Cell_Binding(code, Cell_Binding(pattern));  // bind unbound code

    STATE = ST_COMPOSE2_STRING_EVAL;
    return CONTINUE(
        OUT,
        Copy_Cell(SPARE, code)  // pass non-stack code
    );

} string_eval_in_out: { //////////////////////////////////////////////////////

    if (Is_Cell_A_Veto_Hot_Potato(OUT)) {
        Drop_Data_Stack_To(STACK_BASE);
        return NULL_OUT_VETOING;
    }

    if (Is_Failure(OUT)) {
        Drop_Data_Stack_To(STACK_BASE);
        panic (OUT);
    }

    const Stable* result;
    if (Any_Void(OUT))
        result = Stable_LIB(NONE);  // canonize as empty splice
    else {
        require (
          result = Decay_If_Unstable(OUT)
        );
    }

    StackIndex triples = VAL_INT32(As_Element(SCRATCH));
    assert(Is_Block(Data_Stack_At(Element, triples + 1)));  // evaluated code
    Copy_Cell(Data_Stack_At(Stable, triples + 1), result);  // replace w/eval

    triples += 3;  // skip to next set of 3
    if (triples <= TOP_INDEX) {
        Init_Integer(SCRATCH, triples);
        goto do_string_eval_scratch_is_stackindex;
    }

} string_evaluations_done: {

  // 1. "File calculus" says that if we are splicing a FILE! into a FILE!,
  //    then if the splice ends in slash the template must have a slash after
  //    the splicing slot.  MORE RULES TO BE ADDED...

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    StackIndex triples = STACK_BASE + 1;  // [start_offset, code, end_offset]

    Offset at_offset = 0;

    Size size;
    Utf8(const*) head = Cell_Utf8_Size_At(&size, input);

    for (; triples < TOP_INDEX; triples += 3) {
        Offset start_offset = VAL_INT32(Data_Stack_At(Element, triples));
        Stable* eval = Data_Stack_At(Stable, triples + 1);
        Offset end_offset = VAL_INT32(Data_Stack_At(Element, triples + 2));

        Append_UTF8_May_Panic(
            mo->strand,
            cast(const char*, head) + at_offset,
            start_offset - at_offset,
            STRMODE_NO_CR
        );

        at_offset = end_offset;

        if (Is_None(eval))  // VOID translated to empty splice for data stack
            continue;

        if (Type_Of_Raw(eval) > MAX_TYPE_NOQUOTE_NOQUASI)
            panic ("For the moment, COMPOSE string doesn't handle lift forms");

        if (Is_File(eval) and Is_File(input)) {  // "File calculus" [1]
            const Byte* at = cast(Byte*, head) + at_offset;
            bool eval_slash_tail = (
                Series_Len_At(eval) != 0
                and Codepoint_Back_Is_Ascii_Value(Cell_Strand_Tail(eval), '/')
            );
            bool slash_after_splice = (at[0] == '/');

            if (eval_slash_tail) {
                if (not slash_after_splice)
                    panic (
                        "FILE! spliced into FILE! must end in slash"
                        " if splice slot is followed by slash"
                    );
                ++at_offset;  // skip the slash (use the one we're forming)
            }
            else {
                if (slash_after_splice)
                    panic (
                        "FILE! spliced into FILE! can't end in slash"
                        " unless splice slot followed by slash"
                    );
            }
        }

        Form_Element(mo, cast(Element*, eval));
    }
    Append_UTF8_May_Panic(
        mo->strand,
        cast(const char*, head) + at_offset,
        size - at_offset,
        STRMODE_NO_CR
    );

    Drop_Data_Stack_To(STACK_BASE);

    Strand* str = Pop_Molded_Strand(mo);
    if (not Any_String(input))
        Freeze_Flex(str);

    Heart input_heart = Heart_Of_Builtin_Fundamental(As_Element(input));
    return Init_Series_At_Core(
        OUT, FLAG_HEART_AND_LIFT(input_heart), str, 0, nullptr
    );
}}
