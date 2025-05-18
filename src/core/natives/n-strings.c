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


static bool Check_Char_Range(const Value* val, Codepoint limit)
{
    if (IS_CHAR(val))
        return Cell_Codepoint(val) <= limit;

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

    return Init_Logic(OUT, Check_Char_Range(ARG(VALUE), 0x7f));
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

    return Init_Logic(OUT, Check_Char_Range(ARG(VALUE), 0xff));
}


#define LEVEL_FLAG_DELIMIT_MOLD_RESULT  LEVEL_FLAG_MISCELLANEOUS

#define CELL_FLAG_DELIMITER_NOTE_PENDING    CELL_FLAG_NOTE

#define CELL_FLAG_STACK_NOTE_MOLD           CELL_FLAG_NOTE


#define Push_Join_Delimiter_If_Pending() do { \
    if (delimiter and Get_Cell_Flag(ARG(WITH), DELIMITER_NOTE_PENDING)) { \
        Copy_Cell(PUSH(), ARG(WITH)); \
        Clear_Cell_Flag(ARG(WITH), DELIMITER_NOTE_PENDING); \
    } \
  } while (0)

#define Mark_Join_Delimiter_Pending()  \
    Set_Cell_Flag(ARG(WITH), DELIMITER_NOTE_PENDING) \


