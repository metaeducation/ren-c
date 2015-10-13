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
	MKS_ARRAY		= 1 << 0,	// Contains REBVALs (seen by GC and Debug)
	MKS_POWER_OF_2	= 1 << 1,	// Round size up to a power of 2
	MKS_EXTERNAL	= 1 << 2,	// Uses external pointer--don't alloc data
	MKS_PRESERVE	= 1 << 3,	// "Remake" only (save what data possible)
	MKS_LOCK		= 1 << 4,	// series is unexpandable
	MKS_GC_MANUALS	= 1 << 5	// used in implementation of series itself
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

#define TS_NOT_COPIED (FLAGIT_64(REB_IMAGE) | FLAGIT_64(REB_VECTOR) | FLAGIT_64(REB_TASK) | FLAGIT_64(REB_PORT))
#define TS_STD_SERIES (TS_SERIES & ~TS_NOT_COPIED)
#define TS_SERIES_OBJ ((TS_SERIES | TS_OBJECT) & ~TS_NOT_COPIED)
#define TS_ARRAYS_OBJ ((TS_ARRAY | TS_OBJECT) & ~TS_NOT_COPIED)

#define TS_FUNCLOS (FLAGIT_64(REB_FUNCTION) | FLAGIT_64(REB_CLOSURE))
#define TS_CLONE ((TS_SERIES | TS_FUNCLOS) & ~TS_NOT_COPIED)

// These are the types which have no need to be seen by the GC.  Note that
// this list may change--for instance if garbage collection is added for
// symbols, then word types and typesets would have to be checked too.  Some
// are counterintuitive, for instance DATATYPE! contains a SPEC that is a
// series and thus has to be checked...

#define TS_NO_GC \
	(FLAGIT_64(REB_END) | FLAGIT_64(REB_UNSET) | FLAGIT_64(REB_NONE) \
	| FLAGIT_64(REB_LOGIC) | FLAGIT_64(REB_INTEGER) | FLAGIT_64(REB_DECIMAL) \
	| FLAGIT_64(REB_PERCENT) | FLAGIT_64(REB_MONEY) | FLAGIT_64(REB_CHAR) \
	| FLAGIT_64(REB_PAIR) | FLAGIT_64(REB_TUPLE) | FLAGIT_64(REB_TIME) \
	| FLAGIT_64(REB_DATE) | FLAGIT_64(REB_TYPESET) | TS_WORD \
	| FLAGIT_64(REB_HANDLE))

#define TS_GC (~TS_NO_GC)

// Garbage collection marker function (GC Hook)
typedef void (*REBMRK)(void);

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
	AN_LIMIT,
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
	OPT_ENC_BIG_ENDIAN = 1 << 0, // little is default
	OPT_ENC_UTF8 = 1 << 1,
	OPT_ENC_UTF16 = 1 << 2,
	OPT_ENC_UTF32 = 1 << 3,
	OPT_ENC_BOM = 1 << 4, // byte order marker
	OPT_ENC_CRLF = 1 << 5, // CR line termination, see OPT_ENC_CRLF_MAYBE
	OPT_ENC_UNISRC = 1 << 6, // source is UCS2
	OPT_ENC_RAW = 1 << 7 // raw binary, no encoding
};

#if OS_CRLF
	#define OPT_ENC_CRLF_MAYBE OPT_ENC_CRLF
#else
	#define OPT_ENC_CRLF_MAYBE 0
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
**	Do_Next_May_Throw
**
**		This is a wrapper for the basic building block of Rebol
**		evaluation.  See Do_Next_Core() for its inner workings, but
**		it will return:
**
**			END_FLAG if end of series prohibited a full evaluation
**			THROWN_FLAG if the output is THROWN()--you MUST check!
**			...or the next index position for attempting evaluation
**
**		v-- !!! IMPORTANT !!!
**
**		The THROWN_FLAG means your value does not represent a directly
**		usable value, so you MUST check for it.  It signifies getting
**		back a THROWN()--see notes in sys-value.h about what that
**		means.  If you don't know how to handle it, then at least
**		you need to `raise Error_No_Catch_For_Throw()` on it.  If you *do*
**		handle it, be aware it's a throw label with OPT_VALUE_THROWN
**		set in its header, and shouldn't leak to the rest of the system.
**
**		Note that THROWN() is not an indicator of an error, rather
**		something that ordinary language constructs might meaningfully
**		want to process as they bubble up the stack (some examples
**		would be BREAK, CONTINUE, and even QUIT).  Errors are handled
**		with a different mechanism using longjmp().  So if an actual
**		error happened during the DO then there wouldn't even *BE* a
**		return value...because the function call would never return!
**		See PUSH_TRAP() and Error() for more information.
**
**	Do_Block_Throws
**
**		Do_Block_Throws behaves "as if" it is performing iterated
**		calls to Do_Next_May_Throw until the end of block is reached.
**		(Under the hood it is actually more efficient than doing so.)
**		It is named the way it is because it's expected to usually be
**		used in an 'if' statement.  It cues you into realizing
**		that it returns TRUE if a THROW interrupts this current
**		DO_BLOCK execution--not asking about a "THROWN" that happened
**		as part of a prior statement.
**
**		If it returns FALSE, then the DO completed successfully to
**		end of input without a throw...and the output contains the
**		last value evaluated in the block (empty blocks give UNSET!).
**		If it returns TRUE then it will be the THROWN() value.
**
**		NOTE: Because these macros use each of their arguments exactly
**		once in all builds, they have the same argument evaluation
**		as a function.  So they are named w/Leading_Caps_Underscores
**		to convey that they abide by this contract.
**
***********************************************************************/

