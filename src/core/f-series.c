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
**  Module:  f-series.c
**  Summary: common series handling functions
**  Section: functional
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "sys-core.h"

#define THE_SIGN(v) ((v < 0) ? -1 : (v > 0) ? 1 : 0)

//
//  Do_Series_Action: C
// 
// Common series functions.
//
REBINT Do_Series_Action(struct Reb_Call *call_, REBCNT action, REBVAL *value, REBVAL *arg)
{
    REBINT  index;
    REBINT  tail;
    REBINT  len = 0;

    // Common setup code for all actions:
    if (action != A_MAKE && action != A_TO) {
        index = (REBINT)VAL_INDEX(value);
        tail  = (REBINT)VAL_TAIL(value);
    } else return -1;

    switch (action) {

    //-- Navigation:

    case A_HEAD:
        VAL_INDEX(value) = 0;
        break;

    case A_TAIL:
        VAL_INDEX(value) = (REBCNT)tail;
        break;

    case A_HEADQ:
        DECIDE(index == 0);

    case A_TAILQ:
        DECIDE(index >= tail);

    case A_PASTQ:
        DECIDE(index > tail);

    case A_NEXT:
        if (index < tail) VAL_INDEX(value)++;
        break;

    case A_BACK:
        if (index > 0) VAL_INDEX(value)--;
        break;

    case A_SKIP:
    case A_AT:
        len = Get_Num_Arg(arg);
        {
            REBI64 i = (REBI64)index + (REBI64)len;
            if (action == A_SKIP) {
                if (IS_LOGIC(arg)) i--;
            } else { // A_AT
                if (len > 0) i--;
            }
            if (i > (REBI64)tail) i = (REBI64)tail;
            else if (i < 0) i = 0;
            VAL_INDEX(value) = (REBCNT)i;
        }
        break;
/*
    case A_ATZ:
        len = Get_Num_Arg(arg);
        {
            REBI64 idx = Add_Max(0, index, len, MAX_I32);
            if (idx < 0) idx = 0;
            VAL_INDEX(value) = (REBCNT)idx;
        }
        break;
*/
    case A_INDEX_OF:
        SET_INTEGER(D_OUT, cast(REBI64, index) + 1);
        return R_OUT;

    case A_LENGTH:
        SET_INTEGER(D_OUT, tail > index ? tail - index : 0);
        return R_OUT;

    case A_REMOVE:
        // /PART length
        TRAP_PROTECT(VAL_SERIES(value));
        len = D_REF(2) ? Partial(value, 0, D_ARG(3), 0) : 1;
        index = (REBINT)VAL_INDEX(value);
        if (index < tail && len != 0)
            Remove_Series(VAL_SERIES(value), VAL_INDEX(value), len);
        break;

    case A_ADD:         // Join_Strings(value, arg);
    case A_SUBTRACT:    // "test this" - 10
    case A_MULTIPLY:    // "t" * 4 = "tttt"
    case A_DIVIDE:
    case A_REMAINDER:
    case A_POWER:
    case A_ODDQ:
    case A_EVENQ:
    case A_ABSOLUTE:
        fail (Error_Illegal_Action(VAL_TYPE(value), action));

    default:
        return -1;
    }

    *D_OUT = *value;
    return R_OUT;

is_false:
    return R_FALSE;

is_true:
    return R_TRUE;
}


//
//  Cmp_Block: C
// 
// Compare two blocks and return the difference of the first
// non-matching value.
//
REBINT Cmp_Block(const REBVAL *sval, const REBVAL *tval, REBFLG is_case)
{
    REBVAL  *s = VAL_BLK_DATA(sval);
    REBVAL  *t = VAL_BLK_DATA(tval);
    REBINT  diff;

    if (C_STACK_OVERFLOWING(&s)) Trap_Stack_Overflow();

    if ((VAL_SERIES(sval)==VAL_SERIES(tval))&&
     (VAL_INDEX(sval)==VAL_INDEX(tval)))
         return 0;

    while (!IS_END(s) && (VAL_TYPE(s) == VAL_TYPE(t) ||
                    (IS_NUMBER(s) && IS_NUMBER(t)))) {
        if ((diff = Cmp_Value(s, t, is_case)) != 0)
            return diff;
        s++, t++;
    }
    return VAL_TYPE(s) - VAL_TYPE(t);
}


