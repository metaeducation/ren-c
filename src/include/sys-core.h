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
**  Summary: System Core Include
**  Module:  sys-core.h
**  Author:  Carl Sassenrath
**  Notes:
**
***********************************************************************/

#include "reb-config.h"

// Set as compiler symbol flags:
//#define UNICODE				// enable unicode OS API (windows)

// Internal configuration:
#define REB_DEF					// kernel definitions and structs
//#define SERIES_LABELS			// enable identifier labels for series
#define STACK_MIN   4000		// data stack increment size
#define STACK_LIMIT 400000		// data stack max (6.4MB)
#define MIN_COMMON 10000		// min size of common buffer
#define MAX_COMMON 100000		// max size of common buffer (shrink trigger)
#define	MAX_NUM_LEN 64			// As many numeric digits we will accept on input
#define MAX_SAFE_SERIES 5		// quanitity of most recent series to not GC.
#define MAX_EXPAND_LIST 5		// number of series-1 in Prior_Expand list
#define USE_UNICODE 1			// scanner uses unicode
#define UNICODE_CASES 0x2E00	// size of unicode folding table
#define HAS_SHA1				// allow it
#define HAS_MD5					// allow it

// External system includes:
#include <stdlib.h>
#include <stdarg.h>		// For var-arg Print functions
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <assert.h>

// Special OS-specific definitions:
#ifdef OS_DEFS
	#ifdef TO_WINDOWS
	#include <windows.h>
	#undef IS_ERROR
	#endif
	//#error The target platform must be specified (TO_* define)
#endif

#ifdef OS_IO
	#include <stdio.h>
	#include <stdarg.h>
#endif

// Local includes:
#include "reb-c.h"

// Must be defined at the end of reb-c.h, but not *in* reb-c.h so that
// files including sys-core.h and reb-host.h can have differing
// definitions of REBCHR.  (We want it opaque to the core, but the
// host to have it compatible with the native character type w/o casting)
#ifdef OS_WIDE_CHAR
	#ifdef NDEBUG
		typedef REBUNI REBCHR;
	#else
		typedef struct tagREBCHR {
			REBUNI num;
		} REBCHR;
	#endif
#else
	#ifdef NDEBUG
		typedef REBYTE REBCHR;
	#else
		typedef struct tagREBCHR {
			REBYTE num;
		} REBCHR;
	#endif
#endif

#include "reb-defs.h"
#include "reb-args.h"
#include "tmp-bootdefs.h"
#define PORT_ACTIONS A_CREATE  // port actions begin here

#include "reb-device.h"
#include "reb-types.h"
#include "reb-event.h"

#include "sys-deci.h"

#include "sys-value.h"
#include "tmp-strings.h"
#include "tmp-funcargs.h"

#include "reb-struct.h"

//-- Port actions (for native port schemes):
typedef struct rebol_port_action_map {
	const REBCNT action;
	const REBPAF func;
} PORT_ACTION;

typedef struct rebol_mold {
	REBSER *series;		// destination series (uni)
	REBCNT opts;		// special option flags
	REBINT indent;		// indentation amount
//	REBYTE space;		// ?
	REBYTE period;		// for decimal point
	REBYTE dash;		// for date fields
	REBYTE digits;		// decimal digits
} REB_MOLD;

#include "reb-file.h"
#include "reb-filereq.h"
#include "reb-math.h"
#include "reb-codec.h"

#include "tmp-sysobj.h"
#include "tmp-sysctx.h"

//#include "reb-net.h"
#include "sys-panics.h"
#include "tmp-boot.h"
#include "sys-mem.h"
#include "tmp-errnums.h"
#include "host-lib.h"
#include "sys-stack.h"
#include "sys-state.h"

/***********************************************************************
**
**	Constants
**
***********************************************************************/

enum Boot_Phases {
	BOOT_START = 0,
	BOOT_LOADED,
	BOOT_ERRORS,
	BOOT_MEZZ,
	BOOT_DONE
};

enum Boot_Levels {
	BOOT_LEVEL_BASE,
	BOOT_LEVEL_SYS,
	BOOT_LEVEL_MODS,
	BOOT_LEVEL_FULL
};

