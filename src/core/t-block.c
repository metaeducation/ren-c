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
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  CT_List: C
//
// "Compare Type" dispatcher for the following types: (list here to help
// text searches)
//
//     CT_Block()
//     CT_Group()
//     CT_Path()
//     CT_Set_Path()
//     CT_Get_Path()
//     CT_Lit_Path()
//
REBINT CT_List(const Cell* a, const Cell* b, REBINT mode)
{
    REBINT num = Cmp_Array(a, b, mode == 1);
    if (mode >= 0)
        return (num == 0);
    if (mode == -1)
        return (num >= 0);
    return (num > 0);
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
REB_R MAKE_List(Value* out, enum Reb_Kind kind, const Value* arg) {
    if (Is_Integer(arg) or Is_Decimal(arg)) {
        //
        // `make block! 10` => creates array with certain initial capacity
        //
        return Init_Any_List(out, kind, Make_Array(Int32s(arg, 0)));
    }
    else if (Is_Text(arg)) {
        //
        // `make block! "a <b> #c"` => `[a <b> #c]`, scans as code (unbound)
        //
        // Until UTF-8 Everywhere, text must be converted to UTF-8 before
        // using it with the scanner.
        //
        REBSIZ offset;
        REBSIZ size;
        Blob* temp = Temp_UTF8_At_Managed(
            &offset, &size, arg, Cell_Series_Len_At(arg)
        );
        Push_GC_Guard(temp);
        Option(String*) filename = nullptr;
        Init_Any_List(
            out,
            kind,
            Scan_UTF8_Managed(filename, Blob_At(temp, offset), size)
        );
        Drop_GC_Guard(temp);
        return out;
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
        //     >> p2: make path! compose [(block) 2]
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
        if (
            VAL_ARRAY_LEN_AT(arg) != 2
            || !Any_List(Cell_List_At(arg))
            || !Is_Integer(Cell_List_At(arg) + 1)
        ) {
            goto bad_make;
        }

        Cell* any_array = Cell_List_At(arg);
        REBINT index = VAL_INDEX(any_array) + Int32(Cell_List_At(arg) + 1) - 1;

        if (index < 0 || index > cast(REBINT, VAL_LEN_HEAD(any_array)))
            goto bad_make;

        // !!! Previously this code would clear line break options on path
        // elements, using `CLEAR_VAL_FLAG(..., VALUE_FLAG_LINE)`.  But if
        // arrays are allowed to alias each others contents, the aliasing
        // via MAKE shouldn't modify the store.  Line marker filtering out of
        // paths should be part of the MOLDing logic -or- a path with embedded
        // line markers should use construction syntax to preserve them.

        Specifier* derived = Derive_Specifier(VAL_SPECIFIER(arg), any_array);
        return Init_Any_Series_At_Core(
            out,
            kind,
            Cell_Array(any_array),
            index,
            derived
        );
    }
    else if (Is_Typeset(arg)) {
        //
        // !!! Should MAKE GROUP! and MAKE PATH! from a TYPESET! work like
        // MAKE BLOCK! does?  Allow it for now.
        //
        return Init_Any_List(out, kind, Typeset_To_Array(arg));
    }
    else if (Is_Binary(arg)) {
        //
        // `to block! #{00BDAE....}` assumes the binary data is UTF8, and
        // goes directly to the scanner to make an unbound code array.
        //
        Option(String*) filename = nullptr;
        return Init_Any_List(
            out,
            kind,
            Scan_UTF8_Managed(filename, Cell_Binary_At(arg), Cell_Series_Len_At(arg))
        );
    }
    else if (Is_Map(arg)) {
        return Init_Any_List(out, kind, Map_To_Array(VAL_MAP(arg), 0));
    }
    else if (Any_Context(arg)) {
        return Init_Any_List(out, kind, Context_To_Array(VAL_CONTEXT(arg), 3));
    }
    else if (Is_Varargs(arg)) {
        //
        // Converting a VARARGS! to an ANY-ARRAY! involves spooling those
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
        if (not arg->payload.varargs.phase) {
            //
            // A vararg created from a block AND never passed as an argument
            // so no typeset or quoting settings available.  Can't produce
            // any voids, because the data source is a block.
            //
            assert(Not_Array_Flag(arg->extra.binding, IS_VARLIST));
        }
        else {
            REBCTX *context = CTX(arg->extra.binding);
            Level* param_level = CTX_LEVEL_MAY_FAIL(context);

            Value* param = ACT_PARAMS_HEAD(Level_Phase(param_level))
                + arg->payload.varargs.param_offset;

            if (TYPE_CHECK(param, REB_MAX_NULLED))
                fail (Error_Null_Vararg_Array_Raw());
        }

        StackIndex base = TOP_INDEX;

        do {
            if (Do_Vararg_Op_Maybe_End_Throws(
                out,
                arg,
                VARARG_OP_TAKE
            )){
                Drop_Data_Stack_To(base);
                return R_THROWN;
            }

            if (IS_END(out))
                break;

            Copy_Cell(PUSH(), out);
        } while (true);

        return Init_Any_List(out, kind, Pop_Stack_Values(base));
    }
    else if (Is_Action(arg)) {
        //
        // !!! Experimental behavior; if action can run as arity-0, then
        // invoke it so long as it doesn't return null, collecting values.
        //
        StackIndex base = TOP_INDEX;
        while (true) {
            Value* generated = rebValue(rebEval(arg));
            if (not generated)
                break;
            Copy_Cell(PUSH(), generated);
            rebRelease(generated);
        }
        return Init_Any_List(out, kind, Pop_Stack_Values(base));
    }

  bad_make:;
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_List: C
//
REB_R TO_List(Value* out, enum Reb_Kind kind, const Value* arg) {
    if (
        kind == VAL_TYPE(arg) // always act as COPY if types match
        or Splices_Into_Type_Without_Only(kind, arg) // see comments
    ){
        return Init_Any_List(
            out,
            kind,
            Copy_Values_Len_Shallow(
                Cell_List_At(arg), VAL_SPECIFIER(arg), VAL_ARRAY_LEN_AT(arg)
            )
        );
    }
    else {
        // !!! Review handling of making a 1-element PATH!, e.g. TO PATH! 10
        //
        Array* single = Alloc_Singular(NODE_FLAG_MANAGED);
        Copy_Cell(ARR_SINGLE(single), arg);
        return Init_Any_List(out, kind, single);
    }
}


//
//  Find_In_Array: C
//
// !!! Comment said "Final Parameters: tail - tail position, match - sequence,
// SELECT - (value that follows)".  It's not clear what this meant.
//
REBLEN Find_In_Array(
    Array* array,
    REBLEN index, // index to start search
    REBLEN end, // ending position
    const Cell* target,
    REBLEN len, // length of target
    REBFLGS flags, // see AM_FIND_XXX
    REBINT skip // skip factor
){
    REBLEN start = index;

    if (flags & (AM_FIND_REVERSE | AM_FIND_LAST)) {
        skip = -1;
        start = 0;
        if (flags & AM_FIND_LAST)
            index = end - len;
        else
            --index;
    }

    // Optimized find word in block
    //
    if (Any_Word(target)) {
        for (; index >= start && index < end; index += skip) {
            Cell* item = Array_At(array, index);
            Symbol* target_canon = VAL_WORD_CANON(target); // canonize once
            if (Any_Word(item)) {
                if (flags & AM_FIND_CASE) { // Must be same type and spelling
                    if (
                        Cell_Word_Symbol(item) == Cell_Word_Symbol(target)
                        && VAL_TYPE(item) == VAL_TYPE(target)
                    ){
                        return index;
                    }
                }
                else { // Can be different type or differently cased spelling
                    if (VAL_WORD_CANON(item) == target_canon)
                        return index;
                }
            }
            if (flags & AM_FIND_MATCH)
                break;
        }
        return NOT_FOUND;
    }

    // Match a block against a block
    //
    if (Any_List(target) and not (flags & AM_FIND_ONLY)) {
        for (; index >= start and index < end; index += skip) {
            Cell* item = Array_At(array, index);

            REBLEN count = 0;
            Cell* other = Cell_List_At(target);
            for (; NOT_END(other); ++other, ++item) {
                if (
                    IS_END(item) ||
                    0 != Cmp_Value(item, other, did (flags & AM_FIND_CASE))
                ){
                    break;
                }
                if (++count >= len)
                    return index;
            }
            if (flags & AM_FIND_MATCH)
                break;
        }
        return NOT_FOUND;
    }

    // Find a datatype in block
    //
    if (Is_Datatype(target) || Is_Typeset(target)) {
        for (; index >= start && index < end; index += skip) {
            Cell* item = Array_At(array, index);

            if (Is_Datatype(target)) {
                if (VAL_TYPE(item) == VAL_TYPE_KIND(target))
                    return index;
                if (
                    Is_Datatype(item)
                    && VAL_TYPE_KIND(item) == VAL_TYPE_KIND(target)
                ){
                    return index;
                }
            }
            else if (Is_Typeset(target)) {
                if (TYPE_CHECK(target, VAL_TYPE(item)))
                    return index;
                if (
                    Is_Datatype(item)
                    && TYPE_CHECK(target, VAL_TYPE_KIND(item))
                ){
                    return index;
                }
                if (Is_Typeset(item) && EQUAL_TYPESET(item, target))
                    return index;
            }
            if (flags & AM_FIND_MATCH)
                break;
        }
        return NOT_FOUND;
    }

    // All other cases

    for (; index >= start && index < end; index += skip) {
        Cell* item = Array_At(array, index);
        if (0 == Cmp_Value(item, target, did (flags & AM_FIND_CASE)))
            return index;

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
            cast(const Cell*, v2) + flags->offset,
            cast(const Cell*, v1) + flags->offset,
            flags->cased
        );
    else
        return Cmp_Value(
            cast(const Cell*, v1) + flags->offset,
            cast(const Cell*, v2) + flags->offset,
            flags->cased
        );
}


//
//  Compare_Val_Custom: C
//
static int Compare_Val_Custom(void *arg, const void *v1, const void *v2)
{
    struct sort_flags *flags = cast(struct sort_flags*, arg);

    const bool fully = true; // error if not all arguments consumed

    DECLARE_VALUE (result);
    if (Apply_Only_Throws(
        result,
        fully,
        flags->comparator,
        flags->reverse ? v1 : v2,
        flags->reverse ? v2 : v1,
        rebEND
    )) {
        fail (Error_No_Catch_For_Throw(result));
    }

    REBINT tristate = -1;

    if (Is_Logic(result)) {
        if (VAL_LOGIC(result))
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
    else if (IS_TRUTHY(result))
        tristate = 1;

    return tristate;
}


//
//  Sort_List: C
//
// series [any-series!]
// /case {Case sensitive sort}
// /skip {Treat the series as records of fixed size}
// size [integer!] {Size of each record}
// /compare  {Comparator offset, block or action}
// comparator [integer! block! action!]
// /part {Sort only part of a series}
// limit [any-number! any-series!] {Length of series to sort}
// /all {Compare all fields}
// /reverse {Reverse sort order}
//
static void Sort_List(
    Value* block,
    bool ccase,
    Value* skipv,
    Value* compv,
    Value* part,
    bool all,
    bool rev
) {
    struct sort_flags flags;
    flags.cased = ccase;
    flags.reverse = rev;
    flags.all = all; // !!! not used?

    if (Is_Action(compv)) {
        flags.comparator = compv;
        flags.offset = 0;
    }
    else if (Is_Integer(compv)) {
        flags.comparator = nullptr;
        flags.offset = Int32(compv) - 1;
    }
    else {
        assert(Is_Nulled(compv));
        flags.comparator = nullptr;
        flags.offset = 0;
    }

    REBLEN len = Part_Len_May_Modify_Index(block, part); // length of sort
    if (len <= 1)
        return;

    // Skip factor:
    REBLEN skip;
    if (not Is_Nulled(skipv)) {
        skip = Get_Num_From_Arg(skipv);
        if (skip <= 0 || len % skip != 0 || skip > len)
            fail (Error_Out_Of_Range(skipv));
    }
    else
        skip = 1;

    reb_qsort_r(
        Cell_List_At(block),
        len / skip,
        sizeof(Cell) * skip,
        &flags,
        flags.comparator != nullptr ? &Compare_Val_Custom : &Compare_Val
    );
}


//
//  Shuffle_List: C
//
void Shuffle_List(Value* value, bool secure)
{
    REBLEN n;
    REBLEN k;
    REBLEN idx = VAL_INDEX(value);
    Cell* data = VAL_ARRAY_HEAD(value);

    // Rare case where Cell bit copying is okay...between spots in the
    // same array.
    //
    Cell swap;

    for (n = Cell_Series_Len_At(value); n > 1;) {
        k = idx + (REBLEN)Random_Int(secure) % n;
        n--;

        // Only do the following block when an actual swap occurs.
        // Otherwise an assertion will fail when trying to Blit_Cell() a
    // value to itself.
        if (k != (n + idx)) {
            swap.header = data[k].header;
            swap.payload = data[k].payload;
            swap.extra = data[k].extra;
            Blit_Cell(&data[k], &data[n + idx]);
            Blit_Cell(&data[n + idx], &swap);
    }
    }
}


//
//  PD_List: C
//
// Path dispatch for the following types:
//
//     PD_Block
//     PD_Group
//     PD_Path
//     PD_Get_Path
//     PD_Set_Path
//     PD_Lit_Path
//
REB_R PD_List(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    REBINT n;

    if (Is_Integer(picker) or Is_Decimal(picker)) { // #2312
        n = Int32(picker);
        if (n == 0)
            return nullptr; // Rebol2/Red convention: 0 is not a pick
        if (n < 0)
            ++n; // Rebol2/Red convention: `pick tail [a b c] -1` is `c`
        n += VAL_INDEX(pvs->out) - 1;
    }
    else if (Is_Word(picker)) {
        //
        // Linear search to case-insensitive find ANY-WORD! matching the canon
        // and return the item after it.  Default to out of range.
        //
        n = -1;

        Symbol* canon = VAL_WORD_CANON(picker);
        Cell* item = Cell_List_At(pvs->out);
        REBLEN index = VAL_INDEX(pvs->out);
        for (; NOT_END(item); ++item, ++index) {
            if (Any_Word(item) && canon == VAL_WORD_CANON(item)) {
                n = index + 1;
                break;
            }
        }
    }
    else if (Is_Logic(picker)) {
        //
        // !!! PICK in R3-Alpha historically would use a logic TRUE to get
        // the first element in an array, and a logic FALSE to get the second.
        // It did this regardless of how many elements were in the array.
        // (For safety, it has been suggested arrays > length 2 should fail).
        //
        if (VAL_LOGIC(picker))
            n = VAL_INDEX(pvs->out);
        else
            n = VAL_INDEX(pvs->out) + 1;
    }
    else {
        // For other values, act like a SELECT and give the following item.
        // (Note Find_In_Array_Simple returns the array length if missed,
        // so adding one will be out of bounds.)

        n = 1 + Find_In_Array_Simple(
            Cell_Array(pvs->out),
            VAL_INDEX(pvs->out),
            picker
        );
    }

    if (n < 0 or n >= cast(REBINT, VAL_LEN_HEAD(pvs->out))) {
        if (opt_setval)
            return R_UNHANDLED;

        return nullptr;
    }

    if (opt_setval)
        Fail_If_Read_Only_Flex(Cell_Flex(pvs->out));

    pvs->u.ref.cell = Cell_List_At_Head(pvs->out, n);
    pvs->u.ref.specifier = VAL_SPECIFIER(pvs->out);
    return R_REFERENCE;
}


//
//  Pick_Block: C
//
// Fills out with void if no pick.
//
Cell* Pick_Block(Value* out, const Value* block, const Value* picker)
{
    REBINT n = Get_Num_From_Arg(picker);
    n += VAL_INDEX(block) - 1;
    if (n < 0 || cast(REBLEN, n) >= VAL_LEN_HEAD(block)) {
        Init_Nulled(out);
        return nullptr;
    }

    Cell* slot = Cell_List_At_Head(block, n);
    Derelativize(out, slot, VAL_SPECIFIER(block));
    return slot;
}


//
//  MF_List: C
//
void MF_List(REB_MOLD *mo, const Cell* v, bool form)
{
    if (form && (Is_Block(v) || Is_Group(v))) {
        Form_Array_At(mo, Cell_Array(v), VAL_INDEX(v), 0);
        return;
    }

    bool all;
    if (VAL_INDEX(v) == 0) { // "&& VAL_TYPE(v) <= REB_LIT_PATH" commented out
        //
        // Optimize when no index needed
        //
        all = false;
    }
    else
        all = GET_MOLD_FLAG(mo, MOLD_FLAG_ALL);

    assert(VAL_INDEX(v) <= VAL_LEN_HEAD(v));

    if (all) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_ALL);
        Pre_Mold(mo, v); // #[block! part

        Append_Utf8_Codepoint(mo->series, '[');
        Mold_Array_At(mo, Cell_Array(v), 0, "[]");
        Post_Mold(mo, v);
        Append_Utf8_Codepoint(mo->series, ']');
    }
    else {
        const char *sep;

        enum Reb_Kind kind = VAL_TYPE(v);
        switch(kind) {
        case REB_BLOCK:
            if (GET_MOLD_FLAG(mo, MOLD_FLAG_ONLY)) {
                CLEAR_MOLD_FLAG(mo, MOLD_FLAG_ONLY); // only top level
                sep = "\000\000";
            }
            else
                sep = "[]";
            break;

        case REB_GROUP:
            sep = "()";
            break;

        case REB_GET_PATH:
            Append_Utf8_Codepoint(mo->series, ':');
            sep = "/";
            break;

        case REB_LIT_PATH:
            Append_Utf8_Codepoint(mo->series, '\'');
            // fall through
        case REB_PATH:
        case REB_SET_PATH:
            sep = "/";
            break;

        default:
            sep = nullptr;
        }

        if (Cell_Series_Len_At(v) == 0 and sep[0] == '/')
            Append_Utf8_Codepoint(mo->series, '/'); // 0-arity path is `/`
        else {
            Mold_Array_At(mo, Cell_Array(v), VAL_INDEX(v), sep);
            if (Cell_Series_Len_At(v) == 1 and sep [0] == '/')
                Append_Utf8_Codepoint(mo->series, '/'); // 1-arity path `foo/`
        }

        if (VAL_TYPE(v) == REB_SET_PATH)
            Append_Utf8_Codepoint(mo->series, ':');
    }
}


//
//  REBTYPE: C
//
// Implementation of type dispatch of the following:
//
//     REBTYPE(Block)
//     REBTYPE(Group)
//     REBTYPE(Path)
//     REBTYPE(Get_Path)
//     REBTYPE(Set_Path)
//     REBTYPE(Lit_Path)
//
REBTYPE(List)
{
    Value* list = D_ARG(1);
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    // Common operations for any series type (length, head, etc.)
    //
    REB_R r = Series_Common_Action_Maybe_Unhandled(level_, verb);
    if (r != R_UNHANDLED)
        return r;

    Array* arr = Cell_Array(list);
    Specifier* specifier = VAL_SPECIFIER(list);

    Option(SymId) sym = Cell_Word_Id(verb);
    switch (sym) {
      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        UNUSED(PAR(series));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        Fail_If_Read_Only_Flex(arr);

        REBLEN len;
        if (REF(part)) {
            len = Part_Len_May_Modify_Index(list, ARG(limit));
            if (len == 0)
                return Init_Block(OUT, Make_Array(0)); // new empty block
        }
        else
            len = 1;

        REBLEN index = VAL_INDEX(list); // Partial() can change index

        if (REF(last))
            index = VAL_LEN_HEAD(list) - len;

        if (index >= VAL_LEN_HEAD(list)) {
            if (not REF(part))
                return nullptr;

            return Init_Block(OUT, Make_Array(0)); // new empty block
        }

        if (REF(part))
            Init_Block(
                OUT, Copy_Array_At_Max_Shallow(arr, index, specifier, len)
            );
        else
            Derelativize(OUT, &Array_Head(arr)[index], specifier);

        Remove_Flex(arr, index, len);
        return OUT; }

    //-- Search:

      case SYM_FIND:
      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_FIND; // must be same as select

        UNUSED(PAR(series));
        UNUSED(PAR(value)); // aliased as arg

        REBINT len = Any_List(arg) ? VAL_ARRAY_LEN_AT(arg) : 1;

        REBLEN limit = Part_Tail_May_Modify_Index(list, ARG(limit));
        UNUSED(REF(part)); // checked by if limit is nulled

        REBLEN index = VAL_INDEX(list);

        REBFLGS flags = (
            (REF(only) ? AM_FIND_ONLY : 0)
            | (REF(match) ? AM_FIND_MATCH : 0)
            | (REF(reverse) ? AM_FIND_REVERSE : 0)
            | (REF(case) ? AM_FIND_CASE : 0)
            | (REF(last) ? AM_FIND_LAST : 0)
        );

        REBLEN skip = REF(skip) ? Int32s(ARG(size), 1) : 1;

        REBLEN ret = Find_In_Array(
            arr, index, limit, arg, len, flags, skip
        );

        if (ret >= limit)
            return nullptr;

        if (REF(only))
            len = 1;

        if (Cell_Word_Id(verb) == SYM_FIND) {
            if (REF(tail) || REF(match))
                ret += len;
            VAL_INDEX(list) = ret;
            Copy_Cell(OUT, list);
        }
        else {
            ret += len;
            if (ret >= limit)
                return nullptr;

            Derelativize(OUT, Array_At(arr, ret), specifier);
        }
        return OUT; }

    //-- Modification:
      case SYM_APPEND:
      case SYM_INSERT:
      case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;

        UNUSED(PAR(series));
        UNUSED(PAR(value));

        REBLEN len; // length of target
        if (Cell_Word_Id(verb) == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(list, ARG(limit));
        else
            len = Part_Len_Append_Insert_May_Modify_Index(arg, ARG(limit));

        // Note that while inserting or removing NULL is a no-op, CHANGE with
        // a /PART can actually erase data.
        //
        if (Is_Nulled(arg) and len == 0) { // only nulls bypass write attempts
            if (sym == SYM_APPEND) // append always returns head
                VAL_INDEX(list) = 0;
            RETURN (list); // don't fail on read only if it would be a no-op
        }
        Fail_If_Read_Only_Flex(arr);

        REBLEN index = VAL_INDEX(list);

        REBFLGS flags = 0;
        if (
            not REF(only)
            and Splices_Into_Type_Without_Only(VAL_TYPE(list), arg)
        ){
            flags |= AM_SPLICE;
        }
        if (REF(part))
            flags |= AM_PART;
        if (REF(line))
            flags |= AM_LINE;

        Copy_Cell(OUT, list);
        VAL_INDEX(OUT) = Modify_Array(
            unwrap(Cell_Word_Id(verb)),
            arr,
            index,
            arg,
            flags,
            len,
            REF(dup) ? Int32(ARG(count)) : 1
        );
        return OUT; }

      case SYM_CLEAR: {
        Fail_If_Read_Only_Flex(arr);
        REBLEN index = VAL_INDEX(list);
        if (index < VAL_LEN_HEAD(list)) {
            if (index == 0) Reset_Array(arr);
            else {
                SET_END(Array_At(arr, index));
                Set_Flex_Len(Cell_Flex(list), cast(REBLEN, index));
            }
        }
        RETURN (list);
    }

    //-- Creation:

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        REBU64 types = 0;
        REBLEN tail = Part_Tail_May_Modify_Index(list, ARG(limit));
        UNUSED(REF(part));

        REBLEN index = VAL_INDEX(list);

        if (REF(deep))
            types |= REF(types) ? 0 : TS_STD_SERIES;

        if (REF(types)) {
            if (Is_Datatype(ARG(kinds)))
                types |= FLAGIT_KIND(VAL_TYPE(ARG(kinds)));
            else
                types |= VAL_TYPESET_BITS(ARG(kinds));
        }

        Array* copy = Copy_Array_Core_Managed(
            arr,
            index, // at
            specifier,
            tail, // tail
            0, // extra
            ARRAY_FLAG_HAS_FILE_LINE, // flags
            types // types to copy deeply
        );
        return Init_Any_List(OUT, VAL_TYPE(list), copy);
    }

    //-- Special actions:

    case SYM_SWAP: {
        if (not Any_List(arg))
            fail (Error_Invalid(arg));

        Fail_If_Read_Only_Flex(arr);
        Fail_If_Read_Only_Flex(Cell_Array(arg));

        REBLEN index = VAL_INDEX(list);

        if (
            index < VAL_LEN_HEAD(list)
            && VAL_INDEX(arg) < VAL_LEN_HEAD(arg)
        ){
            // Cell bits can be copied within the same array
            //
            Cell* a = Cell_List_At(list);
            Cell temp;
            temp.header = a->header;
            temp.payload = a->payload;
            temp.extra = a->extra;
            Blit_Cell(Cell_List_At(list), Cell_List_At(arg));
            Blit_Cell(Cell_List_At(arg), &temp);
        }
        RETURN (list);
    }

    case SYM_REVERSE: {
        Fail_If_Read_Only_Flex(arr);

        REBLEN len = Part_Len_May_Modify_Index(list, D_ARG(3));
        if (len == 0)
            RETURN (list); // !!! do 1-element reversals update newlines?

        Cell* front = Cell_List_At(list);
        Cell* back = front + len - 1;

        // We must reverse the sense of the newline markers as well, #2326
        // Elements that used to be the *end* of lines now *start* lines.
        // So really this just means taking newline pointers that were
        // on the next element and putting them on the previous element.

        bool line_back;
        if (back == Array_Last(arr)) // !!! review tail newline handling
            line_back = Get_Array_Flag(arr, NEWLINE_AT_TAIL);
        else
            line_back = GET_VAL_FLAG(back + 1, VALUE_FLAG_NEWLINE_BEFORE);

        for (len /= 2; len > 0; --len, ++front, --back) {
            bool line_front = GET_VAL_FLAG(
                front + 1,
                VALUE_FLAG_NEWLINE_BEFORE
            );

            Cell temp;
            temp.header = front->header;
            temp.extra = front->extra;
            temp.payload = front->payload;

            // When we move the back cell to the front position, it gets the
            // newline flag based on the flag state that was *after* it.
            //
            Blit_Cell(front, back);
            if (line_back)
                SET_VAL_FLAG(front, VALUE_FLAG_NEWLINE_BEFORE);
            else
                CLEAR_VAL_FLAG(front, VALUE_FLAG_NEWLINE_BEFORE);

            // We're pushing the back pointer toward the front, so the flag
            // that was on the back will be the after for the next blit.
            //
            line_back = GET_VAL_FLAG(back, VALUE_FLAG_NEWLINE_BEFORE);
            Blit_Cell(back, &temp);
            if (line_front)
                SET_VAL_FLAG(back, VALUE_FLAG_NEWLINE_BEFORE);
            else
                CLEAR_VAL_FLAG(back, VALUE_FLAG_NEWLINE_BEFORE);
        }
        RETURN (list);
    }

    case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        UNUSED(PAR(series));
        UNUSED(REF(part)); // checks limit as void
        UNUSED(REF(skip)); // checks size as void
        UNUSED(REF(compare)); // checks comparator as void

        Fail_If_Read_Only_Flex(arr);

        Sort_List(
            list,
            REF(case),
            ARG(size), // skip size (may be void if no /SKIP)
            ARG(comparator), // (may be void if no /COMPARE)
            ARG(limit), // (may be void if no /PART)
            REF(all),
            REF(reverse)
        );
        RETURN (list);
    }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        REBLEN index = VAL_INDEX(list);

        if (REF(seed))
            fail (Error_Bad_Refines_Raw());

        if (REF(only)) { // pick an element out of the list
            if (index >= VAL_LEN_HEAD(list))
                return nullptr;

            Init_Integer(
                ARG(seed),
                1 + (Random_Int(REF(secure)) % (VAL_LEN_HEAD(list) - index))
            );

            Cell* slot = Pick_Block(OUT, list, ARG(seed));
            if (Is_Nulled(OUT)) {
                assert(slot);
                UNUSED(slot);
                return nullptr;
            }
            return OUT;

        }

        Shuffle_List(list, REF(secure));
        RETURN (list);
    }

    default:
        break; // fallthrough to error
    }

    // If it wasn't one of the block actions, fall through and let the port
    // system try.  OPEN [scheme: ...], READ [ ], etc.
    //
    // !!! This used to be done by sensing explicitly what a "port action"
    // was, but that involved checking if the action was in a numeric range.
    // The symbol-based action dispatch is more open-ended.  Trying this
    // to see how it works.

    return T_Port(level_, verb);
}


