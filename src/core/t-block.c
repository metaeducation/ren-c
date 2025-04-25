//
//  File: %t-block.c
//  Summary: "block related datatypes"
//  Section: datatypes
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


// !!! Should sequence comparison delegate to this when it detects it has two
// arrays to compare?  That requires canonization assurance.
//
IMPLEMENT_GENERIC(EQUAL_Q, Any_List)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    Element* a = Element_ARG(VALUE1);
    Element* b = Element_ARG(VALUE2);
    bool strict = Bool_ARG(STRICT);

    const Source* a_array = Cell_Array(a);
    const Source* b_array = Cell_Array(b);
    REBLEN a_index = VAL_INDEX(a);  // checks for out of bounds indices
    REBLEN b_index = VAL_INDEX(b);

    if (a_array == b_array)
        return LOGIC(a_index == b_index);

    const Element* a_tail = Array_Tail(a_array);
    const Element* b_tail = Array_Tail(b_array);
    const Element* a_item = Array_At(a_array, a_index);
    const Element* b_item = Array_At(b_array, b_index);
    Length a_len = a_tail - a_item;
    Length b_len = b_tail - b_item;

    if (a_len != b_len)
        return LOGIC(false);

    for (; a_item != a_tail; ++a_item, ++b_item) {
        if (not Equal_Values(a_item, b_item, strict))
            return LOGIC(false);
    }

    assert(b_item == b_tail);  // they were the same length
    return LOGIC(true);  // got to the end
}


// In the rethought model of Ren-C, arbitrary lists cannot be compared for
// being less than or greater than each other.  It's only legal if the
// elements are pairwise comparable:
//
//     >> [1 "b"] < [2 "a"]
//     == ~okay~  ; anti
//
//     >> ["b" 1] < [2 "a"]
//     ** Error: Can't compare  ; raised error, not failure
//
//     >> try ["b" 1] < [2 "a"]
//     == ~null~  ; anti
//
IMPLEMENT_GENERIC(LESSER_Q, Any_List)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* a = Element_ARG(VALUE1);
    Element* b = Element_ARG(VALUE2);

    const Source* a_array = Cell_Array(a);
    const Source* b_array = Cell_Array(b);
    REBLEN a_index = VAL_INDEX(a);  // checks for out of bounds indices
    REBLEN b_index = VAL_INDEX(b);

    if (a_array == b_array)
        return RAISE("Temporarily disallow compare unequal length lists");

    const Element* a_tail = Array_Tail(a_array);
    const Element* b_tail = Array_Tail(b_array);
    const Element* a_item = Array_At(a_array, a_index);
    const Element* b_item = Array_At(b_array, b_index);
    Length a_len = a_tail - a_item;
    Length b_len = b_tail - b_item;

    if (a_len != b_len)
        return LOGIC(false);  // different lengths not considered equal

    for (; a_item != a_tail; ++a_item, ++b_item) {
        bool lesser;
        if (Try_Lesser_Value(&lesser, a_item, b_item))
            return LOGIC(lesser);  // LESSER? result was meaningful

        bool strict = true;
        if (Equal_Values(a_item, b_item, strict))
            continue;  // don't fret they couldn't compare with LESSER?

        return RAISE("Couldn't compare values");  // fret
    }

    assert(b_item == b_tail);  // they were the same length
    return LOGIC(true);  // got to the end
}


// "Make Type" dispatcher for BLOCK!, GROUP!, FENCE!, and variants (THE-GROUP!,
// TYPE-FENCE!, etc.)
//
IMPLEMENT_GENERIC(MAKE, Any_List)
{
    INCLUDE_PARAMS_OF_MAKE;

    Heart heart = Cell_Datatype_Builtin_Heart(ARG(TYPE));
    assert(Any_List_Type(heart));

    Element* arg = Element_ARG(DEF);

    if (Is_Integer(arg) or Is_Decimal(arg)) {
        //
        // `make block! 10` => creates array with certain initial capacity
        //
        return Init_Any_List(OUT, heart, Make_Source_Managed(Int32s(arg, 0)));
    }
    else if (Is_Text(arg)) {
        //
        // `make block! "a <b> #c"` => `[a <b> #c]`, scans as code (unbound)
        //
        Size size;
        Utf8(const*) utf8 = Cell_Utf8_Size_At(&size, arg);

        Option(const String*) file = ANONYMOUS;
        Init_Any_List(
            OUT,
            heart,
            Scan_UTF8_Managed(file, utf8, size)
        );
        return OUT;
    }
    else if (Is_Frame(arg)) {
        //
        // !!! Experimental behavior; if action can run as arity-0, then
        // invoke it so long as it doesn't return null, collecting values.
        //
        StackIndex base = TOP_INDEX;
        while (true) {
            Value* generated = rebValue(arg);
            if (not generated)
                break;
            Copy_Cell(PUSH(), generated);
            rebRelease(generated);
        }
        return Init_Any_List(OUT, heart, Pop_Source_From_Stack(base));
    }
    else if (Is_Varargs(arg)) {
        //
        // Converting a VARARGS! to an ANY-LIST? involves spooling those
        // varargs to the end and making an array out of that.  It's not known
        // how many elements that will be, so they're gathered to the data
        // stack to find the size, then an array made.  Note that | will stop
        // varargs gathering.
        //
        // !!! This MAKE will be destructive to its input (the varargs will
        // be fetched and exhausted).  That's not necessarily obvious, but
        // with a TO conversion it would be even less obvious...
        //

        // If there's any chance that the argument could produce nulls, we
        // can't guarantee an array can be made out of it.
        //
        if (not Extract_Cell_Varargs_Phase(arg)) {
            //
            // A vararg created from a block AND never passed as an argument
            // so no typeset or quoting settings available.  Can't produce
            // any antiforms, because the data source is a block.
            //
            assert(not Is_Stub_Varlist(Cell_Varargs_Origin(arg)));
        }
        else {
            VarList* context = cast(VarList*, Cell_Varargs_Origin(arg));
            Level* param_level = Level_Of_Varlist_May_Fail(context);

            Phase* phase = Level_Phase(param_level);
            Param* param;
            if (CELL_VARARGS_SIGNED_PARAM_INDEX(arg) < 0)
                param = Phase_Param(phase, - CELL_VARARGS_SIGNED_PARAM_INDEX(arg));
            else
                param = Phase_Param(phase, CELL_VARARGS_SIGNED_PARAM_INDEX(arg));

            Init_Nulled(SPARE);
            if (Typecheck_Atom_In_Spare_Uses_Scratch(LEVEL, param, SPECIFIED))
                return RAISE(Error_Null_Vararg_List_Raw());
        }

        StackIndex base = TOP_INDEX;

        do {
            if (Do_Vararg_Op_Maybe_End_Throws(
                OUT,
                VARARG_OP_TAKE,
                arg
            )){
                Drop_Data_Stack_To(base);
                return BOUNCE_THROWN;
            }

            if (Is_Barrier(OUT))
                break;

            Move_Cell(PUSH(), Decay_If_Unstable(OUT));
        } while (true);

        return Init_Any_List(OUT, heart, Pop_Source_From_Stack(base));
    }

    return RAISE(Error_Bad_Make(heart, arg));
}


