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

#include "datatypes/sys-money.h"

#define THE_SIGN(v) ((v < 0) ? -1 : (v > 0) ? 1 : 0)

//
//  Series_Common_Action_Maybe_Unhandled: C
//
// This routine is called to handle actions on ANY-SERIES! that can be taken
// care of without knowing what specific kind of series it is.  So generally
// index manipulation, and things like LENGTH/etc.
//
// It only works when the operation in question applies to an understanding of
// a series as containing fixed-size units.
//
REB_R Series_Common_Action_Maybe_Unhandled(
    REBFRM *frame_,
    const REBVAL *verb
){
    REBVAL *value = D_ARG(1);

    REBINT index = cast(REBINT, VAL_INDEX(value));
    REBINT tail = cast(REBINT, VAL_LEN_HEAD(value));

    REBFLGS sop_flags;  // "SOP_XXX" Set Operation Flags

    switch (VAL_WORD_SYM(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(PAR(value));  // covered by `value`

        REBSYM property = VAL_WORD_SYM(ARG(property));
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
            VAL_INDEX(D_OUT) = cast(REBLEN, tail);
            return Trust_Const(D_OUT);

          case SYM_HEAD_Q:
            return Init_Logic(D_OUT, index == 0);

          case SYM_TAIL_Q:
            return Init_Logic(D_OUT, index >= tail);

          case SYM_PAST_Q:
            return Init_Logic(D_OUT, index > tail);

          case SYM_FILE: {
            const REBSER *s = VAL_SERIES(value);
            if (not IS_SER_ARRAY(s))
                return nullptr;
            if (NOT_ARRAY_FLAG(s, HAS_FILE_LINE_UNMASKED))
                return nullptr;
            return Init_File(D_OUT, LINK_FILE(s)); }

          case SYM_LINE: {
            const REBSER *s = VAL_SERIES(value);
            if (not IS_SER_ARRAY(s))
                return nullptr;
            if (NOT_ARRAY_FLAG(s, HAS_FILE_LINE_UNMASKED))
                return nullptr;
            return Init_Integer(D_OUT, MISC(s).line); }

          default:
            break;
        }

        break; }

      case SYM_SKIP:
      case SYM_AT: {
        INCLUDE_PARAMS_OF_SKIP; // must be compatible with AT
        UNUSED(ARG(series)); // is already `value`

        REBVAL *offset = ARG(offset);

        REBINT len = Get_Num_From_Arg(offset);
        REBI64 i;
        if (VAL_WORD_SYM(verb) == SYM_SKIP) {
            //
            // `skip x logic` means `either logic [skip x] [x]` (this is
            // reversed from R3-Alpha and Rebol2, which skipped when false)
            //
            if (IS_LOGIC(offset)) {
                if (VAL_LOGIC(offset))
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

        VAL_INDEX(value) = cast(REBLEN, i);
        RETURN (Trust_Const(value)); }

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PAR(series));  // accounted for by `value`

        ENSURE_MUTABLE(value);  // !!! Review making this extract

        REBINT len;
        if (REF(part))
            len = Part_Len_May_Modify_Index(value, ARG(part));
        else
            len = 1;

        index = cast(REBINT, VAL_INDEX(value));
        if (index < tail and len != 0)
            Remove_Any_Series_Len(value, VAL_INDEX(value), len);

        RETURN (value); }

      case SYM_INTERSECT:
        sop_flags = SOP_FLAG_CHECK;
        goto set_operation;

      case SYM_UNION:
        sop_flags = SOP_FLAG_BOTH;
        goto set_operation;

      case SYM_DIFFERENCE:
        sop_flags = SOP_FLAG_BOTH | SOP_FLAG_CHECK | SOP_FLAG_INVERT;
        goto set_operation;

      set_operation: {
        INCLUDE_PARAMS_OF_DIFFERENCE;  // should all have same spec
        UNUSED(ARG(value1));  // covered by `value`

        if (IS_BINARY(value))
            return R_UNHANDLED; // !!! unhandled; use bitwise math, for now

        return Init_Any_Series(
            D_OUT,
            VAL_TYPE(value),
            Make_Set_Operation_Series(
                value,
                ARG(value2),
                sop_flags,
                did REF(case),
                REF(skip) ? Int32s(ARG(skip), 1) : 1
            )
        ); }

      default:
        break;
    }

    return R_UNHANDLED; // not a common operation, uhandled (not NULLED_CELL!)
}


