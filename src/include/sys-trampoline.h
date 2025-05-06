//
//  file: %sys-trampoline.h
//  summary: "Trampoline Flags And Other Header Definitions"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022-2024 Ren-C Open Source Contributors
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
// See %c-trampoline.c for an explanation of the Trampoline concept.
//


//=//// TRAMPOLINE_FLAG_RECYCLE ///////////////////////////////////////////=//
//
// The recycle flag indicates a need to run the garbage collector, when
// running it synchronously could be dangerous.  This is important e.g. during
// memory allocation, which can detect crossing a memory usage boundary that
// suggests GC'ing would be good...but might be in the middle of code that is
// halfway through manipulating a managed Flex.  Recycling does not happen
// until the trampoline regains control.
//
#define TRAMPOLINE_FLAG_RECYCLE \
    FLAG_LEFT_BIT(0)


//=//// TRAMPOLINE_FLAG_HALT //////////////////////////////////////////////=//
//
// The halt flag requests returning to the topmost level of the evaluator,
// regardless of how deep a debug stack might be.  The trampoline will only
// react to it when the top level doesn't have LEVEL_FLAG_UNINTERRUPTIBLE set.
// Clients of the API can choose to react to interruption outside of the
// evaluator by asking for rebWasHaltRequested().
//
#define TRAMPOLINE_FLAG_HALT \
    FLAG_LEFT_BIT(1)


//=//// TRAMPOLINE_FLAG_DEBUG_BREAK ///////////////////////////////////////=//
//
// The Debug Break request indicates a desire to enter an interactive
// debugging state.  Because the ability to manage such a state may not be
// registered by the host, this could generate an error.
//
#define TRAMPOLINE_FLAG_DEBUG_BREAK \
    FLAG_LEFT_BIT(2)


INLINE void Set_Trampoline_Flag_Core(Flags f) { // used in %sys-series.h
    g_ts.signal_flags |= f;

    if (g_ts.eval_countdown == -1)  // already set to trigger on next tick...
        return;  // ...we already reconciled the dose

    assert(g_ts.eval_countdown > 0);  // transition to 0 triggers signals

    // This forces the next step in the evaluator to count down to 0 and
    // trigger an interrupt.  But we have to reconcile the count first.
    //
    uintptr_t delta = g_ts.eval_dose - g_ts.eval_countdown;
    if (UINTPTR_MAX - g_ts.total_eval_cycles > delta)
        g_ts.total_eval_cycles += delta;
    else
        g_ts.total_eval_cycles = UINTPTR_MAX;

  #if TRAMPOLINE_COUNTS_TICKS
    assert(g_ts.total_eval_cycles == g_tick);
  #endif

    g_ts.eval_countdown = -1;
}

#define Set_Trampoline_Flag(name) \
    Set_Trampoline_Flag_Core(TRAMPOLINE_FLAG_##name)

#define Get_Trampoline_Flag(name) \
    (did (g_ts.signal_flags & TRAMPOLINE_FLAG_##name))

#define Clear_Trampoline_Flag(name) \
    g_ts.signal_flags &= (~ TRAMPOLINE_FLAG_##name)


//=//// FAKE DEBUGGING MODE FLAG //////////////////////////////////////////=//
//
// While development of an interactive debugger has been out of reach for a
// long time, the system is being continually designed with the idea of
// supporting it.
//
// One of the key premises of being able to implement a stepwise debugger is
// that instead of being trapped in tight optimization loops, there is a
// continuous ability to "bounce" out to the trampoline...yielding a locus
// of control where a debugger can be put.  A good example of this is that
// evaluating an block like [1 + 2, 3 + 4] shouldn't stay nested in a single
// C stack level where it churns forward to the next expression without
// giving up control--however tempting it might be to style the code to
// optimize in a tight loop.
//
// As an attempt to have our cake and eat it too, the idea is that certain
// code is sensitive to whether a debug mode is engaged or not.  If it is not
// then it can pursue more aggressive optimizations and skip over yields.
// But if the mode is enabled, then even situations like (eval []) must yield
// to show that an empty block is there and getting stepped over...vs. just
// pretending it isn't there.
//
// Due to this debugger not existing yet, one thing we can do is to make a
// test in the RUNTIME_CHECKS build that will sporadically answer "yes, we are
// in a debugger mode".  This is done by using a deterministic modulus of
// the trampoline's tick count...which approximates random behavior while
// still giving reproducible code paths between runs.  This way the code paths
// that would be used by the debugger can be exercised, and by choosing a
// relatively sparse modulus, the performance impact is not too bad.
//
// Something to note is that if a decision is made based on being in the
// debug mode, enough memory of that decision has to be kept in order to be
// coherent...since the next time you ask if it's in debug mode the answer
// may be different.  (This is likely to be true of a real debug mode too,
// that might be switched on and off at arbitrary moments of runtime, so it's
// probably a good exercise.)
//
// Each call to In_Debug_Mode() can give its own modulus, so that if an
// operation is particularly costly a larger modulus can be used.

#define In_Debug_Mode(n)  SPORADICALLY(n)
#define In_Optimized_Mode(n)  (not SPORADICALLY(n))
