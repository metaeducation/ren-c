//
//  file: %n-strings.c
//  summary: "native functions for strings"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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


static bool Check_Char_Range(const Stable* val, Codepoint limit)
{
    if (Is_Rune_And_Is_Char(val))
        return Rune_Known_Single_Codepoint(val) <= limit;

    if (Is_Integer(val))
        return VAL_INT64(val) <= cast(REBI64, limit);

    assert(Any_String(val));

    REBLEN len;
    Utf8(const*) up = Cell_Utf8_Len_Size_At(&len, nullptr, val);

    for (; len > 0; len--) {
        Codepoint c;
        up = Utf8_Next(&c, up);

        if (c > limit)
            return false;
    }

    return true;
}


//
//  ascii?: native [
//
//  "Returns TRUE if value or string is in ASCII character range (below 128)"
//
//      return: [logic?]
//      value [any-string? char? integer!]
//  ]
//
DECLARE_NATIVE(ASCII_Q)
{
    INCLUDE_PARAMS_OF_ASCII_Q;

    return LOGIC(Check_Char_Range(ARG(VALUE), 0x7f));
}


//
//  latin1?: native [
//
//  "Returns TRUE if value or string is in Latin-1 character range (below 256)"
//
//      return: [logic?]
//      value [any-string? char? integer!]
//  ]
//
DECLARE_NATIVE(LATIN1_Q)
{
    INCLUDE_PARAMS_OF_LATIN1_Q;

    return LOGIC(Check_Char_Range(ARG(VALUE), 0xff));
}


#define LEVEL_FLAG_DELIMIT_MOLD_RESULT  LEVEL_FLAG_MISCELLANEOUS

#define CELL_FLAG_DELIMITER_NOTE_PENDING    CELL_FLAG_NOTE

#define CELL_FLAG_STACK_NOTE_MOLD           CELL_FLAG_NOTE


#define Push_Join_Delimiter_If_Pending() do { \
    if (delimiter and Get_Cell_Flag(LOCAL(WITH), DELIMITER_NOTE_PENDING)) { \
        Copy_Cell(PUSH(), LOCAL(WITH)); \
        Clear_Cell_Flag(LOCAL(WITH), DELIMITER_NOTE_PENDING); \
    } \
  } while (0)

#define Mark_Join_Delimiter_Pending()  \
    Set_Cell_Flag(LOCAL(WITH), DELIMITER_NOTE_PENDING) \