// Modes allowed by Make_Series function:
enum {
	MKS_NONE		= 0,		// data is opaque (not delved into by the GC)
	MKS_BLOCK		= 1 << 0,	// Contains REBVALs (seen by GC and Debug)
	MKS_POWER_OF_2	= 1 << 1	// Round size up to a power of 2
};

// Modes allowed by Copy_Block function:
enum {
	COPY_SHALLOW = 0,
	COPY_DEEP,			// recurse into blocks
	COPY_STRINGS,		// copy strings in blocks
	COPY_ALL,			// both deep, strings (3)
//	COPY_IGNORE = 4,	// ignore tail position (used for stack args)
	COPY_OBJECT = 8,	// copy an object
	COPY_SAME = 16
};

#define CP_DEEP TYPESET(63)

#define TS_NOT_COPIED (TYPESET(REB_IMAGE) | TYPESET(REB_VECTOR) | TYPESET(REB_TASK) | TYPESET(REB_PORT))
#define TS_STD_SERIES (TS_SERIES & ~TS_NOT_COPIED)
#define TS_SERIES_OBJ ((TS_SERIES | TS_OBJECT) & ~TS_NOT_COPIED)
#define TS_BLOCKS_OBJ ((TS_BLOCK | TS_OBJECT) & ~TS_NOT_COPIED)

#define TS_CODE ((CP_DEEP | TS_SERIES) & ~TS_NOT_COPIED)

#define TS_FUNCLOS (TYPESET(REB_FUNCTION) | TYPESET(REB_CLOSURE))
#define TS_CLONE ((CP_DEEP | TS_SERIES | TS_FUNCLOS) & ~TS_NOT_COPIED)

// Modes allowed by Bind related functions:
enum {
	BIND_ONLY = 0,		// Only bind the words found in the context.
	BIND_SET,			// Add set-words to the context during the bind.
	BIND_ALL,			// Add words to the context during the bind.
	BIND_DEEP = 4,		// Recurse into sub-blocks.
	BIND_GET = 8,		// Lookup :word and use its word value
	BIND_NO_DUP = 16,	// Do not allow dups during word collection (for specs)
	BIND_FUNC = 32,		// Recurse into functions.
	BIND_NO_SELF = 64	// Do not bind SELF (in closures)
};

// Modes for Rebind_Block:
enum {
	REBIND_TYPE = 1,	// Change frame type when rebinding
	REBIND_FUNC	= 2,	// Rebind function and closure bodies
	REBIND_TABLE = 4	// Use bind table when rebinding
};

// Mold and form options:
enum REB_Mold_Opts {
	MOPT_MOLD_ALL,		// Output lexical types in #[type...] format
	MOPT_COMMA_PT,		// Decimal point is a comma.
	MOPT_SLASH_DATE,	// Date as 1/1/2000
//	MOPT_MOLD_VALS,		// Value parts are molded (strings are kept as is)
	MOPT_FILE,			// Molding %file
	MOPT_INDENT,		// Indentation
	MOPT_TIGHT,			// No space between block values
	MOPT_NO_NONE,		// Do not output UNSET or NONE object vars
	MOPT_EMAIL,
	MOPT_ONLY,			// Mold/only - no outer block []
	MOPT_LINES,			// add a linefeed between each value
	MOPT_MAX
};

#define GET_MOPT(v, f) GET_FLAG(v->opts, f)

// Special flags for decimal formatting:
#define DEC_MOLD_PERCENT 1  // follow num with %
#define DEC_MOLD_MINIMAL 2  // allow decimal to be integer

// Temporary:
#define MOPT_ANSI_ONLY	MOPT_MOLD_ALL	// Non ANSI chars are ^() escaped

// Reflector words (words-of, body-of, etc.)
enum Reb_Reflectors {
	OF_BASE,
	OF_WORDS, // to be compatible with R2
	OF_BODY,
	OF_SPEC,
	OF_VALUES,
	OF_TYPES,
	OF_TITLE,
	OF_MAX
};

