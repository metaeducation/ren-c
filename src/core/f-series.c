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
    const REBSYM *verb
){
    REBVAL *v = D_ARG(1);

    REBFLGS sop_flags;  // "SOP_XXX" Set Operation Flags

    SYMID id = ID_OF_SYMBOL(verb);
    switch (id) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(PAR(value));  // covered by `value`

        OPT_SYMID property = VAL_WORD_ID(ARG(property));
        assert(property != SYM_0);

        switch (property) {
          case SYM_INDEX:
            return Init_Integer(OUT, VAL_INDEX_RAW(v) + 1);

          case SYM_LENGTH: {
            REBI64 len_head = VAL_LEN_HEAD(v);
            if (VAL_INDEX_RAW(v) < 0 or VAL_INDEX_RAW(v) > len_head)
                return Init_None(OUT);  // !!! better than error?
            return Init_Integer(OUT, len_head - VAL_INDEX_RAW(v)); }

          case SYM_HEAD:
            Copy_Cell(OUT, v);
            VAL_INDEX_RAW(OUT) = 0;
            return Trust_Const(OUT);

          case SYM_TAIL:
            Copy_Cell(OUT, v);
            VAL_INDEX_RAW(OUT) = VAL_LEN_HEAD(v);
            return Trust_Const(OUT);

          case SYM_HEAD_Q:
            return Init_Logic(OUT, VAL_INDEX_RAW(v) == 0);

          case SYM_TAIL_Q:
            return Init_Logic(
                OUT,
                VAL_INDEX_RAW(v) == cast(REBIDX, VAL_LEN_HEAD(v))
            );

          case SYM_PAST_Q:
            return Init_Logic(
                OUT,
                VAL_INDEX_RAW(v) > cast(REBIDX, VAL_LEN_HEAD(v))
            );

          case SYM_FILE: {
            const REBSER *s = VAL_SERIES(v);
            if (not IS_SER_ARRAY(s))
                return nullptr;
            if (NOT_SUBCLASS_FLAG(ARRAY, s, HAS_FILE_LINE_UNMASKED))
                return nullptr;
            return Init_File(OUT, LINK(Filename, s)); }

          case SYM_LINE: {
            const REBSER *s = VAL_SERIES(v);
            if (not IS_SER_ARRAY(s))
                return nullptr;
            if (NOT_SUBCLASS_FLAG(ARRAY, s, HAS_FILE_LINE_UNMASKED))
                return nullptr;
            return Init_Integer(OUT, s->misc.line); }

          default:
            break;
        }

        break; }

      case SYM_SKIP: {
        INCLUDE_PARAMS_OF_SKIP;
        UNUSED(ARG(series));  // covered by `v`

        // `skip x logic` means `either logic [skip x] [x]` (this is reversed
        // from R3-Alpha and Rebol2, which skipped when false)
        //
        REBI64 i;
        if (IS_LOGIC(ARG(offset))) {
            if (VAL_LOGIC(ARG(offset)))
                i = cast(REBI64, VAL_INDEX_RAW(v)) + 1;
            else
                i = cast(REBI64, VAL_INDEX_RAW(v));
        }
        else {
            // `skip series 1` means second element, add offset as-is
            //
            REBINT offset = Get_Num_From_Arg(ARG(offset));
            i = cast(REBI64, VAL_INDEX_RAW(v)) + cast(REBI64, offset);
        }

        if (not REF(unbounded)) {
            if (i < 0 or i > cast(REBI64, VAL_LEN_HEAD(v)))
                return nullptr;
        }

        VAL_INDEX_RAW(v) = i;
        return_value (Trust_Const(v)); }

      case SYM_AT: {
        INCLUDE_PARAMS_OF_AT;
        UNUSED(ARG(series));  // covered by `v`

        REBINT offset = Get_Num_From_Arg(ARG(index));
        REBI64 i;

        // `at series 1` is first element, e.g. [0] in C.  Adjust offset.
        //
        // Note: Rebol2 and Red treat AT 1 and AT 0 as being the same:
        //
        //     rebol2>> at next next "abcd" 1
        //     == "cd"
        //
        //     rebol2>> at next next "abcd" 0
        //     == "cd"
        //
        // That doesn't make a lot of sense...but since `series/0` will always
        // return NULL and `series/-1` returns the previous element, it hints
        // at special treatment for index 0 (which is C-index -1).
        //
        // !!! Currently left as an open question.

        if (offset > 0)
            i = cast(REBI64, VAL_INDEX_RAW(v)) + cast(REBI64, offset) - 1;
        else
            i = cast(REBI64, VAL_INDEX_RAW(v)) + cast(REBI64, offset);

        if (REF(bounded)) {
            if (i < 0 or i > cast(REBI64, VAL_LEN_HEAD(v)))
                return nullptr;
        }

        VAL_INDEX_RAW(v) = i;
        return_value (Trust_Const(v)); }

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PAR(series));  // accounted for by `value`

        ENSURE_MUTABLE(v);  // !!! Review making this extract

        REBINT len;
        if (REF(part))
            len = Part_Len_May_Modify_Index(v, ARG(part));
        else
            len = 1;

        REBIDX index = VAL_INDEX_RAW(v);
        if (index < cast(REBIDX, VAL_LEN_HEAD(v)) and len != 0)
            Remove_Any_Series_Len(v, index, len);

        return_value (v); }

      case SYM_UNIQUE:  // Note: only has 1 argument, so dummy second arg
        sop_flags = SOP_NONE;
        goto set_operation;

      case SYM_INTERSECT:
        sop_flags = SOP_FLAG_CHECK;
        goto set_operation;

      case SYM_UNION:
        sop_flags = SOP_FLAG_BOTH;
        goto set_operation;

      case SYM_DIFFERENCE:
        sop_flags = SOP_FLAG_BOTH | SOP_FLAG_CHECK | SOP_FLAG_INVERT;
        goto set_operation;

      case SYM_EXCLUDE:
        sop_flags = SOP_FLAG_CHECK | SOP_FLAG_INVERT;
        goto set_operation;

      set_operation: {
        //
        // Note: All set operations share a compatible spec.  The way that
        // UNIQUE is compatible is via a dummy argument in the second
        // parameter slot, so that the /CASE and /SKIP arguments line up.
        //
        INCLUDE_PARAMS_OF_DIFFERENCE;  // should all have compatible specs
        UNUSED(ARG(value1));  // covered by `value`

        return Init_Any_Series(
            OUT,
            VAL_TYPE(v),
            Make_Set_Operation_Series(
                v,
                (id == SYM_UNIQUE)
                    ? cast(REBVAL*, nullptr)  // C++98 ambiguous w/o cast
                    : ARG(value2),
                sop_flags,
                did REF(case),
                REF(skip) ? Int32s(ARG(skip), 1) : 1
            )
        ); }

      default:
        break;
    }

    return R_UNHANDLED;  // not a common operation, uhandled...not Lib(NULL)!
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

    const Cell *s_tail = ARR_TAIL(s_array);
    const Cell *t_tail = ARR_TAIL(t_array);
    const Cell *s = ARR_AT(s_array, s_index);
    const Cell *t = ARR_AT(t_array, t_index);

    if (s == s_tail or t == t_tail)
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

        if (s == s_tail or t == t_tail)
            goto diff_of_ends;
    }

    return VAL_TYPE(s) > VAL_TYPE(t) ? 1 : -1;

  diff_of_ends:
    //
    // Treat end as if it were a REB_xxx type of 0, so all other types would
    // compare larger than it.
    //
    if (s == s_tail) {
        if (t == t_tail)
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
REBINT Cmp_Value(const Cell *sval, const Cell *tval, bool strict)
{
    REBLEN squotes = VAL_NUM_QUOTES(sval);
    REBLEN tquotes = VAL_NUM_QUOTES(tval);
    if (squotes != tquotes)
        return squotes > tquotes ? 1 : -1;

    noquote(const Cell*) s = VAL_UNESCAPED(sval);
    noquote(const Cell*) t = VAL_UNESCAPED(tval);
    enum Reb_Kind s_kind = CELL_HEART(s);
    enum Reb_Kind t_kind = CELL_HEART(t);

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
      case REB_META_BLOCK:
      case REB_THE_BLOCK:
      case REB_GROUP:
      case REB_SET_GROUP:
      case REB_GET_GROUP:
      case REB_META_GROUP:
      case REB_THE_GROUP:
        return CT_Array(s, t, strict);

      case REB_PATH:
      case REB_SET_PATH:
      case REB_GET_PATH:
      case REB_META_PATH:
      case REB_THE_PATH:
      case REB_TUPLE:
      case REB_SET_TUPLE:
      case REB_GET_TUPLE:
      case REB_META_TUPLE:
      case REB_THE_TUPLE:
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
      case REB_META_WORD:
      case REB_THE_WORD:
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

      case REB_NULL: // !!! should nulls be allowed at this level?
        return 0;  // nulls always equal to each other

      case REB_BLANK:
        assert(CT_Blank(s, t, strict) == 0);
        return 0;  // shortcut call to comparison

      case REB_BAD_WORD:
        return CT_Bad_word(s, t, strict);

      case REB_HANDLE:
        return CT_Handle(s, t, strict);

      case REB_COMMA:
        return CT_Comma(s, t, strict);

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
    const Cell *target
){
    const Cell *value = ARR_HEAD(array);

    for (; index < ARR_LEN(array); index++) {
        if (0 == Cmp_Value(value + index, target, false))
            return index;
    }

    return ARR_LEN(array);
}
