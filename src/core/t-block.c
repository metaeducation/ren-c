/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Module:  t-block.c
**  Summary: block related datatypes
**  Section: datatypes
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"


//
//  CT_Array: C
// 
// "Compare Type" dispatcher for the following types:
// 
//     CT_Block(REBVAL *a, REBVAL *b, REBINT mode)
//     CT_Group(REBVAL *a, REBVAL *b, REBINT mode)
//     CT_Path(REBVAL *a, REBVAL *b, REBINT mode)
//     CT_Set_Path(REBVAL *a, REBVAL *b, REBINT mode)
//     CT_Get_Path(REBVAL *a, REBVAL *b, REBINT mode)
//     CT_Lit_Path(REBVAL *a, REBVAL *b, REBINT mode)
//
REBINT CT_Array(const REBVAL *a, const REBVAL *b, REBINT mode)
{
    REBINT num;

    if (mode == 3)
        return VAL_SERIES(a) == VAL_SERIES(b) && VAL_INDEX(a) == VAL_INDEX(b);

    num = Cmp_Block(a, b, LOGICAL(mode > 1));
    if (mode >= 0) return (num == 0);
    if (mode == -1) return (num >= 0);
    return (num > 0);
}

static void No_Nones(REBVAL *arg) {
    arg = VAL_ARRAY_AT(arg);
    for (; NOT_END(arg); arg++) {
        if (IS_NONE(arg)) fail (Error_Invalid_Arg(arg));
    }
}


//
//  MT_Array: C
// 
// "Make Type" dispatcher for the following subtypes:
// 
//     MT_Block
//     MT_Group
//     MT_Path
//     MT_Set_Path
//     MT_Get_Path
//     MT_Lit_Path
//
REBOOL MT_Array(REBVAL *out, REBVAL *data, enum Reb_Kind type)
{
    REBCNT i;

    if (!ANY_ARRAY(data)) return FALSE;
    if (type >= REB_PATH && type <= REB_LIT_PATH) {
        REBVAL *head = VAL_ARRAY_HEAD(data);
        if (IS_END(head) || !ANY_WORD(head))
            return FALSE;
    }

    *out = *data++;
    VAL_RESET_HEADER(out, type);

    // !!! This did not have special END handling previously, but it would have
    // taken the 0 branch.  Review if this is sensible.
    //
    i = NOT_END(data) && IS_INTEGER(data) ? Int32(data) - 1 : 0;

    if (i > VAL_LEN_HEAD(out)) i = VAL_LEN_HEAD(out); // clip it
    VAL_INDEX(out) = i;
    return TRUE;
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
    const REBVAL *target,
    REBCNT len,
    REBCNT flags,
    REBINT skip
) {
    REBVAL *value;
    REBVAL *val;
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
                cnt = (VAL_WORD_SYM(value) == VAL_WORD_SYM(target));
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
            if (0 == Cmp_Value(value, target, LOGICAL(flags & AM_FIND_CASE)))
                return index;
            if (flags & AM_FIND_MATCH) break;
        }
        return NOT_FOUND;
    }
}


