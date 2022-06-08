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

#if DEBUG_COUNT_TICKS  // VERY USEFUL! see notes in evaluator
    PVAR REBTCK TG_Tick;
#endif

//-- Bootstrap variables:
PVAR REBINT PG_Boot_Phase;  // To know how far in the boot we are.
PVAR REBINT PG_Boot_Level;  // User specified startup level

#if DEBUG_COLLECT_STATS
    PVAR REB_STATS *PG_Reb_Stats;  // Various statistics about memory, etc.
#endif

PVAR REBU64 PG_Mem_Usage;   // Overall memory used
PVAR REBU64 PG_Mem_Limit;   // Memory limit set by SECURE



// This is a series that holds 8-platform-pointer REBARR nodes, arranged in
// canon order.  It provides fast access to lib entries by symbol.
//
PVAR REBARR PG_Lib_Patches[LIB_SYMS_MAX];

// In Ren-C, words are REBSER nodes (REBSTR subtype).  They may be GC'd (unless
// they are in the %words.r list, in which case their canon forms are
// protected in order to do SYM_XXX switch statements in the C source, etc.)
//
// There is a global hash table which accelerates finding a word's REBSER
// node from a UTF-8 source string.  Entries are added to it when new canon
// forms of words are created, and removed when they are GC'd.  It is scaled
// according to the total number of canons in the system.
//
PVAR REBSYM PG_Symbol_Canons[ALL_SYMS_MAX + 1];

PVAR REBSER *PG_Symbols_By_Hash; // Symbol REBSTR pointers indexed by hash
PVAR REBLEN PG_Num_Symbol_Slots_In_Use; // Total symbol hash slots (+deleteds)
#if !defined(NDEBUG)
    PVAR REBLEN PG_Num_Symbol_Deleteds; // Deleted symbol hash slots "in use"
#endif
PVAR REBSTR PG_Deleted_Symbol;  // pointer used to indicate a deletion

PVAR REBVAL *Lib_Context_Value;
PVAR REBVAL *Sys_Context_Value;
PVAR REBVAL *User_Context_Value;

PVAR REBCTX *Lib_Context;
PVAR REBCTX *Sys_Context;
PVAR REBCTX *User_Context;

PVAR const REBNAT *PG_Next_Native_Dispatcher;
PVAR REBCTX *PG_Currently_Loading_Module;

//-- Various char tables:
PVAR REBYTE *White_Chars;
PVAR REBUNI *Upper_Cases;
PVAR REBUNI *Lower_Cases;

// Other:
PVAR REBYTE *PG_Pool_Map;   // Memory pool size map (created on boot)

PVAR REB_OPTS *Reb_Opts;

#if DEBUG_HAS_PROBE
    PVAR bool PG_Probe_Failures; // helpful especially for boot errors & panics
#endif

#ifdef INCLUDE_CALLGRIND_NATIVE
    PVAR bool PG_Callgrind_On;
#endif

#if DEBUG_ENABLE_ALWAYS_MALLOC
    PVAR bool PG_Always_Malloc;   // For memory-related troubleshooting
#endif

// These are some canon BLANK, TRUE, and FALSE values (and nulled/end cells).

PVAR REBVAL PG_End_Cell;

PVAR REBVAL PG_None_Isotope;

// These are special return values that can be checked easily by pointer.  They
// could be checked even faster if they were fake immediate values, like
// (REBVAL*)0x00000010...but that is not standard C.
//
PVAR REBVAL PG_R_Invisible;
PVAR REBVAL PG_R_Redo_Unchecked;
PVAR REBVAL PG_R_Redo_Checked;
PVAR REBVAL PG_R_Thrown;
PVAR REBVAL PG_R_Unhandled;

// These are root variables which used to be described in %root.r and kept
// alive by keeping that array alive.  Now they are API handles, kept alive
// by the same mechanism they use.  This means they can be initialized at
// the appropriate moment during the boot, one at a time.

PVAR REBVAL *Root_Typesets;

PVAR REBVAL *Root_None_Tag; // used with RETURN: <none> to suppress results
PVAR REBVAL *Root_With_Tag; // overrides locals gathering (can disable RETURN)
PVAR REBVAL *Root_Variadic_Tag; // marks variadic argument <variadic>
PVAR REBVAL *Root_Opt_Tag; // marks optional argument (can be NULL)
PVAR REBVAL *Root_End_Tag; // marks endable argument (NULL if at end of input)
PVAR REBVAL *Root_Blank_Tag; // marks that passing `_` won't run the action
PVAR REBVAL *Root_Blackhole_Tag; // marks that passing `#` won't run the action
PVAR REBVAL *Root_Local_Tag; // marks beginning of a list of "pure locals"
PVAR REBVAL *Root_Skip_Tag; // marks a hard quote as "skippable" if wrong type
PVAR REBVAL *Root_Const_Tag; // pass a CONST version of the input argument
PVAR REBVAL *Root_Void_Tag;  // tolerance for void returns or passing void args