//
//  Find_In_Array: C
//
// !!! Comment said "Final Parameters: tail - tail position, match - sequence,
// SELECT - (value that follows)".  It's not clear what this meant.
//
// 1. The choice is made that looking for an empty block should match any
//    position (e.g. "there are infinitely many empty blocks spliced in at
//    any block location").  This choice gives an "always matches" option for
//    the pattern to complement the "never matches" option of NULL.
//
REBINT Find_In_Array(
    Sink(Length) len,
    const Array* array,
    REBLEN index_unsigned, // index to start search
    REBLEN end_unsigned, // ending position
    const Value* pattern,
    Flags flags, // see AM_FIND_XXX
    REBINT skip // skip factor
){
    REBINT index = index_unsigned;  // skip can be negative, tested >= 0
    REBINT end = end_unsigned;

    REBINT start;
    if (skip < 0) {
        start = 0;
        --index;  // (find:skip tail of [1 2] 2 -1) should start at the *2*
    }
    else
        start = index;

    // match a block against a block

    if (Is_Splice(pattern)) {
        *len = Cell_Series_Len_At(pattern);
        if (*len == 0)  // empty block matches any position [1]
            return index_unsigned;

        for (; index >= start and index < end; index += skip) {
            const Element* item_tail = Array_Tail(array);
            const Element* item = Array_At(array, index);

            REBLEN count = 0;
            const Element* other_tail;
            const Element* other = Cell_List_At(&other_tail, pattern);
            for (; other != other_tail; ++other, ++item) {
                if (
                    item == item_tail or
                    not Equal_Values(
                        item,
                        other,
                        did (flags & AM_FIND_CASE)
                    )
                ){
                    break;
                }
                if (++count >= *len)
                    return index;
            }
            if (flags & AM_FIND_MATCH)
                break;
        }
        return NOT_FOUND;
    }

    // Apply predicates to items in block

    if (Is_Action(pattern)) {
        *len = 1;

        for (; index >= start and index < end; index += skip) {
            const Cell* item = Array_At(array, index);

            if (rebUnboxLogic(rebRUN(pattern), rebQ(item)))
                return index;

            if (flags & AM_FIND_MATCH)
                break;
        }
        return NOT_FOUND;
    }

    if (Is_Antiform(pattern))
        fail ("Only Antiforms Supported by FIND are ACTION and SPLICE");

    if (Is_Nulled(pattern)) {  // never match [1]
        *len = 0;
        return NOT_FOUND;
    }

    *len = 1;

    // Optimized find word in block

    if (Any_Word(pattern)) {
        for (; index >= start and index < end; index += skip) {
            const Element* item = Array_At(array, index);
            const Symbol* pattern_symbol = Cell_Word_Symbol(pattern);
            if (Any_Word(item)) {
                if (flags & AM_FIND_CASE) { // Must be same type and spelling
                    if (
                        Cell_Word_Symbol(item) == pattern_symbol
                        and Type_Of(item) == Type_Of(pattern)
                    ){
                        return index;
                    }
                }
                else { // Can be different type or differently cased spelling
                    if (Are_Synonyms(Cell_Word_Symbol(item), pattern_symbol))
                        return index;
                }
            }
            if (flags & AM_FIND_MATCH)
                break;
        }
        return NOT_FOUND;
    }

    // All other cases

    for (; index >= start and index < end; index += skip) {
        const Element* item = Array_At(array, index);
        if (Equal_Values(
            item,
            pattern,
            did (flags & AM_FIND_CASE))
        ){
            return index;
        }
        if (flags & AM_FIND_MATCH)
            break;
    }

    return NOT_FOUND;
}


//
//  Shuffle_Array: C
//
// 1. This is a rare case where we could use raw bit copying since the values
//    are in the same array.  However the C++ build asserts that all elements
//    that get instantiated are initialized, so that can cause an assert if
//    the shuffle ends up being a no-op.  So we have to use DECLARE_ELEMENT()
//
void Shuffle_Array(Array* arr, REBLEN idx, bool secure)
{
    REBLEN n;
    REBLEN k;
    Element* data = Array_Head(arr);

    DECLARE_ELEMENT (swap);  // use raw bit copying? [1]

    for (n = Array_Len(arr) - idx; n > 1;) {
        k = idx + (REBLEN)Random_Int(secure) % n;
        n--;

        if (k != (n + idx)) {  // would assert if Copy_Cell() to itself
            assert(
                (data[k].header.bits & CELL_MASK_PERSIST)
                == (data[n + idx].header.bits & CELL_MASK_PERSIST)
            );
            Copy_Cell(swap, &data[k]);
            Copy_Cell(&data[k], &data[n + idx]);
            Copy_Cell(&data[n + idx], swap);
        }
    }
}