#define Do_Next_May_Throw(out,series,index) \
	Do_Core((out), TRUE, (series), (index), TRUE)

#define Do_Block_Throws(out,series,index) \
	(THROWN_FLAG == Do_Core((out), FALSE, (series), (index), TRUE))


/***********************************************************************
**
**  ASSERTIONS
**
**		Assertions are in debug builds only, and use the conventional
**		standard C assert macro.  The code inside the assert will be
**		removed if the flag NDEBUG is defined to indicate "NoDEBUGging".
**		While negative logic is counter-intuitive (e.g. `#ifndef NDEBUG`
**		vs. `#ifdef DEBUG`) it's the standard and is the least of evils:
**
**			http://stackoverflow.com/a/17241278/211160
**
**		Assertions should mostly be used as a kind of "traffic cone"
**		when working on new code (or analyzing a bug you're trying to
**		trigger in development).  It's preferable to update the design
**		via static typing or otherwise as the code hardens.
**
***********************************************************************/

// Included via #include <assert.h> at top of file


/***********************************************************************
**
**	ERROR HANDLING
**
**		Rebol uses a C89-compatible trick to implement two "keywords"
**		for triggering errors, called `raise` and `panic`.  They look
**		like you are passing some kind of object to a reserved word:
**
**			if (Foo_Type(foo) == BAD_FOO) {
**				raise Error_Bad_Foo_Operation(...);
**
**				// this line will never be reached, because it
**				// longjmp'd up the stack where execution continues
**			}
**
**			if (Foo_Type(foo_critical) == BAD_FOO) {
**				panic Error_Bad_Foo_Operation(...);
**
**				// this line will never be reached, because it had
*				// a "panic" and exited the process with a message
**			}
**
**		In actuality, the Error_XXX() function is doing the work of
**		either ending the process or longjmp'ing.  But `raise` and
**		`panic` do some tricks to set the stage for which will happen,
**		with a little syntax cleverness to catch compile time problems.
**
**		It's possible to pass a Rebol ERROR! object by using the
**		form `panic Error_Is(err_value_ptr);`  But there are also macros
**		which allow you to create and parameterize a new error, such
**		as `raise Error_2(RE_SOME_ERR_NUM, arg1, arg2);`.  These
**		macros have an additional benefit in that they can be used
**		with panic even before the system has gotten to a boot state
**		far enough that error objects can even be created.
**
**		Errors are defined in %errors.r.  These definitions contain a
**		formatted message template, showing how the arguments will
**		be displayed when the error is printed.
**
***********************************************************************/

// The same Error_XXX(...) functions are able to be handled as either a
// panic or a raise "argument".  That's because the `panic` and `raise`
// "keywords" are actually tricks that store global or thread-local
// state variables that influence the error function behavior.  A
// variable of this enum type enforms the Error whether it is to act as
// a panic (via `Panic_Core()`) or a raise (via `Raise_Core()`)
//
enum Reb_Fail_Prep {
	FAIL_UNPREPARED,
	FAIL_PREP_PANIC,
	FAIL_PREP_RAISE
};

// The ternary ?: operator is used to implement `panic` and `raise`, and
// the preparing for the handling is done in the condition by a _PREP_
// macro.  Error triggering is done by the "argument", which winds up in
// the false branch position.
//
// The release build always triggers the error for both panic and raise.
// Debug builds trigger an assert at the callsite in the case of panics.
//
#ifdef NDEBUG
	// Do assignment, but stay a C89 "expression" and evaluate to FALSE
	#define RAISE_PREP_ALWAYS_FALSE(file,line) \
		((TG_Fail_Prep = FAIL_PREP_RAISE) != FAIL_PREP_RAISE)

	// Do assignment, but stay a C89 "expression" and evaluate to FALSE
	#define IF_PANIC_PREP_SHOULD_ASSERT(file,line) \
		((TG_Fail_Prep = FAIL_PREP_PANIC) != FAIL_PREP_PANIC)
