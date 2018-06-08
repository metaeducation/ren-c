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
REBNATIVE(stats)
{
    INCLUDE_PARAMS_OF_STATS;

    if (REF(timer)) {
        RESET_VAL_HEADER(D_OUT, REB_TIME);
        VAL_NANO(D_OUT) = OS_DELTA_TIME(PG_Boot_Time) * 1000;
        return R_OUT;
    }

    if (REF(evals)) {
        REBI64 n = Eval_Cycles + Eval_Dose - Eval_Count;
        Init_Integer(D_OUT, n);
        return R_OUT;
    }

#ifdef NDEBUG
    UNUSED(REF(show));
    UNUSED(REF(profile));
    UNUSED(REF(dump_series));
    UNUSED(ARG(pool_id));

    fail (Error_Debug_Only_Raw());
#else
    if (REF(profile)) {
        Move_Value(D_OUT, Get_System(SYS_STANDARD, STD_STATS));
        if (IS_OBJECT(D_OUT)) {
            REBVAL *stats = VAL_CONTEXT_VAR(D_OUT, 1);

            RESET_VAL_HEADER(stats, REB_TIME);
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
            Init_Integer(stats, PG_Reb_Stats->Recycle_Series_Total);

            stats++;
            Init_Integer(stats, PG_Reb_Stats->Blocks);
            stats++;
            Init_Integer(stats, PG_Reb_Stats->Objects);

            stats++;
            Init_Integer(stats, PG_Reb_Stats->Recycle_Counter);
        }

        return R_OUT;
    }

    if (REF(dump_series)) {
        REBVAL *pool_id = ARG(pool_id);
        Dump_Series_In_Pool(VAL_INT32(pool_id));
        return R_BLANK;
    }

    Init_Integer(D_OUT, Inspect_Series(REF(show)));

    if (REF(show))
        Dump_Pools();

    return R_OUT;
#endif
}


//
//  Do_Core_Measured: C
//
// Putting in measurement for Do_Core would interfere with measurements for
// Apply_Core, as it would slow down the very functions that are being timed.
//
void Do_Core_Measured(REBFRM * const f)
{
    // There are a lot of invariants checked on entry to Do_Core(), but this
    // is a simple one that is important enough to mirror here.
    //
    assert(FRM_HAS_MORE(f) || f->flags.bits & DO_FLAG_APPLYING);

    // In order to measure single steps, we convert a DO_FLAG_TO_END request
    // into a sequence of DO/NEXT operations, and loop them.
    //
    REBOOL was_do_to_end = did (f->flags.bits & DO_FLAG_TO_END);
    f->flags.bits &= ~DO_FLAG_TO_END;

    while (TRUE) {
        Do_Core(f);

        if (not was_do_to_end or THROWN(f->out) or FRM_AT_END(f))
            break;
    }

    if (was_do_to_end)
        f->flags.bits |= DO_FLAG_TO_END;
}


enum {
    // A WORD! name for the first non-anonymous symbol with which a function
    // has been invoked.  This may turn into a BLOCK! of all the names a
    // function has been invoked with.
    //
    IDX_STATS_SYMBOL = 0,

    // Number of times the function has been called.
    //
    IDX_STATS_NUMCALLS = 1,

    // !!! More will be added here when timing data is included, but timing
    // is tricky to do meaningfully while subtracting the instrumentation
    // itself out.

    IDX_STATS_MAX
};


