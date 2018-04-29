//
//  File: %d-trace.c
//  Summary: "Tracing Debug Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// TRACE is functionality that was in R3-Alpha for doing low-level tracing.
// It could be turned on with `trace on` and off with `trace off`.  While
// it was on, it would print out information about the current execution step.
//
// Ren-C's goal is to have a fully-featured debugger that should allow a
// TRACE-like facility to be written and customized by the user.  They would
// be able to get access on each step to the call frame, and control the
// evaluator from within.
//
// A lower-level trace facility may still be interesting even then, for
// "debugging the debugger".  Either way, the feature is fully decoupled from
// %c-eval.c, and the system could be compiled without it (or it could be
// done as an extension).
//

#include "sys-core.h"


//
//  Eval_Depth: C
//
REBINT Eval_Depth(void)
{
    REBINT depth = 0;
    REBFRM *frame = FS_TOP;

    for (; frame != NULL; frame = FRM_PRIOR(frame), depth++)
        NOOP;

    return depth;
}


//
//  Frame_At_Depth: C
//
REBFRM *Frame_At_Depth(REBCNT n)
{
    REBFRM *frame = FS_TOP;

    while (frame) {
        if (n == 0) return frame;

        --n;
        frame = FRM_PRIOR(frame);
    }

    return NULL;
}


//
//  Trace_Value: C
//
void Trace_Value(
    const char* label, // currently "match" or "input"
    const RELVAL *value
) {
    Debug_Fmt(RM_TRACE_PARSE_VALUE, label, value);
}


//
//  Trace_String: C
//
void Trace_String(const REBYTE *str, REBINT limit)
{
    static char tracebuf[64];
    int len = MIN(60, limit);
    memcpy(tracebuf, str, len);
    tracebuf[len] = '\0';
    Debug_Fmt(RM_TRACE_PARSE_INPUT, tracebuf);
}


//
//  Trace_Error: C
//
// !!! This does not appear to be used
//
void Trace_Error(const REBVAL *value)
{
    Debug_Fmt(
        RM_TRACE_ERROR,
        &VAL_ERR_VARS(value)->type,
        &VAL_ERR_VARS(value)->id
    );
}


//
//  Do_Core_Traced: C
//
// This is the function which is swapped in for Do_Core when tracing is
// enabled.
//
void Do_Core_Traced(REBFRM * const f)
{
    // There are a lot of invariants checked on entry to Do_Core(), but this is
    // a simple one that is important enough to mirror here.
    //
    assert(FRM_HAS_MORE(f) || f->flags.bits & DO_FLAG_APPLYING);

    int depth = Eval_Depth() - Trace_Depth;
    if (depth < 0 || depth >= Trace_Level) {
        Do_Core(f); // don't apply tracing (REPL uses this to hide)
        return;
    }

    if (depth > 10)
        depth = 10; // don't indent so far it goes off the screen

    // In order to trace single steps, we convert a DO_FLAG_TO_END request
    // into a sequence of DO/NEXT operations, and loop them.
    //
    REBOOL was_do_to_end = did (f->flags.bits & DO_FLAG_TO_END);
    f->flags.bits &= ~DO_FLAG_TO_END;

    while (TRUE) {
        if (not (
            (f->flags.bits & DO_FLAG_APPLYING) // only value is END
            or IS_ACTION(f->value)
            or (Trace_Flags & TRACE_FLAG_FUNCTION)
        )){
            Debug_Space(cast(REBCNT, 4 * depth));

            if (FRM_IS_VALIST(f)) {
                //
                // If you are doing a sequence of REBVAL* held in a C va_list,
                // it doesn't have an "index".  It could manufacture one if
                // you reified it (which will be necessary for any inspections
                // beyond the current element), but TRACE does not currently
                // output more than one unit of lookahead.
                //
                Debug_Fmt_("va: %50r", f->value);
            }
            else
                Debug_Fmt_("%-02d: %50r", FRM_INDEX(f), f->value);

            if (IS_WORD(f->value) || IS_GET_WORD(f->value)) {
                const RELVAL *var = Get_Opt_Var_Else_End(
                    f->value,
                    f->specifier
                );
                if (IS_END(var) || IS_VOID(var)) {
                    Debug_Fmt_(" :"); // just show nothing
                }
                else if (IS_ACTION(var)) {
                    const REBOOL locals = FALSE;
                    const char *type_utf8 = STR_HEAD(Get_Type_Name(var));
                    REBARR *words = List_Func_Words(var, locals);
                    Debug_Fmt_(" : %s %50m", type_utf8, words);
                    Free_Array(words);
                }
                else if (
                    ANY_WORD(var)
                    || ANY_STRING(var)
                    || ANY_ARRAY(var)
                    || ANY_SCALAR(var)
                    || IS_DATE(var)
                    || IS_TIME(var)
                    || IS_BAR(var)
                    || IS_LIT_BAR(var)
                    || IS_BLANK(var)
                ){
                    // These are things that are printed, abbreviated to 50
                    // characters of molding.
                    //
                    Debug_Fmt_(" : %50r", var);
                }
                else {
                    // Just print the type if it's a context, GOB!, etc.
                    //
                    const char *type_utf8 = STR_HEAD(Get_Type_Name(var));
                    Debug_Fmt_(" : %s", type_utf8);
                }
            }
            Debug_Line();
        }

        Do_Core(f);

        if (not was_do_to_end or THROWN(f->out) or FRM_AT_END(f))
            break;
    }

    if (was_do_to_end)
        f->flags.bits |= DO_FLAG_TO_END;
}


