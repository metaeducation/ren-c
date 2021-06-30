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
//  "Stops evaluation and returns to the input prompt."
//
//      ; No arguments
//  ]
//
REBNATIVE(halt)
{
    INCLUDE_PARAMS_OF_HALT;

    return Init_Thrown_With_Label(D_OUT, NULLED_CELL, NATIVE_VAL(halt));
}


//
//  quit: native [
//
//  {Stop evaluating and return control to command shell or calling script}
//
//      value "See: http://en.wikipedia.org/wiki/Exit_status"
//          [<end> <opt> any-value!]
//  ]
//
REBNATIVE(quit)
//
// QUIT is implemented via a thrown signal that bubbles up through the stack.
// It uses the value of its own native function as the name of the throw, like
// `throw/name value :quit`.
{
    INCLUDE_PARAMS_OF_QUIT;

    if (IS_ENDISH_NULLED(ARG(value))) {
        //
        // This returns a BAD-WORD! if there is no arg, in sync with RETURN that
        // has no arg.  But labels it ~quit~.
        //
        Init_Bad_Word(ARG(value), SYM_QUIT);
    }

    return Init_Thrown_With_Label(D_OUT, ARG(value), NATIVE_VAL(quit));
}


//
//  exit-rebol: native [
//
//  {Stop the current Rebol interpreter (cannot be caught by CATCH/QUIT)}
//
//      status "See: http://en.wikipedia.org/wiki/Exit_status"
//          [<opt> <end> integer!]
//  ]
//
REBNATIVE(exit_rebol)
{
    INCLUDE_PARAMS_OF_EXIT_REBOL;

    int status;
    if (IS_NULLED(ARG(status)))
        status = EXIT_SUCCESS;
    else
        status = VAL_INT32(ARG(status));

    exit(status);
}


//
//  recycle: native [
//
//  "Recycles unused memory."
//
//      return: "Number of series nodes recycled (if applicable)"
//          [<opt> integer!]
//      /off "Disable auto-recycling"
//      /on "Enable auto-recycling"
//      /ballast "Trigger for auto-recycle (memory used)"
//          [integer!]
//      /torture "Constant recycle (for internal debugging)"
//      /watch "Monitor recycling (debug only)"
//      /verbose "Dump information about series being recycled (debug only)"
//  ]
//
REBNATIVE(recycle)
{
    INCLUDE_PARAMS_OF_RECYCLE;

    if (REF(off)) {
        GC_Disabled = true;
        return nullptr;
    }

    if (REF(on)) {
        GC_Disabled = false;
        TG_Ballast = TG_Max_Ballast;
    }

    if (REF(ballast)) {
        TG_Max_Ballast = VAL_INT32(ARG(ballast));
        TG_Ballast = TG_Max_Ballast;
    }

    if (REF(torture)) {
        GC_Disabled = false;
        TG_Ballast = 0;
    }

    if (GC_Disabled)
        return nullptr; // don't give misleading "0", since no recycle ran

    REBLEN count;

    if (REF(verbose)) {
      #if defined(NDEBUG)
        fail (Error_Debug_Only_Raw());
      #else
        REBSER *sweeplist = Make_Series(100, FLAG_FLAVOR(NODELIST));
        count = Recycle_Core(false, sweeplist);
        assert(count == SER_USED(sweeplist));

        REBLEN index = 0;
        for (index = 0; index < count; ++index) {
            REBNOD *node = *SER_AT(REBNOD*, sweeplist, index);
            PROBE(node);
            UNUSED(node);
        }

        Free_Unmanaged_Series(sweeplist);

        REBLEN recount = Recycle_Core(false, NULL);
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
        Reb_Opts->watch_recycle = not Reb_Opts->watch_recycle;
        Reb_Opts->watch_expand = not Reb_Opts->watch_expand;
      #endif
    }

    return Init_Integer(D_OUT, count);
}


//
//  limit-usage: native [
//
//  "Set a usage limit only once (used for SECURE)."
//
//      field [word!]
//          "eval (count) or memory (bytes)"
//      limit [any-number!]
//  ]
//
REBNATIVE(limit_usage)
{
    INCLUDE_PARAMS_OF_LIMIT_USAGE;

    SYMID sym = VAL_WORD_ID(ARG(field));

    // !!! comment said "Only gets set once"...why?
    //
    if (sym == SYM_EVAL) {
        if (Eval_Limit == 0)
            Eval_Limit = Int64(ARG(limit));
    }
    else if (sym == SYM_MEMORY) {
        if (PG_Mem_Limit == 0)
            PG_Mem_Limit = Int64(ARG(limit));
    }
    else
        fail (PAR(field));

    return nullptr;
}


