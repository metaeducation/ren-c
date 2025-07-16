//
//  file: %sys-globals.h
//  summary: "Program and Thread Globals"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
#if RUNTIME_CHECKS
    PVAR REB_STATS *PG_Reb_Stats;
#endif

PVAR REBU64 PG_Mem_Usage;   // Overall memory used

// In Ren-C, words are Series nodes (Symbol subtype).  They may be GC'd (unless
// they are in the %words.r list, in which case their canon forms are
// protected in order to do SYM_XXX switch statements in the C source, etc.)
//
// There is a global hash table which accelerates finding a word's Series
// Stub from a UTF-8 source string.  Entries are added to it when new canon
// forms of words are created, and removed when they are GC'd.  It is scaled
// according to the total number of canons in the system.
//
PVAR Flex* PG_Symbol_Canons;  // Canon symbol pointers for words in %words.r
PVAR Flex* PG_Canons_By_Hash;  // Canon symbol pointers indexed by hash
PVAR REBLEN PG_Num_Canon_Slots_In_Use;  // Total canon hash slots (+ deleteds)
#if RUNTIME_CHECKS
    PVAR REBLEN PG_Num_Canon_Deleteds;  // Deleted canon hash slots "in use"
#endif

PVAR VarList* Lib_Context;
PVAR VarList* Sys_Context;

//-- Various char tables:
PVAR Byte *White_Chars;
PVAR Ucs2Unit* Upper_Cases;
PVAR Ucs2Unit* Lower_Cases;

// Other:
PVAR Byte *PG_Pool_Map;   // Memory pool size map (created on boot)

PVAR REBI64 PG_Boot_Time;   // Counter when boot started
PVAR REB_OPTS *Reb_Opts;

#if DEBUG_HAS_PROBE
    PVAR bool g_probe_panics; // helpful especially for boot errors/crashes
#endif

#if DEBUG_HAS_ALWAYS_MALLOC
    PVAR bool PG_Always_Malloc;   // For memory-related troubleshooting
#endif

// These are some canon BLANK, TRUE, and FALSE values (and nulled/end cells).
// In two-element arrays in order that those using them don't accidentally
// pass them to routines that will increment the pointer as if they are
// arrays--they are singular values, and the second element is set to
// be trash to trap any unwanted access.
//
PVAR Cell PG_End_Node;
PVAR Value PG_Nulled_Cell[2];

PVAR Value PG_Blank_Value[2];
PVAR Value PG_Bar_Value[2];
PVAR Value PG_Okay_Value[2];
PVAR Value PG_Trash_Value[2];

PVAR Value PG_Bounce_Invisible[2]; // has "pseudotype" TYPE_R_INVISIBLE
PVAR Value PG_Bounce_Immediate[2]; // has "pseudotype" TYPE_R_IMMEDIATE
PVAR Value PG_Bounce_Redo_Unchecked[2]; // "pseudotype" TYPE_R_REDO + FALSEY flag
PVAR Value PG_Bounce_Redo_Checked[2]; // "pseudotype" TYPE_R_REDO + no FALSEY flag
PVAR Value PG_Bounce_Reference[2]; // "pseudotype" TYPE_R_REFERENCE
PVAR Value PG_Bounce_Thrown[2]; // has "pseudotype" TYPE_R_THROWN

// These are root variables which used to be described in %root.r and kept
// alive by keeping that array alive.  Now they are API handles, kept alive
// by the same mechanism they use.  This means they can be initialized at
// the appropriate moment during the boot, one at a time.

PVAR Value* Root_System;
PVAR Value* Root_Typesets;

PVAR Value* Root_Here_Tag; // used in modern parse as `pos: <here>`
PVAR Value* Root_With_Tag; // overrides locals gathering (can disable RETURN)
PVAR Value* Root_Ellipsis_Tag; // marks variadic argument <...>
PVAR Value* Root_End_Tag; // marks endable argument (NULL if at end of input)
PVAR Value* Root_Undo_Opt_Tag; // marks that passing void will receive null
PVAR Value* Root_Opt_Out_Tag; // marks that passing void won't run the action
PVAR Value* Root_Local_Tag; // marks beginning of a list of "pure locals"
PVAR Value* Root_Skip_Tag; // marks a hard quote as "skippable" if wrong type

PVAR Value* Root_Empty_Text; // read-only ""
PVAR Value* Root_Empty_Binary; // read-only #{}
PVAR Value* Root_Empty_Block; // read-only []
PVAR Array* PG_Empty_Array; // optimization of Cell_Array(Root_Empty_Block)

