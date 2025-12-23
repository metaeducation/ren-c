//
//  file: %mod-debugger.c
//  summary: "Native Functions for debugging"
//  section: extension
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    Sink(Value) out,
    bool interrupted,  // Ctrl-C (as opposed to a BREAKPOINT)
    const Value* paused
){
    UNUSED(interrupted);  // !!! not passed to the REPL, should it be?
    UNUSED(paused);  // !!! feature TBD

    // !!! The unfinished SECURE extension would supposedly either be checked
    // here (or inject a check with HIJACK on BREAKPOINT) to make sure that
    // debugging was allowed.  Review doing that check here.

    Element* inst = Known_Element(rebValue("debug-console"));

    if (Is_Integer(inst)) {
        Init_Thrown_With_Label(TOP_LEVEL, inst, LIB(QUIT));
        rebRelease(inst);
        return true;
    }

    // This is a request to install an evaluator hook.  For instance, the
    // STEP command wants to interject some monitoring to the evaluator, but
    // it does not want to do so until it is at the point of resuming the
    // code that was executing when the breakpoint hit.
    //
    if (Is_Handle(inst)) {
        CFunction* cfunc = Cell_Handle_Cfunc(inst);
        rebRelease(inst);
        UNUSED(cfunc);

        // !!! This used to hook the evaluator, with a hook that is no
        // longer available.  Debugging is being reviewed in light of a
        // stackless model and is non-functional at time of writing.

        Init_Tripwire(out);
        return false;  // no throw, run normally (but now, hooked)
    }

    // If we get an ^( ) back, that's a request to run the code outside of
    // the console's sandbox and return its result.  It's possible to use
    // quoting to return simple values, like ^('x)

    assert(Is_Meta_Form_Of(GROUP, inst));

    bool threw = Eval_Any_List_At_Throws(out, inst, SPECIFIED);

    rebRelease(inst);

    return threw;  // act as if the BREAKPOINT call itself threw
}


//
//  export breakpoint*: native [
//
//  "Signal breakpoint to the host, but do not participate in evaluation"
//
//      return: ~
//  ]
//
DECLARE_NATIVE(BREAKPOINT_P)
//
// !!! Need definition to test for N_DEBUGGER_breakpoint function
{
    if (Do_Breakpoint_Throws(
        SPARE,
        false,  // not a Ctrl-C, it's an actual BREAKPOINT
        LIB(SPACE)  // default result if RESUME does not override
    )){
        return THROWN;
    }

    // !!! Should use a more specific protocol (e.g. pass in END).  But also,
    // this provides a possible motivating case for functions to be able to
    // return *either* a value or no-value...if breakpoint were variadic, it
    // could splice in a value in place of what comes after it.
    //
    if (not Is_Quasi_Word(u_cast(Stable*, SPARE)))
        panic ("BREAKPOINT invisible, can't RESUME:WITH code (use PAUSE)");

    return TRASH;
}


//
//  export pause: native [
//
//  "Pause in the debugger before running the provided code"
//
//      return: [any-stable?]
//      @code [group!]  ; or LIT-WORD! name or BLOCK! for dialect
//          "Run the given code if breakpoint does not override"
//  ]
//
DECLARE_NATIVE(PAUSE)
//
// !!! Need definition to test for N_DEBUGGER_pause function
{
    INCLUDE_PARAMS_OF_PAUSE;

    if (Do_Breakpoint_Throws(
        OUT,
        false,  // not a Ctrl-C, it's an actual BREAKPOINT
        ARG(CODE)  // default result if RESUME does not override
    )){
        return THROWN;
    }

    return OUT;
}


//
//  export resume: native [
//
//  "Resume after a breakpoint, can evaluate code in the breaking context"
//
//      return: []
//      expression "Evaluate the given code as return value from BREAKPOINT"
//          [<end> block!]
//  ]
//
DECLARE_NATIVE(RESUME)
//
// The CONSOLE makes a wall to prevent arbitrary THROWs and FAILs from ending
// a level of interactive inspection.  But RESUME is special, (with a throw
// :NAME of the RESUME native) to signal an end to the interactive session.
//
// When the BREAKPOINT native gets control back from CONSOLE, it evaluates
// a given expression.
//
// !!! Initially, this supported :AT
//
//      :at "Return from another call up stack besides the breakpoint"
//          [frame! integer!]
//
// While an interesting feature, it's not currently a priority.  (It can be
// accomplished with something like `resume [unwind ...]`)
{
    INCLUDE_PARAMS_OF_RESUME;

    Element* expr = Element_ARG(EXPRESSION);
    assert(Is_Block(expr));
    Metafy_Cell(expr);

    // We throw with :NAME as identity of the RESUME function.  (Note: there
    // is no NATIVE() variant for extensions yet.  Extract from current level.)
    //
    DECLARE_STABLE (resume);
    Init_Frame(
        resume,
        Level_Phase(LEVEL),
        Level_Label(LEVEL),
        Level_Coupling(LEVEL)
    );

    // We don't want to run the expression yet.  If we tried to run code from
    // this stack level--and it failed or threw--we'd stay stuck in the
    // breakpoint's sandbox.  We throw it as-is and it gets evaluated later.
    //
    Init_Thrown_With_Label(LEVEL, expr, resume);
    return BOUNCE_THROWN;
}



//
//  export step: native [
//
//  "Perform a step in the debugger"
//
//      return: ~
//      amount [<end> word! integer!]
//          "Number of steps to take (default is 1) or IN, OUT, OVER"
//  ]
//
DECLARE_NATIVE(STEP)
{
    INCLUDE_PARAMS_OF_STEP;
    UNUSED(ARG(AMOUNT));
    panic ("STEP's methodology was deprecated, it is being re-implemented");
}