//
//  check: native [
//
//  "Run an integrity check on a value in debug builds of the interpreter"
//
//      value [<opt> any-value!]
//          {System will terminate abnormally if this value is corrupt.}
//  ]
//
REBNATIVE(check)
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
    REBVAL *value = ARG(value);

    // For starters, check the memory (if it's bad, all other bets are off)
    //
    Check_Memory_Debug();

    // !!! Should call generic ASSERT_VALUE macro with more cases
    //
    if (ANY_SERIES(value)) {
        ASSERT_SERIES(VAL_SERIES(value));
    }
    else if (ANY_CONTEXT(value)) {
        ASSERT_CONTEXT(VAL_CONTEXT(value));
    }
    else if (IS_ACTION(value)) {
        ASSERT_SERIES(VAL_ACTION_KEYLIST(value));
        ASSERT_ARRAY(ACT_DETAILS(VAL_ACTION(value)));
    }

    return Init_True(D_OUT);
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
//  {Get the evaluator tick count (currently only available in debug builds)}
//
//      return: [<opt> integer!]
// ]
//
REBNATIVE(c_debug_tick)
{
    INCLUDE_PARAMS_OF_C_DEBUG_TICK;

  #if !defined(NDEBUG) && defined(DEBUG_COUNT_TICKS)
    return Init_Integer(D_OUT, TG_Tick);
  #else
    return nullptr;
  #endif
}


//
//  c-debug-break-at: native [
//
//  {Break at known evaluation point (only use when running under C debugger}
//
//      return: [<opt>]
//      tick [<blank> integer!]
//          {Get from PANIC, REBFRM.tick, REBSER.tick, REBVAL.extra.tick}
//      /relative
//          {TICK parameter represents a count relative to the current tick}
//      /compensate
//          {Round tick up, as in https://math.stackexchange.com/q/2521219/}
// ]
//
REBNATIVE(c_debug_break_at)
{
    INCLUDE_PARAMS_OF_C_DEBUG_BREAK_AT;

  #if !defined(NDEBUG) && defined(DEBUG_COUNT_TICKS)
    if (REF(compensate)) {
        //
        // Imagine two runs of Rebol console initialization.  In the first,
        // the tick count is 304 when C-DEBUG-BREAK/COMPENSATE is called,
        // right after command line parsing.  Later on a panic() is hit and
        // reports tick count 1020 in the crash log.
        //
        // Wishing to pick apart the bug before it happens, the Rebol Core
        // Developer then re-runs the program with `--breakpoint=1020`, hoping
        // to break at that tick, to catch the downstream appearance of the
        // tick in the panic().  But since command-line processing is in
        // usermode, the addition of the parameter throws off the ticks!
        //
        // https://en.wikipedia.org/wiki/Observer_effect_(physics)
        //
        // Let's say that after the command line processing, it still runs
        // C-DEBUG-BREAK/COMPENSATE, this time at tick 403.  Imagine our goal
        // is to make the parameter to /COMPENSATE something that can be used
        // to conservatively guess the same value to set the tick to, and
        // that /COMPENSATE ARG(bound) that gives a maximum of how far off we
        // could possibly be from the "real" tick. (e.g. "argument processing
        // took no more than 200 additional ticks", which this is consistent
        // with...since 403-304 = 99).
        //
        // The reasoning for why the formula below works for this rounding is
        // given in this StackExchange question and answer:
        //
        // https://math.stackexchange.com/q/2521219/
        //
        REBTCK one = 1; // MSVC gives misguided warning for cast(REBTCK, 1)
        TG_Tick =
            (one << (ceil_log2(TG_Tick) + 1))
            + VAL_INT64(ARG(tick))
            - 1;
        return nullptr;
    }

    if (REF(relative))
        TG_Break_At_Tick = frame_->tick + 1 + VAL_INT64(ARG(tick));
    else
        TG_Break_At_Tick = VAL_INT64(ARG(tick));
    return nullptr;
  #else
    UNUSED(ARG(tick));
    UNUSED(ARG(relative));
    UNUSED(REF(compensate));

    fail (Error_Debug_Only_Raw());
  #endif
}


//
//  c-debug-break: native [
//
//  "Break at next evaluation point (only use when running under C debugger)"
//
//      return: [<invisible>]
//          {Invisibly returns what the expression to the right would have}
//  ]
//
REBNATIVE(c_debug_break)
{
    INCLUDE_PARAMS_OF_C_DEBUG_BREAK;

  #if defined(INCLUDE_C_DEBUG_BREAK_NATIVE)
    #if defined(DEBUG_COUNT_TICKS)
        //
        // For instance with:
        //
        //    print c-debug-break mold value
        //
        // Queue it so the break happens right before the MOLD, not after it
        // happened and has been passed as an argument.
        //
        TG_Break_At_Tick = frame_->tick + 1;
        RETURN_INVISIBLE;
     #else
        // No tick counting or tick-break checking, but still want some
        // debug break functionality (e.g. callgrind build).  Break here--
        // you'll have to step up out into the evaluator stack.
        //
        debug_break();
        RETURN_INVISIBLE;
      #endif
  #else
    fail (Error_Debug_Only_Raw());
  #endif
}
