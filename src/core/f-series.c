//
//  File: %f-series.c
//  Summary: "common series handling functions"
//  Section: functional
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
#include "sys-deci-funcs.h"

#define THE_SIGN(v) ((v < 0) ? -1 : (v > 0) ? 1 : 0)

//
//  Series_Common_Action_Maybe_Unhandled: C
//
// This routine is called to handle actions on ANY-SERIES! that can be taken
// care of without knowing what specific kind of series it is.  So generally
// index manipulation, and things like LENGTH/etc.
//
REB_R Series_Common_Action_Maybe_Unhandled(
    REBFRM *frame_,
    REBVAL *verb
){
    REBVAL *value = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    REBINT index = cast(REBINT, VAL_INDEX(value));
    REBINT tail = cast(REBINT, VAL_LEN_HEAD(value));

    switch (VAL_WORD_SYM(verb)) {

    case SYM_REFLECT: {
        REBSYM property = VAL_WORD_SYM(arg);
        assert(property != SYM_0);

        switch (property) {
        case SYM_INDEX:
            return Init_Integer(D_OUT, cast(REBI64, index) + 1);

        case SYM_LENGTH:
            return Init_Integer(D_OUT, tail > index ? tail - index : 0);

        case SYM_HEAD:
            Move_Value(D_OUT, value);
            VAL_INDEX(D_OUT) = 0;
            return Trust_Const(D_OUT);

        case SYM_TAIL:
            Move_Value(D_OUT, value);
            VAL_INDEX(D_OUT) = cast(REBCNT, tail);
            return Trust_Const(D_OUT);

        case SYM_HEAD_Q:
            return Init_Logic(D_OUT, index == 0);

        case SYM_TAIL_Q:
            return Init_Logic(D_OUT, index >= tail);

        case SYM_PAST_Q:
            return Init_Logic(D_OUT, index > tail);

        case SYM_FILE: {
            REBSER *s = VAL_SERIES(value);
            if (IS_SER_ARRAY(s) and GET_SER_FLAG(s, ARRAY_FLAG_FILE_LINE)) {
                //
                // !!! How to tell whether it's a URL! or a FILE! ?
                //
                Scan_File(
                    D_OUT,
                    cb_cast(STR_HEAD(LINK(s).file)),
                    SER_LEN(LINK(s).file)
                );
                return D_OUT;
            }
            return nullptr; }

        case SYM_LINE: {
            REBSER *s = VAL_SERIES(value);
            if (IS_SER_ARRAY(s) and GET_SER_FLAG(s, ARRAY_FLAG_FILE_LINE))
                return Init_Integer(D_OUT, MISC(s).line);
            return nullptr; }

        default:
            break;
        }

        break; }

    case SYM_SKIP:
    case SYM_AT: {
        INCLUDE_PARAMS_OF_SKIP; // must be compatible with AT

        UNUSED(ARG(series)); // is already `value`
        UNUSED(ARG(offset)); // is already `arg` (AT calls this ARG(index))

        REBINT len = Get_Num_From_Arg(arg);
        REBI64 i;
        if (VAL_WORD_SYM(verb) == SYM_SKIP) {
            //
            // `skip x logic` means `either logic [skip x] [x]` (this is
            // reversed from R3-Alpha and Rebol2, which skipped when false)
            //
            if (IS_LOGIC(arg)) {
                if (VAL_LOGIC(arg))
                    i = cast(REBI64, index) + 1;
                else
                    i = cast(REBI64, index);
            }
            else {
                // `skip series 1` means second element, add the len as-is
                //
                i = cast(REBI64, index) + cast(REBI64, len);
            }
        }
        else {
            assert(VAL_WORD_SYM(verb) == SYM_AT);

            // `at series 1` means first element, adjust index
            //
            // !!! R3-Alpha did this differently for values > 0 vs not, is
            // this what's intended?
            //
            if (len > 0)
                i = cast(REBI64, index) + cast(REBI64, len) - 1;
            else
                i = cast(REBI64, index) + cast(REBI64, len);
        }

        if (i > cast(REBI64, tail)) {
            if (REF(only))
                return nullptr;
            i = cast(REBI64, tail); // past tail clips to tail if not /ONLY
        }
        else if (i < 0) {
            if (REF(only))
                return nullptr;
            i = 0; // past head clips to head if not /ONLY
        }

        VAL_INDEX(value) = cast(REBCNT, i);
        RETURN (Trust_Const(value)); }

    case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        UNUSED(PAR(series)); // already accounted for

        if (REF(map)) {
            UNUSED(ARG(key));
            fail (Error_Bad_Refines_Raw());
        }

        FAIL_IF_READ_ONLY_SERIES(value);

        REBINT len;
        if (REF(part))
            len = Part_Len_May_Modify_Index(value, ARG(limit));
        else
            len = 1;

        index = cast(REBINT, VAL_INDEX(value));
        if (index < tail and len != 0)
            Remove_Series(VAL_SERIES(value), VAL_INDEX(value), len);

        RETURN (value); }

    case SYM_INTERSECT: {
        if (IS_BINARY(value))
            return R_UNHANDLED; // !!! unhandled; use bitwise math, for now

        INCLUDE_PARAMS_OF_INTERSECT;

        UNUSED(ARG(value1)); // covered by value

        return Init_Any_Series(
            D_OUT,
            VAL_TYPE(value),
            Make_Set_Operation_Series(
                value,
                ARG(value2),
                SOP_FLAG_CHECK,
                REF(case),
                REF(skip) ? Int32s(ARG(size), 1) : 1
            )
        ); }

    case SYM_UNION: {
        if (IS_BINARY(value))
            return R_UNHANDLED; // !!! unhandled; use bitwise math, for now

        INCLUDE_PARAMS_OF_UNION;

        UNUSED(ARG(value1)); // covered by value

        return Init_Any_Series(
            D_OUT,
            VAL_TYPE(value),
            Make_Set_Operation_Series(
                value,
                ARG(value2),
                SOP_FLAG_BOTH,
                REF(case),
                REF(skip) ? Int32s(ARG(size), 1) : 1
            )
        ); }

    case SYM_DIFFERENCE: {
        if (IS_BINARY(value))
            return R_UNHANDLED; // !!! unhandled; use bitwise math, for now

        INCLUDE_PARAMS_OF_DIFFERENCE;

        UNUSED(ARG(value1)); // covered by value

        return Init_Any_Series(
            D_OUT,
            VAL_TYPE(value),
            Make_Set_Operation_Series(
                value,
                ARG(value2),
                SOP_FLAG_BOTH | SOP_FLAG_CHECK | SOP_FLAG_INVERT,
                REF(case),
                REF(skip) ? Int32s(ARG(size), 1) : 1
            )
        ); }

    default:
        break;
    }

    return R_UNHANDLED; // not a common operation, uhandled (not NULLED_CELL!)
}