//
//  join: native [
//
//  "Join elements to produce a new value"
//
//      return: [<null> any-utf8? any-list? any-sequence? blob!]
//      base "If no base element and no material in rest to join, gives NULL"
//          [<opt-out> datatype! any-utf8? any-list? any-sequence? blob!]
//      rest "Plain [...] blocks reduced, @[...] block items used as is"
//          [<opt> block! @block! any-utf8? blob! integer!]
//      :with [element? splice!]
//      :head "Include delimiter at head of a non-NULL result"  ; [1]
//      :tail "Include delimiter at tail of a non-NULL result"
//      {original-index}
//  ]
//
DECLARE_NATIVE(JOIN)
//
// 1. If you write (join:with:head text! [] "::") you currently get NULL back
//    but (join:with:head group! [] '::) gives you (::).  The policy needs
//    to be articulated as to what the best behavior is.
{
    INCLUDE_PARAMS_OF_JOIN;

    Option(const Element*) base;  // const (we derive result_heart each time)
    Heart heart;
    if (Is_Datatype(ARG(BASE))) {
        base = nullptr;
        Option(Heart) datatype_heart = Datatype_Heart(ARG(BASE));
        if (not datatype_heart)
            panic (PARAM(BASE));
        heart = unwrap datatype_heart;
    }
    else {
        base = Element_ARG(BASE);
        heart = Heart_Of_Builtin_Fundamental(unwrap base);
    }
    bool joining_datatype = not base;  // compiler should optimize out

    Option(const Element*) rest = Is_Nulled(Stable_LOCAL(REST))
        ? nullptr
        : Element_ARG(REST);

    Element* original_index;

    Option(Element*) delimiter = Is_Nulled(Stable_LOCAL(WITH))
        ? nullptr
        : Element_ARG(WITH);
    if (delimiter)
        possibly(Get_Cell_Flag(unwrap delimiter, DELIMITER_NOTE_PENDING));

    enum {
        ST_JOIN_INITIAL_ENTRY = STATE_0,
        ST_JOIN_STACK_STEPPING,
        ST_JOIN_MOLD_STEPPING,
        ST_JOIN_EVALUATING_THE_GROUP
    };

    if (STATE != ST_JOIN_INITIAL_ENTRY)
        goto not_initial_entry;

  initial_entry: {

    STATIC_ASSERT(
        CELL_FLAG_DELIMITER_NOTE_PENDING
        == CELL_FLAG_PARAM_NOTE_TYPECHECKED
    );
    assert(Get_Cell_Flag(LOCAL(WITH), PARAM_NOTE_TYPECHECKED));
    Clear_Cell_Flag(LOCAL(WITH), PARAM_NOTE_TYPECHECKED);

    if (Any_List(ARG(BASE)) or Any_Sequence(ARG(BASE))) {
        if (
            rest and (
                not Is_Block(unwrap rest)
                and not Is_Pinned_Form_Of(BLOCK,unwrap rest)
            )
        ){
            panic ("JOIN of list or sequence must join with BLOCK!");
        }
    }

    if (not rest) {  // simple base case: nullptr or COPY
        if (joining_datatype)
            return NULLED;
        return rebValue(CANON(COPY), unwrap base);
    }
    if (joining_datatype and Any_Utf8(unwrap rest))
        goto simple_join;

    goto start_complex_join;

} simple_join: { /////////////////////////////////////////////////////////////

  // 1. Hard to unify this mold with code below that uses a level due to
  //    asserts on states balancing.  Easiest to repeat a small bit of code!

    assert(Any_Utf8(unwrap rest));  // shortcut, no evals needed [1]

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    if (ARG(HEAD) and delimiter)
        Form_Element(mo, unwrap delimiter);

    Form_Element(mo, unwrap rest);

    if (ARG(TAIL) and delimiter)
        Form_Element(mo, unwrap delimiter);

    return Init_Text(OUT, Pop_Molded_Strand(mo));

} not_initial_entry: { ///////////////////////////////////////////////////////

    original_index = Element_LOCAL(ORIGINAL_INDEX);

    switch (STATE) {
      case ST_JOIN_MOLD_STEPPING:
        assert(Not_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT));
        goto mold_step_result_in_spare;

      case ST_JOIN_STACK_STEPPING:
        goto stack_step_result_in_spare;

      case ST_JOIN_EVALUATING_THE_GROUP:
        if (Is_Pinned_Form_Of(BLOCK, unwrap rest))
            SUBLEVEL->executor = &Inert_Stepper_Executor;
        else {
            assert(Is_Block(unwrap rest));
            SUBLEVEL->executor = &Stepper_Executor;
        }
        assert(Get_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT));
        goto mold_step_result_in_spare;

      default: assert(false);
    }

} start_complex_join: { //////////////////////////////////////////////////////

  // 1. It's difficult to handle the edge cases like `join:with:head` when you
  //    are doing (join 'a 'b) and get it right.  So we make a feed without
  //    having to make a fake @[...] array (though we could do that as well).
  //    It's a very minor optimization and may not be worth it, but it points
  //    to better optimizations (maybe one that wouldn't require a Level).

    Level* sub;

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;
    if (Is_Block(unwrap rest)) {
        require (
          sub = Make_Level_At(&Stepper_Executor, unwrap rest, flags)
        );
    }
    else if (Is_Pinned_Form_Of(BLOCK, unwrap rest)) {
        require (
          sub = Make_Level_At(&Inert_Stepper_Executor, unwrap rest, flags)
        );
    }
    else {
        Result(Feed*) feed = Prep_Array_Feed(  // leverage feed mechanics [1]
            Alloc_Feed(),
            unwrap rest,  // first--in this case, the only value in the feed...
            g_empty_array,  // ...because we're using the empty array after that
            0,  // ...at index 0
            SPECIFIED,  // !!! context shouldn't matter
            FEED_MASK_DEFAULT | ((unwrap rest)->header.bits & FEED_FLAG_CONST)
        );

        require (
          sub = Make_Level(&Inert_Stepper_Executor, feed, flags)
        );
    }

    Push_Level_Erase_Out_If_State_0(SPARE, sub);

    if (delimiter)
        assert(Not_Cell_Flag(unwrap delimiter, DELIMITER_NOTE_PENDING));

    if (Any_Utf8_Type(heart) or heart == TYPE_BLOB)
        goto start_mold_join;

    assert(Any_List_Type(heart) or Any_Sequence_Type(heart));

    goto start_stack_join;

  start_mold_join: { /////////////////////////////////////////////////////////

    if (not joining_datatype)
        Copy_Cell(PUSH(), unwrap base);

    if (ARG(HEAD) and delimiter)  // speculatively start with
        Copy_Cell(PUSH(), unwrap delimiter);  // may be tossed

    original_index = Init_Integer(LOCAL(ORIGINAL_INDEX), TOP_INDEX);

    if (Is_Level_At_End(sub))
        goto finish_mold_join;

    goto first_mold_step;

} start_stack_join: { ////////////////////////////////////////////////////////

  // 1. (join 'a: [...]) should work, and (join 'a: []) should give `a:`.
  //    To do that we use the flag of whether the join produced anything (e.g.
  //    the output is non-null) and if it didn't, we will add a space back.

    if (not joining_datatype) {
        if (Any_Sequence_Type(heart)) {
            Length len = Sequence_Len(unwrap base);
            REBINT i;
            for (i = 0; i < len; ++i)
                Copy_Sequence_At(PUSH(), unwrap base, i);
            if (Is_Space(TOP_STABLE))
                DROP();  // will add back if join produces nothing [1]
        }
        else {
            const Element* tail;
            const Element* at = List_At(&tail, unwrap base);

            for (; at != tail; ++at)
                Copy_Cell(PUSH(), at);
        }
    }

    if (ARG(HEAD) and delimiter)  // speculatively start with
        Copy_Cell(PUSH(), unwrap delimiter);  // may be tossed

    original_index = Init_Integer(LOCAL(ORIGINAL_INDEX), TOP_INDEX);

    SUBLEVEL->baseline.stack_base = TOP_INDEX;

    if (Is_Level_At_End(sub))
        goto finish_stack_join;

    STATE = ST_JOIN_STACK_STEPPING;
    return CONTINUE_SUBLEVEL(sub);  // no special source rules

}} next_mold_step: { /////////////////////////////////////////////////////////

    Reset_Evaluator_Erase_Out(SUBLEVEL);
    Clear_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT);

} first_mold_step: { /////////////////////////////////////////////////////////

  // 1. There's a concept that being able to put undelimited portions in the
  //    delimit is useful:
  //
  //       >> print ["Outer" "spaced" ["inner" "unspaced"] "seems" "useful"]
  //       Outer spaced innerunspaced seems useful
  //
  //    BUT it may only look like a good idea because it came around before
  //    we could do real string interpolation.  Hacked in for the moment,
  //    review the idea's relevance...

    Level* sub = SUBLEVEL;

    if (Is_Level_At_End(sub))
        goto finish_mold_join;

    const Element* item = At_Level(sub);
    if (Is_Block(item) and delimiter) {  // hack [1]
        Copy_Cell_May_Bind(SPARE, item, Level_Binding(sub));
        Fetch_Next_In_Feed(sub->feed);

        Api(Stable*) unspaced = rebStable(CANON(UNSPACED), rebQ(SPARE));
        if (unspaced == nullptr)  // vaporized, allow it
            goto next_mold_step;

        Push_Join_Delimiter_If_Pending();
        Copy_Cell(PUSH(), unspaced);
        rebRelease(unspaced);
        Mark_Join_Delimiter_Pending();
        goto next_mold_step;
    }

    if (Is_Pinned(item) and not Is_Pinned_Space(item)) {  // fetch and mold
        Set_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT);

        Option(Heart) item_heart = Heart_Of(item);
        if (item_heart == TYPE_WORD or item_heart == TYPE_TUPLE) {
            Element* subscratch = Copy_Cell(Level_Scratch(sub), item);
            Clear_Cell_Sigil(subscratch);
            Bind_Cell_If_Unbound(subscratch, Level_Binding(sub));
            heeded (Corrupt_Cell_If_Needful(Level_Spare(sub)));
            assert(sub->out == SPARE);
            assert(LEVEL_STATE_BYTE(sub) == 0);
            LEVEL_STATE_BYTE(sub) = 1;
            require (
              Get_Var_In_Scratch_To_Out(sub, NO_STEPS)
            );
            LEVEL_STATE_BYTE(sub) = STATE_0;
            Fetch_Next_In_Feed(sub->feed);
            goto mold_step_result_in_spare;
        }

        if (item_heart == TYPE_GROUP) {
            SUBLEVEL->executor = &Just_Use_Out_Executor;
            Copy_Cell_May_Bind(SCRATCH, item, Level_Binding(sub));
            KIND_BYTE(SCRATCH) = TYPE_BLOCK;  // the-block is different
            Fetch_Next_In_Feed(sub->feed);

            SUBLEVEL->baseline.stack_base = TOP_INDEX;
            STATE = ST_JOIN_EVALUATING_THE_GROUP;
            return CONTINUE(SPARE, cast(Element*, SCRATCH));
        }

        panic (item);
    }

    if (Is_Quoted(item)) {  // just mold it
        Push_Join_Delimiter_If_Pending();

        Copy_Cell(PUSH(), item);
        Unquote_Cell(TOP_ELEMENT);
        Set_Cell_Flag(TOP, STACK_NOTE_MOLD);

        Mark_Join_Delimiter_Pending();

        Fetch_Next_In_Feed(sub->feed);
        goto next_mold_step;
    }

    SUBLEVEL->baseline.stack_base = TOP_INDEX;
    STATE = ST_JOIN_MOLD_STEPPING;
    return CONTINUE_SUBLEVEL(sub);  // just evaluate it

} mold_step_result_in_spare: { ///////////////////////////////////////////////

  // 1. spaced [null ...]
  //
  // 2. RUNE! suppresses the delimiter logic.  Hence:
  //
  //        >> delimit ":" ["a" _ "b" # () "c" newline "d" "e"]
  //        == "a b^/c^/d:e"
  //
  //    Only the last interstitial is considered a candidate for delimiting.
  //
  // 3. Empty strings distinct from voids in terms of still being delimited.
  //    This is important, e.g. in comma-delimited formats for empty fields.
  //
  //        >> delimit "," [field1 field2 field3]  ; field2 is ""
  //        one,,three
  //
  //    The same principle would apply to a "space-delimited format".

    if (Any_Void(SPARE))  // spaced [elide print "hi"], etc
        goto next_mold_step;  // vaporize

    if (Is_Error(SPARE) and Is_Error_Veto_Signal(Cell_Error(SPARE)))
        goto vetoed;

    require (
      Stable* spare = Decay_If_Unstable(SPARE)  // may error [1]
    );

    if (Is_Splice(spare)) {  // only allow splice for mold, for now
        const Element* tail;
        const Element* at = List_At(&tail, spare);
        if (at == tail)
            goto next_mold_step;  // vaporize

        if (Not_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT)) {
            for (; at != tail; ++at) {
                Push_Join_Delimiter_If_Pending();
                Copy_Cell(PUSH(), at);
                Mark_Join_Delimiter_Pending();
            }
            goto next_mold_step;
        }
    }
    else if (Is_Antiform(spare))
        return fail (Error_Bad_Antiform(spare));

    if (Is_Rune(spare)) {  // do not delimit (unified w/char) [2]
        if (delimiter)
            Clear_Cell_Flag(unwrap delimiter, DELIMITER_NOTE_PENDING);
        Copy_Cell(PUSH(), spare);
        goto next_mold_step;
    }

    possibly(Is_Text(spare) and String_Len_At(spare) == 0);  // delimits [3]

    Push_Join_Delimiter_If_Pending();
    Copy_Cell(PUSH(), spare);
    if (Get_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT))
        Set_Cell_Flag(TOP, STACK_NOTE_MOLD);
    Mark_Join_Delimiter_Pending();

    goto next_mold_step;

} next_stack_step: { /////////////////////////////////////////////////////////

    Level* sub = SUBLEVEL;

    if (Is_Level_At_End(sub))
        goto finish_stack_join;

    Reset_Evaluator_Erase_Out(sub);
    /* Clear_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT); */

    return CONTINUE_SUBLEVEL(sub);

} stack_step_result_in_spare: { //////////////////////////////////////////////

    if (Any_Void(SPARE))
        goto next_stack_step;  // vaporize

    if (Is_Error(SPARE) and Is_Error_Veto_Signal(Cell_Error(SPARE)))
        goto vetoed;

    require (
      Stable* spare = Decay_If_Unstable(SPARE)
    );

    if (Is_Splice(spare)) {
        const Element* tail;
        const Element* at = List_At(&tail, spare);

        if (at == tail)
            goto next_stack_step;  // don't mark produced something

        for (; at != tail; ++at) {
            Push_Join_Delimiter_If_Pending();
            Copy_Cell(PUSH(), at);
            Mark_Join_Delimiter_Pending();
        }

        goto next_stack_step;
    }
    else if (Is_Antiform(spare))
        return fail (Error_Bad_Antiform(spare));

    Push_Join_Delimiter_If_Pending();
    Copy_Cell(PUSH(), spare);
    Mark_Join_Delimiter_Pending();

    goto next_stack_step;

} finish_mold_join: { /////////////////////////////////////////////////////////

  // Either targeting a BLOB! or a UTF-8! type

    Drop_Level_Unbalanced(SUBLEVEL);

    if (TOP_INDEX == VAL_INT32(original_index)) {  // nothing pushed
        Drop_Data_Stack_To(STACK_BASE);
        if (joining_datatype)
            return NULLED;
        return rebValue(CANON(COPY), rebQ(unwrap base));
    }

    if (ARG(TAIL) and delimiter)
        Copy_Cell(PUSH(), unwrap delimiter);

    if (heart == TYPE_BLOB)
        goto finish_blob_join;

    goto finish_utf8_join;

  finish_utf8_join: { ////////////////////////////////////////////////////////

  // 1. BLOCK!s are prohibitied in DELIMIT because it's too often the case the
  //    result is gibberish--guessing what to do is bad:
  //
  //        >> block: [1 2 <x> hello]
  //
  //        >> print ["Your block is:" block]
  //        Your block is: 12<x>hello  ; ugh.

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

  iterate_utf8_stack: {

    StackIndex at = STACK_BASE + 1;
    StackIndex tail = TOP_INDEX + 1;

    for (; at != tail; ++at) {
        bool mold = Get_Cell_Flag(Data_Stack_At(Stable, at), STACK_NOTE_MOLD);
        Stable* v = Copy_Cell(SPARE, Data_Stack_At(Stable, at));

        if (mold) {
            assert(NOT_MOLD_FLAG(mo, MOLD_FLAG_SPREAD));
            if (Is_Splice(v))
                SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            Mold_Or_Form_Cell_Ignore_Quotes(mo, v, false);
            CLEAR_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            continue;
        }

        assert(not Is_Antiform(v));  // non-molded splices push items

        if (Any_List(v))  // guessing a behavior is bad [1]
            panic ("JOIN requires @var to mold lists");

        if (Any_Sequence(v))  // can have lists in them, dicey [1]
            panic ("JOIN requires @var to mold sequences");

        if (Any_Lifted(cast(Element*, v)) or Sigil_Of(cast(Element*, v)))
            panic ("JOIN requires @var for elements with sigils");

        Form_Element(mo, cast(Element*, v));
    }

} drop_utf8_stack_and_return: {

    Drop_Data_Stack_To(STACK_BASE);  // can't be while OnStack() is in scope

    Utf8(const*) utf8 = cast(
        Utf8(const*), Binary_At(mo->strand, mo->base.size)
    );
    Size size = Strand_Size(mo->strand) - mo->base.size;
    Length len = Strand_Len(mo->strand) - mo->base.index;

    if (heart == TYPE_WORD) {
        require (
          const Symbol* s = Intern_Utf8_Managed(utf8, size)
        );
        Init_Word(OUT, s);
    }
    else if (Any_String_Type(heart)) {
        Init_Any_String(OUT, heart, Pop_Molded_Strand(mo));
    }
    else if (heart == TYPE_RUNE) {
        Init_Utf8_Non_String(OUT, heart, utf8, size, len);
    }
    else if (heart == TYPE_EMAIL) {
        trap (
          const Byte* ep = Scan_Email_To_Stack(utf8, size)
        );
        if (ep != cast(const Byte*, utf8) + size)
            return fail ("Invalid EMAIL!");
        Move_Cell(OUT, TOP_ELEMENT);
        DROP();
    }
    else if (heart == TYPE_URL) {
        if (
            cast(const Byte*, utf8) + size
            != Try_Scan_URL_To_Stack(utf8, size)
        ){
            return fail ("Invalid URL!");
        }
        Move_Cell(OUT, TOP_ELEMENT);
        DROP();
    }
    else
        panic (PARAM(BASE));

    if (mo->strand)
        Drop_Mold(mo);

    return OUT;

}} finish_blob_join: { ///////////////////////////////////////////////////////

    Binary* buf = BYTE_BUF;
    Count used = 0;

    Set_Flex_Len(buf, 0);

  iterate_stack: {

    OnStack(Stable) at = Data_Stack_At(Stable, STACK_BASE + 1);
    OnStack(Stable) tail = Data_Stack_At(Stable, TOP_INDEX + 1);

    for (; at != tail; ++at) {
        if (Get_Cell_Flag(at, STACK_NOTE_MOLD)) {
            DECLARE_MOLDER (mo);
            Push_Mold(mo);
            if (Is_Splice(at))
                SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            Mold_Or_Form_Cell_Ignore_Quotes(mo, at, false);

            Utf8(const*) utf8 = cast(
                Utf8(const*), Binary_At(mo->strand, mo->base.size)
            );
            Size size = Strand_Size(mo->strand) - mo->base.size;

            require (
              Expand_Flex_Tail_And_Update_Used(buf, size)
            );
            memcpy(Binary_At(buf, used), cast(Byte*, utf8), size);

            Drop_Mold(mo);
        }
        else switch (opt Type_Of(at)) {
          case TYPE_INTEGER: {
            require (
              Expand_Flex_Tail_And_Update_Used(buf, 1)
            );
            *Binary_At(buf, used) = cast(Byte, VAL_UINT8(at));  // can panic()
            break; }

          case TYPE_BLOB: {
            Size size;
            const Byte* data = Blob_Size_At(&size, at);
            require (
              Expand_Flex_Tail_And_Update_Used(buf, size)
            );
            memcpy(Binary_At(buf, used), data, size);
            break; }

          case TYPE_RUNE:
          case TYPE_TEXT:
          case TYPE_FILE:
          case TYPE_EMAIL:
          case TYPE_URL:
          case TYPE_TAG: {
            Size utf8_size;
            Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, at);

            require (
              Expand_Flex_Tail_And_Update_Used(buf, utf8_size)
            );
            memcpy(Binary_At(buf, used), cast(Byte*, utf8), utf8_size);
            /*Set_Flex_Len(buf, used + utf8_size); */
            break; }

          default:
            panic (Error_Bad_Value(at));
        }

        used = Flex_Used(buf);
    }

} drop_stack_and_return_blob: {

    Drop_Data_Stack_To(STACK_BASE);  // can't be while OnStack() is in scope

    Binary* bin = Make_Binary(used);
    Term_Binary_Len(bin, used);
    Mem_Copy(Binary_Head(bin), Binary_Head(buf), used);

    Set_Flex_Len(buf, 0);

    return Init_Blob(OUT, bin);

}}} finish_stack_join: { /////////////////////////////////////////////////////

    Drop_Level_Unbalanced(SUBLEVEL);

    if (ARG(TAIL) and delimiter)
        Copy_Cell(PUSH(), unwrap delimiter);

    Sink(Element) out = OUT;
    if (Any_Sequence_Type(heart)) {
        trap (
          Pop_Sequence(out, heart, STACK_BASE)
        );
    }
    else {
        Source* a = Pop_Managed_Source_From_Stack(STACK_BASE);
        Init_Any_List(out, heart, a);
    }

    if (not joining_datatype)
        Tweak_Cell_Binding(out, Cell_Binding(unwrap base));

    return OUT;

} vetoed: { ////////////////////////////////////////////////////////////

    Drop_Data_Stack_To(STACK_BASE);
    Drop_Level(SUBLEVEL);

    return VETOING_NULL;
}}


