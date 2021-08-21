//
//  File: %c-signal.c
//  Summary: "Evaluator Interrupt Signal Handling"
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
bool Do_Signals_Throws(REBVAL *out)
{
    // !!! When it was the case that the only way Do_Signals_Throws would run
    // due to the Eval_Count reaching the end of an Eval_Dose, this way of
    // doing "CPU quota" would work.  Currently, however, it is inaccurate,
    // due to the fact that Do_Signals_Throws can be queued to run by setting
    // the Eval_Count to 1 for a specific signal.  Review.
    //
    Eval_Cycles += Eval_Dose - Eval_Count;

    Eval_Count = Eval_Dose;

    bool thrown = false;

    // The signal mask allows the system to disable processing of some
    // signals.  It defaults to ALL_BITS, but during signal processing
    // itself, the mask is set to 0 to avoid recursion.
    //
    // !!! This seems overdesigned considering SIG_EVENT_PORT isn't used.
    //
    REBFLGS filtered_sigs = Eval_Signals & Eval_Sigmask;
    REBFLGS saved_sigmask = Eval_Sigmask;
    Eval_Sigmask = 0;

    // "Be careful of signal loops! EG: do not PRINT from here."

    if (filtered_sigs & SIG_RECYCLE) {
        CLR_SIGNAL(SIG_RECYCLE);
        Recycle();
    }

#ifdef NOT_USED_INVESTIGATE
    if (filtered_sigs & SIG_EVENT_PORT) {  // !!! Why not used?
        CLR_SIGNAL(SIG_EVENT_PORT);
        Awake_Event_Port();
    }
#endif

    if (filtered_sigs & SIG_HALT) {
        //
        // Early in the booting process, it's not possible to handle Ctrl-C.
        //
        if (TG_Jump_List == nullptr)
            panic ("Ctrl-C or other HALT signal with no trap to process it");

        CLR_SIGNAL(SIG_HALT);
        Eval_Sigmask = saved_sigmask;

        Init_Thrown_With_Label(out, NULLED_CELL, Native(HALT));
        return true; // thrown
    }

    if (filtered_sigs & SIG_INTERRUPT) {
        //
        // Similar to the Ctrl-C halting, the "breakpoint" interrupt request
        // can't be processed early on.  The throw mechanics should panic
        // all right, but it might make more sense to wait.
        //
        CLR_SIGNAL(SIG_INTERRUPT);

        // !!! This can recurse, which may or may not be a bad thing.  But
        // if the garbage collector and such are going to run during this
        // execution, the signal mask has to be turned back on.  Review.
        //
        Eval_Sigmask = saved_sigmask;

        // !!! If implemented, this would allow triggering a breakpoint
        // with a keypress.  This needs to be thought out a bit more,
        // but may not involve much more than running `BREAKPOINT`.
        //
        fail ("BREAKPOINT from SIG_INTERRUPT not currently implemented");
    }

    Eval_Sigmask = saved_sigmask;
    return thrown;
}