static REBINT Try_Get_Array_Index_From_Picker(
    const Element* v,
    const Element* picker
){
    REBINT n;

    if (Is_Integer(picker) or Is_Decimal(picker)) { // #2312
        n = Int32(picker);
        if (n == 0)
            return -1;  // Rebol2/Red convention: 0 is not a pick
        if (n < 0)
            ++n; // Rebol2/Red convention: (pick tail [a b c] -1) is `c`
        n += VAL_INDEX(v) - 1;
    }
    else if (Is_Word(picker)) {
        //
        // Linear search to case-insensitive find ANY-WORD? matching the canon
        // and return the item after it.  Default to out of range.
        //
        n = -1;

        const Symbol* symbol = Cell_Word_Symbol(picker);
        const Element* tail;
        const Element* item = Cell_List_At(&tail, v);
        REBLEN index = VAL_INDEX(v);
        for (; item != tail; ++item, ++index) {
            if (Any_Word(item) and Are_Synonyms(symbol, Cell_Word_Symbol(item))) {
                n = index + 1;
                break;
            }
        }
    }
    else {
        // For other values, act like a SELECT and give the following item.
        // (Note Find_In_Array_Simple returns the list length if missed,
        // so adding one will be out of bounds.)

        n = 1 + Find_In_Array_Simple(
            Cell_Array(v),
            VAL_INDEX(v),
            c_cast(Element*, picker)
        );
    }

    return n;
}


//
//  Try_Pick_Block: C
//
// Fills out with NULL if no pick.
//
bool Try_Pick_Block(
    Sink(Element) out,
    const Element* block,
    const Element* picker
){
    REBINT n = Get_Num_From_Arg(picker);
    n += VAL_INDEX(block) - 1;
    if (n < 0 or n >= Cell_Series_Len_Head(block))
        return false;

    const Element* slot = Array_At(Cell_Array(block), n);
    Copy_Cell(out, slot);
    return true;
}


IMPLEMENT_GENERIC(MOLDIFY, Any_List)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    assert(VAL_INDEX(v) <= Cell_Series_Len_Head(v));

    Heart heart = Heart_Of_Builtin_Fundamental(v);

    if (form) {
        Option(VarList*) context = nullptr;
        bool relax = false;
        Form_Array_At(mo, Cell_Array(v), VAL_INDEX(v), context, relax);
        return TRASH;
    }

    Sigil sigil = maybe Sigil_For_Heart(heart);
    if (sigil)
        Append_Codepoint(mo->string, Char_For_Sigil(sigil));

    const char *sep;

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD)) {
        CLEAR_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);  // only top level
        sep = "\000\000";
    }
    else if (Any_Block_Type(heart))
        sep = "[]";
    else if (Any_Group_Type(heart))
        sep = "()";
    else if (Any_Fence_Type(heart))
        sep = "{}";
    else
        panic (v);

    Mold_Array_At(mo, Cell_Array(v), VAL_INDEX(v), sep);

    return TRASH;
}


IMPLEMENT_GENERIC(OLDGENERIC, Any_List)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* list = cast(Element*, ARG_N(1));
    Context* binding = Cell_List_Binding(list);

    switch (id) {

    //-- Search:

      case SYM_FIND:
      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_FIND; // must be same as select
        UNUSED(PARAM(SERIES));

        Value* pattern = ARG(PATTERN);

        if (Is_Void(pattern))
            return nullptr;  // VOID in, NULL out

        Flags flags = (
            (Bool_ARG(MATCH) ? AM_FIND_MATCH : 0)
            | (Bool_ARG(CASE) ? AM_FIND_CASE : 0)
        );

        REBLEN limit = Part_Tail_May_Modify_Index(list, ARG(PART));

        const Array* arr = Cell_Array(list);
        REBLEN index = VAL_INDEX(list);

        REBINT skip;
        if (Bool_ARG(SKIP)) {
            skip = VAL_INT32(ARG(SKIP));
            if (skip == 0)
                return FAIL(PARAM(SKIP));
        }
        else
            skip = 1;

        Length len;
        REBINT find = Find_In_Array(
            &len,
            arr,
            index,
            limit,
            pattern,
            flags,
            skip
        );

        if (find == NOT_FOUND)
            return nullptr;

        REBLEN ret = find;
        assert(ret <= limit);
        UNUSED(find);

        if (id == SYM_FIND) {
            Source* pack = Make_Source_Managed(2);
            Set_Flex_Len(pack, 2);

            Copy_Meta_Cell(Array_At(pack, 0), list);
            VAL_INDEX_RAW(Array_At(pack, 0)) = ret;

            Copy_Meta_Cell(Array_At(pack, 1), list);
            VAL_INDEX_RAW(Array_At(pack, 1)) = ret + len;

            return Init_Pack(OUT, pack);
        }
        else
            assert(id == SYM_SELECT);

        ret += len;
        if (ret >= limit)
            return nullptr;

        Derelativize(OUT, Array_At(arr, ret), binding);
        return Inherit_Const(stable_OUT, list); }

    //-- Modification:
      case SYM_APPEND:
      case SYM_INSERT:
      case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;
        UNUSED(PARAM(SERIES));

        Value* arg = ARG(VALUE);
        assert(not Is_Nulled(arg));  // not ~null~ in typecheck

        REBLEN len; // length of target
        if (id == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(list, ARG(PART));
        else
            len = Part_Limit_Append_Insert(ARG(PART));

        // Note that while inserting or appending VOID is a no-op, CHANGE with
        // a :PART can actually erase data.
        //
        if (Is_Void(arg) and len == 0) {
            if (id == SYM_APPEND)  // append always returns head
                VAL_INDEX_RAW(list) = 0;
            return COPY(list);  // don't fail on read only if would be a no-op
        }

        Source* arr = Cell_Array_Ensure_Mutable(list);
        REBLEN index = VAL_INDEX(list);

        Flags flags = 0;

        Copy_Cell(OUT, list);

        if (Is_Void(arg)) {
            // not necessarily a no-op (e.g. CHANGE can erase)
        }
        else if (Is_Splice(arg)) {
            flags |= AM_SPLICE;
            QUOTE_BYTE(arg) = NOQUOTE_1;  // make plain group
        }
        else
            assert(not Is_Antiform(arg));

        if (Bool_ARG(PART))
            flags |= AM_PART;
        if (Bool_ARG(LINE))
            flags |= AM_LINE;

        VAL_INDEX_RAW(OUT) = Modify_Array(
            arr,
            index,
            unwrap id,
            arg,
            flags,
            len,
            Bool_ARG(DUP) ? Int32(ARG(DUP)) : 1
        );
        return OUT; }

      case SYM_CLEAR: {
        Array* arr = Cell_Array_Ensure_Mutable(list);
        REBLEN index = VAL_INDEX(list);

        if (index < Cell_Series_Len_Head(list)) {
            if (index == 0)
                Reset_Array(arr);
            else
                Set_Flex_Len(arr, index);
        }
        return COPY(list);
    }

    //-- Special actions:

      case SYM_SWAP: {
        INCLUDE_PARAMS_OF_SWAP;
        UNUSED(ARG(SERIES1));

        Value* arg = ARG(SERIES2);
        if (not Any_List(arg))
            return FAIL(PARAM(SERIES2));

        REBLEN index = VAL_INDEX(list);

        if (
            index < Cell_Series_Len_Head(list)
            and VAL_INDEX(arg) < Cell_Series_Len_Head(arg)
        ){
            // Cell bits can be copied within the same array
            //
            Element* a = Cell_List_At_Ensure_Mutable(nullptr, list);
            Element* b = Cell_List_At_Ensure_Mutable(nullptr, arg);
            Element temp;
            temp.header = a->header;
            temp.payload = a->payload;
            temp.extra = a->extra;
            Copy_Cell(a, b);
            Copy_Cell(b, &temp);
        }
        return COPY(list); }

    // !!! The ability to transform some BLOCK!s into PORT!s for some actions
    // was hardcoded in a fairly ad-hoc way in R3-Alpha, which was based on
    // an integer range of action numbers.  Ren-C turned these numbers into
    // symbols, where order no longer applied.  The mechanism needs to be
    // rethought, see:
    //
    // https://github.com/metaeducation/ren-c/issues/311
    //

      case SYM_READ:
      case SYM_WRITE:
      case SYM_QUERY:
      case SYM_OPEN:
      case SYM_CREATE:
      case SYM_DELETE:
      case SYM_RENAME: {
        //
        // !!! We are going to "re-apply" the call frame with routines we
        // are going to read the ARG_N(1) slot *implicitly* regardless of
        // what value points to.
        //
        const Value* made = rebValue("make port! @", ARG_N(1));
        assert(Is_Port(made));
        Copy_Cell(ARG_N(1), made);
        rebRelease(made);
        return BOUNCE_CONTINUE; }  // should dispatch to the PORT!

      default:
        break; // fallthrough to error
    }

    return UNHANDLED;
}