//
//  debase: native [
//
//  "Decodes base-coded string (BASE-64 default) to binary value"
//
//      return: [blob!]
//      value [blob! text!]
//      :base "The base to convert from: 64, 16, or 2 (defaults to 64)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(DEBASE)
{
    INCLUDE_PARAMS_OF_DEBASE;

    Size size;
    const Byte* bp = Cell_Bytes_At(&size, ARG(VALUE));

    REBINT base = 64;
    if (ARG(BASE))
        base = VAL_INT32(unwrap ARG(BASE));
    else
        base = 64;

    Binary* decoded = opt Decode_Enbased_Utf8_As_Binary(&bp, size, base, 0);
    if (not decoded)
        panic (Error_Invalid_Data_Raw(ARG(VALUE)));

    return Init_Blob(OUT, decoded);
}


//
//  enbase: native [
//
//  "Encodes data into a binary, hexadecimal, or base-64 ASCII string"
//
//      return: [text!]
//      value "If text, will be UTF-8 encoded"
//          [blob! text!]
//      :base "Binary base to use: 64, 16, or 2 (BASE-64 default)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(ENBASE)
{
    INCLUDE_PARAMS_OF_ENBASE;

    REBINT base;
    if (ARG(BASE))
        base = VAL_INT32(unwrap ARG(BASE));
    else
        base = 64;

    Size size;
    const Byte* bp = Cell_Bytes_At(&size, ARG(VALUE));

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    const bool brk = false;
    switch (base) {
      case 64:
        Form_Base64(mo, bp, size, brk);
        break;

      case 16:
        Form_Base16(mo, bp, size, brk);
        break;

      case 2:
        Form_Base2(mo, bp, size, brk);
        break;

      default:
        panic (PARAM(BASE));
    }

    return Init_Text(OUT, Pop_Molded_Strand(mo));
}


