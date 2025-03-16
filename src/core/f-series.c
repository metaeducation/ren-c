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


// Common code for series that just tweaks the index (doesn't matter if it's
// a TEXT!, BLOCK!, BLOB!, etc.)
//
// 1. `skip x logic` means `either logic [skip x] [x]` (this is reversed
//    from R3-Alpha and Rebol2, which skipped when false)
//
IMPLEMENT_GENERIC(skip, any_series)
{
    INCLUDE_PARAMS_OF_SKIP;

    Element* v = Element_ARG(series);
    assert(Any_Series(v));

    REBI64 i;
    if (Is_Logic(ARG(offset))) {  // preserve a behavior for SKIP of LOGIC [1]
        if (Cell_Logic(ARG(offset)))
            i = cast(REBI64, VAL_INDEX_RAW(v)) + 1;
        else
            i = cast(REBI64, VAL_INDEX_RAW(v));
    }
    else {  // `skip series 1` means second element, add offset as-is
        REBINT offset = Get_Num_From_Arg(ARG(offset));
        i = cast(REBI64, VAL_INDEX_RAW(v)) + cast(REBI64, offset);
    }

    if (not REF(unbounded)) {
        if (i < 0 or i > cast(REBI64, Cell_Series_Len_Head(v)))
            return nullptr;
    }

    VAL_INDEX_RAW(v) = i;
    return COPY(Trust_Const(v));
}


// Common code for series that just tweaks the index (doesn't matter if it's
// a TEXT!, BLOCK!, BLOB!, etc.)
//
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
//
IMPLEMENT_GENERIC(at, any_series)
{
    INCLUDE_PARAMS_OF_AT;

    Element* v = Element_ARG(series);
    assert(Any_Series(v));

    REBINT offset = Get_Num_From_Arg(ARG(index));
    REBI64 i;

    if (offset > 0)
        i = cast(REBI64, VAL_INDEX_RAW(v)) + cast(REBI64, offset) - 1;
    else
        i = cast(REBI64, VAL_INDEX_RAW(v)) + cast(REBI64, offset);

    if (REF(bounded)) {
        if (i < 0 or i > cast(REBI64, Cell_Series_Len_Head(v)))
            return nullptr;
    }

    VAL_INDEX_RAW(v) = i;
    return COPY(Trust_Const(v));
}


IMPLEMENT_GENERIC(remove, any_series)
{
    INCLUDE_PARAMS_OF_REMOVE;

    Element* v = Element_ARG(series);

    Ensure_Mutable(v);  // !!! Review making this extract

    REBINT len;
    if (REF(part))
        len = Part_Len_May_Modify_Index(v, ARG(part));
    else
        len = 1;

    REBIDX index = VAL_INDEX_RAW(v);
    if (index < Cell_Series_Len_Head(v) and len != 0)
        Remove_Any_Series_Len(v, index, len);

    return COPY(v);
}


IMPLEMENT_GENERIC(reflect, any_series)
{
    INCLUDE_PARAMS_OF_REFLECT;

    Element* v = Element_ARG(value);
    Option(SymId) id = Cell_Word_Id(ARG(property));

    switch (id) {
      case SYM_INDEX:
        return Init_Integer(OUT, VAL_INDEX_RAW(v) + 1);

      case SYM_LENGTH:
        return Init_Integer(OUT, Cell_Series_Len_At(v));

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
            VAL_INDEX_RAW(v) == Cell_Series_Len_Head(v)
        );

      case SYM_PAST_Q:
        return Init_Logic(
            OUT,
            VAL_INDEX_RAW(v) > Cell_Series_Len_Head(v)
        );

      default:
        break;
    }

    return UNHANDLED;
}


IMPLEMENT_GENERIC(unique, any_series)  // single-arity set operation
{
    INCLUDE_PARAMS_OF_UNIQUE;

    Heart heart = Cell_Heart_Ensure_Noquote(ARG(series));

    Flex* flex = Make_Set_Operation_Flex(
        ARG(series),
        nullptr,  // no ARG(value2)
        SOP_NONE,
        REF(case),
        REF(skip) ? Int32s(ARG(skip), 1) : 1
    );

    return Init_Series(OUT, heart, flex);
}


// The policy for what to do with (intersect '[a b c] '{b c d}) isn't set in
// stone yet.  R3-Alpha and Red prohibit anything but BLOCK!, while Ren-C
// has historically made the result the same type as the first argument.
//
Option(Error*) Trap_Resolve_Dual_Hearts(
    Sink(Heart) heart,
    Value* value1,
    Value* value2
){
    UNUSED(value2);
    *heart = Cell_Heart_Ensure_Noquote(value1);
    return nullptr;
}


IMPLEMENT_GENERIC(intersect, any_series)
{
    INCLUDE_PARAMS_OF_INTERSECT;

    Heart heart;
    Option(Error*) e = Trap_Resolve_Dual_Hearts(
        &heart, ARG(value1), ARG(value2)
    );
    if (e)
        return FAIL(unwrap e);

    Flex* flex = Make_Set_Operation_Flex(
        ARG(value1),
        ARG(value2),
        SOP_FLAG_CHECK,
        REF(case),
        REF(skip) ? Int32s(ARG(skip), 1) : 1
    );

    return Init_Series(OUT, heart, flex);
}