// 1. Historically, TO conversions have been binding agnostic.  Using AS
//    will give you the same binding as the original but no copy, while
//    COPY will give you the same binding as the original.  Should this
//    code delegate to changing the heart byte of "whatever COPY does?"
//
// 2. The scanner uses the data stack, but it could just take sequential
//    cells in any array...and the data stack just being an example of that.
//    Then we wouldn't have to push the cells here.
//
// 3. While it may not seem useful (to word! [...]) only works on single
//    element blocks with a word in them, e.g. (to word! [a]).  All other
//    blocks are errors.
//
IMPLEMENT_GENERIC(TO, Any_List)
{
    INCLUDE_PARAMS_OF_TO;

    Element* list = Element_ARG(ELEMENT);
    Heart to = Cell_Datatype_Builtin_Heart(ARG(TYPE));

    if (Any_List_Type(to)) {
        Length len;
        const Element* at = Cell_List_Len_At(&len, list);
        return Init_Any_List(
            OUT, to, Copy_Values_Len_Shallow(at, len)  // !!! binding? [1]
        );
    }

    if (Any_Sequence_Type(to)) {  // (to path! [a/b/c]) -> a/b/c
        Length len;
        const Element* item = Cell_List_Len_At(&len, list);
        if (Cell_Series_Len_At(list) != 1)
            return RAISE("Can't TO ANY-SEQUENCE? on list with length > 1");

        if (
            (Is_Path(item) and Any_Path_Type(to))
            or (Is_Chain(item) and Any_Chain_Type(to))
            or (Is_Tuple(item) and Any_Tuple_Type(to))
        ){
            Copy_Cell(OUT, item);
            HEART_BYTE(OUT) = to;
            return OUT;
        }

        return RAISE("TO ANY-SEQUENCE? needs list with a sequence in it");
    }

    if (Any_Word_Type(to)) {  // to word! '{a} -> a, see [3]
        Length len;
        const Element* item = Cell_List_Len_At(&len, list);
        if (Cell_Series_Len_At(list) != 1)
            return RAISE("Can't TO ANY-WORD? on list with length > 1");
        if (not Is_Word(item))
            return RAISE("TO ANY-WORD? needs list with one word in it");
        Copy_Cell(OUT, item);
        HEART_BYTE(OUT) = to;
        return OUT;
    }

    if (Any_Utf8_Type(to)) {  // to tag! [1 a #b] => <1 a #b>
        assert(not Any_Word_Type(to));

        DECLARE_MOLDER (mo);
        SET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);
        Push_Mold(mo);

        Mold_Or_Form_Element(mo, list, false);
        if (Any_String_Type(to))
            return Init_Any_String(OUT, to, Pop_Molded_String(mo));

        Init_Utf8_Non_String(
            OUT,
            to,
            cast(Utf8(const*), Binary_At(mo->string, mo->base.size)),
            String_Len(mo->string) - mo->base.index,
            String_Size(mo->string) - mo->base.size
        );
        Drop_Mold(mo);
        return OUT;
    }

    if (to == TYPE_INTEGER) {
        Length len;
        const Element* at = Cell_List_Len_At(&len, list);
        if (len != 1 or not Is_Integer(at))
            return RAISE("TO INTEGER! works on 1-element integer lists");
        return COPY(at);
    }

    if (to == TYPE_MAP) {  // to map! [key1 val1 key2 val2 key3 val3]
        Length len = Cell_Series_Len_At(list);
        if (len % 2 != 0)
            return RAISE("TO MAP! of list must have even number of items");

        const Element* tail;
        const Element* at = Cell_List_At(&tail, list);

        Map* map = Make_Map(len / 2);  // map size is half block length
        Append_Map(map, at, tail, len);
        Rehash_Map(map);
        return Init_Map(OUT, map);
    }

    if (to == TYPE_PAIR) {
        const Element* tail;
        const Element* item = Cell_List_At(&tail, list);

        if (
            Is_Integer(item) and Is_Integer(item + 1)
            and (tail == item + 2)
        ){
            return Init_Pair(OUT, VAL_INT64(item), VAL_INT64(item + 1));
        }
        return FAIL("TO PAIR! only works on lists with two integers");
    }

    if (to == TYPE_BLANK)
        return GENERIC_CFUNC(AS, Any_List)(LEVEL);

    return UNHANDLED;
}


