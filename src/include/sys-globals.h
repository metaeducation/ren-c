//
//  file: %sys-globals.h
//  summary: "Program and Thread Globals"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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

// This is a C array that holds 8-platform-pointer Stubs (the structures
// themselves, not pointers to them), arranged in canon order.  It provides
// fast access to Patches for variables in LIB() by SymId.
//
PVAR Patch g_lib_patches[MAX_SYM_LIB_PREMADE + 1];  // instances, not pointers
PVAR Patch g_datatype_patches[NUM_BUILTIN_TYPES + 1];  // instances

PVAR SeaOfVars* g_datatypes_context;  // immutable by user, canon datatypes
PVAR SeaOfVars* g_lib_context;  // inherits g_datatypes_context
PVAR SeaOfVars* g_sys_util_context;  // inherits g_lib_context
PVAR SeaOfVars* g_user_context;  // inherits g_lib_context

PVAR Element* g_datatypes_module;  // keeps g_datatypes_context alive
PVAR Element* g_lib_module;  // keeps g_lib_context alive
PVAR Element* g_sys_util_module;  // keeps g_sys_util_context alive
PVAR Element* g_user_module;  // keeps g_user_context alive

PVAR RebolContext* librebol_binding;  // global, set to g_lib_context

PVAR bool g_current_uses_librebol;
PVAR CFunction* const* g_native_cfunc_pos;
PVAR SeaOfVars* g_currently_loading_module;

//-- Various char tables:
PVAR Byte* White_Chars;
PVAR Codepoint *Upper_Cases;
PVAR Codepoint *Lower_Cases;

#if DEBUG_HAS_PROBE
    PVAR bool g_probe_panics;  // helpful especially for boot crashes
#endif

#ifdef INCLUDE_CALLGRIND_NATIVE
    PVAR bool PG_Callgrind_On;
#endif

// These are some canon cells that cannot be offered via LIB(XXX).  e.g. if
// TRIPWIRE were actually a TRASH! value, users couldn't access it by typing
// `tripwire` (they'd get an error on trash access)...so it's a function.
//

PVAR Value PG_Feed_At_End;  // Canon pointer; internals use instead of rebEND

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

PVAR WildTwo g_bounce_redo_unchecked;
PVAR WildTwo g_bounce_redo_checked;
PVAR WildTwo g_bounce_downshifted;
PVAR WildTwo g_bounce_thrown;
PVAR WildTwo g_bounce_panic;
PVAR WildTwo g_bounce_continuation;
PVAR WildTwo g_bounce_delegation;
PVAR WildTwo g_bounce_suspend;
PVAR WildTwo g_bounce_bad_intrinsic_arg;

PVAR Flex* g_dispatcher_table;


// These are root variables which used to be described in %root.r and kept
// alive by keeping that array alive.  Now they are API handles, kept alive
// by the same mechanism they use.  This means they can be initialized at
// the appropriate moment during the boot, one at a time.

PVAR Element* g_tag_with;  // documents external variables used by function
PVAR Element* g_tag_variadic;  // marks variadic argument <variadic>
PVAR Element* g_tag_end;  // marks endable argument (NULL if at end of input)
PVAR Element* g_tag_opt_out;  // passing void makes action a noop
PVAR Element* g_tag_opt;  // passing void turns argument into a null
PVAR Element* g_tag_local; // marks beginning of a list of "pure locals"
PVAR Element* g_tag_const; // pass a CONST version of the input argument
PVAR Element* g_tag_divergent;  // marks divergent functions
PVAR Element* g_tag_unrun;  // parameters that degrade antiform actions

PVAR Element* g_tag_here;  // https://forum.rebol.info/t/1558/5

PVAR Element* g_empty_text; // read-only ""
PVAR Element* g_empty_blob; // read-only #{}
PVAR Element* g_empty_block; // read-only []
PVAR Element* g_empty_object;
PVAR Element* g_quasi_null;  // ~null~ quasiform
PVAR Value* g_tripwire;  // ~ antiform (LIB(TRIPWIRE) is a function)
PVAR Source* g_empty_array; // optimization of Cell_Array(g_empty_block)
PVAR VarList* g_empty_varlist;
PVAR Source* g_1_quasi_null_array;  // used by heavy nulls ~[~null~]~

PVAR Value* g_lifted_heavy_null;  // keeps g_1_quasi_null_array alive

PVAR Element* Root_Feed_Null_Substitute;  // flagged with FEED_NOTE_META

PVAR Stub PG_Inaccessible_Stub;  // GC canonizes all inaccessible stubs to this

PVAR Element* Root_Action_Adjunct;

PVAR Value* g_error_stack_overflow;  // made in advance, avoids extra calls
PVAR Value* g_error_no_memory;  // also must be made in advance

PVAR Value* g_error_utf8_too_short;
PVAR Value* g_error_utf8_trail_bad_bit;
PVAR Value* g_error_overlong_utf8;
PVAR Value* g_error_codepoint_too_high;
PVAR Value* g_error_no_utf8_surrogates;
PVAR Value* g_error_illegal_zero_byte;

PVAR Value* g_error_done;
PVAR Value* g_error_veto;

PVAR Cell g_erased_cell;  // target of bottommost level, always erased cell

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

#if TRAMPOLINE_COUNTS_TICKS  // C watchlist uses too often to put in g_ts.tick
    TVAR Tick g_tick;  // starts at 1, so 0 means (! TRAMPOLINE_COUNTS_TICKS)
#endif

#if RUNTIME_CHECKS
    TVAR uintptr_t g_sporadic;  // counter behind SPORADICALLY()
#endif

TVAR MoldState g_mold;

#if RUNTIME_CHECKS
    TVAR Count g_num_evars_outstanding;  // See Init_Evars()
#endif
