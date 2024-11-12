//
//  File: %sys-panic.h
//  Summary: "Force System Exit with Diagnostic Info"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// Panics are the equivalent of the "blue screen of death" and should never
// happen in normal operation.  Generally, it is assumed nothing under the
// user's control could fix or work around the issue, hence the main goal is
// to provide the most diagnostic information possible to devleopers.
//
// The best thing to do is to pass in whatever Cell or Flex subclass
// (including Array*, VarList*, Action*...) is a useful "smoking gun":
//
//     if (VAL_TYPE(value) == REB_QUASIFORM)
//         panic (value);  // checked build points out this file and line
//
//     if (Array_Len(array) < 2)
//         panic (array);  // panic is polymorphic, see Detect_Rebol_Pointer()
//
// But if no smoking gun is available, a UTF-8 string can also be passed to
// panic...and it will terminate with that as a message:
//
//     if (sizeof(foo) != 42)
//         panic ("invalid foo size");  // kind of redundant with file + line
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * It's desired that there be a space in `panic (...)` to make it look
//   more "keyword-like" and draw attention that it's a `noreturn` call.
//
// * The diagnostics are written in such a way that they give the "more likely
//   to succeed" output first, and then get more aggressive to the point of
//   possibly crashing by dereferencing corrupt memory which triggered the
//   panic.  The checked build diagnostics will be more exhaustive, but the
//   release build gives some info.
//

#if TRAMPOLINE_COUNTS_TICKS
    #define TICK g_tick
#else
    #define TICK u_cast(Tick, 0)  // for TRAMPOLINE_COUNTS_TICKS agnostic code
#endif


#if DEBUG_FANCY_PANIC
    #define panic(v) \
        Panic_Core((v), TICK, __FILE__, __LINE__)

    #define panic_at(v,file,line) \
        Panic_Core((v), TICK, (file), (line))
#else
    #define panic(v) \
        Panic_Core((v), TICK, nullptr, 0)

    #define panic_at(v,file,line) \
        UNUSED(file); \
        UNUSED(line); \
        panic(v)
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  TICK-RELATED FUNCTIONS <== **THESE ARE VERY USEFUL**
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Each trampoline step bumps a global count, that in deterministic repro
// cases can be very helpful in identifying the "Tick" where certain problems
// are occurring.  The checked build pokes these Ticks lots of places--into
// Cells when they are formatted, into Flexes when they are allocated
// or freed, or into stack Levels each time they perform a new operation.
//
// BREAK_NOW() will show the stack status at the right moment.  If you have a
// reproducible Tick count, then BREAK_ON_TICK() is useful.  See also
// TICK_BREAKPOINT in %c-eval.c for a description of all the places the debug
// build hides Tick counts which may be useful for sleuthing bug origins.
//
// The SPORADICALLY() macro uses the count to allow flipping between different
// behaviors in checked builds--usually runs the release behavior some of the
// time, and the checked behavior some of the time.
//

#define BREAK_NOW() /* macro means no C stack frame, breaks at callsite */ \
    do { \
        printf("BREAK_ON_TICK(%" PRIu64 ")\n", TICK); \
        fflush(stdout); \
        Dump_Level_Location(TOP_LEVEL); \
        debug_break(); /* see %debug_break.h */ \
    } while (false)

#if TRAMPOLINE_COUNTS_TICKS
    #define BREAK_ON_TICK(tick) \
        if (tick == g_tick) BREAK_NOW()
#endif

#if NO_RUNTIME_CHECKS || (! TRAMPOLINE_COUNTS_TICKS)
    #define SPORADICALLY(modulus)  false
#else
    #define SPORADICALLY(modulus) \
        (g_tick % modulus == 0)
#endif

// Generally, you should prefer SPORADICALLY.  But some cases, like wanting
// to do a periodic startup behavior, doesn't work with that.
//
// !!! Use this very sparingly, and with a small modulus!  If you do something
// half the time (modulus = 2) then it will only reproduce half the time, but
// that's probably enough to still catch whatever you're testing.
//
#if ALLOW_SPORADICALLY_NON_DETERMINISTIC
    #define SPORADICALLY_NON_DETERMINISTIC(modulus) \
        (rand() % modulus == 0)
#else
    #define SPORADICALLY_NON_DETERMINISTIC  SPORADICALLY
#endif