//
//  Cmp_Array: C
//
// Compare two arrays and return the difference of the first
// non-matching value.
//
REBINT Cmp_Array(const REBCEL *sval, const REBCEL *tval, bool is_case)
{
    RELVAL *s = VAL_ARRAY_AT(sval);
    RELVAL *t = VAL_ARRAY_AT(tval);

    if (C_STACK_OVERFLOWING(&s))
        Fail_Stack_Overflow();

    if (
        VAL_SERIES(sval) == VAL_SERIES(tval)
        and VAL_INDEX(sval) == VAL_INDEX(tval)
    ){
         return 0;
    }

    if (IS_END(s) or IS_END(t))
        goto diff_of_ends;

    while (
        VAL_TYPE(s) == VAL_TYPE(t)
        or (ANY_NUMBER(s) and ANY_NUMBER(t))
    ){
        REBINT diff;
        if ((diff = Cmp_Value(s, t, is_case)) != 0)
            return diff;

        s++;
        t++;

        if (IS_END(s) or IS_END(t))
            goto diff_of_ends;
    }

    return VAL_TYPE(s) - VAL_TYPE(t);

diff_of_ends:
    // Treat end as if it were a REB_xxx type of 0, so all other types would
    // compare larger than it.
    //
    if (IS_END(s)) {
        if (IS_END(t))
            return 0;
        return -1;
    }
    return 1;
}


