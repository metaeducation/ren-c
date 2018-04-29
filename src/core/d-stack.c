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
void Collapsify_Array(REBARR *array, REBSPC *specifier, REBCNT limit)
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
            assert(NOT_VAL_FLAG(item, VALUE_FLAG_LINE)); // should be cleared
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
    REBCNT dsp_start = DSP;

    if (FRM_IS_VALIST(f)) {
        //
        // Traversing a C va_arg, so reify into a (truncated) array.
        //
        const REBOOL truncated = TRUE;
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

    REBCNT count = 0;
    RELVAL *item = ARR_AT(FRM_ARRAY(f), start);
    for (; NOT_END(item) and count < 6; ++item, ++count) {
        DS_PUSH_TRASH;
        if (IS_VOID(item)) {
            //
            // If a va_list is used to do a non-evaluative call (something
            // like R3-Alpha's APPLY/ONLY) then void cells are currently
            // allowed.  Reify_Va_To_Array_In_Frame() may come along and
            // make a special block containing voids, which we don't want
            // to expose in a user-visible block.  Since this array is just
            // for display purposes and is "lossy" (as evidenced by the ...)
            // substitute a placeholder to avoid crashing the GC.
            //
            assert(GET_SER_FLAG(FRM_ARRAY(f), ARRAY_FLAG_VOIDS_LEGAL));
            Init_Word(DS_TOP, Canon(SYM___VOID__));
        }
        else
            Derelativize(DS_TOP, item, f->specifier);

        if (count == 0) {
            //
            // Get rid of any newline marker on the first element,
            // that would visually disrupt a backtrace for no reason.
            //
            CLEAR_VAL_FLAG(DS_TOP, VALUE_FLAG_LINE);
        }

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

    REBARR *near = Pop_Stack_Values(dsp_start);

    // Simplify overly-deep blocks embedded in the where so they show (...)
    // instead of printing out fully.
    //
    Collapsify_Array(near, SPECIFIED, 3);

    if (ANY_ARRAY_KIND(f->eval_type))
        Init_Any_Array(out, f->eval_type, near);
    else
        Init_Block(out, near);

    return KNOWN(out);
}


//
//  near-of: native [
//
//  "Get execution point summary for a function call (if still on stack)"
//
//      frame [frame!]
//  ]
//
REBNATIVE(near_of)
{
    INCLUDE_PARAMS_OF_NEAR_OF;

    REBFRM *f = CTX_FRAME_MAY_FAIL(VAL_CONTEXT(ARG(frame)));

    Init_Near_For_Frame(D_OUT, f);
    return R_OUT;
}


//
//  label-of: native [
//
//  "Get word label used to invoke a function call (if still on stack)"
//
//      return: [word! blank!]
//      frame [frame!]
//  ]
//
REBNATIVE(label_of)
{
    INCLUDE_PARAMS_OF_LABEL_OF;

    REBFRM *f = CTX_FRAME_MAY_FAIL(VAL_CONTEXT(ARG(frame)));

    if (f->opt_label == NULL)
        return R_BLANK;

    Init_Word(D_OUT, f->opt_label);
    return R_OUT;
}


//
//  action-of: native [
//
//  {Get the action for a frame}
//
//      return: [action!]
//      frame [frame!]
//  ]
//
REBNATIVE(action_of)
{
    INCLUDE_PARAMS_OF_ACTION_OF;

    REBVAL *frame = ARG(frame);

    // The phase contains the paramlist of the actual function (the context is
    // the keylist of the *underlying* function).
    //
    // But to get the function REBVAL, the phase has to be combined with the
    // binding of the FRAME! value.  Otherwise you'd know (for instance) that
    // you had a RETURN, but you wouldn't know where to return *from*.
    //
    Move_Value(D_OUT, ACT_ARCHETYPE(frame->payload.any_context.phase));
    D_OUT->extra.binding = frame->extra.binding;

    return R_OUT;
}


//
//  parent-of: native [
//
//  "Get the parent of a FRAME!"
//
//      return: [blank! frame!]
//      frame [frame!]
//  ]
//
REBNATIVE(parent_of)
{
    INCLUDE_PARAMS_OF_PARENT_OF;

    REBVAL *frame = ARG(frame);

    // We only want to return frames that are actual function frames (though
    // `pending? = true` ones count).
    //
    REBFRM *f = CTX_FRAME_MAY_FAIL(VAL_CONTEXT(frame));
    while ((f = f->prior) != NULL) {
        if (Is_Action_Frame(f)) {
            Init_Any_Context(
                D_OUT, REB_FRAME, Context_For_Frame_May_Reify_Managed(f)
            );
            D_OUT->payload.any_context.phase = f->phase;
            return R_OUT;
        }
    }

    return R_BLANK;
}


//
//  Is_Context_Running_Or_Pending: C
//
REBOOL Is_Context_Running_Or_Pending(REBCTX *frame_ctx)
{
    REBFRM *f = CTX_FRAME_IF_ON_STACK(frame_ctx);
    if (f == NULL)
        return FALSE;

    if (Is_Action_Frame_Fulfilling(f))
        return FALSE;

    return TRUE;
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
        return R_FALSE;

    return R_TRUE;
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
        return R_TRUE;

    return R_FALSE;
}