//
//  Trap_Alias_Any_List_As: C
//
// 1. The init of a listlike sequence may not use the array you pass in.
//    But regardless, the AS locks it...because whether it decides to
//    use the array or not is an implementation detail.  It will reuse
//    the array at least some of the time, so freeze it all of the time.
//
Option(Error*) Trap_Alias_Any_List_As(
    Sink(Element) out,
    const Element* list,
    Heart as
){
    if (Any_List_Type(as)) {
        Copy_Cell(out, list);
        HEART_BYTE(out) = as;
        return SUCCESS;
    }

    if (Any_Sequence_Type(as)) {
        if (not Is_Source_Frozen_Shallow(Cell_Array(list)))  // freeze it [1]
            Freeze_Source_Shallow(Cell_Array_Ensure_Mutable(list));

        DECLARE_ELEMENT (temp);  // need to rebind
        Option(Error*) e = Trap_Init_Any_Sequence_At_Listlike(
            temp,
            as,
            Cell_Array(list),
            VAL_INDEX(list)
        );
        if (e)
            return e;

        /* Tweak_Cell_Binding(temp) = Cell_Binding(list); */  // may be unfit
        Derelativize(out, temp, Cell_Binding(list));  // try this instead (?)

        return SUCCESS;
    }

    if (as == TYPE_BLANK) {  // !!! think it needs to make list immutable?
        Length len;
        Cell_List_Len_At(&len, list);
        if (len == 0) {
            Init_Blank(out);
            return SUCCESS;
        }
        return Error_User("Can only AS/TO convert empty series to BLANK!");
    }

    return Error_Invalid_Type(as);
}


IMPLEMENT_GENERIC(AS, Any_List)
{
    INCLUDE_PARAMS_OF_AS;

    Element* list = Element_ARG(ELEMENT);
    Heart as = Cell_Datatype_Builtin_Heart(ARG(TYPE));

    Option(Error*) e = Trap_Alias_Any_List_As(OUT, list, as);
    if (e)
        return FAIL(unwrap e);

    return OUT;
}


// 1. We shouldn't be returning a const value from the copy, but if the input
//    value was const and we don't copy some types deeply, those types should
//    retain the constness intended for them.
//
IMPLEMENT_GENERIC(COPY, Any_List)
{
    INCLUDE_PARAMS_OF_COPY;

    Element* list = Element_ARG(VALUE);

    REBLEN tail = Part_Tail_May_Modify_Index(list, ARG(PART));

    const Array* arr = Cell_Array(list);
    REBLEN index = VAL_INDEX(list);

    Flags flags = FLEX_MASK_MANAGED_SOURCE;

    flags |= (list->header.bits & ARRAY_FLAG_CONST_SHALLOW);  // retain [1]

    Source* copy = cast(Source*, Copy_Array_Core_Managed(
        flags, // flags
        arr,
        index, // at
        tail, // tail
        0, // extra
        Bool_ARG(DEEP)
    ));

    Init_Any_List(OUT, Heart_Of_Builtin_Fundamental(list), copy);
    Tweak_Cell_Binding(OUT, Cell_List_Binding(list));
    return OUT;
}


IMPLEMENT_GENERIC(PICK, Any_List)
{
    INCLUDE_PARAMS_OF_PICK;

    const Element* list = Element_ARG(LOCATION);
    const Element* picker = Element_ARG(PICKER);

    REBINT n = Try_Get_Array_Index_From_Picker(list, picker);
    if (n < 0 or n >= Cell_Series_Len_Head(list))
        return RAISE(Error_Bad_Pick_Raw(picker));

    const Element* at = Array_At(Cell_Array(list), n);

    Copy_Cell(OUT, at);
    return Inherit_Const(OUT, list);
}


IMPLEMENT_GENERIC(POKE_P, Any_List)
{
    INCLUDE_PARAMS_OF_POKE_P;

    Element* list = Element_ARG(LOCATION);
    const Element* picker = Element_ARG(PICKER);

    Option(const Value*) opt_poke = Optional_ARG(VALUE);
    if (not opt_poke or Is_Antiform(unwrap opt_poke))
        return FAIL(PARAM(VALUE));
    const Element* poke = c_cast(Element*, unwrap opt_poke);

    // !!! If we are jumping here from getting updated bits, then
    // if the block isn't immutable or locked from modification, the
    // memory may have moved!  There's no way to guarantee semantics
    // of an update if we don't lock the array for the poke duration.
    //
    REBINT n = Try_Get_Array_Index_From_Picker(list, picker);
    if (n < 0 or n >= Cell_Series_Len_Head(list))
        return FAIL(Error_Out_Of_Range(picker));

    Array* mut_arr = Cell_Array_Ensure_Mutable(list);
    Element* at = Array_At(mut_arr, n);
    Copy_Cell(at, poke);

    return nullptr;  // Array* is still fine, caller need not update
}


IMPLEMENT_GENERIC(TAKE, Any_List)
{
    INCLUDE_PARAMS_OF_TAKE;

    if (Bool_ARG(DEEP))
        return FAIL(Error_Bad_Refines_Raw());

    Element* list = Element_ARG(SERIES);
    Heart heart = Heart_Of_Builtin_Fundamental(list);  // TAKE gives same heart

    Source* arr = Cell_Array_Ensure_Mutable(list);

    REBLEN len;
    if (Bool_ARG(PART)) {
        len = Part_Len_May_Modify_Index(list, ARG(PART));
        if (len == 0)
            return Init_Any_List(OUT, heart, Make_Source_Managed(0));
    }
    else
        len = 1;

    REBLEN index = VAL_INDEX(list); // Partial() can change index

    if (Bool_ARG(LAST))
        index = Cell_Series_Len_Head(list) - len;

    if (index >= Cell_Series_Len_Head(list)) {
        if (not Bool_ARG(PART))
            return RAISE(Error_Nothing_To_Take_Raw());

        return Init_Any_List(OUT, heart, Make_Source_Managed(0));
    }

    if (Bool_ARG(PART)) {
        Source* copy = Copy_Source_At_Max_Shallow(arr, index, len);
        Init_Any_List(OUT, heart, copy);
    }
    else
        Derelativize(OUT, Array_At(arr, index), Cell_List_Binding(list));

    Remove_Flex_Units(arr, index, len);
    return OUT;
}


