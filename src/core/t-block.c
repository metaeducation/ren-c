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
REBINT CT_Array(const RELVAL *a, const RELVAL *b, REBINT mode)
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
void MAKE_Array(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    //
    // `make block! 10` => creates array with certain initial capacity
    //
    if (IS_INTEGER(arg) || IS_DECIMAL(arg)) {
        Init_Any_Array(out, kind, Make_Array(Int32s(arg, 0)));
        return;
    }

    // !!! See #2263 -- Ren-C has unified MAKE and construction syntax.  A
    // block parameter to MAKE should be arity 2...the existing array for
    // the data source, and an offset from that array value's index:
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
    // !!! This could be eased to not require the index, but without it then
    // it can be somewhat confusing as to why [[a b c]] is needed instead of
    // just [a b c] as the construction spec.
    //
    if (ANY_ARRAY(arg)) {
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

        REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(arg), any_array);
        Init_Any_Series_At_Core(
            out,
            kind,
            SER(VAL_ARRAY(any_array)),
            index,
            derived
        );

        // !!! Previously this code would clear line break options on path
        // elements, using `CLEAR_VAL_FLAG(..., VALUE_FLAG_LINE)`.  But if
        // arrays are allowed to alias each others contents, the aliasing
        // via MAKE shouldn't modify the store.  Line marker filtering out of
        // paths should be part of the MOLDing logic -or- a path with embedded
        // line markers should use construction syntax to preserve them.

        return;
    }

    // !!! In R3-Alpha, MAKE and TO handled all cases except INTEGER!
    // and TYPESET! in the same way.  Ren-C switches MAKE of ANY-ARRAY!
    // to be special (in order to compatible with construction syntax),
    // continues the special treatment of INTEGER! by MAKE to mean
    // a size, and disallows MAKE TYPESET!.  This is a practical matter
    // of addressing changes in #2263 and keeping legacy working, as
    // opposed to endorsing any rationale in R3-Alpha's choices.
    //
    if (IS_TYPESET(arg))
        goto bad_make;

    TO_Array(out, kind, arg);
    return;

bad_make:
    fail (Error_Bad_Make(kind, arg));
}