// Load option flags:
enum {
	LOAD_ALL = 0,		// Returns header along with script if present
	LOAD_HEADER,		// Converts header to object, checks values
	LOAD_NEXT,			// Load next value
	LOAD_NORMAL,		// Convert header, load script
	LOAD_REQUIRE,		// Header is required, else error
	LOAD_MAX
};

// General constants:
#define NOT_FOUND ((REBCNT)-1)
#define UNKNOWN   ((REBCNT)-1)
#define LF 10
#define CR 13
#define TAB '\t'
#define CRLF "\r\n"
#define TAB_SIZE 4

// Move this:
enum Insert_Arg_Nums {
	AN_SERIES = 1,
	AN_VALUE,
	AN_PART,
	AN_LENGTH,
	AN_ONLY,
	AN_DUP,
	AN_COUNT
};

enum rebol_signals {
	SIG_RECYCLE,
	SIG_ESCAPE,
	SIG_EVENT_PORT,
	SIG_MAX
};

// Security flags:
enum {
	SEC_ALLOW,
	SEC_ASK,
	SEC_THROW,
	SEC_QUIT,
	SEC_MAX
};

// Security policy byte offsets:
enum {
	POL_READ,
	POL_WRITE,
	POL_EXEC,
	POL_MAX
};

// Encoding options:
enum encoding_opts {
	ENC_OPT_BIG,		// big endian (not little)
	ENC_OPT_UTF8,		// UTF-8
	ENC_OPT_UTF16,		// UTF-16
	ENC_OPT_UTF32,		// UTF-32
	ENC_OPT_BOM,		// byte order marker
	ENC_OPT_CRLF,		// CR line termination
	ENC_OPT_NO_COPY,	// do not copy if ASCII
	ENC_OPT_MAX
};

#define ENCF_NO_COPY (1<<ENC_OPT_NO_COPY)
#if OS_CRLF
#define ENCF_OS_CRLF (1<<ENC_OPT_CRLF)
#else
#define ENCF_OS_CRLF 0
#endif

/***********************************************************************
**
**	Macros
**
***********************************************************************/

// Generic defines:
#define ALIGN(s, a) (((s) + (a)-1) & ~((a)-1))

#define MEM_CARE 5				// Lower number for more frequent checks

#define UP_CASE(c) Upper_Cases[c]
#define LO_CASE(c) Lower_Cases[c]
#define IS_WHITE(c) ((c) <= 32 && (White_Chars[c]&1) != 0)
#define IS_SPACE(c) ((c) <= 32 && (White_Chars[c]&2) != 0)

#define SET_SIGNAL(f) SET_FLAG(Eval_Signals, f)
#define GET_SIGNAL(f) GET_FLAG(Eval_Signals, f)
#define CLR_SIGNAL(f) CLR_FLAG(Eval_Signals, f)

#define	DECIDE(cond) if (cond) goto is_true; else goto is_false
#define REM2(a, b) ((b)!=-1 ? (a) % (b) : 0)


/***********************************************************************
**
**	DO OPERATIONS
**
**		Simple macro wrappers for Do_Core.  DO_NEXT can return:
**
**			END_FLAG if end of series prohibited a full evaluation
**			THROWN_FLAG if the output is THROWN()
**			...or the next index position for attempting evaluation
**
**		DO_BLOCK runs from a given index to the tail of the block,
**		either to completion or to throw, return, break, continue, etc.
**		If it returns TRUE, then the output contains a THROWN() value.
**		Otherwise it's the last value evaluated in the block.
**
**		Note that testing for "failure" with 'if (!DO_BLOCK(...))'
**		won't let you catch "errors".  Those will Trap() and you will
**		get no result at all.  A false result just means evaluation
**		hit a "throw-style" construct.
**
**		(RETURN would be an example, since there is not a SET_JUMP
**		at every function call to process a LONG_JUMP.  Hence the
**		REBVAL containing the RETURN instruction just bubbles up the
**		C call stack normally.)
**
***********************************************************************/

#define DO_NEXT(o,s,i) \
	Do_Core((o), TRUE, (s), (i), FALSE)

#define DO_BLOCK(o,s,i) \
	(THROWN_FLAG != Do_Core((o), FALSE, (s), (i), FALSE))


