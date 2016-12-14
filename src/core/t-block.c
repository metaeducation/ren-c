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
// Copyright 2012-2016 Rebol Open Source Contributors
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
    REBINT num;

    num = Cmp_Array(a, b, LOGICAL(mode == 1));
    if (mode >= 0) return (num == 0);
    if (mode == -1) return (num >= 0);
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
        Val_Init_Array(out, kind, Make_Array(Int32s(arg, 0)));
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

        Val_Init_Series_Index_Core(
            out,
            kind,
            ARR_SERIES(VAL_ARRAY(any_array)),
            index,
            IS_SPECIFIC(any_array)
                ? VAL_SPECIFIER(KNOWN(any_array))
                : VAL_SPECIFIER(arg)
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
        Val_Init_Array(out, kind, Typeset_To_Array(arg));
    }
    else if (ANY_ARRAY(arg)) {
        //
        // `to group! [1 2 3]` etc. -- copy the array data at the index
        // position and change the type.  (Note: MAKE does not copy the
        // data, but aliases it under a new kind.)
        //
        Val_Init_Array(
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
        REBCNT index;
        REBSER *utf8 = Temp_Bin_Str_Managed(arg, &index, NULL);
        PUSH_GUARD_SERIES(utf8);
        Val_Init_Array(
            out,
            kind,
            Scan_UTF8_Managed(BIN_HEAD(utf8), BIN_LEN(utf8))
        );
        DROP_GUARD_SERIES(utf8);
    }
    else if (IS_BINARY(arg)) {
        //
        // `to block! #{00BDAE....}` assumes the binary data is UTF8, and
        // goes directly to the scanner to make an unbound code array.
        //
        Val_Init_Array(
            out, kind, Scan_UTF8_Managed(VAL_BIN_AT(arg), VAL_LEN_AT(arg))
        );
    }
    else if (IS_MAP(arg)) {
        Val_Init_Array(out, kind, Map_To_Array(VAL_MAP(arg), 0));
    }
    else if (ANY_CONTEXT(arg)) {
        Val_Init_Array(out, kind, Context_To_Array(VAL_CONTEXT(arg), 3));
    }
    else if (IS_VECTOR(arg)) {
        Val_Init_Array(out, kind, Vector_To_Array(arg));
    }
    else {
        // !!! The general case of not having any special conversion behavior
        // in R3-Alpha is just to fall through to making a 1-element block
        // containing the value.  This may seem somewhat random, and an
        // error may be preferable.
        //
        Val_Init_Array(out, kind, Copy_Values_Len_Shallow(arg, SPECIFIED, 1));
    }
}