//
//  Cmp_Value: C
// 
// Compare two values and return the difference.
// 
// is_case TRUE for case sensitive compare
//
REBINT Cmp_Value(const REBVAL *s, const REBVAL *t, REBFLG is_case)
{
    REBDEC  d1, d2;

    if (VAL_TYPE(t) != VAL_TYPE(s) && !(IS_NUMBER(s) && IS_NUMBER(t)))
        return VAL_TYPE(s) - VAL_TYPE(t);

    switch(VAL_TYPE(s)) {

    case REB_INTEGER:
        if (IS_DECIMAL(t)) {
            d1 = (REBDEC)VAL_INT64(s);
            d2 = VAL_DECIMAL(t);
            goto chkDecimal;
        }
        return THE_SIGN(VAL_INT64(s) - VAL_INT64(t));

    case REB_LOGIC:
        return VAL_LOGIC(s) - VAL_LOGIC(t);

    case REB_CHAR:
        if (is_case) return THE_SIGN(VAL_CHAR(s) - VAL_CHAR(t));
        return THE_SIGN((REBINT)(UP_CASE(VAL_CHAR(s)) - UP_CASE(VAL_CHAR(t))));

    case REB_DECIMAL:
    case REB_MONEY:
            d1 = VAL_DECIMAL(s);
        if (IS_INTEGER(t))
            d2 = (REBDEC)VAL_INT64(t);
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
    case REB_PAREN:
    case REB_MAP:
    case REB_PATH:
    case REB_SET_PATH:
    case REB_GET_PATH:
    case REB_LIT_PATH:
        return Cmp_Block(s, t, is_case);

    case REB_STRING:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
    case REB_TAG:
        return Compare_String_Vals(s, t, (REBOOL)!is_case);

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
    case REB_LIT_WORD:
    case REB_REFINEMENT:
    case REB_ISSUE:
        return Compare_Word(s,t,is_case);

    case REB_ERROR:
        return VAL_ERR_NUM(s) - VAL_ERR_NUM(t);

    case REB_OBJECT:
    case REB_MODULE:
    case REB_PORT:
        return VAL_OBJ_FRAME(s) - VAL_OBJ_FRAME(t);

    case REB_NATIVE:
        return &VAL_FUNC_CODE(s) - &VAL_FUNC_CODE(t);

    case REB_ACTION:
    case REB_COMMAND:
    case REB_FUNCTION:
        return VAL_FUNC_BODY(s) - VAL_FUNC_BODY(t);

    case REB_ROUTINE:
    case REB_CALLBACK:
        return VAL_ROUTINE_INFO(s) - VAL_ROUTINE_INFO(t);

    case REB_LIBRARY:
        return VAL_LIB_HANDLE(s) - VAL_LIB_HANDLE(t);

    case REB_STRUCT:
        return Cmp_Struct(s, t);

    case REB_NONE:
    case REB_UNSET:
    case REB_END:
    default:
        break;

    }
    return 0;
}


//
//  Find_Block_Simple: C
// 
// Simple search for a value in a block. Return the index of
// the value or the TAIL index if not found.
//
REBCNT Find_Block_Simple(REBSER *series, REBCNT index, REBVAL *target)
{
    REBVAL *value = BLK_HEAD(series);

    for (; index < SERIES_TAIL(series); index++) {
        if (0 == Cmp_Value(value+index, target, FALSE)) return index;
    }

    return SERIES_TAIL(series);
}