//
//  Make_Block_Type: C
// 
// Arg can be:
//     1. integer (length of block)
//     2. block (copy it)
//     3. value (convert to a block)
//
void Make_Block_Type(REBVAL *out, enum Reb_Kind type, REBOOL make, REBVAL *arg)
{
    REBCNT len;
    REBARR *array;

    // make block! [1 2 3]
    if (ANY_ARRAY(arg)) {
        len = VAL_ARRAY_LEN_AT(arg);
        if (len > 0 && type >= REB_PATH && type <= REB_LIT_PATH)
            No_Nones(arg);
        array = Copy_Values_Len_Shallow(VAL_ARRAY_AT(arg), len);
        goto done;
    }

    if (IS_STRING(arg)) {
        REBCNT index, len = 0;
        VAL_SERIES(arg) = Temp_Bin_Str_Managed(arg, &index, &len);
        array = Scan_Source(VAL_BIN(arg), VAL_LEN_AT(arg));
        goto done;
    }

    if (IS_BINARY(arg)) {
        array = Scan_Source(VAL_BIN_AT(arg), VAL_LEN_AT(arg));
        goto done;
    }

    if (IS_MAP(arg)) {
        array = Map_To_Array(VAL_MAP(arg), 0);
        goto done;
    }

    if (ANY_CONTEXT(arg)) {
        array = Context_To_Array(VAL_CONTEXT(arg), 3);
        goto done;
    }

    if (IS_VECTOR(arg)) {
        array = Vector_To_Array(arg);
        goto done;
    }

//  if (make && IS_NONE(arg)) {
//      array = Make_Array(0);
//      goto done;
//  }

    // to block! typset
    if (!make && IS_TYPESET(arg) && type == REB_BLOCK) {
        Val_Init_Array(out, type, Typeset_To_Array(arg));
        return;
    }

    if (make) {
        // make block! 10
        if (IS_INTEGER(arg) || IS_DECIMAL(arg)) {
            len = Int32s(arg, 0);
            Val_Init_Array(out, type, Make_Array(len));
            return;
        }
        fail (Error_Invalid_Arg(arg));
    }

    array = Copy_Values_Len_Shallow(arg, 1);

done:
    Val_Init_Array(out, type, array);
    return;
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
            cast(const REBVAL*, v2) + sort_flags.offset,
            cast(const REBVAL*, v1) + sort_flags.offset,
            sort_flags.cased
        );
    else
        return Cmp_Value(
            cast(const REBVAL*, v1) + sort_flags.offset,
            cast(const REBVAL*, v2) + sort_flags.offset,
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
    REBVAL *args = NULL;
    REBINT tristate = -1;
    const void *tmp = NULL;

    REBVAL result;
    VAL_INIT_WRITABLE_DEBUG(&result);

    if (!sort_flags.reverse) { /*swap v1 and v2 */
        tmp = v1;
        v1 = v2;
        v2 = tmp;
    }

    args = ARR_AT(VAL_FUNC_PARAMLIST(sort_flags.compare), 1);
    if (NOT_END(args) && !TYPE_CHECK(args, VAL_TYPE(cast(const REBVAL*, v1)))) {
        fail (Error(
            RE_EXPECT_ARG,
            Type_Of(sort_flags.compare),
            args,
            Type_Of(cast(const REBVAL*, v1))
        ));
    }
    ++ args;
    if (NOT_END(args) && !TYPE_CHECK(args, VAL_TYPE(cast(const REBVAL*, v2)))) {
        fail (Error(
            RE_EXPECT_ARG,
            Type_Of(sort_flags.compare),
            args,
            Type_Of(cast(const REBVAL*, v2))
        ));
    }

    if (Apply_Only_Throws(
        &result,
        sort_flags.compare,
        v1,
        v2,
        END_VALUE
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
    if (ANY_FUNC(compv)) sort_flags.compare = compv;

    // Determine length of sort:
    len = Partial1(block, part);
    if (len <= 1) return;

    // Skip factor:
    if (!IS_UNSET(skipv)) {
        skip = Get_Num_Arg(skipv);
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
//  Trim_Array: C
// 
// See Trim_String().
//
static void Trim_Array(REBARR *array, REBCNT index, REBCNT flags)
{
    REBVAL *head = ARR_HEAD(array);
    REBCNT out = index;
    REBCNT end = ARR_LEN(array);

    if (flags & AM_TRIM_TAIL) {
        for (; end >= (index + 1); end--) {
            if (VAL_TYPE(head + end - 1) > REB_NONE) break;
        }
        Remove_Series(ARR_SERIES(array), end, ARR_LEN(array) - end);
        if (!(flags & AM_TRIM_HEAD) || index >= end) return;
    }

    if (flags & AM_TRIM_HEAD) {
        for (; index < end; index++) {
            if (VAL_TYPE(head + index) > REB_NONE) break;
        }
        Remove_Series(ARR_SERIES(array), out, index - out);
    }

    if (flags == 0) {
        for (; index < end; index++) {
            if (VAL_TYPE(head + index) > REB_NONE) {
                *ARR_AT(array, out) = head[index];
                out++;
            }
        }
        Remove_Series(ARR_SERIES(array), out, end - out);
    }
}


//
//  Shuffle_Block: C
//
void Shuffle_Block(REBVAL *value, REBOOL secure)
{
    REBCNT n;
    REBCNT k;
    REBCNT idx = VAL_INDEX(value);
    REBVAL *data = VAL_ARRAY_HEAD(value);
    REBVAL swap;

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
//     PD_Block(REBPVS *pvs)
//     PD_Group(REBPVS *pvs)
//     PD_Path(REBPVS *pvs)
//     PD_Get_Path(REBPVS *pvs)
//     PD_Set_Path(REBPVS *pvs)
//     PD_Lit_Path(REBPVS *pvs)
//
REBINT PD_Array(REBPVS *pvs)
{
    REBINT n = 0;

    /* Issues!!!
        a/1.3
        a/not-found: 10 error or append?
        a/not-followed: 10 error or append?
    */

    if (IS_INTEGER(pvs->select)) {
        n = Int32(pvs->select) + VAL_INDEX(pvs->value) - 1;
    }
    else if (IS_WORD(pvs->select)) {
        n = Find_Word_In_Array(
            VAL_ARRAY(pvs->value),
            VAL_INDEX(pvs->value),
            VAL_WORD_CANON(pvs->select)
        );
        if (cast(REBCNT, n) != NOT_FOUND) n++;
    }
    else {
        // other values:
        n = 1 + Find_In_Array_Simple(
            VAL_ARRAY(pvs->value),
            VAL_INDEX(pvs->value),
            pvs->select
        );
    }

    if (n < 0 || cast(REBCNT, n) >= VAL_LEN_HEAD(pvs->value)) {
        if (pvs->setval) return PE_BAD_SELECT;
        return PE_NONE;
    }

    if (pvs->setval) FAIL_IF_LOCKED_SERIES(VAL_SERIES(pvs->value));
    pvs->value = VAL_ARRAY_AT_HEAD(pvs->value, n);
    // if valset - check PROTECT on block
    //if (NOT_END(pvs->path+1)) Next_Path(pvs); return PE_OK;
    return PE_SET;
}


//
//  Pick_Block: C
//
REBVAL *Pick_Block(REBVAL *block, REBVAL *selector)
{
    REBINT n = 0;

    n = Get_Num_Arg(selector);
    n += VAL_INDEX(block) - 1;
    if (n < 0 || cast(REBCNT, n) >= VAL_LEN_HEAD(block)) return 0;
    return VAL_ARRAY_AT_HEAD(block, n);
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
    REBVAL  *value = D_ARG(1);
    REBVAL  *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    REBARR *array;
    REBINT  index;
    REBINT  tail;
    REBINT  len;
    REBCNT  args;
    REBCNT  ret;

    REBVAL val;
    VAL_INIT_WRITABLE_DEBUG(&val);

    // Support for port: OPEN [scheme: ...], READ [ ], etc.
    if (action >= PORT_ACTIONS && IS_BLOCK(value))
        return T_Port(call_, action);

    // Most common series actions:  !!! speed this up!
    len = Do_Series_Action(call_, action, value, arg);
    if (len >= 0) return len; // return code

    // Special case (to avoid fetch of index and tail below):
    if (action == A_MAKE || action == A_TO) {
        //
        // make block! ...
        // to block! ...
        //
        Make_Block_Type(
            value, // out
            IS_DATATYPE(value)
                ? VAL_TYPE_KIND(value)
                : VAL_TYPE(value), // type
            LOGICAL(action == A_MAKE), // make? (as opposed to to?)
            arg // size, block to copy, or value to convert
        );

        if (ANY_PATH(value)) {
            // Get rid of any line break options on the path's elements
            REBVAL *clear = VAL_ARRAY_HEAD(value);
            for (; NOT_END(clear); clear++) {
                CLEAR_VAL_FLAG(clear, VALUE_FLAG_LINE);
            }
        }
        *D_OUT = *value;
        return R_OUT;
    }

    index = cast(REBINT, VAL_INDEX(value));
    tail  = cast(REBINT, VAL_LEN_HEAD(value));
    array = VAL_ARRAY(value);

    // Check must be in this order (to avoid checking a non-series value);
    if (action >= A_TAKE && action <= A_SORT)
        FAIL_IF_LOCKED_ARRAY(array);

    switch (action) {

    //-- Picking:

#ifdef REMOVE_THIS

//CHANGE SELECT TO USE PD_BLOCK?

    case A_PATH:
        if (IS_INTEGER(arg)) {
            action = A_PICK;
            goto repick;
        }
        // block/select case:
        ret = Find_In_Array_Simple(array, index, arg);
        goto select_val;

    case A_PATH_SET:
        action = A_POKE;
        // no SELECT case allowed !!!!
#endif

    case A_POKE:
    case A_PICK:
repick:
        value = Pick_Block(value, arg);
        if (action == A_PICK) {
            if (!value) goto is_none;
            *D_OUT = *value;
        } else {
            if (!value) fail (Error_Out_Of_Range(arg));
            arg = D_ARG(3);
            *value = *arg;
            *D_OUT = *arg;
        }
        return R_OUT;

/*
        len = Get_Num_Arg(arg); // Position
        index += len;
        if (len > 0) index--;
        if (len == 0 || index < 0 || index >= tail) {
            if (action == A_PICK) goto is_none;
            fail (Error_Out_Of_Range(arg));
        }
        if (action == A_PICK) {
pick_it:
            *D_OUT = ARR_HEAD(array)[index];
            return R_OUT;
        }
        arg = D_ARG(3);
        *D_OUT = *arg;
        ARR_HEAD(array)[index] = *arg;
        return R_OUT;
*/

    case A_TAKE:
        // take/part:
        if (D_REF(2)) {
            len = Partial1(value, D_ARG(3));
            if (len == 0) {
zero_blk:
                Val_Init_Block(D_OUT, Make_Array(0));
                return R_OUT;
            }
        } else
            len = 1;

        index = VAL_INDEX(value); // /part can change index
        // take/last:
        if (D_REF(5)) index = tail - len;
        if (index < 0 || index >= tail) {
            if (!D_REF(2)) goto is_none;
            goto zero_blk;
        }

        // if no /part, just return value, else return block:
        if (!D_REF(2))
            *D_OUT = ARR_HEAD(array)[index];
        else
            Val_Init_Block(
                D_OUT, Copy_Array_At_Max_Shallow(array, index, len)
            );
        Remove_Series(ARR_SERIES(array), index, len);
        return R_OUT;

    //-- Search:

    case A_FIND:
    case A_SELECT:
        args = Find_Refines(call_, ALL_FIND_REFS);

        len = ANY_ARRAY(arg) ? VAL_ARRAY_LEN_AT(arg) : 1;
        if (args & AM_FIND_PART) tail = Partial1(value, D_ARG(ARG_FIND_LIMIT));
        ret = 1;
        if (args & AM_FIND_SKIP) ret = Int32s(D_ARG(ARG_FIND_SIZE), 1);
        ret = Find_In_Array(array, index, tail, arg, len, args, ret);

        if (ret >= (REBCNT)tail) goto is_none;
        if (args & AM_FIND_ONLY) len = 1;
        if (action == A_FIND) {
            if (args & (AM_FIND_TAIL | AM_FIND_MATCH)) ret += len;
            VAL_INDEX(value) = ret;
        }
        else {
            ret += len;
            if (ret >= (REBCNT)tail) goto is_none;
            value = ARR_AT(array, ret);
        }
        break;

    //-- Modification:
    case A_APPEND:
    case A_INSERT:
    case A_CHANGE:
        // Length of target (may modify index): (arg can be anything)
        len = Partial1((action == A_CHANGE) ? value : arg, D_ARG(AN_LIMIT));
        index = VAL_INDEX(value);
        args = 0;
        if (D_REF(AN_ONLY)) SET_FLAG(args, AN_ONLY);
        if (D_REF(AN_PART)) SET_FLAG(args, AN_PART);
        index = Modify_Array(
            action,
            array,
            index,
            arg,
            args,
            len,
            D_REF(AN_DUP) ? Int32(D_ARG(AN_COUNT)) : 1
        );
        VAL_INDEX(value) = index;
        break;

    case A_CLEAR:
        if (index < tail) {
            if (index == 0) Reset_Array(array);
            else {
                SET_END(ARR_AT(array, index));
                SET_SERIES_LEN(VAL_SERIES(value), cast(REBCNT, index));
            }
        }
        break;

    //-- Creation:

    case A_COPY: // /PART len /DEEP /TYPES kinds
    {
        REBU64 types = 0;
        if (D_REF(ARG_COPY_DEEP)) {
            types |= D_REF(ARG_COPY_TYPES) ? 0 : TS_STD_SERIES;
        }
        if D_REF(ARG_COPY_TYPES) {
            arg = D_ARG(ARG_COPY_KINDS);
            if (IS_DATATYPE(arg)) types |= FLAGIT_KIND(VAL_TYPE(arg));
            else types |= VAL_TYPESET_BITS(arg);
        }
        len = Partial1(value, D_ARG(ARG_COPY_LIMIT));
        VAL_ARRAY(value) = Copy_Array_Core_Managed(
            array,
            VAL_INDEX(value), // at
            VAL_INDEX(value) + len, // tail
            0, // extra
            D_REF(ARG_COPY_DEEP), // deep
            types // types
        );
        VAL_INDEX(value) = 0;
    }
        break;

    //-- Special actions:

    case A_TRIM:
        args = Find_Refines(call_, ALL_TRIM_REFS);
        if (args & ~(AM_TRIM_HEAD|AM_TRIM_TAIL)) fail (Error(RE_BAD_REFINES));
        Trim_Array(array, index, args);
        break;

    case A_SWAP:
        if (!ANY_ARRAY(arg))
            fail (Error_Invalid_Arg(arg));

        // value should have been checked by the action number (sorted by
        // modifying/not modifying), so just check the argument for protect
        //
        // !!! Is relying on action numbers a good idea in general?
        //
        FAIL_IF_LOCKED_ARRAY(VAL_ARRAY(arg));

        if (index < tail && VAL_INDEX(arg) < VAL_LEN_HEAD(arg)) {
            val = *VAL_ARRAY_AT(value);
            *VAL_ARRAY_AT(value) = *VAL_ARRAY_AT(arg);
            *VAL_ARRAY_AT(arg) = val;
        }
        value = 0;
        break;

    case A_REVERSE:
        len = Partial1(value, D_ARG(3));
        if (len == 0) break;
        value = VAL_ARRAY_AT(value);
        arg = value + len - 1;
        for (len /= 2; len > 0; len--) {
            val = *value;
            *value++ = *arg;
            *arg-- = val;
        }
        value = 0;
        break;

    case A_SORT:
        Sort_Block(
            value,
            D_REF(2),   // case sensitive
            D_ARG(4),   // skip size
            D_ARG(6),   // comparator
            D_ARG(8),   // part-length
            D_REF(9),   // all fields
            D_REF(10)   // reverse
        );
        break;

    case A_RANDOM:
        if (!IS_BLOCK(value)) fail (Error_Illegal_Action(VAL_TYPE(value), action));
        if (D_REF(2)) fail (Error(RE_BAD_REFINES)); // seed
        if (D_REF(4)) { // /only
            if (index >= tail) goto is_none;
            len = (REBCNT)Random_Int(D_REF(3)) % (tail - index);  // /secure
            arg = D_ARG(2); // pass to pick
            SET_INTEGER(arg, len+1);
            action = A_PICK;
            goto repick;
        }
        Shuffle_Block(value, D_REF(3));
        break;

    default:
        fail (Error_Illegal_Action(VAL_TYPE(value), action));
    }

    if (!value) {
        *D_OUT = *D_ARG(1);
        return R_OUT;
    }

    *D_OUT = *value;
    return R_OUT;

is_none:
    return R_NONE;
}


#if !defined(NDEBUG)

//
//  Assert_Array_Core: C
//
void Assert_Array_Core(REBARR *array)
{
    REBCNT len;

    // Basic integrity checks (series is not marked free, etc.)  Note that
    // we don't use ASSERT_SERIES the macro here, because that checks to
    // see if the series is an array...and if so, would call this routine!
    //
    Assert_Series_Core(ARR_SERIES(array));

    if (!Is_Array_Series(ARR_SERIES(array)))
        Panic_Array(array);

    for (len = 0; len < ARR_LEN(array); len++) {
        REBVAL *value = ARR_AT(array, len);

        if (IS_END(value)) {
            // Premature end
            Panic_Array(array);
        }
    }

    if (NOT_END(ARR_AT(array, ARR_LEN(array)))) {
        // Not legal to not have an END! at all
        Panic_Array(array);
    }
}
#endif
