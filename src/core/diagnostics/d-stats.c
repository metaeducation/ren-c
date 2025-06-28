//
//  file: %d-stats.c
//  summary: "Statistics gathering for performance analysis"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// taking advantage of hooks provided in EVAL and APPLY.
//

#include "sys-core.h"


//
//  stats: native [
//
//  "Provides status and statistics information about the interpreter"
//
//      return: [null? time! integer! object!]
//      :show "Print formatted results to console"
//      :profile "Returns profiler object"
//      :evals "Number of values evaluated by interpreter"
//      :pool "Dump all flex in pool"
//          [integer!]
//  ]
//
DECLARE_NATIVE(STATS)
{
    INCLUDE_PARAMS_OF_STATS;

    REBI64 num_evals = g_ts.total_eval_cycles + g_ts.eval_dose - g_ts.eval_countdown;

    if (Bool_ARG(EVALS))
        return Init_Integer(OUT, num_evals);

    if (Bool_ARG(PROFILE)) {
      #if DEBUG_COLLECT_STATS
        return rebValue("make object! [",
            "evals:", rebI(num_evals),
            "num-flex-made:", rebI(g_mem.num_flex_made),
            "num-flex-freed:", rebI(g_mem.num_flex_freed),
            "num-flex-expanded:", rebI(g_mem.num_flex_expanded),
            "flex-memory:", rebI(g_mem.flex_memory),
            "recycled-stubs_total:", rebI(g_gc.recycled_stubs_total),
            "blocks-made:", rebI(g_mem.blocks_made),
            "objects-made:", rebI(g_mem.objects_made),
            "recycles:", rebI(g_gc.recycle_counter),
        "]");
      #else
        return PANIC(Error_Checked_Build_Only_Raw());
      #endif
    }

  #if RUNTIME_CHECKS
    if (Bool_ARG(POOL)) {
        Value* pool_id = ARG(POOL);
        Dump_All_Flex_In_Pool(VAL_INT32(pool_id));
        return nullptr;
    }

    if (Bool_ARG(SHOW))
        Dump_Pools();

    return Init_Integer(OUT, Inspect_Flex(Bool_ARG(SHOW)));
  #else
    UNUSED(Bool_ARG(SHOW));
    UNUSED(ARG(POOL));

    return PANIC(Error_Checked_Build_Only_Raw());
  #endif
}


#if defined(INCLUDE_CALLGRIND_NATIVE)
    #include <valgrind/callgrind.h>
#endif

//
//  callgrind: native [
//
//  "Provide access to services in <valgrind/callgrind.h>"
//
//      return: []
//      'instruction [~(on off)~]
//  ]
//
DECLARE_NATIVE(CALLGRIND)
//
// Note: In order to start callgrind without collecting data by default (so
// that you can instrument just part of the code) use:
//
//     valgrind --tool=callgrind --instr-atstart=no --collect-atstart=no ./r3
//
// For easy copying and pasting into the shell, here's a useful command line:
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
    switch (Word_Id(ARG(INSTRUCTION))) {
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
        return PANIC("Currently CALLGRIND only supports ON and OFF");
    }
    return TRIPWIRE;
  #else
    UNUSED(ARG(INSTRUCTION));
    return PANIC("Executable wasn't compiled with INCLUDE_CALLGRIND_NATIVE");
  #endif
}
