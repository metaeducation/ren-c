//
//  File: %d-stats.c
//  Summary: "Statistics gathering for performance analysis"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
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
//      return: [~null~ time! integer!]
//      /show
//          "Print formatted results to console"
//      /profile
//          "Returns profiler object"
//      /timer
//          "High resolution time difference from start"
//      /evals
//          "Number of values evaluated by interpreter"
//      /dump-series
//          "Dump all series in pool"
//      pool-id [integer!]
//          "-1 for all pools"
//  ]
//
DECLARE_NATIVE(stats)
{
    INCLUDE_PARAMS_OF_STATS;

    if (REF(timer)) {
        RESET_CELL(OUT, REB_TIME);
        VAL_NANO(OUT) = OS_DELTA_TIME(PG_Boot_Time) * 1000;
        return OUT;
    }

    if (REF(evals)) {
        REBI64 n = Eval_Cycles + Eval_Dose - Eval_Count;
        return Init_Integer(OUT, n);
    }

#if NO_RUNTIME_CHECKS
    UNUSED(REF(show));
    UNUSED(REF(profile));
    UNUSED(REF(dump_series));
    UNUSED(ARG(pool_id));

    fail (Error_Debug_Only_Raw());
#else
    if (REF(profile)) {
        Copy_Cell(OUT, Get_System(SYS_STANDARD, STD_STATS));
        if (Is_Object(OUT)) {
            Value* stats = Cell_Varlist_VAR(OUT, 1);

            RESET_CELL(stats, REB_TIME);
            VAL_NANO(stats) = OS_DELTA_TIME(PG_Boot_Time) * 1000;
            stats++;
            Init_Integer(stats, Eval_Cycles + Eval_Dose - Eval_Count);
            stats++;
            Init_Integer(stats, 0); // no such thing as natives, only functions

            stats++;
            Init_Integer(stats, PG_Reb_Stats->Series_Made);
            stats++;
            Init_Integer(stats, PG_Reb_Stats->Series_Freed);
            stats++;
            Init_Integer(stats, PG_Reb_Stats->Series_Expanded);
            stats++;
            Init_Integer(stats, PG_Reb_Stats->Series_Memory);
            stats++;
            Init_Integer(stats, PG_Reb_Stats->Recycle_Flex_Total);

            stats++;
            Init_Integer(stats, PG_Reb_Stats->Blocks);
            stats++;
            Init_Integer(stats, PG_Reb_Stats->Objects);

            stats++;
            Init_Integer(stats, PG_Reb_Stats->Recycle_Counter);
        }

        return OUT;
    }

    if (REF(dump_series)) {
        Value* pool_id = ARG(pool_id);
        Dump_Flex_In_Pool(VAL_INT32(pool_id));
        return nullptr;
    }

    if (REF(show))
        Dump_Pools();

    return Init_Integer(OUT, Inspect_Flex(REF(show)));
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
//      return: [nothing!]
//      'instruction [word!]
//          {Currently just either ON or OFF}
//  ]
//
DECLARE_NATIVE(callgrind)
//
// Note: In order to start callgrind without collecting data by default (so
// that you can instrument just part of the code) use:
//
//     valgrind --tool=callgrind --dump-instr=yes --collect-atstart=no ./r3
//
// The tool kcachegrind is very useful for reading the results.
{
    INCLUDE_PARAMS_OF_CALLGRIND;

  #if defined(INCLUDE_CALLGRIND_NATIVE)
    switch (Cell_Word_Id(ARG(instruction))) {
    case SYM_ON:
        CALLGRIND_START_INSTRUMENTATION;
        CALLGRIND_TOGGLE_COLLECT;
        break;

    case SYM_OFF:
        CALLGRIND_TOGGLE_COLLECT;
        CALLGRIND_STOP_INSTRUMENTATION;
        break;

    default:
        fail ("Currently CALLGRIND only supports ON and OFF");
    }
    return Init_Nothing(OUT);
  #else
    UNUSED(ARG(instruction));
    fail ("This executable wasn't compiled with INCLUDE_CALLGRIND_NATIVE");
  #endif
}
