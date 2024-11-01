//
//  File: %sys-trampoline.h
//  Summary: "Trampoline Flags And Other Header Definitions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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


//=////////////////////////////////////////////////////////////////////////=//
//
//     !!! EVALUATOR TICK COUNT - VERY USEFUL - READ THIS SECTION !!!
//
//=////////////////////////////////////////////////////////////////////////=//

// The evaluator `tick` should be visible in the C debugger watchlist as a
// local variable on each evaluator stack level.  So if a fail() happens at a
// deterministic moment in a run, capture the number from the level of interest
// and recompile for a breakpoint at that tick.
//
// If the tick is AFTER command line processing is done, you can request a tick
// breakpoint that way with `--breakpoint NNN`
//
// The debug build carries ticks many other places.  Stubs contain `Stub.tick`
// when created, levels have a `Level.tick`, and the DEBUG_TRACK_EXTEND_CELLS
// switch will double the size of cells so they can carry the tick, file, and
// line where they were initialized.
//
// For custom updating of stored ticks to help debugging some scenarios, see
// Touch_Stub() and Touch_Cell().  Note also that BREAK_NOW() can be called to
// pause and dump state at any moment.

#if RUNTIME_CHECKS && TRAMPOLINE_COUNTS_TICKS
    extern Tick g_break_at_tick;  // in %c-trampoline.c for easy setting

    #define Update_Tick_If_Enabled() \
        do { \
            if (g_ts.tick < UINT64_MAX) /* avoid rollover */ \
                g_ts.tick += 1; \
        } while (false)  // macro so that breakpoint is at right stack level!

    #define Maybe_DebugBreak_On_Tick() \
        do { \
            if ( \
                g_break_at_tick != 0 and g_ts.tick >= g_break_at_tick \
            ){ \
                printf("BREAK_ON_TICK(%" PRIu64 ")\n", g_ts.tick); \
                Dump_Level_Location(level_); \
                debug_break(); /* see %debug_break.h */ \
                g_break_at_tick = 0; \
            } \
        } while (false)  // macro so that breakpoint is at right stack level!
#else
    #define Update_Tick_If_Enabled() NOOP
    #define Maybe_DebugBreak_On_Tick() NOOP
#endif


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
    assert(g_ts.total_eval_cycles == g_ts.tick);
  #endif

    g_ts.eval_countdown = -1;
}

#define Set_Trampoline_Flag(name) \
    Set_Trampoline_Flag_Core(TRAMPOLINE_FLAG_##name)

#define Get_Trampoline_Flag(name) \
    (did (g_ts.signal_flags & TRAMPOLINE_FLAG_##name))

#define Clear_Trampoline_Flag(name) \
    g_ts.signal_flags &= (~ TRAMPOLINE_FLAG_##name)
