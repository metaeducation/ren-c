//
//  file: %n-system.c
//  summary: "native functions for system operations"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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

#include "sys-core.h"


//
//  halt: native [
//
//  "Stops evaluation and returns to the input prompt"
//
//      return: [<divergent>]
//  ]
//
DECLARE_NATIVE(HALT)
{
    INCLUDE_PARAMS_OF_HALT;

    Init_Thrown_With_Label(LEVEL, LIB(NULL), LIB(HALT));
    return BOUNCE_THROWN;
}


//
//  exit-process: native [
//
//  "Stop the interpreter, return exit status"
//
//      return: [<divergent>]
//      status "See: http://en.wikipedia.org/wiki/Exit_status"
//          [integer!]
//      :abrupt "Don't shut down, end process immediately (leaks in Valgrind)"
//  ]
//
DECLARE_NATIVE(EXIT_PROCESS)  // moved to SYS.UTIL/EXIT by boot
{
    INCLUDE_PARAMS_OF_EXIT_PROCESS;

    int status = VAL_INT32(ARG(STATUS));  // exit() takes an int

    if (ARG(ABRUPT))  // doesn't run Shutdown_Core()
        exit(status);

    exit(status);  // !!! Clean shutdown interop with trampoline TBD

  #if defined(__TINYC__)
    DEAD_END;  // TCC doesn't recognize GCC's no return attribute on exit()
  #endif
}


//
//  recycle: native [
//
//  "Recycles unused memory"
//
//      return: [
//          integer!  "Number of Stubs/Pairings recycled (if applicable)"
//          trash!    "if recycling disabled or not requested"
//      ]
//      :off "Disable auto-recycling"
//      :on "Enable auto-recycling"
//      :ballast "Trigger for auto-recycle (memory used)"
//          [integer!]
//      :torture "Constant recycle (for internal debugging)"
//      :watch "Monitor recycling (debug only)"
//      :verbose "Dump information about Flexes being recycled (debug only)"
//  ]
//
DECLARE_NATIVE(RECYCLE)
{
    INCLUDE_PARAMS_OF_RECYCLE;

    if (ARG(OFF)) {
        g_gc.disabled = true;
        return TRASH;
    }

    if (ARG(ON)) {
        g_gc.disabled = false;
        g_gc.ballast = MEM_BALLAST;
    }

    if (ARG(BALLAST)) {
        g_gc.disabled = false;
        g_gc.ballast = VAL_INT32(unwrap ARG(BALLAST));
    }

    if (ARG(TORTURE)) {
        g_gc.disabled = false;
        g_gc.ballast = 0;
    }

    if (g_gc.disabled)
        return TRASH;  // don't give misleading "0", since no recycle ran

    REBLEN count;

    if (ARG(VERBOSE)) {
      #if RUNTIME_CHECKS
        require (
          Flex* sweeplist = Make_Flex(FLAG_FLAVOR(FLAVOR_NODELIST), 100)
        );
        count = Recycle_Core(sweeplist);
        assert(count == Flex_Used(sweeplist));

        Index index = 0;
        for (index = 0; index < count; ++index) {
            Base* base = *Flex_At(Base*, sweeplist, index);
            PROBE(base);
            UNUSED(base);
        }

        Free_Unmanaged_Flex(sweeplist);

        REBLEN recount = Recycle_Core(nullptr);
        assert(recount == count);
      #else
        panic (Error_Checked_Build_Only_Raw());
      #endif
    }
    else {
        count = Recycle();
    }

    if (ARG(WATCH)) {
      #if RUNTIME_CHECKS
        // There might should be some kind of generic way to set these kinds
        // of flags individually, perhaps having them live in SYSTEM/...
        //
        g_gc.watch_recycle = not g_gc.watch_recycle;
        g_mem.watch_expand = not g_mem.watch_expand;
      #else
        panic (Error_Checked_Build_Only_Raw());
      #endif
    }

    return Init_Integer(OUT, count);
}