/***********************************************************************
**
**  ASSERTS, PANICS, and TRAPS
**
**		There are three failure calls for Rebol code; named uniquely
**		for clarity to distinguish them from the generic "crash"
**		(which would usually mean an exception violation).
**
**		Assertions are in debug builds only, and use the conventional
**		standard C assert macro.  The code inside the assert will be
**		removed if the flag NDEBUG is defined to indicate "NoDEBUGging".
**		While negative logic is counter-intuitive (e.g. `#ifndef NDEBUG`
**		vs. `#ifdef DEBUG`) it's the standard and is the least of evils:
**
**			http://stackoverflow.com/a/17241278/211160
**
**		(Assertions should mostly be used as a kind of "traffic cone"
**		when working on new code or analyzing a bug you're trying to
**		trigger in development.  It's preferable to update the design
**		via static typing or otherwise as the code hardens.)
**
**		Panics are "blue screen of death" conditions from which there
**		is no recovery.  They should be ideally identified by a
**		unique "Rebol Panic" code in sys-panics.h, but RP_MISC can be
**		used temporarily until it is named.  To help with debugging
**		the specific location where a crash happened, a macro for
**		Panic() with no parameters (common case) will also assert
**		in a debug build.
**
**		Traps are recoverable conditions which tend to represent
**		errors the user can intercept.  Each call to trap must be
**		identified by a unique "Rebol Error" code and then zero-or-more
**		parameters after that.  The parameters and the codes are
**		specified errors.r, which also has a way of templating a
**		parameterized object with arguments and a formatted message.
**		See that file for examples.  There is also a RE_MISC code
**		that takes no parameters that you can use for testing.
**
**		(Note that panic codes are also error codes; but they can
**		be used before an error object's information is available
**		in the early boot phase.)
**
**		If you call a Panic or a Trap in a function, that function
**		will not return.  As this is done with C methods exit() and
**		setjmp()/longjmp() vs. an exception model, it precludes the
**		ability of the compiler to tell if all your code paths through
**		a function return a value or not.  This is typically handled
**		via the macro DEAD_END, but it's heavy to write:
**
**			if (condition) {Trap(...); DEAD_END;}
**
**		For convenience this is wrapped up as a single macro:
**
**			if (condition) Trap_DEAD_END(...);
**
**		!!! It's a bit unfortunate that Trap wrapper functions exist
**		for something simple as Trap_Range to save on typing; because
**		those have to be re-wrapped here to ensure that if those
**		functions add more behavior then Trap_Range and
**		Trap_Range_DEAD_END won't act differently.  Fewer wrappers
**		would be needed if the error names were shorter, e.g.
**		RE_RANGE instead of RE_OUT_OF_RANGE, as Trap1(RE_RANGE, ...)
**		is not so difficult to type.
**
***********************************************************************/

void Panic_Core(REBINT id, ...);

#define DEAD_END \
	do { \
		assert(FALSE); \
		return 0; \
	} while (0)

#define DEAD_END_VOID \
	do { \
		assert(FALSE); \
		return; \
	} while (0)

#define Panic(rp) \
	do { \
		assert(0 == (rp)); /* fail here in Debug build */ \
		Panic_Core(rp); \
	} while (0)

#if !defined(NDEBUG)
	// "Series Panics" will (hopefully) trigger an alert under memory tools
	// like address sanitizer and valgrind that indicate the call stack at the
	// moment of allocation of a series.  Then you should have TWO stacks: the
	// one at the call of the Panic, and one where that series was alloc'd.

	#define Panic_Series(s) \
		do { \
			Debug_Fmt("Panic_Series() in %s at line %d", __FILE__, __LINE__); \
			if (*(s)->guard == 1020) /* should make valgrind or asan alert */ \
				Panic(RP_MISC);	 \
			Panic(RP_MISC); /* just in case it didn't crash */ \
		} while (0);

	#define Panic_Series_DEAD_END(s) \
		do { \
			Panic_Series(s); \
			DEAD_END; \
		} while (0);
#else
	// Release builds do not pay for the `guard` trick, so they just crash.

	#define Panic_Series(s) Panic(RP_MISC)

	#define Panic_Series_DEAD_END(s) Panic_DEAD_END(RP_MISC)