//
//  Apply_Core_Traced: C
//
// This is the function which is swapped in for Apply_Core when tracing is
// enabled.
//
REB_R Apply_Core_Traced(REBFRM * const f)
{
    int depth = Eval_Depth() - Trace_Depth;
    if (depth < 0 || depth >= Trace_Level)
        return Apply_Core(f); // don't apply tracing (REPL uses this to hide)

    if (depth > 10)
        depth = 10; // don't indent so far it goes off the screen

    if (f->phase == f->original) {
        //
        // Only show the label if this phase is the first phase.

        Debug_Space(cast(REBCNT, 4 * depth));
        Debug_Fmt_(RM_TRACE_FUNCTION, Frame_Label_Or_Anonymous_UTF8(f));
        if (Trace_Flags & TRACE_FLAG_FUNCTION)
            Debug_Values(FRM_ARG(FS_TOP, 1), FRM_NUM_ARGS(FS_TOP), 20);
        else
            Debug_Line();
    }

    // We can only tell if it's the last phase *before* the apply, because if
    // we check *after* it may change to become the last and need R_REDO_XXX.
    //
    REBOOL last_phase = (ACT_UNDERLYING(f->phase) == f->phase);

    REB_R r = Apply_Core(f);

    if (last_phase) {
        //
        // Only show the return result if this is the last phase.

        Debug_Space(cast(REBCNT, 4 * depth));
        Debug_Fmt_(RM_TRACE_RETURN, Frame_Label_Or_Anonymous_UTF8(f));

        switch (r) {
        case R_FALSE:
        r_false:
            Debug_Values(FALSE_VALUE, 1, 50);
            break;

        case R_TRUE:
        r_true:
            Debug_Values(TRUE_VALUE, 1, 50);
            break;

        case R_VOID:
        r_void:
            // It's not legal to mold or form a void, it's not ANY-VALUE!
            // In this case, just don't print anything, like the console does
            // when an evaluation gives a void result.
            //
            Debug_Fmt("\n");
            break;

        case R_BLANK:
            Debug_Values(BLANK_VALUE, 1, 50);
            break;

        case R_BAR:
        r_bar:
            Debug_Values(BAR_VALUE, 1, 50);
            break;

        case R_OUT:
        r_out:
            Debug_Values(f->out, 1, 50);
            break;

        case R_OUT_IS_THROWN: {
            //
            // The system guards against the molding or forming of thrown
            // values, which are actually a pairing of label + value.  "Catch"
            // it temporarily, long enough to output it, then re-throw it.
            //
            DECLARE_LOCAL (arg);
            CATCH_THROWN(arg, f->out); // clears bit

            if (IS_VOID(f->out))
                Debug_Fmt_("throw %50r", arg);
            else
                Debug_Fmt_("throw %30r, label %20r", arg, f->out);

            CONVERT_NAME_TO_THROWN(f->out, arg); // sets bit
            break; }

        case R_OUT_TRUE_IF_WRITTEN:
            if (IS_END(f->out))
                goto r_true;
            else
                goto r_false;
            break;

        case R_OUT_VOID_IF_UNWRITTEN:
            if (IS_END(f->out))
                goto r_void;
            else
                goto r_out;
            break;

        case R_OUT_VOID_IF_UNWRITTEN_TRUTHIFY:
            if (IS_END(f->out))
                goto r_void;
            else if (IS_VOID(f->out) || IS_FALSEY(f->out))
                goto r_bar;
            else
                goto r_out;
            break;

        case R_REDO_CHECKED:
            assert(FALSE); // shouldn't be possible for final phase
            break;

        case R_REDO_UNCHECKED:
            assert(FALSE); // shouldn't be possible for final phase
            break;

        case R_REEVALUATE_CELL:
            Debug_Fmt("..."); // it's EVAL, should we print f->out ?
            break;

        case R_REEVALUATE_CELL_ONLY:
            Debug_Fmt("..."); // it's EVAL/ONLY, should we print f->out ?
            break;

        case R_INVISIBLE:
            // !!! It's really absolutely nothing, so print nothing?
            Debug_Fmt("\n");
            break;

        case R_UNHANDLED: // internal use only, shouldn't be returned
            assert(FALSE);
            break;

        default:
            assert(FALSE);
        }
    }

    return r;
}


//
//  trace: native [
//
//  {Enables and disables evaluation tracing and backtrace.}
//
//      return: [<opt>]
//      mode [integer! logic!]
//      /function
//          "Traces functions only (less output)"
//  ]
//
REBNATIVE(trace)
//
// !!! R3-Alpha had a kind of interesting concept of storing the backtrace in
// a buffer, up to a certain number of lines.  So it wouldn't be visible and
// interfering with your interactive typing, but you could ask for lines out
// of it after the fact.  This makes more sense as a usermode feature, where
// the backtrace is stored structurally, vs trying to implement in C.
//
{
    INCLUDE_PARAMS_OF_TRACE;

    REBVAL *mode = ARG(mode);

    Check_Security(Canon(SYM_DEBUG), POL_READ, 0);

    // Set the trace level:
    if (IS_LOGIC(mode))
        Trace_Level = VAL_LOGIC(mode) ? 100000 : 0;
    else
        Trace_Level = Int32(mode);

    if (Trace_Level) {
        PG_Do = &Do_Core_Traced;
        PG_Apply = &Apply_Core_Traced;

        if (REF(function))
            Trace_Flags |= TRACE_FLAG_FUNCTION;
        Trace_Depth = Eval_Depth() - 1; // subtract current TRACE frame
    }
    else {
        PG_Do = &Do_Core;
        PG_Apply = &Apply_Core;
    }

    return R_VOID;
}