//
//  enhex: native [
//
//  "Converts string to use URL-style hex encoding (%XX)"
//
//      return: [
//          any-string? "See http://en.wikipedia.org/wiki/Percent-encoding"
//      ]
//      string "String to encode, all non-ASCII or illegal URL bytes encoded"
//          [<opt-out> any-string?]
//  ]
//
DECLARE_NATIVE(ENHEX)
//
// 1. !!! Length 4 should be legal here, but a warning in an older GCC is
//    complaining that Encode_UTF8_Char reaches out of array bounds when it
//    does not appear to.  Possibly related to this:
//
//      https://gcc.gnu.org/bugzilla/show_bug.cgi?id=43949
//
// 2. Use uppercase hex digits, per RFC 3896 2.1, which is also consistent
//    with JavaScript's encodeURIComponent()
//
//      https://tools.ietf.org/html/rfc3986#section-2.1
//
//    !!! Should this be controlled by a :RELAX refinement and default to
//    not accepting lowercase?
{
    INCLUDE_PARAMS_OF_ENHEX;

    Element* string = Element_ARG(STRING);

    DECLARE_MOLDER (mo);
    Push_Mold (mo);

    REBLEN len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, string);

    Codepoint c;
    cp = Utf8_Next(&c, cp);

    REBLEN i;
    for (i = 0; i < len; cp = Utf8_Next(&c, cp), ++i) {
        Byte encoded[UNI_ENCODED_MAX];
        REBLEN encoded_size;

        if (Is_Utf8_Lead_Byte(c)) {  // non-ASCII chars MUST be percent-encoded
            encoded_size = Encoded_Size_For_Codepoint(c);
            Encode_UTF8_Char(encoded, c, encoded_size);
        }
        else {
            if (not Ascii_Char_Needs_Percent_Encoding(cast(Byte, c))) {
                Append_Codepoint(mo->strand, c);
                continue;
            }
            encoded[0] = cast(Byte, c);
            encoded_size = 1;
        }

        REBLEN n;
        for (n = 0; n != encoded_size; ++n) {  // use uppercase hex digits [2]
            Append_Codepoint(mo->strand, '%');
            Append_Codepoint(mo->strand, g_hex_digits[(encoded[n] & 0xf0) >> 4]);
            Append_Codepoint(mo->strand, g_hex_digits[encoded[n] & 0xf]);
        }
    }

    return Init_Any_String(
        OUT,
        Heart_Of_Builtin_Fundamental(string),
        Pop_Molded_Strand(mo)
    );
}


