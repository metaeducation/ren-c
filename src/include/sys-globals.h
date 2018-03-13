//
//  File: %sys-globals.h
//  Summary: "Program and Thread Globals"
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

//-- Bootstrap variables:
PVAR REBINT PG_Boot_Phase;  // To know how far in the boot we are.
PVAR REBINT PG_Boot_Level;  // User specified startup level

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

// In Ren-C, words are REBSER nodes (REBSTR subtype).  They may be GC'd (unless
// they are in the %words.r list, in which case their canon forms are
// protected in order to do SYM_XXX switch statements in the C source, etc.)
//
// There is a global hash table which accelerates finding a word's REBSER
// node from a UTF-8 source string.  Entries are added to it when new canon
// forms of words are created, and removed when they are GC'd.  It is scaled
// according to the total number of canons in the system.
//
PVAR REBSTR *PG_Symbol_Canons; // Canon symbol pointers for words in %words.r
PVAR REBSTR *PG_Canons_By_Hash; // Canon REBSER pointers indexed by hash
PVAR REBCNT PG_Num_Canon_Slots_In_Use; // Total canon hash slots (+ deleteds)
#if !defined(NDEBUG)
    PVAR REBCNT PG_Num_Canon_Deleteds; // Deleted canon hash slots "in use"
#endif

//-- Main contexts:
PVAR REBARR *PG_Root_Array; // Frame that holds Root_Vars
PVAR ROOT_VARS *Root_Vars; // PG_Root_Array's values as a C structure

PVAR REBCTX *Lib_Context;
PVAR REBCTX *Sys_Context;

//-- Various char tables:
PVAR REBYTE *White_Chars;
PVAR REBUNI *Upper_Cases;
PVAR REBUNI *Lower_Cases;

// Other:
PVAR REBYTE *PG_Pool_Map;   // Memory pool size map (created on boot)

PVAR REBI64 PG_Boot_Time;   // Counter when boot started
PVAR REB_OPTS *Reb_Opts;

#ifdef DEBUG_HAS_PROBE
    PVAR REBOOL PG_Probe_Failures; // helpful especially for boot errors & panics
#endif

#ifndef NDEBUG
    PVAR REBOOL PG_Always_Malloc;   // For memory-related troubleshooting
#endif

// These are some canon BLANK, TRUE, and FALSE values (and void/end cells).
// In two-element arrays in order that those using them don't accidentally
// pass them to routines that will increment the pointer as if they are
// arrays--they are singular values, and the second element is set to
// be trash to trap any unwanted access.
//
PVAR RELVAL PG_End_Node;
PVAR REBVAL PG_Void_Cell[2];

PVAR REBVAL PG_Blank_Value[2];
PVAR REBVAL PG_Bar_Value[2];
PVAR REBVAL PG_False_Value[2];
PVAR REBVAL PG_True_Value[2];

PVAR REBARR* PG_Empty_Array; // optimization of VAL_ARRAY(EMPTY_BLOCK)

// This signal word should be thread-local, but it will not work
// when implemented that way. Needs research!!!!
PVAR REBFLGS Eval_Signals;   // Signal flags

PVAR REBBRK PG_Breakpoint_Hook; // hook called to spawn the debugger

// !!! See bad hack in %t-port.c that uses this for the moment.
//
PVAR REBVAL PG_Write_Action;


// It is possible to swap out the evaluator for one that does tracing, or
// single step debugging, etc.
//
PVAR REBDOF PG_Do; // Rebol "DO function" (takes REBFRM, returns void)
PVAR REBAPF PG_Apply; // Rebol "APPLY function" (takes REBFRM, returns REB_R)


/***********************************************************************
**
**  Thread Globals - Local to each thread
**
***********************************************************************/

TVAR REBARR *TG_Task_Array; // Array that holds Task_Vars
TVAR TASK_VARS *Task_Vars; // TG_Task_Array's values as a C structure

TVAR REBVAL TG_Thrown_Arg;  // Non-GC protected argument to THROW

//-- Memory and GC:
TVAR REBPOL *Mem_Pools;     // Memory pool array
TVAR REBOOL GC_Recycling;    // True when the GC is in a recycle
TVAR REBINT GC_Ballast;     // Bytes allocated to force automatic GC
TVAR REBOOL GC_Disabled;      // TRUE when RECYCLE/OFF is run
TVAR REBSER *GC_Guarded; // A stack of GC protected series and values
PVAR REBSER *GC_Mark_Stack; // Series pending to mark their reachables as live
TVAR REBSER **Prior_Expand; // Track prior series expansions (acceleration)

TVAR REBSER *TG_Mold_Stack; // Used to prevent infinite loop in cyclical molds

// These manually-managed series must either be freed with Free_Series()
// or handed over to the GC at certain synchronized points, else they
// would represent a memory leak in the release build.
TVAR REBSER *GC_Manuals;    // Manually memory managed (not by GC)

#if !defined(OS_STACK_GROWS_UP) && !defined(OS_STACK_GROWS_DOWN)
    TVAR REBOOL TG_Stack_Grows_Up; // Will be detected via questionable method
#endif
TVAR REBUPT TG_Stack_Limit;    // Limit address for CPU stack.

#ifdef DEBUG_COUNT_TICKS
    //
    // This counter is incremented each time through the DO loop, and can be
    // used for many purposes...including setting breakpoints in routines
    // other than Do_Next that are contingent on a certain "tick" elapsing.
    //
    TVAR REBUPT TG_Tick; // expressions, EVAL moments, PARSE steps bump this
    TVAR REBUPT TG_Break_At_Tick; // runtime break tick set by C-DEBUG_BREAK
#endif

#if !defined(NDEBUG)
    TVAR REBIPT TG_Num_Black_Series;
#endif

// Each time Do_Core is called a Reb_Frame* is pushed to the "frame stack".
// Some pushed entries will represent groups or paths being executed, and
// some will represent functions that are gathering arguments...hence they
// have been "pushed" but are not yet actually running.  This stack must
// be filtered to get an understanding of something like a "backtrace of
// currently running functions".
//
TVAR REBFRM *TG_Frame_Stack;

//-- Evaluation stack:
TVAR REBARR *DS_Array;
TVAR REBDSP DS_Index;
TVAR REBVAL *DS_Movable_Base;

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
    TVAR REBOOL TG_Pushing_Mold; // Push_Mold should not directly recurse
#endif

//-- Evaluation variables:
TVAR REBI64 Eval_Cycles;    // Total evaluation counter (upward)
TVAR REBI64 Eval_Limit;     // Evaluation limit (set by secure)
TVAR REBINT Eval_Count;     // Evaluation counter (downward)
TVAR REBCNT Eval_Dose;      // Evaluation counter reset value
TVAR REBFLGS Eval_Sigmask;   // Masking out signal flags

TVAR REBFLGS Trace_Flags;    // Trace flag
TVAR REBINT Trace_Level;    // Trace depth desired
TVAR REBINT Trace_Depth;    // Tracks trace indentation
TVAR REBCNT Trace_Limit;    // Backtrace buffering limit
TVAR REBSER *Trace_Buffer;  // Holds backtrace lines