//
//  limit-usage: native [
//
//  "Set a usage limit only once (used for SECURE)"
//
//      return: ~
//      field [~(eval memory)~]
//      limit [any-number?]
//  ]
//
DECLARE_NATIVE(LIMIT_USAGE)
{
    INCLUDE_PARAMS_OF_LIMIT_USAGE;

    Option(SymId) sym = Word_Id(ARG(FIELD));

    // !!! comment said "Only gets set once"...why?
    //
    if (sym == SYM_EVAL) {
        if (not g_ts.eval_cycles_limit)
            g_ts.eval_cycles_limit = Int64(ARG(LIMIT));
    }
    else if (sym == SYM_MEMORY) {
        if (not g_mem.usage_limit)
            g_mem.usage_limit = Int64(ARG(LIMIT));
    }
    else
        panic (PARAM(FIELD));

    return TRASH;
}


//
//  check: native [
//
//  "Run integrity check on value in RUNTIME_CHECKS builds of the interpreter"
//
//      return: [any-stable?]
//      value "System will terminate abnormally if this value is corrupt"
//          [any-stable?]
//  ]
//
DECLARE_NATIVE(CHECK)  // !!! Review the necessity of this (hasn't been used)
//
// This forces an integrity check to run on a series.  In R3-Alpha there was
// no checked build, so this was a simple validity check and it returned an
// error on not passing.  But Ren-C is designed to have a build with
// checks that aren't designed to fail gracefully.  So this just runs that
// assert rather than replicating code here that can "tolerate" a bad series.
{
    INCLUDE_PARAMS_OF_CHECK;

  #if RUNTIME_CHECKS
    Stable* value = ARG(VALUE);

    Check_Memory_Debug();  // if memory is bad, all other bets are off

    if (Any_Series(value)) {
        Assert_Flex(Cell_Flex(value));
    }
    else if (Is_Frame(value)) {
        Assert_Flex(Phase_Keylist(Frame_Phase(value)));
        Assert_Array(Details_Array(Phase_Details(Frame_Phase(value))));
    }
    else if (Any_Context(value)) {
        Assert_Varlist(Cell_Varlist(value));
    }

    return COPY(value);
  #else
    UNUSED(ARG(VALUE));
    panic (Error_Checked_Build_Only_Raw());
  #endif
}


// Fast count of number of binary digits in a number:
//
// https://stackoverflow.com/a/15327567/211160
//
int ceil_log2(unsigned long long x) {
    static const unsigned long long t[6] = {
        0xFFFFFFFF00000000ull,
        0x00000000FFFF0000ull,
        0x000000000000FF00ull,
        0x00000000000000F0ull,
        0x000000000000000Cull,
        0x0000000000000002ull
    };

    int y = (((x & (x - 1)) == 0) ? 0 : 1);
    int j = 32;
    int i;

    for (i = 0; i < 6; i++) {
    int k = (((x & t[i]) == 0) ? 0 : j);
        y += k;
        x >>= k;
        j >>= 1;
    }

    return y;
}


//
//  c-debug-tick: native [
//
//  "Get the evaluator tick count (currently only available in checked builds)"
//
//      return: [<null> integer!]
//  ]
//
DECLARE_NATIVE(C_DEBUG_TICK)
{
    INCLUDE_PARAMS_OF_C_DEBUG_TICK;

  #if TRAMPOLINE_COUNTS_TICKS
    return Init_Integer(OUT, g_tick);
  #else
    return NULLED;
  #endif
}


//
//  c-debug-break: ghostable native [
//
//  "Break at next evaluation point (only use when running under C debugger)"
//
//      return: [ghost!]
//  ]
//
DECLARE_NATIVE(C_DEBUG_BREAK)
//
// 1. We usually want to debug the callsite--not the C-DEBUG-BREAK function.
//    Hence the evaluator stepper should usually catch the C-DEBUG-BREAK call
//    and not actually dispatch it, breaking in the evaluator loop.  We still
//    keep a debug break here, in case the function is run through some other
//    means (like an APPLY).  You shouldn't usually be breaking here, though.
//
// 2. Any function that returns a value will interfere with an evaluation
//    stream; the only way something like (10 + c-debug-break 20) could work
//    would be if this function took over evaluation and continued the Level
//    without returning.  But with the trick in [1] we don't need to do any
//    of that, because we're basically never actually running this native.
{
    INCLUDE_PARAMS_OF_C_DEBUG_BREAK;

  #if INCLUDE_C_DEBUG_BREAK_NATIVE
      #if RUNTIME_CHECKS
        debug_break();  // usually we break in the evaluator, not here [1]
      #endif
        return GHOST;  // Note: [1] does (10 + c-debug-break 20), not this [2]
  #else
      panic (Error_Checked_Build_Only_Raw());
  #endif
}
