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


//
//  CT_List: C
//
// "Compare Type" dispatcher for ANY-BLOCK?, ANY-GROUP?
//
// !!! Should CT_Path() delegate to this when it detects it has two arrays
// to compare?  That requires canonization assurance.
//
REBINT CT_List(const Cell* a, const Cell* b, bool strict)
{
    const Array* a_array = Cell_Array(a);
    const Array* b_array = Cell_Array(b);
    REBLEN a_index = VAL_INDEX(a);
    REBLEN b_index = VAL_INDEX(b);

    if (a_array == b_array) {
        if (a_index == b_index)
            return 0;

        if (a_index > b_index)
            return 1;
        return -1;
    }

    const Element* a_tail = Array_Tail(a_array);
    const Element* b_tail = Array_Tail(b_array);
    const Element* a_item = Array_At(a_array, a_index);
    const Element* b_item = Array_At(b_array, b_index);

    if (a_item == a_tail or b_item == b_tail)
        goto diff_of_ends;

    while (
        VAL_TYPE(a_item) == VAL_TYPE(b_item)
        or (Any_Number(a_item) and Any_Number(b_item))
    ){
        REBINT diff;
        if ((diff = Cmp_Value(a_item, b_item, strict)) != 0)
            return diff;

        ++a_item;
        ++b_item;

        if (a_item == a_tail or b_item == b_tail)
            goto diff_of_ends;
    }

    return VAL_TYPE(a_item) > VAL_TYPE(b_item) ? 1 : -1;

  diff_of_ends:
    //
    // Treat end as if it were a REB_xxx type of 0, so all other types would
    // compare larger than it.
    //
    if (a_item == a_tail) {
        if (b_item == b_tail)
            return 0;
        return -1;
    }
    return 1;
}