#else
	// We want to do our assignments without short circuiting, and yet
	// still return FALSE.  The "1 +" keeps a line number of 0 from
	// stopping the chain before it can do the TG_Fail_Prep assignment.
	#define RAISE_PREP_ALWAYS_FALSE(file,line) \
		((TG_Fail_C_File = (file)) && (1 + (TG_Fail_C_Line = (line))) \
		&& ((TG_Fail_Prep = FAIL_PREP_RAISE) != FAIL_PREP_RAISE))

	// Same as above except this time we want to return TRUE at the end
	// in order to make the debug build trigger an assert at the panic
	#define IF_PANIC_PREP_SHOULD_ASSERT(file,line) \
		((TG_Fail_C_File = (file)) && (1 + (TG_Fail_C_Line = (line))) \
		&& ((TG_Fail_Prep = FAIL_PREP_RAISE) == FAIL_PREP_RAISE))

#endif

// The `panic` and `raise` macros are styled to end in a dangling `:` for
// the ternary operator (where the "argument" will be evaluated).  The
// argument must be NORETURN and thus must be void, which is also checked
// by the ternary operator (as both arguments must match).

#define panic \
	IF_PANIC_PREP_SHOULD_ASSERT(__FILE__, __LINE__) ? assert(FALSE) :

#define raise \
	RAISE_PREP_ALWAYS_FALSE(__FILE__, __LINE__) ? assert(FALSE) :

// If you have an already formed ERROR! REBVAL* itself, with any arguments
// already fulfilled, you can use this macro:
//
//		raise Error_Is(err);
//		panic Error_Is(err);
//
// Originally this was just called Error().  However, Ren/C++ has a class
// called Error, and there's no way to rename a macro "out of the way"
// for that collision in its implementation.
//
#define Error_Is(err) \
	((TG_Fail_Prep == FAIL_PREP_RAISE) \
		? Raise_Core(err) \
		: Panic_Core( \
			TG_Fail_Prep == FAIL_PREP_PANIC \
				? VAL_ERR_NUM(err) \
				: cast(REBCNT, RE_NO_PREP), \
			err, \
			TG_Fail_C_File, \
			TG_Fail_C_Line, \
			NULL))

// The `Error_Null()` variadic function lets you trigger an error (when used
// with raise or panic) where you specify an error ID# and its arguments as
// REBVAL*.  Unfortunately, variadic macros in ANSI-C are a bit clunky and
// can't check types or tell when the caller stopped supplying arguments.
// While naming it with _Null helps hint you need to supply a NULL, these
// numbered macro forms take care of it for you.  (Also: in debug builds
// ensures at compile time that you actually passed in REBVAL pointers.)
//
#ifdef NDEBUG
	#define Error_0(num) \
		Error_Null((num), NULL)

	#define Error_1(num,arg1) \
		Error_Null((num), (arg1), NULL)

	#define Error_2(num,arg1,arg2) \
		Error_Null((num), (arg1), (arg2), NULL)

	#define Error_3(num,arg1,arg2,arg3) \
		Error_Null((num), (arg1), (arg2), (arg3), NULL)
#else
	#define Error_0(num) \
		Error_0_Debug(num)

	#define Error_1(num,arg1) \
		Error_1_Debug((num), (arg1))

	#define Error_2(num,arg1,arg2) \
		Error_2_Debug((num), (arg1), (arg2))

	#define Error_3(num,arg1,arg2,arg3) \
		Error_3_Debug((num), (arg1), (arg2), (arg3))
#endif


/***********************************************************************
**
**	PANIC_SERIES
**
**		"Series Panics" will (hopefully) trigger an alert under memory
**		tools like address sanitizer and valgrind that indicate the
**		call stack at the moment of allocation of a series.  Then you
**		should have TWO stacks: the one at the call of the Panic, and
**		one where that series was alloc'd.
**
***********************************************************************/

#if !defined(NDEBUG)
	#define Panic_Series(s) \
		Panic_Series_Debug((s), __FILE__, __LINE__);
#else
	// Release builds do not pay for the `guard` trick, so they just crash.

	#define Panic_Series(s) panic Error_0(RE_MISC)
#endif