// 1. We must reverse the sense of the newline markers as well, #2326
//    Elements that used to be the *end* of lines now *start* lines.  So
//    really this just means taking newline pointers that were on the next
//    element and putting them on the previous element.
//
// 2. When we move the back cell to the front position, it gets the newline
//    flag based on the flag state that was *after* it.
//
// 3. We're pushing the back pointer toward the front, so the flag that was
//    on the back will be the after for the next blit.
//
IMPLEMENT_GENERIC(REVERSE, Any_List)
{
    INCLUDE_PARAMS_OF_REVERSE;

    Element* list = Element_ARG(SERIES);

    Source* arr = Cell_Array_Ensure_Mutable(list);
    REBLEN index = VAL_INDEX(list);

    REBLEN len = Part_Len_May_Modify_Index(list, ARG(PART));
    if (len == 0)
        return COPY(list); // !!! do 1-element reversals update newlines?

    Element* front = Array_At(arr, index);
    Element* back = front + len - 1;

    bool line_back;  // must reverse sense of newlines [1]
    if (back == Array_Last(arr)) // !!! review tail newline handling
        line_back = Get_Source_Flag(arr, NEWLINE_AT_TAIL);
    else
        line_back = Get_Cell_Flag(back + 1, NEWLINE_BEFORE);

    for (len /= 2; len > 0; --len, ++front, --back) {
        bool line_front = Get_Cell_Flag(front + 1, NEWLINE_BEFORE);

        Element temp;
        temp.header = front->header;
        temp.extra = front->extra;
        temp.payload = front->payload;
      #if DEBUG_TRACK_EXTEND_CELLS
        temp.file = front->file;
        temp.line = front->line;
        temp.tick = front->tick;
        temp.touch = front->touch;
      #endif

        front->header = back->header;
        front->extra = back->extra;
        front->payload = back->payload;
      #if DEBUG_TRACK_EXTEND_CELLS
        front->file = back->file;
        front->line = back->line;
        front->tick = back->tick;
        front->touch = back->touch;
      #endif
        if (line_back)  // back to front gets flag that was *after* it [2]
            Set_Cell_Flag(front, NEWLINE_BEFORE);
        else
            Clear_Cell_Flag(front, NEWLINE_BEFORE);

        line_back = Get_Cell_Flag(back, NEWLINE_BEFORE);
        back->header = temp.header;
        back->extra = temp.extra;
        back->payload = temp.payload;
      #if DEBUG_TRACK_EXTEND_CELLS
        back->file = temp.file;
        back->line = temp.line;
        back->tick = temp.tick;
        back->touch = temp.touch;
      #endif
        if (line_front)  // flag on back will be after for next blit [3]
            Set_Cell_Flag(back, NEWLINE_BEFORE);
        else
            Clear_Cell_Flag(back, NEWLINE_BEFORE);
    }
    return COPY(list);
}


// See notes on RANDOM-PICK on whether specializations like this are worth it.
//
IMPLEMENT_GENERIC(RANDOM_PICK, Any_List)
{
    INCLUDE_PARAMS_OF_RANDOM_PICK;

    Element* list = Element_ARG(COLLECTION);

    REBLEN index = VAL_INDEX(list);
    if (index >= Cell_Series_Len_Head(list))
        return RAISE(Error_Bad_Pick_Raw(Init_Integer(SPARE, 0)));

    Element* spare = Init_Integer(
        SPARE,
        1 + (Random_Int(Bool_ARG(SECURE))
            % (Cell_Series_Len_Head(list) - index))
    );

    if (not Try_Pick_Block(OUT, list, spare))
        return nullptr;
    return Inherit_Const(OUT, list);
}


IMPLEMENT_GENERIC(SHUFFLE, Any_List)
{
    INCLUDE_PARAMS_OF_SHUFFLE;

    Element* list = Element_ARG(SERIES);

    Array* arr = Cell_Array_Ensure_Mutable(list);
    Shuffle_Array(arr, VAL_INDEX(list), Bool_ARG(SECURE));
    return COPY(list);
}


//
//  file-of: native:generic [
//
//  "Get the file (or URL) that a value was loaded from, if possible"
//
//      return: "Raised error if no file available (use TRY to get NULL)"
//          [~null~ file! url!]
//      element "Typically only ANY-LIST? know their file"
//          [<maybe> element?]
//  ]
//
DECLARE_NATIVE(FILE_OF)
{
    INCLUDE_PARAMS_OF_FILE_OF;

    Element* elem = Element_ARG(ELEMENT);
    QUOTE_BYTE(elem) = NOQUOTE_1;  // allow line-of and file-of on quoted/quasi

    return Dispatch_Generic(FILE_OF, elem, LEVEL);
}

IMPLEMENT_GENERIC(FILE_OF, Any_Element)  // generic fallthrough: raise error
{
    return RAISE("No file available for element");
}


//
//  line-of: native:generic [
//
//  "Get the line number that a value was loaded from, if possible"
//
//      return: "Raised error if no line available (use TRY to get NULL)"
//          [~null~ integer!]
//      element "Typically only ANY-LIST? know their file"
//          [<maybe> element?]
//  ]
//
DECLARE_NATIVE(LINE_OF)
{
    INCLUDE_PARAMS_OF_LINE_OF;

    Element* elem = Element_ARG(ELEMENT);
    QUOTE_BYTE(elem) = NOQUOTE_1;  // allow line-of and file-of on quoted/quasi

    return Dispatch_Generic(FILE_OF, elem, LEVEL);
}

IMPLEMENT_GENERIC(LINE_OF, Any_Element)  // generic fallthrough: raise error
{
    return RAISE("No line available for element");
}


IMPLEMENT_GENERIC(FILE_OF, Any_List)
{
    INCLUDE_PARAMS_OF_FILE_OF;

    Element* list = Element_ARG(ELEMENT);
    const Source* s = Cell_Array(list);

    Option(const String*) file = Link_Filename(s);
    if (not file)
        return RAISE("No file available for list");
    return Init_File(OUT, unwrap file);  // !!! or URL! (track with bit...)
}