#if !defined(NDEBUG)

//
//  Assert_Array_Core: C
//
void Assert_Array_Core(Array* a)
{
    // Basic integrity checks (series is not marked free, etc.)  Note that
    // we don't use ASSERT_SERIES the macro here, because that checks to
    // see if the series is an array...and if so, would call this routine
    //
    Assert_Flex_Core(a);

    if (not Is_Flex_Array(a))
        panic (a);

    Cell* item = Array_Head(a);
    REBLEN i;
    for (i = 0; i < Array_Len(a); ++i, ++item) {
        if (IS_END(item)) {
            printf("Premature array end at index %d\n", cast(int, i));
            panic (a);
        }
    }

    if (NOT_END(item))
        panic (item);

    if (Is_Flex_Dynamic(a)) {
        REBLEN rest = Flex_Rest(a);
        assert(rest > 0 and rest > i);

        for (; i < rest - 1; ++i, ++item) {
            const bool unwritable = not (item->header.bits & NODE_FLAG_CELL);
            if (Get_Flex_Flag(a, FIXED_SIZE)) {
              #if !defined(NDEBUG)
                if (not unwritable) {
                    printf("Writable cell found in fixed-size array rest\n");
                    panic (a);
                }
              #endif
            }
            else {
                if (unwritable) {
                    printf("Unwritable cell found in array rest capacity\n");
                    panic (a);
                }
            }
        }
        assert(item == Array_At(a, rest - 1));

        Cell* ultimate = Array_At(a, rest - 1);
        if (NOT_END(ultimate) or (ultimate->header.bits & NODE_FLAG_CELL)) {
            printf("Implicit termination/unwritable END missing from array\n");
            panic (a);
        }
    }

}
#endif