IMPLEMENT_GENERIC(union, any_series)
{
    INCLUDE_PARAMS_OF_UNION;

    Heart heart;
    Option(Error*) e = Trap_Resolve_Dual_Hearts(
        &heart, ARG(value1), ARG(value2)
    );
    if (e)
        return FAIL(unwrap e);

    Flex* flex = Make_Set_Operation_Flex(
        ARG(value1),
        ARG(value2),
        SOP_FLAG_BOTH,
        REF(case),
        REF(skip) ? Int32s(ARG(skip), 1) : 1
    );

    return Init_Series(OUT, heart, flex);
}


IMPLEMENT_GENERIC(difference, any_series)
{
    INCLUDE_PARAMS_OF_DIFFERENCE;

    Heart heart;
    Option(Error*) e = Trap_Resolve_Dual_Hearts(
        &heart, ARG(value1), ARG(value2)
    );
    if (e)
        return FAIL(unwrap e);

    Flex* flex = Make_Set_Operation_Flex(
        ARG(value1),
        ARG(value2),
        SOP_FLAG_BOTH | SOP_FLAG_CHECK | SOP_FLAG_INVERT,
        REF(case),
        REF(skip) ? Int32s(ARG(skip), 1) : 1
    );

    return Init_Series(OUT, heart, flex);
}


IMPLEMENT_GENERIC(exclude, any_series)
{
    INCLUDE_PARAMS_OF_EXCLUDE;

    Heart heart;
    Option(Error*) e = Trap_Resolve_Dual_Hearts(
        &heart, ARG(data), ARG(exclusions)
    );
    if (e)
        return FAIL(unwrap e);

    Flex* flex = Make_Set_Operation_Flex(
        ARG(data),
        ARG(exclusions),
        SOP_FLAG_CHECK | SOP_FLAG_INVERT,
        REF(case),
        REF(skip) ? Int32s(ARG(skip), 1) : 1
    );

    return Init_Series(OUT, heart, flex);
}


//
//  Equal_Values: C
//
// Test to see if two values are equal.  Quoting level is heeded, and values
// at distinct quoting levels are not considered equal.
//
bool Equal_Values(const Value* s, const Value* t, bool strict)
{
    if (QUOTE_BYTE(s) != QUOTE_BYTE(t))
        return false;

    Heart s_heart = Cell_Heart(s);
    Heart t_heart = Cell_Heart(t);

    if (
        s_heart != t_heart
        and not (Any_Number_Kind(s_heart) and Any_Number_Kind(t_heart))
    ){
        return false;
    }

    // !!! Apply accelerations here that don't need a frame?  :-/  I dislike
    // the idea of duplicating the work, but it is undoubtedly a bit more
    // costly to make and dispatch the frame, even if done elegantly.

    Level* const L = Make_End_Level(
        &Action_Executor,
        FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING)
    );
    const Value* action = LIB(EQUAL_Q);
    Push_Action(L, action);
    Begin_Action(L, Cell_Frame_Label_Deep(action), PREFIX_0);

    USE_LEVEL_SHORTHANDS (L);
    INCLUDE_PARAMS_OF_EQUAL_Q;

    Copy_Cell(Erase_Cell(ARG(value1)), s);
    Copy_Cell(Erase_Cell(ARG(value2)), t);
    Init_Logic(Erase_Cell(ARG(strict)), strict);

    DECLARE_ATOM (out);

    bool threw = Trampoline_Throws(out, L);
    if (threw)
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));

    return Cell_Logic(out);
}


//
//  Lesser_Value: C
//
// This dispatches to the LESSER? implementation.  It may not be able to
// compare the types, e.g. ("A" < 1) is an error, so you get a bool.
//
bool Try_Lesser_Value(Sink(bool) lesser, const Value* s, const Value* t)
{
    if (QUOTE_BYTE(s) == ANTIFORM_0 or QUOTE_BYTE(t) == ANTIFORM_0)
        return false;  // can't do less than on antiforms

    if (QUOTE_BYTE(s) != QUOTE_BYTE(t))
        return false;  // comparisons against different-quoting levels illegal

    Heart s_heart = Cell_Heart(s);
    Heart t_heart = Cell_Heart(t);

    if (
        s_heart != t_heart
        and not (Any_Number_Kind(s_heart) and Any_Number_Kind(t_heart))
    ){
        return false;
    }

    // !!! Apply accelerations here that don't need a frame?  :-/  I dislike
    // the idea of duplicating the work, but it is undoubtedly a bit more
    // costly to make and dispatch the frame, even if done elegantly.

    Level* const L = Make_End_Level(
        &Action_Executor,
        FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | LEVEL_FLAG_RAISED_RESULT_OK
    );
    const Value* action = LIB(LESSER_Q);
    Push_Action(L, action);
    Begin_Action(L, Cell_Frame_Label_Deep(action), PREFIX_0);

    USE_LEVEL_SHORTHANDS (L);
    INCLUDE_PARAMS_OF_LESSER_Q;

    Copy_Cell(Erase_Cell(ARG(value1)), s);
    Copy_Cell(Erase_Cell(ARG(value2)), t);

    DECLARE_ATOM (out);

    bool threw = Trampoline_Throws(out, L);
    if (threw)
        fail (Error_No_Catch_For_Throw(TOP_LEVEL));

    if (Is_Raised(out))
        return false;

    *lesser = Cell_Logic(out);
    return true;
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

    bool strict = false;
    for (; index < Array_Len(array); index++) {
        if (Equal_Values(value + index, target, strict))
            return index;
    }

    return Array_Len(array);
}
