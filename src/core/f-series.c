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

#include "cells/cell-money.h"

#define THE_SIGN(v) ((v < 0) ? -1 : (v > 0) ? 1 : 0)

//
//  Series_Common_Action_Maybe_Unhandled: C
//
// This routine is called to handle actions on ANY-SERIES? that can be taken
// care of without knowing what specific kind of series it is.  So generally
// index manipulation, and things like LENGTH/etc.
//
// It only works when the operation in question applies to an understanding of
// a series as containing fixed-size units.
//
Bounce Series_Common_Action_Maybe_Unhandled(
    Level* level_,
    const Symbol* verb
){
    Value* v = D_ARG(1);

    Flags sop_flags;  // "SOP_XXX" Set Operation Flags

    Option(SymId) id = Symbol_Id(verb);
    switch (id) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(PARAM(value));  // covered by `value`

        Option(SymId) property = Cell_Word_Id(ARG(property));

        switch (property) {
          case SYM_INDEX:
            return Init_Integer(OUT, VAL_INDEX_RAW(v) + 1);

          case SYM_LENGTH: {
            REBI64 len_head = Cell_Series_Len_Head(v);
            if (VAL_INDEX_RAW(v) < 0 or VAL_INDEX_RAW(v) > len_head)
                return TRASH;  // !!! better than error?
            return Init_Integer(OUT, len_head - VAL_INDEX_RAW(v)); }

          case SYM_HEAD:
            Copy_Cell(OUT, v);
            VAL_INDEX_RAW(OUT) = 0;
            return Trust_Const(OUT);

          case SYM_TAIL:
            Copy_Cell(OUT, v);
            VAL_INDEX_RAW(OUT) = Cell_Series_Len_Head(v);
            return Trust_Const(OUT);

          case SYM_HEAD_Q:
            return Init_Logic(OUT, VAL_INDEX_RAW(v) == 0);

          case SYM_TAIL_Q:
            return Init_Logic(
                OUT,
                VAL_INDEX_RAW(v) == cast(REBIDX, Cell_Series_Len_Head(v))
            );

          case SYM_PAST_Q:
            return Init_Logic(
                OUT,
                VAL_INDEX_RAW(v) > cast(REBIDX, Cell_Series_Len_Head(v))
            );

          case SYM_FILE: {
            const Flex* f = Cell_Flex(v);
            if (not Is_Flex_Array(f))
                return nullptr;
            if (Not_Array_Flag(c_cast(Array*, f), HAS_FILE_LINE_UNMASKED))
                return nullptr;
            return Init_File(OUT, LINK(Filename, f)); }

          case SYM_LINE: {
            const Flex* f = Cell_Flex(v);
            if (not Is_Flex_Array(f))
                return nullptr;
            if (Not_Array_Flag(c_cast(Array*, f), HAS_FILE_LINE_UNMASKED))
                return nullptr;
            return Init_Integer(OUT, f->misc.line); }

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
        if (Is_Logic(ARG(offset))) {
            if (Cell_Logic(ARG(offset)))
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
            if (i < 0 or i > cast(REBI64, Cell_Series_Len_Head(v)))
                return nullptr;
        }

        VAL_INDEX_RAW(v) = i;
        return COPY(Trust_Const(v)); }

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
            if (i < 0 or i > cast(REBI64, Cell_Series_Len_Head(v)))
                return nullptr;
        }

        VAL_INDEX_RAW(v) = i;
        return COPY(Trust_Const(v)); }

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PARAM(series));  // accounted for by `value`

        Ensure_Mutable(v);  // !!! Review making this extract

        REBINT len;
        if (REF(part))
            len = Part_Len_May_Modify_Index(v, ARG(part));
        else
            len = 1;

        REBIDX index = VAL_INDEX_RAW(v);
        if (index < cast(REBIDX, Cell_Series_Len_Head(v)) and len != 0)
            Remove_Any_Series_Len(v, index, len);

        return COPY(v); }

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

        return Init_Series(
            OUT,
            Cell_Heart_Ensure_Noquote(v),
            Make_Set_Operation_Flex(
                v,
                (id == SYM_UNIQUE)
                    ? cast(Value*, nullptr)  // C++98 ambiguous w/o cast
                    : ARG(value2),
                sop_flags,
                REF(case),
                REF(skip) ? Int32s(ARG(skip), 1) : 1
            )
        ); }

      default:
        break;
    }

    fail (UNHANDLED);
}


