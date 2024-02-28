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

    for (; frame != FS_BOTTOM; frame = FRM_PRIOR(frame), depth++)
        NOOP;

    return depth;
}


//
//  Frame_At_Depth: C
//
REBFRM *Frame_At_Depth(REBLEN n)
{
    REBFRM *frame = FS_TOP;

    while (frame) {
        if (n == 0) return frame;

        --n;
        frame = FRM_PRIOR(frame);
    }

    return nullptr;
}


//
//  Trace_Value: C
//
void Trace_Value(
    const char* label, // currently "match" or "input"
    const Cell* value
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
void Trace_Error(const Value* value)
{
    Debug_Fmt(
        RM_TRACE_ERROR,
        &VAL_ERR_VARS(value)->type,
        &VAL_ERR_VARS(value)->id
    );
}


//
//  Traced_Eval_Hook_Throws: C
//
// This is the function which is swapped in for Eval_Core when tracing is
// enabled.
//
bool Traced_Eval_Hook_Throws(REBFRM * const f)
{
    int depth = Eval_Depth() - Trace_Depth;
    if (depth < 0 || depth >= Trace_Level)
        return Eval_Core_Throws(f); // don't trace (REPL uses this to hide)

    if (depth > 10)
        depth = 10; // don't indent so far it goes off the screen

    // In order to trace single steps, we convert a DO_FLAG_TO_END request
    // into a sequence of EVALUATE operations, and loop them.
    //
    uintptr_t saved_flags = f->flags.bits;

    while (true) {
        if (not (
            VAL_TYPE_RAW(f->value) == REB_ACTION
            or (Trace_Flags & TRACE_FLAG_FUNCTION)
        )){
            // If a caller reuses a frame (as we are doing by single-stepping),
            // they are responsible for setting the flags each time.  This is
            // verified in the debug build via DO_FLAG_FINAL_DEBUG.
            //
            f->flags.bits = saved_flags & (~DO_FLAG_TO_END);

            Debug_Space(cast(REBLEN, 4 * depth));

            if (FRM_IS_VALIST(f)) {
                //
                // If you are doing a sequence of Value* held in a C va_list,
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
                const Cell* var = Try_Get_Opt_Var(
                    f->value,
                    f->specifier
                );
                if (not var) {
                    Debug_Fmt_(" : \\\\end\\\\"); // displays as "\\end\\"
                }
                else if (IS_NULLED(var)) {
                    Debug_Fmt_(" : \\\\null\\\\"); // displays as "\\null\\"
                }
                else if (IS_ACTION(var)) {
                    const bool locals = false;
                    const char *type_utf8 = Symbol_Head(Get_Type_Name(var));
                    DECLARE_VALUE (words);
                    Init_Block(words, List_Func_Words(var, locals));
                    Debug_Fmt_(" : %s %50r", type_utf8, words);
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
                    // Just print the type if it's a context, etc.
                    //
                    const char *type_utf8 = Symbol_Head(Get_Type_Name(var));
                    Debug_Fmt_(" : %s", type_utf8);
                }
            }
            Debug_Line();
        }

        bool threw = Eval_Core_Throws(f);

        if (not (saved_flags & DO_FLAG_TO_END)) {
            //
            // If we didn't morph the flag bits from wanting a full DO to
            // wanting only a EVALUATE, then the original intent was actually
            // just an EVALUATE.  Return the frame state as-is.
            //
            return threw;
        }

        if (threw or IS_END(f->value)) {
            //
            // If we get here, that means the initial request was for a DO
            // to END but we distorted it into stepwise.  We don't restore
            // the flags fully in a "spent frame" whether it was THROWN or
            // not (that's the caller's job).  But to be "invisible" we do
            // put back the DO_FLAG_TO_END.
            //
            f->flags.bits |= DO_FLAG_TO_END;
            return threw;
        }

        // keep looping (it was originally DO_FLAG_TO_END, which we are
        // simulating step-by-step)
    }
}


//
//  Traced_Dispatcher_Hook: C
//
// This is the function which is swapped in for Dispatcher_Core when tracing
// isenabled.
//
REB_R Traced_Dispatcher_Hook(REBFRM * const f)
{
    int depth = Eval_Depth() - Trace_Depth;
    if (depth < 0 || depth >= Trace_Level)
        return Dispatcher_Core(f);

    if (depth > 10)
        depth = 10; // don't indent so far it goes off the screen

    REBACT *phase = FRM_PHASE(f);

    if (phase == f->original) {
        //
        // Only show the label if this phase is the first phase.

        Debug_Space(cast(REBLEN, 4 * depth));
        Debug_Fmt_(RM_TRACE_FUNCTION, Frame_Label_Or_Anonymous_UTF8(f));
        if (Trace_Flags & TRACE_FLAG_FUNCTION)
            Debug_Values(FRM_ARG(FS_TOP, 1), FRM_NUM_ARGS(FS_TOP), 20);
        else
            Debug_Line();
    }

    // We can only tell if it's the last phase *before* the apply, because if
    // we check *after* it may change to become the last and need R_REDO_XXX.
    //
    bool last_phase = (ACT_UNDERLYING(phase) == phase);

    REB_R r = Dispatcher_Core(f);

    // When you HIJACK a function with an incompatible frame, it can REDO
    // even on what looks like the "last phase" because it is wiring in a new
    // function.  Review ramifications of this, and whether it should be
    // exposed vs. skipped as "not the last phase" (e.g. the function with
    // this frame's label will still be running, not running under a new name)
    //
    if (VAL_TYPE_RAW(r) == REB_R_REDO) {
        const bool checked = NOT_VAL_FLAG(r, VALUE_FLAG_FALSEY);
        if (not checked)
            last_phase = false;
    }

    if (last_phase) {
        //
        // Only show the return result if this is the last phase.

        Debug_Space(cast(REBLEN, 4 * depth));
        Debug_Fmt_(RM_TRACE_RETURN, Frame_Label_Or_Anonymous_UTF8(f));

        if (r == f->out) {

          process_out:;

            assert(not THROWN(f->out));
            Debug_Values(f->out, 1, 50);
        }
        else if (r == nullptr) {
            Debug_Fmt("// null\n");
        }
        if (VAL_TYPE_RAW(r) <= REB_MAX_NULLED) {
            Handle_Api_Dispatcher_Result(f, r);
            r = f->out;
            goto process_out;
        }
        else switch (VAL_TYPE_RAW(r)) {

        case REB_0_END:
            assert(false);
            break;

        case REB_R_THROWN: {
            // The system guards against the molding or forming of thrown
            // values, which are actually a pairing of label + value.
            // "Catch" it temporarily, long enough to output it, then
            // re-throw it.
            //
            DECLARE_VALUE (arg);
            CATCH_THROWN(arg, f->out); // clears bit

            if (IS_NULLED(f->out))
                Debug_Fmt_("throw %50r", arg);
            else
                Debug_Fmt_("throw %30r, label %20r", arg, f->out);

            CONVERT_NAME_TO_THROWN(f->out, arg); // sets bit
            break; }

        case REB_R_INVISIBLE:
            Debug_Fmt("\\\\invisible\\\\\n"); // displays as "\\invisible\\"
            break;

        case REB_R_REFERENCE:
        case REB_R_IMMEDIATE:
            assert(false); // internal use only, shouldn't be returned
            break;

        default:
            panic ("Unknown REB_R value received during trace hook");
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
DECLARE_NATIVE(trace)
//
// !!! R3-Alpha had a kind of interesting concept of storing the backtrace in
// a buffer, up to a certain number of lines.  So it wouldn't be visible and
// interfering with your interactive typing, but you could ask for lines out
// of it after the fact.  This makes more sense as a usermode feature, where
// the backtrace is stored structurally, vs trying to implement in C.
//
{
    INCLUDE_PARAMS_OF_TRACE;

    Value* mode = ARG(mode);

    Check_Security(Canon(SYM_DEBUG), POL_READ, 0);

    // Set the trace level:
    if (IS_LOGIC(mode))
        Trace_Level = VAL_LOGIC(mode) ? 100000 : 0;
    else
        Trace_Level = Int32(mode);

    if (Trace_Level) {
        PG_Eval_Throws = &Traced_Eval_Hook_Throws;
        PG_Dispatcher = &Traced_Dispatcher_Hook;

        if (REF(function))
            Trace_Flags |= TRACE_FLAG_FUNCTION;
        Trace_Depth = Eval_Depth() - 1; // subtract current TRACE frame
    }
    else {
        PG_Eval_Throws = &Eval_Core_Throws;
        PG_Dispatcher = &Dispatcher_Core;
    }

    return nullptr;
}