//
//  MAKE_List: C
//
// "Make Type" dispatcher for the following subtypes:
//
//     MAKE_Block
//     MAKE_Group
//     MAKE_Path
//     MAKE_Set_Path
//     MAKE_Get_Path
//     MAKE_Lit_Path
//
Bounce MAKE_List(
    Level* level_,
    Kind k,
    Option(const Value*) parent,
    const Value* arg
){
    Heart heart = cast(Heart, k);
    assert(Any_List_Kind(heart));

    if (parent)
        return RAISE(Error_Bad_Make_Parent(heart, unwrap parent));

    if (Is_Integer(arg) or Is_Decimal(arg)) {
        //
        // `make block! 10` => creates array with certain initial capacity
        //
        return Init_Any_List(OUT, heart, Make_Array(Int32s(arg, 0)));
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
    else if (Any_List(arg)) {
        //
        // !!! Ren-C unified MAKE and construction syntax, see #2263.  This is
        // now a questionable idea, as MAKE and TO have their roles defined
        // with more clarity (e.g. MAKE is allowed to throw and run arbitrary
        // code, while TO is not, so MAKE seems bad to run while scanning.)
        //
        // However, the idea was that if MAKE of a BLOCK! via a definition
        // itself was a block, then the block would have 2 elements in it,
        // with one existing array and an index into that array:
        //
        //     >> p1: #[path! [[a b c] 2]]
        //     == b/c
        //
        //     >> head p1
        //     == a/b/c
        //
        //     >> block: [a b c]
        //     >> p2: make path! compose [((block)) 2]
        //     == b/c
        //
        //     >> append block 'd
        //     == [a b c d]
        //
        //     >> p2
        //     == b/c/d
        //
        // !!! This could be eased to not require the index, but without it
        // then it can be somewhat confusing as to why [[a b c]] is needed
        // instead of just [a b c] as the construction spec.
        //
        REBLEN len;
        const Element* at = Cell_List_Len_At(&len, arg);

        if (len != 2 or not Any_List(at) or not Is_Integer(at + 1))
            goto bad_make;

        const Cell* list = at;
        REBINT index = VAL_INDEX(list) + Int32(at + 1) - 1;

        if (index < 0 or index > Cell_Series_Len_Head(list))
            goto bad_make;

        // !!! Previously this code would clear line break options on path
        // elements, using `Clear_Cell_Flag(..., CELL_FLAG_LINE)`.  But if
        // lists are allowed to alias each others contents, the aliasing
        // via MAKE shouldn't modify the store.  Line marker filtering out of
        // paths should be part of the MOLDing logic -or- a path with embedded
        // line markers should use construction syntax to preserve them.

        Context* derived = Derive_Binding(Cell_List_Binding(arg), list);
        return Init_Series_At_Core(
            OUT,
            heart,
            Cell_Array(list),
            index,
            derived
        );
    }
    else if (Any_List(arg)) {
        //
        // `to group! [1 2 3]` etc. -- copy the array data at the index
        // position and change the type.  (Note: MAKE does not copy the
        // data, but aliases it under a new kind.)
        //
        REBLEN len;
        const Element* at = Cell_List_Len_At(&len, arg);
        return Init_Any_List(
            OUT,
            heart,
            Copy_Values_Len_Shallow(at, len)
        );
    }
    else if (Is_Text(arg)) {
        //
        // `to block! "some string"` historically runs TRANSCODE, so you
        // get unbound code.
        //
        Size utf8_size;
        Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, arg);
        Option(const String*) file = ANONYMOUS;
        return Init_Any_List(
            OUT,
            heart,
            Scan_UTF8_Managed(file, utf8, utf8_size)
        );
    }
    else if (Is_Binary(arg)) {
        //
        // `to block! #{00BDAE....}` assumes the binary data is UTF8, and
        // uses TRANSCODE to make unbound code.
        //
        Option(const String*) file = ANONYMOUS;

        Size size;
        const Byte* at = Cell_Binary_Size_At(&size, arg);
        return Init_Any_List(
            OUT,
            heart,
            Scan_UTF8_Managed(file, at, size)
        );
    }
    else if (Is_Map(arg)) {
        return Init_Any_List(OUT, heart, Map_To_Array(VAL_MAP(arg), 0));
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
        return Init_Any_List(OUT, heart, Pop_Stack_Values(base));
    }
    else if (Any_Context(arg)) {
        return Init_Any_List(OUT, heart, Context_To_Array(arg, 3));
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
            assert(not Is_Stub_Varlist(Cell_Varargs_Source(arg)));
        }
        else {
            VarList* context = cast(VarList*, Cell_Varargs_Source(arg));
            Level* param_level = Level_Of_Varlist_May_Fail(context);

            Value* param = Array_Head(
                Varlist_Array(ACT_EXEMPLAR(Level_Phase(param_level)))
            );
            if (VAL_VARARGS_SIGNED_PARAM_INDEX(arg) < 0)
                param += - VAL_VARARGS_SIGNED_PARAM_INDEX(arg);
            else
                param += VAL_VARARGS_SIGNED_PARAM_INDEX(arg);

            if (Typecheck_Atom(param, Lib(NULL)))
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

        return Init_Any_List(OUT, heart, Pop_Stack_Values(base));
    }

  bad_make:

    return RAISE(Error_Bad_Make(heart, arg));
}