//
//  dehex: native [
//
//  "Converts URL-style encoded strings, %XX is interpreted as UTF-8 byte"
//
//      return: [any-string?]
//      string "See http://en.wikipedia.org/wiki/Percent-encoding"
//          [any-string?]
//      :blob "Give result as a binary BLOB!, permits %00 encodings"  ; [1]
//  ]
//
DECLARE_NATIVE(DEHEX)
//
// 1. Ren-C is committed to having string types not contain the 0 codepoint,
//    but it's explicitly legal for percent encoding to allow %00 in URLs.
//    Sounds dangerous, but we can support that by returning a BLOB!.  The
//    code was written to use the mold buffer, however, and would have to
//    be rewritten to use a byte buffer for that feature.
{
    INCLUDE_PARAMS_OF_DEHEX;

    Element* string = Element_ARG(STRING);

    if (ARG(BLOB))
        panic ("DEHEX:BLOB not yet implemented, but will permit %00");

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    Utf8(const*) cp = Cell_Utf8_Head(string);

    Codepoint c;
    cp = Utf8_Next(&c, cp);

    while (c != '\0') {
        if (c != '%') {
            Append_Codepoint(mo->strand, c);
            cp = Utf8_Next(&c, cp); // c may be '\0', guaranteed if `i == len`
            continue;
        }

        Byte scan[5];  // 4 bytes plus terminator is max, see RFC 3986
        Size scan_size = 0;

        do {
            if (scan_size > 4)
                return fail ("Percent sequence over 4 bytes long (bad UTF-8)");

            Codepoint hex1;
            Codepoint hex2;
            cp = Utf8_Next(&hex1, cp);
            if (hex1 == '\0')
                hex2 = '\0';
            else
                cp = Utf8_Next(&hex2, cp);

            Byte nibble1;
            Byte nibble2;
            if (
                hex1 > UINT8_MAX
                or not Try_Get_Lex_Hexdigit(&nibble1, cast(Byte, hex1))
                or hex2 > UINT8_MAX
                or not Try_Get_Lex_Hexdigit(&nibble2, cast(Byte, hex2))
            ){
                return fail ("2 hex digits must follow percent, e.g. %XX");
            }

            Byte b = (nibble1 << 4) + nibble2;

            if (scan_size == 0 and Is_Continuation_Byte(b))
                return fail ("UTF-8 can't start with continuation byte");

            if (scan_size > 0 and not Is_Continuation_Byte(b)) {  // next char
                cp = Step_Back_Codepoint(cp);
                cp = Step_Back_Codepoint(cp);
                cp = Step_Back_Codepoint(cp);
                assert(*cast(Byte*, cp) == '%');
                break;
            }

            scan[scan_size] = b;
            ++scan_size;

            cp = Utf8_Next(&c, cp);

            if (Is_Byte_Ascii(b))
                break;
        } while (c == '%');

        scan[scan_size] = '\0';

        const Byte* next = scan;
        trap (
          Codepoint decoded = Back_Scan_Utf8_Char(&next, &scan_size)
        );
        --scan_size;  // see definition of Back_Scan for why it's off by one
        if (scan_size != 0)
            return fail ("Extra continuation characters in %XX of dehex");

        Append_Codepoint(mo->strand, decoded);
    }

    return Init_Any_String(
        OUT,
        Heart_Of_Builtin_Fundamental(string),
        Pop_Molded_Strand(mo)
    );
}