#endif


#define Panic_DEAD_END(rp) \
	do { \
		Panic(rp); \
		DEAD_END; \
	} while (0)

#define Trap3_DEAD_END(re,a1,a2,a3) \
	do { \
		Trap3((re), (a1), (a2), (a3)); \
		DEAD_END; \
	} while (0)

#define Trap_DEAD_END(re) \
	Trap3_DEAD_END((re), 0, 0, 0)

#define Trap1_DEAD_END(re,a1) \
	Trap3_DEAD_END((re), (a1), 0, 0)

#define Trap2_DEAD_END(re,a1,a2) \
	Trap3_DEAD_END((re), (a1), (a2), 0)

#define Trap_Arg_DEAD_END(a) \
	do { \
		Trap_Arg(a); \
		DEAD_END; \
	} while (0)

#define Trap_Type_DEAD_END(a) \
	do { \
		Trap_Type(a); \
		DEAD_END; \
	} while (0)

#define Trap_Range_DEAD_END(a) \
	do { \
		Trap_Range(a); \
		DEAD_END; \
	} while (0)

#define Trap_Make_DEAD_END(t,s) \
	do { \
		Trap_Make((t), (s)); \
		DEAD_END; \
	} while (0)

#define Trap_Reflect_DEAD_END(t,a) \
	do { \
		Trap_Reflect((t), (a)); \
		DEAD_END; \
	} while (0)

#define Trap_Action_DEAD_END(t,a) \
	do { \
		Trap_Action((t), (a)); \
		DEAD_END; \
	} while (0)

#define Trap_Types_DEAD_END(re,t1,t2) \
	do { \
		Trap_Types((re), (t1), (t2)); \
		DEAD_END; \
	} while (0)

#define Trap_Port_DEAD_END(re,p,c) \
	do { \
		Trap_Port((re), (p), (c)); \
		DEAD_END; \
	} while (0)


/***********************************************************************
**
**  DEBUG PROBING
**
**		Debugging Rebol has traditionally been "printf-style".  Hence
**		a good mechanism for putting debug info into the executable and
**		creating IDE files was not in the open source release.  As
**		these weaknesses are remedied with CMake targets and other
**		methods, adding probes into the code is still useful.
**
**		In order to make it easier to find out where a piece of debug
**		spew is coming from, the file and line number are included.
**		You should not check in calls to PROBE, and they are only
**		in Debug builds.
**
***********************************************************************/

#if !defined(NDEBUG)
	#define PROBE(v) \
		Probe_Core_Debug(NULL, __FILE__, __LINE__, (v))

	#define PROBE_MSG(v, m) \
		Probe_Core_Debug((m), __FILE__, __LINE__, (v))
#endif


#define	NO_RESULT	((REBCNT)(-1))
#define	ALL_BITS	((REBCNT)(-1))
#ifdef HAS_LL_CONSTS
#define	ALL_64		((REBU64)0xffffffffffffffffLL)
#else
#define	ALL_64		((REBU64)0xffffffffffffffffL)
#endif

#define BOOT_STR(c,i) c_cast(const REBYTE *, PG_Boot_Strs[(c) + (i)])

//-- Temporary Buffers
//   These are reused for cases for appending, when length cannot be known.
#define BUF_EMIT  VAL_SERIES(TASK_BUF_EMIT)
#define BUF_WORDS VAL_SERIES(TASK_BUF_WORDS)
#define BUF_PRINT VAL_SERIES(TASK_BUF_PRINT)
#define BUF_FORM  VAL_SERIES(TASK_BUF_FORM)
#define BUF_MOLD  VAL_SERIES(TASK_BUF_MOLD)
#define BUF_UTF8  VAL_SERIES(TASK_BUF_UTF8)
#define MOLD_LOOP VAL_SERIES(TASK_MOLD_LOOP)

#ifdef OS_WIDE_CHAR
#define BUF_OS_STR BUF_MOLD
#else
#define BUF_OS_STR BUF_FORM
#endif

