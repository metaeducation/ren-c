//
//  File: %sys-globals.h
//  Summary: "Program and Thread Globals"
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
PVAR const REBSTR *PG_Slash_1_Canon;  // Preallocated "fake" word for `/`
PVAR const REBSTR *PG_Dot_1_Canon;  // Preallocated "fake" word for `.`

PVAR REBSER *PG_Symbol_Canons; // Canon symbol pointers for words in %words.r
PVAR REBSER *PG_Canons_By_Hash; // Canon REBSER pointers indexed by hash
PVAR REBLEN PG_Num_Canon_Slots_In_Use; // Total canon hash slots (+ deleteds)
#if !defined(NDEBUG)
    PVAR REBLEN PG_Num_Canon_Deleteds; // Deleted canon hash slots "in use"
#endif
PVAR const REBSTR *PG_Bar_Canon;  // fast canon value for testing for `|`

PVAR REBCTX *Lib_Context;
PVAR REBCTX *Sys_Context;

//-- Various char tables:
PVAR REBYTE *White_Chars;
PVAR REBUNI *Upper_Cases;
PVAR REBUNI *Lower_Cases;

// Other:
PVAR REBYTE *PG_Pool_Map;   // Memory pool size map (created on boot)

PVAR REB_OPTS *Reb_Opts;

#ifdef DEBUG_HAS_PROBE
    PVAR bool PG_Probe_Failures; // helpful especially for boot errors & panics
#endif

#ifdef INCLUDE_CALLGRIND_NATIVE
    PVAR bool PG_Callgrind_On;
#endif

#ifdef DEBUG_ENABLE_ALWAYS_MALLOC
    PVAR bool PG_Always_Malloc;   // For memory-related troubleshooting
#endif

// These are some canon BLANK, TRUE, and FALSE values (and nulled/end cells).

PVAR RELVAL PG_End_Node;
PVAR REBVAL PG_Nulled_Cell;

PVAR REBVAL PG_Blank_Value;
PVAR REBVAL PG_False_Value;
PVAR REBVAL PG_True_Value;

#ifdef DEBUG_TRASH_MEMORY
    PVAR REBVAL PG_Trash_Value_Debug;
#endif

PVAR REBVAL PG_R_Invisible;  // has "pseudotype" REB_R_INVISIBLE
PVAR REBVAL PG_R_Immediate;  // has "pseudotype" REB_R_IMMEDIATE
PVAR REBVAL PG_R_Redo_Unchecked;  // "pseudotype" REB_R_REDO + false extra
PVAR REBVAL PG_R_Redo_Checked;  // "pseudotype" REB_R_REDO + true extra
PVAR REBVAL PG_R_Reference;  // "pseudotype" REB_R_REFERENCE
PVAR REBVAL PG_R_Thrown;  // has "pseudotype" REB_R_THROWN

// These are root variables which used to be described in %root.r and kept
// alive by keeping that array alive.  Now they are API handles, kept alive
// by the same mechanism they use.  This means they can be initialized at
// the appropriate moment during the boot, one at a time.

PVAR REBVAL *Root_System;
PVAR REBVAL *Root_Typesets;

PVAR REBVAL *Root_Void_Tag; // used with RETURN: <void> to suppress results
PVAR REBVAL *Root_With_Tag; // overrides locals gathering (can disable RETURN)
PVAR REBVAL *Root_Variadic_Tag; // marks variadic argument <variadic>
PVAR REBVAL *Root_Opt_Tag; // marks optional argument (can be NULL)
PVAR REBVAL *Root_End_Tag; // marks endable argument (NULL if at end of input)
PVAR REBVAL *Root_Blank_Tag; // marks that passing blank won't run the action
PVAR REBVAL *Root_Local_Tag; // marks beginning of a list of "pure locals"
PVAR REBVAL *Root_Skip_Tag; // marks a hard quote as "skippable" if wrong type
PVAR REBVAL *Root_Const_Tag; // pass a CONST version of the input argument
PVAR REBVAL *Root_Output_Tag;  // argument goes to set-block! output
PVAR REBVAL *Root_Invisible_Tag;  // return value can be invisible
PVAR REBVAL *Root_Elide_Tag;  // will make any return result act invisibly
PVAR REBVAL *Root_Modal_Tag;  // !!! needed for bootstrap, vs @arg modal