PVAR Value* Root_Space_Char; // ' ' as a CHAR!
PVAR Value* Root_Newline_Char; // '\n' as a CHAR!

PVAR Value* Root_Action_Meta;

PVAR Value* Root_Stats_Map;

PVAR Value* Root_Stackoverflow_Error; // made in advance, avoids extra calls
PVAR Value* g_error_veto; // made in advance, avoids extra calls


// This signal word should be thread-local, but it will not work
// when implemented that way. Needs research!!!!
PVAR Flags Eval_Signals;   // Signal flags

// The "dummy" action is used in frames which are marked as being action
// frames because they need a varlist, that don't actually execute.
//
PVAR REBACT *PG_Dummy_Action;

/***********************************************************************
**
**  Thread Globals - Local to each thread
**
***********************************************************************/

TVAR Value TG_Thrown_Arg;  // Non-GC protected argument to THROW

// !!! These values were held in REBVALs for some reason in R3-Alpha, which
// means that since they were INTEGER! they were signed 64-bit integers.  It
// seems the code wants to clip them to 32-bit often, however.
//
TVAR REBI64 TG_Ballast;
TVAR REBI64 TG_Max_Ballast;

//-- Memory and GC:
TVAR REBPOL *Mem_Pools;     // Memory pool array
TVAR bool GC_Recycling;    // True when the GC is in a recycle
TVAR REBINT GC_Ballast;     // Bytes allocated to force automatic GC
TVAR bool GC_Disabled;      // true when RECYCLE/OFF is run
TVAR Flex* GC_Guarded; // A stack of GC protected series and values
PVAR Flex* GC_Mark_Stack; // Series pending to mark their reachables as live
TVAR Flex* *Prior_Expand; // Track prior series expansions (acceleration)

TVAR Flex* TG_Mold_Stack; // Used to prevent infinite loop in cyclical molds

TVAR Array* TG_Buf_Collect; // for collecting object keys or words
TVAR Strand* TG_Buf_Ucs2; // UCS2 reused buffer
TVAR Binary* TG_Byte_Buf; // temporary byte buffer used mainly by raw print
TVAR Binary* TG_Mold_Buf; // temporary UTF8 buffer - used mainly by mold

TVAR Flex* GC_Manuals;    // Manually memory managed (not by GC)

#if !defined(OS_STACK_GROWS_UP) && !defined(OS_STACK_GROWS_DOWN)
    TVAR bool TG_Stack_Grows_Up; // Will be detected via questionable method
#endif
TVAR uintptr_t TG_Stack_Limit;    // Limit address for CPU stack.

#if DEBUG_COUNT_TICKS
    //
    // This counter is incremented each time through the DO loop, and can be
    // used for many purposes...including setting breakpoints in routines
    // other than Do_Next that are contingent on a certain "tick" elapsing.
    //
    TVAR Tick TG_Tick; // expressions, EVAL moments, PARSE steps bump this
    TVAR Tick TG_Break_At_Tick; // runtime break tick set by C-DEBUG_BREAK
#endif

#if RUNTIME_CHECKS
    TVAR intptr_t TG_Num_Black_Flex;
#endif

// Each time Eval_Core is called a LevelStruct is pushed to the "level stack".
// Some pushed entries will represent groups or paths being executed, and
// some will represent functions that are gathering arguments...hence they
// have been "pushed" but are not yet actually running.  This stack must
// be filtered to get an understanding of something like a "backtrace of
// currently running functions".
//
TVAR Level* TG_Top_Level;
TVAR Level* TG_Bottom_Level;
TVAR struct Reb_Level_Source TG_Level_Source_End;


// When Drop_Level() happens, it may have an allocated varlist Array that
// can be reused by the next Push_Level().  Reusing this has a significant
// performance impact, as opposed to paying for freeing the memory when a
// frame is dropped and then reallocating it when the next one is pushed.
//
TVAR Array* TG_Reuse;

//-- Evaluation stack:
TVAR Array* DS_Array;
TVAR StackIndex DS_Index;
TVAR Value* DS_Movable_Top;
TVAR Value* DS_Movable_Tail;

TVAR struct Reb_State *Saved_State; // Saved state for Catch (CPU state, etc.)

#if RUNTIME_CHECKS
    TVAR bool TG_Pushing_Mold; // Push_Mold should not directly recurse
#endif

//-- Evaluation variables:
TVAR REBI64 Eval_Cycles;    // Total evaluation counter (upward)
TVAR int_fast32_t Eval_Count;     // Evaluation counter (downward)
TVAR uint_fast32_t Eval_Dose;      // Evaluation counter reset value
TVAR Flags Eval_Sigmask;   // Masking out signal flags