//
//  TO_List: C
//
Bounce TO_List(Level* level_, Kind k, const Value* arg) {
    Heart heart = cast(Heart, k);

    if (Any_Sequence(arg)) {
        StackIndex base = TOP_INDEX;
        Length len = Cell_Sequence_Len(arg);
        REBLEN i;
        for (i = 0; i < len; ++i) {
            Derelativize_Sequence_At(
                PUSH(),
                c_cast(Element*, arg),
                Cell_Sequence_Binding(arg),
                i
            );
        }
        return Init_Any_List(OUT, heart, Pop_Stack_Values(base));
    }
    else if (Any_List(arg)) {
        Length len;
        const Element* at = Cell_List_Len_At(&len, arg);
        return Init_Any_List(
            OUT,
            heart,
            Copy_Values_Len_Shallow(at, len)
        );
    }
    else {
        // !!! Review handling of making a 1-element PATH!, e.g. TO PATH! 10
        //
        Array* single = Alloc_Singular(NODE_FLAG_MANAGED);
        Copy_Cell(Stub_Cell(single), arg);
        return Init_Any_List(OUT, heart, single);
    }
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
        --index;  // (find:skip tail [1 2] 2 -1) should start at the *2*
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
                    0 != Cmp_Value(
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
        fail ("Only Antiforms Supported by FIND are MATCHES and SPREAD");

    if (Any_Type_Value(pattern) and not (flags & AM_FIND_CASE))
        fail (
            "FIND without :CASE temporarily not taking TYPE-XXX! use MATCHES"
            " see https://forum.rebol.info/t/1881"
        );

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
                        and VAL_TYPE(item) == VAL_TYPE(pattern)
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
        if (0 == Cmp_Value(
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


struct sort_flags {
    bool cased;
    bool reverse;
    REBLEN offset;
    Value* comparator;
    bool all; // !!! not used?
};


//
//  Compare_Val: C
//
static int Compare_Val(void *arg, const void *v1, const void *v2)
{
    struct sort_flags *flags = cast(struct sort_flags*, arg);

    // !!!! BE SURE that 64 bit large difference comparisons work

    if (flags->reverse)
        return Cmp_Value(
            c_cast(Value*, v2) + flags->offset,
            c_cast(Value*, v1) + flags->offset,
            flags->cased
        );
    else
        return Cmp_Value(
            c_cast(Value*, v1) + flags->offset,
            c_cast(Value*, v2) + flags->offset,
            flags->cased
        );
}


//
//  Compare_Val_Custom: C
//
static int Compare_Val_Custom(void *arg, const void *v1, const void *v2)
{
    struct sort_flags *flags = cast(struct sort_flags*, arg);

    DECLARE_VALUE (result);
    if (rebRunThrows(
        result,  // <-- output cell
        rebRUN(flags->comparator),
            flags->reverse ? c_cast(Value*, v1) : c_cast(Value*, v2),
            flags->reverse ? c_cast(Value*, v2) : c_cast(Value*, v1)
    )){
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));
    }

    REBINT tristate = -1;

    if (Is_Logic(result)) {
        if (Cell_Logic(result))
            tristate = 1;
    }
    else if (Is_Integer(result)) {
        if (VAL_INT64(result) > 0)
            tristate = 1;
        else if (VAL_INT64(result) == 0)
            tristate = 0;
    }
    else if (Is_Decimal(result)) {
        if (VAL_DECIMAL(result) > 0)
            tristate = 1;
        else if (VAL_DECIMAL(result) == 0)
            tristate = 0;
    }
    else if (Is_Trigger(result))
        tristate = 1;

    return tristate;
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
    const Value* v,
    const Value* picker
){
    REBINT n;

    if (Is_Integer(picker) or Is_Decimal(picker)) { // #2312
        n = Int32(picker);
        if (n == 0)
            return -1;  // Rebol2/Red convention: 0 is not a pick
        if (n < 0)
            ++n; // Rebol2/Red convention: `pick tail [a b c] -1` is `c`
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
    else if (Is_Logic(picker)) {
        //
        // !!! PICK in R3-Alpha historically would use a logic TRUE to get
        // the first element in a list, and a logic FALSE to get the second.
        // It did this regardless of how many elements were in the list.
        // (For safety, it has been suggested lists > length 2 should fail).
        //
        if (Cell_Logic(picker))
            n = VAL_INDEX(v);
        else
            n = VAL_INDEX(v) + 1;
    }
    else if (Is_Antiform(picker)) {
        fail (Error_Bad_Antiform(picker));
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
    Sink(Value) out,
    const Value* block,
    const Value* picker
){
    REBINT n = Get_Num_From_Arg(picker);
    n += VAL_INDEX(block) - 1;
    if (n < 0 or n >= Cell_Series_Len_Head(block))
        return false;

    const Element* slot = Array_At(Cell_Array(block), n);
    Copy_Cell(out, slot);
    return true;
}


//
//  MF_List: C
//
void MF_List(Molder* mo, const Cell* v, bool form)
{
    Heart heart = Cell_Heart(v);  // may be quoted, but mold renders the quotes

    if (form) {
        Option(VarList*) context = nullptr;
        bool relax = false;
        Form_Array_At(mo, Cell_Array(v), VAL_INDEX(v), context, relax);
        return;
    }

    assert(VAL_INDEX(v) <= Cell_Series_Len_Head(v));

    const char *sep;

    switch (heart) {
      case REB_META_BLOCK:
        Append_Codepoint(mo->string, '^');
        goto block;

      case REB_THE_BLOCK:
        Append_Codepoint(mo->string, '@');
        goto block;

      case REB_TYPE_BLOCK:
        Append_Codepoint(mo->string, '&');
        goto block;

      case REB_VAR_BLOCK:
        Append_Codepoint(mo->string, '$');
        goto block;

      case REB_BLOCK:
        block:
        sep = "[]";
        break;

      case REB_META_GROUP:
        Append_Codepoint(mo->string, '^');
        goto group;

      case REB_THE_GROUP:
        Append_Codepoint(mo->string, '@');
        goto group;

      case REB_TYPE_GROUP:
        Append_Codepoint(mo->string, '&');
        goto group;

      case REB_VAR_GROUP:
        Append_Codepoint(mo->string, '$');
        goto group;

      case REB_GROUP:
      group:
        if (GET_MOLD_FLAG(mo, MOLD_FLAG_SPREAD)) {
            CLEAR_MOLD_FLAG(mo, MOLD_FLAG_SPREAD);  // only top level
            sep = "\000\000";
        }
        else
            sep = "()";
        break;

      default:
        panic ("Unknown list heart passed to MF_List");
    }

    Mold_Array_At(mo, Cell_Array(v), VAL_INDEX(v), sep);
}


//
//  REBTYPE: C
//
// Implementation of type dispatch for ANY-LIST? (ANY-BLOCK? and ANY-GROUP?)
//
REBTYPE(List)
{
    Value* list = D_ARG(1);

    Context* binding = Cell_List_Binding(list);

    Option(SymId) id = Symbol_Id(verb);

    switch (id) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));
        UNUSED(ARG(property));

        return Series_Common_Action_Maybe_Unhandled(level_, verb); }

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        REBINT n = Try_Get_Array_Index_From_Picker(list, picker);
        if (n < 0 or n >= Cell_Series_Len_Head(list))
            return RAISE(Error_Bad_Pick_Raw(picker));

        const Element* at = Array_At(Cell_Array(list), n);

        Copy_Cell(OUT, at);
        Inherit_Const(stable_OUT, list);
        return OUT; }


    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE_P: {
        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);

        const Value* setval = ARG(value);

        if (Is_Nulled(setval))
            fail (Error_Need_Non_Null_Raw());  // also can't put in blocks

        if (Is_Antiform(setval))
            fail (Error_Bad_Antiform(setval));  // can't put in blocks

        // !!! If we are jumping here from getting updated bits, then
        // if the block isn't immutable or locked from modification, the
        // memory may have moved!  There's no way to guarantee semantics
        // of an update if we don't lock the array for the poke duration.
        //
        REBINT n = Try_Get_Array_Index_From_Picker(list, picker);
        if (n < 0 or n >= Cell_Series_Len_Head(list))
            fail (Error_Out_Of_Range(picker));

        Array* mut_arr = Cell_Array_Ensure_Mutable(list);
        Element* at = Array_At(mut_arr, n);
        Copy_Cell(at, c_cast(Element*, setval));

        return nullptr; }  // Array* is still fine, caller need not update


      case SYM_UNIQUE:
      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE:
      case SYM_EXCLUDE:
        //
      case SYM_SKIP:
      case SYM_AT:
      case SYM_REMOVE:
        return Series_Common_Action_Maybe_Unhandled(level_, verb);

      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        UNUSED(PARAM(series));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        Array* arr = Cell_Array_Ensure_Mutable(list);

        REBLEN len;
        if (REF(part)) {
            len = Part_Len_May_Modify_Index(list, ARG(part));
            if (len == 0)
                return Init_Any_List(OUT, Cell_Heart(list), Make_Array(0));
        }
        else
            len = 1;

        REBLEN index = VAL_INDEX(list); // Partial() can change index

        if (REF(last))
            index = Cell_Series_Len_Head(list) - len;

        if (index >= Cell_Series_Len_Head(list)) {
            if (not REF(part))
                return RAISE(Error_Nothing_To_Take_Raw());

            return Init_Any_List(OUT, Cell_Heart(list), Make_Array(0));
        }

        if (REF(part))
            Init_Any_List(
                OUT,
                Cell_Heart(list),
                Copy_Array_At_Max_Shallow(arr, index, len)
            );
        else
            Derelativize(OUT, &Array_Head(arr)[index], binding);

        Remove_Flex_Units(arr, index, len);
        return OUT; }

    //-- Search:

      case SYM_FIND:
      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_FIND; // must be same as select
        UNUSED(PARAM(series));

        Value* pattern = ARG(pattern);

        if (Is_Void(pattern))
            return nullptr;  // VOID in, NULL out

        Flags flags = (
            (REF(match) ? AM_FIND_MATCH : 0)
            | (REF(case) ? AM_FIND_CASE : 0)
        );

        REBLEN limit = Part_Tail_May_Modify_Index(list, ARG(part));

        const Array* arr = Cell_Array(list);
        REBLEN index = VAL_INDEX(list);

        REBINT skip;
        if (REF(skip)) {
            skip = VAL_INT32(ARG(skip));
            if (skip == 0)
                fail (PARAM(skip));
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
            Array* pack = Make_Array_Core(2, NODE_FLAG_MANAGED);
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
        UNUSED(PARAM(series));

        Value* arg = ARG(value);
        assert(not Is_Nulled(arg));  // not ~null~ in typecheck

        REBLEN len; // length of target
        if (id == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(list, ARG(part));
        else
            len = Part_Limit_Append_Insert(ARG(part));

        // Note that while inserting or appending VOID is a no-op, CHANGE with
        // a :PART can actually erase data.
        //
        if (Is_Void(arg) and len == 0) {
            if (id == SYM_APPEND)  // append always returns head
                VAL_INDEX_RAW(list) = 0;
            return COPY(list);  // don't fail on read only if would be a no-op
        }

        Array* arr = Cell_Array_Ensure_Mutable(list);
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
        else if (Is_Antiform(arg))  // only SPLICE! in typecheck
            fail (Error_Bad_Antiform(arg));  // ...but that doesn't filter yet

        if (REF(part))
            flags |= AM_PART;
        if (REF(line))
            flags |= AM_LINE;

        VAL_INDEX_RAW(OUT) = Modify_Array(
            arr,
            index,
            unwrap id,
            arg,
            flags,
            len,
            REF(dup) ? Int32(ARG(dup)) : 1
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

    //-- Creation:

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PARAM(value));

        REBLEN tail = Part_Tail_May_Modify_Index(list, ARG(part));

        const Array* arr = Cell_Array(list);
        REBLEN index = VAL_INDEX(list);

        Flags flags = ARRAY_MASK_HAS_FILE_LINE;

        // We shouldn't be returning a const value from the copy, but if the
        // input value was const and we don't copy some types deeply, those
        // types should retain the constness intended for them.
        //
        flags |= (list->header.bits & ARRAY_FLAG_CONST_SHALLOW);

        Array* copy = Copy_Array_Core_Managed(
            arr,
            index, // at
            tail, // tail
            0, // extra
            flags, // flags
            did REF(deep)
        );

        Init_Any_List(OUT, Cell_Heart_Ensure_Noquote(list), copy);
        BINDING(OUT) = Cell_List_Binding(list);
        return OUT; }

    //-- Special actions:

      case SYM_SWAP: {
        Value* arg = D_ARG(2);
        if (not Any_List(arg))
            fail (arg);

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

      case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;
        UNUSED(ARG(series));  // covered by `v`

        Array* arr = Cell_Array_Ensure_Mutable(list);
        REBLEN index = VAL_INDEX(list);

        REBLEN len = Part_Len_May_Modify_Index(list, ARG(part));
        if (len == 0)
            return COPY(list); // !!! do 1-element reversals update newlines?

        Cell* front = Array_At(arr, index);
        Cell* back = front + len - 1;

        // We must reverse the sense of the newline markers as well, #2326
        // Elements that used to be the *end* of lines now *start* lines.
        // So really this just means taking newline pointers that were
        // on the next element and putting them on the previous element.

        bool line_back;
        if (back == Array_Last(arr)) // !!! review tail newline handling
            line_back = Get_Array_Flag(arr, NEWLINE_AT_TAIL);
        else
            line_back = Get_Cell_Flag(back + 1, NEWLINE_BEFORE);

        for (len /= 2; len > 0; --len, ++front, --back) {
            bool line_front = Get_Cell_Flag(front + 1, NEWLINE_BEFORE);

            Cell temp;
            temp.header = front->header;
            temp.extra = front->extra;
            temp.payload = front->payload;
          #if DEBUG_TRACK_EXTEND_CELLS
            temp.file = front->file;
            temp.line = front->line;
            temp.tick = front->tick;
            temp.touch = front->touch;
          #endif

            // When we move the back cell to the front position, it gets the
            // newline flag based on the flag state that was *after* it.
            //
            front->header = back->header;
            front->extra = back->extra;
            front->payload = back->payload;
          #if DEBUG_TRACK_EXTEND_CELLS
            front->file = back->file;
            front->line = back->line;
            front->tick = back->tick;
            front->touch = back->touch;
          #endif
            if (line_back)
                Set_Cell_Flag(front, NEWLINE_BEFORE);
            else
                Clear_Cell_Flag(front, NEWLINE_BEFORE);

            // We're pushing the back pointer toward the front, so the flag
            // that was on the back will be the after for the next blit.
            //
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

            if (line_front)
                Set_Cell_Flag(back, NEWLINE_BEFORE);
            else
                Clear_Cell_Flag(back, NEWLINE_BEFORE);
        }
        return COPY(list); }

      case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;
        UNUSED(PARAM(series));  // covered by `v`

        Array* arr = Cell_Array_Ensure_Mutable(list);

        struct sort_flags flags;
        flags.cased = REF(case);
        flags.reverse = REF(reverse);
        flags.all = REF(all);  // !!! not used?

        Value* cmp = ARG(compare);  // null if no :COMPARE
        Deactivate_If_Action(cmp);
        if (Is_Frame(cmp)) {
            flags.comparator = cmp;
            flags.offset = 0;
        }
        else if (Is_Integer(cmp)) {
            flags.comparator = nullptr;
            flags.offset = Int32(cmp) - 1;
        }
        else {
            assert(Is_Nulled(cmp));
            flags.comparator = nullptr;
            flags.offset = 0;
        }

        Copy_Cell(OUT, list);  // save list before messing with index

        REBLEN len = Part_Len_May_Modify_Index(list, ARG(part));
        if (len <= 1)
            return OUT;
        REBLEN index = VAL_INDEX(list);  // ^-- may have been modified

        // Skip factor:
        REBLEN skip;
        if (Is_Nulled(ARG(skip)))
            skip = 1;
        else {
            skip = Get_Num_From_Arg(ARG(skip));
            if (skip <= 0 or len % skip != 0 or skip > len)
                fail (Error_Out_Of_Range(ARG(skip)));
        }

        reb_qsort_r(
            Array_At(arr, index),
            len / skip,
            sizeof(Cell) * skip,
            &flags,
            flags.comparator != nullptr ? &Compare_Val_Custom : &Compare_Val
        );

        return OUT; }

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;
        UNUSED(PARAM(value));  // covered by `v`

        REBLEN index = VAL_INDEX(list);

        if (REF(seed))
            fail (Error_Bad_Refines_Raw());

        if (REF(only)) { // pick an element out of the list
            if (index >= Cell_Series_Len_Head(list))
                return nullptr;

            Init_Integer(
                ARG(seed),
                1 + (Random_Int(REF(secure))
                    % (Cell_Series_Len_Head(list) - index))
            );

            if (not Try_Pick_Block(OUT, list, ARG(seed)))
                return nullptr;
            return Inherit_Const(stable_OUT, list);
        }

        Array* arr = Cell_Array_Ensure_Mutable(list);
        Shuffle_Array(arr, VAL_INDEX(list), REF(secure));
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
        // are going to read the D_ARG(1) slot *implicitly* regardless of
        // what value points to.
        //
        const Value* made = rebValue("make port! @", D_ARG(1));
        assert(Is_Port(made));
        Copy_Cell(D_ARG(1), made);
        rebRelease(made);
        return BOUNCE_CONTINUE; }  // should dispatch to the PORT!

      default:
        break; // fallthrough to error
    }

    fail (UNHANDLED);
}


