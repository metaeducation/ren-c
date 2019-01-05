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
//  CT_Array: C
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
REBINT CT_Array(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    REBINT num = Cmp_Array(a, b, mode == 1);
    if (mode >= 0)
        return (num == 0);
    if (mode == -1)
        return (num >= 0);
    return (num > 0);
}


//
//  MAKE_Array: C
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
REB_R MAKE_Array(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    if (IS_INTEGER(arg) or IS_DECIMAL(arg)) {
        //
        // `make block! 10` => creates array with certain initial capacity
        //
        return Init_Any_Array(out, kind, Make_Arr(Int32s(arg, 0)));
    }
    else if (IS_TEXT(arg)) {
        //
        // `make block! "a <b> #c"` => `[a <b> #c]`, scans as code (unbound)
        //
        // Until UTF-8 Everywhere, text must be converted to UTF-8 before
        // using it with the scanner.
        //
        REBSIZ offset;
        REBSIZ size;
        REBSER *temp = Temp_UTF8_At_Managed(
            &offset, &size, arg, VAL_LEN_AT(arg)
        );
        PUSH_GC_GUARD(temp);
        REBSTR * const filename = Canon(SYM___ANONYMOUS__);
        Init_Any_Array(
            out,
            kind,
            Scan_UTF8_Managed(filename, BIN_AT(temp, offset), size)
        );
        DROP_GC_GUARD(temp);
        return out;
    }
    else if (ANY_ARRAY(arg)) {
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
            || !ANY_ARRAY(VAL_ARRAY_AT(arg))
            || !IS_INTEGER(VAL_ARRAY_AT(arg) + 1)
        ) {
            goto bad_make;
        }

        RELVAL *any_array = VAL_ARRAY_AT(arg);
        REBINT index = VAL_INDEX(any_array) + Int32(VAL_ARRAY_AT(arg) + 1) - 1;

        if (index < 0 || index > cast(REBINT, VAL_LEN_HEAD(any_array)))
            goto bad_make;

        // !!! Previously this code would clear line break options on path
        // elements, using `CLEAR_VAL_FLAG(..., VALUE_FLAG_LINE)`.  But if
        // arrays are allowed to alias each others contents, the aliasing
        // via MAKE shouldn't modify the store.  Line marker filtering out of
        // paths should be part of the MOLDing logic -or- a path with embedded
        // line markers should use construction syntax to preserve them.

        REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(arg), any_array);
        return Init_Any_Series_At_Core(
            out,
            kind,
            SER(VAL_ARRAY(any_array)),
            index,
            derived
        );
    }
    else if (IS_TYPESET(arg)) {
        //
        // !!! Should MAKE GROUP! and MAKE PATH! from a TYPESET! work like
        // MAKE BLOCK! does?  Allow it for now.
        //
        return Init_Any_Array(out, kind, Typeset_To_Array(arg));
    }
    else if (IS_BINARY(arg)) {
        //
        // `to block! #{00BDAE....}` assumes the binary data is UTF8, and
        // goes directly to the scanner to make an unbound code array.
        //
        REBSTR * const filename = Canon(SYM___ANONYMOUS__);
        return Init_Any_Array(
            out,
            kind,
            Scan_UTF8_Managed(filename, VAL_BIN_AT(arg), VAL_LEN_AT(arg))
        );
    }
    else if (IS_MAP(arg)) {
        return Init_Any_Array(out, kind, Map_To_Array(VAL_MAP(arg), 0));
    }
    else if (ANY_CONTEXT(arg)) {
        return Init_Any_Array(out, kind, Context_To_Array(VAL_CONTEXT(arg), 3));
    }
    else if (IS_VECTOR(arg)) {
        return Init_Any_Array(out, kind, Vector_To_Array(arg));
    }
    else if (IS_VARARGS(arg)) {
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
            assert(
                NOT_SER_FLAG(
                    arg->extra.binding, ARRAY_FLAG_VARLIST
                )
            );
        }
        else {
            REBCTX *context = CTX(arg->extra.binding);
            REBFRM *param_frame = CTX_FRAME_MAY_FAIL(context);

            REBVAL *param = ACT_PARAMS_HEAD(FRM_PHASE(param_frame))
                + arg->payload.varargs.param_offset;

            if (TYPE_CHECK(param, REB_MAX_NULLED))
                fail (Error_Null_Vararg_Array_Raw());
        }

        REBDSP dsp_orig = DSP;

        do {
            if (Do_Vararg_Op_Maybe_End_Throws(
                out,
                arg,
                VARARG_OP_TAKE
            )){
                DS_DROP_TO(dsp_orig);
                return R_THROWN;
            }

            if (IS_END(out))
                break;

            Move_Value(DS_PUSH(), out);
        } while (true);

        return Init_Any_Array(out, kind, Pop_Stack_Values(dsp_orig));
    }
    else if (IS_ACTION(arg)) {
        //
        // !!! Experimental behavior; if action can run as arity-0, then
        // invoke it so long as it doesn't return null, collecting values.
        //
        REBDSP dsp_orig = DSP;
        while (true) {
            REBVAL *generated = rebRun(rebEval(arg), rebEND);
            if (not generated)
                break;
            Move_Value(DS_PUSH(), generated);
            rebRelease(generated);
        }
        return Init_Any_Array(out, kind, Pop_Stack_Values(dsp_orig));
    }

  bad_make:;
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Array: C
//
REB_R TO_Array(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    if (
        kind == VAL_TYPE(arg) // always act as COPY if types match
        or Splices_Into_Type_Without_Only(kind, arg) // see comments
    ){
        return Init_Any_Array(
            out,
            kind,
            Copy_Values_Len_Shallow(
                VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg), VAL_ARRAY_LEN_AT(arg)
            )
        );
    }
    else {
        // !!! Review handling of making a 1-element PATH!, e.g. TO PATH! 10
        //
        REBARR *single = Alloc_Singular(NODE_FLAG_MANAGED);
        Move_Value(ARR_SINGLE(single), arg);
        return Init_Any_Array(out, kind, single);
    }
}


