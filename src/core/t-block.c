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
REBINT CT_Array(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    if (C_STACK_OVERFLOWING(&strict))
        Fail_Stack_Overflow();

    return Compare_Arrays_At_Indexes(
        VAL_ARRAY(a),
        VAL_INDEX(a),
        VAL_ARRAY(b),
        VAL_INDEX(b),
        strict
    );
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
REB_R MAKE_Array(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (IS_INTEGER(arg) or IS_DECIMAL(arg)) {
        //
        // `make block! 10` => creates array with certain initial capacity
        //
        return Init_Any_Array(out, kind, Make_Array(Int32s(arg, 0)));
    }
    else if (IS_TEXT(arg)) {
        //
        // `make block! "a <b> #c"` => `[a <b> #c]`, scans as code (unbound)
        //
        REBSIZ size = VAL_SIZE_LIMIT_AT(nullptr, arg, UNKNOWN);

        REBSTR * const filename = Canon(SYM___ANONYMOUS__);
        Init_Any_Array(
            out,
            kind,
            Scan_UTF8_Managed(filename, VAL_STRING_AT(arg), size)
        );
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
        if (
            VAL_ARRAY_LEN_AT(arg) != 2
            or not ANY_ARRAY(VAL_ARRAY_AT(arg))
            or not IS_INTEGER(VAL_ARRAY_AT(arg) + 1)
        ) {
            goto bad_make;
        }

        const RELVAL *any_array = VAL_ARRAY_AT(arg);
        REBINT index = VAL_INDEX(any_array) + Int32(VAL_ARRAY_AT(arg) + 1) - 1;

        if (index < 0 or index > cast(REBINT, VAL_LEN_HEAD(any_array)))
            goto bad_make;

        // !!! Previously this code would clear line break options on path
        // elements, using `CLEAR_CELL_FLAG(..., CELL_FLAG_LINE)`.  But if
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
    else if (ANY_ARRAY(arg)) {
        //
        // `to group! [1 2 3]` etc. -- copy the array data at the index
        // position and change the type.  (Note: MAKE does not copy the
        // data, but aliases it under a new kind.)
        //
        return Init_Any_Array(
            out,
            kind,
            Copy_Values_Len_Shallow(
                VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg), VAL_ARRAY_LEN_AT(arg)
            )
        );
    }
    else if (IS_TEXT(arg)) {
        //
        // `to block! "some string"` historically scans the source, so you
        // get an unbound code array.
        //
        REBSIZ utf8_size;
        const REBYTE *utf8 = VAL_UTF8_AT(&utf8_size, arg);
        REBSTR * const filename = Canon(SYM___ANONYMOUS__);
        return Init_Any_Array(
            out,
            kind,
            Scan_UTF8_Managed(filename, utf8, utf8_size)
        );
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
        if (not VAL_VARARGS_PHASE(arg)) {
            //
            // A vararg created from a block AND never passed as an argument
            // so no typeset or quoting settings available.  Can't produce
            // any voids, because the data source is a block.
            //
            assert(NOT_ARRAY_FLAG(EXTRA(Binding, arg).node, IS_VARLIST));
        }
        else {
            REBCTX *context = CTX(EXTRA(Binding, arg).node);
            REBFRM *param_frame = CTX_FRAME_MAY_FAIL(context);

            REBVAL *param = SPECIFIC(
                ARR_HEAD(ACT_PARAMLIST(FRM_PHASE(param_frame)))
            );
            if (VAL_VARARGS_SIGNED_PARAM_INDEX(arg) < 0)
                param += - VAL_VARARGS_SIGNED_PARAM_INDEX(arg);
            else
                param += VAL_VARARGS_SIGNED_PARAM_INDEX(arg);

            if (TYPE_CHECK(param, REB_NULLED))
                fail (Error_Null_Vararg_Array_Raw());
        }

        REBDSP dsp_orig = DSP;

        do {
            if (Do_Vararg_Op_Maybe_End_Throws(
                out,
                VARARG_OP_TAKE,
                arg
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
            REBVAL *generated = rebValue(arg, rebEND);
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
    if (ANY_PATH(arg)) {
        REBDSP dsp_orig = DSP;
        REBLEN len = VAL_SEQUENCE_LEN(arg);
        REBLEN i;
        for (i = 0; i < len; ++i)
            Derelativize(
                DS_PUSH(),
                VAL_SEQUENCE_AT(out, arg, i),
                VAL_SEQUENCE_SPECIFIER(arg)
            );
        return Init_Any_Array(out, kind, Pop_Stack_Values(dsp_orig));
    }
    else if (ANY_ARRAY(arg)) {
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
REBLEN Find_In_Array(
    const REBARR *array,
    REBLEN index_unsigned, // index to start search
    REBLEN end_unsigned, // ending position
    const RELVAL *target,
    REBLEN len, // length of target
    REBFLGS flags, // see AM_FIND_XXX
    REBINT skip // skip factor
){
    REBINT index = index_unsigned;  // skip can be negative, tested >= 0
    REBINT end = end_unsigned;

    REBINT start;
    if (skip < 0) {
        start = 0;
        --index;  // `find/skip tail [1 2] 2 -1` should start at the *2*
    }
    else
        start = index;

    // Optimized find word in block
    //
    if (ANY_WORD(target)) {
        for (; index >= start and index < end; index += skip) {
            const RELVAL *item = ARR_AT(array, index);
            REBSTR *target_canon = VAL_WORD_CANON(target); // canonize once
            if (ANY_WORD(item)) {
                if (flags & AM_FIND_CASE) { // Must be same type and spelling
                    if (
                        VAL_WORD_SPELLING(item) == VAL_WORD_SPELLING(target)
                        and VAL_TYPE(item) == VAL_TYPE(target)
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
            const RELVAL *item = ARR_AT(array, index);

            REBLEN count = 0;
            const RELVAL *other = VAL_ARRAY_AT(target);
            for (; NOT_END(other); ++other, ++item) {
                if (
                    IS_END(item) or
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
    if (IS_DATATYPE(target) or IS_TYPESET(target)) {
        for (; index >= start and index < end; index += skip) {
            const RELVAL *item = ARR_AT(array, index);

            if (IS_DATATYPE(target)) {
                if (VAL_TYPE(item) == VAL_TYPE_KIND(target))
                    return index;
                if (
                    IS_DATATYPE(item)
                    and VAL_TYPE_KIND(item) == VAL_TYPE_KIND(target)
                ){
                    return index;
                }
            }
            else if (IS_TYPESET(target)) {
                if (TYPE_CHECK(target, VAL_TYPE(item)))
                    return index;
                if (
                    IS_DATATYPE(item)
                    and TYPE_CHECK(target, VAL_TYPE_KIND(item))
                ){
                    return index;
                }
                if (IS_TYPESET(item) and EQUAL_TYPESET(item, target))
                    return index;
            }
            if (flags & AM_FIND_MATCH)
                break;
        }
        return NOT_FOUND;
    }

    // All other cases

    for (; index >= start and index < end; index += skip) {
        const RELVAL *item = ARR_AT(array, index);
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
    if (RunQ_Throws(
        result,
        fully,
        rebU(flags->comparator),
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
//  Shuffle_Array: C
//
void Shuffle_Array(REBARR *arr, REBLEN idx, bool secure)
{
    REBLEN n;
    REBLEN k;
    RELVAL *data = ARR_HEAD(arr);

    // Rare case where RELVAL bit copying is okay...between spots in the
    // same array.
    //
    RELVAL swap;

    for (n = ARR_LEN(arr) - idx; n > 1;) {
        k = idx + (REBLEN)Random_Int(secure) % n;
        n--;

        // Only do the following block when an actual swap occurs.
        // Otherwise an assertion will fail when trying to Blit_Cell() a
        // value to itself.
        //
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
// Path dispatch for ANY-ARRAY! (covers ANY-BLOCK! and ANY-GROUP!)
//
// !!! There is currently some delegation to this routine by ANY-SEQUENCE! if
// the underlying implementation is a REBARR*.
//
REB_R PD_Array(
    REBPVS *pvs,
    const RELVAL *picker,
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
        const RELVAL *item = VAL_ARRAY_AT(pvs->out);
        REBLEN index = VAL_INDEX(pvs->out);
        for (; NOT_END(item); ++item, ++index) {
            if (ANY_WORD(item) and canon == VAL_WORD_CANON(item)) {
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
        ENSURE_MUTABLE(pvs->out);

    // assume it will only write if opt_setval (mutability checked for)
    //
    pvs->u.ref.cell = m_cast(RELVAL*, VAL_ARRAY_AT_HEAD(pvs->out, n));
    pvs->u.ref.specifier = VAL_SPECIFIER(pvs->out);
    return R_REFERENCE;
}


//
//  Pick_Block: C
//
// Fills out with void if no pick.
//
RELVAL *Pick_Block(REBVAL *out, const REBVAL *block, const RELVAL *picker)
{
    REBINT n = Get_Num_From_Arg(picker);
    n += VAL_INDEX(block) - 1;
    if (n < 0 or cast(REBLEN, n) >= VAL_LEN_HEAD(block)) {
        Init_Nulled(out);
        return NULL;
    }

    const RELVAL *slot = VAL_ARRAY_AT_HEAD(block, n);
    Derelativize(out, slot, VAL_SPECIFIER(block));
    return out;
}


//
//  MF_Array: C
//
void MF_Array(REB_MOLD *mo, REBCEL(const*) v, bool form)
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
    if (VAL_INDEX(v) == 0) { // "and VAL_TYPE(v) <= REB_LIT_PATH" commented out
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

        Append_Codepoint(mo->series, '[');
        Mold_Array_At(mo, VAL_ARRAY(v), 0, "[]");
        Post_Mold(mo, v);
        Append_Codepoint(mo->series, ']');
    }
    else {
        const char *sep;

        switch (kind) {
          case REB_GET_BLOCK:
            Append_Codepoint(mo->series, ':');
            goto block;

          case REB_SYM_BLOCK:
            Append_Codepoint(mo->series, '@');
            goto block;

          case REB_BLOCK:
          case REB_SET_BLOCK:
          block:
            if (GET_MOLD_FLAG(mo, MOLD_FLAG_ONLY)) {
                CLEAR_MOLD_FLAG(mo, MOLD_FLAG_ONLY); // only top level
                sep = "\000\000";
            }
            else
                sep = "[]";
            break;

          case REB_GET_GROUP:
            Append_Codepoint(mo->series, ':');
            goto group;

          case REB_SYM_GROUP:
            Append_Codepoint(mo->series, '@');
            goto group;

          case REB_GROUP:
          case REB_SET_GROUP:
          group:
            sep = "()";
            break;

          default:
            panic ("Unknown array kind passed to MF_Array");
        }

        Mold_Array_At(mo, VAL_ARRAY(v), VAL_INDEX(v), sep);

        if (kind == REB_SET_GROUP or kind == REB_SET_BLOCK)
            Append_Codepoint(mo->series, ':');
    }
}


//
//  REBTYPE: C
//
// Implementation of type dispatch for ANY-ARRAY! (ANY-BLOCK! and ANY-GROUP!)
//
REBTYPE(Array)
{
    REBVAL *array = D_ARG(1);

    REBSPC *specifier = VAL_SPECIFIER(array);

    REBSYM sym = VAL_WORD_SYM(verb);
    switch (sym) {
      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE:
        //
      case SYM_REFLECT:
      case SYM_SKIP:
      case SYM_AT:
      case SYM_REMOVE:
        return Series_Common_Action_Maybe_Unhandled(frame_, verb);

      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        UNUSED(PAR(series));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        REBARR *arr = VAL_ARRAY_ENSURE_MUTABLE(array);

        REBLEN len;
        if (REF(part)) {
            len = Part_Len_May_Modify_Index(array, ARG(part));
            if (len == 0)
                return Init_Block(D_OUT, Make_Array(0)); // new empty block
        }
        else
            len = 1;

        REBLEN index = VAL_INDEX(array); // Partial() can change index

        if (REF(last))
            index = VAL_LEN_HEAD(array) - len;

        if (index >= VAL_LEN_HEAD(array)) {
            if (not REF(part))
                return nullptr;

            return Init_Block(D_OUT, Make_Array(0)); // new empty block
        }

        if (REF(part))
            Init_Block(
                D_OUT, Copy_Array_At_Max_Shallow(arr, index, specifier, len)
            );
        else
            Derelativize(D_OUT, &ARR_HEAD(arr)[index], specifier);

        Remove_Series_Units(SER(arr), index, len);
        return D_OUT; }

    //-- Search:

      case SYM_FIND:
      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_FIND; // must be same as select
        UNUSED(PAR(series));

        UNUSED(REF(reverse));  // Deprecated https://forum.rebol.info/t/1126
        UNUSED(REF(last));  // ...a HIJACK in %mezz-legacy errors if used

        REBVAL *pattern = ARG(pattern);

        REBINT len = ANY_ARRAY(pattern) ? VAL_ARRAY_LEN_AT(pattern) : 1;

        REBLEN limit = Part_Tail_May_Modify_Index(array, ARG(part));

        const REBARR *arr = VAL_ARRAY(array);
        REBLEN index = VAL_INDEX(array);

        REBFLGS flags = (
            (REF(only) ? AM_FIND_ONLY : 0)
            | (REF(match) ? AM_FIND_MATCH : 0)
            | (REF(case) ? AM_FIND_CASE : 0)
        );

        REBINT skip;
        if (REF(skip)) {
            skip = VAL_INT32(ARG(skip));
            if (skip == 0)
                fail (PAR(skip));
        }
        else
            skip = 1;

        REBLEN ret = Find_In_Array(
            arr, index, limit, pattern, len, flags, skip
        );

        if (ret == NOT_FOUND)
            return nullptr;

        assert(ret <= limit);

        if (REF(only))
            len = 1;

        if (VAL_WORD_SYM(verb) == SYM_FIND) {
            if (REF(tail) or REF(match))
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

        REBLEN len; // length of target
        if (VAL_WORD_SYM(verb) == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(array, ARG(part));
        else
            len = Part_Limit_Append_Insert(ARG(part));

        // Note that while inserting or appending NULL is a no-op, CHANGE with
        // a /PART can actually erase data.
        //
        if (IS_NULLED(ARG(value)) and len == 0) {  // only nulls bypass writes
            if (sym == SYM_APPEND) // append always returns head
                VAL_INDEX(array) = 0;
            RETURN (array); // don't fail on read only if it would be a no-op
        }

        REBARR *arr = VAL_ARRAY_ENSURE_MUTABLE(array);
        REBLEN index = VAL_INDEX(array);

        REBFLGS flags = 0;
        if (not REF(only) and Splices_Without_Only(ARG(value)))
            flags |= AM_SPLICE;
        if (REF(part))
            flags |= AM_PART;
        if (REF(line))
            flags |= AM_LINE;

        Move_Value(D_OUT, array);
        VAL_INDEX(D_OUT) = Modify_Array(
            arr,
            index,
            cast(enum Reb_Symbol, sym),
            ARG(value),
            flags,
            len,
            REF(dup) ? Int32(ARG(dup)) : 1
        );
        return D_OUT; }

      case SYM_CLEAR: {
        REBARR *arr = VAL_ARRAY_ENSURE_MUTABLE(array);
        REBLEN index = VAL_INDEX(array);

        if (index < VAL_LEN_HEAD(array)) {
            if (index == 0) Reset_Array(arr);
            else {
                SET_END(ARR_AT(arr, index));
                SET_SERIES_LEN(SER(arr), cast(REBLEN, index));
            }
        }
        RETURN (array);
    }

    //-- Creation:

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PAR(value));

        REBU64 types = 0;
        REBLEN tail = Part_Tail_May_Modify_Index(array, ARG(part));

        const REBARR *arr = VAL_ARRAY(array);
        REBLEN index = VAL_INDEX(array);

        if (REF(deep))
            types |= REF(types) ? 0 : TS_STD_SERIES;

        if (REF(types)) {
            if (IS_DATATYPE(ARG(types)))
                types |= FLAGIT_KIND(VAL_TYPE(ARG(types)));
            else {
                types |= VAL_TYPESET_LOW_BITS(ARG(types));
                types |= cast(REBU64, VAL_TYPESET_HIGH_BITS(ARG(types)))
                    << 32;
            }
        }

        REBFLGS flags = ARRAY_MASK_HAS_FILE_LINE;

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

        return Init_Any_Array(D_OUT, VAL_TYPE(array), copy); }

    //-- Special actions:

      case SYM_SWAP: {
        REBVAL *arg = D_ARG(2);
        if (not ANY_ARRAY(arg))
            fail (arg);

        REBLEN index = VAL_INDEX(array);

        if (
            index < VAL_LEN_HEAD(array)
            and VAL_INDEX(arg) < VAL_LEN_HEAD(arg)
        ){
            // RELVAL bits can be copied within the same array
            //
            RELVAL *a = VAL_ARRAY_AT_ENSURE_MUTABLE(array);
            RELVAL *b = VAL_ARRAY_AT_ENSURE_MUTABLE(arg);
            RELVAL temp;
            temp.header = a->header;
            temp.payload = a->payload;
            temp.extra = a->extra;
            Blit_Cell(a, b);
            Blit_Cell(b, &temp);
        }
        RETURN (array); }

      case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;
        UNUSED(ARG(series));  // covered by `v`

        REBARR *arr = VAL_ARRAY_ENSURE_MUTABLE(array);
        REBLEN index = VAL_INDEX(array);

        REBLEN len = Part_Len_May_Modify_Index(array, ARG(part));
        if (len == 0)
            RETURN (array); // !!! do 1-element reversals update newlines?

        RELVAL *front = ARR_AT(arr, index);
        RELVAL *back = front + len - 1;

        // We must reverse the sense of the newline markers as well, #2326
        // Elements that used to be the *end* of lines now *start* lines.
        // So really this just means taking newline pointers that were
        // on the next element and putting them on the previous element.

        bool line_back;
        if (back == ARR_LAST(arr)) // !!! review tail newline handling
            line_back = GET_ARRAY_FLAG(arr, NEWLINE_AT_TAIL);
        else
            line_back = GET_CELL_FLAG(back + 1, NEWLINE_BEFORE);

        for (len /= 2; len > 0; --len, ++front, --back) {
            bool line_front = GET_CELL_FLAG(front + 1, NEWLINE_BEFORE);

            RELVAL temp;
            temp.header = front->header;
            temp.extra = front->extra;
            temp.payload = front->payload;

            // When we move the back cell to the front position, it gets the
            // newline flag based on the flag state that was *after* it.
            //
            Blit_Cell(front, back);
            if (line_back)
                SET_CELL_FLAG(front, NEWLINE_BEFORE);
            else
                CLEAR_CELL_FLAG(front, NEWLINE_BEFORE);

            // We're pushing the back pointer toward the front, so the flag
            // that was on the back will be the after for the next blit.
            //
            line_back = GET_CELL_FLAG(back, NEWLINE_BEFORE);
            Blit_Cell(back, &temp);
            if (line_front)
                SET_CELL_FLAG(back, NEWLINE_BEFORE);
            else
                CLEAR_CELL_FLAG(back, NEWLINE_BEFORE);
        }
        RETURN (array); }

      case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;
        UNUSED(PAR(series));  // covered by `v`

        REBARR *arr = VAL_ARRAY_ENSURE_MUTABLE(array);

        struct sort_flags flags;
        flags.cased = did REF(case);
        flags.reverse = did REF(reverse);
        flags.all = did REF(all);  // !!! not used?

        REBVAL *cmp = ARG(compare);  // null if no /COMPARE
        if (IS_ACTION(cmp)) {
            flags.comparator = cmp;
            flags.offset = 0;
        }
        else if (IS_INTEGER(cmp)) {
            flags.comparator = nullptr;
            flags.offset = Int32(cmp) - 1;
        }
        else {
            assert(IS_NULLED(cmp));
            flags.comparator = nullptr;
            flags.offset = 0;
        }

        Move_Value(D_OUT, array);  // save array before messing with index

        REBLEN len = Part_Len_May_Modify_Index(array, ARG(part));
        if (len <= 1)
            return D_OUT;
        REBLEN index = VAL_INDEX(array);  // ^-- may have been modified

        // Skip factor:
        REBLEN skip;
        if (IS_NULLED(ARG(skip)))
            skip = 1;
        else {
            skip = Get_Num_From_Arg(ARG(skip));
            if (skip <= 0 or len % skip != 0 or skip > len)
                fail (Error_Out_Of_Range(ARG(skip)));
        }

        reb_qsort_r(
            ARR_AT(arr, index),
            len / skip,
            sizeof(REBVAL) * skip,
            &flags,
            flags.comparator != nullptr ? &Compare_Val_Custom : &Compare_Val
        );

        return D_OUT; }

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;
        UNUSED(PAR(value));  // covered by `v`

        REBLEN index = VAL_INDEX(array);

        if (REF(seed))
            fail (Error_Bad_Refines_Raw());

        if (REF(only)) { // pick an element out of the array
            if (index >= VAL_LEN_HEAD(array))
                return nullptr;

            Init_Integer(
                ARG(seed),
                1 + (Random_Int(did REF(secure))
                    % (VAL_LEN_HEAD(array) - index))
            );

            RELVAL *slot = Pick_Block(D_OUT, array, ARG(seed));
            if (IS_NULLED(D_OUT)) {
                assert(slot);
                UNUSED(slot);
                return nullptr;
            }
            return Inherit_Const(D_OUT, array);
        }

        REBARR *arr = VAL_ARRAY_ENSURE_MUTABLE(array);
        Shuffle_Array(arr, VAL_INDEX(array), did REF(secure));
        RETURN (array); }

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

    REBARR *a = Make_Array_Core(
        1,
        NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE
    );

    if (IS_NULLED(v)) {
        // leave empty
    } else {
        Move_Value(ARR_HEAD(a), v);
        TERM_ARRAY_LEN(a, 1);
    }
    return Init_Block(D_OUT, Freeze_Array_Shallow(a));
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

    REBARR *a = Make_Array_Core(
        1,
        NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE
    );

    if (IS_NULLED(v)) {
        // leave empty
    } else {
        Move_Value(ARR_HEAD(a), v);
        TERM_ARRAY_LEN(a, 1);
    }
    return Init_Group(D_OUT, Freeze_Array_Shallow(a));
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

    REBARR *a = Make_Array_Core(
        1,
        NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE
    );

    if (IS_NULLED(v)) {
        // leave empty
    } else {
        Move_Value(ARR_HEAD(a), v);
        TERM_ARRAY_LEN(a, 1);
    }
    return Init_Block(D_OUT, Freeze_Array_Shallow(a));
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

    REBARR *a = Make_Array_Core(
        1,
        NODE_FLAG_MANAGED | ARRAY_MASK_HAS_FILE_LINE
    );

    if (IS_NULLED(v)) {
        // leave empty
    } else {
        Move_Value(ARR_HEAD(a), v);
        TERM_ARRAY_LEN(a, 1);
    }
    return Init_Group(D_OUT, Freeze_Array_Shallow(a));
}


#if !defined(NDEBUG)

//
//  Assert_Array_Core: C
//
void Assert_Array_Core(const REBARR *a)
{
    // Basic integrity checks (series is not marked free, etc.)  Note that
    // we don't use ASSERT_SERIES the macro here, because that checks to
    // see if the series is an array...and if so, would call this routine
    //
    Assert_Series_Core(SER(a));

    if (not IS_SER_ARRAY(a))
        panic (a);

    const RELVAL *item = ARR_HEAD(a);
    REBLEN i;
    REBLEN len = ARR_LEN(a);
    for (i = 0; i < len; ++i, ++item) {
        if (IS_END(item)) {
            printf("Premature array end at index %d\n", cast(int, i));
            panic (a);
        }
        if (not GET_ARRAY_FLAG(a, IS_PARAMLIST)) {  // uses > REB_MAX for PTYPE
            if (KIND_BYTE_UNCHECKED(item) % REB_64 >= REB_MAX) {
                printf("Invalid KIND_BYTE at index %d\n", cast(int, i));
                panic (a);
            }
        }
    }

    if (NOT_END(item))
        panic (item);

    if (IS_SER_DYNAMIC(a)) {
        REBLEN rest = SER_REST(SER(a));
        assert(rest > 0 and rest > i);

        for (; i < rest - 1; ++i, ++item) {
            const bool unwritable = not (item->header.bits & NODE_FLAG_CELL);
            if (GET_SERIES_FLAG(a, FIXED_SIZE)) {
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

        const RELVAL *ultimate = ARR_AT(a, rest - 1);
        if (NOT_END(ultimate) or (ultimate->header.bits & NODE_FLAG_CELL)) {
            printf("Implicit termination/unwritable END missing from array\n");
            panic (a);
        }
    }

}
#endif