IMPLEMENT_GENERIC(LINE_OF, Any_List)
{
    INCLUDE_PARAMS_OF_LINE_OF;

    Element* list = Element_ARG(ELEMENT);
    const Source* s = Cell_Array(list);

    if (MISC_SOURCE_LINE(s) == 0)
        return RAISE("No line available for list");
    return Init_Integer(OUT, MISC_SOURCE_LINE(s));
}


typedef struct {
    bool cased;
    bool reverse;
    REBLEN offset;
    const Value* comparator;
} SortInfo;


//
//  Qsort_Values_Callback: C
//
static int Qsort_Values_Callback(void *state, const void *p1, const void *p2)
{
    SortInfo* info = cast(SortInfo*, state);

    const Element* v1 = Known_Element(c_cast(Atom*, p1));
    const Element* v2 = Known_Element(c_cast(Atom*, p2));
    possibly(info->cased);  // !!! not applicable in LESSER? comparisons
    bool strict = false;

    DECLARE_VALUE (result);
    if (rebRunThrows(
        result,  // <-- output cell
        rebRUN(info->comparator),
            info->reverse ? rebQ(v1) : rebQ(v2),
            info->reverse ? rebQ(v2) : rebQ(v1)
    )){
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));
    }

    if (not Is_Logic(result))
        fail ("SORT predicate must return logic (NULL or OKAY antiform)");

    if (Cell_Logic(result))  // comparator has LESSER? semantics
        return 1;  // returning 1 means lesser, it seems (?)

    if (Equal_Values(v1, v2, strict))
        return 0;

    return -1;  // not lesser, and not equal, so assume greater
}


IMPLEMENT_GENERIC(SORT, Any_List)
{
    INCLUDE_PARAMS_OF_SORT;

    Element* list = Element_ARG(SERIES);
    Array* arr = Cell_Array_Ensure_Mutable(list);

    SortInfo info;
    info.cased = Bool_ARG(CASE);
    info.reverse = Bool_ARG(REVERSE);
    UNUSED(Bool_ARG(ALL));  // !!! not used?

    Value* cmp = ARG(COMPARE);  // null if no :COMPARE
    Deactivate_If_Action(cmp);
    if (Is_Frame(cmp)) {
        info.comparator = cmp;
        info.offset = 0;
    }
    else if (Is_Integer(cmp)) {
        info.comparator = nullptr;
        info.offset = Int32(cmp) - 1;
        fail ("INTEGER! support (e.g. column select) not working in sort");
    }
    else {
        assert(Is_Nulled(cmp));
        info.comparator = LIB(LESSER_Q);
        info.offset = 0;
    }

    Copy_Cell(OUT, list);  // save list before messing with index

    REBLEN len = Part_Len_May_Modify_Index(list, ARG(PART));
    if (len <= 1)
        return OUT;
    REBLEN index = VAL_INDEX(list);  // ^-- may have been modified

    // Skip factor:
    REBLEN skip;
    if (Is_Nulled(ARG(SKIP)))
        skip = 1;
    else {
        skip = Get_Num_From_Arg(ARG(SKIP));
        if (skip <= 0 or len % skip != 0 or skip > len)
            return FAIL(Error_Out_Of_Range(ARG(SKIP)));
    }

    bsd_qsort_r(
        Array_At(arr, index),
        len / skip,
        sizeof(Cell) * skip,
        &info,
        &Qsort_Values_Callback
    );

    return OUT;
}

//
//  blockify: native [
//
//  "If a value isn't already a BLOCK!, enclose it in a block, else return it"
//
//      return: [block!]
//      value "VOID input will produce an empty block"
//          [~void~ element?]
//  ]
//
DECLARE_NATIVE(BLOCKIFY)
{
    INCLUDE_PARAMS_OF_BLOCKIFY;

    Value* v = ARG(VALUE);
    if (Is_Block(v))
        return COPY(v);

    Source* a = Make_Source_Managed(1);

    if (Is_Void(v)) {
        // leave empty
    } else {
        Set_Flex_Len(a, 1);
        Copy_Cell(Array_Head(a), cast(Element*, v));
    }
    return Init_Block(OUT, Freeze_Source_Shallow(a));
}


//
//  groupify: native [
//
//  "If a value isn't already a GROUP!, enclose it in a group, else return it"
//
//      return: [group!]
//      value "VOID input will produce an empty group"
//          [~void~ element?]
//  ]
//
DECLARE_NATIVE(GROUPIFY)
{
    INCLUDE_PARAMS_OF_GROUPIFY;

    Value* v = ARG(VALUE);
    if (Is_Group(v))
        return COPY(v);

    Source* a = Make_Source_Managed(1);

    if (Is_Void(v)) {
        // leave empty
    } else {
        Set_Flex_Len(a, 1);
        Copy_Cell(Array_Head(a), cast(Element*, v));
    }
    return Init_Group(OUT, Freeze_Source_Shallow(a));
}


//
//  envelop: native [
//
//  "Enclose element(s) in arbitrarily deep list structures"
//
//      return: [any-list?]
//      example "Example's binding (or lack of) will be used"
//          [datatype! any-list?]
//      content "Void input is treated the same as an empty splice"
//          [~void~ element? splice!]
//  ]
//
DECLARE_NATIVE(ENVELOP)
//
// Prototyped using API calls.  Improve performance once it's hammered out.
{
    INCLUDE_PARAMS_OF_ENVELOP;

    Value* example = ARG(EXAMPLE);
    Value* content = ARG(CONTENT);

    Element* copy;

    if (Is_Datatype(example)) {
        if (not Any_List_Type(Cell_Datatype_Type(example)))
            return FAIL("If ENVELOP example is datatype, must be a list type");

        copy = Known_Element(rebValue(CANON(MAKE), ARG(EXAMPLE), rebI(1)));
    }
    else
        copy = Known_Element(rebValue("copy:deep", rebQ(ARG(EXAMPLE))));

    Length len;
    if (
        Is_Void(content)
        or (Is_Splice(content) and (Cell_List_Len_At(&len, content), len == 0))
    ){
        return copy;
    }

    Element* temp = copy;
    while (true) {
        const Element* tail;
        Element* at = Cell_List_At_Known_Mutable(&tail, temp);
        if (at == tail) {  // empty list, just append
            rebElide(CANON(APPEND), rebQ(temp), rebQ(content));
            return copy;
        }
        if (Any_List(at)) {  // content should be inserted deeper
            temp = at;
            continue;
        }
        VAL_INDEX_UNBOUNDED(temp) += 1;  // just skip first item
        rebElide(CANON(INSERT), rebQ(temp), rebQ(content));
        VAL_INDEX_UNBOUNDED(temp) -= 1;  // put back if copy = temp for head
        return copy;
    }
}


