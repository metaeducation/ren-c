/***********************************************************************
**
**  REBOL [R3] Language Interpreter and Run-time Environment
**
**  Copyright 2012 REBOL Technologies
**  REBOL is a trademark of REBOL Technologies
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**  http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
**
************************************************************************
**
**  Summary: Program and Thread Globals
**  Module:  sys-globals.h
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

//-- Bootstrap variables:
PVAR REBINT PG_Boot_Phase;  // To know how far in the boot we are.
PVAR REBINT PG_Boot_Level;  // User specified startup level
PVAR REBYTE **PG_Boot_Strs; // Special strings in boot.r (RS_ constants)

// PG_Reb_Stats - Various statistics about memory, etc.  This is only tracked
// in the debug build, as this data gathering is a sort of constant "tax" on
// the system.  While it might arguably be interesting to non-debug build
// users who are trying to optimize their code, the compromise of having to
// maintain the numbers suggests those users should be empowered with a debug
// build if they are doing such work (they should probably have one for other
// reasons; note this has been true of things like Windows NT where there were
// indeed "checked" builds given to those who had such interest.)
//
#if !defined(NDEBUG)
    PVAR REB_STATS *PG_Reb_Stats;
#endif

PVAR REBU64 PG_Mem_Usage;   // Overall memory used
PVAR REBU64 PG_Mem_Limit;   // Memory limit set by SECURE

//-- Symbol Table:
PVAR REBSER *PG_Word_Names; // Holds all word strings. Never removed.
PVAR WORD_TABLE PG_Word_Table; // Symbol values accessed by hash

//-- Main contexts:
PVAR REBCTX *PG_Root_Context; // Frame that holds Root_Vars
PVAR ROOT_VARS *Root_Vars; // VARLIST of PG_Root_Context as a C structure

PVAR REBCTX *Lib_Context;
PVAR REBCTX *Sys_Context;

//-- Various char tables:
PVAR REBYTE *White_Chars;
PVAR REBUNI *Upper_Cases;
PVAR REBUNI *Lower_Cases;

// Other:
PVAR REBYTE *PG_Pool_Map;   // Memory pool size map (created on boot)

PVAR REBI64 PG_Boot_Time;   // Counter when boot started
PVAR REBINT Current_Year;
PVAR REB_OPTS *Reb_Opts;

#ifndef NDEBUG
    PVAR REBOOL PG_Always_Malloc;   // For memory-related troubleshooting
#endif

// These are some canon UNSET, NONE, TRUE, and FALSE values.  They are here
// in two-element arrays in order that those using them don't accidentally
// pass them to routines that will increment the pointer as if they are
// arrays--they are singular values, and the second element is set to
// be trash to trap any unwanted access.
//
PVAR REBVAL PG_Unset_Value[2];
PVAR REBVAL PG_None_Value[2];
PVAR REBVAL PG_False_Value[2];
PVAR REBVAL PG_True_Value[2];

// A value with END set, which comes in handy if you ever need the address of
// an end for a noop to pass to a routine expecting an end-terminated series
//
// It is dynamically allocated via malloc in order to ensure that all parts
// besides the header are uninitialized memory, to prevent reading of the
// other three platform words inside of it.
//
PVAR REBVAL *PG_End_Val;

// This signal word should be thread-local, but it will not work
// when implemented that way. Needs research!!!!
PVAR REBCNT Eval_Signals;   // Signal flags

PVAR REBFUN *PG_Eval_Func; // EVAL native func (never GC'd)
PVAR REBFUN *PG_Return_Func; // RETURN native func (never GC'd)
PVAR REBFUN *PG_Leave_Func; // LEAVE native func (never GC'd)

// Hook called when BREAKPOINT is hit.  It will return TRUE if the breakpoint
// is quitting, or FALSE if it is continuing.  (Note that if one is HALTing,
// then it won't return at all...because that is done via longjmp.)
//
PVAR REBBRK PG_Breakpoint_Quitting_Hook;


/***********************************************************************
**
**  Thread Globals - Local to each thread
**
***********************************************************************/

TVAR REBCTX *TG_Task_Context; // Frame that holds Task_Vars
TVAR TASK_VARS *Task_Vars; // VARLIST of Task_Vars as a C structure

