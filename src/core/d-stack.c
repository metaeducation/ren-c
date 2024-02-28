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
// Copyright 2015-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
void Collapsify_Array(Array* array, REBSPC *specifier, REBLEN limit)
{
    Cell* item = ARR_HEAD(array);
    for (; NOT_END(item); ++item) {
        if (ANY_ARRAY(item) and VAL_LEN_AT(item) > limit) {
            REBSPC *derived = Derive_Specifier(specifier, item);
            Array* copy = Copy_Array_At_Max_Shallow(
                Cell_Array(item),
                VAL_INDEX(item),
                derived,
                limit + 1
            );

            Init_Word(Array_At(copy, limit), Canon(SYM_ELLIPSIS));

            Collapsify_Array(
                copy,
                SPECIFIED,
                limit
            );

            enum Reb_Kind kind = VAL_TYPE(item);
            Init_Any_Array_At(item, kind, copy, 0); // at 0 now
            assert(IS_SPECIFIC(item));
            assert(
                NOT_VAL_FLAG(item, VALUE_FLAG_NEWLINE_BEFORE) // gets cleared
            );
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
// on an irreversible C va_list of Value*, where these disappear as the
// evaluation proceeds.  A special debug setting would be needed to hang
// onto these values for the purposes of better error messages (at the cost
// of performance).
//
Value* Init_Near_For_Frame(Cell* out, REBFRM *f)
{
    REBLEN dsp_start = DSP;

    if (NOT_END(f->value) and FRM_IS_VALIST(f)) {
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
    /*
    if (...) {
        DS_PUSH_TRASH;
        Init_Word(DS_TOP, ...?)
    }
    */

    REBINT start = FRM_INDEX(f) - 3;
    if (start > 0) {
        DS_PUSH_TRASH;
        Init_Word(DS_TOP, Canon(SYM_ELLIPSIS));
    }
    else if (start < 0)
        start = 0;

    REBLEN count = 0;
    Cell* item = Array_At(FRM_ARRAY(f), start);
    for (; NOT_END(item) and count < 6; ++item, ++count) {
        DS_PUSH_TRASH;
        if (IS_NULLED(item)) {
            //
            // If a va_list is used to do a non-evaluative call (something
            // like R3-Alpha's APPLY/ONLY) then nulled cells are currently
            // allowed.  Reify_Va_To_Array_In_Frame() may come along and
            // make a special block containing voids, which we don't want
            // to expose in a user-visible block.  Since this array is just
            // for display purposes and is "lossy" (as evidenced by the ...)
            // substitute a placeholder to avoid crashing the GC.
            //
            assert(GET_SER_FLAG(FRM_ARRAY(f), ARRAY_FLAG_NULLEDS_LEGAL));
            Init_Word(DS_TOP, Canon(SYM___VOID__));
        }
        else
            Derelativize(DS_TOP, item, f->specifier);

        if (count == FRM_INDEX(f) - start - 1) {
            //
            // Leave a marker at the point of the error, currently `~~`.
            // (Formerly it was ?? but that is now being actually used).
            //
            // This is the marker for an execution point, so it can either
            // mean "error source is to the left" or just "frame is at a
            // breakpoint at that position".
            //
            DS_PUSH_TRASH;
            Init_Word(DS_TOP, Canon(SYM__T_T));
        }
    }

    if (NOT_END(item)) {
        DS_PUSH_TRASH;
        Init_Word(DS_TOP, Canon(SYM_ELLIPSIS));
    }

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

    Array* near = Pop_Stack_Values(dsp_start);

    // Simplify overly-deep blocks embedded in the where so they show (...)
    // instead of printing out fully.
    //
    Collapsify_Array(near, SPECIFIED, 3);

    if (ANY_ARRAY_KIND(VAL_TYPE_RAW(f->value)))
        Init_Any_Array(out, VAL_TYPE(f->value), near);
    else
        Init_Block(out, near);

    return KNOWN(out);
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
DECLARE_NATIVE(running_q)
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
DECLARE_NATIVE(pending_q)
{
    INCLUDE_PARAMS_OF_PENDING_Q;

    REBCTX *frame_ctx = VAL_CONTEXT(ARG(frame));

    REBFRM *f = CTX_FRAME_MAY_FAIL(frame_ctx);

    if (Is_Action_Frame_Fulfilling(f))
        return Init_True(D_OUT);

    return Init_False(D_OUT);
}