/***********************************************************************
**
**	SERIES MANAGED MEMORY
**
**		When a series is allocated by the Make_Series routine, it is
**		not initially seen by the garbage collector.  To keep from
**		leaking it, then it must be either freed with Free_Series or
**		delegated to the GC to manage with MANAGE_SERIES.
**
**		(In debug builds, there is a test at the end of every Rebol
**		function dispatch that checks to make sure one of those two
**		things happened for any series allocated during the call.)
**
**		The implementation of MANAGE_SERIES is shallow--it only sets
**		a bit on that *one* series, not the series referenced by
**		values inside of it.  This means that you cannot build a
**		hierarchical structure that isn't visible to the GC and
**		then do a single MANAGE_SERIES call on the root to hand it
**		over to the garbage collector.  While it would be technically
**		possible to deeply walk the structure, the efficiency gained
**		from pre-building the structure with the managed bit set is
**		significant...so that's how deep copies and the loader do it.
**
**		(In debug builds, if any unmanaged series are found inside
**		of values reachable by the GC, it will raise an alert.)
**
***********************************************************************/

#define MANAGE_SERIES(series) \
	Manage_Series(series)

#define ENSURE_SERIES_MANAGED(series) \
	(SERIES_GET_FLAG((series), SER_MANAGED) \
		? NOOP \
		: MANAGE_SERIES(series))

// Debug build includes testing that the managed state of the frame and
// its word series is the same for the "ensure" case.  It also adds a
// few assert macros.
//
#ifdef NDEBUG
	#define MANAGE_FRAME(frame) \
		(MANAGE_SERIES(frame), MANAGE_SERIES(FRM_KEYLIST(frame)))

	#define ENSURE_FRAME_MANAGED(frame) \
		(SERIES_GET_FLAG((frame), SER_MANAGED) \
			? NOOP \
			: MANAGE_FRAME(frame))

	#define MANUALS_LEAK_CHECK(manuals,label_str) \
		NOOP

	#define ASSERT_SERIES_MANAGED(series) \
		NOOP

	#define ASSERT_VALUE_MANAGED(value) \
		NOOP
#else
	#define MANAGE_FRAME(frame) \
		Manage_Frame_Debug(frame)

	#define ENSURE_FRAME_MANAGED(frame) \
		((SERIES_GET_FLAG((frame), SER_MANAGED) \
		&& SERIES_GET_FLAG(FRM_KEYLIST(frame), SER_MANAGED)) \
			? NOOP \
			: MANAGE_FRAME(frame))

	#define MANUALS_LEAK_CHECK(manuals,label_str) \
		Manuals_Leak_Check_Debug((manuals), (label_str))

	#define ASSERT_SERIES_MANAGED(series) \
		do { \
			if (!SERIES_GET_FLAG((series), SER_MANAGED)) \
				Panic_Series(series); \
		} while (0)

	#define ASSERT_VALUE_MANAGED(value) \
		Assert_Value_Managed_Debug(value)
#endif


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
#define BUF_COLLECT VAL_SERIES(TASK_BUF_COLLECT)
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


//
// GUARDING SERIES (OR VALUE CONTENTS) FROM GARBAGE COLLECTION
//
// The garbage collector can run anytime the evaluator runs.  So if a series
// has had MANAGE_SERIES run on it, the potential exists that any C pointers
// that are outstanding may "go bad" if the series wasn't reachable from
// the root set.  This is important to remember any time a pointer is held
// across a call that runs arbitrary user code.
//
// This simple stack approach allows pushing protection for a series, and
// then can release protection only for the last series pushed.  A parallel
// pair of macros exists for pushing and popping of guard status for values,
// to protect any series referred to by the value's contents.  (Note: This can
// only be used on values that do not live inside of series, because there is
// no way to guarantee a value in a series will keep its address besides
// guarding the series AND locking it from resizing.)
//
// The guard stack is not meant to accumulate, and must be cleared out before
// a command ends or a PUSH_TRAP/DROP_TRAP.
//

#define PUSH_GUARD_SERIES(s) \
	Guard_Series_Core(s)

#define DROP_GUARD_SERIES(s) \
	do { \
		GC_Series_Guard->tail--; \
		assert((s) == cast(REBSER **, GC_Series_Guard->data)[ \
			GC_Series_Guard->tail \
		]); \
	} while (0)

#ifdef NDEBUG
	#define ASSERT_NOT_IN_SERIES_DATA(p) NOOP
#else
	#define ASSERT_NOT_IN_SERIES_DATA(v) \
		Assert_Not_In_Series_Data_Debug(v)
#endif

