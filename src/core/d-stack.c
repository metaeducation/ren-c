//
// Rebol 3 Language Interpreter and Run-time Environment
// "Ren-C" branch @ https://github.com/metaeducation/ren-c
//
//=////////////////////////////////////////////////////////////////////////=//
//
//  Summary: Debug Stack Reflection and Querying
//  File: %d-stack.h
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
void Collapsify_Array(REBARR *array, REBSPC *specifier, REBLEN limit)
{
    RELVAL *item = ARR_HEAD(array);
    for (; NOT_END(item); ++item) {
        if (ANY_ARRAY(item) and VAL_LEN_AT(item) > limit) {
            REBSPC *derived = Derive_Specifier(specifier, item);
            REBARR *copy = Copy_Array_At_Max_Shallow(
                VAL_ARRAY(item),
                VAL_INDEX(item),
                derived,
                limit + 1
            );

            Init_Word(ARR_AT(copy, limit), Canon(SYM_ELLIPSIS));

            Collapsify_Array(
                copy,
                SPECIFIED,
                limit
            );

            enum Reb_Kind kind = VAL_TYPE(item);
            Init_Any_Array_At(item, kind, copy, 0); // at 0 now
            assert(IS_SPECIFIC(item));
            assert(NOT_CELL_FLAG(item, NEWLINE_BEFORE)); // gets cleared
        }
    }
}


//
//  Init_Near_For_Frame: C
//
// Each call frame maintains the array it is executing in, the current index
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
// !!! DO also offers a feature whereby values can be supplied at the start
// of an evaluation which are not resident in the array.  It also can run
// on an irreversible C va_list of REBVAL*, where these disappear as the
// evaluation proceeds.  A special debug setting would be needed to hang
// onto these values for the purposes of better error messages (at the cost
// of performance).
//
REBVAL *Init_Near_For_Frame(RELVAL *out, REBFRM *f)
{
    REBLEN dsp_start = DSP;

    if (NOT_END(f->feed->value) and FRM_IS_VARIADIC(f)) {
        //
        // Traversing a C va_arg, so reify into a (truncated) array.
        //
        const bool truncated = true;
        Reify_Va_To_Array_In_Frame(f, truncated);
    }

    // Get at most 6 values out of the array.  Ideally 3 before and after
    // the error point.  If truncating either the head or tail of the
    // values, put ellipses.

    // !!! We may be running a function where the value for the function was a
    // "head" value not in the array.  These cases could substitute the symbol
    // for the currently executing function.  Reconsider when such cases
    // appear and can be studied.

    REBINT start = FRM_INDEX(f) - 3;
    if (start > 0)
        Init_Word(DS_PUSH(), Canon(SYM_ELLIPSIS));
    else if (start < 0)
        start = 0;

    REBLEN count = 0;
    const RELVAL *item = ARR_AT(FRM_ARRAY(f), start);
    for (; NOT_END(item) and count < 6; ++item, ++count) {
        assert(not IS_NULLED(item));  // can't be in arrays, API won't splice
        Derelativize(DS_PUSH(), item, f->feed->specifier);

        if (count == FRM_INDEX(f) - start - 1) {
            //
            // Leave a marker at the point of the error, currently `~~`.
            //
            // This is the marker for an execution point, so it can either
            // mean "error source is to the left" or just "frame is at a
            // breakpoint at that position".
            //
            Init_Word(DS_PUSH(), Canon(SYM__T_T));
        }
    }

    if (NOT_END(item))
        Init_Word(DS_PUSH(), Canon(SYM_ELLIPSIS));

    // !!! This code can be called on an executing frame, such as when an
    // error happens in that frame.  Or it can be called on a pending frame
    // when examining a backtrace...where the function hasn't been called
    // yet.  This needs some way of differentiation, consider it.
    //
    /*
    if (Is_Action_Frame(f) and Is_Action_Frame_Fulfilling(f)) {
        ???
    }
    */

    REBARR *near = Pop_Stack_Values(dsp_start);

    // Simplify overly-deep blocks embedded in the where so they show (...)
    // instead of printing out fully.
    //
    Collapsify_Array(near, SPECIFIED, 3);

    // The evaluator tries to keep the frame state as lean as it can, so at
    // the moment it may not even know if it's dispatching a SET-PATH! or a
    // GET-PATH!.  It has one bit to say whether it's in a path mode or not,
    // so assume that will help a little bit with making the error clear.
    //
    if (GET_EVAL_FLAG(f, PATH_MODE)) {
        Freeze_Array_Shallow(near);

        REBVAL *p = Try_Init_Any_Path_Arraylike(out, REB_PATH, near);
        assert(p);  // !!! Do we know the array is a valid path?
        UNUSED(p);
    }
    else
        Init_Block(out, near);

    return SPECIFIC(out);
}


//
//  Is_Context_Running_Or_Pending: C
//
bool Is_Context_Running_Or_Pending(REBCTX *frame_ctx)
{
    REBFRM *f = CTX_FRAME_IF_ON_STACK(frame_ctx);
    if (not f)
        return false;

    if (Is_Action_Frame_Fulfilling(f))
        return false;

    return true;
}


//
//  running?: native [
//
//  "Returns TRUE if a FRAME! is on the stack and executing (arguments done)."
//
//      frame [frame!]
//  ]
//
REBNATIVE(running_q)
{
    INCLUDE_PARAMS_OF_RUNNING_Q;

    REBCTX *frame_ctx = VAL_CONTEXT(ARG(frame));

    REBFRM *f = CTX_FRAME_MAY_FAIL(frame_ctx);

    if (Is_Action_Frame_Fulfilling(f))
        return Init_False(D_OUT);

    return Init_True(D_OUT);
}


//
//  pending?: native [
//
//  "Returns TRUE if a FRAME! is on the stack, but is gathering arguments."
//
//      frame [frame!]
//  ]
//
REBNATIVE(pending_q)
{
    INCLUDE_PARAMS_OF_PENDING_Q;

    REBCTX *frame_ctx = VAL_CONTEXT(ARG(frame));

    REBFRM *f = CTX_FRAME_MAY_FAIL(frame_ctx);

    if (Is_Action_Frame_Fulfilling(f))
        return Init_True(D_OUT);

    return Init_False(D_OUT);
}