//
//  Find_In_Array: C
//
// Flags are set according to: ALL_FIND_REFS
//
// Main Parameters:
// start - index to start search
// end   - ending position
// len   - length of target
// skip  - skip factor
// dir   - direction
//
// Comparison Parameters:
// case  - case sensitivity
// wild  - wild cards/keys
//
// Final Parmameters:
// tail  - tail position
// match - sequence
// SELECT - (value that follows)
//
REBCNT Find_In_Array(
    REBARR *array,
    REBCNT index,
    REBCNT end,
    const RELVAL *target,
    REBCNT len,
    REBFLGS flags,
    REBINT skip
) {
    RELVAL *value;
    RELVAL *val;
    REBCNT cnt;
    REBCNT start = index;

    if (flags & (AM_FIND_REVERSE | AM_FIND_LAST)) {
        skip = -1;
        start = 0;
        if (flags & AM_FIND_LAST) index = end - len;
        else index--;
    }

    // Optimized find word in block:
    if (ANY_WORD(target)) {
        for (; index >= start && index < end; index += skip) {
            value = ARR_AT(array, index);
            if (ANY_WORD(value)) {
                cnt = (VAL_WORD_SPELLING(value) == VAL_WORD_SPELLING(target));
                if (flags & AM_FIND_CASE) {
                    // Must be same type and spelling:
                    if (cnt && VAL_TYPE(value) == VAL_TYPE(target)) return index;
                }
                else {
                    // Can be different type or alias:
                    if (cnt || VAL_WORD_CANON(value) == VAL_WORD_CANON(target)) return index;
                }
            }
            if (flags & AM_FIND_MATCH) break;
        }
        return NOT_FOUND;
    }
    // Match a block against a block:
    else if (ANY_ARRAY(target) && !(flags & AM_FIND_ONLY)) {
        for (; index >= start && index < end; index += skip) {
            cnt = 0;
            value = ARR_AT(array, index);
            for (val = VAL_ARRAY_AT(target); NOT_END(val); val++, value++) {
                if (0 != Cmp_Value(value, val, LOGICAL(flags & AM_FIND_CASE)))
                    break;
                if (++cnt >= len) {
                    return index;
                }
            }
            if (flags & AM_FIND_MATCH) break;
        }
        return NOT_FOUND;
    }
    // Find a datatype in block:
    else if (IS_DATATYPE(target) || IS_TYPESET(target)) {
        for (; index >= start && index < end; index += skip) {
            value = ARR_AT(array, index);
            // Used if's so we can trace it...
            if (IS_DATATYPE(target)) {
                if (VAL_TYPE(value) == VAL_TYPE_KIND(target)) return index;
                if (IS_DATATYPE(value) && VAL_TYPE_KIND(value) == VAL_TYPE_KIND(target)) return index;
            }
            if (IS_TYPESET(target)) {
                if (TYPE_CHECK(target, VAL_TYPE(value))) return index;
                if (IS_DATATYPE(value) && TYPE_CHECK(target, VAL_TYPE_KIND(value))) return index;
                if (IS_TYPESET(value) && EQUAL_TYPESET(value, target)) return index;
            }
            if (flags & AM_FIND_MATCH) break;
        }
        return NOT_FOUND;
    }
    // All other cases:
    else {
        for (; index >= start && index < end; index += skip) {
            value = ARR_AT(array, index);
            if (
                0 == Cmp_Value(
                    value, target, LOGICAL(flags & AM_FIND_CASE)
                )
            ) {
                return index;
            }

            if (flags & AM_FIND_MATCH) break;
        }
        return NOT_FOUND;
    }
}


// WARNING! Not re-entrant. !!!  Must find a way to push it on stack?
// Fields initialized to zero due to global scope
static struct {
    REBOOL cased;
    REBOOL reverse;
    REBCNT offset;
    REBVAL *compare;
} sort_flags;


//
//  Compare_Val: C
//
static int Compare_Val(void *thunk, const void *v1, const void *v2)
{
    // !!!! BE SURE that 64 bit large difference comparisons work

    if (sort_flags.reverse)
        return Cmp_Value(
            cast(const RELVAL*, v2) + sort_flags.offset,
            cast(const RELVAL*, v1) + sort_flags.offset,
            sort_flags.cased
        );
    else
        return Cmp_Value(
            cast(const RELVAL*, v1) + sort_flags.offset,
            cast(const RELVAL*, v2) + sort_flags.offset,
            sort_flags.cased
        );

/*
    REBI64 n = VAL_INT64((REBVAL*)v1) - VAL_INT64((REBVAL*)v2);
    if (n > 0) return 1;
    if (n < 0) return -1;
    return 0;
*/
}