PVAR REBVAL *Root_Empty_Text; // read-only ""
PVAR REBVAL *Root_Empty_Binary; // read-only #{}
PVAR REBVAL *Root_Empty_Block; // read-only []
PVAR REBVAL *Root_2_Blanks_Block;  // read-only [_ _]
PVAR REBARR* PG_Empty_Array; // optimization of VAL_ARRAY(Root_Empty_Block)
PVAR REBARR* PG_2_Blanks_Array;  // surrogate array used by `/` paths

PVAR REBVAL *Root_Space_Char; // ' ' as a CHAR!
PVAR REBVAL *Root_Newline_Char; // '\n' as a CHAR!

PVAR REBVAL *Root_Action_Meta;

PVAR REBVAL *Root_Stackoverflow_Error;  // made in advance, avoids extra calls
PVAR REBVAL *Root_No_Memory_Error;  // also must be made in advance

PVAR REBARR *PG_Extension_Types;  // array of datatypes created by extensions

// This signal word should be thread-local, but it will not work
// when implemented that way. Needs research!!!!
PVAR REBFLGS Eval_Signals;   // Signal flags

PVAR REBDEV *PG_Device_List;  // Linked list of R3-Alpha-style "devices"


/***********************************************************************
**
**  Thread Globals - Local to each thread
**
***********************************************************************/

TVAR REBVAL TG_Thrown_Arg;  // Non-GC protected argument to THROW
#if !defined(NDEBUG)
    //
    // For reasons explained in %sys-frame.h, the thrown label is typically
    // stored in the output cell...but to make sure access goes through the
    // VAL_THROWN_LABEL(), a global is used "SPORADICALLY()"
    //
    TVAR REBVAL TG_Thrown_Label_Debug;
#endif

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
TVAR REBSER *GC_Guarded; // A stack of GC protected series and values
PVAR REBSER *GC_Mark_Stack; // Series pending to mark their reachables as live
TVAR REBSER **Prior_Expand; // Track prior series expansions (acceleration)

#if !defined(NDEBUG)  // Used by the FUZZ native to inject memory failures
    TVAR REBINT PG_Fuzz_Factor;  // (-) => a countdown, (+) percent of 10000
#endif

TVAR REBSER *TG_Mold_Stack; // Used to prevent infinite loop in cyclical molds

TVAR REBARR *TG_Buf_Collect; // for collecting object keys or words
TVAR REBSER *TG_Byte_Buf; // temporary byte buffer used mainly by raw print
TVAR REBSTR *TG_Mold_Buf; // temporary UTF8 buffer - used mainly by mold

TVAR REBSER *GC_Manuals;    // Manually memory managed (not by GC)

#if !defined(OS_STACK_GROWS_UP) && !defined(OS_STACK_GROWS_DOWN)
    TVAR bool TG_Stack_Grows_Up; // Will be detected via questionable method
#endif
TVAR uintptr_t TG_Stack_Limit;    // Limit address for CPU stack.

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
TVAR struct Reb_Feed TG_Frame_Feed_End;


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

TVAR struct Reb_State *TG_Jump_List; // Saved state for TRAP (CPU state, etc.)

#if !defined(NDEBUG)
    TVAR bool TG_Pushing_Mold; // Push_Mold should not directly recurse
#endif

//-- Evaluation variables:
TVAR REBI64 Eval_Cycles;    // Total evaluation counter (upward)
TVAR REBI64 Eval_Limit;     // Evaluation limit (set by secure)
TVAR int_fast32_t Eval_Count;     // Evaluation counter (downward)
TVAR uint_fast32_t Eval_Dose;      // Evaluation counter reset value
TVAR REBFLGS Eval_Sigmask;   // Masking out signal flags

TVAR REBFLGS Trace_Flags;    // Trace flag
TVAR REBINT Trace_Level;    // Trace depth desired
TVAR REBINT Trace_Depth;    // Tracks trace indentation
TVAR REBLEN Trace_Limit;    // Backtrace buffering limit
TVAR REBSER *Trace_Buffer;  // Holds backtrace lines