//
//  Apply_Core_Measured: C
//
// This is the function which is swapped in for Apply_Core when stats are
// enabled.
//
// In order to actually be accurate, it would need some way to subtract out
// its own effect on the timing of functions above on the stack.
//
REB_R Apply_Core_Measured(REBFRM * const f)
{
    REBMAP *m = VAL_MAP(Root_Stats_Map);

    REBOOL is_first_phase = (f->phase == f->original);

    // We can only tell if it's the last phase *before* the apply; because if
    // we check *after* it may change to become the last and need R_REDO_XXX.
    //
    REBOOL is_last_phase = (ACT_UNDERLYING(f->phase) == f->phase);

    if (is_first_phase) {
        //
        // Currently we get a call for each "phase" of a composite function.
        // Whether this is good or bad remains to be seen, but doing otherwise
        // would require restructuring the evaluator in a way that would
        // compromise its efficiency.  But as a result, if we want to store
        // the accumulated time for this function run we need to have a map
        // from frame to start time.
        //
        // This is where we would be starting a timer.  A simpler case is
        // being studied for starters...of just counting.
    }

    REB_R r = Apply_Core(f);

    if (is_last_phase) {
        //
        // Finalize the inclusive time if it's the last phase.  Timing info
        // is being skipped for starters, just to increment a count of how
        // many times the function gets called.

        const REBOOL cased = FALSE;
        REBINT n = Find_Map_Entry(
            m,
            ACT_ARCHETYPE(f->original),
            SPECIFIED,
            NULL, // searching now, not inserting, so pass NULL
            SPECIFIED,
            cased // shouldn't matter
        );

        if (n == 0) {
            //
            // There's no entry yet for this ACTION!, initialize one.

            REBARR *a = Make_Array(IDX_STATS_MAX);
            if (f->opt_label != NULL)
                Init_Word(ARR_AT(a, IDX_STATS_SYMBOL), f->opt_label);
            else
                Init_Blank(ARR_AT(a, IDX_STATS_SYMBOL));
            Init_Integer(ARR_AT(a, IDX_STATS_NUMCALLS), 1);
            TERM_ARRAY_LEN(a, IDX_STATS_MAX);

            DECLARE_LOCAL (stats);
            Init_Block(stats, a);

            n = Find_Map_Entry(
                m,
                ACT_ARCHETYPE(f->original),
                SPECIFIED,
                stats, // inserting now, so don't pass NULL
                SPECIFIED,
                cased // shouldn't matter
            );
            assert(n != 0); // should have inserted
        }
        else {
            REBVAL *stats = KNOWN(ARR_AT(MAP_PAIRLIST(m), ((n - 1) * 2) + 1));

            REBARR *a = IS_BLOCK(stats) ? VAL_ARRAY(stats) : NULL;

            if (
                a != NULL
                && ARR_LEN(a) == IDX_STATS_MAX
                && (
                    IS_WORD(ARR_AT(a, IDX_STATS_SYMBOL))
                    || IS_BLANK(ARR_AT(a, IDX_STATS_SYMBOL))
                )
                && IS_INTEGER(ARR_AT(a, IDX_STATS_NUMCALLS))
            ){
                if (
                    IS_BLANK(ARR_AT(a, IDX_STATS_SYMBOL))
                    && f->opt_label != NULL
                ){
                    Init_Word(ARR_AT(a, IDX_STATS_SYMBOL), f->opt_label);
                }
                Init_Integer(
                    ARR_AT(a, IDX_STATS_NUMCALLS),
                    VAL_INT64(ARR_AT(a, IDX_STATS_NUMCALLS)) + 1
                );
            }
            else if (not IS_ERROR(stats)) {
                //
                // The user might muck with the MAP! so we put an ERROR! in
                // to signal something went wrong, parameterized with the
                // invalid value...as long as it isn't already an error.
                //
                Init_Error(stats, Error_Invalid_Arg_Raw(stats));
            }
        }

        // Not clear if there's any statistical reason to process the r result
        // here, but leave the scaffold in case there is.
        //
        switch (r) {
        case R_FALSE:
            break;

        case R_TRUE:
            break;

        case R_VOID:
            break;

        case R_BLANK:
            break;

        case R_BAR:
            break;

        case R_OUT:
            break;

        case R_OUT_IS_THROWN: {
            break; }

        case R_REDO_CHECKED:
            assert(FALSE); // shouldn't be possible for final phase
            break;

        case R_REDO_UNCHECKED:
            assert(FALSE); // shouldn't be possible for final phase
            break;

        case R_REEVALUATE_CELL:
            break;

        case R_REEVALUATE_CELL_ONLY:
            break;

        case R_INVISIBLE:
            break;

        case R_UNHANDLED: // internal use only, shouldn't be returned
            assert(FALSE);
            break;

        default:
            assert(FALSE);
        }
    }

    return r;
}


//
//  metrics: native [
//
//  {Track function calls and inclusive timings for those calls.}
//
//      return: [map!]
//      mode [logic!]
//          {Whether metrics should be on or off.}
//  ]
//
REBNATIVE(metrics)
{
    INCLUDE_PARAMS_OF_METRICS;

    REBVAL *mode = ARG(mode);

    Check_Security(Canon(SYM_DEBUG), POL_READ, 0);

    if (VAL_LOGIC(mode)) {
        //PG_Do = &Do_Core_Measured;
        PG_Apply = &Apply_Core_Measured;
    }
    else {
        //PG_Do = &Do_Core;
        PG_Apply = &Apply_Core;
    }

    Move_Value(D_OUT, Root_Stats_Map);
    return R_OUT;
}


#ifdef INCLUDE_CALLGRIND_NATIVE
    #include <valgrind/callgrind.h>
#endif

//
//  callgrind: native [
//
//  {Provide access to services in <valgrind/callgrind.h>}
//
//      'instruction [word!]
//          {Currently just either ON or OFF}
//  ]
//
REBNATIVE(callgrind)
//
// Note: In order to start callgrind without collecting data by default (so
// that you can instrument just part of the code) use:
//
//     valgrind --tool=callgrind --dump-instr=yes --collect-atstart=no ./r3
//
// The tool kcachegrind is very useful for reading the results.
{
    INCLUDE_PARAMS_OF_CALLGRIND;

#ifdef INCLUDE_CALLGRIND_NATIVE
    switch (VAL_WORD_SYM(ARG(instruction))) {
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
    return R_VOID;

#else
    UNUSED(ARG(instruction));
    fail ("This exeuctable wasn't compiled with INCLUDE_CALLGRIND_NATIVE");
#endif
}