TVAR REBVAL TG_Thrown_Arg;  // Non-GC protected argument to THROW

//-- Memory and GC:
TVAR REBPOL *Mem_Pools;     // Memory pool array
TVAR REBINT GC_Disabled;    // GC disabled counter for critical sections.
TVAR REBINT GC_Ballast;     // Bytes allocated to force automatic GC
TVAR REBOOL GC_Active;      // TRUE when recycle is enabled (set by RECYCLE func)
TVAR REBSER *GC_Series_Guard; // A stack of protected series (removed by pop)
TVAR REBSER *GC_Value_Guard; // A stack of protected series (removed by pop)
PVAR REBSER *GC_Mark_Stack; // Series pending to mark their reachables as live
TVAR REBSER **Prior_Expand; // Track prior series expansions (acceleration)

TVAR REBMRK GC_Mark_Hook;   // Mark hook (set by Ren/C host to mark values)

// These manually-managed series must either be freed with Free_Series()
// or handed over to the GC at certain synchronized points, else they
// would represent a memory leak in the release build.
TVAR REBSER *GC_Manuals;    // Manually memory managed (not by GC)

TVAR REBUPT Stack_Limit;    // Limit address for CPU stack.

#if !defined(NDEBUG)
    // This counter is incremented each time through the DO loop, and can be
    // used for many purposes...including setting breakpoints in routines
    // other than Do_Next that are contingent on a certain "tick" elapsing.
    //
    TVAR REBCNT TG_Do_Count;
#endif

// Each time Do_Core is called a Reb_Call* is pushed to the Do_Stack.  Some
// of the pushed entries will represent parens or paths being executed, and
// some will represent functions that are gathering arguments...hence they
// have been "pushed" but are not yet actually running.  This stack must
// be filtered to get an understanding of something like a "backtrace of
// currently running functions".
//
TVAR struct Reb_Call *TG_Do_Stack;

//-- Evaluation stack:
TVAR REBARR *DS_Array;
TVAR REBDSP DS_Index;
TVAR REBVAL *DS_Movable_Base;

TVAR struct Reb_Call *CS_Running;   // Call frame if *running* function

// We store the head chunk of the current chunker even though it could be
// computed, because it's quicker to compare to a pointer than to do the
// math to calculate it on each Drop_Chunk...and it only needs to be updated
// when a chunk boundary gets crossed (pushing or dropping)
//
TVAR struct Reb_Chunk *TG_Top_Chunk;
TVAR struct Reb_Chunk *TG_Head_Chunk;
TVAR struct Reb_Chunker *TG_Root_Chunker;

TVAR struct Reb_State *Saved_State; // Saved state for Catch (CPU state, etc.)

#if !defined(NDEBUG)
    // In debug builds, the `panic` and `fail` macros capture the file and
    // line number of instantiation so any Make_Error can pick it up.
    TVAR const char *TG_Erroring_C_File;
    TVAR int TG_Erroring_C_Line;

    TVAR REBFLGS TG_Pushing_Mold; // Push_Mold should not directly recurse
#endif

//-- Evaluation variables:
TVAR REBI64 Eval_Cycles;    // Total evaluation counter (upward)
TVAR REBI64 Eval_Limit;     // Evaluation limit (set by secure)
TVAR REBINT Eval_Count;     // Evaluation counter (downward)
TVAR REBINT Eval_Dose;      // Evaluation counter reset value
TVAR REBCNT Eval_Sigmask;   // Masking out signal flags

TVAR REBCNT Trace_Flags;    // Trace flag
TVAR REBINT Trace_Level;    // Trace depth desired
TVAR REBINT Trace_Depth;    // Tracks trace indentation
TVAR REBCNT Trace_Limit;    // Backtrace buffering limit
TVAR REBSER *Trace_Buffer;  // Holds backtrace lines

TVAR REBI64 Eval_Natives;
TVAR REBI64 Eval_Functions;

//-- Other per thread globals:
TVAR REBSER *Bind_Table;    // Used to quickly bind words to contexts

TVAR REBVAL Callback_Error; //Error produced by callback!, note it's not callback://