//
//  /blockify: native [
//
//  "If a value isn't already a BLOCK!, enclose it in a block, else return it"
//
//      return: [block!]
//      value "VOID input will produce an empty block"
//          [~void~ element?]
//  ]
//
DECLARE_NATIVE(blockify)
{
    INCLUDE_PARAMS_OF_BLOCKIFY;

    Value* v = ARG(value);
    if (Is_Block(v))
        return COPY(v);

    Array* a = Make_Array_Core(
        1,
        NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE
    );

    if (Is_Void(v)) {
        // leave empty
    } else {
        Set_Flex_Len(a, 1);
        Copy_Cell(Array_Head(a), cast(Element*, v));
    }
    return Init_Block(OUT, Freeze_Array_Shallow(a));
}


//
//  /groupify: native [
//
//  "If a value isn't already a GROUP!, enclose it in a group, else return it"
//
//      return: [group!]
//      value "VOID input will produce an empty group"
//          [~void~ element?]
//  ]
//
DECLARE_NATIVE(groupify)
{
    INCLUDE_PARAMS_OF_GROUPIFY;

    Value* v = ARG(value);
    if (Is_Group(v))
        return COPY(v);

    Array* a = Make_Array_Core(
        1,
        NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE
    );

    if (Is_Void(v)) {
        // leave empty
    } else {
        Set_Flex_Len(a, 1);
        Copy_Cell(Array_Head(a), cast(Element*, v));
    }
    return Init_Group(OUT, Freeze_Array_Shallow(a));
}


