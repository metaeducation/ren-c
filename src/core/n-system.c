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
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
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
    UNUSED(frame_);
    fail (VAL_CONTEXT(TASK_HALT_ERROR));
}


//
//  quit: native [
//
//  {Stop evaluating and return control to command shell or calling script.}
//
//      /with
//          {Yield a result (mapped to an integer if given to shell)}
//      value [any-value!]
//          "See: http://en.wikipedia.org/wiki/Exit_status"
//  ]
//
REBNATIVE(quit)
//
// QUIT is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name value :quit`.
{
    INCLUDE_PARAMS_OF_QUIT;

    Move_Value(D_OUT, NAT_VALUE(quit));

    if (REF(with))
        CONVERT_NAME_TO_THROWN(D_OUT, ARG(value));
    else {
        // Chosen to do it this way because returning to a calling script it
        // will be no value by default, for parity with BREAK and EXIT without
        // a /WITH.  Long view would have RETURN work this way too: CC#2241

        // void translated to 0 if it gets caught for the shell, see #2241

        CONVERT_NAME_TO_THROWN(D_OUT, VOID_CELL);
    }

    return R_OUT_IS_THROWN;
}


//
//  exit-rebol: native [
//
//  {Stop the current Rebol interpreter, cannot be caught by CATCH/QUIT.}
//
//      /with
//          {Yield a result (mapped to an integer if given to shell)}
//      value [any-value!]
//          "See: http://en.wikipedia.org/wiki/Exit_status"
//  ]
//
REBNATIVE(exit_rebol)
{
    INCLUDE_PARAMS_OF_EXIT_REBOL;

    int code;
    if (REF(with))
        code = VAL_INT32(ARG(value));
    else
        code = EXIT_SUCCESS;

    exit(code);
}


//
//  recycle: native [
//
//  "Recycles unused memory."
//
//      return: [<opt> integer!]
//          {Number of series nodes recycled (if applicable)}
//      /off
//          "Disable auto-recycling"
//      /on
//          "Enable auto-recycling"
//      /ballast
//          "Trigger for auto-recycle (memory used)"
//      size [integer!]
//      /torture
//          "Constant recycle (for internal debugging)"
//      /verbose
//          "Dump out information about series being recycled"
//  ]
//
REBNATIVE(recycle)
{
    INCLUDE_PARAMS_OF_RECYCLE;

    if (REF(off)) {
        GC_Disabled = TRUE;
        return R_VOID;
    }

    if (REF(on)) {
        GC_Disabled = FALSE;
        VAL_INT64(TASK_BALLAST) = VAL_INT32(TASK_MAX_BALLAST);
    }

    if (REF(ballast)) {
        Move_Value(TASK_MAX_BALLAST, ARG(size));
        VAL_INT64(TASK_BALLAST) = VAL_INT32(TASK_MAX_BALLAST);
    }

    if (REF(torture)) {
        GC_Disabled = TRUE;
        VAL_INT64(TASK_BALLAST) = 0;
    }

    if (GC_Disabled)
        return R_VOID; // don't give back misleading "0", since no recycle ran

    REBCNT count;

    if (REF(verbose)) {
    #if defined(NDEBUG)
        fail (Error_Debug_Only_Raw());
    #else
        REBSER *sweeplist = Make_Series(100, sizeof(REBNOD*));
        count = Recycle_Core(FALSE, sweeplist);
        assert(count == SER_LEN(sweeplist));

        REBCNT index = 0;
        for (index = 0; index < count; ++index) {
            REBNOD *node = *SER_AT(REBNOD*, sweeplist, index);
            PROBE(node);
        }

        Free_Series(sweeplist);

        REBCNT recount = Recycle_Core(FALSE, NULL);
        assert(recount == count);
    #endif
    }
    else {
        count = Recycle();
    }

    Init_Integer(D_OUT, count);
    return R_OUT;
}


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
        VAL_TIME(D_OUT) = OS_DELTA_TIME(PG_Boot_Time, 0) * 1000;
        Reset_Val_Header(D_OUT, REB_TIME);
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

            VAL_TIME(stats) = OS_DELTA_TIME(PG_Boot_Time, 0) * 1000;
            Reset_Val_Header(stats, REB_TIME);
            stats++;
            Init_Integer(stats, Eval_Cycles + Eval_Dose - Eval_Count);
            stats++;
            Init_Integer(stats, 0); // no such thing as natives, only functions
            stats++;
            Init_Integer(stats, Eval_Functions);

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
//  evoke: native [
//
//  "Special guru meditations. (Not for beginners.)"
//
//      chant [word! block! integer!]
//          "Single or block of words ('? to list)"
//  ]
//
REBNATIVE(evoke)
{
    INCLUDE_PARAMS_OF_EVOKE;

#ifdef NDEBUG
    UNUSED(ARG(chant));

    fail (Error_Debug_Only_Raw());
#else
    RELVAL *arg = ARG(chant);
    REBCNT len;

    Check_Security(Canon(SYM_DEBUG), POL_READ, 0);

    if (IS_BLOCK(arg)) {
        len = VAL_LEN_AT(arg);
        arg = VAL_ARRAY_AT(arg);
    }
    else len = 1;

    for (; len > 0; len--, arg++) {
        if (IS_WORD(arg)) {
            switch (VAL_WORD_SYM(arg)) {
            case SYM_CRASH_DUMP:
                Reb_Opts->crash_dump = TRUE;
                break;

            case SYM_WATCH_RECYCLE:
                Reb_Opts->watch_recycle = NOT(Reb_Opts->watch_recycle);
                break;

            case SYM_CRASH:
                panic ("evoke 'crash was executed");

            default:
                Debug_Fmt(RM_EVOKE_HELP);
            }
        }
        if (IS_INTEGER(arg)) {
            switch (Int32(arg)) {
            case 0:
                Check_Memory_Debug();
                break;

            case 1:
                Reb_Opts->watch_expand = TRUE;
                break;

            default:
                Debug_Fmt(RM_EVOKE_HELP);
            }
        }
    }

    return R_VOID;
#endif
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

    REBSYM sym = VAL_WORD_SYM(ARG(field));

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
        fail (ARG(field));

    return R_VOID;
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

    // !!! Should call generic ASSERT_VALUE macro with more cases
    //
    if (ANY_SERIES(value)) {
        ASSERT_SERIES(VAL_SERIES(value));
    }
    else if (ANY_CONTEXT(value)) {
        ASSERT_CONTEXT(VAL_CONTEXT(value));
    }
    else if (IS_FUNCTION(value)) {
        ASSERT_ARRAY(VAL_FUNC_PARAMLIST(value));
        ASSERT_ARRAY(VAL_ARRAY(VAL_FUNC_BODY(value)));
    }

    return R_TRUE;
#endif
}
