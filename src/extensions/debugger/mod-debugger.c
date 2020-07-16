//
//  File: %mod-debugger.c
//  Summary: "Native Functions for debugging"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015-2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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
// !!! Interactive debugging is a work in progress; R3-Alpha had no breakpoint
// facility or otherwise, so this is all new.  Writing a debugger that has
// usermode code is made complicated by Rebol's single-threaded model, because
// it means the debugger implementation itself has to be careful to avoid
// being seen on the stack.
//

#include "sys-core.h"

#include "tmp-mod-debugger.h"


// Index values for the properties in a "resume instruction" (see notes on
// REBNATIVE(resume))
//
enum {
    RESUME_INST_MODE = 0,   // FALSE if /WITH, TRUE if /DO, BLANK! if default
    RESUME_INST_PAYLOAD,    // code block to /DO or value of /WITH
    RESUME_INST_TARGET,     // unwind target, BLANK! to return from breakpoint
    RESUME_INST_MAX
};


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
    bool interrupted, // Ctrl-C (as opposed to a BREAKPOINT)
    const REBVAL *default_value,
    bool do_default
){
    UNUSED(interrupted); // not passed to the REPL, should it be?

    REBVAL *inst = rebValue("console/resumable", rebEND);

    if (IS_INTEGER(inst)) {
        Move_Value(out, KNOWN(NAT_VALUE(quit)));
        CONVERT_NAME_TO_THROWN(out, inst);
        rebRelease(inst);
        return true;
    }

    assert(IS_PATH(inst));
    assert(VAL_LEN_HEAD(inst) == RESUME_INST_MAX);

    const REBVAL *mode = KNOWN(VAL_ARRAY_AT_HEAD(inst, RESUME_INST_MODE));
    const REBVAL *payload
        = KNOWN(VAL_ARRAY_AT_HEAD(inst, RESUME_INST_PAYLOAD));
    REBVAL *target = KNOWN(VAL_ARRAY_AT_HEAD(inst, RESUME_INST_TARGET));
    rebRelease(inst);

    assert(IS_BLANK(target)); // for now, no /AT

    if (IS_BLANK(mode)) {
        //
        // If the resume instruction had no /DO or /WITH of its own,
        // then it doesn't override whatever the breakpoint provided
        // as a default.  (If neither the breakpoint nor the resume
        // provided a /DO or a /WITH, result will be void.)
        //
        payload = default_value;
        if (do_default)
            mode = TRUE_VALUE;
        else
            mode = FALSE_VALUE;
    }

    assert(IS_LOGIC(mode));

    if (VAL_LOGIC(mode)) {
        if (Do_Any_Array_At_Throws(out, payload)) {
            if (not IS_BLANK(target)) // throwing incompatible with /AT
                fail (Error_No_Catch_For_Throw(out));

            return true; // act as if the BREAKPOINT call itself threw
        }

        // Ordinary evaluation result...
    }
    else
        Move_Value(out, payload);

    return false;
}


//
//  export breakpoint: native [
//
//  "Signal breakpoint to the host, but do not participate in evaluation"
//
//      return: []
//          {Returns nothing, not even void ("invisible", like COMMENT)}
//  ]
//
REBNATIVE(breakpoint)
//
// !!! Need definition to test for N_DEBUGGER_breakpoint function
{
    if (Do_Breakpoint_Throws(
        D_OUT,
        false, // not a Ctrl-C, it's an actual BREAKPOINT
        NULLED_CELL, // default result if RESUME does not override
        false // !execute (don't try to evaluate the NULLED_CELL)
    )){
        return R_THROWN;
    }

    // !!! Should use a more specific protocol (e.g. pass in END).  But also,
    // this provides a possible motivating case for functions to be able to
    // return *either* a value or no-value...if breakpoint were variadic, it
    // could splice in a value in place of what comes after it.
    //
    if (not IS_NULLED(D_OUT))
        fail ("BREAKPOINT is invisible, can't RESUME/WITH code (use PAUSE)");

    return R_INVISIBLE;
}


//
//  export pause: native [
//
//  "Pause in the debugger before running the provided code"
//
//      return: [<opt> any-value!]
//          "Result of the code evaluation, or RESUME/WITH value if override"
//      :code [group!] ;-- or LIT-WORD! name or BLOCK! for dialect
//          "Run the given code if breakpoint does not override"
//  ]
//
REBNATIVE(pause)
//
// !!! Need definition to test for N_DEBUGGER_pause function
{
    DEBUGGER_INCLUDE_PARAMS_OF_PAUSE;

    if (Do_Breakpoint_Throws(
        D_OUT,
        false, // not a Ctrl-C, it's an actual BREAKPOINT
        ARG(code), // default result if RESUME does not override
        true // execute (run the GROUP! as code, don't return as-is)
    )){
        return R_THROWN;
    }

    return D_OUT;
}
