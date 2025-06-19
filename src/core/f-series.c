//
//  file: %f-series.c
//  summary: "common series handling functions"
//  section: functional
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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

#define THE_SIGN(v) ((v < 0) ? -1 : (v > 0) ? 1 : 0)


// Common code for series that just tweaks the index (doesn't matter if it's
// a TEXT!, BLOCK!, BLOB!, etc.)
//
// 1. `skip x logic` means `either logic [skip x] [x]` (this is reversed
//    from R3-Alpha and Rebol2, which skipped when false)
//
IMPLEMENT_GENERIC(SKIP, Any_Series)
{
    INCLUDE_PARAMS_OF_SKIP;

    Element* v = Element_ARG(SERIES);
    assert(Any_Series(v));

    REBI64 i;
    if (Is_Logic(ARG(OFFSET))) {  // preserve a behavior for SKIP of LOGIC [1]
        if (Cell_Logic(ARG(OFFSET)))
            i = cast(REBI64, VAL_INDEX_RAW(v)) + 1;
        else
            i = cast(REBI64, VAL_INDEX_RAW(v));
    }
    else {  // `skip series 1` means second element, add offset as-is
        REBINT offset = Get_Num_From_Arg(ARG(OFFSET));
        i = cast(REBI64, VAL_INDEX_RAW(v)) + cast(REBI64, offset);
    }

    if (not Bool_ARG(UNBOUNDED)) {
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
IMPLEMENT_GENERIC(AT, Any_Series)
{
    INCLUDE_PARAMS_OF_AT;

    Element* v = Element_ARG(SERIES);
    assert(Any_Series(v));

    REBINT offset = Get_Num_From_Arg(ARG(INDEX));
    REBI64 i;

    if (offset > 0)
        i = cast(REBI64, VAL_INDEX_RAW(v)) + cast(REBI64, offset) - 1;
    else
        i = cast(REBI64, VAL_INDEX_RAW(v)) + cast(REBI64, offset);

    if (Bool_ARG(BOUNDED)) {
        if (i < 0 or i > cast(REBI64, Cell_Series_Len_Head(v)))
            return nullptr;
    }

    VAL_INDEX_RAW(v) = i;
    return COPY(Trust_Const(v));
}


IMPLEMENT_GENERIC(REMOVE, Any_Series)
{
    INCLUDE_PARAMS_OF_REMOVE;

    Element* v = Element_ARG(SERIES);

    Ensure_Mutable(v);  // !!! Review making this extract

    REBINT len;
    if (Bool_ARG(PART))
        len = Part_Len_May_Modify_Index(v, ARG(PART));
    else
        len = 1;

    REBIDX index = VAL_INDEX_RAW(v);
    if (index < Cell_Series_Len_Head(v) and len != 0)
        Remove_Any_Series_Len(v, index, len);

    return COPY(v);
}


IMPLEMENT_GENERIC(LENGTH_OF, Any_Series)
{
    INCLUDE_PARAMS_OF_LENGTH_OF;

    Element* ser = Element_ARG(ELEMENT);
    return Init_Integer(OUT, Cell_Series_Len_At(ser));
}


IMPLEMENT_GENERIC(INDEX_OF, Any_Series)  // 1-based
{
    INCLUDE_PARAMS_OF_INDEX_OF;

    Element* ser = Element_ARG(ELEMENT);
    return Init_Integer(OUT, VAL_INDEX_RAW(ser) + 1);
}


IMPLEMENT_GENERIC(OFFSET_OF, Any_Series)  // 0-based
{
    INCLUDE_PARAMS_OF_OFFSET_OF;

    Element* ser = Element_ARG(ELEMENT);
    return Init_Integer(OUT, VAL_INDEX_RAW(ser));
}


//
//  head-of: native:generic [
//
//  "Get the head of a series (or other type...? HEAD of a PORT?)"
//
//      return: [null? fundamental?]
//      element [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(HEAD_OF)
{
    INCLUDE_PARAMS_OF_HEAD_OF;

    return Dispatch_Generic(HEAD_OF, Element_ARG(ELEMENT), LEVEL);
}


//
//  tail-of: native:generic [
//
//  "Get the tail of a series (or other type...? TAIL of a PORT?)"
//
//      return: [null? fundamental?]
//      element [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(TAIL_OF)
{
    INCLUDE_PARAMS_OF_TAIL_OF;

    return Dispatch_Generic(TAIL_OF, Element_ARG(ELEMENT), LEVEL);
}


//
//  head?: native:generic [
//
//  "Test if something is at the head position"
//
//      return: [logic?]
//      element [<opt-out> fundamental?]
//  ]
//
DECLARE_NATIVE(HEAD_Q)
{
    INCLUDE_PARAMS_OF_HEAD_Q;

    Element* elem = Element_ARG(ELEMENT);
    Plainify(elem);  // (head? @[a b c]) -> ~okay~

    return Dispatch_Generic(HEAD_Q, elem, LEVEL);
}


//
//  tail?: native:generic [
//
//  "Test if something is at the tail position"
//
//      return: [logic?]
//      element [fundamental?]
//  ]
//
DECLARE_NATIVE(TAIL_Q)
{
    INCLUDE_PARAMS_OF_TAIL_Q;

    Element* elem = Element_ARG(ELEMENT);
    Plainify(elem);  // (tail? @[]) -> ~okay~

    return Dispatch_Generic(TAIL_Q, elem, LEVEL);
}


//
//  past?: native:generic [
//
//  "Test if something is past the tail position"
//
//      return: [logic?]
//      element [fundamental?]
//  ]
//
DECLARE_NATIVE(PAST_Q)
{
    INCLUDE_PARAMS_OF_PAST_Q;

    Element* elem = Element_ARG(ELEMENT);
    Plainify(elem);  // (past? next of @[]) -> ~okay~

    return Dispatch_Generic(PAST_Q, elem, LEVEL);
}


IMPLEMENT_GENERIC(HEAD_OF, Any_Series)
{
    INCLUDE_PARAMS_OF_HEAD_OF;

    Element* ser = Element_ARG(ELEMENT);

    Copy_Cell(OUT, ser);
    VAL_INDEX_RAW(OUT) = 0;
    return Trust_Const(OUT);
}


IMPLEMENT_GENERIC(TAIL_OF, Any_Series)
{
    INCLUDE_PARAMS_OF_TAIL_OF;

    Element* ser = Element_ARG(ELEMENT);

    Copy_Cell(OUT, ser);
    VAL_INDEX_RAW(OUT) = Cell_Series_Len_Head(ser);
    return Trust_Const(OUT);
}


IMPLEMENT_GENERIC(HEAD_Q, Any_Series)
{
    INCLUDE_PARAMS_OF_HEAD_Q;

    Element* ser = Element_ARG(ELEMENT);

    return Init_Logic(OUT, VAL_INDEX_RAW(ser) == 0);
}


IMPLEMENT_GENERIC(TAIL_Q, Any_Series)
{
    INCLUDE_PARAMS_OF_TAIL_Q;

    Element* ser = Element_ARG(ELEMENT);

    return Init_Logic(
        OUT,
        VAL_INDEX_RAW(ser) == Cell_Series_Len_Head(ser)
    );
}


IMPLEMENT_GENERIC(PAST_Q, Any_Series)
{
    INCLUDE_PARAMS_OF_PAST_Q;

    Element* ser = Element_ARG(ELEMENT);

    return Init_Logic(
        OUT,
        VAL_INDEX_RAW(ser) > Cell_Series_Len_Head(ser)
    );
}


IMPLEMENT_GENERIC(UNIQUE, Any_Series)  // single-arity set operation
{
    INCLUDE_PARAMS_OF_UNIQUE;

    Heart heart = Heart_Of_Builtin_Fundamental(Element_ARG(SERIES));

    Flex* flex = Make_Set_Operation_Flex(
        ARG(SERIES),
        nullptr,  // no ARG(VALUE2)
        SOP_NONE,
        Bool_ARG(CASE),
        Bool_ARG(SKIP) ? Int32s(ARG(SKIP), 1) : 1
    );

    return Init_Series(OUT, heart, flex);
}


// The policy for what to do with (intersect '[a b c] '{b c d}) isn't set in
// stone yet.  R3-Alpha and Red prohibit anything but BLOCK!, while Ren-C
// has historically made the result the same type as the first argument.
//
Option(Error*) Trap_Resolve_Dual_Hearts(
    Sink(Heart) heart,
    Element* value1,
    Element* value2
){
    UNUSED(value2);
    *heart = Heart_Of_Builtin_Fundamental(value1);
    return SUCCESS;
}


IMPLEMENT_GENERIC(INTERSECT, Any_Series)
{
    INCLUDE_PARAMS_OF_INTERSECT;

    Heart heart;
    Option(Error*) e = Trap_Resolve_Dual_Hearts(
        &heart, Element_ARG(VALUE1), Element_ARG(VALUE2)
    );
    if (e)
        return PANIC(unwrap e);

    Flex* flex = Make_Set_Operation_Flex(
        ARG(VALUE1),
        ARG(VALUE2),
        SOP_FLAG_CHECK,
        Bool_ARG(CASE),
        Bool_ARG(SKIP) ? Int32s(ARG(SKIP), 1) : 1
    );

    return Init_Series(OUT, heart, flex);
}


IMPLEMENT_GENERIC(UNION, Any_Series)
{
    INCLUDE_PARAMS_OF_UNION;

    Heart heart;
    Option(Error*) e = Trap_Resolve_Dual_Hearts(
        &heart, Element_ARG(VALUE1), Element_ARG(VALUE2)
    );
    if (e)
        return PANIC(unwrap e);

    Flex* flex = Make_Set_Operation_Flex(
        ARG(VALUE1),
        ARG(VALUE2),
        SOP_FLAG_BOTH,
        Bool_ARG(CASE),
        Bool_ARG(SKIP) ? Int32s(ARG(SKIP), 1) : 1
    );

    return Init_Series(OUT, heart, flex);
}


IMPLEMENT_GENERIC(DIFFERENCE, Any_Series)
{
    INCLUDE_PARAMS_OF_DIFFERENCE;

    Heart heart;
    Option(Error*) e = Trap_Resolve_Dual_Hearts(
        &heart, Element_ARG(VALUE1), Element_ARG(VALUE2)
    );
    if (e)
        return PANIC(unwrap e);

    Flex* flex = Make_Set_Operation_Flex(
        ARG(VALUE1),
        ARG(VALUE2),
        SOP_FLAG_BOTH | SOP_FLAG_CHECK | SOP_FLAG_INVERT,
        Bool_ARG(CASE),
        Bool_ARG(SKIP) ? Int32s(ARG(SKIP), 1) : 1
    );

    return Init_Series(OUT, heart, flex);
}


IMPLEMENT_GENERIC(EXCLUDE, Any_Series)
{
    INCLUDE_PARAMS_OF_EXCLUDE;

    Heart heart;
    Option(Error*) e = Trap_Resolve_Dual_Hearts(
        &heart, Element_ARG(DATA), Element_ARG(EXCLUSIONS)
    );
    if (e)
        return PANIC(unwrap e);

    Flex* flex = Make_Set_Operation_Flex(
        ARG(DATA),
        ARG(EXCLUSIONS),
        SOP_FLAG_CHECK | SOP_FLAG_INVERT,
        Bool_ARG(CASE),
        Bool_ARG(SKIP) ? Int32s(ARG(SKIP), 1) : 1
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
    if (LIFT_BYTE(s) != LIFT_BYTE(t))
        return false;

    Option(Heart) s_heart = Heart_Of(s);
    Option(Heart) t_heart = Heart_Of(t);

    if (not s_heart and not t_heart)
        panic ("Custom type Equal_Values not implemented yet");

    if (not s_heart or not t_heart)
        return false;  // one is a custom type, the other is not, so not equal

    if (
        (unwrap s_heart) != (unwrap t_heart)
        and not (Any_Number_Type(s_heart) and Any_Number_Type(t_heart))
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

    Push_Action(L, LIB(EQUAL_Q), PREFIX_0);

    USE_LEVEL_SHORTHANDS (L);
    INCLUDE_PARAMS_OF_EQUAL_Q;

    bool relax = not strict;

    Copy_Lifted_Cell(Erase_Cell(ARG(VALUE1)), s);
    Copy_Lifted_Cell(Erase_Cell(ARG(VALUE2)), t);
    Init_Logic(Erase_Cell(ARG(RELAX)), relax);

    DECLARE_ATOM (out);

    bool threw = Trampoline_Throws(out, L);
    if (threw)
        panic (Error_No_Catch_For_Throw(TOP_LEVEL));

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
    if (LIFT_BYTE(s) == ANTIFORM_0 or LIFT_BYTE(t) == ANTIFORM_0)
        return false;  // can't do less than on antiforms

    if (LIFT_BYTE(s) != LIFT_BYTE(t))
        return false;  // comparisons against different-quoting levels illegal

    Option(Heart) s_heart = Heart_Of(s);
    Option(Heart) t_heart = Heart_Of(t);

    if (not s_heart and not t_heart)
        panic ("Custom type Try_Lesser_Value not implemented yet");

    if (not s_heart or not t_heart)
        return false;  // one is a custom type, the other is not, so not equal

    if (
        (unwrap s_heart) != (unwrap t_heart)
        and not (Any_Number_Type(s_heart) and Any_Number_Type(t_heart))
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

    Push_Action(L, LIB(LESSER_Q), PREFIX_0);

    USE_LEVEL_SHORTHANDS (L);
    INCLUDE_PARAMS_OF_LESSER_Q;

    Copy_Cell(Erase_Cell(ARG(VALUE1)), s);
    Copy_Cell(Erase_Cell(ARG(VALUE2)), t);

    DECLARE_ATOM (out);

    bool threw = Trampoline_Throws(out, L);
    if (threw)
        panic (Error_No_Catch_For_Throw(TOP_LEVEL));

    if (Is_Error(out))
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