//
//  deline: native [
//
//  "Converts string terminators to standard format, e.g. CR LF to LF"
//
//      return: [text! block!]
//      input "Will be modified (unless :LINES used)"
//          [text! blob!]
//      :lines "Return block of lines (works for LF, CR-LF endings)"
//  ]
//
DECLARE_NATIVE(DELINE)
{
    INCLUDE_PARAMS_OF_DELINE;

    // AS TEXT! verifies the UTF-8 validity of a BLOB!, and checks for any
    // embedded '\0' bytes, illegal in texts...without copying the input.
    //
    Api(Stable*) input = rebStable("as text!", ARG(INPUT));

    if (ARG(LINES)) {
        Init_Block(OUT, Split_Lines(cast(Element*, input)));
        rebRelease(input);
        return OUT;
    }

    Strand* s = Cell_Strand_Ensure_Mutable(input);
    REBLEN len_head = Strand_Len(s);

    REBLEN len_at = Series_Len_At(input);

    Utf8(*) dest = String_At_Known_Mutable(input);
    Utf8(const*) src = dest;

    // DELINE tolerates either LF or CR LF, in order to avoid disincentivizing
    // remote data in CR LF format from being "fixed" to pure LF format, for
    // fear of breaking someone else's script.  However, files must be in
    // *all* CR LF or *all* LF format.  If they are mixed they are considered
    // to be malformed...and need custom handling.
    //
    bool seen_a_cr_lf = false;
    bool seen_a_lone_lf = false;

    REBLEN n;
    for (n = 0; n < len_at;) {
        Codepoint c;
        src = Utf8_Next(&c, src);
        ++n;
        if (c == LF) {
            if (seen_a_cr_lf)
                panic (Error_Mixed_Cr_Lf_Found_Raw());
            seen_a_lone_lf = true;
        }

        if (c == CR) {
            if (seen_a_lone_lf)
                panic (Error_Mixed_Cr_Lf_Found_Raw());

            dest = Write_Codepoint(dest, LF);
            src = Utf8_Next(&c, src);
            ++n;  // will see '\0' terminator before loop check, so is safe
            if (c == LF) {
                --len_head;  // don't write carraige return, note loss of char
                seen_a_cr_lf = true;
                continue;
            }
            panic (  // DELINE requires any CR to be followed by an LF
                Error_Illegal_Cr(Step_Back_Codepoint(src), Strand_Head(s))
            );
        }
        dest = Write_Codepoint(dest, c);
    }

    Term_Strand_Len_Size(s, len_head, dest - String_At(input));

    return input;
}