//
//  Cmp_Value: C
//
// Compare two values and return the difference.
//
// is_case should be true for case sensitive compare
//
REBINT Cmp_Value(const RELVAL *s, const RELVAL *t, bool is_case)
{
    REBDEC  d1, d2;

    if (VAL_TYPE(t) != VAL_TYPE(s) and not (ANY_NUMBER(s) and ANY_NUMBER(t)))
        return VAL_TYPE(s) - VAL_TYPE(t);

    assert(NOT_END(s) and NOT_END(t));

    switch(VAL_TYPE(s)) {
    case REB_INTEGER:
        if (IS_DECIMAL(t)) {
            d1 = cast(REBDEC, VAL_INT64(s));
            d2 = VAL_DECIMAL(t);
            goto chkDecimal;
        }
        return THE_SIGN(VAL_INT64(s) - VAL_INT64(t));

    case REB_LOGIC:
        return VAL_LOGIC(s) - VAL_LOGIC(t);

    case REB_CHAR:
        if (is_case)
            return THE_SIGN(VAL_CHAR(s) - VAL_CHAR(t));
        return THE_SIGN((REBINT)(UP_CASE(VAL_CHAR(s)) - UP_CASE(VAL_CHAR(t))));

    case REB_PERCENT:
    case REB_DECIMAL:
    case REB_MONEY:
        if (IS_MONEY(s))
            d1 = deci_to_decimal(VAL_MONEY_AMOUNT(s));
        else
            d1 = VAL_DECIMAL(s);
        if (IS_INTEGER(t))
            d2 = cast(REBDEC, VAL_INT64(t));
        else if (IS_MONEY(t))
            d2 = deci_to_decimal(VAL_MONEY_AMOUNT(t));
        else
            d2 = VAL_DECIMAL(t);
chkDecimal:
        if (Eq_Decimal(d1, d2))
            return 0;
        if (d1 < d2)
            return -1;
        return 1;

    case REB_PAIR:
        return Cmp_Pair(s, t);

    case REB_EVENT:
        return Cmp_Event(s, t);

    case REB_GOB:
        return Cmp_Gob(s, t);

    case REB_TUPLE:
        return Cmp_Tuple(s, t);

    case REB_TIME:
        return Cmp_Time(s, t);

    case REB_DATE:
        return Cmp_Date(s, t);

    case REB_BLOCK:
    case REB_GROUP:
    case REB_MAP:
    case REB_PATH:
    case REB_SET_PATH:
    case REB_GET_PATH:
        return Cmp_Array(s, t, is_case);

    case REB_TEXT:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
    case REB_TAG:
        return Compare_String_Vals(s, t, not is_case);

    case REB_BITSET:
    case REB_BINARY:
    case REB_IMAGE:
        return Compare_Binary_Vals(s, t);

    case REB_VECTOR:
        return Compare_Vector(s, t);

    case REB_DATATYPE:
        return VAL_TYPE_KIND(s) - VAL_TYPE_KIND(t);

    case REB_WORD:
    case REB_SET_WORD:
    case REB_GET_WORD:
    case REB_ISSUE:
        return Compare_Word(s,t,is_case);

    case REB_ERROR:
    case REB_OBJECT:
    case REB_MODULE:
    case REB_PORT:
        return VAL_CONTEXT(s) - VAL_CONTEXT(t);

    case REB_ACTION:
        return VAL_ACT_PARAMLIST(s) - VAL_ACT_PARAMLIST(t);

    case REB_LIBRARY:
        return VAL_LIBRARY(s) - VAL_LIBRARY(t);

    case REB_STRUCT:
        fail ("Temporary disablement of comparison of STRUCT!");
        /* return Cmp_Struct(s, t); */

    case REB_BLANK:
    case REB_MAX_NULLED:
    default:
        break;

    }
    return 0;
}


//
//  Find_In_Array_Simple: C
//
// Simple search for a value in an array. Return the index of
// the value or the TAIL index if not found.
//
REBCNT Find_In_Array_Simple(REBARR *array, REBCNT index, const RELVAL *target)
{
    RELVAL *value = ARR_HEAD(array);

    for (; index < ARR_LEN(array); index++) {
        if (0 == Cmp_Value(value + index, target, false))
            return index;
    }

    return ARR_LEN(array);
}
