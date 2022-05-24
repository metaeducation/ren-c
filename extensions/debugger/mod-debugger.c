//
//  File: %mod-debugger.c
//  Summary: "Native Functions for debugging"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// One goal of Ren-C's debugger is to have as much of it possible written in
// usermode Rebol code, and be easy to hack on and automate.
//
// This file contains interactive debugging support for breaking and
// resuming.  The instructions BREAKPOINT and PAUSE are natives which will
// invoke the CONSOLE function to start an interactive session.  During that
// time Rebol functions may continue to be called, though there is a sandbox
// which prevents the code from throwing or causing errors which will
// propagate past the breakpoint.  The only way to resume normal operation
// is with a "resume instruction".
//
// Hence RESUME and QUIT should be the only ways to get out of the breakpoint.
// Note that RESUME/DO provides a loophole, where it's possible to run code
// that performs a THROW or FAIL which is not trapped by the sandbox.
//

#include "sys-core.h"

#include "tmp-mod-debugger.h"


//
//  Do_Breakpoint_Throws: C
//
// A call to Do_Breakpoint_Throws will call the CONSOLE function.  The RESUME
// native cooperates with the CONSOLE by being able to give back a value (or
// give back code to run to produce a value) that the breakpoint returns.
//
// !!! RESUME had another feature, which is to be able to actually unwind and
// simulate a return /AT a function *further up the stack*.  For the moment
// this is not implemented.
//
bool Do_Breakpoint_Throws(
    REBVAL *out,
    bool interrupted,  // Ctrl-C (as opposed to a BREAKPOINT)
    const REBVAL *paused
){
    UNUSED(interrupted);  // !!! not passed to the REPL, should it be?
    UNUSED(paused);  // !!! feature TBD

    // !!! The unfinished SECURE extension would supposedly either be checked
    // here (or inject a check with HIJACK on BREAKPOINT) to make sure that
    // debugging was allowed.  Review doing that check here.

    REBVAL *inst = rebValue("debug-console");

    if (IS_INTEGER(inst)) {
        Init_Thrown_With_Label(out, inst, Lib(QUIT));
        rebRelease(inst);
        return true;
    }

    // This is a request to install an evaluator hook.  For instance, the
    // STEP command wants to interject some monitoring to the evaluator, but
    // it does not want to do so until it is at the point of resuming the
    // code that was executing when the breakpoint hit.
    //
    if (IS_HANDLE(inst)) {
        CFUNC *cfunc = VAL_HANDLE_CFUNC(inst);
        rebRelease(inst);
        UNUSED(cfunc);

        // !!! This used to hook the evaluator, with a hook that is no
        // longer available.  Debugging is being reviewed in light of a
        // stackless model and is non-functional at time of writing.

        Init_None(out);
        return false;  // no throw, run normally (but now, hooked)
    }

    // If we get an ^( ) back, that's a request to run the code outside of
    // the console's sandbox and return its result.  It's possible to use
    // quoting to return simple values, like ^('x)

    assert(IS_META_GROUP(inst));

    bool threw = Do_Any_Array_At_Throws(out, inst, SPECIFIED);

    rebRelease(inst);

    return threw;  // act as if the BREAKPOINT call itself threw
}


//
//  export breakpoint*: native [
//
//  "Signal breakpoint to the host, but do not participate in evaluation"
//
//      return: <void>
//          {Returns nothing, not even void ("invisible", like COMMENT)}
//  ]
//
REBNATIVE(breakpoint_p)
//
// !!! Need definition to test for N_DEBUGGER_breakpoint function
{
    if (Do_Breakpoint_Throws(
        SPARE,
        false,  // not a Ctrl-C, it's an actual BREAKPOINT
        Lib(BLANK)  // default result if RESUME does not override
    )){
        return_thrown (OUT);
    }

    // !!! Should use a more specific protocol (e.g. pass in END).  But also,
    // this provides a possible motivating case for functions to be able to
    // return *either* a value or no-value...if breakpoint were variadic, it
    // could splice in a value in place of what comes after it.
    //
    if (not IS_BAD_WORD(SPARE))
        fail ("BREAKPOINT is invisible, can't RESUME/WITH code (use PAUSE)");

    return_invisible (OUT);
}


//
//  export pause: native [
//
//  "Pause in the debugger before running the provided code"
//
//      return: [<opt> any-value!]
//          "Result of the code evaluation, or RESUME/WITH value if override"
//      :code [group!]  ; or LIT-WORD! name or BLOCK! for dialect
//          "Run the given code if breakpoint does not override"
//  ]
//
REBNATIVE(pause)
//
// !!! Need definition to test for N_DEBUGGER_pause function
{
    DEBUGGER_INCLUDE_PARAMS_OF_PAUSE;

    if (Do_Breakpoint_Throws(
        OUT,
        false,  // not a Ctrl-C, it's an actual BREAKPOINT
        ARG(code)  // default result if RESUME does not override
    )){
        return_thrown (OUT);
    }

    return OUT;
}


//
//  export resume: native [
//
//  {Resume after a breakpoint, can evaluate code in the breaking context.}
//
//      return: []  ; !!! Notation for divergent function?
//      expression "Evalue the given code as return value from BREAKPOINT"
//          [<end> block!]
//  ]
//
REBNATIVE(resume)
//
// The CONSOLE makes a wall to prevent arbitrary THROWs and FAILs from ending
// a level of interactive inspection.  But RESUME is special, (with a throw
// /NAME of the RESUME native) to signal an end to the interactive session.
//
// When the BREAKPOINT native gets control back from CONSOLE, it evaluates
// a given expression.
//
// !!! Initially, this supported /AT:
//
//      /at
//          "Return from another call up stack besides the breakpoint"
//      level [frame! action! integer!]
//          "Stack level to target in unwinding (can be BACKTRACE #)"
//
// While an interesting feature, it's not currently a priority.  (It can be
// accomplished with something like `resume [unwind ...]`)
{
    DEBUGGER_INCLUDE_PARAMS_OF_RESUME;

    REBVAL *expr = ARG(expression);
    if (IS_NULLED(expr))  // e.g. <end> (actuall null not legal)
        Init_Any_Array(expr, REB_META_GROUP, EMPTY_ARRAY);
    else {
        assert(IS_BLOCK(expr));
        mutable_KIND3Q_BYTE(expr) = mutable_HEART_BYTE(expr) = REB_META_GROUP;
    }

    // We throw with /NAME as identity of the RESUME function.  (Note: there
    // is no NATIVE() variant for extensions yet.  Extract from current frame.)
    //
    DECLARE_LOCAL (resume);
    Init_Action(
        resume,
        FRM_PHASE(frame_),
        FRM_LABEL(frame_),
        FRM_BINDING(frame_)
    );

    // We don't want to run the expression yet.  If we tried to run code from
    // this stack level--and it failed or threw--we'd stay stuck in the
    // breakpoint's sandbox.  We throw it as-is and it gets evaluated later.
    //
    return Init_Thrown_With_Label(OUT, expr, resume);
}



//
//  export step: native [
//
//  "Perform a step in the debugger"
//
//      return: <none>
//      amount [<end> word! integer!]
//          "Number of steps to take (default is 1) or IN, OUT, OVER"
//  ]
//
REBNATIVE(step)
{
    DEBUGGER_INCLUDE_PARAMS_OF_STEP;
    UNUSED(ARG(amount));
    fail ("STEP's methodology was deprecated, it is being re-implemented");
}