//
//  /enblock: native [
//
//  "Enclose a value in a BLOCK!, even if it's already a block"
//
//      return: [block!]
//      value "VOID input will produce an empty block"
//          [~void~ element?]
//  ]
//
DECLARE_NATIVE(enblock)
{
    INCLUDE_PARAMS_OF_ENBLOCK;

    Value* v = ARG(value);

    Array* a = Make_Array_Core(
        1,
        NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE
    );

    if (Is_Void(v)) {
        // leave empty
    } else {
        Set_Flex_Len(a, 1);
        Copy_Cell(Array_Head(a), cast(Element*, v));
    }
    return Init_Block(OUT, Freeze_Array_Shallow(a));
}


//
//  /engroup: native [
//
//  "Enclose a value in a GROUP!, even if it's already a group"
//
//      return: [group!]
//      value "VOID input will produce an empty group"
//          [~void~ element?]
//  ]
//
DECLARE_NATIVE(engroup)
{
    INCLUDE_PARAMS_OF_ENGROUP;

    Value* v = ARG(value);

    Array* a = Make_Array_Core(
        1,
        NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE
    );

    if (Is_Void(v)) {
        // leave empty
    } else {
        Set_Flex_Len(a, 1);
        Copy_Cell(Array_Head(a), cast(Element*, v));
    }
    return Init_Group(OUT, Freeze_Array_Shallow(a));
}


