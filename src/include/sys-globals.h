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

// These are some canon BLANK, TRUE, and FALSE values (and nulled/end cells).
// In two-element arrays in order that those using them don't accidentally
// pass them to routines that will increment the pointer as if they are
// arrays--they are singular values, and the second element is set to
// be trash to trap any unwanted access.
//
PVAR RELVAL PG_End_Node;
PVAR REBVAL PG_Nulled_Cell[2];

PVAR REBVAL PG_Blank_Value[2];
PVAR REBVAL PG_Bar_Value[2];
PVAR REBVAL PG_False_Value[2];
PVAR REBVAL PG_True_Value[2];
PVAR REBVAL PG_Void_Value[2];

// These are root variables which used to be described in %root.r and kept
// alive by keeping that array alive.  Now they are API handles, kept alive
// by the same mechanism they use.  This means they can be initialized at
// the appropriate moment during the boot, one at a time.

PVAR REBVAL *Root_System;
PVAR REBVAL *Root_Typesets;

PVAR REBVAL *Root_Void_Tag; // used with RETURN: <void> to suppress results
PVAR REBVAL *Root_With_Tag; // overrides locals gathering (can disable RETURN)
PVAR REBVAL *Root_Ellipsis_Tag; // marks variadic argument <...>
PVAR REBVAL *Root_Opt_Tag; // marks optional argument (can be void)
PVAR REBVAL *Root_End_Tag; // marks endable argument (void if at end of input)
PVAR REBVAL *Root_Local_Tag; // marks beginning of a list of "pure locals"
PVAR REBVAL *Root_Skip_Tag; // marks a hard quote as "skippable" if wrong type

PVAR REBVAL *Root_Empty_String; // read-only ""
PVAR REBVAL *Root_Empty_Block; // read-only []
PVAR REBARR* PG_Empty_Array; // optimization of VAL_ARRAY(Root_Empty_Block)

PVAR REBVAL *Root_Space_Char; // ' ' as a CHAR!
PVAR REBVAL *Root_Newline_Char; // '\n' as a CHAR!

PVAR REBVAL *Root_Action_Meta;

PVAR REBVAL *Root_Stats_Map;

PVAR REBVAL *Root_Stackoverflow_Error; // made in advance, avoids extra calls


// This signal word should be thread-local, but it will not work
// when implemented that way. Needs research!!!!
PVAR REBFLGS Eval_Signals;   // Signal flags

PVAR REBBRK PG_Breakpoint_Hook; // hook called to spawn the debugger

// The "dummy" action is used in frames which are marked as being action
// frames because they need a varlist, that don't actually execute.
//
PVAR REBACT *PG_Dummy_Action;

// It is possible to swap out the evaluator for one that does tracing, or
// single step debugging, etc.
//
PVAR REBDOF PG_Eval; // Evaluator (takes REBFRM, returns void)
PVAR REBDSF PG_Dispatcher; // Dispatcher (takes REBFRM, returns REB_R)


/***********************************************************************
**
**  Thread Globals - Local to each thread
**
***********************************************************************/

TVAR REBVAL TG_Thrown_Arg;  // Non-GC protected argument to THROW

// !!! These values were held in REBVALs for some reason in R3-Alpha, which
// means that since they were INTEGER! they were signed 64-bit integers.  It
// seems the code wants to clip them to 32-bit often, however.
//
TVAR REBI64 TG_Ballast;
TVAR REBI64 TG_Max_Ballast;

//-- Memory and GC:
TVAR REBPOL *Mem_Pools;     // Memory pool array
TVAR REBOOL GC_Recycling;    // True when the GC is in a recycle
TVAR REBINT GC_Ballast;     // Bytes allocated to force automatic GC
TVAR REBOOL GC_Disabled;      // TRUE when RECYCLE/OFF is run
TVAR REBSER *GC_Guarded; // A stack of GC protected series and values
PVAR REBSER *GC_Mark_Stack; // Series pending to mark their reachables as live
TVAR REBSER **Prior_Expand; // Track prior series expansions (acceleration)

TVAR REBSER *TG_Mold_Stack; // Used to prevent infinite loop in cyclical molds

TVAR REBARR *TG_Buf_Collect; // for collecting object keys or words
TVAR REBSER *TG_Buf_Utf8; // UTF8 reused buffer
TVAR REBSER *TG_Byte_Buf; // temporary byte buffer used mainly by raw print
TVAR REBSER *TG_Mold_Buf; // temporary UTF8 buffer - used mainly by mold

TVAR REBSER *GC_Manuals;    // Manually memory managed (not by GC)

#if !defined(OS_STACK_GROWS_UP) && !defined(OS_STACK_GROWS_DOWN)
    TVAR REBOOL TG_Stack_Grows_Up; // Will be detected via questionable method
#endif
TVAR uintptr_t TG_Stack_Limit;    // Limit address for CPU stack.

#ifdef DEBUG_COUNT_TICKS
    //
    // This counter is incremented each time through the DO loop, and can be
    // used for many purposes...including setting breakpoints in routines
    // other than Do_Next that are contingent on a certain "tick" elapsing.
    //
    TVAR REBTCK TG_Tick; // expressions, EVAL moments, PARSE steps bump this
    TVAR REBTCK TG_Break_At_Tick; // runtime break tick set by C-DEBUG_BREAK
#endif

#if !defined(NDEBUG)
    TVAR intptr_t TG_Num_Black_Series;
#endif

// Each time Eval_Core is called a Reb_Frame* is pushed to the "frame stack".
// Some pushed entries will represent groups or paths being executed, and
// some will represent functions that are gathering arguments...hence they
// have been "pushed" but are not yet actually running.  This stack must
// be filtered to get an understanding of something like a "backtrace of
// currently running functions".
//
TVAR REBFRM *TG_Top_Frame;
TVAR REBFRM *TG_Bottom_Frame;


// When Drop_Frame() happens, it may have an allocated varlist REBARR that
// can be reused by the next Push_Frame().  Reusing this has a significant
// performance impact, as opposed to paying for freeing the memory when a
// frame is dropped and then reallocating it when the next one is pushed.
//
TVAR REBARR *TG_Reuse;

//-- Evaluation stack:
TVAR REBARR *DS_Array;
TVAR REBDSP DS_Index;
TVAR REBVAL *DS_Movable_Top;

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


// Though only the first byte of these value cells are used, they must be
// declared on RELVAL pointer boundaries for alignment reasons.  A char string
// might be on an any byte.
//
// That means these cells are basically all wasted space besides that first
// byte.  So their payload and extra fields can be used for other things
// (e.g. where other quantities above are looked for)
//
PVAR struct Reb_Cell PG_R_FALSE;
PVAR struct Reb_Cell PG_R_TRUE;
PVAR struct Reb_Cell PG_R_VOID;
PVAR struct Reb_Cell PG_R_BLANK;
PVAR struct Reb_Cell PG_R_BAR;
PVAR struct Reb_Cell PG_R_REDO_CHECKED;
PVAR struct Reb_Cell PG_R_REDO_UNCHECKED;
PVAR struct Reb_Cell PG_R_REEVALUATE_CELL;
PVAR struct Reb_Cell PG_R_REEVALUATE_CELL_ONLY;
PVAR struct Reb_Cell PG_R_INVISIBLE;
PVAR struct Reb_Cell PG_R_REFERENCE;
PVAR struct Reb_Cell PG_R_IMMEDIATE;
PVAR struct Reb_Cell PG_R_UNHANDLED;
PVAR struct Reb_Cell PG_R_END;
PVAR struct Reb_Cell PG_R_THROWN;
