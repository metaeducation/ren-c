//
//  File: %n-strings.c
//  Summary: "native functions for strings"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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

#include "cells/cell-money.h"


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
//  /ascii?: native [
//
//  "Returns TRUE if value or string is in ASCII character range (below 128)"
//
//      return: [logic?]
//      value [any-string? char? integer!]
//  ]
//
DECLARE_NATIVE(ascii_q)
{
    INCLUDE_PARAMS_OF_ASCII_Q;

    return Init_Logic(OUT, Check_Char_Range(ARG(value), 0x7f));
}


//
//  /latin1?: native [
//
//  "Returns TRUE if value or string is in Latin-1 character range (below 256)"
//
//      return: [logic?]
//      value [any-string? char? integer!]
//  ]
//
DECLARE_NATIVE(latin1_q)
{
    INCLUDE_PARAMS_OF_LATIN1_Q;

    return Init_Logic(OUT, Check_Char_Range(ARG(value), 0xff));
}


#define LEVEL_FLAG_DELIMIT_MOLD_RESULT  LEVEL_FLAG_MISCELLANEOUS

#define CELL_FLAG_DELIMITER_NOTE_PENDING    CELL_FLAG_NOTE

#define CELL_FLAG_STACK_NOTE_MOLD           CELL_FLAG_NOTE


#define Push_Join_Delimiter_If_Pending() do { \
    if (not Is_Nulled(delimiter) and  \
      Get_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING)) { \
        Copy_Cell(PUSH(), delimiter); \
        Clear_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING); \
    } \
  } while (0)

#define Mark_Join_Delimiter_Pending() \
    Set_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING)


