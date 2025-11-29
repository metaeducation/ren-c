//
//  file: %d-trace.c
//  summary: "Tracing Debug Routines"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// TRACE was functionality that was in R3-Alpha for doing low-level tracing.
// It could be turned on with `trace on` and off with `trace off`.  While
// it was on, it would print out information about the current execution step.
//
// Ren-C's goal is to have a fully-featured debugger that should allow a
// TRACE-like facility to be written and customized by the user.  They would
// be able to get access on each step of the trampoline, and control the
// evaluator from within.
//
// A lower-level trace facility may still be interesting even then, for
// "debugging the debugger".
//
// The redesign of the evaluator to be based around a "trampoline" and to
// operate stacklessly requires a complete rewrite of the trace facility.
// While that work has not been done, an example of how powerful the new
// hooking mechanisms are can be seen in this demo of a GUI debugger for
// PARSE running in a web browser:
//
// https://forum.rebol.info/t/visualizing-parse/1639/8
//
// So the fundamentals are in place, but it just needs some shaping up and
// attention given.  (Essentially, the evaluator needs to expose some kind of
// parallel interface for hooking "evaluations" instead of combinators.)
//

#include "sys-core.h"


//
//  trace: native [
//
//  "Enables and disables evaluation tracing and backtrace"
//
//      return: []
//      mode [integer! logic?]
//      :function "Traces functions only (less output)"
//  ]
//
DECLARE_NATIVE(TRACE)
//
// Ren-C seeks to completely redesign tracing in a generalized way, such that
// dialects can have their own tracing fit into the architecture.  The old
// trace command was ripped out in favor of just using a debugger with the
// C sources, since there aren't any real "users" who need the non-generalized
// debug features at this time.
//
// !!! R3-Alpha had a kind of interesting concept of storing the backtrace in
// a buffer, up to a certain number of lines.  So it wouldn't be visible and
// interfering with your interactive typing, but you could ask for lines out
// of it after the fact.  This makes more sense as a usermode feature, where
// the backtrace is stored structurally, vs trying to implement in C.
{
    INCLUDE_PARAMS_OF_TRACE;

    UNUSED(ARG(MODE));
    UNUSED(ARG(FUNCTION));

    panic ("TRACE is being redesigned in light of Trampolines/Stackless");
}