//
//  join: native [
//
//  "Join elements to produce a new value"
//
//      return: "Null if no base element and no material in rest to join"
//          [null? any-utf8? any-list? any-sequence? blob!]
//      base [datatype! any-utf8? any-list? any-sequence? blob!]
//      rest "Plain [...] blocks reduced, @[...] block items used as is"
//          [<undo-opt> block! @block! any-utf8? blob! integer!]
//      :with [element? splice!]
//      :head "Include delimiter at head of a non-NULL result"
//      :tail "Include delimiter at tail of a non-NULL result"
//      <local> original-index
//  ]
//
DECLARE_NATIVE(JOIN)
{
    INCLUDE_PARAMS_OF_JOIN;

    Option(const Element*) base;  // const (we derive result_heart each time)
    Heart heart;
    if (Is_Datatype(ARG(BASE))) {
        base = nullptr;
        Option(Heart) datatype_heart = Cell_Datatype_Heart(ARG(BASE));
        if (not datatype_heart)
            return PANIC(PARAM(BASE));
        heart = unwrap datatype_heart;
    }
    else {
        base = Element_ARG(BASE);
        heart = Heart_Of_Builtin_Fundamental(unwrap base);
    }
    bool joining_datatype = not base;  // compiler should optimize out

    Option(const Element*) rest = Is_Nulled(ARG(REST))
        ? nullptr
        : Element_ARG(REST);

    Value* original_index = LOCAL(ORIGINAL_INDEX);

    Option(Element*) delimiter = Is_Nulled(ARG(WITH))
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

    switch (STATE) {
      case ST_JOIN_INITIAL_ENTRY: {
        STATIC_ASSERT(
            CELL_FLAG_DELIMITER_NOTE_PENDING
            == CELL_FLAG_PARAM_NOTE_TYPECHECKED
        );
        assert(Get_Cell_Flag(ARG(WITH), PARAM_NOTE_TYPECHECKED));
        Clear_Cell_Flag(ARG(WITH), PARAM_NOTE_TYPECHECKED);

        if (Any_List(ARG(BASE)) or Any_Sequence(ARG(BASE))) {
            if (
                rest and (
                    not Is_Block(unwrap rest)
                    and not Is_Pinned(BLOCK,unwrap rest)
                )
            ){
                return PANIC("JOIN of list or sequence must join with BLOCK!");
            }
        }

        if (not rest) {  // simple base case: nullptr or COPY
            if (joining_datatype)
                return nullptr;
            return rebValue(CANON(COPY), unwrap base);
        }
        if (joining_datatype and Any_Utf8(unwrap rest))
            goto simple_join;
        goto join_initial_entry; }

      case ST_JOIN_MOLD_STEPPING:
        assert(Not_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT));
        Meta_Unquotify_Undecayed(SPARE);
        goto mold_step_result_in_spare;

      case ST_JOIN_STACK_STEPPING:
        goto stack_step_meta_in_spare;

      case ST_JOIN_EVALUATING_THE_GROUP:
        if (Is_Pinned(BLOCK, unwrap rest))
            SUBLEVEL->executor = &Inert_Meta_Stepper_Executor;
        else {
            assert(Is_Block(unwrap rest));
            SUBLEVEL->executor = &Meta_Stepper_Executor;
        }
        assert(Get_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT));
        goto mold_step_result_in_spare;

      default: assert(false);
    }

  simple_join: { /////////////////////////////////////////////////////////////

    // 1. Hard to unify this mold with code below that uses a level due to
    //    asserts on states balancing.  Easiest to repeat a small bit of code!

    assert(Any_Utf8(unwrap rest));  // shortcut, no evals needed [1]

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    if (Bool_ARG(HEAD) and delimiter)
        Form_Element(mo, unwrap delimiter);

    Form_Element(mo, unwrap rest);

    if (Bool_ARG(TAIL) and delimiter)
        Form_Element(mo, unwrap delimiter);

    return Init_Text(OUT, Pop_Molded_String(mo));

} join_initial_entry: { //////////////////////////////////////////////////////

    // 1. It's difficult to handle the edge cases like `join:with:head` when
    //    you are doing (join 'a 'b) and get it right.  So we make a feed
    //    without having to make a fake @[...] array (though we could do
    //    that as well).  It's a very minor optimization and may not be
    //    worth it, but it points to maybe being able to use a better
    //    optimization in the future (maybe one that wouldn't require a
    //    Level at all).

    Level* sub;

    Flags flags = LEVEL_FLAG_TRAMPOLINE_KEEPALIVE;
    if (Is_Block(unwrap rest)) {
        sub = Make_Level_At(&Meta_Stepper_Executor, unwrap rest, flags);
    }
    else if (Is_Pinned(BLOCK, unwrap rest))
        sub = Make_Level_At(&Inert_Meta_Stepper_Executor, unwrap rest, flags);
    else {
        Feed* feed = Prep_Array_Feed(  // leverage feed mechanics [1]
            Alloc_Feed(),
            unwrap rest,  // first--in this case, the only value in the feed...
            g_empty_array,  // ...because we're using the empty array after that
            0,  // ...at index 0
            SPECIFIED,  // !!! context shouldn't matter
            FEED_MASK_DEFAULT | ((unwrap rest)->header.bits & FEED_FLAG_CONST)
        );

        sub = Make_Level(&Inert_Meta_Stepper_Executor, feed, flags);
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

    if (Bool_ARG(HEAD) and delimiter)  // speculatively start with
        Copy_Cell(PUSH(), unwrap delimiter);  // may be tossed

    Init_Integer(original_index, TOP_INDEX);

    goto first_mold_step;

} start_stack_join: { ////////////////////////////////////////////////////////

    // 1. We want (join 'a: [...]) to work, and (join 'a: []) to give `a:`
    //    In order to do that we use the flag of whether the join produced
    //    anything (e.g. the output is non-null) and if it didn't, we will
    //    add a space back.

    if (not joining_datatype) {
        if (Any_Sequence_Type(heart)) {
            Length len = Cell_Sequence_Len(unwrap base);
            REBINT i;
            for (i = 0; i < len; ++i)
                Copy_Sequence_At(PUSH(), unwrap base, i);
            if (Is_Space(TOP))
                DROP();  // will add back if join produces nothing [1]
        }
        else {
            const Element* tail;
            const Element* at = Cell_List_At(&tail, unwrap base);

            for (; at != tail; ++at)
                Copy_Cell(PUSH(), at);
        }
    }

    if (Bool_ARG(HEAD) and delimiter)  // speculatively start with
        Copy_Cell(PUSH(), unwrap delimiter);  // may be tossed

    Init_Integer(original_index, TOP_INDEX);

    SUBLEVEL->baseline.stack_base = TOP_INDEX;
    STATE = ST_JOIN_STACK_STEPPING;
    return CONTINUE_SUBLEVEL(sub);  // no special source rules

}} next_mold_step: { /////////////////////////////////////////////////////////

    Reset_Evaluator_Erase_Out(SUBLEVEL);
    Clear_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT);

} first_mold_step: { /////////////////////////////////////////////////////////

    Level* sub = SUBLEVEL;

    // 1. There's a concept that being able to put undelimited portions in the
    //    delimit is useful:
    //
    //       >> print ["Outer" "spaced" ["inner" "unspaced"] "seems" "useful"]
    //       Outer spaced innerunspaced seems useful
    //
    //    BUT it may only look like a good idea because it came around before
    //    we could do real string interpolation.  Hacked in for the moment,
    //    review the idea's relevance...

    if (Is_Level_At_End(sub))
        goto finish_mold_join;

    const Element* item = At_Level(sub);
    if (Is_Block(item) and delimiter) {  // hack [1]
        Derelativize(SPARE, item, Level_Binding(sub));
        Fetch_Next_In_Feed(sub->feed);

        Value* unspaced = rebValue(CANON(UNSPACED), rebQ(SPARE));
        if (unspaced == nullptr)  // vaporized, allow it
            goto next_mold_step;

        Push_Join_Delimiter_If_Pending();
        Copy_Cell(PUSH(), unspaced);
        rebRelease(unspaced);
        Mark_Join_Delimiter_Pending();
        goto next_mold_step;
    }

    if (Any_Pinned(item)) {  // fetch and mold
        Set_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT);

        if (Is_Pinned(WORD, item)) {
            Get_Var_May_Panic(SPARE, item, Level_Binding(sub));
            Fetch_Next_In_Feed(sub->feed);
            goto mold_step_result_in_spare;
        }

        if (Is_Pinned(GROUP, item)) {
            SUBLEVEL->executor = &Just_Use_Out_Executor;
            Derelativize(SCRATCH, item, Level_Binding(sub));
            HEART_BYTE(SCRATCH) = TYPE_BLOCK;  // the-block is different
            Fetch_Next_In_Feed(sub->feed);

            SUBLEVEL->baseline.stack_base = TOP_INDEX;
            STATE = ST_JOIN_EVALUATING_THE_GROUP;
            return CONTINUE(SPARE, cast(Element*, SCRATCH));
        }

        return PANIC(item);
    }

    if (Is_Quoted(item)) {  // just mold it
        Push_Join_Delimiter_If_Pending();

        Copy_Cell(PUSH(), item);
        Unquotify(TOP_ELEMENT);
        Set_Cell_Flag(TOP, STACK_NOTE_MOLD);

        Mark_Join_Delimiter_Pending();

        Fetch_Next_In_Feed(sub->feed);
        goto next_mold_step;
    }

    SUBLEVEL->baseline.stack_base = TOP_INDEX;
    STATE = ST_JOIN_MOLD_STEPPING;
    return CONTINUE_SUBLEVEL(sub);  // just evaluate it

} mold_step_result_in_spare: { ///////////////////////////////////////////////

    if (Is_Ghost_Or_Void(SPARE))  // spaced [elide print "hi"], etc
        goto next_mold_step;  // vaporize

    if (Is_Error(SPARE) and Is_Error_Veto_Signal(Cell_Error(SPARE)))
        goto vetoed;

    Decay_If_Unstable(SPARE);  // spaced [match [logic?] false ...]

    if (Is_Splice(SPARE)) {  // only allow splice for mold, for now
        const Element* tail;
        const Element* at = Cell_List_At(&tail, SPARE);
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
    else if (Is_Antiform(SPARE))
        return FAIL(Error_Bad_Antiform(SPARE));

    if (Is_Rune(SPARE)) {  // do not delimit (unified w/char) [5]
        if (delimiter)
            Clear_Cell_Flag(unwrap delimiter, DELIMITER_NOTE_PENDING);
        Copy_Cell(PUSH(), stable_SPARE);
        goto next_mold_step;
    }

    Push_Join_Delimiter_If_Pending();
    Copy_Cell(PUSH(), stable_SPARE);
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

} stack_step_meta_in_spare: { ////////////////////////////////////////////////

    Meta_Unquotify_Undecayed(SPARE);

    if (Is_Ghost_Or_Void(SPARE))
        goto next_stack_step;  // vaporize

    if (Is_Error(SPARE) and Is_Error_Veto_Signal(Cell_Error(SPARE)))
        goto vetoed;

    Decay_If_Unstable(SPARE);

    if (Is_Splice(SPARE)) {
        const Element* tail;
        const Element* at = Cell_List_At(&tail, SPARE);

        if (at == tail)
            goto next_stack_step;  // don't mark produced something

        for (; at != tail; ++at) {
            Push_Join_Delimiter_If_Pending();
            Copy_Cell(PUSH(), at);
            Mark_Join_Delimiter_Pending();
        }

        goto next_stack_step;
    }
    else if (Is_Antiform(SPARE))
        return FAIL(Error_Bad_Antiform(SPARE));

    Push_Join_Delimiter_If_Pending();
    Copy_Cell(PUSH(), stable_SPARE);
    Mark_Join_Delimiter_Pending();

    goto next_stack_step;

} finish_mold_join: { /////////////////////////////////////////////////////////

    // Process the items that made it to the stack.
    //
    // 4. BLOCK!s are prohibitied in DELIMIT because it's too often the case
    //    the result is gibberish--guessing what to do is bad:
    //
    //        >> block: [1 2 <x> hello]
    //
    //        >> print ["Your block is:" block]
    //        Your block is: 12<x>hello  ; ugh.
    //
    // 5. CHAR! suppresses the delimiter logic.  Hence:
    //
    //        >> delimit ":" ["a" space "b" newline void "c" newline "d" "e"]
    //        == "a b^/c^/d:e"
    //
    //    Only the last interstitial is considered a candidate for delimiting.
    //
    // 6. Empty strings distinct from voids in terms of still being delimited.
    //    This is important, e.g. in comma-delimited formats for empty fields.
    //
    //        >> delimit "," [field1 field2 field3]  ; field2 is ""
    //        one,,three
    //
    //    The same principle would apply to a "space-delimited format".

    Drop_Level_Unbalanced(SUBLEVEL);

    if (TOP_INDEX == VAL_INT32(original_index)) {  // nothing pushed
        Drop_Data_Stack_To(STACK_BASE);
        if (joining_datatype)
            return nullptr;
        return rebValue(CANON(COPY), rebQ(unwrap base));
    }

    if (Bool_ARG(TAIL) and delimiter)
        Copy_Cell(PUSH(), unwrap delimiter);

    if (heart == TYPE_BLOB)
        goto finish_blob_join;

    goto finish_utf8_join;

  finish_utf8_join: { ////////////////////////////////////////////////////////

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

  blockscope {
    StackIndex at = STACK_BASE + 1;
    StackIndex tail = TOP_INDEX + 1;

    for (; at != tail; ++at) {
        bool mold = Get_Cell_Flag(Data_Stack_At(Value, at), STACK_NOTE_MOLD);
        Value* v = Copy_Cell(stable_SPARE, Data_Stack_At(Value, at));

        if (mold) {
            assert(NOT_MOLD_FLAG(mo, MOLD_FLAG_SPREAD));
            if (Is_Splice(v))
                SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            Mold_Or_Form_Cell_Ignore_Quotes(mo, v, false);
            CLEAR_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            continue;
        }

        assert(not Is_Antiform(v));  // non-molded splices push items

        if (Any_List(v))  // guessing a behavior is bad [4]
            return PANIC("JOIN requires @var to mold lists");

        if (Any_Sequence(v))  // can have lists in them, dicey [4]
            return PANIC("JOIN requires @var to mold sequences");

        if (Is_Metaform(cast(Element*, v)) or Sigil_Of(cast(Element*, v)))
            return PANIC("JOIN requires @var for elements with sigils");

        Form_Element(mo, cast(Element*, v));
    }
  }

    Drop_Data_Stack_To(STACK_BASE);  // can't be while OnStack() is in scope

    Utf8(const*) utf8 = cast(
        Utf8(const*), Binary_At(mo->string, mo->base.size)
    );
    Size size = String_Size(mo->string) - mo->base.size;
    Length len = String_Len(mo->string) - mo->base.index;

    if (heart == TYPE_WORD) {
        const Symbol* s = Intern_UTF8_Managed(utf8, size);
        Init_Word(OUT, s);
    }
    else if (Any_String_Type(heart)) {
        Init_Any_String(OUT, heart, Pop_Molded_String(mo));
    }
    else if (heart == TYPE_RUNE) {
        Init_Utf8_Non_String(OUT, heart, utf8, size, len);
    }
    else if (heart == TYPE_EMAIL) {
        if (
            cast(const Byte*, utf8) + size
            != Try_Scan_Email_To_Stack(utf8, size)
        ){
            return FAIL("Invalid EMAIL!");
        }
        Move_Drop_Top_Stack_Element(OUT);
    }
    else if (heart == TYPE_URL) {
        if (
            cast(const Byte*, utf8) + size
            != Try_Scan_URL_To_Stack(utf8, size)
        ){
            return FAIL("Invalid URL!");
        }
        Move_Drop_Top_Stack_Element(OUT);
    }
    else
        return PANIC(PARAM(BASE));

    if (mo->string)
        Drop_Mold(mo);

    return OUT;

} finish_blob_join: { ////////////////////////////////////////////////////////

    Binary* buf = BYTE_BUF;
    Count used = 0;

    Set_Flex_Len(buf, 0);

  blockscope {  // needed so Drop_Stack_To() can be outside the block
    OnStack(Value*) at = Data_Stack_At(Value, STACK_BASE + 1);
    OnStack(Value*) tail = Data_Stack_At(Value, TOP_INDEX + 1);

    for (; at != tail; ++at) {
        if (Get_Cell_Flag(at, STACK_NOTE_MOLD)) {
            DECLARE_MOLDER (mo);
            Push_Mold(mo);
            if (Is_Splice(at))
                SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            Mold_Or_Form_Cell_Ignore_Quotes(mo, at, false);

            Utf8(const*) utf8 = cast(
                Utf8(const*), Binary_At(mo->string, mo->base.size)
            );
            Size size = String_Size(mo->string) - mo->base.size;

            Expand_Flex_Tail(buf, size);
            memcpy(Binary_At(buf, used), utf8, size);

            Drop_Mold(mo);
        }
        else switch (Type_Of(at)) {
          case TYPE_INTEGER:
            Expand_Flex_Tail(buf, 1);
            *Binary_At(buf, used) = cast(Byte, VAL_UINT8(at));  // can panic()
            break;

          case TYPE_BLOB: {
            Size size;
            const Byte* data = Cell_Blob_Size_At(&size, at);
            Expand_Flex_Tail(buf, size);
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

            Expand_Flex_Tail(buf, utf8_size);
            memcpy(Binary_At(buf, used), utf8, utf8_size);
            /*Set_Flex_Len(buf, used + utf8_size); */
            break; }

          default:
            return PANIC(Error_Bad_Value(at));
        }

        used = Flex_Used(buf);
    }
  }

    Drop_Data_Stack_To(STACK_BASE);  // can't be while OnStack() is in scope

    Binary* bin = Make_Binary(used);
    Term_Binary_Len(bin, used);
    Mem_Copy(Binary_Head(bin), Binary_Head(buf), used);

    Set_Flex_Len(buf, 0);

    return Init_Blob(OUT, bin);

}} finish_stack_join: { //////////////////////////////////////////////////////

    Drop_Level_Unbalanced(SUBLEVEL);

    if (Bool_ARG(TAIL) and delimiter)
        Copy_Cell(PUSH(), unwrap delimiter);

    if (Any_Sequence_Type(heart)) {
        Option(Error*) error = Trap_Pop_Sequence(OUT, heart, STACK_BASE);
        if (error)
            return FAIL(unwrap error);
    }
    else {
        Source* a = Pop_Managed_Source_From_Stack(STACK_BASE);
        Init_Any_List(OUT, heart, a);
    }

    if (not joining_datatype)
        Tweak_Cell_Binding(OUT, Cell_Binding(unwrap base));

    return OUT;

} vetoed: { ////////////////////////////////////////////////////////////

    Drop_Data_Stack_To(STACK_BASE);
    Drop_Level(SUBLEVEL);

    return nullptr;
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
    if (Bool_ARG(BASE))
        base = VAL_INT32(ARG(BASE));
    else
        base = 64;

    Binary* decoded = maybe Decode_Enbased_Utf8_As_Binary(&bp, size, base, 0);
    if (not decoded)
        return PANIC(Error_Invalid_Data_Raw(ARG(VALUE)));

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
    if (Bool_ARG(BASE))
        base = VAL_INT32(ARG(BASE));
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
        return PANIC(PARAM(BASE));
    }

    return Init_Text(OUT, Pop_Molded_String(mo));
}


