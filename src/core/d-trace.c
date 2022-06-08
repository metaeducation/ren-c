//
//  File: %d-trace.c
//  Summary: "Tracing Debug Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
//  Trace_Value: C
//
void Trace_Value(
    const char* label, // currently "match" or "input"
    const Cell *value
){
    // !!! The way the parse code is currently organized, the value passed
    // in is a relative value.  It would take some changing to get a specific
    // value, but that's needed by the API.  Molding can be done on just a
    // relative value, however.

    DECLARE_MOLD (mo);
    Push_Mold(mo);
    Mold_Value(mo, value);

    DECLARE_LOCAL (molded);
    Init_Text(molded, Pop_Molded_String(mo));
    PUSH_GC_GUARD(molded);

    rebElide("print [",
        "{Parse}", rebT(label), "{:}", molded,
    "]");

    DROP_GC_GUARD(molded);
}


//
//  Trace_Parse_Input: C
//
void Trace_Parse_Input(const REBVAL *str)
{
    if (IS_END(str)) {
        rebElide("print {Parse Input: ** END **}");
        return;
    }

    rebElide("print [",
        "{Parse input:} mold/limit", str, "60"
    "]");
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
// Currently TRACE only applies to PARSE.
{
    INCLUDE_PARAMS_OF_TRACE;

    REBVAL *mode = ARG(mode);

    // Set the trace level:
    if (IS_LOGIC(mode))
        Trace_Level = VAL_LOGIC(mode) ? 100000 : 0;
    else
        Trace_Level = Int32(mode);

    UNUSED(ARG(function));

    return nullptr;
}
