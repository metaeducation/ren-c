//
//  File: %d-trace.c
//  Summary: "Tracing Debug Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// "debugging the debugger".  Either way, the routines have been extracted
// from %c-do.c in order to reduce the total length of that very long file.
//

#include "sys-core.h"


//
//  Eval_Depth: C
//
REBINT Eval_Depth(void)
{
    REBINT depth = 0;
    struct Reb_Frame *frame = FS_TOP;

    for (; frame != NULL; frame = FRM_PRIOR(frame), depth++)
        NOOP;

    return depth;
}


//
//  Frame_At_Depth: C
//
struct Reb_Frame *Frame_At_Depth(REBCNT n)
{
    struct Reb_Frame *frame = FS_TOP;

    while (frame) {
        if (n == 0) return frame;

        --n;
        frame = FRM_PRIOR(frame);
    }

    return NULL;
}


static REBINT Init_Depth(void)
{
    // Check the trace depth is ok:
    REBINT depth = Eval_Depth() - Trace_Depth;
    if (depth < 0 || depth >= Trace_Level) return -1;
    if (depth > 10) depth = 10;
    Debug_Space(cast(REBCNT, 4 * depth));
    return depth;
}


#define CHECK_DEPTH(d) if ((d = Init_Depth()) < 0) return;\


//
//  Trace_Line: C
//
void Trace_Line(struct Reb_Frame *f)
{
    int depth;

    if (GET_FLAG(Trace_Flags, 1)) return; // function
    if (IS_FUNCTION(f->value)) return;

    CHECK_DEPTH(depth);

    if (IS_END(f->value)) {
        Debug_Fmt_("END");
    }
    else if (f->flags & DO_FLAG_VA_LIST) {
        Debug_Fmt_("VA_LIST_FLAG...");
    }
    else {
        Debug_Fmt_("%-02d: %50r", cast(REBINT, f->index), f->value);
    }

    if (IS_WORD(f->value) || IS_GET_WORD(f->value)) {
        const REBVAL *var = GET_OPT_VAR_MAY_FAIL(f->value, f->specifier);
        if (VAL_TYPE(var) < REB_FUNCTION)
            Debug_Fmt_(" : %50r", var);
        else if (VAL_TYPE(var) == REB_FUNCTION) {
            REBARR *words = List_Func_Words(var, FALSE); // no locals
            Debug_Fmt_(" : %s %50m", Get_Type_Name(var), words);
            Free_Array(words);
        }
        else
            Debug_Fmt_(" : %s", Get_Type_Name(var));
    }
    /*if (ANY_WORD(value)) {
        word = value;
        if (IS_WORD(value)) value = GET_OPT_VAR_MAY_FAIL(word);
        Debug_Fmt_(
            " : %50r",
            VAL_WORD_CONTEXT(word),
            VAL_WORD_INDEX(word),
            Get_Type_Name(value)
        );
    }
    if (Trace_Stack) Debug_Fmt(" : %s %50m", DSP, FS_TOP);
    else
    */
    Debug_Line();
}


//
//  Trace_Func: C
//
void Trace_Func(REBSTR *label, const REBVAL *value)
{
    int depth;
    CHECK_DEPTH(depth);
    Debug_Fmt_("--> %s", STR_HEAD(label));
    if (GET_FLAG(Trace_Flags, 1))
        Debug_Values(FRM_ARG(FS_TOP, 1), FRM_NUM_ARGS(FS_TOP), 20);
    else Debug_Line();
}


//
//  Trace_Return: C
//
void Trace_Return(REBSTR *label, const REBVAL *value)
{
    int depth;
    CHECK_DEPTH(depth);
    Debug_Fmt_("<-- %s ==", STR_HEAD(label));
    Debug_Values(value, 1, 50);
}


//
//  Trace_Value: C
//
void Trace_Value(
    const char* label, // currently "match" or "input"
    const RELVAL *value
) {
    int depth;
    CHECK_DEPTH(depth);
    Debug_Fmt("Parse %s: %r", label, value);
}


//
//  Trace_String: C
//
void Trace_String(const REBYTE *str, REBINT limit)
{
    static char tracebuf[64];
    int depth;
    int len = MIN(60, limit);
    CHECK_DEPTH(depth);
    memcpy(tracebuf, str, len);
    tracebuf[len] = '\0';
    Debug_Fmt("Parse input: %s", tracebuf);
}


//
//  Trace_Error: C
//
void Trace_Error(const REBVAL *value)
{
    int depth;
    CHECK_DEPTH(depth);
    Debug_Fmt(
        "**: error : %r %r",
        &VAL_ERR_VARS(value)->type,
        &VAL_ERR_VARS(value)->id
    );
}


//
//  trace: native [
//
//  {Enables and disables evaluation tracing and backtrace.}
//
//      mode [integer! logic!]
//      /back {Set mode ON to enable or integer for lines to display}
//      /function "Traces functions only (less output)"
//  ]
//
REBNATIVE(trace)
{
    REBVAL *arg = D_ARG(1);

    Check_Security(Canon(SYM_DEBUG), POL_READ, 0);

    // The /back option: ON and OFF, or INTEGER! for # of lines:
    if (D_REF(2)) { // /back
        if (IS_LOGIC(arg)) {
            Enable_Backtrace(VAL_LOGIC(arg));
        }
        else if (IS_INTEGER(arg)) {
            REBINT lines = Int32(arg);
            Trace_Flags = 0;
            if (lines < 0) {
                fail (Error_Invalid_Arg(arg));
                return R_VOID;
            }

            Display_Backtrace(cast(REBCNT, lines));
            return R_VOID;
        }
    }
    else Enable_Backtrace(FALSE);

    // Set the trace level:
    if (IS_LOGIC(arg)) {
        Trace_Level = VAL_LOGIC(arg) ? 100000 : 0;
    }
    else Trace_Level = Int32(arg);

    if (Trace_Level) {
        Trace_Flags = 1;
        if (D_REF(3)) SET_FLAG(Trace_Flags, 1); // function
        Trace_Depth = Eval_Depth() - 1; // subtract current TRACE frame
    }
    else Trace_Flags = 0;

    return R_VOID;
}


#if !defined(NDEBUG)

//
//  Trace_Fetch_Debug: C
//
// When down to the wire and wanting to debug the evaluator, it can be very
// useful to see the steps of the states it's going through to see what is
// wrong.  This routine hooks the individual fetch and writes at a more
// fine-grained level than a breakpoint at each DO/NEXT point.
//
void Trace_Fetch_Debug(const char* msg, struct Reb_Frame *f, REBOOL after) {
    Debug_Fmt(
        "%d - %s : %s",
        cast(REBCNT, f->index),
        msg,
        after ? "AFTER" : "BEFORE"
    );

    if (IS_END(f->value))
        Debug_Fmt("f->value is END");
    else
        PROBE(f->value);
}

#endif