//
//  /join: native [
//
//  "Join elements to produce a new value"
//
//      return: "Null if no base element and no material in rest to join"
//          [~null~ any-utf8? any-list? any-sequence? blob!]
//      base [type-block! any-utf8? any-list? any-sequence? blob!]
//      rest "Plain [...] blocks reduced, @[...] block items used as is"
//          [~void~ block! the-block! any-utf8? blob!]
//      :with [element? splice?]
//      :head "Include delimiter at head of a non-NULL result"
//      :tail "Include delimiter at tail of a non-NULL result"
//      <local> original-index
//  ]
//
DECLARE_NATIVE(join)
{
    INCLUDE_PARAMS_OF_JOIN;

    Element* base = cast(Element*, ARG(base));
    Value* rest = ARG(rest);

    Value* original_index = LOCAL(original_index);

    Value* delimiter = ARG(with);
    possibly(Get_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING));

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
        assert(Get_Cell_Flag(delimiter, PARAM_NOTE_TYPECHECKED));
        Clear_Cell_Flag(delimiter, PARAM_NOTE_TYPECHECKED);

        if (Is_Void(rest)) {  // simple base case: nullptr or COPY
            if (Is_Type_Block(base))
                return nullptr;
            return rebValue(CANON(COPY), base);
        }
        if (Is_Type_Block(base) and Any_Utf8(rest))
            goto simple_join;
        goto join_initial_entry; }

      case ST_JOIN_MOLD_STEPPING:
        assert(Not_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT));
        goto mold_step_result_in_spare;

      case ST_JOIN_STACK_STEPPING:
        goto stack_step_result_in_spare;

      case ST_JOIN_EVALUATING_THE_GROUP:
        if (Is_The_Block(rest))
            SUBLEVEL->executor = &Inert_Stepper_Executor;
        else {
            assert(Is_Block(rest));
            SUBLEVEL->executor = &Stepper_Executor;
        }
        assert(Get_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT));
        goto mold_step_result_in_spare;

      default: assert(false);
    }

  simple_join: { /////////////////////////////////////////////////////////////

    // 1. Hard to unify this mold with code below that uses a level due to
    //    asserts on states balancing.  Easiest to repeat a small bit of code!

    assert(Any_Utf8(rest));  // shortcut, no evals needed [1]

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    if (REF(head) and not Is_Nulled(delimiter))
        Form_Element(mo, cast(Element*, delimiter));

    Form_Element(mo, cast(Element*, rest));

    if (REF(tail) and not Is_Nulled(delimiter))
        Form_Element(mo, cast(Element*, delimiter));

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
    if (Is_Block(rest)) {
        sub = Make_Level_At(&Stepper_Executor, rest, flags);
    }
    else if (Is_The_Block(rest))
        sub = Make_Level_At(&Inert_Stepper_Executor, rest, flags);
    else {
        Feed* feed = Prep_Array_Feed(  // leverage feed mechanics [1]
            Alloc_Feed(),
            rest,  // first--in this case, the only value in the feed...
            EMPTY_ARRAY,  // ...because we're using the empty array after that
            0,  // ...at index 0
            SPECIFIED,  // !!! context shouldn't matter
            FEED_MASK_DEFAULT | (rest->header.bits & FEED_FLAG_CONST)
        );

        sub = Make_Level(&Inert_Stepper_Executor, feed, flags);
    }

    Push_Level_Erase_Out_If_State_0(SPARE, sub);

    assert(Not_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING));

    Heart result_heart;
    if (Is_Type_Block(base))
        result_heart = VAL_TYPE_HEART(base);
    else
        result_heart = Cell_Heart_Ensure_Noquote(base);

    if (Any_Utf8_Kind(result_heart) or result_heart == REB_BLOB)
        goto start_mold_join;

    assert(Any_List_Kind(result_heart) or Any_Sequence_Kind(result_heart));
    goto start_stack_join;

  start_mold_join: { /////////////////////////////////////////////////////////

    if (not Is_Type_Block(base))
        Copy_Cell(PUSH(), base);

    if (REF(head) and not Is_Nulled(delimiter))  // speculatively start with
        Copy_Cell(PUSH(), cast(Element*, delimiter));  // may be tossed

    Init_Integer(original_index, TOP_INDEX);

    goto first_mold_step;

} start_stack_join: { ////////////////////////////////////////////////////////

    // 1. We want (join 'a: [...]) to work, and (join 'a: []) to give `a:`
    //    In order to do that we use the flag of whether the join produced
    //    anything (e.g. the output is non-null) and if it didn't, we will
    //    add a blank back.

    if (not Is_Type_Block(base)) {
        if (Any_Sequence_Kind(result_heart)) {
            Length len = Cell_Sequence_Len(base);
            REBINT i;
            for (i = 0; i < len; ++i)
                Copy_Sequence_At(PUSH(), base, i);
            if (Is_Blank(TOP))
                DROP();  // will add back if join produces nothing [1]
        }
        else {
            const Element* tail;
            const Element* at = Cell_List_At(&tail, base);

            for (; at != tail; ++at)
                Copy_Cell(PUSH(), at);
        }
    }

    if (REF(head) and not Is_Nulled(delimiter))  // speculatively start with
        Copy_Cell(PUSH(), cast(Element*, delimiter));  // may be tossed

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
    //    delimit is useful--and it really is:
    //
    //       >> print ["Outer" "spaced" ["inner" "unspaced"] "is" "useful"]
    //       Outer spaced innerunspaced is useful
    //
    //    Hacked in for the moment, but this routine should be reformulated
    //    to make it part of one continuous mold.
    //
    // 2. Blanks at source-level count as spaces (deemed too potentially broken
    //    to fetch them from variables and have them mean space).  This is
    //    a long-running experiment that may not pan out, but is cool enough to
    //    keep weighing the pros/cons.  Looked-up-to blanks are illegal.

    if (Is_Level_At_End(sub))
        goto finish_mold_join;

    const Element* item = At_Level(sub);
    if (Is_Block(item) and not Is_Nulled(delimiter)) {  // hack [1]
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

    if (Is_Blank(item)) {  // BLANK! acts as space [2]
        Clear_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING);
        Init_Space(PUSH());
        Fetch_Next_In_Feed(sub->feed);
        goto next_mold_step;
    }

    if (Any_The_Value(item)) {  // fetch and mold
        Set_Level_Flag(LEVEL, DELIMIT_MOLD_RESULT);

        if (Is_The_Word(item) or Is_The_Tuple(item)) {
            Get_Var_May_Fail(SPARE, item, Level_Binding(sub));
            Fetch_Next_In_Feed(sub->feed);
            goto mold_step_result_in_spare;
        }

        if (Is_The_Group(item)) {
            SUBLEVEL->executor = &Just_Use_Out_Executor;
            Derelativize(SCRATCH, item, Level_Binding(sub));
            HEART_BYTE(SCRATCH) = REB_BLOCK;  // the-block is different
            Fetch_Next_In_Feed(sub->feed);

            SUBLEVEL->baseline.stack_base = TOP_INDEX;
            STATE = ST_JOIN_EVALUATING_THE_GROUP;
            return CONTINUE(SPARE, cast(Element*, SCRATCH));
        }

        return FAIL(item);
    }

    if (Is_Quoted(item)) {  // just mold it
        Push_Join_Delimiter_If_Pending();

        Copy_Cell(PUSH(), item);
        Unquotify(TOP, 1);
        Set_Cell_Flag(TOP, STACK_NOTE_MOLD);

        Mark_Join_Delimiter_Pending();

        Fetch_Next_In_Feed(sub->feed);
        goto next_mold_step;
    }

    SUBLEVEL->baseline.stack_base = TOP_INDEX;
    STATE = ST_JOIN_MOLD_STEPPING;
    return CONTINUE_SUBLEVEL(sub);  // just evaluate it

} mold_step_result_in_spare: { ///////////////////////////////////////////////

    // 3. Erroring on NULL has been found to catch real bugs in practice.  It
    //    also enables clever constructs like CURTAIL.
    //

    if (Is_Elision(SPARE))  // spaced [elide print "hi"], etc
        goto next_mold_step;  // vaporize

    Decay_If_Unstable(SPARE);  // spaced [match [logic?] false ...]

    if (Is_Void(SPARE))  // spaced [maybe null], spaced [if null [<a>]], etc
        goto next_mold_step;  // vaporize

    if (Is_Nulled(SPARE))  // catches bugs in practice [3]
        return RAISE(Error_Need_Non_Null_Raw());

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
        return RAISE(Error_Bad_Antiform(SPARE));

    if (Is_Issue(SPARE)) {  // do not delimit (unified w/char) [5]
        Clear_Cell_Flag(delimiter, DELIMITER_NOTE_PENDING);
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

} stack_step_result_in_spare: { //////////////////////////////////////////////

    if (Is_Elision(SPARE))
        goto next_stack_step;  // vaporize

    Decay_If_Unstable(SPARE);

    if (Is_Void(SPARE))
        goto next_stack_step;  // vaporize

    if (Is_Nulled(SPARE))  // catches bugs in practice [3]
        return RAISE(Error_Need_Non_Null_Raw());

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
        return RAISE(Error_Bad_Antiform(SPARE));

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
        if (Is_Type_Block(base))
            return nullptr;
        return rebValue(CANON(COPY), rebQ(base));
    }

    if (REF(tail) and not Is_Nulled(delimiter))
        Copy_Cell(PUSH(), cast(Element*, delimiter));

    Heart heart;
    if (Is_Type_Block(base))
        heart = VAL_TYPE_HEART(base);
    else
        heart = Cell_Heart_Ensure_Noquote(base);

    if (heart == REB_BLOB)
        goto finish_blob_join;

    goto finish_utf8_join;

  finish_utf8_join: { ////////////////////////////////////////////////////////

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

  blockscope {
    OnStack(Value*) at = Data_Stack_At(Value, STACK_BASE + 1);
    OnStack(Value*) tail = Data_Stack_At(Value, TOP_INDEX + 1);

    for (; at != tail; ++at) {
        if (Get_Cell_Flag(at, STACK_NOTE_MOLD)) {
            assert(NOT_MOLD_FLAG(mo, MOLD_FLAG_SPREAD));
            if (Is_Splice(at))
                SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            Mold_Or_Form_Cell_Ignore_Quotes(mo, at, false);
            CLEAR_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
            continue;
        }

        assert(not Is_Antiform(at));  // non-molded splices push items

        if (Any_List(at))  // guessing a behavior is bad [4]
            return FAIL("JOIN requires @var to mold lists");

        if (Any_Sequence(at))  // can have lists in them, dicey [4]
            return FAIL("JOIN requires @var to mold sequences");

        if (Sigil_Of(cast(Element*, at)))
            return FAIL("JOIN requires @var for elements with sigils");

        if (Is_Blank(at))
            return FAIL("JOIN only treats source-level BLANK! as space");

        Form_Element(mo, cast(Element*, at));
    }
  }

    Drop_Data_Stack_To(STACK_BASE);  // can't be while OnStack() is in scope

    Utf8(const*) utf8 = cast(
        Utf8(const*), Binary_At(mo->string, mo->base.size)
    );
    Size size = String_Size(mo->string) - mo->base.size;
    Length len = String_Len(mo->string) - mo->base.index;

    if (Any_Word_Kind(heart)) {
        const Symbol* s = Intern_UTF8_Managed(utf8, size);
        Init_Any_Word(OUT, heart, s);
    }
    else if (Any_String_Kind(heart)) {
        Init_Any_String(OUT, heart, Pop_Molded_String(mo));
    }
    else if (heart == REB_ISSUE) {
        Init_Utf8_Non_String(OUT, heart, utf8, size, len);
    }
    else if (heart == REB_EMAIL) {
        if (utf8 + size != Try_Scan_Email_To_Stack(utf8, size))
            return RAISE("Invalid EMAIL!");
        Move_Drop_Top_Stack_Element(OUT);
    }
    else if (heart == REB_URL) {
        if (utf8 + size != Try_Scan_URL_To_Stack(utf8, size))
            return RAISE("Invalid URL!");
        Move_Drop_Top_Stack_Element(OUT);
    }
    else
        return FAIL(PARAM(base));

    if (mo->string)
        Drop_Mold(mo);

    return OUT;

} finish_blob_join: { ////////////////////////////////////////////////////////

    Binary* buf = BYTE_BUF;
    Count used = 0;

    Set_Flex_Len(buf, 0);

  blockscope {
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
        else switch (VAL_TYPE(at)) {
          case REB_BLANK:
            return FAIL("JOIN only treats source-level BLANK! as space");

          case REB_INTEGER:
            Expand_Flex_Tail(buf, 1);
            *Binary_At(buf, used) = cast(Byte, VAL_UINT8(at));  // can fail()
            break;

          case REB_BLOB: {
            Size size;
            const Byte* data = Cell_Blob_Size_At(&size, at);
            Expand_Flex_Tail(buf, size);
            memcpy(Binary_At(buf, used), data, size);
            break; }

          case REB_ISSUE:
          case REB_TEXT:
          case REB_FILE:
          case REB_EMAIL:
          case REB_URL:
          case REB_TAG: {
            Size utf8_size;
            Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, at);

            Expand_Flex_Tail(buf, utf8_size);
            memcpy(Binary_At(buf, used), utf8, utf8_size);
            /*Set_Flex_Len(buf, used + utf8_size); */
            break; }

          default:
            return FAIL(Error_Bad_Value(at));
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

    if (TOP_INDEX == VAL_INT32(original_index)) {  // nothing pushed
        Drop_Data_Stack_To(STACK_BASE);
        if (Is_Type_Block(base))
            return nullptr;
        return rebValue(CANON(COPY), rebQ(base));
    }

    if (REF(tail) and not Is_Nulled(delimiter))
        Copy_Cell(PUSH(), cast(Element*, delimiter));

    Heart heart;
    if (Is_Type_Block(base))
        heart = VAL_TYPE_HEART(base);
    else
        heart = Cell_Heart_Ensure_Noquote(base);

    if (Any_Sequence_Kind(heart)) {
        Option(Error*) error = Trap_Pop_Sequence(OUT, heart, STACK_BASE);
        if (error)
            return RAISE(unwrap error);

        if (not Is_Type_Block(base))
            BINDING(OUT) = BINDING(base);
    }
    else {
        Init_Any_List(OUT, heart, Pop_Managed_Source_From_Stack(STACK_BASE));

        if (not Is_Type_Block(base))
            BINDING(OUT) = BINDING(base);
    }

    return OUT;

}}


//
//  /debase: native [
//
//  "Decodes base-coded string (BASE-64 default) to binary value"
//
//      return: [blob!]
//      value [blob! text!]
//      :base "The base to convert from: 64, 16, or 2 (defaults to 64)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(debase)
{
    INCLUDE_PARAMS_OF_DEBASE;

    Size size;
    const Byte* bp = Cell_Bytes_At(&size, ARG(value));

    REBINT base = 64;
    if (REF(base))
        base = VAL_INT32(ARG(base));
    else
        base = 64;

    Binary* decoded = maybe Decode_Enbased_Utf8_As_Binary(&bp, size, base, 0);
    if (not decoded)
        return FAIL(Error_Invalid_Data_Raw(ARG(value)));

    return Init_Blob(OUT, decoded);
}


//
//  /enbase: native [
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
DECLARE_NATIVE(enbase)
{
    INCLUDE_PARAMS_OF_ENBASE;

    REBINT base;
    if (REF(base))
        base = VAL_INT32(ARG(base));
    else
        base = 64;

    Size size;
    const Byte* bp = Cell_Bytes_At(&size, ARG(value));

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
        return FAIL(PARAM(base));
    }

    return Init_Text(OUT, Pop_Molded_String(mo));
}