//
//  Find_In_Array: C
//
// !!! Comment said "Final Parameters: tail - tail position, match - sequence,
// SELECT - (value that follows)".  It's not clear what this meant.
//
REBCNT Find_In_Array(
    REBARR *array,
    REBCNT index, // index to start search
    REBCNT end, // ending position
    const RELVAL *target,
    REBCNT len, // length of target
    REBFLGS flags, // see AM_FIND_XXX
    REBINT skip // skip factor
){
    REBCNT start = index;

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
    if (ANY_WORD(target)) {
        for (; index >= start && index < end; index += skip) {
            RELVAL *item = ARR_AT(array, index);
            REBSTR *target_canon = VAL_WORD_CANON(target); // canonize once
            if (ANY_WORD(item)) {
                if (flags & AM_FIND_CASE) { // Must be same type and spelling
                    if (
                        VAL_WORD_SPELLING(item) == VAL_WORD_SPELLING(target)
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
    if (ANY_ARRAY(target) and not (flags & AM_FIND_ONLY)) {
        for (; index >= start and index < end; index += skip) {
            RELVAL *item = ARR_AT(array, index);

            REBCNT count = 0;
            RELVAL *other = VAL_ARRAY_AT(target);
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
    if (IS_DATATYPE(target) || IS_TYPESET(target)) {
        for (; index >= start && index < end; index += skip) {
            RELVAL *item = ARR_AT(array, index);

            if (IS_DATATYPE(target)) {
                if (VAL_TYPE(item) == VAL_TYPE_KIND(target))
                    return index;
                if (
                    IS_DATATYPE(item)
                    && VAL_TYPE_KIND(item) == VAL_TYPE_KIND(target)
                ){
                    return index;
                }
            }
            else if (IS_TYPESET(target)) {
                if (TYPE_CHECK(target, VAL_TYPE(item)))
                    return index;
                if (
                    IS_DATATYPE(item)
                    && TYPE_CHECK(target, VAL_TYPE_KIND(item))
                ){
                    return index;
                }
                if (IS_TYPESET(item) && EQUAL_TYPESET(item, target))
                    return index;
            }
            if (flags & AM_FIND_MATCH)
                break;
        }
        return NOT_FOUND;
    }

    // All other cases

    for (; index >= start && index < end; index += skip) {
        RELVAL *item = ARR_AT(array, index);
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
    REBCNT offset;
    REBVAL *comparator;
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
            cast(const RELVAL*, v2) + flags->offset,
            cast(const RELVAL*, v1) + flags->offset,
            flags->cased
        );
    else
        return Cmp_Value(
            cast(const RELVAL*, v1) + flags->offset,
            cast(const RELVAL*, v2) + flags->offset,
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

    DECLARE_LOCAL (result);
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

    if (IS_LOGIC(result)) {
        if (VAL_LOGIC(result))
            tristate = 1;
    }
    else if (IS_INTEGER(result)) {
        if (VAL_INT64(result) > 0)
            tristate = 1;
        else if (VAL_INT64(result) == 0)
            tristate = 0;
    }
    else if (IS_DECIMAL(result)) {
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
//  Sort_Block: C
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
static void Sort_Block(
    REBVAL *block,
    bool ccase,
    REBVAL *skipv,
    REBVAL *compv,
    REBVAL *part,
    bool all,
    bool rev
) {
    struct sort_flags flags;
    flags.cased = ccase;
    flags.reverse = rev;
    flags.all = all; // !!! not used?

    if (IS_ACTION(compv)) {
        flags.comparator = compv;
        flags.offset = 0;
    }
    else if (IS_INTEGER(compv)) {
        flags.comparator = NULL;
        flags.offset = Int32(compv) - 1;
    }
    else {
        assert(IS_NULLED(compv));
        flags.comparator = NULL;
        flags.offset = 0;
    }

    REBCNT len = Part_Len_May_Modify_Index(block, part); // length of sort
    if (len <= 1)
        return;

    // Skip factor:
    REBCNT skip;
    if (not IS_NULLED(skipv)) {
        skip = Get_Num_From_Arg(skipv);
        if (skip <= 0 || len % skip != 0 || skip > len)
            fail (Error_Out_Of_Range(skipv));
    }
    else
        skip = 1;

    reb_qsort_r(
        VAL_ARRAY_AT(block),
        len / skip,
        sizeof(REBVAL) * skip,
        &flags,
        flags.comparator != NULL ? &Compare_Val_Custom : &Compare_Val
    );
}


//
//  Shuffle_Block: C
//
void Shuffle_Block(REBVAL *value, bool secure)
{
    REBCNT n;
    REBCNT k;
    REBCNT idx = VAL_INDEX(value);
    RELVAL *data = VAL_ARRAY_HEAD(value);

    // Rare case where RELVAL bit copying is okay...between spots in the
    // same array.
    //
    RELVAL swap;

    for (n = VAL_LEN_AT(value); n > 1;) {
        k = idx + (REBCNT)Random_Int(secure) % n;
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
//  PD_Array: C
//
// Path dispatch for the following types:
//
//     PD_Block
//     PD_Group
//
// It is delegated to by path dispatch if the path payload is an array:
//
//     PD_Path
//     PD_Get_Path
//     PD_Set_Path
//
REB_R PD_Array(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    REBINT n;

    if (IS_INTEGER(picker) or IS_DECIMAL(picker)) { // #2312
        n = Int32(picker);
        if (n == 0)
            return nullptr; // Rebol2/Red convention: 0 is not a pick
        if (n < 0)
            ++n; // Rebol2/Red convention: `pick tail [a b c] -1` is `c`
        n += VAL_INDEX(pvs->out) - 1;
    }
    else if (IS_WORD(picker)) {
        //
        // Linear search to case-insensitive find ANY-WORD! matching the canon
        // and return the item after it.  Default to out of range.
        //
        n = -1;

        REBSTR *canon = VAL_WORD_CANON(picker);
        RELVAL *item = VAL_ARRAY_AT(pvs->out);
        REBCNT index = VAL_INDEX(pvs->out);
        for (; NOT_END(item); ++item, ++index) {
            if (ANY_WORD(item) && canon == VAL_WORD_CANON(item)) {
                n = index + 1;
                break;
            }
        }
    }
    else if (IS_LOGIC(picker)) {
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
            VAL_ARRAY(pvs->out),
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
        FAIL_IF_READ_ONLY_SERIES(pvs->out);

    pvs->u.ref.cell = VAL_ARRAY_AT_HEAD(pvs->out, n);
    pvs->u.ref.specifier = VAL_SPECIFIER(pvs->out);
    return R_REFERENCE;
}


//
//  Pick_Block: C
//
// Fills out with void if no pick.
//
RELVAL *Pick_Block(REBVAL *out, const REBVAL *block, const REBVAL *picker)
{
    REBINT n = Get_Num_From_Arg(picker);
    n += VAL_INDEX(block) - 1;
    if (n < 0 || cast(REBCNT, n) >= VAL_LEN_HEAD(block)) {
        Init_Nulled(out);
        return NULL;
    }

    RELVAL *slot = VAL_ARRAY_AT_HEAD(block, n);
    Derelativize(out, slot, VAL_SPECIFIER(block));
    return slot;
}


//
//  MF_Array: C
//
void MF_Array(REB_MOLD *mo, const REBCEL *v, bool form)
{
    // Routine may be called on value that reports REB_QUOTED, even if it
    // has no additional payload and is aliasing the cell itself.  Checking
    // the type could be avoided if each type had its own dispatcher, but
    // this routine seems to need to be generic.
    //
    enum Reb_Kind kind = CELL_KIND(v);

    if (form) {
        Form_Array_At(mo, VAL_ARRAY(v), VAL_INDEX(v), 0);
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
        Mold_Array_At(mo, VAL_ARRAY(v), 0, "[]");
        Post_Mold(mo, v);
        Append_Utf8_Codepoint(mo->series, ']');
    }
    else {
        const char *sep;

        switch (kind) {
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

          default:
            panic ("Unknown array kind passed to MF_Array");
        }

        Mold_Array_At(mo, VAL_ARRAY(v), VAL_INDEX(v), sep);
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
REBTYPE(Array)
{
    REBVAL *array = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    // Common operations for any series type (length, head, etc.)
    //
    REB_R r = Series_Common_Action_Maybe_Unhandled(frame_, verb);
    if (r != R_UNHANDLED)
        return r;

    REBARR *arr = VAL_ARRAY(array);
    REBSPC *specifier = VAL_SPECIFIER(array);

    REBSYM sym = VAL_WORD_SYM(verb);
    switch (sym) {
      case SYM_TAKE_P: {
        INCLUDE_PARAMS_OF_TAKE_P;

        UNUSED(PAR(series));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        FAIL_IF_READ_ONLY_ARRAY(array);

        REBCNT len;
        if (REF(part)) {
            len = Part_Len_May_Modify_Index(array, ARG(limit));
            if (len == 0)
                return Init_Block(D_OUT, Make_Arr(0)); // new empty block
        }
        else
            len = 1;

        REBCNT index = VAL_INDEX(array); // Partial() can change index

        if (REF(last))
            index = VAL_LEN_HEAD(array) - len;

        if (index >= VAL_LEN_HEAD(array)) {
            if (not REF(part))
                return nullptr;

            return Init_Block(D_OUT, Make_Arr(0)); // new empty block
        }

        if (REF(part))
            Init_Block(
                D_OUT, Copy_Array_At_Max_Shallow(arr, index, specifier, len)
            );
        else
            Derelativize(D_OUT, &ARR_HEAD(arr)[index], specifier);

        Remove_Series(SER(arr), index, len);
        return D_OUT; }

    //-- Search:

      case SYM_FIND:
      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_FIND; // must be same as select

        UNUSED(PAR(series));
        UNUSED(PAR(value)); // aliased as arg

        REBINT len = ANY_ARRAY(arg) ? VAL_ARRAY_LEN_AT(arg) : 1;

        REBCNT limit = Part_Tail_May_Modify_Index(array, ARG(limit));
        UNUSED(REF(part)); // checked by if limit is nulled

        REBCNT index = VAL_INDEX(array);

        REBFLGS flags = (
            (REF(only) ? AM_FIND_ONLY : 0)
            | (REF(match) ? AM_FIND_MATCH : 0)
            | (REF(reverse) ? AM_FIND_REVERSE : 0)
            | (REF(case) ? AM_FIND_CASE : 0)
            | (REF(last) ? AM_FIND_LAST : 0)
        );

        REBCNT skip = REF(skip) ? Int32s(ARG(size), 1) : 1;

        REBCNT ret = Find_In_Array(
            arr, index, limit, arg, len, flags, skip
        );

        if (ret >= limit)
            return nullptr;

        if (REF(only))
            len = 1;

        if (VAL_WORD_SYM(verb) == SYM_FIND) {
            if (REF(tail) || REF(match))
                ret += len;
            VAL_INDEX(array) = ret;
            Move_Value(D_OUT, array);
        }
        else {
            ret += len;
            if (ret >= limit)
                return nullptr;

            Derelativize(D_OUT, ARR_AT(arr, ret), specifier);
        }
        return Inherit_Const(D_OUT, array); }

    //-- Modification:
      case SYM_APPEND:
      case SYM_INSERT:
      case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;

        UNUSED(PAR(series));
        UNUSED(PAR(value));

        REBCNT len; // length of target
        if (VAL_WORD_SYM(verb) == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(array, ARG(limit));
        else
            len = Part_Len_Append_Insert_May_Modify_Index(arg, ARG(limit));

        // Note that while inserting or removing NULL is a no-op, CHANGE with
        // a /PART can actually erase data.
        //
        if (IS_NULLED(arg) and len == 0) { // only nulls bypass write attempts
            if (sym == SYM_APPEND) // append always returns head
                VAL_INDEX(array) = 0;
            RETURN (array); // don't fail on read only if it would be a no-op
        }
        FAIL_IF_READ_ONLY_ARRAY(array);

        REBCNT index = VAL_INDEX(array);

        REBFLGS flags = 0;
        if (
            not REF(only)
            and Splices_Into_Type_Without_Only(VAL_TYPE(array), arg)
        ){
            flags |= AM_SPLICE;
        }
        if (REF(part))
            flags |= AM_PART;
        if (REF(line))
            flags |= AM_LINE;

        Move_Value(D_OUT, array);
        VAL_INDEX(D_OUT) = Modify_Array(
            VAL_WORD_SPELLING(verb),
            arr,
            index,
            arg,
            flags,
            len,
            REF(dup) ? Int32(ARG(count)) : 1
        );
        return D_OUT; }

      case SYM_CLEAR: {
        FAIL_IF_READ_ONLY_ARRAY(array);
        REBCNT index = VAL_INDEX(array);
        if (index < VAL_LEN_HEAD(array)) {
            if (index == 0) Reset_Array(arr);
            else {
                SET_END(ARR_AT(arr, index));
                SET_SERIES_LEN(VAL_SERIES(array), cast(REBCNT, index));
            }
        }
        RETURN (array);
    }

    //-- Creation:

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        REBU64 types = 0;
        REBCNT tail = Part_Tail_May_Modify_Index(array, ARG(limit));
        UNUSED(REF(part));

        REBCNT index = VAL_INDEX(array);

        if (REF(deep))
            types |= REF(types) ? 0 : TS_STD_SERIES;

        if (REF(types)) {
            if (IS_DATATYPE(ARG(kinds)))
                types |= FLAGIT_KIND(VAL_TYPE(ARG(kinds)));
            else
                types |= VAL_TYPESET_BITS(ARG(kinds));
        }

        REBFLGS flags = ARRAY_FLAG_FILE_LINE;

        // We shouldn't be returning a const value from the copy, but if the
        // input value was const and we don't copy some types deeply, those
        // types should retain the constness intended for them.
        //
        flags |= (array->header.bits & ARRAY_FLAG_CONST_SHALLOW);

        REBARR *copy = Copy_Array_Core_Managed(
            arr,
            index, // at
            specifier,
            tail, // tail
            0, // extra
            flags, // flags
            types // types to copy deeply
        );

        return Init_Any_Array(D_OUT, VAL_TYPE(array), copy);
    }

    //-- Special actions:

    case SYM_SWAP: {
        if (not ANY_ARRAY(arg))
            fail (Error_Invalid(arg));

        FAIL_IF_READ_ONLY_ARRAY(array);
        FAIL_IF_READ_ONLY_ARRAY(arg);

        REBCNT index = VAL_INDEX(array);

        if (
            index < VAL_LEN_HEAD(array)
            && VAL_INDEX(arg) < VAL_LEN_HEAD(arg)
        ){
            // RELVAL bits can be copied within the same array
            //
            RELVAL *a = VAL_ARRAY_AT(array);
            RELVAL temp;
            temp.header = a->header;
            temp.payload = a->payload;
            temp.extra = a->extra;
            Blit_Cell(VAL_ARRAY_AT(array), VAL_ARRAY_AT(arg));
            Blit_Cell(VAL_ARRAY_AT(arg), &temp);
        }
        RETURN (array);
    }

    case SYM_REVERSE: {
        FAIL_IF_READ_ONLY_ARRAY(array);

        REBCNT len = Part_Len_May_Modify_Index(array, D_ARG(3));
        if (len == 0)
            RETURN (array); // !!! do 1-element reversals update newlines?

        RELVAL *front = VAL_ARRAY_AT(array);
        RELVAL *back = front + len - 1;

        // We must reverse the sense of the newline markers as well, #2326
        // Elements that used to be the *end* of lines now *start* lines.
        // So really this just means taking newline pointers that were
        // on the next element and putting them on the previous element.

        bool line_back;
        if (back == ARR_LAST(arr)) // !!! review tail newline handling
            line_back = GET_SER_FLAG(arr, ARRAY_FLAG_TAIL_NEWLINE);
        else
            line_back = GET_VAL_FLAG(back + 1, VALUE_FLAG_NEWLINE_BEFORE);

        for (len /= 2; len > 0; --len, ++front, --back) {
            bool line_front = GET_VAL_FLAG(
                front + 1,
                VALUE_FLAG_NEWLINE_BEFORE
            );

            RELVAL temp;
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
        RETURN (array);
    }

    case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        UNUSED(PAR(series));
        UNUSED(REF(part)); // checks limit as void
        UNUSED(REF(skip)); // checks size as void
        UNUSED(REF(compare)); // checks comparator as void

        FAIL_IF_READ_ONLY_ARRAY(array);

        Sort_Block(
            array,
            REF(case),
            ARG(size), // skip size (may be void if no /SKIP)
            ARG(comparator), // (may be void if no /COMPARE)
            ARG(limit), // (may be void if no /PART)
            REF(all),
            REF(reverse)
        );
        RETURN (array);
    }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        REBCNT index = VAL_INDEX(array);

        if (REF(seed))
            fail (Error_Bad_Refines_Raw());

        if (REF(only)) { // pick an element out of the array
            if (index >= VAL_LEN_HEAD(array))
                return nullptr;

            Init_Integer(
                ARG(seed),
                1 + (Random_Int(REF(secure)) % (VAL_LEN_HEAD(array) - index))
            );

            RELVAL *slot = Pick_Block(D_OUT, array, ARG(seed));
            if (IS_NULLED(D_OUT)) {
                assert(slot);
                UNUSED(slot);
                return nullptr;
            }
            return Inherit_Const(D_OUT, array);

        }

        FAIL_IF_READ_ONLY_ARRAY(array);
        Shuffle_Block(array, REF(secure));
        RETURN (array);
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

    return T_Port(frame_, verb);
}


//
//  blockify: native [
//
//  {If a value isn't already a BLOCK!, enclose it in a block, else return it}
//
//      return: [block!]
//      value "NULL input will produce an empty block"
//          [<opt> any-value!]
//  ]
//
REBNATIVE(blockify)
{
    INCLUDE_PARAMS_OF_BLOCKIFY;

    REBVAL *v = ARG(value);
    if (IS_BLOCK(v))
        RETURN (v);

    REBARR *a = Make_Arr_Core(1, NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE);
    if (IS_NULLED(v)) {
        // leave empty
    } else {
        Move_Value(ARR_HEAD(a), v);
        TERM_ARRAY_LEN(a, 1);
    }
    return Init_Block(D_OUT, a);
}


//
//  groupify: native [
//
//  {If a value isn't already a GROUP!, enclose it in a group, else return it}
//
//      return: [group!]
//      value "NULL input will produce an empty group"
//          [<opt> any-value!]
//  ]
//
REBNATIVE(groupify)
{
    INCLUDE_PARAMS_OF_GROUPIFY;

    REBVAL *v = ARG(value);
    if (IS_GROUP(v))
        RETURN (v);

    REBARR *a = Make_Arr_Core(1, NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE);
    if (IS_NULLED(v)) {
        // leave empty
    } else {
        Move_Value(ARR_HEAD(a), v);
        TERM_ARRAY_LEN(a, 1);
    }
    return Init_Group(D_OUT, a);
}


//
//  enblock: native [
//
//  {Enclose a value in a BLOCK!, even if it's already a block}
//
//      return: [block!]
//      value "NULL input will produce an empty block"
//          [<opt> any-value!]
//  ]
//
REBNATIVE(enblock)
{
    INCLUDE_PARAMS_OF_ENBLOCK;

    REBVAL *v = ARG(value);

    REBARR *a = Make_Arr_Core(1, NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE);
    if (IS_NULLED(v)) {
        // leave empty
    } else {
        Move_Value(ARR_HEAD(a), v);
        TERM_ARRAY_LEN(a, 1);
    }
    return Init_Block(D_OUT, a);
}


//
//  engroup: native [
//
//  {Enclose a value in a GROUP!, even if it's already a group}
//
//      return: [group!]
//      value "NULL input will produce an empty group"
//          [<opt> any-value!]
//  ]
//
REBNATIVE(engroup)
{
    INCLUDE_PARAMS_OF_ENGROUP;

    REBVAL *v = ARG(value);

    REBARR *a = Make_Arr_Core(1, NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE);
    if (IS_NULLED(v)) {
        // leave empty
    } else {
        Move_Value(ARR_HEAD(a), v);
        TERM_ARRAY_LEN(a, 1);
    }
    return Init_Group(D_OUT, a);
}


#if !defined(NDEBUG)

//
//  Assert_Array_Core: C
//
void Assert_Array_Core(REBARR *a)
{
    // Basic integrity checks (series is not marked free, etc.)  Note that
    // we don't use ASSERT_SERIES the macro here, because that checks to
    // see if the series is an array...and if so, would call this routine
    //
    Assert_Series_Core(SER(a));

    if (not IS_SER_ARRAY(a))
        panic (a);

    RELVAL *item = ARR_HEAD(a);
    REBCNT i;
    for (i = 0; i < ARR_LEN(a); ++i, ++item) {
        if (IS_END(item)) {
            printf("Premature array end at index %d\n", cast(int, i));
            panic (a);
        }
    }

    if (NOT_END(item))
        panic (item);

    if (IS_SER_DYNAMIC(a)) {
        REBCNT rest = SER_REST(SER(a));
        assert(rest > 0 and rest > i);

        for (; i < rest - 1; ++i, ++item) {
            const bool unwritable = not (item->header.bits & NODE_FLAG_CELL);
            if (GET_SER_FLAG(a, SERIES_FLAG_FIXED_SIZE)) {
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
        assert(item == ARR_AT(a, rest - 1));

        RELVAL *ultimate = ARR_AT(a, rest - 1);
        if (NOT_END(ultimate) or (ultimate->header.bits & NODE_FLAG_CELL)) {
            printf("Implicit termination/unwritable END missing from array\n");
            panic (a);
        }
    }

}
#endif
