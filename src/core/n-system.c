//
//  File: %n-system.c
//  Summary: "native functions for system operations"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//      return: []
//  ]
//
DECLARE_NATIVE(halt)
{
    INCLUDE_PARAMS_OF_HALT;

    return Init_Thrown_With_Label(LEVEL, Lib(NULL), Lib(HALT));
}


//
//  exit: native [
//
//  "Stop the interpreter, return exit status"
//
//      return: []
//      status "See: http://en.wikipedia.org/wiki/Exit_status"
//          [integer!]
//      /abrupt "Don't shut down, end process immediately (leaks in Valgrind)"
//  ]
//
DECLARE_NATIVE(exit)  // moved to SYS.UTIL/EXIT by boot code, for safety
{
    INCLUDE_PARAMS_OF_EXIT;

    int status = VAL_INT32(ARG(status));  // exit() takes an int

    if (REF(abrupt))  // doesn't run Shutdown_Core()
        exit(status);

    exit(status);  // !!! Clean shutdown interop with trampoline TBD
}


//
//  recycle: native [
//
//  "Recycles unused memory"
//
//      return: "Number of Flex Nodes recycled (if applicable)"
//          [~null~ integer!]
//      /off "Disable auto-recycling"
//      /on "Enable auto-recycling"
//      /ballast "Trigger for auto-recycle (memory used)"
//          [integer!]
//      /torture "Constant recycle (for internal debugging)"
//      /watch "Monitor recycling (debug only)"
//      /verbose "Dump information about Flexes being recycled (debug only)"
//  ]
//
DECLARE_NATIVE(recycle)
{
    INCLUDE_PARAMS_OF_RECYCLE;

    if (REF(off)) {
        g_gc.disabled = true;
        return nullptr;
    }

    if (REF(on)) {
        g_gc.disabled = false;
        g_gc.ballast = MEM_BALLAST;
    }

    if (REF(ballast)) {
        g_gc.disabled = false;
        g_gc.ballast = VAL_INT32(ARG(ballast));
    }

    if (REF(torture)) {
        g_gc.disabled = false;
        g_gc.ballast = 0;
    }

    if (g_gc.disabled)
        return nullptr; // don't give misleading "0", since no recycle ran

    REBLEN count;

    if (REF(verbose)) {
      #if defined(NDEBUG)
        fail (Error_Debug_Only_Raw());
      #else
        Flex* sweeplist = Make_Flex_Core(100, FLAG_FLAVOR(NODELIST));
        count = Recycle_Core(sweeplist);
        assert(count == Flex_Used(sweeplist));

        REBLEN index = 0;
        for (index = 0; index < count; ++index) {
            Node* node = *Flex_At(Node*, sweeplist, index);
            PROBE(node);
            UNUSED(node);
        }

        Free_Unmanaged_Flex(sweeplist);

        REBLEN recount = Recycle_Core(nullptr);
        assert(recount == count);
      #endif
    }
    else {
        count = Recycle();
    }

    if (REF(watch)) {
      #if defined(NDEBUG)
        fail (Error_Debug_Only_Raw());
      #else
        // There might should be some kind of generic way to set these kinds
        // of flags individually, perhaps having them live in SYSTEM/...
        //
        g_gc.watch_recycle = not g_gc.watch_recycle;
        g_mem.watch_expand = not g_mem.watch_expand;
      #endif
    }

    return Init_Integer(OUT, count);
}


//
//  limit-usage: native [
//
//  "Set a usage limit only once (used for SECURE)"
//
//      return: [~]
//      field ['eval 'memory]
//      limit [any-number?]
//  ]
//
DECLARE_NATIVE(limit_usage)
{
    INCLUDE_PARAMS_OF_LIMIT_USAGE;

    Option(SymId) sym = Cell_Word_Id(ARG(field));

    // !!! comment said "Only gets set once"...why?
    //
    if (sym == SYM_EVAL) {
        if (not g_ts.eval_cycles_limit)
            g_ts.eval_cycles_limit = Int64(ARG(limit));
    }
    else if (sym == SYM_MEMORY) {
        if (not g_mem.usage_limit)
            g_mem.usage_limit = Int64(ARG(limit));
    }
    else
        fail (PARAM(field));

    return NOTHING;
}


//
//  check: native [
//
//  "Run an integrity check on a value in debug builds of the interpreter"
//
//      return: [any-value?]
//      value "System will terminate abnormally if this value is corrupt"
//          [any-value?]
//  ]
//
DECLARE_NATIVE(check)
//
// This forces an integrity check to run on a series.  In R3-Alpha there was
// no debug build, so this was a simple validity check and it returned an
// error on not passing.  But Ren-C is designed to have a debug build with
// checks that aren't designed to fail gracefully.  So this just runs that
// assert rather than replicating code here that can "tolerate" a bad series.
// Review the necessity of this native.
{
    INCLUDE_PARAMS_OF_CHECK;

#ifdef NDEBUG
    UNUSED(ARG(value));

    fail (Error_Debug_Only_Raw());
#else
    Value* value = ARG(value);

    // For starters, check the memory (if it's bad, all other bets are off)
    //
    Check_Memory_Debug();

    // !!! Should call generic Assert_Value() macro with more cases
    //
    if (Any_Series(value)) {
        Assert_Flex(Cell_Flex(value));
    }
    else if (Is_Frame(value)) {
        Assert_Flex(VAL_ACTION_KEYLIST(value));
        Assert_Array(Phase_Details(ACT_IDENTITY(VAL_ACTION(value))));
    }
    else if (Any_Context(value)) {
        Assert_Varlist(Cell_Varlist(value));
    }

    return COPY(value);
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
//  "Get the evaluator tick count (currently only available in debug builds)"
//
//      return: [~null~ integer!]
//  ]
//
DECLARE_NATIVE(c_debug_tick)
{
    INCLUDE_PARAMS_OF_C_DEBUG_TICK;

  #if !defined(NDEBUG) && DEBUG_COUNT_TICKS
    return Init_Integer(OUT, TG_tick);
  #else
    return nullptr;
  #endif
}


//
//  c-debug-break: native [
//
//  "Break at next evaluation point (only use when running under C debugger)"
//
//      return: [~[]~]
//          "Invisibly returns what the expression to the right would have"
//  ]
//
DECLARE_NATIVE(c_debug_break)
{
    INCLUDE_PARAMS_OF_C_DEBUG_BREAK;

  #if !INCLUDE_C_DEBUG_BREAK_NATIVE
    fail (Error_Debug_Only_Raw());
  #else
    #if DEBUG_COUNT_TICKS
        //
        // For instance with:
        //
        //    print c-debug-break mold value
        //
        // Queue it so the break happens right before the MOLD, not after it
        // happened and has been passed as an argument.
        //
        g_break_at_tick = level_->tick + 1;
        return Init_Nihil(OUT);
     #else
        // No tick counting or tick-break checking, but still want some
        // debug break functionality (e.g. callgrind build).  Break here--
        // you'll have to step up out into the evaluator stack.
        //
      #if DEBUG
        debug_break();
      #endif
        return Init_Nihil(OUT);
      #endif
  #endif
}