//
//  /enhex: native [
//
//  "Converts string to use URL-style hex encoding (%XX)"
//
//      return: "See http://en.wikipedia.org/wiki/Percent-encoding"
//          [any-string?]
//      string "String to encode, all non-ASCII or illegal URL bytes encoded"
//          [any-string?]
//  ]
//
DECLARE_NATIVE(enhex)
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

    DECLARE_MOLDER (mo);
    Push_Mold (mo);

    REBLEN len;
    Utf8(const*) cp = Cell_Utf8_Len_Size_At(&len, nullptr, ARG(string));

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
            Append_Codepoint(mo->string, Hex_Digits[(encoded[n] & 0xf0) >> 4]);
            Append_Codepoint(mo->string, Hex_Digits[encoded[n] & 0xf]);
        }
    }

    return Init_Any_String(
        OUT,
        Cell_Heart_Ensure_Noquote(ARG(string)),
        Pop_Molded_String(mo)
    );
}


//
//  /dehex: native [
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
DECLARE_NATIVE(dehex)
//
// 1. Ren-C is committed to having string types not contain the 0 codepoint,
//    but it's explicitly legal for percent encoding to allow %00 in URLs.
//    Sounds dangerous, but we can support that by returning a BLOB!.  The
//    code was written to use the mold buffer, however, and would have to
//    be rewritten to use a byte buffer for that feature.
{
    INCLUDE_PARAMS_OF_DEHEX;

    if (REF(blob))
        return FAIL("DEHEX:BLOB not yet implemented, but will permit %00");

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    Utf8(const*) cp = Cell_Utf8_Head(ARG(string));

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
                return RAISE("Percent sequence over 4 bytes long (bad UTF-8)");

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
                return RAISE("2 hex digits must follow percent, e.g. %XX");
            }

            Byte b = (nibble1 << 4) + nibble2;

            if (scan_size == 0 and Is_Continuation_Byte(b))
                return RAISE("UTF-8 can't start with continuation byte");

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
            return RAISE(unwrap e);

        --scan_size;  // see definition of Back_Scan for why it's off by one
        if (scan_size != 0)
            return RAISE("Extra continuation characters in %XX of dehex");

        Append_Codepoint(mo->string, decoded);
    }

    return Init_Any_String(
        OUT,
        Cell_Heart_Ensure_Noquote(ARG(string)),
        Pop_Molded_String(mo)
    );
}