// Save/Unsave Macros:
#define SAVE_SERIES(s) Save_Series(s)
#define UNSAVE_SERIES(s) \
	do { \
		GC_Protect->tail--; \
		assert(((REBSER **)(GC_Protect->data))[GC_Protect->tail] == s); \
	} while (0)


// Rebol doesn't want to crash in the event of a stack overflow, but would
// like to gracefully trap it and return the user to the console.  While it
// is possible for Rebol to set a limit to how deeply it allows function
// calls in the interpreter to recurse, there's no *portable* way to
// catch a stack overflow in the C code of the interpreter itself.
//
// Hence, Rebol uses a non-portable and non-standard heuristic.  It looks
// at the compiled addresses of local (stack-allocated) variables in a
// function, and decides from their relative pointers if memory is growing
// "up" or "down".  It then extrapolates that C function call frames will
// be laid out consecutively, and the memory difference between a stack
// variable in the topmost stacks can be checked against some limit.
//
// This has nothing to do with guarantees in the C standard, and compilers
// can really put variables at any address they feel like:
//
//     http://stackoverflow.com/a/1677482/211160
//
// Additionally, it puts the burden on every recursive or deeply nested
// routine to sprinkle calls to the CHECK_C_STACK_OVERFLOW macro somewhere
// in it.  The ideal answer is to make Rebol itself corral an interpreted
// script such that it can't cause the C code to stack overflow.  Lacking
// that ideal this technique could break, so build configurations should
// be able to turn it off if needed.
//
// In the meantime, CHECK_C_STACK_OVERFLOW is a macro which takes the
// address of some variable local to the currently executed function.

#ifdef OS_STACK_GROWS_UP
	#define CHECK_C_STACK_OVERFLOW(local_var) \
		if (cast(REBUPT, local_var) >= Stack_Limit) Trap_Stack_Overflow();
#else
	#define CHECK_C_STACK_OVERFLOW(local_var) \
		if (cast(REBUPT, local_var) <= Stack_Limit) Trap_Stack_Overflow();
#endif

#define STACK_BOUNDS (4*1024*1000) // note: need a better way to set it !!
// Also: made somewhat smaller than linker setting to allow trapping it


/***********************************************************************
**
**	Structures
**
***********************************************************************/

// Word Table Structure - used to manage hashed word tables (symbol tables).
typedef struct rebol_word_table
{
	REBSER	*series;	// Global block of words
	REBSER	*hashes;	// Hash table
//	REBCNT	count;		// Number of units used in hash table
} WORD_TABLE;

//-- Measurement Variables:
typedef struct rebol_stats {
	REBI64	Series_Memory;
	REBCNT	Series_Made;
	REBCNT	Series_Freed;
	REBCNT	Series_Expanded;
	REBCNT	Recycle_Counter;
	REBCNT	Recycle_Series_Total;
	REBCNT	Recycle_Series;
	REBI64  Recycle_Prior_Eval;
	REBCNT	Mark_Count;
	REBCNT	Free_List_Checked;
	REBCNT	Blocks;
	REBCNT	Objects;
} REB_STATS;

//-- Options of various kinds:
typedef struct rebol_opts {
	REBFLG	watch_obj_copy;
	REBFLG	watch_recycle;
	REBFLG	watch_series;
	REBFLG	watch_expand;
	REBFLG	crash_dump;
} REB_OPTS;

typedef struct rebol_time_fields {
	REBCNT h;
	REBCNT m;
	REBCNT s;
	REBCNT n;
} REB_TIMEF;


// DO evaltype dispatch function
typedef void (*REBDOF)(const REBVAL *ds);


/***********************************************************************
**
**	Thread Shared Variables
**
**		Set by main boot and not changed after that.
**
***********************************************************************/

extern const REBACT Value_Dispatch[];
//extern const REBYTE Upper_Case[];
//extern const REBYTE Lower_Case[];


#include "tmp-funcs.h"


/***********************************************************************
**
**	Threaded Global Variables
**
***********************************************************************/

#ifdef __cplusplus
	#define PVAR extern "C"
	#define TVAR extern "C" THREAD
#else
	#define PVAR extern
	#define TVAR extern THREAD
#endif

#include "sys-globals.h"
