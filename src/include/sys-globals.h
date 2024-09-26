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

PVAR Element* Lib_Module;
PVAR Element* Sys_Util_Module;
PVAR Element* User_Context_Value;

PVAR SeaOfVars* Lib_Context;
PVAR SeaOfVars* Sys_Context;
PVAR SeaOfVars* User_Context;

PVAR CFunction* const* g_native_cfunc_pos;
PVAR VarList* PG_Currently_Loading_Module;

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
// are turned into a `Bounce` (which is a `Node*` at time of writing).
//
// * If Bounce were `void*`, then these could just be `char` turned into
//   `char*` and have their first byte checked for UTF-8 and act as the signal.
//   But type checking is a little better if we make sure it's at least a
//   Node*, and direct pointer comparison is faster than dereference anyway.
//
// * Comparisons to literal pointers like (Node*)0x00000010 might be faster,
//   but that is "implementation defined behavior" and could have problems.
//   Consider investigating if it's worth going outside the C standard in some
//   builds to use such "magic numbers":
//
//     https://stackoverflow.com/q/51083356

PVAR Value PG_Bounce_Redo_Unchecked;
PVAR Value PG_Bounce_Redo_Checked;
PVAR Value PG_Bounce_Thrown;
PVAR Value PG_Bounce_Continuation;
PVAR Value PG_Bounce_Delegation;
PVAR Value PG_Bounce_Suspend;


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
PVAR Array* PG_Empty_Array; // optimization of Cell_Array(Root_Empty_Block)
PVAR Array* PG_1_Quasi_Null_Array;  // used by heavy nulls ~[~null~]~
PVAR Array* PG_1_Quasi_Void_Array;  // used by heavy voids ~[~void~]~

PVAR Value* Root_Meta_Heavy_Null;  // keeps PG_1_Quasi_Null_Array alive
PVAR Value* Root_Meta_Heavy_Void;  // keeps PG_1_Quasi_Void_Array alive

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