//
//  enline: native [
//
//  "Converts string terminators to native OS format, e.g. LF to CRLF"
//
//      return: [any-string?]
//      string [any-string?] "(modified)"
//  ]
//
DECLARE_NATIVE(ENLINE)
{
    INCLUDE_PARAMS_OF_ENLINE;

    Element* string = Element_ARG(STRING);

    Strand* s = Cell_Strand_Ensure_Mutable(string);
    Index idx = Series_Index(string);

    Length len;
    Size size = String_Size_Limit_At(&len, string, UNLIMITED);

    REBLEN delta = 0;

    // Calculate the size difference by counting the number of LF's
    // that have no CR's in front of them.
    //
    // !!! The Utf8(*) interface isn't technically necessary if one is
    // counting to the end (one could just go by bytes instead of characters)
    // but this would not work if someone added, say, an ENLINE:PART...since
    // the byte ending position of interest might not be end of the string.

    Utf8(*) cp = Strand_At(s, idx);

    bool relax = false;  // !!! in case we wanted to tolerate CR LF already?
    Codepoint c_prev = '\0';

    REBLEN n;
    for (n = 0; n < len; ++n) {
        Codepoint c;
        cp = Utf8_Next(&c, cp);
        if (c == LF and (not relax or c_prev != CR))
            ++delta;
        if (c == CR and not relax)  // !!! Note: `relax` fixed at false, ATM
            panic (
                Error_Illegal_Cr(Step_Back_Codepoint(cp), Strand_Head(s))
            );
        c_prev = c;
    }

    if (delta == 0)
        return COPY(ARG(STRING)); // nothing to do

    REBLEN old_len = Misc_Num_Codepoints(s);
    require (  // setting `used` will corrupt misc.num_codepoints
      Expand_Flex_Tail_And_Update_Used(s, delta)
    );
    Tweak_Misc_Num_Codepoints(s, old_len + delta);  // just adding CR's

    // One feature of using UTF-8 for strings is that CR/LF substitution can
    // stay a byte-oriented process..because UTF-8 doesn't reuse bytes in the
    // ASCII range, and CR and LF are ASCII.  So as long as the "sliding" is
    // done in terms of byte sizes and not character lengths, it should work.

    Free_Bookmarks_Maybe_Null(s);  // !!! Could this be avoided sometimes?

    Byte* bp = Strand_Head(s); // expand may change the pointer
    Size tail = Strand_Size(s); // size in bytes after expansion

    // Add missing CRs

    while (delta > 0) {

        bp[tail--] = bp[size];  // Copy src to dst.

        if (
            bp[size] == LF
            and (
                not relax  // !!! Note: `relax` fixed at false, ATM
                or size == 0
                or bp[size - 1] != CR
            )
        ){
            bp[tail--] = CR;
            --delta;
        }
        --size;
    }

    return COPY(string);
}


