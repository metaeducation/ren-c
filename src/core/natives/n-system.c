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
//  exit: native [
//
//  "Stop the interpreter, return exit status"
//
//      return: [<divergent>]
//      status "See: http://en.wikipedia.org/wiki/Exit_status"
//          [integer!]
//      :abrupt "Don't shut down, end process immediately (leaks in Valgrind)"
//  ]
//
DECLARE_NATIVE(EXIT)  // moved to SYS.UTIL/EXIT by boot code, for safety
{
    INCLUDE_PARAMS_OF_EXIT;

    int status = VAL_INT32(ARG(STATUS));  // exit() takes an int

    if (Bool_ARG(ABRUPT))  // doesn't run Shutdown_Core()
        exit(status);

    exit(status);  // !!! Clean shutdown interop with trampoline TBD
}


//
//  recycle: native [
//
//  "Recycles unused memory"
//
//      return: "Number of Stubs/Pairings recycled (if applicable)"
//          [null? integer!]
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

    if (Bool_ARG(OFF)) {
        g_gc.disabled = true;
        return NULLED;
    }

    if (Bool_ARG(ON)) {
        g_gc.disabled = false;
        g_gc.ballast = MEM_BALLAST;
    }

    if (Bool_ARG(BALLAST)) {
        g_gc.disabled = false;
        g_gc.ballast = VAL_INT32(ARG(BALLAST));
    }

    if (Bool_ARG(TORTURE)) {
        g_gc.disabled = false;
        g_gc.ballast = 0;
    }

    if (g_gc.disabled)
        return NULLED;  // don't give misleading "0", since no recycle ran

    REBLEN count;

    if (Bool_ARG(VERBOSE)) {
      #if RUNTIME_CHECKS
        require (
          Flex* sweeplist = Make_Flex(FLAG_FLAVOR(FLAVOR_NODELIST), 100)
        );
        count = Recycle_Core(sweeplist);
        assert(count == Flex_Used(sweeplist));

        REBLEN index = 0;
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

    if (Bool_ARG(WATCH)) {
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
//      return: []
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

    return TRIPWIRE;
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
    Value* value = ARG(VALUE);

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
//      return: [null? integer!]
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
//  c-debug-break: native [
//
//  "Break at next evaluation point (only use when running under C debugger)"
//
//      return: [ghost!] "Invisible"
//  ]
//
DECLARE_NATIVE(C_DEBUG_BREAK)
//
// 1. If we are counting ticks, we can put off actually breaking until the
//    trampoline is right about to run the next step.  For instance with:
//
//        print c-debug-break mold value
//
//    Queue it so the break happens right before the MOLD.
//
// 2. In performant builds without TRAMPOLINE_COUNTS_TICKS but that still have
//    debugging information (e.g. callgrind builds) then C-DEBUG-BREAK can
//    still be useful.  Break right here in this native call...you'll have to
//    step up out into the evaluator stack manually to get to the next step.
{
    INCLUDE_PARAMS_OF_C_DEBUG_BREAK;

  #if (INCLUDE_C_DEBUG_BREAK_NATIVE)
    #if TRAMPOLINE_COUNTS_TICKS
        g_break_at_tick = g_tick + 1;  // queue break for next step [1]
        return Init_Unsurprising_Ghost(OUT);
     #else
      #if RUNTIME_CHECKS
        debug_break();  // break right here, now [2]
      #endif
        return Init_Unsurprising_Ghost(OUT);
      #endif
  #else
      panic (Error_Checked_Build_Only_Raw());
  #endif
}
