//
//  File: %d-stats.c
//  Summary: "Statistics gathering for performance analysis"
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
// These routines are for gathering statistics and metrics.  While some of
// the metrics-gathering may require custom code in the memory allocator,
// it is hoped that many services can be built as an optional extension by
// taking advantage of hooks provided in DO and APPLY.
//

#include "sys-core.h"


//
//  stats: native [
//
//  {Provides status and statistics information about the interpreter.}
//
//      return: [<opt> time! integer! object!]
//      /show "Print formatted results to console"
//      /profile "Returns profiler object"
//      /evals "Number of values evaluated by interpreter"
//      /pool "Dump all series in pool"
//          [integer!]
//  ]
//
REBNATIVE(stats)
{
    INCLUDE_PARAMS_OF_STATS;

    REBI64 num_evals = Total_Eval_Cycles + Eval_Dose - Eval_Countdown;

    if (REF(evals))
        return Init_Integer(D_OUT, num_evals);

    if (REF(profile)) {
      #if DEBUG_COLLECT_STATS
        return rebValue("make object! [",
            "evals:", rebI(num_evals),
            "series-made:", rebI(PG_Reb_Stats->Series_Made),
            "series-freed:", rebI(PG_Reb_Stats->Series_Freed),
            "series-expanded:", rebI(PG_Reb_Stats->Series_Expanded),
            "series-bytes:", rebI(PG_Reb_Stats->Series_Memory),
            "series-recycled:", rebI(PG_Reb_Stats->Recycle_Series_Total),
            "made-blocks:", rebI(PG_Reb_Stats->Blocks),
            "made-objects:", rebI(PG_Reb_Stats->Objects),
            "recycles:", rebI(PG_Reb_Stats->Recycle_Counter),
        "]");
      #else
        fail (Error_Debug_Only_Raw());
      #endif
    }

  #if !defined(NDEBUG)
    if (REF(pool)) {
        REBVAL *pool_id = ARG(pool);
        Dump_Series_In_Pool(VAL_INT32(pool_id));
        return nullptr;
    }

    if (REF(show))
        Dump_Pools();

    return Init_Integer(D_OUT, Inspect_Series(did REF(show)));
  #else
    UNUSED(REF(show));
    UNUSED(ARG(pool));

    fail (Error_Debug_Only_Raw());
  #endif
}


#if defined(INCLUDE_CALLGRIND_NATIVE)
    #include <valgrind/callgrind.h>
#endif

//
//  callgrind: native [
//
//  {Provide access to services in <valgrind/callgrind.h>}
//
//      return: <none>
//      'instruction "Currently just either ON or OFF"
//          [word!]
//  ]
//
REBNATIVE(callgrind)
//
// Note: In order to start callgrind without collecting data by default (so
// that you can instrument just part of the code) use:
//
//     valgrind --tool=callgrind --instr-atstart=no --collect-atstart=no ./r3
//
// For easy copy/paste into the shell, here's a useful command line:
/*
     valgrind --tool=callgrind \
          --collect-jumps=yes \
          --dump-instr=yes \
          --instr-atstart=no \
          --collect-atstart=no \
          ./r3
*/
// The tool kcachegrind is very useful for reading the results.
{
    INCLUDE_PARAMS_OF_CALLGRIND;

  #if defined(INCLUDE_CALLGRIND_NATIVE)
    switch (VAL_WORD_ID(ARG(instruction))) {
      case SYM_ON:
        PG_Callgrind_On = true;
        CALLGRIND_START_INSTRUMENTATION;
        CALLGRIND_TOGGLE_COLLECT;
        break;

      case SYM_OFF:
        PG_Callgrind_On = false;
        CALLGRIND_TOGGLE_COLLECT;
        CALLGRIND_STOP_INSTRUMENTATION;
        break;

      default:
        fail ("Currently CALLGRIND only supports ON and OFF");
    }
    return Init_None(D_OUT);
  #else
    UNUSED(ARG(instruction));
    fail ("This executable wasn't compiled with INCLUDE_CALLGRIND_NATIVE");
  #endif
}