//
//  /glom: native [
//
//  "Efficient destructive appending operation that will reuse appended memory"
//
//      return: [blank! block!]
//      accumulator [blank! block!]
//      result [~void~ element? splice?]
//  ]
//
DECLARE_NATIVE(glom)
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

    Value* accumulator = ARG(accumulator);
    Value* result = ARG(result);

    // !!! This logic is repeated in APPEND etc.  It should be factored out.
    //
    bool splice = false;

    if (Is_Void(result))
        return COPY(accumulator);

    if (Is_Splice(result)) {
        splice = true;
        assert(HEART_BYTE(result) == REB_GROUP);
        HEART_BYTE(result) = REB_BLOCK;  // interface is for blocks
        QUOTE_BYTE(result) = NOQUOTE_1;
    }

    if (Is_Blank(accumulator)) {
        if (splice)  // it was a non-quoted block initially
            return COPY(result);  // see note: index may be nonzero

        Array* a = Make_Array_Core(1, NODE_FLAG_MANAGED);
        Set_Flex_Len(a, 1);
        Copy_Cell(Array_Head(a), cast(Element*, result));  // not void / splice
        return Init_Block(OUT, a);
    }

    assert(Is_Block(accumulator));
    Array* a = Cell_Array_Ensure_Mutable(accumulator);

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
        Decay_Flex(r);
    }

    return COPY(accumulator);
}


#if DEBUG

//
//  Assert_Array_Core: C
//
void Assert_Array_Core(const Array* a)
{
    assert(Stub_Flavor(a) != FLAVOR_DATASTACK);  // has special handling

    Assert_Flex_Basics_Core(a);  // not marked free, etc.

    if (not Is_Stub_Array(a))
        panic (a);

    const Cell* item = Array_Head(a);
    Offset n;
    Length len = Array_Len(a);
    for (n = 0; n < len; ++n, ++item) {
        Assert_Cell_Readable(item);
        if (HEART_BYTE(item) >= REB_MAX) {
            printf("Invalid HEART_BYTE() at index %d\n", cast(int, n));
            panic (a);
        }
    }

    if (Get_Flex_Flag(a, DYNAMIC)) {
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
                (item->header.bits != CELL_MASK_0)
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
