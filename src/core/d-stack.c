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
        if (ANY_ARRAY(item) && VAL_LEN_AT(item) > limit) {
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
//  Make_Where_For_Frame: C
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
// !!! Unfortunately, Rebol doesn't formalize this very well.  There is no
// lock on segments of blocks during their evaluation (should there be?).
// It's possible for self-modifying code to scramble the blocks being executed.
// The DO evaluator is robust in terms of not *crashing*, but the semantics
// may well suprise users.
//
// !!! DO also offers a feature whereby values can be supplied at the start
// of an evaluation which are not resident in the array.  It also can run
// on an irreversible C va_list of REBVAL*, where these disappear as the
// evaluation proceeds.  A special debug setting would be needed to hang
// onto these values for the purposes of better error messages (at the cost
// of performance).
//
REBARR *Make_Where_For_Frame(REBFRM *f)
{
    REBARR *where;

    REBCNT dsp_start = DSP;

    REBCNT start;
    REBCNT end;
    REBCNT n;

    REBOOL pending;

    if (FRM_IS_VALIST(f)) {
        //
        // Traversing a C va_arg, so reify into a (truncated) array.
        //
        const REBOOL truncated = TRUE;
        Reify_Va_To_Array_In_Frame(f, truncated);
    }

    // WARNING: MIN is a C macro and repeats its arguments.
    //
    start = MIN(ARR_LEN(FRM_ARRAY(f)), FRM_EXPR_INDEX(f));
    end = MIN(ARR_LEN(FRM_ARRAY(f)), FRM_INDEX(f));

    assert(end >= start);

    assert(Is_Any_Function_Frame(f));
    pending = Is_Function_Frame_Fulfilling(f);

    // !!! We may be running a function where the value for the function was a
    // "head" value not in the array.  These cases could substitute the symbol
    // for the currently executing function.  Reconsider when such cases
    // appear and can be studied.
    /*
        DS_PUSH_TRASH;
        Init_Word(DS_TOP, FRM_LABEL(f));
    */

    for (n = start; n < end; ++n) {
        DS_PUSH_TRASH;
        Derelativize(
            DS_TOP,
            ARR_AT(FRM_ARRAY(f), n),
            f->specifier
        );
        if (n == start) {
            //
            // Get rid of any newline marker on the first element,
            // that would visually disrupt the backtrace for no reason.
            //
            CLEAR_VAL_FLAG(DS_TOP, VALUE_FLAG_LINE);
        }
    }

    // We add an ellipsis to a pending frame to make it a little bit
    // clearer what is going on.  If someone sees a where that looks
    // like just `* [print]` the asterisk alone doesn't quite send
    // home the message that print is not running and it is
    // argument fulfillment that is why it's not "on the stack"
    // yet, so `* [print ...]` is an attempt to say that better.
    //
    // !!! This is in-band, which can be mixed up with literal usage
    // of ellipsis.  Could there be a better "out-of-band" conveyance?
    // Might the system use colorization in a value option bit?
    //
    if (pending) {
        DS_PUSH_TRASH;
        Init_Word(DS_TOP, Canon(SYM_ELLIPSIS));
    }

    where = Pop_Stack_Values(dsp_start);

    // Simplify overly-deep blocks embedded in the where so they show (...)
    // instead of printing out fully.
    //
    Collapsify_Array(where, SPECIFIED, 3);

    return where;
}


//
//  where-of: native [
//
//  "Get execution point summary for a function call (if still on stack)"
//
//      level [frame! function! integer! blank!]
//  ]
//
REBNATIVE(where_of)
//
// !!! This routine should probably be used to get the information for the
// where of an error, which should likely be out-of-band.
{
    INCLUDE_PARAMS_OF_WHERE_OF;

    REBFRM *frame = Frame_For_Stack_Level(NULL, ARG(level), TRUE);
    if (frame == NULL)
        fail (Error_Invalid_Arg(ARG(level)));

    Init_Block(D_OUT, Make_Where_For_Frame(frame));
    return R_OUT;
}


//
//  label-of: native [
//
//  "Get word label used to invoke a function call (if still on stack)"
//
//      level [frame! function! integer!]
//  ]
//
REBNATIVE(label_of)
{
    INCLUDE_PARAMS_OF_LABEL_OF;

    REBFRM *frame = Frame_For_Stack_Level(NULL, ARG(level), TRUE);

    // Make it slightly easier by returning a NONE! instead of giving an
    // error for a frame that isn't on the stack.
    //
    // !!! Should a function that was invoked by something other than a WORD!
    // return something like TRUE instead of a fake symbol?
    //
    if (frame == NULL)
        return R_BLANK;

    Init_Word(D_OUT, FRM_LABEL(frame));
    return R_OUT;
}


//
//  function-of: native [
//
//  "Get the FUNCTION! for a stack level or frame"
//
//      return: [function!]
//      level [frame! integer!]
//  ]
//
REBNATIVE(function_of)
{
    INCLUDE_PARAMS_OF_FUNCTION_OF;

    REBVAL *level = ARG(level);

    if (IS_FRAME(level)) {
        //
        // If a FRAME!, then the keylist *should* be the function params,
        // which should be coercible to a function even when the call is
        // no longer on the stack.
        //
        REBCTX *context = VAL_CONTEXT(level);
        Move_Value(D_OUT, CTX_FRAME_FUNC_VALUE(context));
    }
    else {
        REBFRM *frame = Frame_For_Stack_Level(NULL, level, TRUE);
        if (!frame)
            fail (Error_Invalid_Arg(level));

        Move_Value(D_OUT, FUNC_VALUE(frame->func));
    }

    return R_OUT;
}


//
//  backtrace-index: native [
//
//  "Get the index of a given frame or function as BACKTRACE shows it"
//
//      level [function! frame!]
//          {The function or frame to get an index for (NONE! if not running)}
//  ]
//
REBNATIVE(backtrace_index)
{
    INCLUDE_PARAMS_OF_BACKTRACE_INDEX;

    REBCNT number;

    if (NULL != Frame_For_Stack_Level(&number, ARG(level), TRUE)) {
        SET_INTEGER(D_OUT, number);
        return R_OUT;
    }

    return R_BLANK;
}


//
//  backtrace: native [
//
//  "Backtrace to find a specific FRAME!, or other queried property."
//
//      return: [<opt> block! frame!]
//          "Nothing if printing, if specific level a frame! else block"
//      level [<end> blank! integer! function!]
//          "Stack level to return frame for (blank to list)"
//      /limit
//          "Limit the length of the backtrace"
//      frames [blank! integer!]
//          "Max number of frames (pending and active), blank for no limit"
//      /brief
//          "Do not list depths, just function labels on one line"
//  ]
//
REBNATIVE(backtrace)
{
    INCLUDE_PARAMS_OF_BACKTRACE;

    Check_Security(Canon(SYM_DEBUG), POL_READ, 0);

    // Note: Running this code path is *intentionally* redundant with
    // Frame_For_Stack_Level, as a way of keeping the numbers listed in a
    // backtrace lined up with what that routine returns.  This isn't a very
    // performance-critical routine, so it's good to have the doublecheck.
    //
    REBVAL *level = ARG(level);
    REBOOL get_frame = NOT(IS_VOID(level) || IS_BLANK(level));
    if (get_frame) {
        //
        // /LIMIT assumes that you are returning a list of backtrace items,
        // while specifying a level gives one.  They are mutually exclusive.
        //
        if (REF(limit) || REF(brief))
            fail (Error_Bad_Refines_Raw());

        // See notes on handling of breakpoint below for why 0 is accepted.
        //
        if (IS_INTEGER(level) && VAL_INT32(level) < 0)
            fail (Error_Invalid_Arg(level));
    }

    REBCNT max_rows; // The "frames" from /LIMIT, plus one (for ellipsis)
    if (REF(limit)) {
        if (IS_BLANK(ARG(frames)))
            max_rows = MAX_U32; // NONE is no limit--as many frames as possible
        else {
            if (VAL_INT32(ARG(frames)) < 0)
                fail (Error_Invalid_Arg(ARG(frames)));
            max_rows = VAL_INT32(ARG(frames)) + 1; // + 1 for ellipsis
        }
    }
    else
        max_rows = 20; // On an 80x25 terminal leaves room to type afterward

    REBDSP dsp_orig = DSP; // original stack pointer (for gathered backtrace)

    REBCNT row = 0; // row we're on (incl. pending frames and maybe ellipsis)
    REBCNT number = 0; // level label number in the loop(no pending frames)
    REBOOL first = TRUE; // special check of first frame for "breakpoint 0"

    REBFRM *f;
    for (f = FS_TOP->prior; f != NULL; f = f->prior) {
        //
        // Only consider invoked or pending functions in the backtrace.
        //
        // !!! The pending functions aren't actually being "called" yet,
        // their frames are in a partial state of construction.  However it
        // gives a fuller picture to see them in the backtrace.  It may
        // be interesting to see GROUP! stack levels that are being
        // executed as well (as they are something like DO).
        //
        if (NOT(Is_Any_Function_Frame(f)))
            continue;

        REBOOL pending = Is_Function_Frame_Fulfilling(f);
        if (NOT(pending)) {
            if (
                first
                && (
                    FUNC_DISPATCHER(f->func) == &N_pause
                    || FUNC_DISPATCHER(f->func) == &N_breakpoint
                )
            ) {
                // Omitting breakpoints from the list entirely presents a
                // skewed picture of what's going on.  But giving them
                // "index 1" means that inspecting the frame you're actually
                // interested in (the one where you put the breakpoint) bumps
                // to 2, which feels unnatural.
                //
                // Compromise by not incrementing the stack numbering for
                // this case, leaving a leading breakpoint frame at index 0.
            }
            else
                ++number;
        }

        first = FALSE;

        ++row;

    #if !defined(NDEBUG)
        //
        // Try and keep the numbering in sync with query used by host to get
        // function frames to do binding in the REPL with.
        //
        if (!pending) {
            DECLARE_LOCAL (temp_val);
            SET_INTEGER(temp_val, number);

            REBCNT temp_num;
            if (
                Frame_For_Stack_Level(&temp_num, temp_val, TRUE) != f
                || temp_num != number
            ) {
                printf(
                    "%d != Frame_For_Stack_Level %d",
                    cast(int, number),
                    cast(int, temp_num)
                );
                fflush(stdout);
                assert(FALSE);
            }
        }
    #endif

        if (get_frame) {
            if (IS_INTEGER(level)) {
                if (number != cast(REBCNT, VAL_INT32(level))) // is positive
                    continue;
            }
            else {
                assert(IS_FUNCTION(level));
                if (f->func != VAL_FUNC(level))
                    continue;
            }
        }
        else {
            if (row >= max_rows) {
                //
                // If there's more stack levels to be shown than we were asked
                // to show, then put an `+ ...` in the list and break.
                //
                DS_PUSH_TRASH;
                Init_Word(DS_TOP, Canon(SYM_PLUS));

                if (NOT(REF(brief))) {
                    //
                    // In the non-/ONLY backtrace, the pairing of the ellipsis
                    // with a plus is used in order to keep the "record size"
                    // of the list at an even 2.  Asterisk might have been
                    // used but that is taken for "pending frames".
                    //
                    // !!! Review arbitrary symbolic choices.
                    //
                    DS_PUSH_TRASH;
                    Init_Word(DS_TOP, Canon(SYM_ASTERISK));
                    SET_VAL_FLAG(DS_TOP, VALUE_FLAG_LINE); // put on own line
                }
                break;
            }
        }

        if (get_frame) {
            //
            // If we were fetching a single stack level, then our result will
            // be a FRAME! (which can be queried for further properties via
            // `where-of`, `label-of`, `function-of`, etc.)
            //
            Init_Any_Context(
                D_OUT,
                REB_FRAME,
                Context_For_Frame_May_Reify_Managed(f)
            );
            return R_OUT;
        }

        // !!! Should /BRIEF omit pending frames?  Should it have a less
        // "loaded" name for the refinement?
        //
        if (REF(brief)) {
            DS_PUSH_TRASH;
            Init_Word(DS_TOP, FRM_LABEL(f));
            continue;
        }

        DS_PUSH_TRASH;
        Init_Block(DS_TOP, Make_Where_For_Frame(f));

        // If building a backtrace, we just keep accumulating results as long
        // as there are stack levels left and the limit hasn't been hit.

        // The integer identifying the stack level (used to refer to it
        // in other debugging commands).  Since we're going in reverse, we
        // add it after the props so it will show up before, and give it
        // the newline break marker.
        //
        DS_PUSH_TRASH;
        if (pending) {
            //
            // You cannot (or should not) switch to inspect a pending frame,
            // as it is partially constructed.  It gets a "*" in the list
            // instead of a number.
            //
            // !!! This may be too restrictive; though it is true you can't
            // resume/from or exit/from a pending frame (due to the index
            // not knowing how many values it would have consumed if a
            // call were to complete), inspecting the existing args could
            // be okay.  Disallowing it offers more flexibility in the
            // dealings with the arguments, however (for instance: not having
            // to initialize not-yet-filled args could be one thing).
            //
            Init_Word(DS_TOP, Canon(SYM_ASTERISK));
        }
        else
            SET_INTEGER(DS_TOP, number);

        SET_VAL_FLAG(DS_TOP, VALUE_FLAG_LINE);
    }

    // If we ran out of stack levels before finding the single one requested
    // via /AT, return a NONE!
    //
    // !!! Would it be better to give an error?
    //
    if (get_frame)
        return R_BLANK;

    // Return accumulated backtrace otherwise, in the reverse order pushed
    //
    Init_Block(D_OUT, Pop_Stack_Values_Reversed(dsp_orig));
    return R_OUT;
}


//
//  Frame_For_Stack_Level: C
//
// Level can be a void, an INTEGER!, an ANY-FUNCTION!, or a FRAME!.  If
// level is void then it means give whatever the first call found is.
//
// Returns NULL if the given level number does not correspond to a running
// function on the stack.
//
// Can optionally give back the index number of the stack level (counting
// where the most recently pushed stack level is the lowest #)
//
// !!! Unfortunate repetition of logic inside of BACKTRACE.  Assertions
// are used to try and keep them in sync, by noticing during backtrace
// if the stack level numbers being handed out don't line up with what
// would be given back by this routine.  But it would be nice to find a way
// to unify the logic for omitting things like breakpoint frames, or either
// considering pending frames or not.
//
REBFRM *Frame_For_Stack_Level(
    REBCNT *number_out,
    const REBVAL *level,
    REBOOL skip_current
) {
    REBFRM *frame = FS_TOP;
    REBOOL first = TRUE;
    REBINT num = 0;

    if (IS_INTEGER(level)) {
        if (VAL_INT32(level) < 0) {
            //
            // !!! fail() here, or just return NULL?
            //
            return NULL;
        }
    }

    // We may need to skip some number of frames, if there have been stack
    // levels added since the numeric reference point that "level" was
    // supposed to refer to has changed.  For now that's only allowed to
    // be one level, because it's rather fuzzy which stack levels to
    // omit otherwise (pending? parens?)
    //
    if (skip_current)
        frame = frame->prior;

    for (; frame != NULL; frame = frame->prior) {
        if (NOT(Is_Any_Function_Frame(frame))) {
            //
            // Don't consider pending calls, or GROUP!, or any non-invoked
            // function as a candidate to target.
            //
            // !!! The inability to target a GROUP! by number is an artifact
            // of implementation, in that there's no hook in Do_Core() at
            // the point of group evaluation to process the return.  The
            // matter is different with a pending function call, because its
            // arguments are only partially processed--hence something
            // like a RESUME/AT or an EXIT/FROM would not know which array
            // index to pick up running from.
            //
            continue;
        }

        REBOOL pending = Is_Function_Frame_Fulfilling(frame);
        if (NOT(pending)) {
            if (first) {
                if (
                    FUNC_DISPATCHER(frame->func) == &N_pause
                    || FUNC_DISPATCHER(frame->func) == N_breakpoint
                ) {
                    // this is considered the "0".  Return it only if 0 was requested
                    // specifically (you don't "count down to it");
                    //
                    if (IS_INTEGER(level) && num == VAL_INT32(level))
                        goto return_maybe_set_number_out;
                    else {
                        first = FALSE;
                        continue;
                    }
                }
                else {
                    ++num; // bump up from 0
                }
            }
        }

        first = FALSE;

        if (pending) continue;

        if (IS_INTEGER(level) && num == VAL_INT32(level))
            goto return_maybe_set_number_out;

        if (IS_VOID(level) || IS_BLANK(level)) {
            //
            // Take first actual frame if void or blank
            //
            goto return_maybe_set_number_out;
        }
        else if (IS_INTEGER(level)) {
            ++num;
            if (num == VAL_INT32(level))
                goto return_maybe_set_number_out;
        }
        else if (IS_FRAME(level)) {
            if (frame->varlist == CTX_VARLIST(VAL_CONTEXT(level))) {
                goto return_maybe_set_number_out;
            }
        }
        else {
            assert(IS_FUNCTION(level));
            if (VAL_FUNC(level) == frame->func)
                goto return_maybe_set_number_out;
        }
    }

    // Didn't find it...
    //
    return NULL;

return_maybe_set_number_out:
    if (number_out)
        *number_out = num;
    return frame;
}


//
//  Is_Context_Running_Or_Pending: C
//
REBOOL Is_Context_Running_Or_Pending(REBCTX *frame_ctx)
{
    REBFRM *f = CTX_FRAME_IF_ON_STACK(frame_ctx);
    if (f == NULL)
        return FALSE;

    if (Is_Function_Frame_Fulfilling(f))
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

    REBFRM *f = CTX_FRAME_IF_ON_STACK(frame_ctx);
    if (f == NULL)
        return R_FALSE;

    if (Is_Function_Frame_Fulfilling(f))
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

    REBFRM *f = CTX_FRAME_IF_ON_STACK(frame_ctx);
    if (f == NULL)
        return R_FALSE;

    if (Is_Function_Frame_Fulfilling(f))
        return R_TRUE;

    return R_FALSE;
}