//
//  entab: native [
//
//  "Converts spaces to tabs (default tab size is 4)"
//
//      return: [any-string?]
//      string "(modified)"
//          [any-string?]
//      :size "Specifies the number of spaces per tab"
//          [integer!]
//  ]
//
DECLARE_NATIVE(ENTAB)
{
    INCLUDE_PARAMS_OF_ENTAB;

    Element* string = Element_ARG(STRING);

    REBINT tabsize;
    if (ARG(SIZE))
        tabsize = Int32s(unwrap ARG(SIZE), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    REBLEN len = Series_Len_At(string);

    Utf8(const*) up = String_At(string);
    Index index = Series_Index(string);

    REBINT n = 0;
    for (; index < len; index++) {
        Codepoint c;
        up = Utf8_Next(&c, up);

        // Count leading spaces, insert TAB for each tabsize:
        if (c == ' ') {
            if (++n >= tabsize) {
                Append_Codepoint(mo->strand, '\t');
                n = 0;
            }
            continue;
        }

        // Hitting a leading TAB resets space counter:
        if (c == '\t') {
            Append_Codepoint(mo->strand, '\t');
            n = 0;
        }
        else {
            // Incomplete tab space, pad with spaces:
            for (; n > 0; n--)
                Append_Codepoint(mo->strand, ' ');

            // Copy chars thru end-of-line (or end of buffer):
            for (; index < len; ++index) {
                if (c == '\n') {
                    //
                    // !!! The original code didn't seem to actually move the
                    // append pointer, it just changed the last character to
                    // a newline.  Was this the intent?
                    //
                    Append_Codepoint(mo->strand, '\n');
                    break;
                }
                Append_Codepoint(mo->strand, c);
                up = Utf8_Next(&c, up);
            }
        }
    }

    Heart heart = Heart_Of_Builtin_Fundamental(string);
    return Init_Any_String(OUT, heart, Pop_Molded_Strand(mo));
}


//
//  detab: native [
//
//  "Converts tabs to spaces (default tab size is 4)"
//
//      return: [any-string?]
//      string "(modified)"
//          [any-string?]
//      :size "Specifies the number of spaces per tab"
//          [integer!]
//  ]
//
DECLARE_NATIVE(DETAB)
{
    INCLUDE_PARAMS_OF_DETAB;

    Element* string = Element_ARG(STRING);

    REBLEN len = Series_Len_At(string);

    REBINT tabsize;
    if (ARG(SIZE))
        tabsize = Int32s(unwrap ARG(SIZE), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    // Estimate new length based on tab expansion:

    Utf8(const*) cp = String_At(ARG(STRING));
    Index index = Series_Index(ARG(STRING));

    REBLEN n = 0;

    for (; index < len; ++index) {
        Codepoint c;
        cp = Utf8_Next(&c, cp);

        if (c == '\t') {
            Append_Codepoint(mo->strand, ' ');
            n++;
            for (; n % tabsize != 0; n++)
                Append_Codepoint(mo->strand, ' ');
            continue;
        }

        if (c == '\n')
            n = 0;
        else
            ++n;

        Append_Codepoint(mo->strand, c);
    }

    Heart heart = Heart_Of_Builtin_Fundamental(string);
    return Init_Any_String(OUT, heart, Pop_Molded_Strand(mo));
}


//
//  lowercase: native [
//
//  "Converts string of characters to lowercase"
//
//      return: [any-string? char?]
//      string "(modified if series)"
//          [any-string? char?]
//      :part "Limits to a given length or position"
//          [any-number? any-string?]
//  ]
//
DECLARE_NATIVE(LOWERCASE)
{
    INCLUDE_PARAMS_OF_LOWERCASE;

    Change_Case(OUT, ARG(STRING), ARG(PART), false);
    return OUT;
}


//
//  uppercase: native [
//
//  "Converts string of characters to uppercase"
//
//      return: [any-string? char?]
//      string "(modified if series)"
//          [any-string? char?]
//      :part "Limits to a given length or position"
//          [any-number? any-string?]
//  ]
//
DECLARE_NATIVE(UPPERCASE)
{
    INCLUDE_PARAMS_OF_UPPERCASE;

    Change_Case(OUT, ARG(STRING), ARG(PART), true);
    return OUT;
}


//
//  to-hex: native [
//
//  "Converts numeric value to a hex rune! datatype (with leading # and 0's)"
//
//      return: [rune!]
//      value [integer! tuple!]
//      :size "Specify number of hex digits in result"
//          [integer!]
//  ]
//
DECLARE_NATIVE(TO_HEX)
{
    INCLUDE_PARAMS_OF_TO_HEX;

    Element* arg = Element_ARG(VALUE);

    REBLEN len;
    if (ARG(SIZE))
        len = VAL_INT64(unwrap ARG(SIZE));
    else
        len = 0;  // !!! avoid compiler warning--but rethink this routine

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    if (Is_Integer(arg)) {
        if (not ARG(SIZE) or len > MAX_HEX_LEN)
            len = MAX_HEX_LEN;

        Form_Hex_Pad(mo, VAL_INT64(arg), len);
    }
    else if (Is_Tuple(arg)) {
        REBLEN n;
        if (
            not ARG(SIZE)
            or len > 2 * MAX_TUPLE
            or len > 2 * Sequence_Len(arg)
        ){
            len = 2 * Sequence_Len(arg);
        }
        for (n = 0; n != Sequence_Len(arg); n++)
            Form_Hex2(mo, Sequence_Byte_At(arg, n));
        for (; n < 3; n++)
            Form_Hex2(mo, 0);
    }
    else
        panic (PARAM(VALUE));

    // !!! Issue should be able to use string from mold buffer directly when
    // UTF-8 Everywhere unification of ANY-WORD? and ANY-STRING? is done.
    //
    assert(len == Strand_Size(mo->strand) - mo->base.size);
    if (not Try_Scan_Rune_To_Stack(Binary_At(mo->strand, mo->base.size), len))
        panic (PARAM(VALUE));

    Move_Cell(OUT, TOP_ELEMENT);
    DROP();
    Drop_Mold(mo);
    return OUT;
}


//
//  invalid-utf8?: native [
//
//  "Checks UTF-8 encoding; if invalid gives position in binary of the error"
//
//      return: [<null> blob!]
//      data [blob!]
//  ]
//
DECLARE_NATIVE(INVALID_UTF8_Q)
//
// !!! A motivation for adding this native was because R3-Alpha did not fully
// validate UTF-8 input, for perceived reasons of performance:
//
// https://github.com/rebol/rebol-issues/issues/638
//
// Ren-C reinstated full validation, as it only causes a hit when a non-ASCII
// sequence is read (which is relatively rare in Rebol).  However, it is
// helpful to have a function that will locate invalid byte sequences if one
// is going to try doing something like substituting a character at the
// invalid positions.
{
    INCLUDE_PARAMS_OF_INVALID_UTF8_Q;

    Stable* arg = ARG(DATA);

    Size size;
    const Byte* utf8 = Blob_Size_At(&size, arg);

    const Byte* end = utf8 + size;

    REBLEN trail;
    for (; utf8 != end; utf8 += trail) {
        trail = g_trailing_bytes_for_utf8[*utf8] + 1;
        if (utf8 + trail > end or not Is_Legal_UTF8(utf8, trail)) {
            Copy_Cell(OUT, arg);
            SERIES_INDEX_UNBOUNDED(OUT) = utf8 - Binary_Head(Cell_Binary(arg));
            return OUT;
        }
    }

    return NULLED;  // no invalid byte found
}
