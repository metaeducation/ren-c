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
    PVAR Tick TG_tick;
#endif

//-- Bootstrap variables:
PVAR REBINT PG_Boot_Phase;  // To know how far in the boot we are.

// This is a C array that holds 8-platform-pointer Array Flex Stubs (the
// structures themselves, not pointers to them), arranged in canon order.
// It provides fast access to Patches for variables in LIB by SymId.
//
PVAR Array PG_Lib_Patches[LIB_SYMS_MAX];

PVAR Element* Lib_Context_Value;
PVAR Element* Sys_Util_Module;
PVAR Element* User_Context_Value;

PVAR Context* Lib_Context;
PVAR Context* Sys_Context;
PVAR Context* User_Context;

PVAR CFunction* const* g_native_cfunc_pos;
PVAR Context* PG_Currently_Loading_Module;

//-- Various char tables:
PVAR Byte* White_Chars;
PVAR Codepoint *Upper_Cases;
PVAR Codepoint *Lower_Cases;

#if DEBUG_HAS_PROBE
    PVAR bool PG_Probe_Failures; // helpful especially for boot errors & panics
#endif

#ifdef INCLUDE_CALLGRIND_NATIVE
    PVAR bool PG_Callgrind_On;
#endif

// These are some canon cells that cannot be offered via Lib(XXX).  e.g. if
// NOTHING were a variable in Lib, users couldn't access it by typing
// `nothing` (they'd get an error on antiform var access)...so it's a function.
//

PVAR Value PG_Feed_At_End;  // Canon pointer; internals use instead of rebEND

PVAR Value PG_Nothing_Value;

// These are special return values that can be checked easily by pointer.  They
// could be checked even faster if they were fake immediate values, like
// (Value*)0x00000010...but that is not standard C.
//
PVAR Value PG_R_Redo_Unchecked;
PVAR Value PG_R_Redo_Checked;
PVAR Value PG_R_Thrown;
PVAR Value PG_R_Continuation;
PVAR Value PG_R_Delegation;
PVAR Value PG_R_Suspend;


// These are root variables which used to be described in %root.r and kept
// alive by keeping that array alive.  Now they are API handles, kept alive
// by the same mechanism they use.  This means they can be initialized at
// the appropriate moment during the boot, one at a time.

PVAR Element* Root_With_Tag; // overrides locals gathering (can disable RETURN)
PVAR Element* Root_Variadic_Tag; // marks variadic argument <variadic>
PVAR Element* Root_End_Tag; // marks endable argument (NULL if at end of input)
PVAR Element* Root_Maybe_Tag; // passing void won't run the action, return null
PVAR Element* Root_Local_Tag; // marks beginning of a list of "pure locals"
PVAR Element* Root_Const_Tag; // pass a CONST version of the input argument
PVAR Element* Root_Void_Tag;  // tolerance for void returns or passing void args
PVAR Element* Root_Unrun_Tag;  // parameters that degrade antiform actions

PVAR Element* Root_Here_Tag;  // https://forum.rebol.info/t/1558/5

PVAR Element* Root_Empty_Text; // read-only ""
PVAR Element* Root_Empty_Binary; // read-only #{}
PVAR Element* Root_Empty_Block; // read-only []
PVAR Element* Root_2_Blanks_Block;  // read-only [_ _]
PVAR Array* PG_Empty_Array; // optimization of Cell_Array(Root_Empty_Block)
PVAR Array* PG_1_Quasi_Null_Array;  // used by heavy nulls ~[~null~]~
PVAR Array* PG_1_Quasi_Void_Array;  // used by heavy voids ~[~void~]~
PVAR Array* PG_1_Meta_False_Array;  // used by heavy false ~[~false~]~
PVAR Array* PG_2_Blanks_Array;  // surrogate array used by `/` paths

PVAR Value* Root_Heavy_Null;  // antiform block containing a blank
PVAR Value* Root_Heavy_Void;  // antiform block containing a quasi null
PVAR Value* Root_Heavy_False;  // antiform block containing a meta false

PVAR Element* Root_Feed_Null_Substitute;  // flagged with FEED_NOTE_META

PVAR Stub PG_Inaccessible_Stub;  // GC canonizes all inaccessible stubs to this

PVAR Value* Root_Action_Adjunct;

PVAR Value* Root_Stackoverflow_Error;  // made in advance, avoids extra calls
PVAR Value* Root_No_Memory_Error;  // also must be made in advance

TVAR Feed* TG_End_Feed;

TVAR Binary* TG_Byte_Buf;  // byte buffer used in various spots (as BYTE_BUF)


/***********************************************************************
**
**  Thread Globals - Local to each thread
**
***********************************************************************/

TVAR MemoryState g_mem;

TVAR SymbolState g_symbols;

TVAR GarbageCollectorState g_gc;

TVAR DataStackState g_ds;

TVAR TrampolineState g_ts;

TVAR MoldState g_mold;

#if DEBUG
    TVAR Count g_num_evars_outstanding;  // See Init_Evars()
#endif
