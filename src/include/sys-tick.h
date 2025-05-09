//
//  file: %sys-tick.h
//  summary: "Definitions pertaining to the tick count"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
//     !!! BREAKING ON TICKS - VERY USEFUL - READ THROUGH THIS FILE !!!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// In builds with TRAMPOLINE_COUNTS_TICKS, each bounce of the Trampoline will
// increment a counter.  This is useful for debugging, since so long as your
// code is deterministic it will reach the same tick count each time something
// happens, meaning you can use it as a trigger for breakpoints.
//
// Checked builds have switches for carrying ticks many places.  Levels have
// `Level.tick`.  If DEBUG_STUB_ORIGINS is enabled, then stubs will be
// expanded and contain `Stub.tick`.  If DEBUG_TRACK_EXTEND_CELLS is enabled,
// then Cells will have their size doubled to carry the tick, file, and line
// where they were last initialized or touched.
//
// For custom updating of stored ticks to help debugging some scenarios, see
// Touch_Stub() and Touch_Cell().
//
// The evaluator `tick` should be visible in the C debugger watchlist as a
// local variable on each evaluator stack level.  So if a panic() happens at a
// deterministic moment in a run, capture the number from the level of interest
// and recompile for a breakpoint at that tick.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * There was a command line processing switch called `--breakpoint TICK`
//   which was supposed to let you set the breakpoint at a particular tick.
//   However, the fact that the command line processing code is usermode
//   meant that it skewed the tick count.  There was code that would set
//   the tick count to some high fixed value after command line processing
//   was done--but this has atrophied.  Fixing it is TBD.
//


//=//// TRAMPOLINE TICK BREAKING, see %c-trampoline.c /////////////////////=//
//
// In %c-trampoline.c, you can edit a Tick value to break on and compile that
// in easily.  The break will occur in the trampoline.  If you want to break
// at a targeted location, use the BREAK_NOW() or BREAK_ON_TICK() macros.
//

#if RUNTIME_CHECKS && TRAMPOLINE_COUNTS_TICKS
    extern Tick g_break_at_tick;  // in %c-trampoline.c for easy setting

    #define Update_Tick_If_Enabled() \
        do { \
            if (g_tick < UINT64_MAX) /* avoid rollover */ \
                g_tick += 1; \
        } while (0)  // macro so that breakpoint is at right stack level!

    #define Maybe_Trampoline_Break_On_Tick(L) \
        do { \
            if ( \
                g_break_at_tick != 0 and g_tick == g_break_at_tick \
            ){ \
                printf("TRAMPOLINE g_break_at_tick = %" PRIu64 "\n", g_tick); \
                debug_break(); /* see %debug_break.h */ \
            } \
        } while (0)  // macro so that breakpoint is at right stack level!
#else
    #define Update_Tick_If_Enabled()  NOOP
    #define Maybe_Trampoline_Break_On_Tick(L)  NOOP
#endif

#define BREAK_NOW() /* macro means no C stack frame, breaks at callsite */ \
    do { \
        printf("BREAK_NOW() tick = %" PRIu64 ")\n", TICK); \
        fflush(stdout); \
        debug_break(); /* see %debug_break.h */ \
    } while (0)

#if TRAMPOLINE_COUNTS_TICKS
    #define BREAK_ON_TICK(tick) do { \
        if (tick == g_tick) { \
            printf("BREAK_ON_TICK(%" PRIu64 ")\n", tick); \
            fflush(stdout); \
            debug_break(); /* see %debug_break.h */ \
        } \
    } while (0)
#endif


//=//// SPORADIC BEHAVIOR SWITCHING //////////////////////////////////////=//
//
// There are behaviors that vary between the RUNTIME_CHECKS build and the
// NO_RUNTIME_CHECKS builds.  Since most day-to-day testing is done in the
// debug build it's good to throw in the occasional test of the release build
// behavior.  SPORADICALLY() does this deterministically, based on a count:
//
//     if (SPORADICALLY(10))  // branch in RUNTIME_CHECKS build, 1 in 10 times
//         Do_Release_Build_Behavior(...);
//     else
//         Do_Debug_Build_Behavior(...);
//
// In the NO_RUNTIME_CHECKS build, it will never run.  Another usage of this
// idea would be if there were a particularly expensive check that it was
// suitable to run occasionally:
//
//     #define EXPENSIVE_CHECK_INTERVAL  50
//
//     #if RUNTIME_CHECKS
//         if (SPORADICALLY(EXPENSIVE_CHECK_INTERVAL))
//            Do_Expensive_Runtime_Checks(...)
//         else
//            Do_Cheaper_Runtime_Checks(...)
//     #endif
//
// 1. SPORADICALLY() used to be based on g_tick.  But this had the unappealing
//    property of acting the same for all calls during the same tick.  We
//    don't want to increment g_tick outside of the trampoline, so this uses
//    a different counter that we can increment.
//
// 2. You should almost always prefer SPORADICALLY() to something based on
//    actual random numbers, because it's much harder to reproduce a case
//    that isn't deterministic.  Rare cases would be testing behaviors
//    during startup, that don't have any wildcard factor to vary g_sporadic.
//    If you use this, you should pick a small modulus, to make it easier to
//    reproduce the behavior.
//

#if NO_RUNTIME_CHECKS
    #define SPORADICALLY(modulus)  false
#else
    #define SPORADICALLY(modulus) \
        ((++g_sporadic) % modulus == 0)  // notice incrementation [1]
#endif

#if ALLOW_SPORADICALLY_NON_DETERMINISTIC  // use this sparingly! [2]
    #define SPORADICALLY_NON_DETERMINISTIC(modulus) \
        (rand() % modulus == 0)
#else
    #define SPORADICALLY_NON_DETERMINISTIC  SPORADICALLY
#endif
