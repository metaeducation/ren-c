//
// Rebol 3 Language Interpreter and Run-time Environment
// "Ren-C" branch @ https://github.com/metaeducation/ren-c
//
//=////////////////////////////////////////////////////////////////////////=//
//
//  summary: Debug Stack Reflection and Querying
//  file: %d-stack.h
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This file contains interactive debugging support for examining and
// interacting with the stack.
//
// !!! Interactive debugging is a work in progress, and comments are in the
// functions below.
//

#include "sys-core.h"


//
//  Collapsify_Array: C
//
// This will replace "long" nested blocks with collapsed versions with
// ellipses to show they have been cut off.  It does not change the arrays
// in question, but replaces them with copies.
//
void Collapsify_Array(Array* array, REBLEN limit)
{
    const Element* tail = Array_Tail(array);
    Element* item = Array_Head(array);
    for (; item != tail; ++item) {
        if (Any_List(item) and Cell_Series_Len_At(item) > limit) {
            Source* copy = Copy_Source_At_Max_Shallow(
                Cell_Array(item),
                VAL_INDEX(item),
                limit + 1
            );

            Init_Word(Array_At(copy, limit), CANON(ELLIPSIS_3));

            Collapsify_Array(copy, limit);

            Heart heart = Heart_Of_Builtin_Fundamental(item);  // !!! Quoteds?
            Init_Any_List_At(item, heart, copy, 0);  // at 0 now
            assert(Not_Cell_Flag(item, NEWLINE_BEFORE));  // gets cleared
        }
    }
}


//
//  Init_Near_For_Level: C
//
// Each stack level maintains the array it is executing in, the current index
// in that array, and the index of where the current expression started.
// This can be deduced into a segment of code to display in the debug views
// to indicate roughly "what's running" at that stack level.  The code is
// a shallow copy of the array content.
//
// The resulting WHERE information only includes the range of the array being
// executed up to the point of currently relevant evaluation.  It does not
// go all the way to the tail of the block (where future potential evaluation
// should be.
//
// !!! Eval also offers a feature whereby values can be supplied at the start
// of an evaluation which are not resident in the array.  It also can run
// on an irreversible C va_list of Value*, where these disappear as the
// evaluation proceeds.  A special debug setting would be needed to hang
// onto these values for the purposes of better error messages (at the cost
// of performance).
//
Element* Init_Near_For_Level(Sink(Element) out, Level* L)
{
    StackIndex base = TOP_INDEX;

    if (Level_Is_Variadic(L)) {
        //
        // A variadic feed may not be able to be reified, if the data is
        // malformed.  But it also might be able to be... *unless this is
        // a scanner level itself raising the error*.
        //
        const bool truncated = true;
        Reify_Variadic_Feed_As_Array_Feed(L->feed, truncated);
    }

    // Get at most 6 values out of the array.  Ideally 3 before and after
    // the error point.  If truncating either the head or tail of the
    // values, put ellipses.

    // !!! We may be running a function where the value for the function was a
    // "head" value not in the array.  These cases could substitute the symbol
    // for the currently executing function.  Reconsider when such cases
    // appear and can be studied.

    REBINT start = Level_Array_Index(L) - 3;
    if (start > 0)
        Init_Word(PUSH(), CANON(ELLIPSIS_3));
    else if (start < 0)
        start = 0;

    REBLEN count = 0;
    const Element* tail = Array_Tail(Level_Array(L));
    const Element* item = Array_At(Level_Array(L), start);
    for (; item != tail and count < 6; ++item, ++count) {
        Copy_Cell(PUSH(), item);

        if (count == Level_Array_Index(L) - start - 1) {
            //
            // Leave a marker at the point of the error, currently `**`.
            //
            // This is the marker for an execution point, so it can either
            // mean "error source is to the left" or just "frame is at a
            // breakpoint at that position".
            //
            Init_Word(PUSH(), CANON(_P_P));
        }
    }

    if (item != tail)
        Init_Word(PUSH(), CANON(ELLIPSIS_3));

    // !!! This code can be called on an executing level, such as when an
    // error happens in that level.  Or it can be called on a pending level
    // when examining a backtrace...where the function hasn't been called
    // yet.  This needs some way of differentiation, consider it.
    //
    /*
    if (Is_Action_Level(L) and Is_Level_Fulfilling(L)) {
        ???
    }
    */

    Source* near = Pop_Managed_Source_From_Stack(base);

    // Simplify overly-deep blocks embedded in the where so they show (...)
    // instead of printing out fully.
    //
    Collapsify_Array(near, 3);

    Init_Block(out, near);

    return out;
}


//
//  Is_Varlist_Running_Or_Pending: C
//
bool Is_Varlist_Running_Or_Pending(VarList* varlist)
{
    Level* L = Level_Of_Varlist_If_Running(varlist);
    if (not L)
        return false;

    if (Is_Level_Fulfilling(L))
        return false;

    return true;
}


//
//  running?: native [
//
//  "Returns TRUE if a FRAME! is on the stack and executing (arguments done)"
//
//      return: [logic?]
//      frame [frame!]
//  ]
//
DECLARE_NATIVE(RUNNING_Q)
{
    INCLUDE_PARAMS_OF_RUNNING_Q;

    VarList* frame_ctx = Cell_Varlist(ARG(FRAME));

    Level* L = Level_Of_Varlist_May_Panic(frame_ctx);

    if (Is_Level_Fulfilling(L))
        return Init_Logic(OUT, false);

    return Init_Logic(OUT, true);
}


//
//  pending?: native [
//
//  "Returns TRUE if a FRAME! is on the stack, but is gathering arguments"
//
//      return: [logic?]
//      frame [frame!]
//  ]
//
DECLARE_NATIVE(PENDING_Q)
{
    INCLUDE_PARAMS_OF_PENDING_Q;

    VarList* frame_ctx = Cell_Varlist(ARG(FRAME));

    Level* L = Level_Of_Varlist_May_Panic(frame_ctx);

    if (Is_Level_Fulfilling(L))
        return Init_Logic(OUT, true);

    return Init_Logic(OUT, false);
}