//
//  enhex: native [
//
//  "Converts string to use URL-style hex encoding (%XX)"
//
//      return: "See http://en.wikipedia.org/wiki/Percent-encoding"
//          [any-string?]
//      string "String to encode, all non-ASCII or illegal URL bytes encoded"
//          [any-string?]
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

        if (c >= 0x80) {  // all non-ASCII characters *must* be percent encoded
            encoded_size = Encoded_Size_For_Codepoint(c);
            Encode_UTF8_Char(encoded, c, encoded_size);
        }
        else {
            if (not Ascii_Char_Needs_Percent_Encoding(cast(Byte, c))) {
                Append_Codepoint(mo->string, c);
                continue;
            }
            encoded[0] = cast(Byte, c);
            encoded_size = 1;
        }

        REBLEN n;
        for (n = 0; n != encoded_size; ++n) {  // use uppercase hex digits [2]
            Append_Codepoint(mo->string, '%');
            Append_Codepoint(mo->string, g_hex_digits[(encoded[n] & 0xf0) >> 4]);
            Append_Codepoint(mo->string, g_hex_digits[encoded[n] & 0xf]);
        }
    }

    return Init_Any_String(
        OUT,
        Heart_Of_Builtin_Fundamental(string),
        Pop_Molded_String(mo)
    );
}