//
// Compare_Arrays_At_Indexes: C
//
REBINT Compare_Arrays_At_Indexes(
    const REBARR *s_array,
    REBLEN s_index,
    const REBARR *t_array,
    REBLEN t_index,
    bool is_case
){
    if (C_STACK_OVERFLOWING(&is_case))
        Fail_Stack_Overflow();

    if (s_array == t_array and s_index == t_index)
         return 0;

    const RELVAL *s = ARR_AT(s_array, s_index);
    const RELVAL *t = ARR_AT(t_array, t_index);

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

    return VAL_TYPE(s) > VAL_TYPE(t) ? 1 : -1;

  diff_of_ends:
    //
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
REBINT Cmp_Value(const RELVAL *sval, const RELVAL *tval, bool strict)
{
    REBLEN squotes = VAL_NUM_QUOTES(sval);
    REBLEN tquotes = VAL_NUM_QUOTES(tval);
    if (strict and (squotes != tquotes))
        return squotes > tquotes ? 1 : -1;

    REBCEL(const*) s = VAL_UNESCAPED(sval);
    REBCEL(const*) t = VAL_UNESCAPED(tval);
    enum Reb_Kind s_kind = CELL_TYPE(s);
    enum Reb_Kind t_kind = CELL_TYPE(t);

    if (
        s_kind != t_kind
        and not (ANY_NUMBER_KIND(s_kind) and ANY_NUMBER_KIND(t_kind))
    ){
        return s_kind > t_kind ? 1 : -1;
    }

    // !!! The strange and ad-hoc way this routine was written has some
    // special-case handling for numeric types.  It only allows the values to
    // be of unequal types below if they are both ANY-NUMBER!, so those cases
    // are more complex and jump around, reusing code via a goto and passing
    // the canonized decimal form via d1/d2.
    //
    REBDEC d1;
    REBDEC d2;

    switch (s_kind) {
      case REB_INTEGER:
        if (t_kind == REB_DECIMAL) {
            d1 = cast(REBDEC, VAL_INT64(s));
            d2 = VAL_DECIMAL(t);
            goto chkDecimal;
        }
        return CT_Integer(s, t, strict);

      case REB_LOGIC:
        return CT_Logic(s, t, strict);

      case REB_PERCENT:
      case REB_DECIMAL:
      case REB_MONEY:
        if (s_kind == REB_MONEY)
            d1 = deci_to_decimal(VAL_MONEY_AMOUNT(s));
        else
            d1 = VAL_DECIMAL(s);
        if (t_kind == REB_INTEGER)
            d2 = cast(REBDEC, VAL_INT64(t));
        else if (t_kind == REB_MONEY)
            d2 = deci_to_decimal(VAL_MONEY_AMOUNT(t));
        else
            d2 = VAL_DECIMAL(t);

      chkDecimal:;

        if (Eq_Decimal(d1, d2))
            return 0;
        if (d1 < d2)
            return -1;
        return 1;

      case REB_PAIR:
        return CT_Pair(s, t, strict);

      case REB_TIME:
        return CT_Time(s, t, strict);

      case REB_DATE:
        return CT_Date(s, t, strict);

      case REB_BLOCK:
      case REB_SET_BLOCK:
      case REB_GET_BLOCK:
      case REB_SYM_BLOCK:
      case REB_GROUP:
      case REB_SET_GROUP:
      case REB_GET_GROUP:
      case REB_SYM_GROUP:
        return CT_Array(s, t, strict);

      case REB_PATH:
      case REB_SET_PATH:
      case REB_GET_PATH:
      case REB_SYM_PATH:
      case REB_TUPLE:
      case REB_SET_TUPLE:
      case REB_GET_TUPLE:
      case REB_SYM_TUPLE:
        return CT_Sequence(s, t, strict);

      case REB_MAP:
        return CT_Map(s, t, strict);  // !!! Not implemented

      case REB_TEXT:
      case REB_FILE:
      case REB_EMAIL:
      case REB_URL:
      case REB_TAG:
      case REB_ISSUE:
        return CT_String(s, t, strict);

      case REB_BITSET:
        return CT_Bitset(s, t, strict);

      case REB_BINARY:
        return CT_Binary(s, t, strict);

      case REB_DATATYPE:
        return CT_Datatype(s, t, strict);

      case REB_WORD:
      case REB_SET_WORD:
      case REB_GET_WORD:
      case REB_SYM_WORD:
        return CT_Word(s, t, strict);

      case REB_ERROR:
      case REB_OBJECT:
      case REB_MODULE:
      case REB_PORT:
        return CT_Context(s, t, strict);

      case REB_ACTION:
        return CT_Action(s, t, strict);

      case REB_CUSTOM:
        //
        // !!! Comparison in R3-Alpha never had a design document; it's not
        // clear what all the variations were for.  Extensions have a CT_XXX
        // hook, what's different about that from the Cmp_XXX functions?
        //
        /* return Cmp_Gob(s, t); */
        /* return Compare_Vector(s, t); */
        /* return Cmp_Struct(s, t); */
        /* return Cmp_Event(s, t); */
        /* return VAL_LIBRARY(s) - VAL_LIBRARY(t); */
        fail ("Temporary disablement of CUSTOM! comparisons");

      case REB_BLANK:
      case REB_NULL: // !!! should nulls be allowed at this level?
      case REB_VOID:
        return CT_Unit(s, t, strict);

      default:
        break;
    }

    panic (nullptr);  // all cases should be handled above
}


//
//  Find_In_Array_Simple: C
//
// Simple search for a value in an array. Return the index of
// the value or the TAIL index if not found.
//
REBLEN Find_In_Array_Simple(
    const REBARR *array,
    REBLEN index,
    const RELVAL *target
){
    const RELVAL *value = ARR_HEAD(array);

    for (; index < ARR_LEN(array); index++) {
        if (0 == Cmp_Value(value + index, target, false))
            return index;
    }

    return ARR_LEN(array);
}
