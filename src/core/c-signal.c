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

#include "sys-core.h"


//
//  Do_Signals_Throws: C
//
// !!! R3-Alpha's evaluator loop had a countdown (eval_countdown) which was
// decremented on every step.  When this counter reached zero, it would call
// this routine to process any "signals"...which could be requests for
// garbage collection, network-related, Ctrl-C being hit, etc.
//
// It also would check the eval_signals mask to see if it was non-zero on
// every step.  If it was, then it would always call this routine--regardless
// of the eval_countdown.
//
// While a broader review of how signals would work in Ren-C is pending, it
// seems best to avoid checking two things each step.  So only eval_countdown
// is checked, and places that set eval_signals set eval_countdown it to 1.
// Then if the eval_signals are not cleared by the end of this routine,
// it resets the eval_countdown to 1 rather than giving it the full
// EVAL_DOSE of counts until next call.  Same outcome, but cheaper.
//
// Currently the ability of a signal to THROW comes from the processing of
// breakpoints.  The RESUME instruction is able to execute code with /DO,
// and that code may escape from a debug interrupt signal (like Ctrl-C).
//
bool Do_Signals_Throws(Level* level_)
{
    if (g_ts.eval_countdown >= 0) {  // natural countdown or invocation
        //
        // Periodic reconciliation of total evaluation cycles.  Avoids needing
        // to touch *both* eval_countdown and total_eval_cycles on every eval.
        //
        uintptr_t delta = g_ts.eval_dose - g_ts.eval_countdown;
        if (UINTPTR_MAX - g_ts.total_eval_cycles > delta)
            g_ts.total_eval_cycles += delta;
        else
            g_ts.total_eval_cycles = UINTPTR_MAX;
    }
    else if (g_ts.eval_countdown == -2) {
        //
        // SET_SIGNAL() sets the countdown to -1, which then reaches -2 on
        // a tick of the evaluator.  We *only* add that one tick, because
        // reconciliation was already performed.
        //
        if (g_ts.total_eval_cycles < UINTPTR_MAX)
            g_ts.total_eval_cycles += 1;
    }
    else {
        // This means SET_SIGNAL() ran, and Do_Signals_Throws() was called
        // before the evaluator was called.  That can happen with the manual
        // call in Prin_OS_String at time of writing.  There's no tick that
        // needs accounting for in this case.
        //
        assert(g_ts.eval_countdown == -1);
    }

  #if DEBUG_COUNT_TICKS
    assert(g_ts.total_eval_cycles == TG_tick);
  #endif

    g_ts.eval_countdown = g_ts.eval_dose;

    bool thrown = false;

    // The signal mask allows the system to disable processing of some
    // signals.  It defaults to ALL_BITS, but during signal processing
    // itself, the mask is set to 0 to avoid recursion.
    //
    Flags filtered_sigs = g_ts.eval_signals & g_ts.eval_sigmask;
    Flags saved_sigmask = g_ts.eval_sigmask;
    g_ts.eval_sigmask = 0;

    // "Be careful of signal loops! EG: do not PRINT from here."

    if (filtered_sigs & SIG_RECYCLE) {
        CLR_SIGNAL(SIG_RECYCLE);
        Recycle();
    }

    if (filtered_sigs & SIG_HALT) {
        //
        // Early in the booting process, it's not possible to handle Ctrl-C.
        //
        if (g_ts.jump_list == nullptr)
            panic ("Ctrl-C or other HALT signal with no rescue to process it");

        CLR_SIGNAL(SIG_HALT);
        g_ts.eval_sigmask = saved_sigmask;

        Init_Thrown_With_Label(LEVEL, Lib(NULL), Lib(HALT));
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
        g_ts.eval_sigmask = saved_sigmask;

        // !!! If implemented, this would allow triggering a breakpoint
        // with a keypress.  This needs to be thought out a bit more,
        // but may not involve much more than running `BREAKPOINT`.
        //
        fail ("BREAKPOINT from SIG_INTERRUPT not currently implemented");
    }

    g_ts.eval_sigmask = saved_sigmask;
    return thrown;
}