//
//  /deline: native [
//
//  "Converts string terminators to standard format, e.g. CR LF to LF"
//
//      return: [text! block!]
//      input "Will be modified (unless :LINES used)"
//          [text! blob!]
//      :lines "Return block of lines (works for LF, CR-LF endings)"
//  ]
//
DECLARE_NATIVE(deline)
{
    INCLUDE_PARAMS_OF_DELINE;

    // AS TEXT! verifies the UTF-8 validity of a BLOB!, and checks for any
    // embedded '\0' bytes, illegal in texts...without copying the input.
    //
    Value* input = rebValue("as text!", ARG(input));

    if (REF(lines)) {
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
                return FAIL(Error_Mixed_Cr_Lf_Found_Raw());
            seen_a_lone_lf = true;
        }

        if (c == CR) {
            if (seen_a_lone_lf)
                return FAIL(Error_Mixed_Cr_Lf_Found_Raw());

            dest = Write_Codepoint(dest, LF);
            src = Utf8_Next(&c, src);
            ++n;  // will see '\0' terminator before loop check, so is safe
            if (c == LF) {
                --len_head;  // don't write carraige return, note loss of char
                seen_a_cr_lf = true;
                continue;
            }
            return FAIL(  // DELINE requires any CR to be followed by an LF
                Error_Illegal_Cr(Step_Back_Codepoint(src), String_Head(s))
            );
        }
        dest = Write_Codepoint(dest, c);
    }

    Term_String_Len_Size(s, len_head, dest - Cell_String_At(input));

    return input;
}