//
//  Cmp_Value: C
//
// Compare two values and return the difference.
//
// is_case should be true for case sensitive compare
//
REBINT Cmp_Value(const Cell* s, const Cell* t, bool strict)
{
    Byte squotes = QUOTE_BYTE(s);
    Byte tquotes = QUOTE_BYTE(t);
    if (squotes != tquotes)
        return squotes > tquotes ? 1 : -1;

    Heart s_heart = Cell_Heart(s);
    Heart t_heart = Cell_Heart(t);

    if (
        s_heart != t_heart
        and not (Any_Number_Kind(s_heart) and Any_Number_Kind(t_heart))
    ){
        return s_heart > t_heart ? 1 : -1;
    }

    // !!! The strange and ad-hoc way this routine was written has some
    // special-case handling for numeric types.  It only allows the values to
    // be of unequal types below if they are both ANY-NUMBER?, so those cases
    // are more complex and jump around, reusing code via a goto and passing
    // the canonized decimal form via d1/d2.
    //
    REBDEC d1;
    REBDEC d2;

    switch (s_heart) {
      case REB_INTEGER:
        if (t_heart == REB_DECIMAL) {
            d1 = cast(REBDEC, VAL_INT64(s));
            d2 = VAL_DECIMAL(t);
            goto chkDecimal;
        }
        return CT_Integer(s, t, strict);

      case REB_PERCENT:
      case REB_DECIMAL:
      case REB_MONEY:
        if (s_heart == REB_MONEY)
            d1 = deci_to_decimal(VAL_MONEY_AMOUNT(s));
        else
            d1 = VAL_DECIMAL(s);
        if (t_heart == REB_INTEGER)
            d2 = cast(REBDEC, VAL_INT64(t));
        else if (t_heart == REB_MONEY)
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
      case REB_TYPE_BLOCK:
      case REB_VAR_BLOCK:
        //
      case REB_GROUP:
      case REB_SET_GROUP:
      case REB_GET_GROUP:
      case REB_META_GROUP:
      case REB_THE_GROUP:
      case REB_TYPE_GROUP:
      case REB_VAR_GROUP:
        return CT_List(s, t, strict);

      case REB_PATH:
      case REB_SET_PATH:
      case REB_GET_PATH:
      case REB_META_PATH:
      case REB_THE_PATH:
      case REB_TYPE_PATH:
      case REB_VAR_PATH:
        //
      case REB_TUPLE:
      case REB_SET_TUPLE:
      case REB_GET_TUPLE:
      case REB_META_TUPLE:
      case REB_THE_TUPLE:
      case REB_TYPE_TUPLE:
      case REB_VAR_TUPLE:
        return CT_Sequence(s, t, strict);

      case REB_MAP:
        return CT_Map(s, t, strict);  // !!! Not implemented

      case REB_SIGIL:
        if (Cell_Sigil(s) == Cell_Sigil(t))
            return 0;
        return Cell_Sigil(s) > Cell_Sigil(t) ? 1 : 0;

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

      case REB_WORD:
      case REB_SET_WORD:
      case REB_GET_WORD:
      case REB_META_WORD:
      case REB_THE_WORD:
      case REB_TYPE_WORD:
      case REB_VAR_WORD:
        return CT_Word(s, t, strict);

      case REB_ERROR:
      case REB_OBJECT:
      case REB_MODULE:
      case REB_PORT:
        return CT_Context(s, t, strict);

      case REB_FRAME:
        return CT_Frame(s, t, strict);

      case REB_BLANK:
        assert(CT_Blank(s, t, strict) == 0);
        return 0;  // shortcut call to comparison

      case REB_HANDLE:
        return CT_Handle(s, t, strict);

      case REB_COMMA:
        return CT_Comma(s, t, strict);

      case REB_PARAMETER:
        return CT_Parameter(s, t, strict);

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
    const Array* array,
    REBLEN index,
    const Element* target
){
    const Element* value = Array_Head(array);

    for (; index < Array_Len(array); index++) {
        if (0 == Cmp_Value(value + index, target, false))
            return index;
    }

    return Array_Len(array);
}
