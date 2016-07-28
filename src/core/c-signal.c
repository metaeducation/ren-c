//
//  File: %c-signal.c
//  Summary: "Evaluator Interrupt Signal Handling"
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
// "Signal" refers to special events to process periodically during
// evaluation. Search for SET_SIGNAL to find them.
//
// (Note: Not to be confused with SIGINT and unix "signals", although on
// unix an evaluator signal can be triggered by a unix signal.)
//
// Note in signal dispatch that R3-Alpha did not have a policy articulated on
// dealing with the interrupt nature of the SIGINT signals sent by Ctrl-C:
//
// https://en.wikipedia.org/wiki/Unix_signal
//
// Guarding against errors being longjmp'd when an evaluation is in effect
// isn't the only time these signals are processed.  Rebol's Process_Signals
// currently happens during I/O, such as printing output.  As a consequence,
// a Ctrl-C can be picked up and then triggered during an Out_Value, jumping
// the stack from there.
//
// This means a top-level trap must always be in effect, even though no eval
// is running.  This trap's job is to handle errors that happen *while
// reporting another error*, with Ctrl-C triggering a HALT being the most
// likely example if not running an evaluation (though any fail() could
// cause it)
//

#include "sys-core.h"


//
//  Do_Signals_Throws: C
//
// !!! R3-Alpha's evaluator loop had a countdown (Eval_Count) which was
// decremented on every step.  When this counter reached zero, it would call
// this routine to process any "signals"...which could be requests for
// garbage collection, network-related, Ctrl-C being hit, etc.
//
// It also would check the Eval_Signals mask to see if it was non-zero on
// every step.  If it was, then it would always call this routine--regardless
// of the Eval_Count.
//
// While a broader review of how signals would work in Ren-C is pending, it
// seems best to avoid checking two things each step.  So only the Eval_Count
// is checked, and places that set Eval_Signals set it to 1...to have the
// same effect as if it were being checked.  Then if the Eval_Signals are
// not cleared by the end of this routine, it resets the Eval_Count to 1
// rather than giving it the full EVAL_DOSE of counts until next call.
//
// Currently the ability of a signal to THROW comes from the processing of
// breakpoints.  The RESUME instruction is able to execute code with /DO,
// and that code may escape from a debug interrupt signal (like Ctrl-C).
//
REBOOL Do_Signals_Throws(REBVAL *out)
{
    REBOOL thrown = FALSE;
    SET_VOID(out);

#if !defined(NDEBUG)
    if (!Saved_State && PG_Boot_Phase >= BOOT_MEZZ) {
        printf(
            "WARNING: Printing while no top-level trap handler in place\n"
            "This is dangerous because of Ctrl-C processing, there's\n"
            "nothing to catch the HALT signal.\n"
        );
        fflush(stdout);
    }
#endif

    REBCNT mask = Eval_Sigmask;
    REBCNT sigs = Eval_Signals & mask;

    if (!sigs) goto done;

    // Be careful of signal loops! EG: do not PRINT from here.

    Eval_Sigmask = 0;   // avoid infinite loop

    // Check for recycle signal:
    if (GET_FLAG(sigs, SIG_RECYCLE)) {
        CLR_SIGNAL(SIG_RECYCLE);
        Recycle();
    }

#ifdef NOT_USED_INVESTIGATE
    if (GET_FLAG(sigs, SIG_EVENT_PORT)) {  // !!! Why not used?
        CLR_SIGNAL(SIG_EVENT_PORT);
        Awake_Event_Port();
    }
#endif

    // Breaking only allowed after MEZZ boot
    //
    if (GET_FLAG(sigs, SIG_INTERRUPT) && PG_Boot_Phase >= BOOT_MEZZ) {
        CLR_SIGNAL(SIG_INTERRUPT);
        Eval_Sigmask = mask;

        if (Do_Breakpoint_Throws(out, TRUE, VOID_CELL, FALSE))
            thrown = TRUE;

        goto done;
    }

    // Halting only allowed after MEZZ boot
    //
    if (GET_FLAG(sigs, SIG_HALT) && PG_Boot_Phase >= BOOT_MEZZ) {
        CLR_SIGNAL(SIG_HALT);
        Eval_Sigmask = mask;

        fail (VAL_CONTEXT(TASK_HALT_ERROR));
    }

    Eval_Sigmask = mask;

done:
    if (Eval_Signals)
        Eval_Count = 1; // will call this routine again on next evaluation
    else {
        // Accumulate evaluation counter and reset countdown:

        if (Eval_Limit != 0 && Eval_Cycles > Eval_Limit)
            Check_Security(Canon(SYM_EVAL), POL_EXEC, 0);

        Eval_Cycles += Eval_Dose - Eval_Count;
        Eval_Count = Eval_Dose;
    }

    return thrown;
}