//
//  Compare_Call: C
//
static int Compare_Call(void *thunk, const void *v1, const void *v2)
{
    RELVAL *args = NULL;
    REBINT tristate = -1;
    const void *tmp = NULL;

    REBVAL result;

    if (!sort_flags.reverse) { /*swap v1 and v2 */
        tmp = v1;
        v1 = v2;
        v2 = tmp;
    }

    args = ARR_AT(VAL_FUNC_PARAMLIST(sort_flags.compare), 1);
    if (NOT_END(args) && !TYPE_CHECK(args, VAL_TYPE(cast(const RELVAL*, v1)))) {
        fail (Error(
            RE_EXPECT_ARG,
            Type_Of(sort_flags.compare),
            args,
            Type_Of(cast(const RELVAL*, v1))
        ));
    }
    ++ args;
    if (NOT_END(args) && !TYPE_CHECK(args, VAL_TYPE(cast(const RELVAL*, v2)))) {
        fail (Error(
            RE_EXPECT_ARG,
            Type_Of(sort_flags.compare),
            args,
            Type_Of(cast(const RELVAL*, v2))
        ));
    }

    if (Apply_Only_Throws(
        &result,
        TRUE,
        sort_flags.compare,
        v1,
        v2,
        END_CELL
    )) {
        fail (Error_No_Catch_For_Throw(&result));
    }

    if (IS_LOGIC(&result)) {
        if (VAL_LOGIC(&result)) tristate = 1;
    }
    else if (IS_INTEGER(&result)) {
        if (VAL_INT64(&result) > 0) tristate = 1;
        if (VAL_INT64(&result) == 0) tristate = 0;
    }
    else if (IS_DECIMAL(&result)) {
        if (VAL_DECIMAL(&result) > 0) tristate = 1;
        if (VAL_DECIMAL(&result) == 0) tristate = 0;
    }
    else if (IS_CONDITIONAL_TRUE(&result)) tristate = 1;

    return tristate;
}


//
//  Sort_Block: C
//
// series [any-series!]
// /case {Case sensitive sort}
// /skip {Treat the series as records of fixed size}
// size [integer!] {Size of each record}
// /compare  {Comparator offset, block or function}
// comparator [integer! block! function!]
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
    REBCNT len;
    REBCNT skip = 1;
    REBCNT size = sizeof(REBVAL);
//  int (*sfunc)(const void *v1, const void *v2);

    sort_flags.cased = ccase;
    sort_flags.reverse = rev;
    sort_flags.compare = 0;
    sort_flags.offset = 0;

    if (IS_INTEGER(compv)) sort_flags.offset = Int32(compv)-1;
    if (IS_FUNCTION(compv)) sort_flags.compare = compv;

    // Determine length of sort:
    Partial1(block, part, &len);
    if (len <= 1) return;

    // Skip factor:
    if (!IS_VOID(skipv)) {
        skip = Get_Num_From_Arg(skipv);
        if (skip <= 0 || len % skip != 0 || skip > len)
            fail (Error_Out_Of_Range(skipv));
    }

    // Use fast quicksort library function:
    if (skip > 1) len /= skip, size *= skip;

    if (sort_flags.compare)
        reb_qsort_r(VAL_ARRAY_AT(block), len, size, NULL, Compare_Call);
    else
        reb_qsort_r(VAL_ARRAY_AT(block), len, size, NULL, Compare_Val);

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
        swap = data[k];
        data[k] = data[n + idx];
        data[n + idx] = swap;
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
REBINT PD_Array(REBPVS *pvs)
{
    REBINT n = 0;

    /* Issues!!!
        a/1.3
        a/not-found: 10 error or append?
        a/not-followed: 10 error or append?
    */

    if (IS_INTEGER(pvs->selector)) {
        n = Int32(pvs->selector) + VAL_INDEX(pvs->value) - 1;
    }
    else if (IS_WORD(pvs->selector)) {
        n = Find_Word_In_Array(
            VAL_ARRAY(pvs->value),
            VAL_INDEX(pvs->value),
            VAL_WORD_CANON(pvs->selector)
        );
        if (cast(REBCNT, n) != NOT_FOUND) n++;
    }
    else {
        // other values:
        n = 1 + Find_In_Array_Simple(
            VAL_ARRAY(pvs->value),
            VAL_INDEX(pvs->value),
            pvs->selector
        );
    }

    if (n < 0 || cast(REBCNT, n) >= VAL_LEN_HEAD(pvs->value)) {
        if (pvs->opt_setval)
            fail (Error_Bad_Path_Select(pvs));

        return PE_NONE;
    }

    if (pvs->opt_setval)
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(pvs->value));

    pvs->value_specifier = IS_SPECIFIC(pvs->value)
        ? VAL_SPECIFIER(const_KNOWN(pvs->value))
        : pvs->value_specifier;

    pvs->value = VAL_ARRAY_AT_HEAD(pvs->value, n);