//
//  /enline: native [
//
//  "Converts string terminators to native OS format, e.g. LF to CRLF"
//
//      return: [any-string?]
//      string [any-string?] "(modified)"
//  ]
//
DECLARE_NATIVE(enline)
{
    INCLUDE_PARAMS_OF_ENLINE;

    Value* val = ARG(string);

    String* s = Cell_String_Ensure_Mutable(val);
    REBLEN idx = VAL_INDEX(val);

    Length len;
    Size size = Cell_String_Size_Limit_At(&len, val, UNLIMITED);

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
            return FAIL(
                Error_Illegal_Cr(Step_Back_Codepoint(cp), String_Head(s))
            );
        c_prev = c;
    }

    if (delta == 0)
        return COPY(ARG(string)); // nothing to do

    REBLEN old_len = s->misc.length;
    Expand_Flex_Tail(s, delta);  // corrupts str->misc.length
    s->misc.length = old_len + delta;  // just adding CR's

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

    return COPY(ARG(string));
}


//
//  /entab: native [
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
DECLARE_NATIVE(entab)
{
    INCLUDE_PARAMS_OF_ENTAB;

    REBINT tabsize;
    if (REF(size))
        tabsize = Int32s(ARG(size), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    REBLEN len = Cell_Series_Len_At(ARG(string));

    Utf8(const*) up = Cell_String_At(ARG(string));
    REBLEN index = VAL_INDEX(ARG(string));

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

    Heart heart = Cell_Heart_Ensure_Noquote(ARG(string));
    return Init_Any_String(OUT, heart, Pop_Molded_String(mo));
}


//
//  /detab: native [
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
DECLARE_NATIVE(detab)
{
    INCLUDE_PARAMS_OF_DETAB;

    REBLEN len = Cell_Series_Len_At(ARG(string));

    REBINT tabsize;
    if (REF(size))
        tabsize = Int32s(ARG(size), 1);
    else
        tabsize = TAB_SIZE;

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    // Estimate new length based on tab expansion:

    Utf8(const*) cp = Cell_String_At(ARG(string));
    REBLEN index = VAL_INDEX(ARG(string));

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

    Heart heart = Cell_Heart_Ensure_Noquote(ARG(string));
    return Init_Any_String(OUT, heart, Pop_Molded_String(mo));
}


//
//  /lowercase: native [
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
DECLARE_NATIVE(lowercase)
{
    INCLUDE_PARAMS_OF_LOWERCASE;

    Change_Case(OUT, ARG(string), ARG(part), false);
    return OUT;
}


//
//  /uppercase: native [
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
DECLARE_NATIVE(uppercase)
{
    INCLUDE_PARAMS_OF_UPPERCASE;

    Change_Case(OUT, ARG(string), ARG(part), true);
    return OUT;
}


//
//  /to-hex: native [
//
//  "Converts numeric value to a hex issue! datatype (with leading # and 0's)"
//
//      return: [issue!]
//      value [integer! tuple!]
//      :size "Specify number of hex digits in result"
//          [integer!]
//  ]
//
DECLARE_NATIVE(to_hex)
{
    INCLUDE_PARAMS_OF_TO_HEX;

    Value* arg = ARG(value);

    REBLEN len;
    if (REF(size))
        len = VAL_INT64(ARG(size));
    else
        len = 0;  // !!! avoid compiler warning--but rethink this routine

    DECLARE_MOLDER (mo);
    Push_Mold(mo);

    if (Is_Integer(arg)) {
        if (not REF(size) or len > MAX_HEX_LEN)
            len = MAX_HEX_LEN;

        Form_Hex_Pad(mo, VAL_INT64(arg), len);
    }
    else if (Is_Tuple(arg)) {
        REBLEN n;
        if (
            not REF(size)
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
        return FAIL(PARAM(value));

    // !!! Issue should be able to use string from mold buffer directly when
    // UTF-8 Everywhere unification of ANY-WORD? and ANY-STRING? is done.
    //
    assert(len == String_Size(mo->string) - mo->base.size);
    if (not Try_Scan_Issue_To_Stack(Binary_At(mo->string, mo->base.size), len))
        return FAIL(PARAM(value));

    Move_Drop_Top_Stack_Element(OUT);
    Drop_Mold(mo);
    return OUT;
}


//
//  /invalid-utf8?: native [
//
//  "Checks UTF-8 encoding"
//
//      return: "NULL if correct, otherwise position in binary of the error"
//          [~null~ blob!]
//      data [blob!]
//  ]
//
DECLARE_NATIVE(invalid_utf8_q)
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

    Value* arg = ARG(data);

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