#define PUSH_GUARD_VALUE(v) \
	Guard_Value_Core(v)

#define DROP_GUARD_VALUE(v) \
	do { \
		GC_Value_Guard->tail--; \
		assert((v) == cast(REBVAL **, GC_Value_Guard->data)[ \
			GC_Value_Guard->tail \
		]); \
	} while (0)


// Rebol doesn't want to crash in the event of a stack overflow, but would
// like to gracefully trap it and return the user to the console.  While it
// is possible for Rebol to set a limit to how deeply it allows function
// calls in the interpreter to recurse, there's no *portable* way to
// catch a stack overflow in the C code of the interpreter itself.
//
// Hence, by default Rebol will use a non-standard heuristic.  It looks
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
// routine to sprinkle calls to the C_STACK_OVERFLOWING macro somewhere
// in it.  The ideal answer is to make Rebol itself corral an interpreted
// script such that it can't cause the C code to stack overflow.  Lacking
// that ideal this technique could break, so build configurations should
// be able to turn it off if needed.
//
// In the meantime, C_STACK_OVERFLOWING is a macro which takes the
// address of some variable local to the currently executed function.
// Note that because the limit is noticed before the C stack has *actually*
// overflowed, you still have a bit of stack room to do the cleanup and
// raise an error trap.  (You need to take care of any unmanaged series
// allocations, etc).  So cleaning up that state should be doable without
// making deep function calls.

#ifdef OS_STACK_GROWS_UP
	#define C_STACK_OVERFLOWING(address_of_local_var) \
		(cast(REBUPT, address_of_local_var) >= Stack_Limit)
#else
	#define C_STACK_OVERFLOWING(address_of_local_var) \
		(cast(REBUPT, address_of_local_var) <= Stack_Limit)
#endif

#define STACK_BOUNDS (4*1024*1000) // note: need a better way to set it !!
// Also: made somewhat smaller than linker setting to allow trapping it


/***********************************************************************
**
**	BINDING CONVENIENCE MACROS
**
**		** WARNING ** -- Don't pass these routines something like a
**		singular REBVAL* (such as a REB_BLOCK) which you wish to have
**		bound.  You must pass its *contents* as an array...as the
**		deliberately-long-name implies!
**
**		So don't do this:
**
**			REBVAL *block = D_ARG(1);
**			REBVAL *something = D_ARG(2);
**			Bind_Values_Deep(block, frame);
**
**		What will happen is that the block will be treated as an
**		array of values and get incremented.  In the above case it
**		would reach to the next argument and bind it too (while
**		likely crashing at some point not too long after that).
**
**		Instead write:
**
**			Bind_Values_Deep(VAL_BLK_HEAD(block), frame);
**
**		That will pass the address of the first value element of
**		the block's contents.  You could use a later value element,
**		but note that the interface as written doesn't have a length
**		limit.  So although you can control where it starts, it will
**		keep binding until it hits a REB_END marker value.
**
***********************************************************************/

#define Bind_Values_Deep(values,frame) \
	Bind_Values_Core((values), (frame), BIND_DEEP)

#define Bind_Values_All_Deep(values,frame) \
	Bind_Values_Core((values), (frame), BIND_ALL | BIND_DEEP)

#define Bind_Values_Shallow(values, frame) \
	Bind_Values_Core((values), (frame), BIND_ONLY)

// Gave this a complex name to warn of its peculiarities.  Calling with
// just BIND_SET is shallow and tricky because the set words must occur
// before the uses (to be applied to bindings of those uses)!
//
#define Bind_Values_Set_Forward_Shallow(values, frame) \
	Bind_Values_Core((values), (frame), BIND_SET)

#define Unbind_Values_Deep(values) \
	Unbind_Values_Core((values), NULL, TRUE)


/***********************************************************************
**
**	Legacy Modes Checking
**
**		Ren/C wants to try out new things that will likely be included
**		it the official Rebol3 release.  But it also wants transitioning
**		to be feasible from Rebol2 and R3-Alpha, without paying that
**		much to check for "old" modes if they're not being used.  So
**		system/options contains flags used for enabling specific
**		features relied upon by old code.
**
**		In order to keep these easements from adding to the measured
**		performance cost in the system (and to keep them from being
**		used for anything besides porting), they are only supported in
**		debug builds.
**
***********************************************************************/

#if !defined(NDEBUG)
	#define LEGACY(option) ( \
		(PG_Boot_Phase >= BOOT_ERRORS) \
		&& IS_CONDITIONAL_TRUE(Get_System(SYS_OPTIONS, (option))) \
	)
#endif


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