//
//  TO_Array: C
//
void TO_Array(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    if (IS_TYPESET(arg)) {
        //
        // This makes a block of types out of a typeset.  Previously it was
        // restricted to only BLOCK!, now it lets you turn a typeset into
        // a GROUP! or a PATH!, etc.
        //
        Init_Any_Array(out, kind, Typeset_To_Array(arg));
    }
    else if (ANY_ARRAY(arg)) {
        //
        // `to group! [1 2 3]` etc. -- copy the array data at the index
        // position and change the type.  (Note: MAKE does not copy the
        // data, but aliases it under a new kind.)
        //
        Init_Any_Array(
            out,
            kind,
            Copy_Values_Len_Shallow(
                VAL_ARRAY_AT(arg), VAL_SPECIFIER(arg), VAL_ARRAY_LEN_AT(arg)
            )
        );
    }
    else if (IS_STRING(arg)) {
        //
        // `to block! "some string"` historically scans the source, so you
        // get an unbound code array.  Because the string may contain REBUNI
        // characters, it may have to be converted to UTF8 before being
        // used with the scanner.
        //
        REBSIZ offset;
        REBSIZ size;
        REBSER *temp = Temp_UTF8_At_Managed(
            &offset, &size, arg, VAL_LEN_AT(arg)
        );
        PUSH_GUARD_SERIES(temp);
        REBSTR * const filename = Canon(SYM___ANONYMOUS__);
        Init_Any_Array(
            out,
            kind,
            Scan_UTF8_Managed(filename, BIN_AT(temp, offset), size)
        );
        DROP_GUARD_SERIES(temp);
    }
    else if (IS_BINARY(arg)) {
        //
        // `to block! #{00BDAE....}` assumes the binary data is UTF8, and
        // goes directly to the scanner to make an unbound code array.
        //
        REBSTR * const filename = Canon(SYM___ANONYMOUS__);
        Init_Any_Array(
            out,
            kind,
            Scan_UTF8_Managed(filename, VAL_BIN_AT(arg), VAL_LEN_AT(arg))
        );
    }
    else if (IS_MAP(arg)) {
        Init_Any_Array(out, kind, Map_To_Array(VAL_MAP(arg), 0));
    }
    else if (ANY_CONTEXT(arg)) {
        Init_Any_Array(out, kind, Context_To_Array(VAL_CONTEXT(arg), 3));
    }
    else if (IS_VECTOR(arg)) {
        Init_Any_Array(out, kind, Vector_To_Array(arg));
    }
    else {
        // !!! The general case of not having any special conversion behavior
        // in R3-Alpha is just to fall through to making a 1-element block
        // containing the value.  This may seem somewhat random, and an
        // error may be preferable.
        //
        Init_Any_Array(out, kind, Copy_Values_Len_Shallow(arg, SPECIFIED, 1));
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
    REBOOL cased;
    REBOOL reverse;
    REBCNT offset;
    REBVAL *comparator;
    REBOOL all; // !!! not used?
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

    const REBOOL fully = TRUE; // error if not all arguments consumed

    DECLARE_LOCAL (result);
    if (Apply_Only_Throws(
        result,
        fully,
        flags->comparator,
        flags->reverse ? v1 : v2,
        flags->reverse ? v2 : v1,
        END
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
    REBOOL ccase,
    REBVAL *skipv,
    REBVAL *compv,
    REBVAL *part,
    REBOOL all,
    REBOOL rev
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
        assert(IS_VOID(compv));
        flags.comparator = NULL;
        flags.offset = 0;
    }

    // Determine length of sort:
    REBCNT len;
    Partial1(block, part, &len);
    if (len <= 1)
        return;

    // Skip factor:
    REBCNT skip;
    if (!IS_VOID(skipv)) {
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
void Shuffle_Block(REBVAL *value, REBOOL secure)
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
//     PD_Path
//     PD_Get_Path
//     PD_Set_Path
//     PD_Lit_Path
//
REB_R PD_Array(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval)
{
    REBCNT n = NOT_FOUND; // -1

    /* Issues!!!
        a/1.3
        a/not-found: 10 error or append?
        a/not-followed: 10 error or append?
    */

    if (IS_INTEGER(picker)) {
        n = Int32(picker) + VAL_INDEX(pvs->out) - 1;
    }
    else if (IS_WORD(picker)) {
        //
        // Linear search to find ANY-WORD! matching the canon.

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
        // other values:
        n = 1 + Find_In_Array_Simple(
            VAL_ARRAY(pvs->out),
            VAL_INDEX(pvs->out),
            picker
        );
    }

    if (n == NOT_FOUND || n >= VAL_LEN_HEAD(pvs->out)) {
        if (opt_setval != NULL)
            return R_UNHANDLED;

        Init_Void(pvs->out);
        return R_OUT;
    }

    if (opt_setval != NULL)
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(pvs->out));

    Init_Reference(
        pvs->out,
        VAL_ARRAY_AT_HEAD(pvs->out, n),
        VAL_SPECIFIER(pvs->out)
    );

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
        Init_Void(out);
        return NULL;
    }

    RELVAL *slot = VAL_ARRAY_AT_HEAD(block, n);
    Derelativize(out, slot, VAL_SPECIFIER(block));
    return slot;
}


//
//  MF_Array: C
//
void MF_Array(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    if (form && (IS_BLOCK(v) || IS_GROUP(v))) {
        Form_Array_At(mo, VAL_ARRAY(v), VAL_INDEX(v), 0);
        return;
    }

    REBOOL all;
    if (VAL_INDEX(v) == 0) { // "&& VAL_TYPE(v) <= REB_LIT_PATH" commented out
        //
        // Optimize when no index needed
        //
        all = FALSE;
    }
    else
        all = GET_MOLD_FLAG(mo, MOLD_FLAG_ALL);

    REBOOL over;
    if (VAL_INDEX(v) >= VAL_LEN_HEAD(v)) {
        //
        // If out of range, do not cause error to avoid error looping.
        //
        over = TRUE; // Force it into []
    }
    else
        over = FALSE;

    if (all || (over && !IS_BLOCK(v) && !IS_GROUP(v))) {
        SET_MOLD_FLAG(mo, MOLD_FLAG_ALL);
        Pre_Mold(mo, v); // #[block! part

        Append_Utf8_Codepoint(mo->series, '[');
        Mold_Array_At(mo, VAL_ARRAY(v), 0, 0);
        Post_Mold(mo, v);
        Append_Utf8_Codepoint(mo->series, ']');
    }
    else {
        const char *sep;

        switch(VAL_TYPE(v)) {
        case REB_BLOCK:
            if (GET_MOLD_FLAG(mo, MOLD_FLAG_ONLY)) {
                CLEAR_MOLD_FLAG(mo, MOLD_FLAG_ONLY); // only top level
                sep = "\000\000";
            }
            else
                sep = 0;
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
            sep = NULL;
        }

        if (over)
            Append_Unencoded(mo->series, sep ? sep : "[]");
        else
            Mold_Array_At(mo, VAL_ARRAY(v), VAL_INDEX(v), sep);

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
REBTYPE(Array)
{
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    // Common operations for any series type (length, head, etc.)
    {
        REB_R r = Series_Common_Action_Maybe_Unhandled(frame_, verb);
        if (r != R_UNHANDLED)
            return r;
    }

    // NOTE: Partial1() used below can mutate VAL_INDEX(value), be aware :-/
    //
    REBARR *array = VAL_ARRAY(value);
    REBCNT index = VAL_INDEX(value);
    REBSPC *specifier = VAL_SPECIFIER(value);

    switch (verb) {

    case SYM_TAKE_P: {
        INCLUDE_PARAMS_OF_TAKE_P;

        UNUSED(PAR(series));
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        REBCNT len;

        FAIL_IF_READ_ONLY_ARRAY(array);

        if (REF(part)) {
            Partial1(value, ARG(limit), &len);
            if (len == 0)
                goto return_empty_block;

            assert(VAL_LEN_HEAD(value) >= len);
        }
        else
            len = 1;

        index = VAL_INDEX(value); // /part can change index

        if (REF(last))
            index = VAL_LEN_HEAD(value) - len;

        if (index >= VAL_LEN_HEAD(value)) {
            if (not REF(part))
                return R_VOID;

            goto return_empty_block;
        }

        if (REF(part))
            Init_Block(
                D_OUT, Copy_Array_At_Max_Shallow(array, index, specifier, len)
            );
        else
            Derelativize(D_OUT, &ARR_HEAD(array)[index], specifier);

        Remove_Series(SER(array), index, len);
        return R_OUT;
    }

    //-- Search:

    case SYM_FIND:
    case SYM_SELECT: {
        INCLUDE_PARAMS_OF_FIND; // must be same as select

        const REB_R r_not_found = (verb == SYM_FIND) ? R_BLANK : R_VOID;

        UNUSED(PAR(series));
        UNUSED(PAR(value)); // aliased as arg

        REBINT len = ANY_ARRAY(arg) ? VAL_ARRAY_LEN_AT(arg) : 1;

        REBCNT limit;
        if (REF(part))
            Partial1(value, ARG(limit), &limit);
        else
            limit = VAL_LEN_HEAD(value);

        REBFLGS flags = (
            (REF(only) ? AM_FIND_ONLY : 0)
            | (REF(match) ? AM_FIND_MATCH : 0)
            | (REF(reverse) ? AM_FIND_REVERSE : 0)
            | (REF(case) ? AM_FIND_CASE : 0)
            | (REF(last) ? AM_FIND_LAST : 0)
        );

        REBCNT skip = REF(skip) ? Int32s(ARG(size), 1) : 1;

        REBCNT ret = Find_In_Array(
            array, index, limit, arg, len, flags, skip
        );

        if (ret >= limit)
            return r_not_found;

        if (REF(only))
            len = 1;

        if (verb == SYM_FIND) {
            if (REF(tail) || REF(match))
                ret += len;
            VAL_INDEX(value) = ret;
            Move_Value(D_OUT, value);
        }
        else {
            ret += len;
            if (ret >= limit)
                return r_not_found;

            Derelativize(D_OUT, ARR_AT(array, ret), specifier);
        }
        return R_OUT;
    }

    //-- Modification:
    case SYM_APPEND:
    case SYM_INSERT:
    case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;

        UNUSED(PAR(series));
        UNUSED(PAR(value));

        // Length of target (may modify index): (arg can be anything)
        //
        REBCNT len;
        Partial1(
            (verb == SYM_CHANGE)
                ? value
                : arg,
            ARG(limit),
            &len
        );

        FAIL_IF_READ_ONLY_ARRAY(array);
        index = VAL_INDEX(value);

        REBFLGS flags = 0;
        if (REF(only))
            flags |= AM_ONLY;
        if (REF(part))
            flags |= AM_PART;

        index = Modify_Array(
            verb,
            array,
            index,
            arg,
            flags,
            len,
            REF(dup) ? Int32(ARG(count)) : 1
        );
        VAL_INDEX(value) = index;
        Move_Value(D_OUT, value);
        return R_OUT;
    }

    case SYM_CLEAR: {
        FAIL_IF_READ_ONLY_ARRAY(array);
        if (index < VAL_LEN_HEAD(value)) {
            if (index == 0) Reset_Array(array);
            else {
                SET_END(ARR_AT(array, index));
                SET_SERIES_LEN(VAL_SERIES(value), cast(REBCNT, index));
            }
        }
        Move_Value(D_OUT, value);
        return R_OUT;
    }

    //-- Creation:

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        REBU64 types = 0;
        REBCNT tail = 0;

        UNUSED(REF(part));
        Partial1(value, ARG(limit), &tail); // may change VAL_INDEX
        tail += VAL_INDEX(value);

        if (REF(deep))
            types |= REF(types) ? 0 : TS_STD_SERIES;

        if (REF(types)) {
            if (IS_DATATYPE(ARG(kinds)))
                types |= FLAGIT_KIND(VAL_TYPE(ARG(kinds)));
            else
                types |= VAL_TYPESET_BITS(ARG(kinds));
        }

        REBARR *copy = Copy_Array_Core_Managed(
            array,
            VAL_INDEX(value), // at
            specifier,
            tail, // tail
            0, // extra
            ARRAY_FLAG_FILE_LINE, // flags
            types // types to copy deeply
        );
        Init_Any_Array(D_OUT, VAL_TYPE(value), copy);
        return R_OUT;
    }

    //-- Special actions:

    case SYM_SWAP: {
        if (not ANY_ARRAY(arg))
            fail (Error_Invalid(arg));

        FAIL_IF_READ_ONLY_ARRAY(array);
        FAIL_IF_READ_ONLY_ARRAY(VAL_ARRAY(arg));

        if (
            index < VAL_LEN_HEAD(value)
            && VAL_INDEX(arg) < VAL_LEN_HEAD(arg)
        ){
            // RELVAL bits can be copied within the same array
            //
            RELVAL *a = VAL_ARRAY_AT(value);
            RELVAL temp;
            temp.header = a->header;
            temp.payload = a->payload;
            temp.extra = a->extra;
            Blit_Cell(VAL_ARRAY_AT(value), VAL_ARRAY_AT(arg));
            Blit_Cell(VAL_ARRAY_AT(arg), &temp);
        }
        Move_Value(D_OUT, D_ARG(1));
        return R_OUT;
    }

    case SYM_REVERSE: {
        REBCNT len;
        Partial1(value, D_ARG(3), &len);

        FAIL_IF_READ_ONLY_ARRAY(array);

        if (len != 0) {
            //
            // RELVAL bits may be copied from slots within the same array
            //
            RELVAL *front = VAL_ARRAY_AT(value);
            RELVAL *back = front + len - 1;
            for (len /= 2; len > 0; --len, ++front, --back) {
                RELVAL temp;
                temp.header = front->header;
                temp.payload = front->payload;
                temp.extra = front->extra;
                Blit_Cell(front, back);
                Blit_Cell(back, &temp);
            }
        }
        Move_Value(D_OUT, D_ARG(1));
        return R_OUT;
    }

    case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        UNUSED(PAR(series));
        UNUSED(REF(part)); // checks limit as void
        UNUSED(REF(skip)); // checks size as void
        UNUSED(REF(compare)); // checks comparator as void

        FAIL_IF_READ_ONLY_ARRAY(array);

        Sort_Block(
            value,
            REF(case),
            ARG(size), // skip size (may be void if no /SKIP)
            ARG(comparator), // (may be void if no /COMPARE)
            ARG(limit), // (may be void if no /PART)
            REF(all),
            REF(reverse)
        );
        Move_Value(D_OUT, value);
        return R_OUT;
    }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(seed))
            fail (Error_Bad_Refines_Raw());

        if (REF(only)) { // pick an element out of the array
            if (index >= VAL_LEN_HEAD(value))
                return R_BLANK;

            Init_Integer(
                ARG(seed),
                1 + (Random_Int(REF(secure)) % (VAL_LEN_HEAD(value) - index))
            );

            RELVAL *slot = Pick_Block(D_OUT, value, ARG(seed));
            if (IS_VOID(D_OUT)) {
                assert(slot == NULL);
                UNUSED(slot);
                return R_VOID;
            }
            return R_OUT;

        }

        Shuffle_Block(value, REF(secure));
        Move_Value(D_OUT, value);
        return R_OUT;
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

return_empty_block:
    Init_Block(D_OUT, Make_Array(0));
    return R_OUT;
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

    if (NOT_SER_FLAG(a, SERIES_FLAG_ARRAY))
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

    if (GET_SER_INFO(a, SERIES_INFO_HAS_DYNAMIC)) {
        REBCNT rest = SER_REST(SER(a));

        assert(rest > 0 && rest > i);
        for (; i < rest - 1; ++i, ++item) {
            if (not (item->header.bits & NODE_FLAG_CELL)) {
                printf("Unwritable cell found in array rest capacity\n");
                panic (a);
            }
        }
        assert(item == ARR_AT(a, rest - 1));

        RELVAL *ultimate = ARR_AT(a, rest - 1);
        if (NOT_END(ultimate) || (ultimate->header.bits & NODE_FLAG_CELL)) {
            printf("Implicit termination/unwritable END missing from array\n");
            panic (a);
        }
    }

}
#endif