//
//  dehex: native [
//
//  "Converts URL-style encoded strings, %XX is interpreted as UTF-8 byte"
//
//      return: "Decoded string, with the same string type as the input"
//          [any-string?]
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

    if (Bool_ARG(BLOB))
        return PANIC("DEHEX:BLOB not yet implemented, but will permit %00");

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    Utf8(const*) cp = Cell_Utf8_Head(string);

    Codepoint c;
    cp = Utf8_Next(&c, cp);

    while (c != '\0') {
        if (c != '%') {
            Append_Codepoint(mo->string, c);
            cp = Utf8_Next(&c, cp); // c may be '\0', guaranteed if `i == len`
            continue;
        }

        Byte scan[5];  // 4 bytes plus terminator is max, see RFC 3986
        Size scan_size = 0;

        do {
            if (scan_size > 4)
                return FAIL("Percent sequence over 4 bytes long (bad UTF-8)");

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
                return FAIL("2 hex digits must follow percent, e.g. %XX");
            }

            Byte b = (nibble1 << 4) + nibble2;

            if (scan_size == 0 and Is_Continuation_Byte(b))
                return FAIL("UTF-8 can't start with continuation byte");

            if (scan_size > 0 and not Is_Continuation_Byte(b)) {  // next char
                cp = Step_Back_Codepoint(cp);
                cp = Step_Back_Codepoint(cp);
                cp = Step_Back_Codepoint(cp);
                assert(*c_cast(Byte*, cp) == '%');
                break;
            }

            scan[scan_size] = b;
            ++scan_size;

            cp = Utf8_Next(&c, cp);

            if (b < 0x80)
                break;
        } while (c == '%');

        scan[scan_size] = '\0';

        const Byte* next = scan;
        Codepoint decoded;
        Option(Error*) e = Trap_Back_Scan_Utf8_Char(
            &decoded, &next, &scan_size
        );
        if (e)
            return FAIL(unwrap e);

        --scan_size;  // see definition of Back_Scan for why it's off by one
        if (scan_size != 0)
            return FAIL("Extra continuation characters in %XX of dehex");

        Append_Codepoint(mo->string, decoded);
    }

    return Init_Any_String(
        OUT,
        Heart_Of_Builtin_Fundamental(string),
        Pop_Molded_String(mo)
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
    Value* input = rebValue("as text!", ARG(INPUT));

    if (Bool_ARG(LINES)) {
        Init_Block(OUT, Split_Lines(cast(Element*, input)));
        rebRelease(input);
        return OUT;
    }

    String* s = Cell_String_Ensure_Mutable(input);
    REBLEN len_head = String_Len(s);

    REBLEN len_at = Cell_Series_Len_At(input);

    Utf8(*) dest = Cell_String_At_Known_Mutable(input);
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
                return PANIC(Error_Mixed_Cr_Lf_Found_Raw());
            seen_a_lone_lf = true;
        }

        if (c == CR) {
            if (seen_a_lone_lf)
                return PANIC(Error_Mixed_Cr_Lf_Found_Raw());

            dest = Write_Codepoint(dest, LF);
            src = Utf8_Next(&c, src);
            ++n;  // will see '\0' terminator before loop check, so is safe
            if (c == LF) {
                --len_head;  // don't write carraige return, note loss of char
                seen_a_cr_lf = true;
                continue;
            }
            return PANIC(  // DELINE requires any CR to be followed by an LF
                Error_Illegal_Cr(Step_Back_Codepoint(src), String_Head(s))
            );
        }
        dest = Write_Codepoint(dest, c);
    }

    Term_String_Len_Size(s, len_head, dest - Cell_String_At(input));

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

    String* s = Cell_String_Ensure_Mutable(string);
    REBLEN idx = VAL_INDEX(string);

    Length len;
    Size size = Cell_String_Size_Limit_At(&len, string, UNLIMITED);

    REBLEN delta = 0;

    // Calculate the size difference by counting the number of LF's
    // that have no CR's in front of them.
    //
    // !!! The Utf8(*) interface isn't technically necessary if one is
    // counting to the end (one could just go by bytes instead of characters)
    // but this would not work if someone added, say, an ENLINE:PART...since
    // the byte ending position of interest might not be end of the string.

    Utf8(*) cp = String_At(s, idx);

    bool relax = false;  // !!! in case we wanted to tolerate CR LF already?
    Codepoint c_prev = '\0';

    REBLEN n;
    for (n = 0; n < len; ++n) {
        Codepoint c;
        cp = Utf8_Next(&c, cp);
        if (c == LF and (not relax or c_prev != CR))
            ++delta;
        if (c == CR and not relax)  // !!! Note: `relax` fixed at false, ATM
            return PANIC(
                Error_Illegal_Cr(Step_Back_Codepoint(cp), String_Head(s))
            );
        c_prev = c;
    }

    if (delta == 0)
        return COPY(ARG(STRING)); // nothing to do

    REBLEN old_len = Misc_Num_Codepoints(s);
    Expand_Flex_Tail(s, delta);  // corrupts misc.num_codepoints
    Tweak_Misc_Num_Codepoints(s, old_len + delta);  // just adding CR's

    // One feature of using UTF-8 for strings is that CR/LF substitution can
    // stay a byte-oriented process..because UTF-8 doesn't reuse bytes in the
    // ASCII range, and CR and LF are ASCII.  So as long as the "sliding" is
    // done in terms of byte sizes and not character lengths, it should work.

    Free_Bookmarks_Maybe_Null(s);  // !!! Could this be avoided sometimes?

    Byte* bp = String_Head(s); // expand may change the pointer
    Size tail = String_Size(s); // size in bytes after expansion

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
    if (Bool_ARG(SIZE))
        tabsize = Int32s(ARG(SIZE), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    REBLEN len = Cell_Series_Len_At(string);

    Utf8(const*) up = Cell_String_At(string);
    REBLEN index = VAL_INDEX(string);

    REBINT n = 0;
    for (; index < len; index++) {
        Codepoint c;
        up = Utf8_Next(&c, up);

        // Count leading spaces, insert TAB for each tabsize:
        if (c == ' ') {
            if (++n >= tabsize) {
                Append_Codepoint(mo->string, '\t');
                n = 0;
            }
            continue;
        }

        // Hitting a leading TAB resets space counter:
        if (c == '\t') {
            Append_Codepoint(mo->string, '\t');
            n = 0;
        }
        else {
            // Incomplete tab space, pad with spaces:
            for (; n > 0; n--)
                Append_Codepoint(mo->string, ' ');

            // Copy chars thru end-of-line (or end of buffer):
            for (; index < len; ++index) {
                if (c == '\n') {
                    //
                    // !!! The original code didn't seem to actually move the
                    // append pointer, it just changed the last character to
                    // a newline.  Was this the intent?
                    //
                    Append_Codepoint(mo->string, '\n');
                    break;
                }
                Append_Codepoint(mo->string, c);
                up = Utf8_Next(&c, up);
            }
        }
    }

    Heart heart = Heart_Of_Builtin_Fundamental(string);
    return Init_Any_String(OUT, heart, Pop_Molded_String(mo));
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

    REBLEN len = Cell_Series_Len_At(string);

    REBINT tabsize;
    if (Bool_ARG(SIZE))
        tabsize = Int32s(ARG(SIZE), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    // Estimate new length based on tab expansion:

    Utf8(const*) cp = Cell_String_At(ARG(STRING));
    REBLEN index = VAL_INDEX(ARG(STRING));

    REBLEN n = 0;

    for (; index < len; ++index) {
        Codepoint c;
        cp = Utf8_Next(&c, cp);

        if (c == '\t') {
            Append_Codepoint(mo->string, ' ');
            n++;
            for (; n % tabsize != 0; n++)
                Append_Codepoint(mo->string, ' ');
            continue;
        }

        if (c == '\n')
            n = 0;
        else
            ++n;

        Append_Codepoint(mo->string, c);
    }

    Heart heart = Heart_Of_Builtin_Fundamental(string);
    return Init_Any_String(OUT, heart, Pop_Molded_String(mo));
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

    Value* arg = ARG(VALUE);

    REBLEN len;
    if (Bool_ARG(SIZE))
        len = VAL_INT64(ARG(SIZE));
    else
        len = 0;  // !!! avoid compiler warning--but rethink this routine

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    if (Is_Integer(arg)) {
        if (not Bool_ARG(SIZE) or len > MAX_HEX_LEN)
            len = MAX_HEX_LEN;

        Form_Hex_Pad(mo, VAL_INT64(arg), len);
    }
    else if (Is_Tuple(arg)) {
        REBLEN n;
        if (
            not Bool_ARG(SIZE)
            or len > 2 * MAX_TUPLE
            or len > 2 * Cell_Sequence_Len(arg)
        ){
            len = 2 * Cell_Sequence_Len(arg);
        }
        for (n = 0; n != Cell_Sequence_Len(arg); n++)
            Form_Hex2(mo, Cell_Sequence_Byte_At(arg, n));
        for (; n < 3; n++)
            Form_Hex2(mo, 0);
    }
    else
        return PANIC(PARAM(VALUE));

    // !!! Issue should be able to use string from mold buffer directly when
    // UTF-8 Everywhere unification of ANY-WORD? and ANY-STRING? is done.
    //
    assert(len == String_Size(mo->string) - mo->base.size);
    if (not Try_Scan_Rune_To_Stack(Binary_At(mo->string, mo->base.size), len))
        return PANIC(PARAM(VALUE));

    Move_Drop_Top_Stack_Element(OUT);
    Drop_Mold(mo);
    return OUT;
}


//
//  invalid-utf8?: native [
//
//  "Checks UTF-8 encoding"
//
//      return: "NULL if correct, otherwise position in binary of the error"
//          [null? blob!]
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

    Value* arg = ARG(DATA);

    Size size;
    const Byte* utf8 = Cell_Blob_Size_At(&size, arg);

    const Byte* end = utf8 + size;

    REBLEN trail;
    for (; utf8 != end; utf8 += trail) {
        trail = g_trailing_bytes_for_utf8[*utf8] + 1;
        if (utf8 + trail > end or not Is_Legal_UTF8(utf8, trail)) {
            Copy_Cell(OUT, arg);
            VAL_INDEX_RAW(OUT) = utf8 - Binary_Head(Cell_Binary(arg));
            return OUT;
        }
    }

    return nullptr;  // no invalid byte found
}