#if !defined(NDEBUG)
    if (pvs->value_specifier == SPECIFIED && IS_RELATIVE(pvs->value)) {
        Debug_Fmt("Relative value found in PD_Array with no specifier");
        PROBE_MSG(pvs->value, "the value");
        Panic_Array(VAL_ARRAY(pvs->value));
        assert(FALSE);
    }
#endif

    return PE_SET_IF_END;
}


//
//  Pick_Block: C
//
// Fills out with void if no pick.
//
RELVAL *Pick_Block(REBVAL *out, const REBVAL *block, const REBVAL *selector)
{
    REBINT n = 0;

    n = Get_Num_From_Arg(selector);
    n += VAL_INDEX(block) - 1;
    if (n < 0 || cast(REBCNT, n) >= VAL_LEN_HEAD(block)) {
        SET_VOID(out);
        return NULL;
    }
    else {
        RELVAL *slot = VAL_ARRAY_AT_HEAD(block, n);
        Derelativize(out, slot, VAL_SPECIFIER(block));
        return slot;
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
        REB_R r;
        if (Series_Common_Action_Returns(&r, frame_, action))
            return r;
    }

    // NOTE: Partial1() used below can mutate VAL_INDEX(value), be aware :-/
    //
    REBARR *array = VAL_ARRAY(value);
    REBINT index = cast(REBINT, VAL_INDEX(value));
    REBCTX *specifier = VAL_SPECIFIER(value);

    switch (action) {
    case SYM_POKE:
    case SYM_PICK: {
        RELVAL *slot;
    pick_using_arg:
        slot = Pick_Block(D_OUT, value, arg);
        if (action == SYM_PICK) {
            if (IS_VOID(D_OUT)) {
                assert(!slot);
                return R_VOID;
            }
        } else {
            FAIL_IF_LOCKED_ARRAY(array);
            if (IS_VOID(D_OUT)) {
                assert(!slot);
                fail (Error_Out_Of_Range(arg));
            }
            arg = D_ARG(3);
            *slot = *arg;
            *D_OUT = *arg;
        }
        return R_OUT;
    }

    case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        REBCNT len;

        FAIL_IF_LOCKED_ARRAY(array);

        if (REF(part)) {
            Partial1(value, ARG(limit), &len);
            if (len == 0)
                goto return_empty_block;
        } else
            len = 1;

        index = VAL_INDEX(value); // /part can change index

        if (REF(last))
            index = VAL_LEN_HEAD(value) - len;

        if (index < 0 || index >= cast(REBINT, VAL_LEN_HEAD(value))) {
            if (NOT(REF(part)))
                return R_VOID;

            goto return_empty_block;
        }

        if (REF(part))
            Val_Init_Block(
                D_OUT, Copy_Array_At_Max_Shallow(array, index, specifier, len)
            );
        else
            Derelativize(D_OUT, &ARR_HEAD(array)[index], specifier);

        Remove_Series(ARR_SERIES(array), index, len);
        return R_OUT;
    }

    //-- Search:

    case SYM_FIND:
    case SYM_SELECT: {
        INCLUDE_PARAMS_OF_FIND;

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
        );

        REBCNT skip = REF(skip) ? Int32s(ARG(size), 1) : 1;

        REBCNT ret = Find_In_Array(
            array, index, limit, arg, len, flags, skip
        );

        if (ret >= limit) {
            if (action == SYM_FIND)
                return R_BLANK;
            return R_VOID;
        }

        if (REF(only))
            len = 1;

        if (action == SYM_FIND) {
            if (REF(tail) || REF(match))
                ret += len;
            VAL_INDEX(value) = ret;
            *D_OUT = *value;
        }
        else {
            ret += len;
            if (ret >= limit) {
                if (action == SYM_FIND)
                    return R_BLANK;
                return R_VOID;
            }
            Derelativize(D_OUT, ARR_AT(array, ret), specifier);
        }
        return R_OUT;
    }

    //-- Modification:
    case SYM_APPEND:
    case SYM_INSERT:
    case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;

        // Length of target (may modify index): (arg can be anything)
        //
        REBCNT len;
        Partial1(
            (action == SYM_CHANGE)
                ? value
                : arg,
            ARG(limit),
            &len
        );

        FAIL_IF_LOCKED_ARRAY(array);
        index = VAL_INDEX(value);

        REBFLGS flags = 0;
        if (REF(only))
            flags |= AM_ONLY;
        if (REF(part))
            flags |= AM_PART;

        index = Modify_Array(
            action,
            array,
            index,
            arg,
            flags,
            len,
            REF(dup) ? Int32(ARG(count)) : 1
        );
        VAL_INDEX(value) = index;
        *D_OUT = *value;
        return R_OUT;
    }

    case SYM_CLEAR: {
        FAIL_IF_LOCKED_ARRAY(array);
        if (index < cast(REBINT, VAL_LEN_HEAD(value))) {
            if (index == 0) Reset_Array(array);
            else {
                SET_END(ARR_AT(array, index));
                SET_SERIES_LEN(VAL_SERIES(value), cast(REBCNT, index));
            }
        }
        *D_OUT = *value;
        return R_OUT;
    }

    //-- Creation:

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        REBU64 types = 0;
        REBCNT tail = 0;
        index = VAL_INDEX(value);
        Partial1(value, ARG(limit), &tail);
        tail += index;

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
            REF(deep), // deep
            types // types
        );
        Val_Init_Array(D_OUT, VAL_TYPE(value), copy);
        return R_OUT;
    }

    //-- Special actions:

    case SYM_TRIM: {
        INCLUDE_PARAMS_OF_TRIM;
        FAIL_IF_LOCKED_ARRAY(array);

        if (REF(auto) || REF(with) || REF(all) || REF(lines))
            fail (Error(RE_BAD_REFINES));

        RELVAL *head = ARR_HEAD(array);
        REBCNT out = index;
        REBCNT end = ARR_LEN(array);

        if (REF(tail)) {
            for (; end >= (index + 1); end--) {
                if (VAL_TYPE(head + end - 1) != REB_BLANK)
                    break;
            }
            Remove_Series(ARR_SERIES(array), end, ARR_LEN(array) - end);

            // if (!(flags & AM_TRIM_HEAD) || index >= end) return;
        }

        if (REF(head)) {
            for (; index < end; index++) {
                if (VAL_TYPE(head + index) != REB_BLANK) break;
            }
            Remove_Series(ARR_SERIES(array), out, index - out);
        }

        if (NOT(REF(head) || REF(tail))) {
            for (; index < end; index++) {
                if (VAL_TYPE(head + index) != REB_BLANK) {
                    //
                    // Rare case of legal RELVAL bit copying... from one slot
                    // in an array to another in that same array.
                    //
                    *ARR_AT(array, out) = head[index];
                    out++;
                }
            }
            Remove_Series(ARR_SERIES(array), out, end - out);
        }

        *D_OUT = *value;
        return R_OUT;
    }

    case SYM_SWAP: {
        if (!ANY_ARRAY(arg))
            fail (Error_Invalid_Arg(arg));

        FAIL_IF_LOCKED_ARRAY(array);
        FAIL_IF_LOCKED_ARRAY(VAL_ARRAY(arg));

        if (
            index < cast(REBINT, VAL_LEN_HEAD(value))
            && VAL_INDEX(arg) < VAL_LEN_HEAD(arg)
        ) {
            // RELVAL bits can be copied within the same array
            //
            RELVAL temp = *VAL_ARRAY_AT(value);
            *VAL_ARRAY_AT(value) = *VAL_ARRAY_AT(arg);
            *VAL_ARRAY_AT(arg) = temp;
        }
        *D_OUT = *D_ARG(1);
        return R_OUT;
    }

    case SYM_REVERSE: {
        REBCNT len;
        Partial1(value, D_ARG(3), &len);

        FAIL_IF_LOCKED_ARRAY(array);

        if (len != 0) {
            //
            // RELVAL bits may be copied from slots within the same array
            //
            RELVAL *front = VAL_ARRAY_AT(value);
            RELVAL *back = front + len - 1;
            for (len /= 2; len > 0; len--) {
                RELVAL temp = *front;
                *front++ = *back;
                *back-- = temp;
            }
        }
        *D_OUT = *D_ARG(1);
        return R_OUT;
    }

    case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        FAIL_IF_LOCKED_ARRAY(array);
        Sort_Block(
            value,
            REF(case),
            ARG(size), // skip size (may be void if no /SKIP)
            ARG(comparator), // (may be void if no /COMPARE)
            ARG(limit), // (may be void if no /PART)
            REF(all),
            REF(reverse)
        );
        *D_OUT = *value;
        return R_OUT;
    }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        if (REF(seed))
            fail (Error(RE_BAD_REFINES));

        if (REF(only)) { // pick an element out of the array
            if (index >= cast(REBINT, VAL_LEN_HEAD(value)))
                return R_BLANK;

            SET_INTEGER(
                ARG(seed),
                1 + (Random_Int(REF(secure)) % (VAL_LEN_HEAD(value) - index))
            );
            arg = ARG(seed); // argument to pick
            action = SYM_PICK;
            goto pick_using_arg;
        }

        Shuffle_Block(value, REF(secure));
        *D_OUT = *value;
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

    return T_Port(frame_, action);