PVAR REBVAL *Root_Unspecialized_Tag;  // unique unspecialized slot identity

PVAR REBVAL *Root_Here_Tag;  // https://forum.rebol.info/t/1558/5

PVAR REBVAL *Root_Empty_Text; // read-only ""
PVAR REBVAL *Root_Empty_Binary; // read-only #{}
PVAR REBVAL *Root_Empty_Block; // read-only []
PVAR REBVAL *Root_2_Blanks_Block;  // read-only [_ _]
PVAR REBARR* PG_Empty_Array; // optimization of VAL_ARRAY(Root_Empty_Block)
PVAR REBARR* PG_2_Blanks_Array;  // surrogate array used by `/` paths

PVAR REBARR PG_Inaccessible_Series;  // singular inaccessible varlist

PVAR REBVAL *Root_Action_Meta;

PVAR REBVAL *Root_Stackoverflow_Error;  // made in advance, avoids extra calls
PVAR REBVAL *Root_No_Memory_Error;  // also must be made in advance

PVAR REBARR *PG_Extension_Types;  // array of datatypes created by extensions

// This signal word should be thread-local, but it will not work
// when implemented that way. Needs research!!!!
PVAR REBFLGS Eval_Signals;   // Signal flags

// !!! R3-Alpha was prescriptive about the design of devices and how they
// managed requests.  Ren-C pulls back on this and has devices manage their
// own idea of how they handle events, by only giving them a hook to
// respond to Poll().
//
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

TVAR REBBIN *TG_Byte_Buf; // temporary byte buffer used mainly by raw print
TVAR REBSTR *TG_Mold_Buf; // temporary UTF8 buffer - used mainly by mold

TVAR REBSER *GC_Manuals;    // Manually memory managed (not by GC)

#if !defined(OS_STACK_GROWS_UP) && !defined(OS_STACK_GROWS_DOWN)
    TVAR bool TG_Stack_Grows_Up; // Will be detected via questionable method
#endif
TVAR uintptr_t TG_Stack_Limit;    // Limit address for CPU stack.

#if !defined(NDEBUG)
    TVAR intptr_t TG_Num_Black_Series;
#endif

#if DEBUG_EXTANT_STACK_POINTERS
    TVAR REBLEN TG_Stack_Outstanding;  // how many DS_AT()/DS_TOP refs extant
#endif

#if DEBUG_MONITOR_SERIES
    PVAR const REBNOD *PG_Monitor_Node_Debug;
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
TVAR REBFED *TG_End_Feed;


//-- Evaluation stack:
TVAR REBARR *DS_Array;
TVAR REBDSP DS_Index;
TVAR REBVAL *DS_Movable_Top;
TVAR const Cell *DS_Movable_Tail;

TVAR struct Reb_Jump *TG_Jump_List;  // Saved state for TRAP

#if !defined(NDEBUG)
    TVAR bool TG_Pushing_Mold; // Push_Mold should not directly recurse
#endif

// !!! In R3-Alpha, micro-optimizations were stylized so that it would set
// a counter for how many cycles would pass before it automatically triggered
// garbage collection.  It would decrement that counter looking for zero, and
// when zero was reached it would add the number of cycles it had been
// counting down from to the total.  This avoided needing to do math on
// multiple counters on every eval step...limiting it to a periodic
// reconciliation when GCs occurred.  Ren-C keeps this, but the debug build
// double checks that whatever magic is done reflects the real count.
//
#if !defined(NDEBUG)
    TVAR REBI64 Total_Eval_Cycles_Doublecheck;
#endif
TVAR REBI64 Total_Eval_Cycles;      // Total evaluation counter (upward)
TVAR REBI64 Eval_Limit;             // Evaluation limit (set by secure)
TVAR int_fast32_t Eval_Countdown;  // Evaluation counter until Do_Signals()
TVAR int_fast32_t Eval_Dose;        // Evaluation counter reset value
TVAR REBFLGS Eval_Sigmask;          // Masking out signal flags

TVAR REBFLGS Trace_Flags;    // Trace flag
TVAR REBINT Trace_Level;    // Trace depth desired
TVAR REBINT Trace_Depth;    // Tracks trace indentation
TVAR REBLEN Trace_Limit;    // Backtrace buffering limit
TVAR REBSER *Trace_Buffer;  // Holds backtrace lines