//
//  glom: native [
//
//  "Efficient destructive appending operation that will reuse appended memory"
//
//      return: [blank! block!]
//      accumulator [blank! block!]
//      result [~void~ element? splice!]
//  ]
//
DECLARE_NATIVE(GLOM)
//
// GLOM was designed to bubble up `pending` values (e.g. collected values) in
// UPARSE, which are lists...but often they will be empty.  So creating lots of
// empty blocks was undesirable.  So having the accumulators start at null
// and be willing to start by taking over a bubbled up BLOCK! was desirable.
//
// https://forum.rebol.info/t/efficient-consuming-append-like-operator-glom/1647
{
    INCLUDE_PARAMS_OF_GLOM;

    // NOTE: if the accumulator or result are blocks, there's no guarantee they
    // are at the head.  VAL_INDEX() might be nonzero.  GLOM could prohibit
    // that or just take advantage of it if it's expedient (e.g. avoid a
    // resize by moving the data within an array and returning a 0 index).

    Value* accumulator = ARG(ACCUMULATOR);
    Value* result = ARG(RESULT);

    // !!! This logic is repeated in APPEND etc.  It should be factored out.
    //
    bool splice = false;

    if (Is_Void(result))
        return COPY(accumulator);

    if (Is_Splice(result)) {
        splice = true;
        assert(HEART_BYTE(result) == TYPE_GROUP);
        HEART_BYTE(result) = TYPE_BLOCK;  // interface is for blocks
        QUOTE_BYTE(result) = NOQUOTE_1;
    }

    if (Is_Blank(accumulator)) {
        if (splice)  // it was a non-quoted block initially
            return COPY(result);  // see note: index may be nonzero

        Source* a = Make_Source_Managed(1);
        Set_Flex_Len(a, 1);
        Copy_Cell(Array_Head(a), cast(Element*, result));  // not void / splice
        return Init_Block(OUT, a);
    }

    assert(Is_Block(accumulator));
    Source* a = Cell_Array_Ensure_Mutable(accumulator);

    if (not splice) {
        //
        // Here we are just appending one item.  We don't do anything special
        // at this time, but we should be willing to return VAL_INDEX()=0 and
        // reclaim any bias or space at the head vs. doing an expansion.  In
        // practice all GLOM that exist for the moment will be working on
        // series that are at their head, so this won't help.
        //
        Copy_Cell(Alloc_Tail_Array(a), cast(Element*, result));
    }
    else {
        // We're appending multiple items from result.  But we want to avoid
        // allocating new arrays if at all possible...and we are fluidly
        // willing to promote the result array to be the accumulator if that
        // is necessary.
        //
        // But in the interests of time, just expand the target array for now
        // if necessary--work on other details later.
        //
        Array* r = Cell_Array_Ensure_Mutable(result);
        Length a_len = Array_Len(a);
        Length r_len = Array_Len(r);
        Expand_Flex_Tail(a, r_len);  // can move memory, get `at` after
        Element* dst = Array_At(a, a_len);  // old tail position
        Element* src = Array_Head(r);

        REBLEN index;
        for (index = 0; index < r_len; ++index, ++src, ++dst)
            Copy_Cell(dst, src);

        assert(Array_Len(a) == a_len + r_len);  // Expand_Flex_Tail sets

     #if DEBUG_POISON_FLEX_TAILS
        Term_Flex_If_Necessary(a);
     #endif

        // GLOM only works with mutable arrays, as part of its efficiency.  We
        // show a hint of the optimizations to come by decaying the incoming
        // result array (we might sporadically do it the other way just to
        // establish that the optimizations could obliterate either).
        //
        Diminish_Stub(r);
    }

    return COPY(accumulator);
}


#if RUNTIME_CHECKS

//
//  Assert_Array_Core: C
//
void Assert_Array_Core(const Array* a)
{
    Assert_Flex_Basics_Core(a);  // not marked free, etc.

    if (not Stub_Holds_Cells(a))
        panic (a);

    const Cell* item = Array_Head(a);
    Offset n;
    Length len = Array_Len(a);
    for (n = 0; n < len; ++n, ++item) {
        if (Stub_Flavor(a) == FLAVOR_DATASTACK) {
            if (Is_Cell_Poisoned(item))
                continue;  // poison okay in datastacks
        }
        if (Stub_Flavor(a) == FLAVOR_DETAILS) {
            if (not Is_Cell_Readable(item))
                continue;  // unreadable cells ok in details
        }

        Assert_Cell_Readable(item);
        if (HEART_BYTE(item) > MAX_HEART_BYTE) {
            printf("Invalid HEART_BYTE() at index %d\n", cast(int, n));
            panic (a);
        }
    }

    if (Get_Stub_Flag(a, DYNAMIC)) {
        Length rest = Flex_Rest(a);

      #if DEBUG_POISON_FLEX_TAILS
        assert(rest > 0 and rest > n);
        if (Not_Flex_Flag(a, FIXED_SIZE) and not Is_Cell_Poisoned(item))
            panic (item);
        ++item;
        rest = rest - 1;
      #endif

        for (; n < rest; ++n, ++item) {
            const bool unwritable = (
                (item->header.bits != CELL_MASK_ERASED_0)
                and not (item->header.bits & NODE_FLAG_CELL)
            );
            if (Get_Flex_Flag(a, FIXED_SIZE)) {
                if (not unwritable) {
                    printf("Writable cell found in fixed-size array rest\n");
                    panic (a);
                }
            }
            else {
                if (unwritable) {
                    printf("Unwritable cell found in array rest capacity\n");
                    panic (a);
                }
            }
        }
    }
}

#endif