return_empty_block:
    Val_Init_Block(D_OUT, Make_Array(0));
    return R_OUT;
}


#if !defined(NDEBUG)

//
//  Assert_Array_Core: C
//
void Assert_Array_Core(REBARR *array)
{
    // Basic integrity checks (series is not marked free, etc.)  Note that
    // we don't use ASSERT_SERIES the macro here, because that checks to
    // see if the series is an array...and if so, would call this routine
    //
    Assert_Series_Core(ARR_SERIES(array));

    if (NOT(GET_ARR_FLAG(array, SERIES_FLAG_ARRAY))) {
        printf("Assert_Array called on series without SERIES_FLAG_ARRAY\n");
        Panic_Array(array);
    }

    RELVAL *value = ARR_HEAD(array);
    REBCNT i;
    for (i = 0; i < ARR_LEN(array); ++i, ++value) {
        if (IS_END(value)) {
            printf("Premature END found in Assert_Array\n");
            printf("At index %d, length is %d\n", i, ARR_LEN(array));
            fflush(stdout);
            Panic_Array(array);
        }
    }

    if (NOT_END(value)) {
        printf("END missing in Assert_Array, length is %d\n", ARR_LEN(array));
        fflush(stdout);
        Panic_Array(array);
    }

    if (GET_ARR_FLAG(array, SERIES_FLAG_HAS_DYNAMIC)) {
        REBCNT rest = SER_REST(ARR_SERIES(array));

#ifdef __cplusplus
        assert(rest > 0 && rest > i);
        for (; i < rest - 1; ++i, ++value) {
            if (NOT(value->header.bits & NOT_FREE_MASK)) {
                printf("Unwritable cell found in array rest capacity\n");
                fflush(stdout);
                Panic_Array(array);
            }
        }
        assert(value == ARR_AT(array, rest - 1));
#endif
        if (ARR_AT(array, rest - 1)->header.bits != END_MASK) {
            printf("Implicit termination/unwritable END missing from array\n");
            fflush(stdout);
            assert(FALSE);
        }
    }

}
#endif
