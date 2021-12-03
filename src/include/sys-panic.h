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
// The best thing to do is to pass in whatever REBVAL* or REBSER* subclass
// (including REBARR*, REBCTX*, REBACT*...) is the most useful "smoking gun":
//
//     if (VAL_TYPE(value) == REB_BAD_WORD)
//         panic (value);  // debug build points out this file and line
//
//     if (ARR_LEN(array) < 2)
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
//   panic.  The debug build diagnostics will be more exhaustive, but the
//   release build gives some info.
//

#if DEBUG_COUNT_TICKS
    //
    // !!! The TG_Tick gets used in inline functions, and as a result it must
    // be defined earlier than when the %sys-globals.h file can be included.
    // This may be worked around by making sure all the types used in that
    // file are present in %reb-defs.h ... review.
    //
    extern REBTCK TG_Break_At_Tick;
#endif

#if DEBUG_COUNT_TICKS
    #if DEBUG_FANCY_PANIC
        #define panic(v) \
            Panic_Core((v), TG_Tick, __FILE__, __LINE__)

        #define panic_at(v,file,line) \
            Panic_Core((v), TG_Tick, (file), (line))
    #else
        #define panic(v) \
            Panic_Core((v), TG_Tick, NULL, 0)

        #define panic_at(v,file,line) \
            UNUSED(file); \
            UNUSED(line); \
            panic(v)

    #endif
#else
    #if DEBUG_FANCY_PANIC
        #define panic(v) \
            Panic_Core((v), 0, __FILE__, __LINE__)

        #define panic_at(v,file,line) \
            Panic_Core((v), 0, (file), (line))
    #else
        #define panic(v) \
            Panic_Core((v), 0, NULL, 0)

        #define panic_at(v,file,line) \
            UNUSED(file); \
            UNUSED(line); \
            panic(v)
    #endif
#endif



//
// PROGRAMMATIC C BREAKPOINT
//
// This header file brings in the ability to trigger a programmatic breakpoint
// in C code, by calling `debug_break();`  It is not supported by HaikuOS R1,
// so instead kick into an infinite loop which can be broken and stepped out
// of in the debugger.
//
#if INCLUDE_C_DEBUG_BREAK_NATIVE || DEBUG_COUNT_TICKS
    #if TO_HAIKU || TO_EMSCRIPTEN
        inline static void debug_break() {
            int x = 0;
          #if !defined(NDEBUG)
            printf("debug_break() called\n");
            fflush(stdout);
          #endif
            while (1) { ++x; }
            x = 0; // set next statement in debugger to here
        }
    #else
        #include "debugbreak.h"
    #endif
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  TICK-RELATED FUNCTIONS <== **THESE ARE VERY USEFUL**
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Each iteration of DO bumps a global count, that in deterministic repro
// cases can be very helpful in identifying the "tick" where certain problems
// are occurring.  The debug build pokes this ticks lots of places--into
// value cells when they are formatted, into series when they are allocated
// or freed, or into stack frames each time they perform a new operation.
//
// BREAK_NOW() will show the stack status at the right moment.  If you have a
// reproducible tick count, then BREAK_ON_TICK() is useful.  See also
// TICK_BREAKPOINT in %c-eval.c for a description of all the places the debug
// build hides tick counts which may be useful for sleuthing bug origins.
//
// The SPORADICALLY() macro uses the count to allow flipping between different
// behaviors in debug builds--usually to run the release behavior some of the
// time, and the debug behavior some of the time.  This exercises the release
// code path even when doing a debug build.
//

#define BREAK_NOW() /* macro means no stack frame, breaks at callsite */ \
    do { \
        printf("BREAK_ON_TICK() @ tick %ld\n", cast(long int, TG_Tick)); \
        fflush(stdout); \
        Dump_Frame_Location(nullptr, FS_TOP); \
        debug_break(); /* see %debug_break.h */ \
    } while (false)

#define BREAK_ON_TICK(tick) \
    if (tick == TG_Tick) BREAK_NOW()

#if defined(NDEBUG) || (! DEBUG_COUNT_TICKS)
    #define SPORADICALLY(modulus) \
        false
#else
    #define SPORADICALLY(modulus) \
        (TG_Tick % modulus == 0)
#endif
